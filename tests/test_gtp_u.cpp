#include "../src/lte/gtp_u.h"
#include "../src/lte/s1ap_link.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <chrono>

int main() {
    using namespace rbs::lte;

    // ── Basic encode/decode round-trip ────────────────────────────────────────
    // TS 29.060 §6.1
    const uint32_t teid1     = 0x12345678u;
    rbs::ByteBuffer payload1 = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};

    rbs::ByteBuffer frame = gtpuEncode(teid1, payload1);
    // Frame must be GTPU_HEADER_SIZE + payload
    assert(frame.size() == GTPU_HEADER_SIZE + payload1.size());
    // Verify header bytes per spec
    assert(frame[0] == GTPU_FLAGS_MINIMAL);   // 0x30
    assert(frame[1] == GTPU_MSG_TPDU);        // 0xFF
    uint16_t declLen = static_cast<uint16_t>((frame[2] << 8) | frame[3]);
    assert(declLen == static_cast<uint16_t>(payload1.size()));
    uint32_t teidFromFrame = (static_cast<uint32_t>(frame[4]) << 24)
                           | (static_cast<uint32_t>(frame[5]) << 16)
                           | (static_cast<uint32_t>(frame[6]) << 8)
                           |  static_cast<uint32_t>(frame[7]);
    assert(teidFromFrame == teid1);

    uint32_t        decTeid{};
    rbs::ByteBuffer decPayload;
    assert(gtpuDecode(frame, decTeid, decPayload));
    assert(decTeid == teid1);
    assert(decPayload == payload1);

    // ── Zero-length payload ───────────────────────────────────────────────────
    rbs::ByteBuffer emptyPayload;
    rbs::ByteBuffer emptyFrame = gtpuEncode(0xFFFFFFFFu, emptyPayload);
    assert(emptyFrame.size() == GTPU_HEADER_SIZE);
    uint32_t        decTeid2{};
    rbs::ByteBuffer decPayload2;
    assert(gtpuDecode(emptyFrame, decTeid2, decPayload2));
    assert(decTeid2 == 0xFFFFFFFFu);
    assert(decPayload2.empty());

    // ── Large payload (1500 bytes / Ethernet MTU) ─────────────────────────────
    rbs::ByteBuffer largePl(1500, 0xAB);
    rbs::ByteBuffer largeFrame = gtpuEncode(1u, largePl);
    assert(largeFrame.size() == GTPU_HEADER_SIZE + 1500);
    uint32_t        decTeid3{};
    rbs::ByteBuffer decPayload3;
    assert(gtpuDecode(largeFrame, decTeid3, decPayload3));
    assert(decPayload3 == largePl);

    // ── Reject: too short ─────────────────────────────────────────────────────
    rbs::ByteBuffer shortBuf(4, 0x00);
    uint32_t dummyTeid{};
    rbs::ByteBuffer dummyPl;
    assert(!gtpuDecode(shortBuf, dummyTeid, dummyPl));

    // ── Reject: wrong flags ───────────────────────────────────────────────────
    rbs::ByteBuffer badFlags = gtpuEncode(1u, {0x01});
    badFlags[0] = 0x20;   // version=1,PT=0 (signalling) — not T-PDU
    assert(!gtpuDecode(badFlags, dummyTeid, dummyPl));

    // ── Reject: wrong msgType ─────────────────────────────────────────────────
    rbs::ByteBuffer badMsg = gtpuEncode(1u, {0x01});
    badMsg[1] = 0x01;   // Echo Request, not T-PDU
    assert(!gtpuDecode(badMsg, dummyTeid, dummyPl));

    // ── S1ULink loopback: eNB ↔ simulated SGW over UDP on 127.0.0.1 ──────────
    // Tests full UDP forwarding: sendGtpuPdu → UDP → onRxPacket → recvGtpuPdu
    // Uses high ephemeral ports to avoid conflicts. TS 29.060 §4.
    {
        rbs::net::UdpSocket::wsaInit();

        constexpr uint16_t portEnb = 43500;
        constexpr uint16_t portSgw = 43501;
        constexpr uint32_t teid    = 0xABCD1234u;

        rbs::lte::S1ULink enb("test-enb", portEnb);
        rbs::lte::S1ULink sgw("test-sgw", portSgw);

        // 127.0.0.1 in network byte order (big-endian uint32)
        uint32_t loopback;
        ::inet_pton(AF_INET, "127.0.0.1", &loopback);

        // Both sides share the same TEID (simulation simplification):
        // eNB sends UL with teid in header → SGW routes by teid to rnti=1,erabId=1.
        // SGW sends DL with teid in header → eNB routes by teid to rnti=1,erabId=1.
        rbs::lte::GTPUTunnel enbTunnel{teid, loopback, portSgw};
        rbs::lte::GTPUTunnel sgwTunnel{teid, loopback, portEnb};

        assert(enb.createTunnel(1, 1, enbTunnel));
        assert(sgw.createTunnel(1, 1, sgwTunnel));

        // UL: eNB → SGW
        const rbs::ByteBuffer ulPkt = {0x45, 0x00, 0x00, 0x14, 0x00, 0x01,
                                       0x40, 0x00, 0x40, 0x11, 0x00, 0x00};
        assert(enb.sendGtpuPdu(1, 1, ulPkt));

        rbs::ByteBuffer rcvUl;
        bool gotUl = false;
        for (int i = 0; i < 50 && !gotUl; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            gotUl = sgw.recvGtpuPdu(1, 1, rcvUl);
        }
        assert(gotUl);
        assert(rcvUl == ulPkt);

        // DL: SGW → eNB
        const rbs::ByteBuffer dlPkt = {0x60, 0x00, 0x00, 0x00, 0x00, 0x08,
                                       0x11, 0x40, 0x00, 0x00, 0x00, 0x00};
        assert(sgw.sendGtpuPdu(1, 1, dlPkt));

        rbs::ByteBuffer rcvDl;
        bool gotDl = false;
        for (int i = 0; i < 50 && !gotDl; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            gotDl = enb.recvGtpuPdu(1, 1, rcvDl);
        }
        assert(gotDl);
        assert(rcvDl == dlPkt);

        enb.deleteTunnel(1, 1);
        sgw.deleteTunnel(1, 1);

        rbs::net::UdpSocket::wsaCleanup();
    }

    std::puts("test_gtp_u PASSED");
    return 0;
}

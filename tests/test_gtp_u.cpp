#include "../src/lte/gtp_u.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

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

    std::puts("test_gtp_u PASSED");
    return 0;
}

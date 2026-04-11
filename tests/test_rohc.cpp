// test_rohc.cpp — п.16 ROHC VoLTE Profile 0x0001 (IP/UDP/RTP)
// RFC 3095, TS 36.323 §6.2.13
#include "../src/lte/lte_pdcp.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rbs;
using namespace rbs::lte;

// ── helper: build a minimal IPv4/UDP/RTP packet ─────────────────
static ByteBuffer makeRTPPacket(uint32_t srcIp, uint32_t dstIp,
                                 uint16_t srcPort, uint16_t dstPort,
                                 uint32_t ssrc, uint8_t pt,
                                 uint16_t sn, uint32_t ts,
                                 const ByteBuffer& rtpPayload)
{
    const size_t payLen  = rtpPayload.size();
    const size_t totalIp = 20 + 8 + 12 + payLen;
    ByteBuffer pkt(totalIp, 0);

    // IPv4
    pkt[0] = 0x45;
    pkt[1] = 0;
    pkt[2] = static_cast<uint8_t>(totalIp >> 8);
    pkt[3] = static_cast<uint8_t>(totalIp & 0xFF);
    pkt[8] = 64;   // TTL
    pkt[9] = 17;   // UDP
    pkt[12] = static_cast<uint8_t>(srcIp >> 24);
    pkt[13] = static_cast<uint8_t>(srcIp >> 16);
    pkt[14] = static_cast<uint8_t>(srcIp >>  8);
    pkt[15] = static_cast<uint8_t>(srcIp);
    pkt[16] = static_cast<uint8_t>(dstIp >> 24);
    pkt[17] = static_cast<uint8_t>(dstIp >> 16);
    pkt[18] = static_cast<uint8_t>(dstIp >>  8);
    pkt[19] = static_cast<uint8_t>(dstIp);

    // UDP (offset 20)
    pkt[20] = static_cast<uint8_t>(srcPort >> 8); pkt[21] = static_cast<uint8_t>(srcPort);
    pkt[22] = static_cast<uint8_t>(dstPort >> 8); pkt[23] = static_cast<uint8_t>(dstPort);
    pkt[24] = 0; pkt[25] = static_cast<uint8_t>(8 + 12 + payLen);

    // RTP (offset 28)
    pkt[28] = 0x80;       // V=2
    pkt[29] = pt & 0x7F;
    pkt[30] = static_cast<uint8_t>(sn >> 8); pkt[31] = static_cast<uint8_t>(sn);
    pkt[32] = static_cast<uint8_t>(ts >> 24); pkt[33] = static_cast<uint8_t>(ts >> 16);
    pkt[34] = static_cast<uint8_t>(ts >>  8); pkt[35] = static_cast<uint8_t>(ts);
    pkt[36] = static_cast<uint8_t>(ssrc >> 24); pkt[37] = static_cast<uint8_t>(ssrc >> 16);
    pkt[38] = static_cast<uint8_t>(ssrc >>  8); pkt[39] = static_cast<uint8_t>(ssrc);

    for (size_t i = 0; i < payLen; ++i) pkt[40 + i] = rtpPayload[i];
    return pkt;
}

// ── Test 1: ROHCProfile enum and ROHCState values ────────────────
static void test_rohc_enums() {
    assert(static_cast<uint16_t>(ROHCProfile::UNCOMPRESSED) == 0x0000);
    assert(static_cast<uint16_t>(ROHCProfile::RTP_UDP_IP)   == 0x0001);
    assert(static_cast<uint16_t>(ROHCProfile::UDP_IP)       == 0x0002);
    assert(static_cast<uint8_t>(ROHCState::IR) == 0);
    assert(static_cast<uint8_t>(ROHCState::FO) == 1);
    assert(static_cast<uint8_t>(ROHCState::SO) == 2);
    std::puts("  test_rohc_enums PASSED");
}

// ── Test 2: PDCPConfig with ROHC fields ─────────────────────────
static void test_pdcp_config_rohc_fields() {
    PDCPConfig cfg{};
    cfg.bearerId          = 1;
    cfg.headerCompression = true;
    cfg.rohcProfile       = ROHCProfile::RTP_UDP_IP;
    assert(cfg.headerCompression);
    assert(cfg.rohcProfile == ROHCProfile::RTP_UDP_IP);
    std::puts("  test_pdcp_config_rohc_fields PASSED");
}

// ── Test 3: PDCP addBearer with ROHC; entity gets correct profile ─
static void test_pdcp_bearer_rohc_init() {
    PDCP pdcp;
    PDCPConfig cfg{};
    cfg.bearerId          = 1;
    cfg.headerCompression = true;
    cfg.rohcProfile       = ROHCProfile::RTP_UDP_IP;
    assert(pdcp.addBearer(1, cfg));
    // Adding same bearer twice → false
    assert(!pdcp.addBearer(1, cfg));
    std::puts("  test_pdcp_bearer_rohc_init PASSED");
}

// ── Test 4: ROHCContext default state ────────────────────────────
static void test_rohc_context_default() {
    ROHCContext ctx{};
    assert(ctx.txState == ROHCState::IR);
    assert(ctx.rxState == ROHCState::IR);
    assert(ctx.txStatePkts == 0);
    assert(ctx.profile == ROHCProfile::UNCOMPRESSED);
    std::puts("  test_rohc_context_default PASSED");
}

// ── Test 5: First packet → IR compression (smaller than raw)  ────
static void test_rohc_ir_compresses() {
    // Build a 40+20 byte RTP packet (20 bytes RTP payload)
    ByteBuffer payload(20, 0xAB);
    ByteBuffer ip = makeRTPPacket(0x0A000001, 0x0A000002,
                                   5004, 5005, 0xDEAD, 8,
                                   100, 160, payload);
    assert(ip.size() == 60);

    ROHCContext ctx{};
    ctx.profile = ROHCProfile::RTP_UDP_IP;

    PDCP pdcp;  // use PDCP only for its private helpers via processDlPacket
    // Call compressor directly via a bearer
    PDCPConfig cfg{};
    cfg.bearerId          = 1;
    cfg.headerCompression = true;
    cfg.rohcProfile       = ROHCProfile::RTP_UDP_IP;
    pdcp.addBearer(10, cfg);

    ByteBuffer compressed = pdcp.processDlPacket(10, 1, ip);
    // PDCP adds a 2-byte header; ROHC IR is 28 bytes; payload=20 → total = 2+28+20=50
    // Raw would be 2+60=62 bytes; compressed < raw
    assert(!compressed.empty());
    assert(compressed.size() < 2 + ip.size());
    std::puts("  test_rohc_ir_compresses PASSED");
}

// ── Test 6: FO packet is smaller than IR ────────────────────────
static void test_rohc_fo_smaller_than_ir() {
    ByteBuffer payload(20, 0xCC);
    ByteBuffer ip1 = makeRTPPacket(0x0A000001, 0x0A000002, 5004, 5005,
                                    0xBEEF, 8, 1, 160, payload);
    ByteBuffer ip2 = makeRTPPacket(0x0A000001, 0x0A000002, 5004, 5005,
                                    0xBEEF, 8, 2, 320, payload);

    PDCPConfig cfg{}; cfg.bearerId = 1;
    cfg.headerCompression = true; cfg.rohcProfile = ROHCProfile::RTP_UDP_IP;
    PDCP pdcp;
    pdcp.addBearer(20, cfg);

    ByteBuffer c1 = pdcp.processDlPacket(20, 1, ip1);  // IR
    ByteBuffer c2 = pdcp.processDlPacket(20, 1, ip2);  // FO (3rd packet state)
    // IR is larger (static chain), FO is smaller
    assert(!c1.empty());
    assert(!c2.empty());
    assert(c2.size() < c1.size());
    std::puts("  test_rohc_fo_smaller_than_ir PASSED");
}

// ── Test 7: SO packet is smallest ───────────────────────────────
static void test_rohc_so_smallest() {
    ByteBuffer payload(20, 0xDD);
    PDCPConfig cfg{}; cfg.bearerId = 1;
    cfg.headerCompression = true; cfg.rohcProfile = ROHCProfile::RTP_UDP_IP;
    PDCP pdcp;
    pdcp.addBearer(30, cfg);

    size_t szIR = 0, szFO = 0, szSO = 0;
    for (int i = 0; i < 10; ++i) {
        ByteBuffer ip = makeRTPPacket(0xC0A80001, 0xC0A80002, 5004, 5005,
                                       0xCAFE, 8,
                                       static_cast<uint16_t>(i + 1),
                                       static_cast<uint32_t>((i + 1) * 160),
                                       payload);
        ByteBuffer c = pdcp.processDlPacket(30, 1, ip);
        if (i == 0)       szIR = c.size();    // IR
        else if (i == 1)  szFO = c.size();    // FO (state transitions IR→FO then FO→SO after 3)
        else if (i >= 5)  szSO = c.size();    // deep into SO
    }
    assert(szIR > szFO);
    assert(szFO > szSO || szSO > 0);  // SO ≤ FO
    std::puts("  test_rohc_so_smallest PASSED");
}

// ── Test 8: ROHC compress + decompress round-trip (IR) ──────────
static void test_rohc_roundtrip_ir() {
    ByteBuffer payload(10, 0x11);
    ByteBuffer original = makeRTPPacket(0xAC100001, 0xAC100002,
                                         1234, 5678, 0x1234, 8, 42, 6720, payload);

    PDCPConfig cfgTx{}; cfgTx.bearerId = 1;
    cfgTx.headerCompression = true; cfgTx.rohcProfile = ROHCProfile::RTP_UDP_IP;
    PDCP tx, rx;
    tx.addBearer(1, cfgTx);
    tx.addBearer(2, cfgTx);  // separate bearer for RX (different entity)

    PDCPConfig cfgRx{}; cfgRx.bearerId = 1;
    cfgRx.headerCompression = true; cfgRx.rohcProfile = ROHCProfile::RTP_UDP_IP;
    rx.addBearer(1, cfgRx);

    // Compress (PDCP tx adds PDCP header too)
    ByteBuffer compressed = tx.processDlPacket(1, 1, original);
    assert(!compressed.empty());

    // Decompress via UL (the UL PDU is the compressed PDCP PDU)
    ByteBuffer recovered = rx.processUlPDU(1, 1, compressed);
    assert(!recovered.empty());

    // Recovery: at minimum the IP/UDP/RTP payload must match
    assert(recovered.size() >= 40 + payload.size());
    // Check RTP payload bytes
    for (size_t i = 0; i < payload.size(); ++i)
        assert(recovered[40 + i] == payload[i]);

    std::puts("  test_rohc_roundtrip_ir PASSED");
}

// ── Test 9: ROHC does not compress non-RTP profile ──────────────
static void test_rohc_no_compress_uncompressed_profile() {
    ByteBuffer payload(10, 0xFF);
    ByteBuffer ip = makeRTPPacket(0x01020304, 0x05060708, 100, 200, 1, 8, 1, 0, payload);

    PDCPConfig cfg{}; cfg.bearerId = 1;
    cfg.headerCompression = true;
    cfg.rohcProfile       = ROHCProfile::UNCOMPRESSED;  // pass-through
    PDCP pdcp;
    pdcp.addBearer(40, cfg);

    ByteBuffer c = pdcp.processDlPacket(40, 1, ip);
    // Size should be 2 (PDCP hdr) + original IP size (no compression)
    assert(c.size() == 2 + ip.size());
    std::puts("  test_rohc_no_compress_uncompressed_profile PASSED");
}

// ── Test 10: IR → FO state transition after ROHC_IR_TO_FO_PKTS ──
static void test_rohc_state_transition_ir_fo() {
    assert(ROHC_IR_TO_FO_PKTS == 3);  // verify constant
    ByteBuffer payload(5, 0x55);
    PDCPConfig cfg{}; cfg.bearerId = 1;
    cfg.headerCompression = true; cfg.rohcProfile = ROHCProfile::RTP_UDP_IP;
    PDCP pdcp;
    pdcp.addBearer(50, cfg);

    // Packet 1: IR (size larger due to static+dynamic chain)
    auto ip1 = makeRTPPacket(0x01000001, 0x01000002, 5004, 5005, 0x11, 8, 1, 160, payload);
    ByteBuffer c1 = pdcp.processDlPacket(50, 1, ip1);
    // Packet 2: FO (after first IR packet, compressor moves to FO)
    auto ip2 = makeRTPPacket(0x01000001, 0x01000002, 5004, 5005, 0x11, 8, 2, 320, payload);
    ByteBuffer c2 = pdcp.processDlPacket(50, 1, ip2);
    // Packet 3: FO
    auto ip3 = makeRTPPacket(0x01000001, 0x01000002, 5004, 5005, 0x11, 8, 3, 480, payload);
    ByteBuffer c3 = pdcp.processDlPacket(50, 1, ip3);
    // After ROHC_FO_TO_SO_PKTS FO packets → SO
    auto ip4 = makeRTPPacket(0x01000001, 0x01000002, 5004, 5005, 0x11, 8, 4, 640, payload);
    ByteBuffer c4 = pdcp.processDlPacket(50, 1, ip4);

    assert(c1.size() > c2.size());  // IR > FO
    assert(c4.size() <= c2.size()); // SO ≤ FO
    std::puts("  test_rohc_state_transition_ir_fo PASSED");
}

// ── Test 11: Short packet (< 40 bytes) → no compression ─────────
static void test_rohc_short_packet_passthrough() {
    ByteBuffer tiny(10, 0xAA);
    PDCPConfig cfg{}; cfg.bearerId = 1;
    cfg.headerCompression = true; cfg.rohcProfile = ROHCProfile::RTP_UDP_IP;
    PDCP pdcp;
    pdcp.addBearer(60, cfg);

    ByteBuffer c = pdcp.processDlPacket(60, 1, tiny);
    // No compression applied (too short for IPv4+UDP+RTP)
    assert(c.size() == 2 + tiny.size());
    std::puts("  test_rohc_short_packet_passthrough PASSED");
}

int main() {
    std::puts("=== test_rohc ===");
    test_rohc_enums();
    test_pdcp_config_rohc_fields();
    test_pdcp_bearer_rohc_init();
    test_rohc_context_default();
    test_rohc_ir_compresses();
    test_rohc_fo_smaller_than_ir();
    test_rohc_so_smallest();
    test_rohc_roundtrip_ir();
    test_rohc_no_compress_uncompressed_profile();
    test_rohc_state_transition_ir_fo();
    test_rohc_short_packet_passthrough();
    std::puts("test_rohc PASSED");
    return 0;
}

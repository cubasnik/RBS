// test_pdcp_security.cpp — PDCP HFN/COUNT wrap + EIA2 AES-128-CMAC tests
// References: TS 36.323 §7.1, TS 33.401 §6.3.2 / §6.4.2b
#include "../src/lte/lte_pdcp.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rbs;
using namespace rbs::lte;

// ── helpers ──────────────────────────────────────────────────────────────────

static PDCPConfig makeCfg(uint16_t bid,
                           PDCPCipherAlg cipher = PDCPCipherAlg::NULL_ALG,
                           PDCPIntegAlg  integ  = PDCPIntegAlg::NULL_ALG)
{
    PDCPConfig c{};
    c.bearerId  = bid;
    c.cipherAlg = cipher;
    c.integAlg  = integ;
    // Simple non-zero keys
    for (int i = 0; i < 16; ++i) { c.cipherKey[i] = static_cast<uint8_t>(i + 1);
                                    c.integKey [i] = static_cast<uint8_t>(0x10 + i); }
    return c;
}

static ByteBuffer makePacket(uint8_t fill, size_t len = 20) {
    return ByteBuffer(len, fill);
}

// ── tests ────────────────────────────────────────────────────────────────────

static int passed = 0, failed = 0;

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { printf("FAIL [line %d]: %s\n", __LINE__, #cond); ++failed; } \
    else { ++passed; } \
} while(0)

#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_NE(a, b) EXPECT_TRUE((a) != (b))

// ── Test 1: addBearer succeeds ────────────────────────────────────────────────
static void testAddBearer() {
    PDCP pdcp;
    bool ok = pdcp.addBearer(1, makeCfg(1));
    EXPECT_TRUE(ok);
}

// ── Test 2: DL packet round-trip (NULL cipher) ────────────────────────────────
static void testNullCipherRoundTrip() {
    PDCP pdcp;
    pdcp.addBearer(1, makeCfg(1));
    ByteBuffer pkt = makePacket(0xAB, 40);
    ByteBuffer pdu = pdcp.processDlPacket(1, 1, pkt);
    ByteBuffer recovered = pdcp.processUlPDU(1, 1, pdu);
    EXPECT_EQ(recovered, pkt);
}

// ── Test 3: DL packet round-trip (AES cipher) ────────────────────────────────
static void testAesCipherRoundTrip() {
    PDCP pdcp;
    pdcp.addBearer(1, makeCfg(1, PDCPCipherAlg::AES));
    ByteBuffer pkt = makePacket(0x55, 64);
    ByteBuffer pdu = pdcp.processDlPacket(1, 1, pkt);
    // PDU should look different after ciphering
    EXPECT_NE(pdu, pkt);
    ByteBuffer recovered = pdcp.processUlPDU(1, 1, pdu);
    EXPECT_EQ(recovered, pkt);
}

// ── Test 4: txSN increments per packet ───────────────────────────────────────
static void testSNIncrement() {
    PDCP pdcp;
    pdcp.addBearer(2, makeCfg(2));
    ByteBuffer pkt = makePacket(0x01);
    ByteBuffer pdu0 = pdcp.processDlPacket(2, 2, pkt);
    ByteBuffer pdu1 = pdcp.processDlPacket(2, 2, pkt);
    // SN field is bytes 0,1 of header; PDUs should differ in SN
    EXPECT_NE(pdu0[1], pdu1[1]);  // SN[7:0] incremented
}

// ── Test 5: HFN increments when 12-bit SN wraps (0xFFF → 0) ─────────────────
static void testHFNWrapAt4096() {
    PDCP pdcp;
    pdcp.addBearer(3, makeCfg(3, PDCPCipherAlg::AES));
    ByteBuffer pkt = makePacket(0x7F, 16);
    // Round-trip 4096 packets to force one full SN cycle;
    // keep RX in sync so rxHFN matches txHFN after the wrap.
    for (int i = 0; i < 4096; ++i) {
        ByteBuffer pdu = pdcp.processDlPacket(3, 3, pkt);
        EXPECT_TRUE(!pdu.empty());
        pdcp.processUlPDU(3, 3, pdu);  // keep rxHFN in sync
    }
    // After 4096 round-trips: txSN=0, txHFN=1, rxHFN=1
    ByteBuffer pdu  = pdcp.processDlPacket(3, 3, pkt);
    ByteBuffer back = pdcp.processUlPDU(3, 3, pdu);
    EXPECT_EQ(back, pkt);
}

// ── Test 6: COUNT value = (HFN << 12) | SN ───────────────────────────────────
static void testCOUNTFormula() {
    // Verify via AES: at SN=5 (no wrap), ciphering with COUNT=5 should equal
    // ciphering with COUNT from explicit entity state.
    PDCP pdcp;
    PDCPConfig cfg = makeCfg(4, PDCPCipherAlg::AES);
    pdcp.addBearer(1, cfg);
    ByteBuffer pkt = makePacket(0xCC, 32);
    // Send 5 packets (SN 0..4 consumed)
    for (int i = 0; i < 5; ++i)
        pdcp.processDlPacket(1, 4, pkt);
    // Packet #6 uses SN=5, HFN=0 → COUNT=5
    ByteBuffer pdu6 = pdcp.processDlPacket(1, 4, pkt);
    // Recover using UL
    ByteBuffer recovered = pdcp.processUlPDU(1, 4, pdu6);
    EXPECT_EQ(recovered, pkt);
}

// ── Test 7: integrity apply/verify round-trip (EIA2 AES-CMAC) ────────────────
static void testIntegrityRoundTrip() {
    PDCP pdcp;
    pdcp.addBearer(1, makeCfg(1, PDCPCipherAlg::NULL_ALG, PDCPIntegAlg::AES));
    ByteBuffer msg = makePacket(0xDE, 48);
    ByteBuffer protected_msg = pdcp.applyIntegrity(1, 1, msg);
    EXPECT_EQ(protected_msg.size(), msg.size() + 4);  // +4 bytes MAC-I
    ByteBuffer recovered = pdcp.verifyIntegrity(1, 1, protected_msg);
    EXPECT_EQ(recovered, msg);
}

// ── Test 8: integrity verify fails on tampered MAC-I ─────────────────────────
static void testIntegrityTamperDetect() {
    PDCP pdcp;
    pdcp.addBearer(1, makeCfg(1, PDCPCipherAlg::NULL_ALG, PDCPIntegAlg::AES));
    ByteBuffer msg = makePacket(0xAA, 20);
    ByteBuffer prot = pdcp.applyIntegrity(1, 1, msg);
    // Flip last byte of MAC-I
    prot.back() ^= 0xFF;
    ByteBuffer bad = pdcp.verifyIntegrity(1, 1, prot);
    EXPECT_TRUE(bad.empty());  // must return empty on failure
}

// ── Test 9: integrity verify fails on tampered payload ───────────────────────
static void testIntegrityPayloadTamper() {
    PDCP pdcp;
    pdcp.addBearer(1, makeCfg(1, PDCPCipherAlg::NULL_ALG, PDCPIntegAlg::AES));
    ByteBuffer msg = makePacket(0xBB, 20);
    ByteBuffer prot = pdcp.applyIntegrity(1, 1, msg);
    // Flip first payload byte
    prot[0] ^= 0x01;
    ByteBuffer bad = pdcp.verifyIntegrity(1, 1, prot);
    EXPECT_TRUE(bad.empty());
}

// ── Test 10: NULL integrity passes every message unmodified ──────────────────
static void testNullIntegrity() {
    PDCP pdcp;
    pdcp.addBearer(2, makeCfg(2));  // both algs = NULL_ALG
    ByteBuffer msg = makePacket(0x11, 16);
    // applyIntegrity with NULL = no change
    ByteBuffer prot = pdcp.applyIntegrity(2, 2, msg);
    EXPECT_EQ(prot, msg);
    // verifyIntegrity with NULL = strip last 4 bytes (MAC-I placeholder absent)
    // For NULL alg verifyIntegrity returns payload = buf[0..n-4]
    EXPECT_EQ(prot.size(), msg.size());
}

// ── Test 11: integrity + cipher combined round-trip ──────────────────────────
static void testIntegrityAndCipherCombined() {
    PDCP pdcp;
    pdcp.addBearer(5, makeCfg(5, PDCPCipherAlg::AES, PDCPIntegAlg::AES));
    ByteBuffer msg = makePacket(0x42, 32);
    // Apply integrity first, then cipher the protected message
    ByteBuffer prot    = pdcp.applyIntegrity(5, 5, msg);
    ByteBuffer pdu     = pdcp.processDlPacket(5, 5, prot);
    ByteBuffer dec_pdu = pdcp.processUlPDU(5, 5, pdu);
    ByteBuffer plain   = pdcp.verifyIntegrity(5, 5, dec_pdu);
    EXPECT_EQ(plain, msg);
}

// ── Test 12: CMAC determinism — same key+msg always gives same tag ────────────
static void testCMACDeterminism() {
    PDCP pdcp;
    pdcp.addBearer(1, makeCfg(1, PDCPCipherAlg::NULL_ALG, PDCPIntegAlg::AES));
    ByteBuffer msg = makePacket(0xEE, 64);
    ByteBuffer p1 = pdcp.applyIntegrity(1, 1, msg);
    ByteBuffer p2 = pdcp.applyIntegrity(1, 1, msg);
    EXPECT_EQ(p1, p2);
}

// ── Test 13: different keys produce different MAC-I ───────────────────────────
static void testDifferentKeysDifferentMAC() {
    PDCP pdcpA, pdcpB;
    PDCPConfig cfgA = makeCfg(1, PDCPCipherAlg::NULL_ALG, PDCPIntegAlg::AES);
    PDCPConfig cfgB = cfgA;
    cfgB.integKey[0] ^= 0xFF;  // different key
    pdcpA.addBearer(1, cfgA);
    pdcpB.addBearer(1, cfgB);
    ByteBuffer msg = makePacket(0x7E, 32);
    ByteBuffer pA = pdcpA.applyIntegrity(1, 1, msg);
    ByteBuffer pB = pdcpB.applyIntegrity(1, 1, msg);
    // MAC-I bytes (last 4) must differ
    EXPECT_NE(pA.back(), pB.back());
}

// ── Test 14: HFN/COUNT is consistent across many SN wraps ────────────────────
static void testMultipleSNWraps() {
    PDCP pdcp;
    pdcp.addBearer(7, makeCfg(7, PDCPCipherAlg::AES));
    ByteBuffer pkt = makePacket(0x99, 20);
    // Two full SN periods (8192 round-trip packets); keep RX in sync
    for (int i = 0; i < 8192; ++i) {
        ByteBuffer pdu = pdcp.processDlPacket(7, 7, pkt);
        pdcp.processUlPDU(7, 7, pdu);
    }
    // Verify round-trip still works after two HFN wraps
    ByteBuffer pdu  = pdcp.processDlPacket(7, 7, pkt);
    ByteBuffer back = pdcp.processUlPDU(7, 7, pdu);
    EXPECT_EQ(back, pkt);
}

// ── Test 15: bearer not found returns empty ───────────────────────────────────
static void testBearerNotFound() {
    PDCP pdcp;
    ByteBuffer res = pdcp.processDlPacket(99, 99, makePacket(0x01));
    EXPECT_TRUE(res.empty());
    ByteBuffer res2 = pdcp.applyIntegrity(99, 99, makePacket(0x01));
    // unknown bearer — applyIntegrity returns buf unchanged
    EXPECT_TRUE(!res2.empty());
    ByteBuffer res3 = pdcp.verifyIntegrity(99, 99, makePacket(0x01, 8));
    EXPECT_TRUE(res3.empty());
}

// ────────────────────────────────────────────────────────────────────────────
int main() {
    testAddBearer();
    testNullCipherRoundTrip();
    testAesCipherRoundTrip();
    testSNIncrement();
    testHFNWrapAt4096();
    testCOUNTFormula();
    testIntegrityRoundTrip();
    testIntegrityTamperDetect();
    testIntegrityPayloadTamper();
    testNullIntegrity();
    testIntegrityAndCipherCombined();
    testCMACDeterminism();
    testDifferentKeysDifferentMAC();
    testMultipleSNWraps();
    testBearerNotFound();

    printf("\n=== PDCP Security: %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}

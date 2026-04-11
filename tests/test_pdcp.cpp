#include "../src/lte/lte_pdcp.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    rbs::lte::PDCP pdcp;

    rbs::lte::PDCPConfig cfg{};
    cfg.bearerId           = 1;
    cfg.cipherAlg          = rbs::lte::PDCPCipherAlg::NULL_ALG;
    cfg.headerCompression  = false;

    const rbs::RNTI rnti = 42;
    assert(pdcp.addBearer(rnti, cfg));

    // DL: IP packet → PDCP PDU
    rbs::ByteBuffer ipPkt(64, 0xAB);
    rbs::ByteBuffer pdu = pdcp.processDlPacket(rnti, 1, ipPkt);

    // PDU must be larger by 2 (PDCP header)
    assert(pdu.size() == ipPkt.size() + 2);
    // D/C bit must be set in first byte
    assert((pdu[0] & 0x80) == 0x80);

    // UL: PDCP PDU → IP packet (loopback via a new bearer)
    rbs::lte::PDCPConfig cfg2 = cfg;
    cfg2.bearerId = 2;
    const rbs::RNTI rnti2 = 43;
    assert(pdcp.addBearer(rnti2, cfg2));

    rbs::ByteBuffer ulPdu = pdcp.processDlPacket(rnti2, 2, ipPkt);
    rbs::ByteBuffer recovered = pdcp.processUlPDU(rnti2, 2, ulPdu);
    assert(recovered.size() == ipPkt.size());
    assert(recovered == ipPkt);

    // Remove bearer
    assert(pdcp.removeBearer(rnti, 1));
    // Second removal must fail
    assert(!pdcp.removeBearer(rnti, 1));

    // ── AES-128-CTR known-answer test (FIPS 197 / TS 33.401 §6.4.4) ──────────
    // Key = 16×0x00, count = 0 → CTR block = 16×0x00
    // AES_128(0x00...00, 0x00...00) = 66 e9 4b d4 ef 8a 2c 3b 88 4c fa 59 ca 34 2b 2e
    // CT of 16 zero bytes = AES keystream XOR 0x00...00 = keystream itself
    {
        rbs::lte::PDCP pdcp2;
        rbs::lte::PDCPConfig acfg{};
        acfg.bearerId  = 1;
        acfg.cipherAlg = rbs::lte::PDCPCipherAlg::AES;
        std::memset(acfg.cipherKey, 0x00, 16);  // all-zero key
        const rbs::RNTI ar = 10;
        assert(pdcp2.addBearer(ar, acfg));

        // Encrypt 16 zero bytes (txSN=0 → count=0)
        rbs::ByteBuffer zeros(16, 0x00);
        rbs::ByteBuffer encPdu = pdcp2.processDlPacket(ar, 1, zeros);
        // Strip 2-byte PDCP header to get ciphertext
        assert(encPdu.size() == 18);
        // Expected first 16 bytes of AES-128-CTR keystream (NIST all-zeros vector)
        static const uint8_t kExpected[16] = {
            0x66,0xe9,0x4b,0xd4,0xef,0x8a,0x2c,0x3b,
            0x88,0x4c,0xfa,0x59,0xca,0x34,0x2b,0x2e
        };
        assert(std::memcmp(encPdu.data() + 2, kExpected, 16) == 0);
    }

    // ── AES-128-CTR round-trip test ───────────────────────────────────────────
    {
        rbs::lte::PDCP pdcp3;
        rbs::lte::PDCPConfig acfg{};
        acfg.bearerId  = 1;
        acfg.cipherAlg = rbs::lte::PDCPCipherAlg::AES;
        // Non-trivial key: 0x00,0x01,...,0x0f
        for (int i = 0; i < 16; ++i) acfg.cipherKey[i] = static_cast<uint8_t>(i);
        const rbs::RNTI br = 20;
        assert(pdcp3.addBearer(br, acfg));

        rbs::ByteBuffer plain(64, 0xBE);
        rbs::ByteBuffer enc = pdcp3.processDlPacket(br, 1, plain);
        rbs::ByteBuffer dec = pdcp3.processUlPDU(br, 1, enc);
        assert(dec == plain);
    }

    // ── SNOW 3G (EEA1) round-trip test — TS 33.401 §6.4.3 ─────────────────────
    {
        rbs::lte::PDCP pdcp4;
        rbs::lte::PDCPConfig scfg{};
        scfg.bearerId  = 1;
        scfg.cipherAlg = rbs::lte::PDCPCipherAlg::SNOW3G;
        for (int i = 0; i < 16; ++i) scfg.cipherKey[i] = static_cast<uint8_t>(i);
        const rbs::RNTI sr = 30;
        assert(pdcp4.addBearer(sr, scfg));

        rbs::ByteBuffer plain(32, 0xAB);
        rbs::ByteBuffer enc = pdcp4.processDlPacket(sr, 1, plain);
        // Ciphertext must differ from plaintext (stream cipher applied)
        assert(enc.size() == plain.size() + 2);
        bool differs = false;
        for (size_t i = 0; i < plain.size(); ++i)
            if (enc[i + 2] != plain[i]) { differs = true; break; }
        assert(differs);

        rbs::ByteBuffer dec = pdcp4.processUlPDU(sr, 1, enc);
        assert(dec == plain);
    }

    // ── ZUC (EEA3) round-trip test — TS 33.401 §6.4.6 ─────────────────────────
    {
        rbs::lte::PDCP pdcp5;
        rbs::lte::PDCPConfig zcfg{};
        zcfg.bearerId  = 1;
        zcfg.cipherAlg = rbs::lte::PDCPCipherAlg::ZUC;
        for (int i = 0; i < 16; ++i) zcfg.cipherKey[i] = static_cast<uint8_t>(i ^ 0xFF);
        const rbs::RNTI zr = 40;
        assert(pdcp5.addBearer(zr, zcfg));

        rbs::ByteBuffer plain(32, 0xCD);
        rbs::ByteBuffer enc = pdcp5.processDlPacket(zr, 1, plain);
        assert(enc.size() == plain.size() + 2);
        bool differs = false;
        for (size_t i = 0; i < plain.size(); ++i)
            if (enc[i + 2] != plain[i]) { differs = true; break; }
        assert(differs);

        rbs::ByteBuffer dec = pdcp5.processUlPDU(zr, 1, enc);
        assert(dec == plain);
    }

    std::puts("test_pdcp PASSED");
    return 0;
}

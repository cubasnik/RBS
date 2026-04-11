#include "../src/lte/lte_rlc.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <numeric>
#ifdef _MSC_VER
#  include <crtdbg.h>
#endif

int main() {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    rbs::lte::LTERlc rlc;

    const rbs::RNTI rnti = 10;

    // ── Radio Bearer setup ───────────────────────────────────────────────────
    assert(rlc.addRB(rnti, 3, rbs::lte::LTERlcMode::AM));
    assert(rlc.addRB(rnti, 1, rbs::lte::LTERlcMode::AM));

    // Initial sequence numbers must be 0
    assert(rlc.txSN(rnti, 3) == 0);
    assert(rlc.rxSN(rnti, 3) == 0);

    // ── DL path: SDU → PDU via AM segmentation ───────────────────────────────
    rbs::ByteBuffer sdu1(100, 0xAA);
    assert(rlc.sendSdu(rnti, 3, sdu1));

    // Poll should yield a PDU (AM header 2 bytes + payload)
    rbs::ByteBuffer pdu1;
    assert(rlc.pollPdu(rnti, 3, pdu1, 1500));
    assert(pdu1.size() > 2);

    // Sequence number advances after poll
    assert(rlc.txSN(rnti, 3) == 1);

    // ── UL loopback: deliver PDU back, reassemble SDU ────────────────────────
    rlc.deliverPdu(rnti, 3, pdu1);

    rbs::ByteBuffer recovered;
    assert(rlc.receiveSdu(rnti, 3, recovered));
    assert(recovered.size() == sdu1.size());
    assert(recovered == sdu1);

    // ── Multiple SDUs, multiple polls ────────────────────────────────────────
    rbs::ByteBuffer sdu2(40, 0xBB);
    rbs::ByteBuffer sdu3(40, 0xCC);
    assert(rlc.sendSdu(rnti, 3, sdu2));
    assert(rlc.sendSdu(rnti, 3, sdu3));

    rbs::ByteBuffer pdu2, pdu3;
    assert(rlc.pollPdu(rnti, 3, pdu2, 1500));
    assert(rlc.pollPdu(rnti, 3, pdu3, 1500));
    // Drain any STATUS PDUs the AM engine may have enqueued
    rbs::ByteBuffer extra;
    while (rlc.pollPdu(rnti, 3, extra, 1500)) { /* flush */ }

    // ── Radio Bearer teardown ────────────────────────────────────────────────
    assert(rlc.removeRB(rnti, 3));
    assert(rlc.removeRB(rnti, 1));

    // Double-remove must fail
    assert(!rlc.removeRB(rnti, 3));

    // ── TM mode bearer ───────────────────────────────────────────────────────
    const rbs::RNTI rnti2 = 20;
    assert(rlc.addRB(rnti2, 0, rbs::lte::LTERlcMode::TM));

    rbs::ByteBuffer tmSdu(12, 0x55);
    assert(rlc.sendSdu(rnti2, 0, tmSdu));

    rbs::ByteBuffer tmPdu;
    // TM: no header overhead — PDU equals SDU byte-for-byte
    assert(rlc.pollPdu(rnti2, 0, tmPdu, 1500));
    assert(tmPdu == tmSdu);

    assert(rlc.removeRB(rnti2, 0));

    // ── AM segmentation + reassembly across multiple PDUs ────────────────────
    // 500-byte SDU, maxBytes=100 → payload per PDU = 98 bytes
    // Expected: 6 PDUs (5×98 + 1×10) with FI=01/11/11/11/11/10
    {
        const rbs::RNTI r3 = 30;
        assert(rlc.addRB(r3, 5, rbs::lte::LTERlcMode::AM));

        rbs::ByteBuffer bigSdu(500);
        std::iota(bigSdu.begin(), bigSdu.end(), static_cast<uint8_t>(0));
        assert(rlc.sendSdu(r3, 5, bigSdu));

        // Collect all PDUs
        std::vector<rbs::ByteBuffer> pdus;
        rbs::ByteBuffer p;
        while (rlc.pollPdu(r3, 5, p, 100)) pdus.push_back(p);

        // 500 / 98 = 5 remainder 10 → 6 PDUs
        assert(pdus.size() == 6);
        assert(rlc.txSN(r3, 5) == 6);

        // Check FI bits: [4:3] of byte 0, mask with 0x18 then >> 3
        auto fi = [](const rbs::ByteBuffer& pdu) -> uint8_t {
            return (pdu[0] >> 3) & 0x03;
        };
        assert(fi(pdus[0]) == 0x01);  // first segment
        assert(fi(pdus[1]) == 0x03);  // middle
        assert(fi(pdus[2]) == 0x03);  // middle
        assert(fi(pdus[3]) == 0x03);  // middle
        assert(fi(pdus[4]) == 0x03);  // middle
        assert(fi(pdus[5]) == 0x02);  // last segment

        // Deliver all PDUs → reassemble
        for (const auto& seg : pdus) rlc.deliverPdu(r3, 5, seg);

        // poll & discard the STATUS PDU triggered by poll bit on last segment
        rbs::ByteBuffer status;
        while (rlc.pollPdu(r3, 5, status, 1500)) { /* flush STATUS PDUs */ }

        rbs::ByteBuffer reassembled;
        assert(rlc.receiveSdu(r3, 5, reassembled));
        assert(reassembled == bigSdu);

        assert(rlc.removeRB(r3, 5));
    }

    // ── AM ARQ: NACK triggers retransmission from TX window ──────────────────
    {
        const rbs::RNTI r4 = 40;
        assert(rlc.addRB(r4, 6, rbs::lte::LTERlcMode::AM));

        rbs::ByteBuffer arqSdu(50, 0xDE);
        assert(rlc.sendSdu(r4, 6, arqSdu));

        // Get PDU (SN=0) but do NOT deliver it (simulate lost packet)
        rbs::ByteBuffer lostPdu;
        assert(rlc.pollPdu(r4, 6, lostPdu, 200));
        assert(rlc.txSN(r4, 6) == 1);

        // Build STATUS PDU: ACK_SN=1, NACK_SN=0
        // byte0: D/C=0, CPT=000, ACK_SN[9:5]=0  → 0x00
        // byte1: ACK_SN[4:0]=1 → 0x08, E1=1      → 0x0C
        // byte2: NACK_SN[9:5]=0                   → 0x00
        // byte3: NACK_SN[4:0]=0, E1=0             → 0x00
        rbs::ByteBuffer statusPdu = {0x00, 0x0C, 0x00, 0x00};
        rlc.deliverPdu(r4, 6, statusPdu);

        // pollPdu must return the retransmitted PDU (SN=0 from txWindow)
        rbs::ByteBuffer retxPdu;
        assert(rlc.pollPdu(r4, 6, retxPdu, 200));
        assert(retxPdu == lostPdu);  // identical to the original transmission

        // Now deliver the retransmitted PDU → should reassemble SDU
        rlc.deliverPdu(r4, 6, retxPdu);
        rbs::ByteBuffer arqRecovered;
        assert(rlc.receiveSdu(r4, 6, arqRecovered));
        assert(arqRecovered == arqSdu);

        assert(rlc.removeRB(r4, 6));
    }

    std::puts("test_lte_rlc PASSED");
    return 0;
}

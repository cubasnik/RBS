#include "../src/lte/lte_rlc.h"
#include <cassert>
#include <cstdio>
#include <cstring>
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

    std::puts("test_lte_rlc PASSED");
    return 0;
}

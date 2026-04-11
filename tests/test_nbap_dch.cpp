// Tests for NBAP DCH / HSDPA channel-setup procedures (п.10).
//
// Coverage:
//   1. nbap_encode_CommonTransportChannelSetupRequest  — TS 25.433 §8.3.2
//   2. nbap_encode_RadioLinkReconfigurePrepare         — TS 25.433 §8.1.5
//   3. nbap_encode_RadioLinkReconfigureCommit          — TS 25.433 §8.1.6
//   4. nbap_encode_RadioLinkSetupRequestFDD_HSDPA      — TS 25.433 §8.3.15
//   5. IubNbap::commonTransportChannelSetup
//   6. IubNbap::radioLinkReconfigurePrepare / Commit
//   7. IubNbap::radioLinkSetupHSDPA
//   8. UMTSStack::admitUEHSDPA
//   9. UMTSStack::reconfigureDCH

#include "../src/umts/nbap_codec.h"
#include "../src/umts/iub_link.h"
#include "../src/umts/umts_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::umts;

// Helper: verify the APER CommonTransportChannelSetupRequest / first byte rule.
static void check_initiating(const ByteBuffer& pdu, const char* name) {
    assert(!pdu.empty() && "PDU must be non-empty");
    // APER CHOICE with ext marker: first emitted bit is ext=0 → bit 7 of byte 0 must be 0.
    assert((pdu[0] & 0x80) == 0);
    (void)name;
}

// ─── Test 1 ──────────────────────────────────────────────────────────────────
// CommonTransportChannelSetupRequest: FACH / PCH / RACH each produce valid PDU
static void testCommonTransChSetupCodec() {
    for (auto ch : { NBAPCommonChannel::FACH,
                     NBAPCommonChannel::PCH,
                     NBAPCommonChannel::RACH }) {
        auto pdu = nbap_encode_CommonTransportChannelSetupRequest(1, ch);
        assert(pdu.size() >= 4);
        check_initiating(pdu, "CommonTransChSetup");
    }
    // Different channel types → different PDUs
    auto fach = nbap_encode_CommonTransportChannelSetupRequest(1, NBAPCommonChannel::FACH, 0);
    auto pch  = nbap_encode_CommonTransportChannelSetupRequest(1, NBAPCommonChannel::PCH,  0);
    auto rach = nbap_encode_CommonTransportChannelSetupRequest(1, NBAPCommonChannel::RACH, 0);
    assert(fach != pch);
    assert(fach != rach);
    std::puts("  PASS testCommonTransChSetupCodec");
}

// ─── Test 2 ──────────────────────────────────────────────────────────────────
// RadioLinkReconfigurePrepare: each SF value produces a distinct valid PDU
static void testRLReconfigurePrepareCodec() {
    for (auto sf : { SF::SF4, SF::SF8, SF::SF16, SF::SF32,
                     SF::SF64, SF::SF128, SF::SF256 }) {
        auto pdu = nbap_encode_RadioLinkReconfigurePrepare(0x0001, sf);
        assert(pdu.size() >= 4);
        check_initiating(pdu, "RLReconfigPrepare");
    }
    // Different SF → different payload
    auto p16  = nbap_encode_RadioLinkReconfigurePrepare(1, SF::SF16,  0);
    auto p64  = nbap_encode_RadioLinkReconfigurePrepare(1, SF::SF64,  0);
    auto p256 = nbap_encode_RadioLinkReconfigurePrepare(1, SF::SF256, 0);
    assert(p16 != p64);
    assert(p16 != p256);
    assert(p64 != p256);
    std::puts("  PASS testRLReconfigurePrepareCodec");
}

// ─── Test 3 ──────────────────────────────────────────────────────────────────
// RadioLinkReconfigureCommit: minimal PDU, different txId → different bytes
static void testRLReconfigureCommitCodec() {
    auto pdu = nbap_encode_RadioLinkReconfigureCommit(0x0001);
    assert(pdu.size() >= 4);
    check_initiating(pdu, "RLReconfigCommit");
    // txId is embedded in the envelope
    auto p0 = nbap_encode_RadioLinkReconfigureCommit(0x0001, 0);
    auto p1 = nbap_encode_RadioLinkReconfigureCommit(0x0001, 1);
    assert(p0 != p1);
    std::puts("  PASS testRLReconfigureCommitCodec");
}

// ─── Test 4 ──────────────────────────────────────────────────────────────────
// RadioLinkSetupRequestFDD_HSDPA: encodes HS-DSCH codes and power
static void testRLSetupHSDPACodec() {
    auto pdu = nbap_encode_RadioLinkSetupRequestFDD_HSDPA(0x0001, 5, 300);
    assert(pdu.size() >= 4);
    check_initiating(pdu, "RLSetupHSDPA");
    // Changing hsDschCodes changes the payload
    auto p5  = nbap_encode_RadioLinkSetupRequestFDD_HSDPA(0x0001, 5,  300, 0);
    auto p10 = nbap_encode_RadioLinkSetupRequestFDD_HSDPA(0x0001, 10, 300, 0);
    assert(p5 != p10);
    // Changing power also changes payload
    auto pPow0   = nbap_encode_RadioLinkSetupRequestFDD_HSDPA(0x0001, 5, 100, 0);
    auto pPow400 = nbap_encode_RadioLinkSetupRequestFDD_HSDPA(0x0001, 5, 400, 0);
    assert(pPow0 != pPow400);
    std::puts("  PASS testRLSetupHSDPACodec");
}

// ─── Test 5 ──────────────────────────────────────────────────────────────────
// IubNbap::commonTransportChannelSetup — all three channel types succeed
static void testIubCommonTransChSetup() {
    IubNbap nbap("NodeB-DCH-1");
    assert(nbap.connect("127.0.0.1", 25470));
    assert(nbap.commonTransportChannelSetup(1, NBAPCommonChannel::FACH));
    assert(nbap.commonTransportChannelSetup(1, NBAPCommonChannel::PCH));
    assert(nbap.commonTransportChannelSetup(1, NBAPCommonChannel::RACH));
    // Fails when not connected
    nbap.disconnect();
    assert(!nbap.commonTransportChannelSetup(1, NBAPCommonChannel::FACH));
    std::puts("  PASS testIubCommonTransChSetup");
}

// ─── Test 6 ──────────────────────────────────────────────────────────────────
// IubNbap::radioLinkReconfigurePrepare / radioLinkReconfigureCommit
static void testIubRLReconfig() {
    IubNbap nbap("NodeB-DCH-2");
    assert(nbap.connect("127.0.0.1", 25470));

    // Must have an established radio link first
    assert(nbap.radioLinkSetup(0x0010, 50, SF::SF16));

    // Prepare: SF16 → SF32
    assert(nbap.radioLinkReconfigurePrepare(0x0010, SF::SF32));
    // The simulated RNC ACK (COMMIT) should be queued
    NBAPMessage ack{};
    assert(nbap.recvNbapMsg(ack));
    assert(ack.procedure == NBAPProcedure::RADIO_LINK_RECONFIGURE_COMMIT);

    // Commit
    assert(nbap.radioLinkReconfigureCommit(0x0010));

    // Unknown RNTI → both return false
    assert(!nbap.radioLinkReconfigurePrepare(0xFFFF, SF::SF64));
    assert(!nbap.radioLinkReconfigureCommit(0xFFFF));
    std::puts("  PASS testIubRLReconfig");
}

// ─── Test 7 ──────────────────────────────────────────────────────────────────
// IubNbap::radioLinkSetupHSDPA — creates link, deletion succeeds afterwards
static void testIubRLSetupHSDPA() {
    IubNbap nbap("NodeB-DCH-3");
    assert(nbap.connect("127.0.0.1", 25470));
    assert(nbap.radioLinkSetupHSDPA(0x0020, 51, 5));
    // The link should be tracked → deletion must succeed
    assert(nbap.radioLinkDeletion(0x0020));
    // Fails when not connected
    IubNbap nbap2("NodeB-DCH-3b");
    assert(!nbap2.radioLinkSetupHSDPA(0x0030, 52, 5));
    std::puts("  PASS testIubRLSetupHSDPA");
}

// ─── Test 8 ──────────────────────────────────────────────────────────────────
// UMTSStack::admitUEHSDPA — HSDPA UE counted alongside DCH UE
static void testUMTSStackHSDPA() {
    UMTSCellConfig cfg{};
    cfg.cellId         = 5;
    cfg.uarfcn         = 10562;
    cfg.band           = UMTSBand::B1;
    cfg.txPower        = {43.0};
    cfg.primaryScrCode = 50;
    cfg.mcc            = 250;
    cfg.mnc            = 1;

    auto rf = std::make_shared<hal::RFHardware>(1, 1);
    assert(rf->initialise());

    UMTSStack stack(rf, cfg);
    assert(stack.start());
    assert(stack.connectedUECount() == 0);

    // Admit one DCH UE and one HSDPA UE
    RNTI r1 = stack.admitUE     (311480000000001ULL, SF::SF16);
    RNTI r2 = stack.admitUEHSDPA(311480000000002ULL);
    assert(r1 != 0 && r2 != 0 && r1 != r2);
    assert(stack.connectedUECount() == 2);

    stack.releaseUE(r1);
    stack.releaseUE(r2);
    assert(stack.connectedUECount() == 0);

    stack.stop();
    std::puts("  PASS testUMTSStackHSDPA");
}

// ─── Test 9 ──────────────────────────────────────────────────────────────────
// UMTSStack::reconfigureDCH — known RNTI succeeds, unknown fails
static void testUMTSStackDCHReconfig() {
    UMTSCellConfig cfg{};
    cfg.cellId = 6;
    cfg.uarfcn = 10562;
    cfg.band   = UMTSBand::B1;

    auto rf = std::make_shared<hal::RFHardware>(1, 1);
    assert(rf->initialise());

    UMTSStack stack(rf, cfg);
    assert(stack.start());

    RNTI rnti = stack.admitUE(311480000000003ULL, SF::SF16);
    assert(rnti != 0);
    assert(stack.reconfigureDCH(rnti, SF::SF32));
    assert(!stack.reconfigureDCH(0xFFFF, SF::SF32));  // unknown RNTI → false

    stack.releaseUE(rnti);
    stack.stop();
    std::puts("  PASS testUMTSStackDCHReconfig");
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::puts("=== test_nbap_dch ===");
    testCommonTransChSetupCodec();
    testRLReconfigurePrepareCodec();
    testRLReconfigureCommitCodec();
    testRLSetupHSDPACodec();
    testIubCommonTransChSetup();
    testIubRLReconfig();
    testIubRLSetupHSDPA();
    testUMTSStackHSDPA();
    testUMTSStackDCHReconfig();
    std::puts("test_nbap_dch PASSED");
    return 0;
}

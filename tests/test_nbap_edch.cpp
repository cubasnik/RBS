// Tests for HSUPA / E-DCH channel-setup procedures (п.13).
// Coverage:
//   1. nbap_encode_RadioLinkSetupRequestFDD_EDCH — valid APER PDU, non-empty
//   2. PDU with TTI_2MS differs from TTI_10MS
//   3. Different maxBitrateIdx produces different PDUs
//   4. IubNbap::radioLinkSetupEDCH — connected success
//   5. IubNbap::radioLinkSetupEDCH — not connected → false
//   6. IubNbap::radioLinkSetupEDCH then radioLinkDeletion
//   7. UMTSMAC::assignEDCH — non-zero RNTI, edchUECount=1
//   8. UMTSMAC release E-DCH — edchUECount decrements
//   9. UMTSStack::admitUEEDCH — connectedUECount=1, releaseUE returns to 0
//  10. E-DCH and HSDPA coexist — independent counters

#include "../src/umts/nbap_codec.h"
#include "../src/umts/iub_link.h"
#include "../src/umts/umts_stack.h"
#include "../src/umts/umts_mac.h"
#include "../src/umts/umts_phy.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::umts;

// ── Helper: check the PDU has an APER InitiatingMessage outer structure ───────
static void check_initiating(const ByteBuffer& pdu, const char* name) {
    assert(!pdu.empty() && "PDU must be non-empty");
    // APER outer CHOICE index 0 (initiatingMessage): first byte bit7 = 0
    assert((pdu[0] & 0x80) == 0);
    (void)name;
}

// ── Test 1 — E-DCH codec basic validity ──────────────────────────────────────
static void test_edch_codec_basic() {
    ByteBuffer pdu = nbap_encode_RadioLinkSetupRequestFDD_EDCH(0x1234);
    check_initiating(pdu, "RadioLinkSetupRequestFDD_EDCH");
    // PDU must carry 4 IEs → length > 10 bytes
    assert(pdu.size() > 10);
    std::puts("  [PASS] test_edch_codec_basic");
}

// ── Test 2 — TTI_2MS and TTI_10MS produce different PDUs ─────────────────────
static void test_edch_codec_tti_diff() {
    ByteBuffer pdu2ms  = nbap_encode_RadioLinkSetupRequestFDD_EDCH(
        0x0001, EDCHTTI::TTI_2MS,  4);
    ByteBuffer pdu10ms = nbap_encode_RadioLinkSetupRequestFDD_EDCH(
        0x0001, EDCHTTI::TTI_10MS, 4);
    assert(pdu2ms.size() == pdu10ms.size()); // same structure, different bits
    assert(pdu2ms != pdu10ms);
    std::puts("  [PASS] test_edch_codec_tti_diff");
}

// ── Test 3 — Different maxBitrateIdx produces different PDUs ─────────────────
static void test_edch_codec_bitrate_diff() {
    ByteBuffer pduLow  = nbap_encode_RadioLinkSetupRequestFDD_EDCH(
        0x0002, EDCHTTI::TTI_10MS, 0);
    ByteBuffer pduHigh = nbap_encode_RadioLinkSetupRequestFDD_EDCH(
        0x0002, EDCHTTI::TTI_10MS, 7);
    assert(!pduLow.empty());
    assert(!pduHigh.empty());
    assert(pduLow != pduHigh);
    std::puts("  [PASS] test_edch_codec_bitrate_diff");
}

// ── Test 4 — IubNbap::radioLinkSetupEDCH succeeds when connected ─────────────
static void test_iub_edch_connected() {
    IubNbap iub("NodeB-TEST");
    iub.connect("127.0.0.1", 38412);
    assert(iub.isConnected());
    assert(iub.radioLinkSetupEDCH(0x0010, 55, EDCHTTI::TTI_10MS));
    std::puts("  [PASS] test_iub_edch_connected");
}

// ── Test 5 — IubNbap::radioLinkSetupEDCH fails when not connected ────────────
static void test_iub_edch_not_connected() {
    IubNbap iub("NodeB-TEST2");
    // not connected — radioLinkSetupEDCH must return false
    assert(!iub.radioLinkSetupEDCH(0x0011, 56, EDCHTTI::TTI_2MS));
    std::puts("  [PASS] test_iub_edch_not_connected");
}

// ── Test 6 — E-DCH setup then deletion succeeds ──────────────────────────────
static void test_iub_edch_setup_and_delete() {
    IubNbap iub("NodeB-TEST3");
    iub.connect("127.0.0.1", 38412);
    assert(iub.radioLinkSetupEDCH(0x0020, 60));
    assert(iub.radioLinkDeletion(0x0020));
    std::puts("  [PASS] test_iub_edch_setup_and_delete");
}

// ── Test 7 — UMTSMAC::assignEDCH non-zero RNTI, edchUECount increments ───────
static void test_mac_assign_edch() {
    auto rf  = std::make_shared<hal::RFHardware>();
    UMTSCellConfig cfg{};
    cfg.cellId = 200; cfg.primaryScrCode = 10;
    auto phy = std::make_shared<UMTSPhy>(rf, cfg);
    UMTSMAC mac(phy, cfg);
    mac.start();

    assert(mac.edchUECount() == 0);
    [[maybe_unused]] RNTI r = mac.assignEDCH();
    assert(r != 0);
    assert(mac.edchUECount() == 1);
    assert(mac.activeChannelCount() == 1);

    mac.stop();
    std::puts("  [PASS] test_mac_assign_edch");
}

// ── Test 8 — UMTSMAC release E-DCH decrements edchUECount ───────────────────
static void test_mac_release_edch() {
    auto rf  = std::make_shared<hal::RFHardware>();
    UMTSCellConfig cfg{};
    cfg.cellId = 201; cfg.primaryScrCode = 11;
    auto phy = std::make_shared<UMTSPhy>(rf, cfg);
    UMTSMAC mac(phy, cfg);
    mac.start();

    [[maybe_unused]] RNTI r1 = mac.assignEDCH();
    [[maybe_unused]] RNTI r2 = mac.assignEDCH();
    assert(mac.edchUECount() == 2);
    assert(mac.releaseDCH(r1));
    assert(mac.edchUECount() == 1);
    assert(mac.releaseDCH(r2));
    assert(mac.edchUECount() == 0);

    mac.stop();
    std::puts("  [PASS] test_mac_release_edch");
}

// ── Test 9 — UMTSStack::admitUEEDCH + releaseUE ─────────────────────────────
static void test_stack_edch() {
    auto rf = std::make_shared<hal::RFHardware>();
    UMTSCellConfig cfg{};
    cfg.cellId = 202; cfg.primaryScrCode = 12;
    UMTSStack stack(rf, cfg);
    stack.start();

    RNTI r = stack.admitUEEDCH(8765432100ULL);
    assert(r != 0);
    assert(stack.connectedUECount() == 1);
    stack.releaseUE(r);
    assert(stack.connectedUECount() == 0);

    stack.stop();
    std::puts("  [PASS] test_stack_edch");
}

// ── Test 10 — E-DCH and HSDPA coexist with independent counters ──────────────
static void test_edch_hsdpa_coexist() {
    auto rf  = std::make_shared<hal::RFHardware>();
    UMTSCellConfig cfg{};
    cfg.cellId = 203; cfg.primaryScrCode = 13;
    auto phy = std::make_shared<UMTSPhy>(rf, cfg);
    UMTSMAC mac(phy, cfg);
    mac.start();

    RNTI dch   = mac.assignDCH(SF::SF16);
    RNTI hsdpa = mac.assignHSDSCH();
    RNTI edch  = mac.assignEDCH();

    assert(mac.activeChannelCount() == 3);
    assert(mac.hsdschUECount() == 1);
    assert(mac.edchUECount()   == 1);

    mac.releaseDCH(dch);
    mac.releaseDCH(hsdpa);
    assert(mac.hsdschUECount() == 0);
    assert(mac.edchUECount()   == 1);

    mac.releaseDCH(edch);
    assert(mac.edchUECount() == 0);
    assert(mac.activeChannelCount() == 0);

    mac.stop();
    std::puts("  [PASS] test_edch_hsdpa_coexist");
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::puts("=== test_nbap_edch ===");
    test_edch_codec_basic();
    test_edch_codec_tti_diff();
    test_edch_codec_bitrate_diff();
    test_iub_edch_connected();
    test_iub_edch_not_connected();
    test_iub_edch_setup_and_delete();
    test_mac_assign_edch();
    test_mac_release_edch();
    test_stack_edch();
    test_edch_hsdpa_coexist();
    std::puts("test_nbap_edch PASSED");
    return 0;
}

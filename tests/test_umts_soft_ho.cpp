// Tests for UMTS Soft Handover — Active Set management (п.14).
// Coverage:
//   1.  UMTSRrc::addToActiveSet — simple add, dedup, max-size cap
//   2.  UMTSRrc::removeFromActiveSet — found and not-found cases
//   3.  Event 1A via processMeasurementReport — AS grows by one
//   4.  Event 1B via processMeasurementReport — AS shrinks
//   5.  Event 1C via processMeasurementReport — weakest replaced
//   6.  Active set capped at 3 cells (TS 25.331 FDD limit)
//   7.  IubNbap::radioLinkAddition — success when primary link exists
//   8.  IubNbap::radioLinkAddition — fails without primary link
//   9.  IubNbap::radioLinkDeletionSHO — adds then removes
//  10.  UMTSStack::softHandoverUpdate — end-to-end 1A/1B round-trip
//  11.  UMTSStack::activeSet — query after stack-level admitUE
//  12.  Unknown RNTI in softHandoverUpdate — no crash

#include "../src/umts/umts_rrc.h"
#include "../src/umts/iub_link.h"
#include "../src/umts/umts_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::umts;

// ─── helpers ─────────────────────────────────────────────────────────────────
static UMTSCellConfig makeCfg(uint32_t cellId, uint16_t psc = 10) {
    UMTSCellConfig c{};
    c.cellId = cellId;
    c.primaryScrCode = psc;
    return c;
}

static MeasurementReport makeReport(RNTI rnti, RrcMeasEvent ev, ScrCode psc,
                                    int8_t ecNo = -8) {
    MeasurementReport r{};
    r.rnti             = rnti;
    r.event            = ev;
    r.triggeringScrCode = psc;
    r.cpichEcNo_dB     = ecNo;
    r.cpichRscp_dBm    = -75;
    return r;
}

// ── Test 1 — addToActiveSet: add, dedup, max-size ────────────────────────────
static void test_rrc_add_to_active_set() {
    UMTSRrc rrc;
    rrc.handleConnectionRequest(0x0001, 111111111ULL);

    ActiveSetEntry e1{100, 0, -6, true};
    ActiveSetEntry e2{200, 0, -8, false};
    ActiveSetEntry e3{300, 0, -10, false};
    ActiveSetEntry e4{400, 0, -12, false};  // should be rejected (cap=3)

    assert(rrc.addToActiveSet(0x0001, e1));
    assert(rrc.activeSet(0x0001).size() == 1);
    assert(rrc.addToActiveSet(0x0001, e1));  // dedup — still 1
    assert(rrc.activeSet(0x0001).size() == 1);
    assert(rrc.addToActiveSet(0x0001, e2));
    assert(rrc.addToActiveSet(0x0001, e3));
    assert(rrc.activeSet(0x0001).size() == 3);
    assert(!rrc.addToActiveSet(0x0001, e4));  // cap reached
    assert(rrc.activeSet(0x0001).size() == 3);
    std::puts("  [PASS] test_rrc_add_to_active_set");
}

// ── Test 2 — removeFromActiveSet: found and not-found ────────────────────────
static void test_rrc_remove_from_active_set() {
    UMTSRrc rrc;
    rrc.handleConnectionRequest(0x0002, 222222222ULL);

    rrc.addToActiveSet(0x0002, {50, 0, -5, true});
    rrc.addToActiveSet(0x0002, {60, 0, -9, false});
    assert(rrc.activeSet(0x0002).size() == 2);

    assert(rrc.removeFromActiveSet(0x0002, 60));
    assert(rrc.activeSet(0x0002).size() == 1);
    assert(!rrc.removeFromActiveSet(0x0002, 99));  // not present
    std::puts("  [PASS] test_rrc_remove_from_active_set");
}

// ── Test 3 — Event 1A → AS grows ─────────────────────────────────────────────
static void test_event_1a_adds_cell() {
    UMTSRrc rrc;
    rrc.handleConnectionRequest(0x0003, 333ULL);

    rrc.processMeasurementReport(makeReport(0x0003, RrcMeasEvent::EVENT_1A, 500, -7));
    assert(rrc.activeSet(0x0003).size() == 1);
    assert(rrc.activeSet(0x0003)[0].primaryScrCode == 500);

    rrc.processMeasurementReport(makeReport(0x0003, RrcMeasEvent::EVENT_1A, 501, -9));
    assert(rrc.activeSet(0x0003).size() == 2);
    std::puts("  [PASS] test_event_1a_adds_cell");
}

// ── Test 4 — Event 1B → AS shrinks ───────────────────────────────────────────
static void test_event_1b_removes_cell() {
    UMTSRrc rrc;
    rrc.handleConnectionRequest(0x0004, 444ULL);

    rrc.processMeasurementReport(makeReport(0x0004, RrcMeasEvent::EVENT_1A, 600, -6));
    rrc.processMeasurementReport(makeReport(0x0004, RrcMeasEvent::EVENT_1A, 601, -9));
    assert(rrc.activeSet(0x0004).size() == 2);

    rrc.processMeasurementReport(makeReport(0x0004, RrcMeasEvent::EVENT_1B, 601, -25));
    assert(rrc.activeSet(0x0004).size() == 1);
    assert(rrc.activeSet(0x0004)[0].primaryScrCode == 600);
    std::puts("  [PASS] test_event_1b_removes_cell");
}

// ── Test 5 — Event 1C → weakest replaced ─────────────────────────────────────
static void test_event_1c_replaces_weakest() {
    UMTSRrc rrc;
    rrc.handleConnectionRequest(0x0005, 555ULL);

    // Fill AS: PSC 10 @ -5dB, PSC 11 @ -15dB (weakest), PSC 12 @ -10dB
    rrc.addToActiveSet(0x0005, {10, 0, -5,  true});
    rrc.addToActiveSet(0x0005, {11, 0, -15, false});
    rrc.addToActiveSet(0x0005, {12, 0, -10, false});
    assert(rrc.activeSet(0x0005).size() == 3);

    // Event 1C: new cell PSC 20 @ -3dB beats weakest (PSC 11 @ -15dB)
    rrc.processMeasurementReport(makeReport(0x0005, RrcMeasEvent::EVENT_1C, 20, -3));

    // PSC 11 removed, PSC 20 added
    const auto& as = rrc.activeSet(0x0005);
    assert(as.size() == 3);
    bool has11 = false, has20 = false;
    for (auto& e : as) {
        if (e.primaryScrCode == 11) has11 = true;
        if (e.primaryScrCode == 20) has20 = true;
    }
    assert(!has11);
    assert(has20);
    std::puts("  [PASS] test_event_1c_replaces_weakest");
}

// ── Test 6 — Cap at 3 cells via Event 1A ─────────────────────────────────────
static void test_active_set_cap() {
    UMTSRrc rrc;
    rrc.handleConnectionRequest(0x0006, 666ULL);

    for (uint16_t psc = 700; psc <= 703; ++psc)
        rrc.processMeasurementReport(makeReport(0x0006, RrcMeasEvent::EVENT_1A, psc, -8));

    // Must not exceed 3 even after 4 events
    assert(rrc.activeSet(0x0006).size() == 3);
    std::puts("  [PASS] test_active_set_cap");
}

// ── Test 7 — IubNbap::radioLinkAddition succeeds with primary link ────────────
static void test_iub_rl_addition_ok() {
    IubNbap iub("NodeB-HO");
    iub.connect("127.0.0.1", 25412);
    assert(iub.radioLinkSetup(0x0010, 50, SF::SF16));
    assert(iub.radioLinkAddition(0x0010, 51, SF::SF16));
    std::puts("  [PASS] test_iub_rl_addition_ok");
}

// ── Test 8 — IubNbap::radioLinkAddition fails without primary link ─────────────
static void test_iub_rl_addition_no_primary() {
    IubNbap iub("NodeB-HO2");
    iub.connect("127.0.0.1", 25412);
    // No radioLinkSetup called first
    assert(!iub.radioLinkAddition(0x0020, 52, SF::SF16));
    std::puts("  [PASS] test_iub_rl_addition_no_primary");
}

// ── Test 9 — IubNbap: add SHO leg and delete it ───────────────────────────────
static void test_iub_sho_leg_add_delete() {
    IubNbap iub("NodeB-HO3");
    iub.connect("127.0.0.1", 25412);
    assert(iub.radioLinkSetup(0x0030, 100, SF::SF16));
    assert(iub.radioLinkAddition(0x0030, 101, SF::SF16));
    assert(iub.radioLinkAddition(0x0030, 102, SF::SF16));
    assert(iub.radioLinkDeletionSHO(0x0030, 101));
    assert(!iub.radioLinkDeletionSHO(0x0030, 101));  // already gone
    assert(iub.radioLinkDeletionSHO(0x0030, 102));
    std::puts("  [PASS] test_iub_sho_leg_add_delete");
}

// ── Test 10 — UMTSStack::softHandoverUpdate end-to-end 1A/1B ──────────────────
static void test_stack_soft_ho_end_to_end() {
    auto rf = std::make_shared<hal::RFHardware>();
    UMTSStack stack(rf, makeCfg(300, 30));
    stack.start();

    RNTI r = stack.admitUE(99999999ULL, SF::SF16);
    assert(r != 0);
    assert(stack.activeSet(r).empty());  // no AS yet

    // Event 1A: neighbour 31 better than threshold
    stack.softHandoverUpdate(makeReport(r, RrcMeasEvent::EVENT_1A, 31, -7));
    assert(stack.activeSet(r).size() == 1);

    // Event 1A: another neighbour 32
    stack.softHandoverUpdate(makeReport(r, RrcMeasEvent::EVENT_1A, 32, -9));
    assert(stack.activeSet(r).size() == 2);

    // Event 1B: PSC 31 drops below threshold
    stack.softHandoverUpdate(makeReport(r, RrcMeasEvent::EVENT_1B, 31, -24));
    assert(stack.activeSet(r).size() == 1);
    assert(stack.activeSet(r)[0].primaryScrCode == 32);

    stack.stop();
    std::puts("  [PASS] test_stack_soft_ho_end_to_end");
}

// ── Test 11 — activeSet query after admitUE (empty initially) ─────────────────
static void test_stack_active_set_query() {
    auto rf = std::make_shared<hal::RFHardware>();
    UMTSStack stack(rf, makeCfg(301, 31));
    stack.start();

    RNTI r = stack.admitUE(88888888ULL, SF::SF16);
    assert(r != 0);
    assert(stack.activeSet(r).empty());

    stack.releaseUE(r);
    stack.stop();
    std::puts("  [PASS] test_stack_active_set_query");
}

// ── Test 12 — softHandoverUpdate with unknown RNTI — no crash ─────────────────
static void test_stack_unknown_rnti_no_crash() {
    auto rf = std::make_shared<hal::RFHardware>();
    UMTSStack stack(rf, makeCfg(302, 32));
    stack.start();

    // Nothing admitted — should silently return
    stack.softHandoverUpdate(makeReport(0xDEAD, RrcMeasEvent::EVENT_1A, 40, -8));
    assert(stack.activeSet(0xDEAD).empty());

    stack.stop();
    std::puts("  [PASS] test_stack_unknown_rnti_no_crash");
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::puts("=== test_umts_soft_ho ===");
    test_rrc_add_to_active_set();
    test_rrc_remove_from_active_set();
    test_event_1a_adds_cell();
    test_event_1b_removes_cell();
    test_event_1c_replaces_weakest();
    test_active_set_cap();
    test_iub_rl_addition_ok();
    test_iub_rl_addition_no_primary();
    test_iub_sho_leg_add_delete();
    test_stack_soft_ho_end_to_end();
    test_stack_active_set_query();
    test_stack_unknown_rnti_no_crash();
    std::puts("test_umts_soft_ho PASSED");
    return 0;
}

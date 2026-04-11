// п.12 — LTE RRC Measurement Control & Reporting
// Tests: A1/A2/A3/A5 event triggers, MeasConfig storage, HO callback
// References: TS 36.331 §5.5.4, §6.3.5
#include "../src/lte/lte_rrc.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

using namespace rbs;
using namespace rbs::lte;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers

static void connectUE(LTERrc& rrc, RNTI rnti) {
    assert(rrc.handleConnectionRequest(rnti, static_cast<IMSI>(rnti) * 1000));
}

static LTERrcMeasResult makeMeasResult(RNTI rnti, uint8_t measId,
                                        int16_t servRsrp,
                                        const std::vector<std::tuple<uint16_t, EARFCN, int16_t>>& neighs)
{
    LTERrcMeasResult mr{};
    mr.rnti = rnti;  mr.measId = measId;  mr.rsrp_q = servRsrp;
    for (auto& [pci, earfcn, rsrp] : neighs)
        mr.neighbours.push_back({pci, earfcn, rsrp, 20});
    return mr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: MeasConfig stored per UE — two UEs get independent configs
static void test_measconfig_stored_per_ue() {
    LTERrc rrc;
    const RNTI r1 = 100, r2 = 101;
    connectUE(rrc, r1);  connectUE(rrc, r2);

    MeasObject mo1{1, 3000, LTEBand::B3};
    ReportConfig rc1{1, MeasEventType::A3, RrcTriggerQty::RSRP, 3, 45, 55, 160};
    rrc.sendMeasurementConfig(r1, mo1, rc1);

    MeasObject mo2{2, 1800, LTEBand::B3};
    ReportConfig rc2{2, MeasEventType::A2, RrcTriggerQty::RSRP, 3, 40, 50, 320};
    rrc.sendMeasurementConfig(r2, mo2, rc2);

    // No crash — configs accepted for both UEs

    rrc.releaseConnection(r1);
    rrc.releaseConnection(r2);
    std::puts("test_measconfig_stored_per_ue PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: A3 NOT triggered when neighbour RSRP < serving + offset
static void test_a3_not_triggered_below_offset() {
    LTERrc rrc;
    const RNTI r1 = 110;
    connectUE(rrc, r1);

    bool hoFired = false;
    rrc.setHandoverCallback([&](RNTI, uint16_t, EARFCN){ hoFired = true; });

    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{1, MeasEventType::A3, RrcTriggerQty::RSRP, 5, 45, 55, 160};
    rrc.sendMeasurementConfig(r1, mo, rc);

    // servRSRP=60, neighRSRP=63 → 63 < 60+5=65 → A3 NOT triggered
    auto mr = makeMeasResult(r1, 1, 60, {{300, 3050, 63}});
    rrc.processMeasurementReport(mr);

    assert(!hoFired);
    rrc.releaseConnection(r1);
    std::puts("test_a3_not_triggered_below_offset PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: A3 triggered when neighbour RSRP >= serving + offset
static void test_a3_triggered_above_offset() {
    LTERrc rrc;
    const RNTI r1 = 120;
    connectUE(rrc, r1);

    bool hoFired = false;
    uint16_t hoPci = 0;
    rrc.setHandoverCallback([&](RNTI, uint16_t pci, EARFCN){ hoFired = true; hoPci = pci; });

    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{1, MeasEventType::A3, RrcTriggerQty::RSRP, 5, 45, 55, 160};
    rrc.sendMeasurementConfig(r1, mo, rc);

    // servRSRP=60, neighRSRP=65 → 65 >= 60+5=65 → A3 triggered
    auto mr = makeMeasResult(r1, 1, 60, {{301, 3050, 65}});
    rrc.processMeasurementReport(mr);

    assert(hoFired);
    assert(hoPci == 301);
    rrc.releaseConnection(r1);
    std::puts("test_a3_triggered_above_offset PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: A1 triggered when serving RSRP > threshold1 (no HO)
static void test_a1_good_coverage() {
    LTERrc rrc;
    const RNTI r1 = 130;
    connectUE(rrc, r1);

    bool hoFired = false;
    rrc.setHandoverCallback([&](RNTI, uint16_t, EARFCN){ hoFired = true; });

    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{1, MeasEventType::A1, RrcTriggerQty::RSRP, 3, 50, 55, 160};
    rrc.sendMeasurementConfig(r1, mo, rc);

    // A1: serving=60 > threshold1=50 → event fires but no HO
    auto mr = makeMeasResult(r1, 1, 60, {{302, 3050, 55}});
    rrc.processMeasurementReport(mr);

    assert(!hoFired);  // A1 does not trigger HO
    rrc.releaseConnection(r1);
    std::puts("test_a1_good_coverage PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: A2 trigger HO when serving falls below threshold1
static void test_a2_poor_coverage_triggers_ho() {
    LTERrc rrc;
    const RNTI r1 = 140;
    connectUE(rrc, r1);

    bool hoFired = false;
    uint16_t hoPci = 0;
    rrc.setHandoverCallback([&](RNTI, uint16_t pci, EARFCN){ hoFired = true; hoPci = pci; });

    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{2, MeasEventType::A2, RrcTriggerQty::RSRP, 3, 50, 55, 160};
    rrc.sendMeasurementConfig(r1, mo, rc);

    // A2: serving=40 < threshold1=50 AND neighbour present → HO to best neighbour
    auto mr = makeMeasResult(r1, 2, 40, {{310, 3050, 62}, {311, 3060, 70}});
    rrc.processMeasurementReport(mr);

    assert(hoFired);
    assert(hoPci == 311);  // best neighbour (RSRP=70) wins
    rrc.releaseConnection(r1);
    std::puts("test_a2_poor_coverage_triggers_ho PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: A2 NOT triggered when serving above threshold1
static void test_a2_not_triggered_above_threshold() {
    LTERrc rrc;
    const RNTI r1 = 150;
    connectUE(rrc, r1);

    bool hoFired = false;
    rrc.setHandoverCallback([&](RNTI, uint16_t, EARFCN){ hoFired = true; });

    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{2, MeasEventType::A2, RrcTriggerQty::RSRP, 3, 50, 55, 160};
    rrc.sendMeasurementConfig(r1, mo, rc);

    // A2: serving=60 >= threshold1=50 → NOT triggered
    auto mr = makeMeasResult(r1, 2, 60, {{310, 3050, 70}});
    rrc.processMeasurementReport(mr);

    assert(!hoFired);
    rrc.releaseConnection(r1);
    std::puts("test_a2_not_triggered_above_threshold PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: A5 triggered when serving < thr1 AND neighbour > thr2
static void test_a5_inter_freq_ho() {
    LTERrc rrc;
    const RNTI r1 = 160;
    connectUE(rrc, r1);

    bool hoFired = false;
    EARFCN hoEarfcn = 0;
    rrc.setHandoverCallback([&](RNTI, uint16_t, EARFCN earfcn){ hoFired = true; hoEarfcn = earfcn; });

    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{3, MeasEventType::A5, RrcTriggerQty::RSRP, 3, 50, 60, 160};
    rrc.sendMeasurementConfig(r1, mo, rc);

    // A5: serving=42 < thr1=50 AND neigh=65 > thr2=60 → triggered
    auto mr = makeMeasResult(r1, 3, 42, {{320, 1800, 65}});
    rrc.processMeasurementReport(mr);

    assert(hoFired);
    assert(hoEarfcn == 1800);
    rrc.releaseConnection(r1);
    std::puts("test_a5_inter_freq_ho PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: A5 NOT triggered when neighbour RSRP <= thr2
static void test_a5_not_triggered_neigh_below_thr2() {
    LTERrc rrc;
    const RNTI r1 = 170;
    connectUE(rrc, r1);

    bool hoFired = false;
    rrc.setHandoverCallback([&](RNTI, uint16_t, EARFCN){ hoFired = true; });

    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{3, MeasEventType::A5, RrcTriggerQty::RSRP, 3, 50, 60, 160};
    rrc.sendMeasurementConfig(r1, mo, rc);

    // A5: serving=42 < thr1=50 BUT neigh=58 < thr2=60 → NOT triggered
    auto mr = makeMeasResult(r1, 3, 42, {{320, 1800, 58}});
    rrc.processMeasurementReport(mr);

    assert(!hoFired);
    rrc.releaseConnection(r1);
    std::puts("test_a5_not_triggered_neigh_below_thr2 PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9: Only matching measId config evaluates, other ignored
static void test_only_matching_measid_fires() {
    LTERrc rrc;
    const RNTI r1 = 180;
    connectUE(rrc, r1);

    int hoCalls = 0;
    rrc.setHandoverCallback([&](RNTI, uint16_t, EARFCN){ ++hoCalls; });

    // Two configs: ID=1 (A3 offset=5), ID=2 (A3 offset=100 — impossible to fire)
    rrc.sendMeasurementConfig(r1, {1, 3000, LTEBand::B3},
        {1, MeasEventType::A3, RrcTriggerQty::RSRP, 5, 45, 55, 160});
    rrc.sendMeasurementConfig(r1, {2, 3010, LTEBand::B3},
        {2, MeasEventType::A3, RrcTriggerQty::RSRP, 100, 45, 55, 160});

    // Report with measId=1 → A3 offset=5, neigh(65) >= serv(60)+5 → should fire
    auto mr1 = makeMeasResult(r1, 1, 60, {{330, 3050, 65}});
    rrc.processMeasurementReport(mr1);
    assert(hoCalls == 1);

    // Report with measId=2 → A3 offset=100, neigh(65) >= serv(60)+100 false → won't fire
    auto mr2 = makeMeasResult(r1, 2, 60, {{330, 3050, 65}});
    rrc.processMeasurementReport(mr2);
    assert(hoCalls == 1);  // unchanged

    rrc.releaseConnection(r1);
    std::puts("test_only_matching_measid_fires PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10: No crash when report received for unknown RNTI
static void test_unknown_rnti_no_crash() {
    LTERrc rrc;
    // No UE registered
    LTERrcMeasResult mr{};
    mr.rnti = 999;  mr.measId = 1;  mr.rsrp_q = 60;
    mr.neighbours.push_back({400, 3050, 80, 20});
    rrc.processMeasurementReport(mr);  // must NOT crash
    std::puts("test_unknown_rnti_no_crash PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    test_measconfig_stored_per_ue();
    test_a3_not_triggered_below_offset();
    test_a3_triggered_above_offset();
    test_a1_good_coverage();
    test_a2_poor_coverage_triggers_ho();
    test_a2_not_triggered_above_threshold();
    test_a5_inter_freq_ho();
    test_a5_not_triggered_neigh_below_thr2();
    test_only_matching_measid_fires();
    test_unknown_rnti_no_crash();
    std::puts("ALL test_lte_measurement PASSED");
    return 0;
}

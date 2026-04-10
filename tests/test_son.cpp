// ─────────────────────────────────────────────────────────────────────────────
// Unit tests for rbs::SON — ANR, MLB, MRO.
// Refs: 3GPP TS 36.902 §5.2 (ANR), §5.3 (MLB), §5.4 (MRO).
// ─────────────────────────────────────────────────────────────────────────────
#include "../src/common/son.h"
#include <cassert>
#include <cstdio>
#include <cmath>   // std::fabs

using namespace rbs;

// ── helpers ───────────────────────────────────────────────────────────────────
static constexpr CellId CELL_LTE_A  = 100;
static constexpr CellId CELL_LTE_B  = 101;
static constexpr CellId CELL_LTE_C  = 102;
static constexpr CellId CELL_GSM_D  = 200;
static constexpr IMSI   UE1         = 310150000000001ULL;
static constexpr IMSI   UE2         = 310150000000002ULL;

static NeighbourCell makeLTENeighbour(CellId id, float rsrp = -80.0f) {
    return NeighbourCell{id, RAT::LTE, rsrp, -9.0f};
}
static NeighbourCell makeGSMNeighbour(CellId id, float rsrp = -75.0f) {
    return NeighbourCell{id, RAT::GSM, rsrp, -8.0f};
}

// ── ANR tests ─────────────────────────────────────────────────────────────────

static void testANR_addAndGet() {
    SON son;
    assert(son.neighbourCount(CELL_LTE_A) == 0);

    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_B));
    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_C));
    son.addNeighbour(CELL_LTE_A, makeGSMNeighbour(CELL_GSM_D));
    assert(son.neighbourCount(CELL_LTE_A) == 3);

    auto nbs = son.getNeighbours(CELL_LTE_A);
    assert(nbs.size() == 3);

    // Check per-RAT entries
    int lteCount = 0, gsmCount = 0;
    for (const auto& nb : nbs) {
        if (nb.rat == RAT::LTE) ++lteCount;
        if (nb.rat == RAT::GSM) ++gsmCount;
    }
    assert(lteCount == 2);
    assert(gsmCount == 1);

    std::puts("  PASS testANR_addAndGet");
}

static void testANR_overwrite() {
    SON son;
    son.addNeighbour(CELL_LTE_A, NeighbourCell{CELL_LTE_B, RAT::LTE, -90.0f, -10.0f});
    // Overwrite with better RSRP
    son.addNeighbour(CELL_LTE_A, NeighbourCell{CELL_LTE_B, RAT::LTE, -75.0f, -8.0f});
    assert(son.neighbourCount(CELL_LTE_A) == 1);  // still 1 entry

    auto nbs = son.getNeighbours(CELL_LTE_A);
    assert(nbs[0].rsrp_dBm == -75.0f);

    std::puts("  PASS testANR_overwrite");
}

static void testANR_remove() {
    SON son;
    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_B));
    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_C));
    assert(son.neighbourCount(CELL_LTE_A) == 2);

    son.removeNeighbour(CELL_LTE_A, CELL_LTE_B);
    assert(son.neighbourCount(CELL_LTE_A) == 1);
    assert(son.getNeighbours(CELL_LTE_A)[0].cellId == CELL_LTE_C);

    // No-op for unknown cell
    son.removeNeighbour(CELL_LTE_A, 9999);
    assert(son.neighbourCount(CELL_LTE_A) == 1);

    std::puts("  PASS testANR_remove");
}

static void testANR_autoDiscover() {
    SON son;
    // Measurement above threshold → auto-add
    son.reportMeasurement(CELL_LTE_A, UE1,
                          NeighbourCell{CELL_LTE_B, RAT::LTE, -95.0f, -9.5f});
    assert(son.neighbourCount(CELL_LTE_A) == 1);

    // Duplicate measurement → update, still 1 entry
    son.reportMeasurement(CELL_LTE_A, UE2,
                          NeighbourCell{CELL_LTE_B, RAT::LTE, -88.0f, -8.0f});
    assert(son.neighbourCount(CELL_LTE_A) == 1);
    assert(std::fabs(son.getNeighbours(CELL_LTE_A)[0].rsrp_dBm - (-88.0f)) < 0.01f);

    // Measurement below threshold → ignored
    son.reportMeasurement(CELL_LTE_A, UE1,
                          NeighbourCell{CELL_LTE_C, RAT::LTE, -115.0f, -15.0f});
    assert(son.neighbourCount(CELL_LTE_A) == 1);

    // Inter-RAT measurement (GSM) → also auto-added if above threshold
    son.reportMeasurement(CELL_LTE_A, UE1,
                          NeighbourCell{CELL_GSM_D, RAT::GSM, -85.0f, -7.0f});
    assert(son.neighbourCount(CELL_LTE_A) == 2);

    std::puts("  PASS testANR_autoDiscover");
}

// ── MLB tests ─────────────────────────────────────────────────────────────────

static void testMLB_updateAndGet() {
    SON son;
    assert(!son.getCellLoad(CELL_LTE_A).has_value());

    son.updateCellLoad(CELL_LTE_A, 45, 30);
    auto load = son.getCellLoad(CELL_LTE_A);
    assert(load.has_value());
    assert(load->dlPrbPct == 45);
    assert(load->ulPrbPct == 30);

    // Update
    son.updateCellLoad(CELL_LTE_A, 82, 50);
    assert(son.getCellLoad(CELL_LTE_A)->dlPrbPct == 82);

    std::puts("  PASS testMLB_updateAndGet");
}

static void testMLB_isOverloaded() {
    SON son;
    son.updateCellLoad(CELL_LTE_A, 79, 40);
    assert(!son.isOverloaded(CELL_LTE_A));           // default threshold=80
    assert(!son.isOverloaded(CELL_LTE_A, 80));

    son.updateCellLoad(CELL_LTE_A, 80, 40);
    assert(son.isOverloaded(CELL_LTE_A));            // 80 >= 80

    son.updateCellLoad(CELL_LTE_A, 95, 60);
    assert(son.isOverloaded(CELL_LTE_A, 90));
    assert(!son.isOverloaded(CELL_LTE_A, 96));

    // Cell with no load report → never overloaded
    assert(!son.isOverloaded(CELL_LTE_B));

    std::puts("  PASS testMLB_isOverloaded");
}

static void testMLB_bestNeighbourForOffload() {
    SON son;
    // Build NCL for CELL_LTE_A
    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_B));
    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_C));

    // Only B has a load report
    son.updateCellLoad(CELL_LTE_B, 40, 20);
    auto best = son.getBestNeighbourForOffload(CELL_LTE_A);
    assert(best.has_value() && *best == CELL_LTE_B);

    // Add load for C — lower than B
    son.updateCellLoad(CELL_LTE_C, 25, 10);
    best = son.getBestNeighbourForOffload(CELL_LTE_A);
    assert(best.has_value() && *best == CELL_LTE_C);

    // No neighbours → nullopt
    assert(!son.getBestNeighbourForOffload(CELL_GSM_D).has_value());

    // Neighbours but no load data for any → nullopt
    SON son2;
    son2.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_B));
    assert(!son2.getBestNeighbourForOffload(CELL_LTE_A).has_value());

    std::puts("  PASS testMLB_bestNeighbourForOffload");
}

// ── MRO tests ─────────────────────────────────────────────────────────────────

static void testMRO_statsAccumulation() {
    SON son;
    // No stats yet → 1.0 success rate
    assert(son.getHOSuccessRate(CELL_LTE_A, CELL_LTE_B) == 1.0f);
    assert(son.getHOStats(CELL_LTE_A, CELL_LTE_B).successes == 0);

    son.reportHOSuccess(CELL_LTE_A, CELL_LTE_B);
    son.reportHOSuccess(CELL_LTE_A, CELL_LTE_B);
    son.reportHOSuccess(CELL_LTE_A, CELL_LTE_B);
    son.reportHOFailure(CELL_LTE_A, CELL_LTE_B);  // 3/4 = 0.75

    auto stats = son.getHOStats(CELL_LTE_A, CELL_LTE_B);
    assert(stats.successes == 3);
    assert(stats.failures  == 1);
    assert(std::fabs(stats.successRate() - 0.75f) < 0.001f);

    // Stats are per cell-pair; reverse direction is independent
    assert(son.getHOStats(CELL_LTE_B, CELL_LTE_A).successes == 0);

    std::puts("  PASS testMRO_statsAccumulation");
}

static void testMRO_a3OffsetDelta() {
    SON son;

    // High success rate (>0.95) → +2 dB (relax trigger)
    for (int i = 0; i < 20; ++i) son.reportHOSuccess(CELL_LTE_A, CELL_LTE_B);
    assert(son.suggestA3OffsetDelta(CELL_LTE_A, CELL_LTE_B) == +2);

    // New pair: low success rate (<0.80) → -2 dB (tighter trigger)
    for (int i = 0; i < 2; ++i) son.reportHOSuccess(CELL_LTE_B, CELL_LTE_C);
    for (int i = 0; i < 8; ++i) son.reportHOFailure(CELL_LTE_B, CELL_LTE_C);
    // 2/10 = 0.20
    assert(son.suggestA3OffsetDelta(CELL_LTE_B, CELL_LTE_C) == -2);

    // Moderate: 8 success, 2 fail = 0.80 exactly → 0
    for (int i = 0; i < 8; ++i) son.reportHOSuccess(CELL_LTE_A, CELL_LTE_C);
    for (int i = 0; i < 2; ++i) son.reportHOFailure(CELL_LTE_A, CELL_LTE_C);
    // 0.80 is NOT < 0.80, so we expect 0
    assert(son.suggestA3OffsetDelta(CELL_LTE_A, CELL_LTE_C) == 0);

    // No stats → successRate == 1 > 0.95 → +2
    assert(son.suggestA3OffsetDelta(CELL_GSM_D, CELL_LTE_A) == +2);

    std::puts("  PASS testMRO_a3OffsetDelta");
}

// ── Reset test ────────────────────────────────────────────────────────────────

static void testReset() {
    SON son;
    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_B));
    son.updateCellLoad(CELL_LTE_A, 70, 40);
    son.reportHOSuccess(CELL_LTE_A, CELL_LTE_B);

    assert(son.neighbourCount(CELL_LTE_A) == 1);
    assert(son.getCellLoad(CELL_LTE_A).has_value());
    assert(son.getHOStats(CELL_LTE_A, CELL_LTE_B).successes == 1);

    son.reset();

    assert(son.neighbourCount(CELL_LTE_A) == 0);
    assert(!son.getCellLoad(CELL_LTE_A).has_value());
    assert(son.getHOStats(CELL_LTE_A, CELL_LTE_B).successes == 0);

    std::puts("  PASS testReset");
}

// ── End-to-end SON scenario ───────────────────────────────────────────────────

static void testScenario_overloadedCellOffload() {
    // Two LTE cells: A is overloaded, B is a known neighbour with spare capacity.
    // SON should recommend offloading UEs from A to B.
    SON son;

    son.addNeighbour(CELL_LTE_A, makeLTENeighbour(CELL_LTE_B));
    son.updateCellLoad(CELL_LTE_A, 92, 55);   // A: overloaded
    son.updateCellLoad(CELL_LTE_B, 35, 20);   // B: lightly loaded

    assert(son.isOverloaded(CELL_LTE_A));
    auto target = son.getBestNeighbourForOffload(CELL_LTE_A);
    assert(target.has_value() && *target == CELL_LTE_B);

    // Simulate 5 successful HOs to B, performance looks good
    for (int i = 0; i < 5; ++i) son.reportHOSuccess(CELL_LTE_A, CELL_LTE_B);
    assert(son.suggestA3OffsetDelta(CELL_LTE_A, CELL_LTE_B) == +2);

    // Load drops after offload
    son.updateCellLoad(CELL_LTE_A, 65, 40);
    assert(!son.isOverloaded(CELL_LTE_A));

    std::puts("  PASS testScenario_overloadedCellOffload");
}

static void testScenario_interRatANR() {
    // LTE cell measures a GSM and a UMTS neighbour via inter-RAT measurements.
    SON son;
    son.reportMeasurement(CELL_LTE_A, UE1,
                          NeighbourCell{CELL_GSM_D,  RAT::GSM,  -88.0f, -9.0f});
    son.reportMeasurement(CELL_LTE_A, UE2,
                          NeighbourCell{200+1, RAT::UMTS, -92.0f, -10.0f});
    assert(son.neighbourCount(CELL_LTE_A) == 2);

    auto nbs = son.getNeighbours(CELL_LTE_A);
    bool hasGSM  = false, hasUMTS = false;
    for (const auto& nb : nbs) {
        if (nb.rat == RAT::GSM)  hasGSM  = true;
        if (nb.rat == RAT::UMTS) hasUMTS = true;
    }
    assert(hasGSM && hasUMTS);

    std::puts("  PASS testScenario_interRatANR");
}

int main() {
    std::puts("=== test_son ===");

    // ANR
    testANR_addAndGet();
    testANR_overwrite();
    testANR_remove();
    testANR_autoDiscover();

    // MLB
    testMLB_updateAndGet();
    testMLB_isOverloaded();
    testMLB_bestNeighbourForOffload();

    // MRO
    testMRO_statsAccumulation();
    testMRO_a3OffsetDelta();

    // General
    testReset();
    testScenario_overloadedCellOffload();
    testScenario_interRatANR();

    std::puts("test_son PASSED");
    return 0;
}

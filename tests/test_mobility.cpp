// ─────────────────────────────────────────────────────────────────────────────
// Unit tests for rbs::MobilityManager (inter-RAT handover tracking).
// ─────────────────────────────────────────────────────────────────────────────
#include "../src/common/mobility_manager.h"
#include "../src/oms/oms.h"
#include <cassert>
#include <cstdio>

using namespace rbs;

// ── helpers ───────────────────────────────────────────────────────────────────
static constexpr IMSI UE1 = 311480000000001ULL;
static constexpr IMSI UE2 = 311480000000002ULL;
static constexpr IMSI UE3 = 311480000000003ULL;

// ── Test 1: register / lookup / unregister ────────────────────────────────────
static void testRegisterLookupUnregister() {
    MobilityManager mm;

    assert(mm.registeredUECount() == 0);
    assert(!mm.getUELocation(UE1).has_value());

    mm.registerUE(UE1, RAT::LTE, 0x0010, 103);
    assert(mm.registeredUECount() == 1);

    auto loc = mm.getUELocation(UE1);
    assert(loc.has_value());
    assert(loc->rat    == RAT::LTE);
    assert(loc->rnti   == 0x0010);
    assert(loc->cellId == 103);

    mm.unregisterUE(UE1);
    assert(mm.registeredUECount() == 0);
    assert(!mm.getUELocation(UE1).has_value());

    // Unregistering unknown UE is a no-op
    mm.unregisterUE(UE1);
    assert(mm.registeredUECount() == 0);

    std::puts("  PASS testRegisterLookupUnregister");
}

// ── Test 2: multiple UEs tracked independently ────────────────────────────────
static void testMultipleUEs() {
    MobilityManager mm;

    mm.registerUE(UE1, RAT::LTE,  0x0001, 10);
    mm.registerUE(UE2, RAT::UMTS, 0x0002, 20);
    mm.registerUE(UE3, RAT::GSM,  0x0003, 30);
    assert(mm.registeredUECount() == 3);

    assert(mm.getUELocation(UE1)->rat == RAT::LTE);
    assert(mm.getUELocation(UE2)->rat == RAT::UMTS);
    assert(mm.getUELocation(UE3)->rat == RAT::GSM);

    mm.unregisterUE(UE2);
    assert(mm.registeredUECount() == 2);
    assert(!mm.getUELocation(UE2).has_value());

    std::puts("  PASS testMultipleUEs");
}

// ── Test 3: successful inter-RAT handover LTE → GSM ──────────────────────────
static void testSuccessfulHandover() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::LTE, 0x0010, 103);

    assert(mm.handoverCount() == 0);

    // Callback simulates GSM stack admitting the UE and returning a new RNTI
    bool cbCalled = false;
    RNTI newGsmRnti = 0x0042;

    bool ok = mm.triggerHandover(UE1, RAT::GSM, 101,
        [&](IMSI imsi, RAT target) -> RNTI {
            cbCalled = true;
            assert(imsi   == UE1);
            assert(target == RAT::GSM);
            return newGsmRnti;
        });

    assert(ok);
    assert(cbCalled);
    assert(mm.handoverCount() == 1);

    auto loc = mm.getUELocation(UE1);
    assert(loc.has_value());
    assert(loc->rat    == RAT::GSM);
    assert(loc->rnti   == newGsmRnti);
    assert(loc->cellId == 101);

    std::puts("  PASS testSuccessfulHandover");
}

// ── Test 4: handover failure (callback returns 0) ─────────────────────────────
static void testHandoverFailure() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::LTE, 0x0010, 103);

    bool ok = mm.triggerHandover(UE1, RAT::UMTS, 102,
        [](IMSI, RAT) -> RNTI { return 0; });  // simulate admission failure

    assert(!ok);
    assert(mm.handoverCount() == 0);

    // UE record must be unchanged
    auto loc = mm.getUELocation(UE1);
    assert(loc.has_value());
    assert(loc->rat == RAT::LTE);

    std::puts("  PASS testHandoverFailure");
}

// ── Test 5: handover on unknown UE ───────────────────────────────────────────
static void testHandoverUnknownUE() {
    MobilityManager mm;

    bool ok = mm.triggerHandover(UE1, RAT::GSM, 101,
        [](IMSI, RAT) -> RNTI { return 0xAAAA; });

    assert(!ok);
    assert(mm.handoverCount() == 0);

    std::puts("  PASS testHandoverUnknownUE");
}

// ── Test 6: handover when already on target RAT ───────────────────────────────
static void testHandoverSameRAT() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::UMTS, 0x0005, 102);

    bool ok = mm.triggerHandover(UE1, RAT::UMTS, 102,
        [](IMSI, RAT) -> RNTI { return 0xBBBB; });

    assert(!ok);
    assert(mm.handoverCount() == 0);

    std::puts("  PASS testHandoverSameRAT");
}

// ── Test 7: chained handovers (LTE→UMTS→GSM) ─────────────────────────────────
static void testChainedHandovers() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::LTE, 0x0001, 103);

    // HO 1: LTE → UMTS
    bool ho1 = mm.triggerHandover(UE1, RAT::UMTS, 102,
        [](IMSI, RAT) -> RNTI { return 0x0002; });
    assert(ho1);
    assert(mm.getUELocation(UE1)->rat == RAT::UMTS);

    // HO 2: UMTS → GSM
    bool ho2 = mm.triggerHandover(UE1, RAT::GSM, 101,
        [](IMSI, RAT) -> RNTI { return 0x0003; });
    assert(ho2);
    assert(mm.getUELocation(UE1)->rat == RAT::GSM);

    assert(mm.handoverCount() == 2);

    std::puts("  PASS testChainedHandovers");
}

// ── Test 8: reset clears state ────────────────────────────────────────────────
static void testReset() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::LTE,  0x0001, 103);
    mm.registerUE(UE2, RAT::UMTS, 0x0002, 102);
    mm.triggerHandover(UE1, RAT::GSM, 101,
        [](IMSI, RAT) -> RNTI { return 0x000A; });

    assert(mm.registeredUECount() == 2);
    assert(mm.handoverCount()     == 1);

    mm.reset();
    assert(mm.registeredUECount() == 0);
    assert(mm.handoverCount()     == 0);
    assert(!mm.getUELocation(UE1).has_value());

    std::puts("  PASS testReset");
}

// ── Test 9: OMS counter integration ──────────────────────────────────────────
static void testOMSCounterIntegration() {
    auto& oms = oms::OMS::instance();
    MobilityManager mm;

    mm.registerUE(UE1, RAT::LTE, 0x0001, 103);
    mm.registerUE(UE2, RAT::LTE, 0x0002, 103);

    // Trigger HO for UE1
    mm.triggerHandover(UE1, RAT::GSM, 101,
        [](IMSI, RAT) -> RNTI { return 0x000A; });

    // Update OMS counter to reflect completed handovers
    oms.updateCounter("mobility.interRatHO",
                      static_cast<double>(mm.handoverCount()), "HOs");
    assert(oms.getCounter("mobility.interRatHO") == 1.0);

    // Trigger HO for UE2
    mm.triggerHandover(UE2, RAT::UMTS, 102,
        [](IMSI, RAT) -> RNTI { return 0x000B; });

    oms.updateCounter("mobility.interRatHO",
                      static_cast<double>(mm.handoverCount()), "HOs");
    assert(oms.getCounter("mobility.interRatHO") == 2.0);

    std::puts("  PASS testOMSCounterIntegration");
}

int main() {
    std::puts("=== test_mobility ===");
    testRegisterLookupUnregister();
    testMultipleUEs();
    testSuccessfulHandover();
    testHandoverFailure();
    testHandoverUnknownUE();
    testHandoverSameRAT();
    testChainedHandovers();
    testReset();
    testOMSCounterIntegration();
    std::puts("test_mobility PASSED");
    return 0;
}

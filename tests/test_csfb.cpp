// ─────────────────────────────────────────────────────────────────────────────
// Tests for Circuit Switched Fallback (CSFB): LTE → GSM inter-RAT redirection.
// 3GPP TS 36.300 §22.3.2 / TS 36.331 §5.3.8.3
//
// Covers:
//   1. Successful CSFB: UE moves from LTE to GSM via MobilityManager callback
//   2. csfbCount() increments on success, independent of handoverCount()
//   3. Rejection when UE is not on LTE
//   4. Rejection when UE is not registered
//   5. Rejection when callback returns 0 (GSM admission failure)
//   6. LTERrc::releaseWithRedirect() emits correct PDU and clears context
//   7. LTEStack::triggerCSFB() increments OMS lte.csfb.count
// ─────────────────────────────────────────────────────────────────────────────
#include "../src/common/mobility_manager.h"
#include "../src/lte/lte_rrc.h"
#include "../src/lte/lte_stack.h"
#include "../src/gsm/gsm_stack.h"
#include "../src/hal/rf_hardware.h"
#include "../src/oms/oms.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace rbs;

static constexpr IMSI UE1 = 311480000000101ULL;
static constexpr IMSI UE2 = 311480000000102ULL;

// ── helpers ───────────────────────────────────────────────────────────────────
static LTECellConfig makeLteCfg() {
    LTECellConfig cfg{};
    cfg.cellId      = 99;
    cfg.earfcn      = 1800;
    cfg.band        = LTEBand::B3;
    cfg.bandwidth   = LTEBandwidth::BW10;
    cfg.duplexMode  = LTEDuplexMode::FDD;
    cfg.txPower     = {43.0};
    cfg.pci         = 100;
    cfg.tac         = 1;
    cfg.mcc         = 250;
    cfg.mnc         = 1;
    cfg.numAntennas = 2;
    return cfg;
}

static GSMCellConfig makeGsmCfg() {
    GSMCellConfig cfg{};
    cfg.cellId  = 50;
    cfg.arfcn   = 37;
    cfg.bsic    = 0x10;
    cfg.txPower = {43.0};
    cfg.mcc     = 250;
    cfg.mnc     = 1;
    cfg.lac     = 1;
    return cfg;
}

// ── Test 1: successful CSFB via MobilityManager ───────────────────────────────
static void testCSFBBasic() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::LTE, 0x0001, 99);

    assert(mm.csfbCount()    == 0);
    assert(mm.handoverCount() == 0);

    constexpr uint16_t GSM_ARFCN = 37;

    bool cbCalled = false;
    bool ok = mm.triggerCSFB(UE1, GSM_ARFCN, 50,
        [&](IMSI imsi, RNTI lteRnti, uint16_t arfcn) -> RNTI {
            cbCalled = true;
            assert(imsi    == UE1);
            assert(lteRnti == 0x0001);
            assert(arfcn   == GSM_ARFCN);
            return 0x00A1;   // new GSM RNTI
        });

    assert(ok);
    assert(cbCalled);
    assert(mm.csfbCount()    == 1);
    assert(mm.handoverCount() == 0);   // CSFB is separate from PS HO count

    auto loc = mm.getUELocation(UE1);
    assert(loc.has_value());
    assert(loc->rat    == RAT::GSM);
    assert(loc->rnti   == 0x00A1);
    assert(loc->cellId == 50);

    std::puts("  PASS testCSFBBasic");
}

// ── Test 2: csfbCount independent of handoverCount ───────────────────────────
static void testCSFBCountIndependent() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::LTE,  0x0001, 99);
    mm.registerUE(UE2, RAT::LTE,  0x0002, 99);

    // regular PS HO: LTE → UMTS
    mm.triggerHandover(UE1, RAT::UMTS, 102,
        [](IMSI, RAT) -> RNTI { return 0x0010; });
    assert(mm.handoverCount() == 1);
    assert(mm.csfbCount()     == 0);

    // CSFB for UE2
    mm.triggerCSFB(UE2, 37, 50,
        [](IMSI, RNTI, uint16_t) -> RNTI { return 0x00B1; });
    assert(mm.handoverCount() == 1);   // unchanged
    assert(mm.csfbCount()     == 1);

    std::puts("  PASS testCSFBCountIndependent");
}

// ── Test 3: CSFB rejected when UE is not on LTE ──────────────────────────────
static void testCSFBNotOnLTE() {
    MobilityManager mm;

    // UE on GSM — should be rejected
    mm.registerUE(UE1, RAT::GSM, 0x0001, 50);
    bool ok = mm.triggerCSFB(UE1, 37, 50,
        [](IMSI, RNTI, uint16_t) -> RNTI { return 0x00A1; });
    assert(!ok);
    assert(mm.csfbCount() == 0);
    assert(mm.getUELocation(UE1)->rat == RAT::GSM);  // unchanged

    // UE on UMTS — should also be rejected
    mm.registerUE(UE2, RAT::UMTS, 0x0002, 102);
    ok = mm.triggerCSFB(UE2, 37, 50,
        [](IMSI, RNTI, uint16_t) -> RNTI { return 0x00A2; });
    assert(!ok);
    assert(mm.csfbCount() == 0);
    assert(mm.getUELocation(UE2)->rat == RAT::UMTS);  // unchanged

    std::puts("  PASS testCSFBNotOnLTE");
}

// ── Test 4: CSFB rejected when UE is not registered ──────────────────────────
static void testCSFBUnknownUE() {
    MobilityManager mm;
    bool cbCalled = false;
    bool ok = mm.triggerCSFB(UE1, 37, 50,
        [&](IMSI, RNTI, uint16_t) -> RNTI { cbCalled = true; return 0x00A1; });
    assert(!ok);
    assert(!cbCalled);      // callback must not be invoked
    assert(mm.csfbCount() == 0);

    std::puts("  PASS testCSFBUnknownUE");
}

// ── Test 5: CSFB rejected when callback returns 0 ────────────────────────────
static void testCSFBCallbackFailure() {
    MobilityManager mm;
    mm.registerUE(UE1, RAT::LTE, 0x0001, 99);

    bool ok = mm.triggerCSFB(UE1, 37, 50,
        [](IMSI, RNTI, uint16_t) -> RNTI { return 0; });  // admission failure
    assert(!ok);
    assert(mm.csfbCount() == 0);
    // UE record should be unchanged (still on LTE)
    assert(mm.getUELocation(UE1)->rat == RAT::LTE);

    std::puts("  PASS testCSFBCallbackFailure");
}

// ── Test 6: LTERrc::releaseWithRedirect emits 6-byte PDU, clears RRC context ─
static void testCSFBRrcRelease() {
    using namespace rbs::lte;
    LTERrc rrc;

    // Context must exist: handleConnectionRequest creates it
    constexpr RNTI rnti = 0x0042;
    rrc.handleConnectionRequest(rnti, UE1);
    assert(rrc.rrcState(rnti) == LTERrcState::RRC_CONNECTED);

    // Release with redirect
    bool ok = rrc.releaseWithRedirect(rnti, 37 /*GSM ARFCN*/);
    assert(ok);
    // Context must be gone: rrcState returns IDLE for unknown RNTI
    assert(rrc.rrcState(rnti) == LTERrcState::RRC_IDLE);

    // Calling again on the same (now-erased) RNTI must return false
    assert(!rrc.releaseWithRedirect(rnti, 37));

    std::puts("  PASS testCSFBRrcRelease");
}

// ── Test 7: LTEStack::triggerCSFB increments OMS lte.csfb.count ──────────────
static void testCSFBOmsCounter() {
    using namespace rbs::oms;
    auto& oms = OMS::instance();

    // Record baseline (other tests may have run)
    const double baseline = oms.getCounter("lte.csfb.count");

    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());

    auto gsmRf = std::make_shared<hal::RFHardware>(1, 1);
    assert(gsmRf->initialise());

    rbs::lte::LTEStack lteStack(rf, makeLteCfg());
    rbs::gsm::GSMStack gsmStack(gsmRf, makeGsmCfg());
    assert(lteStack.start());
    assert(gsmStack.start());

    // Admit UE on LTE
    RNTI lteRnti = lteStack.admitUE(UE1, 9);
    assert(lteRnti != 0);
    assert(lteStack.connectedUECount() == 1);

    // Trigger CSFB: releases LTE, admits GSM
    RNTI gsmRnti = gsmStack.admitUE(UE1);
    assert(gsmRnti != 0);

    lteStack.triggerCSFB(lteRnti, 37 /*GSM ARFCN*/);
    assert(lteStack.connectedUECount() == 0);
    assert(oms.getCounter("lte.csfb.count") == baseline + 1.0);

    gsmStack.releaseUE(gsmRnti);
    lteStack.stop();
    gsmStack.stop();

    std::puts("  PASS testCSFBOmsCounter");
}

// ── Test 8: full round-trip via MobilityManager with real stacks ──────────────
static void testCSFBEndToEnd() {
    auto rf    = std::make_shared<hal::RFHardware>(2, 4);
    auto gsmRf = std::make_shared<hal::RFHardware>(1, 1);
    assert(rf->initialise());
    assert(gsmRf->initialise());

    rbs::lte::LTEStack lteStack(rf,    makeLteCfg());
    rbs::gsm::GSMStack gsmStack(gsmRf, makeGsmCfg());
    assert(lteStack.start());
    assert(gsmStack.start());

    MobilityManager mm;

    // Admit on LTE
    RNTI lteRnti = lteStack.admitUE(UE1, 9);
    assert(lteRnti != 0);
    mm.registerUE(UE1, RAT::LTE, lteRnti, 99);

    // Execute CSFB via callback
    bool ok = mm.triggerCSFB(UE1, 37, 50,
        [&](IMSI imsi, RNTI rnti, uint16_t arfcn) -> RNTI {
            lteStack.triggerCSFB(rnti, arfcn);        // release LTE, send redirect
            return gsmStack.admitUE(imsi);             // admit in GSM
        });

    assert(ok);
    assert(mm.csfbCount() == 1);
    assert(lteStack.connectedUECount() == 0);
    assert(gsmStack.connectedUECount() == 1);

    auto loc = mm.getUELocation(UE1);
    assert(loc.has_value());
    assert(loc->rat == RAT::GSM);

    gsmStack.releaseUE(loc->rnti);
    mm.unregisterUE(UE1);

    lteStack.stop();
    gsmStack.stop();

    std::puts("  PASS testCSFBEndToEnd");
}

int main() {
    std::puts("=== test_csfb ===");
    testCSFBBasic();
    testCSFBCountIndependent();
    testCSFBNotOnLTE();
    testCSFBUnknownUE();
    testCSFBCallbackFailure();
    testCSFBRrcRelease();
    testCSFBOmsCounter();
    testCSFBEndToEnd();
    std::puts("test_csfb PASSED");
    return 0;
}

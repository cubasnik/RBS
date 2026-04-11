// test_nr_phy.cpp — п.17 5G NR PHY (SSB, SFN clock, PSS/SSS/PBCH)
// TS 38.211 §4 (frame), §7.4.2 (PSS/SSS), §7.4.3 (SSB)
// TS 38.213 §4.1 (SSB periodicity)
#include "../src/nr/nr_phy.h"
#include "../src/common/types.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <memory>

using namespace rbs;
using namespace rbs::nr;
using namespace rbs::hal;

static std::shared_ptr<RFHardware> makeRF() {
    return std::make_shared<RFHardware>();
}

static NRCellConfig makeNRCfg(uint32_t arfcn = 620000,
                               NRScs scs = NRScs::SCS30,
                               uint16_t pci = 42,
                               uint8_t ssbMs = 20)
{
    NRCellConfig cfg{};
    cfg.cellId         = 100;
    cfg.nrArfcn        = arfcn;
    cfg.scs            = scs;
    cfg.band           = 78;
    cfg.gnbDuId        = 0x1234567890ULL & 0xFFFFFFFFFULL;
    cfg.gnbCuId        = 0x0987654321ULL & 0xFFFFFFFFFULL;
    cfg.nrCellIdentity = 0xABCDEF123ULL & 0xFFFFFFFFFULL;
    cfg.nrPci          = pci;
    cfg.ssbPeriodMs    = ssbMs;
    cfg.tac            = 1;
    cfg.mcc            = 1;
    cfg.mnc            = 1;
    return cfg;
}

// ── Test 1: NRScs enum values and helpers ─────────────────────────
static void test_nr_scs_enum() {
    assert(static_cast<uint8_t>(NRScs::SCS15)  == 0);
    assert(static_cast<uint8_t>(NRScs::SCS30)  == 1);
    assert(static_cast<uint8_t>(NRScs::SCS60)  == 2);
    assert(static_cast<uint8_t>(NRScs::SCS120) == 3);
    assert(nrScsKhz(NRScs::SCS15)  == 15);
    assert(nrScsKhz(NRScs::SCS30)  == 30);
    assert(nrScsKhz(NRScs::SCS60)  == 60);
    assert(nrScsKhz(NRScs::SCS120) == 120);
    assert(nrSlotsPerFrame(NRScs::SCS15)  == 10);
    assert(nrSlotsPerFrame(NRScs::SCS30)  == 20);
    assert(nrSlotsPerFrame(NRScs::SCS60)  == 40);
    assert(nrSlotsPerFrame(NRScs::SCS120) == 80);
    std::puts("  test_nr_scs_enum PASSED");
}

// ── Test 2: NRCellConfig fields ───────────────────────────────────
static void test_nr_cell_config() {
    auto cfg = makeNRCfg();
    assert(cfg.nrArfcn == 620000);
    assert(cfg.scs == NRScs::SCS30);
    assert(cfg.nrPci == 42);
    assert(cfg.ssbPeriodMs == 20);
    assert(cfg.band == 78);
    std::puts("  test_nr_cell_config PASSED");
}

// ── Test 3: NRPhy start / stop ────────────────────────────────────
static void test_nr_phy_start_stop() {
    auto rf = makeRF();
    NRPhy phy(rf, makeNRCfg());
    assert(!phy.isRunning());
    assert(phy.start());
    assert(phy.isRunning());
    assert(phy.start());  // idempotent
    phy.stop();
    assert(!phy.isRunning());
    phy.stop();           // idempotent
    std::puts("  test_nr_phy_start_stop PASSED");
}

// ── Test 4: SFN advances on tick ──────────────────────────────────
static void test_nr_phy_sfn_advances() {
    auto rf = makeRF();
    NRPhy phy(rf, makeNRCfg());
    phy.start();
    assert(phy.currentSFN() == 0);
    for (int i = 0; i < 10; ++i) phy.tick();   // 1 frame = 10 ms
    assert(phy.currentSFN() == 1);
    // 1024 frames = 10240 ticks → wraps to 0
    for (int i = 0; i < 1024 * 10 - 10; ++i) phy.tick();
    assert(phy.currentSFN() == 0);
    phy.stop();
    std::puts("  test_nr_phy_sfn_advances PASSED");
}

// ── Test 5: SSB generated at correct period ───────────────────────
static void test_nr_phy_ssb_period() {
    auto rf = makeRF();
    // SSB period = 20 ms, FR1 → 4 SSBs per burst
    NRPhy phy(rf, makeNRCfg(620000, NRScs::SCS15, 1, 20));

    std::vector<NRSSBlock> received;
    phy.setSSBCallback([&](const NRSSBlock& ssb) { received.push_back(ssb); });
    phy.start();
    for (int i = 0; i < 60; ++i) phy.tick();   // 60 ms → 3 bursts = 12 SSBs
    assert(received.size() == 12);
    assert(phy.ssbTxCount() == 12);
    phy.stop();
    std::puts("  test_nr_phy_ssb_period PASSED");
}

// ── Test 6: PSS has correct length and non-trivial content ────────
static void test_nr_phy_pss_content() {
    auto rf = makeRF();
    auto cfg = makeNRCfg(620000, NRScs::SCS15, 0, 5);
    NRPhy phy(rf, cfg);
    NRSSBlock captured;
    phy.setSSBCallback([&](const NRSSBlock& s) { captured = s; });
    phy.start();
    phy.tick();   // first tick → SSB burst at t=0
    assert(!captured.pss.empty());
    assert(captured.pss.size()  == 127);
    assert(captured.sss.size()  == 127);
    assert(!captured.pbch.empty());
    bool allZero = true;
    for (auto v : captured.pss) if (v != 0) { allZero = false; break; }
    assert(!allZero);
    phy.stop();
    std::puts("  test_nr_phy_pss_content PASSED");
}

// ── Test 7: Different PCIs produce different PSS/SSS ──────────────
static void test_nr_phy_pci_unique_signals() {
    auto rf = makeRF();
    NRSSBlock ssb0, ssb1, ssb503;

    auto run = [&](uint16_t pci, NRSSBlock& out) {
        auto cfg = makeNRCfg(620000, NRScs::SCS15, pci, 5);
        NRPhy phy(rf, cfg);
        phy.setSSBCallback([&](const NRSSBlock& s) { out = s; });
        phy.start(); phy.tick(); phy.stop();
    };
    run(0, ssb0); run(1, ssb1); run(503, ssb503);
    assert(ssb0.pss != ssb1.pss || ssb0.sss != ssb1.sss);
    assert(ssb0.pss != ssb503.pss || ssb0.sss != ssb503.sss);
    std::puts("  test_nr_phy_pci_unique_signals PASSED");
}

// ── Test 8: PBCH encodes SFN correctly ───────────────────────────
static void test_nr_phy_pbch_sfn_encoding() {
    auto rf = makeRF();
    auto cfg = makeNRCfg(620000, NRScs::SCS30, 100, 5);
    NRPhy phy(rf, cfg);
    std::vector<NRSSBlock> ssbs;
    phy.setSSBCallback([&](const NRSSBlock& s) { ssbs.push_back(s); });
    phy.start();
    for (int i = 0; i < 15; ++i) phy.tick();   // 3 bursts
    assert(!ssbs.empty());
    // First burst: SFN=0 → top 4 bits of SFN in PBCH[0] bits 5:2 == 0
    assert(((ssbs[0].pbch[0] >> 2) & 0x0F) == 0);
    phy.stop();
    std::puts("  test_nr_phy_pbch_sfn_encoding PASSED");
}

// ── Test 9: SSB beam indices 0-3 per burst ───────────────────────
static void test_nr_phy_ssb_indices() {
    auto rf = makeRF();
    auto cfg = makeNRCfg(620000, NRScs::SCS15, 42, 5);
    NRPhy phy(rf, cfg);
    std::vector<uint8_t> indices;
    phy.setSSBCallback([&](const NRSSBlock& s) { indices.push_back(s.ssbIdx); });
    phy.start();
    phy.tick();   // first burst: 4 beams
    assert(indices.size() == 4);
    for (uint8_t i = 0; i < 4; ++i) assert(indices[i] == i);
    phy.stop();
    std::puts("  test_nr_phy_ssb_indices PASSED");
}

// ── Test 10: RAT enum includes NR ────────────────────────────────
static void test_rat_nr_enum() {
    assert(static_cast<uint8_t>(RAT::NR) == 3);
    assert(std::string(ratToString(RAT::NR)) == "NR");
    std::puts("  test_rat_nr_enum PASSED");
}

int main() {
    std::puts("=== test_nr_phy ===");
    test_nr_scs_enum();
    test_nr_cell_config();
    test_nr_phy_start_stop();
    test_nr_phy_sfn_advances();
    test_nr_phy_ssb_period();
    test_nr_phy_pss_content();
    test_nr_phy_pci_unique_signals();
    test_nr_phy_pbch_sfn_encoding();
    test_nr_phy_ssb_indices();
    test_rat_nr_enum();
    std::puts("test_nr_phy PASSED");
    return 0;
}

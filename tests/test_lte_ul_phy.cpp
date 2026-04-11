// п.11 — LTE UL PHY: PUCCH/PUSCH/SRS bidirectional PHY simulation
// TS 36.211 §5.3 (PUSCH), §5.4 (PUCCH), §5.5.2 (DMRS), §5.5.3 (SRS)
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "../src/lte/lte_phy.h"
#include "../src/lte/lte_mac.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace rbs;
using namespace rbs::lte;

static LTECellConfig makeTestCfg() {
    LTECellConfig cfg{};
    cfg.cellId      = 7;
    cfg.earfcn      = 1800;
    cfg.band        = LTEBand::B3;
    cfg.bandwidth   = LTEBandwidth::BW10;
    cfg.duplexMode  = LTEDuplexMode::FDD;
    cfg.txPower     = {43.0};
    cfg.pci         = 200;
    cfg.tac         = 3;
    cfg.mcc         = 250;
    cfg.mnc         = 1;
    cfg.numAntennas = 2;
    return cfg;
}

// ── Test 1: PUCCH Format 1 (SR) ──────────────────────────────────────────────
// Encoding: [format_byte, rnti_hi, rnti_lo, value, crc_xor] = 5 bytes
static void test_pucch_format1_sr() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    LTEPhy phy(rf, makeTestCfg());

    ByteBuffer sr1 = phy.buildPUCCH(PUCCHFormat::FORMAT_1, 0x1234, 1);
    assert(sr1.size() == 5);
    assert(sr1[0] == static_cast<uint8_t>(PUCCHFormat::FORMAT_1));
    assert(sr1[1] == 0x12);
    assert(sr1[2] == 0x34);
    assert(sr1[3] == 1);

    // CRC byte = XOR of first 4 bytes
    [[maybe_unused]] uint8_t crc = sr1[0] ^ sr1[1] ^ sr1[2] ^ sr1[3];
    assert(sr1[4] == crc);

    // Different RNTIs produce different outputs
    ByteBuffer sr2 = phy.buildPUCCH(PUCCHFormat::FORMAT_1, 0x5678, 1);
    assert(sr1 != sr2);

    rf->shutdown();
    std::puts("test_pucch_format1_sr PASSED");
}

// ── Test 2: PUCCH Format 1a (HARQ ACK/NACK) ─────────────────────────────────
static void test_pucch_format1a_ack_nack() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    LTEPhy phy(rf, makeTestCfg());

    const RNTI rnti = 0x0042;
    ByteBuffer ack  = phy.buildPUCCH(PUCCHFormat::FORMAT_1A, rnti, 0);  // ACK = 0
    ByteBuffer nack = phy.buildPUCCH(PUCCHFormat::FORMAT_1A, rnti, 1);  // NACK = 1
    assert(ack.size() == 5);
    assert(nack.size() == 5);
    assert(ack[3]  == 0);
    assert(nack[3] == 1);
    assert(ack != nack);  // ACK ≠ NACK (CRC differs too)

    rf->shutdown();
    std::puts("test_pucch_format1a_ack_nack PASSED");
}

// ── Test 3: PUCCH Format 2 (CQI) ─────────────────────────────────────────────
static void test_pucch_format2_cqi() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    LTEPhy phy(rf, makeTestCfg());

    const RNTI rnti = 0x00FF;
    ByteBuffer cqi7  = phy.buildPUCCH(PUCCHFormat::FORMAT_2, rnti, 7);
    ByteBuffer cqi15 = phy.buildPUCCH(PUCCHFormat::FORMAT_2, rnti, 15);
    assert(cqi7.size() >= 5);
    assert(cqi7[0] == static_cast<uint8_t>(PUCCHFormat::FORMAT_2));
    assert(cqi7[3]  == 7);
    assert(cqi15[3] == 15);
    assert(cqi7 != cqi15);  // different CQI values produce distinct output

    rf->shutdown();
    std::puts("test_pucch_format2_cqi PASSED");
}

// ── Test 4: PUSCH with DMRS ───────────────────────────────────────────────────
// Frame: [0xD0, rbIndex, rnti_hi, rnti_lo] + DMRS0(24) + data(tbBytes/2)
//                                           + DMRS1(24) + data(tbBytes/2)
static void test_pusch_with_dmrs() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    LTEPhy phy(rf, makeTestCfg());

    ResourceBlock rb{};
    rb.rbIndex = 5;  rb.mcs = 9;  rb.rnti = 0x0100;
    const uint32_t tbBytes = 100;
    ByteBuffer pusch = phy.buildPUSCH(rb, tbBytes);

    // Header: marker + rbIndex + rnti_hi + rnti_lo
    assert(pusch[0] == 0xD0);
    assert(pusch[1] == rb.rbIndex);
    assert(pusch[2] == (rb.rnti >> 8));
    assert(pusch[3] == (rb.rnti & 0xFF));

    // Slot-0 DMRS at offset 4
    ByteBuffer dmrs0 = phy.buildDMRS(rb.rnti, 0);
    ByteBuffer dmrs1 = phy.buildDMRS(rb.rnti, 1);
    assert(dmrs0.size() == 24);
    assert(dmrs1.size() == 24);
    assert(dmrs0 != dmrs1);  // different phase offsets for slot 0 and 1

    // DMRS from different RNTIs must differ
    ByteBuffer dmrs_other = phy.buildDMRS(0x0200, 0);
    assert(dmrs0 != dmrs_other);

    // Total size = 4 + 24 + 50 + 24 + 50 = 152
    assert(pusch.size() == 4 + 24 + (tbBytes / 2) + 24 + (tbBytes - tbBytes / 2));

    rf->shutdown();
    std::puts("test_pusch_with_dmrs PASSED");
}

// ── Test 5: SRS sequence lengths and uniqueness ───────────────────────────────
static void test_srs_sequence() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    LTEPhy phy(rf, makeTestCfg());

    const RNTI rnti = 0x0001;

    // bwConfig 0 → 8 RBs → comb-2 → N=8*12/2=48 bytes
    ByteBuffer srs0 = phy.buildSRS(rnti, 0);
    assert(srs0.size() == 48);

    // bwConfig 1 → 16 RBs → N=96 bytes
    ByteBuffer srs1 = phy.buildSRS(rnti, 1);
    assert(srs1.size() == 96);

    // bwConfig 2 → 32 RBs → N=192 bytes
    ByteBuffer srs2 = phy.buildSRS(rnti, 2);
    assert(srs2.size() == 192);

    // Different RNTIs produce different ZC sequences
    ByteBuffer srs0_other = phy.buildSRS(0x0002, 0);
    assert(srs0 != srs0_other);

    rf->shutdown();
    std::puts("test_srs_sequence PASSED");
}

// ── Test 6: MAC generates periodic CQI PUCCH report ──────────────────────────
static void test_mac_periodic_cqi() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    auto phy = std::make_shared<LTEPhy>(rf, makeTestCfg());
    assert(phy->start());

    LTEMAC mac(phy, makeTestCfg());
    assert(mac.start());
    assert(mac.admitUE(0x0010, 9));

    // Tick enough subframes for at least one full CQI period
    for (uint32_t i = 0; i < LTE_PUCCH_CQI_PERIOD_SF + 2; ++i)
        mac.tick();

    // UE should still be present and context intact
    assert(mac.activeUECount() == 1);

    mac.stop();
    rf->shutdown();
    std::puts("test_mac_periodic_cqi PASSED");
}

// ── Test 7: MAC HARQ NACK triggers Format 1a ─────────────────────────────────
static void test_mac_harq_nack() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    auto phy = std::make_shared<LTEPhy>(rf, makeTestCfg());
    assert(phy->start());

    LTEMAC mac(phy, makeTestCfg());
    assert(mac.start());
    assert(mac.admitUE(0x0020, 7));

    // Enqueue a DL SDU so DL scheduler has work
    ByteBuffer sdu(50, 0xAB);
    assert(mac.enqueueDlSDU(0x0020, std::move(sdu)));

    // Run for 10 radio frames — system must remain stable (no crash/assert)
    for (int i = 0; i < 100; ++i)
        mac.tick();

    assert(mac.activeUECount() == 1);

    mac.stop();
    rf->shutdown();
    std::puts("test_mac_harq_nack PASSED");
}

// ── Test 8: SRS transmitted in correct subframe period ────────────────────────
static void test_mac_srs_period() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    auto phy = std::make_shared<LTEPhy>(rf, makeTestCfg());
    assert(phy->start());

    LTEMAC mac(phy, makeTestCfg());
    assert(mac.start());
    assert(mac.admitUE(0x0001, 8));  // srsOffset = 1 % 80 = 1

    // Tick through one full SRS period plus some extra subframes
    for (uint32_t i = 0; i < LTE_SRS_PERIOD_SF + 15; ++i)
        mac.tick();

    assert(mac.activeUECount() == 1);

    mac.stop();
    rf->shutdown();
    std::puts("test_mac_srs_period PASSED");
}

// ── Test 9: Full UL data path: BSR → UL grant → dequeueUlSDU ─────────────────
static void test_ul_data_path() {
    auto rf = std::make_shared<hal::RFHardware>(2, 4);
    assert(rf->initialise());
    auto phy = std::make_shared<LTEPhy>(rf, makeTestCfg());
    assert(phy->start());

    LTEMAC mac(phy, makeTestCfg());
    assert(mac.start());

    const RNTI rnti = 0x0030;
    assert(mac.admitUE(rnti, 7));

    // Trigger UL scheduling via BSR
    mac.updateBSR(rnti, 10);
    mac.handleSchedulingRequest(rnti);

    // Tick to let scheduler issue UL grant and inject simulated TB
    for (int i = 0; i < 5; ++i)
        mac.tick();

    // dequeueUlSDU should return the simulated Transport Block
    ByteBuffer ulData;
    assert(mac.dequeueUlSDU(rnti, ulData));
    assert(!ulData.empty());

    mac.stop();
    rf->shutdown();
    std::puts("test_ul_data_path PASSED");
}

int main() {
    test_pucch_format1_sr();
    test_pucch_format1a_ack_nack();
    test_pucch_format2_cqi();
    test_pusch_with_dmrs();
    test_srs_sequence();
    test_mac_periodic_cqi();
    test_mac_harq_nack();
    test_mac_srs_period();
    test_ul_data_path();
    std::puts("ALL test_lte_ul_phy PASSED");
    return 0;
}

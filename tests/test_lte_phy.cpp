#include "../src/lte/lte_phy.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

int main() {
    rbs::LTECellConfig cfg{};
    cfg.cellId      = 5;
    cfg.earfcn      = 3100;
    cfg.band        = rbs::LTEBand::B7;
    cfg.bandwidth   = rbs::LTEBandwidth::BW10;
    cfg.duplexMode  = rbs::LTEDuplexMode::FDD;
    cfg.txPower     = {43.0};
    cfg.pci         = 100;
    cfg.tac         = 2;
    cfg.mcc         = 250;
    cfg.mnc         = 1;
    cfg.numAntennas = 2;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 4);
    assert(rf->initialise());

    rbs::lte::LTEPhy phy(rf, cfg);
    assert(phy.start());

    // Initial state: SFN=0, subframe=0
    assert(phy.currentSFN()           == 0);
    assert(phy.currentSubframeIndex() == 0);

    // 10 ticks  = one radio frame (SFN advances to 1)
    for (int i = 0; i < 10; ++i) phy.tick();
    assert(phy.currentSFN()           == 1);
    assert(phy.currentSubframeIndex() == 0);

    // 5 more ticks = half-frame
    for (int i = 0; i < 5; ++i) phy.tick();
    assert(phy.currentSubframeIndex() == 5);

    // RSRP reading is within plausible simulation range
    assert(phy.measuredRSRP() < 0.0);

    // transmit/receive subframe API
    rbs::LTESubframe dlSf{};
    dlSf.sfn            = phy.currentSFN();
    dlSf.subframeIndex  = phy.currentSubframeIndex();
    assert(phy.transmitSubframe(dlSf));

    rbs::LTESubframe ulSf{};
    assert(phy.receiveSubframe(ulSf));

    // numResourceBlocks for BW10 = 50 RBs
    assert(phy.numResourceBlocks() == 50);

    phy.stop();
    rf->shutdown();

    std::puts("test_lte_phy PASSED");
    return 0;
}

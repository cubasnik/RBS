#include "../src/gsm/gsm_phy.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

int main() {
    rbs::GSMCellConfig cfg{};
    cfg.cellId   = 1;
    cfg.arfcn    = 60;
    cfg.band     = rbs::GSMBand::DCS1800;
    cfg.txPower  = {43.0};
    cfg.bsic     = 10;
    cfg.lac      = 1000;
    cfg.mcc      = 250;
    cfg.mnc      = 1;

    auto rf  = std::make_shared<rbs::hal::RFHardware>(2, 2);
    assert(rf->initialise());

    rbs::gsm::GSMPhy phy(rf, cfg);
    assert(phy.start());

    // Run 16 ticks (two full TDMA frames)
    for (int i = 0; i < 16; ++i) phy.tick();

    assert(phy.currentFrameNumber() == 2);
    assert(phy.currentTimeSlot()    == 0);

    phy.stop();
    rf->shutdown();

    std::puts("test_gsm_phy PASSED");
    return 0;
}

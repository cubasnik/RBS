#include "../src/lte/lte_mac.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

int main() {
    rbs::LTECellConfig cfg{};
    cfg.cellId      = 3;
    cfg.earfcn      = 1800;
    cfg.band        = rbs::LTEBand::B3;
    cfg.bandwidth   = rbs::LTEBandwidth::BW20;
    cfg.duplexMode  = rbs::LTEDuplexMode::FDD;
    cfg.txPower     = {43.0};
    cfg.pci         = 300;
    cfg.tac         = 1;
    cfg.mcc         = 250;
    cfg.mnc         = 1;
    cfg.numAntennas = 2;

    auto rf  = std::make_shared<rbs::hal::RFHardware>(2, 4);
    assert(rf->initialise());

    auto phy = std::make_shared<rbs::lte::LTEPhy>(rf, cfg);
    assert(phy->start());

    rbs::lte::LTEMAC mac(phy, cfg);
    assert(mac.start());

    // Admit 3 UEs
    assert(mac.admitUE(1, 9));
    assert(mac.admitUE(2, 11));
    assert(mac.admitUE(3, 7));
    assert(mac.activeUECount() == 3);

    // Enqueue DL data for UE 1
    rbs::ByteBuffer sdu(100, 0xCC);
    assert(mac.enqueueDlSDU(1, sdu));

    // Run 5 subframes
    for (int i = 0; i < 5; ++i) mac.tick();

    // CQI / BSR updates
    mac.updateCQI(2, 13);
    mac.updateBSR(3, 10);
    mac.handleSchedulingRequest(3);

    // Release one UE
    assert(mac.releaseUE(2));
    assert(mac.activeUECount() == 2);

    // CQI → MCS mapping checks
    assert(rbs::lte::LTEMAC::cqiToMcs(1)  == 0);
    assert(rbs::lte::LTEMAC::cqiToMcs(7)  == 10);
    assert(rbs::lte::LTEMAC::cqiToMcs(15) == 28);

    mac.stop();
    phy->stop();
    rf->shutdown();

    std::puts("test_lte_mac PASSED");
    return 0;
}

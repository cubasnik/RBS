#include "../src/umts/umts_phy.h"
#include "../src/umts/umts_mac.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

int main() {
    rbs::UMTSCellConfig cfg{};
    cfg.cellId          = 2;
    cfg.uarfcn          = 10700;
    cfg.band            = rbs::UMTSBand::B1;
    cfg.txPower         = {43.0};
    cfg.primaryScrCode  = 42;
    cfg.lac             = 2;
    cfg.rac             = 1;
    cfg.mcc             = 250;
    cfg.mnc             = 1;

    auto rf  = std::make_shared<rbs::hal::RFHardware>(2, 4);
    assert(rf->initialise());

    auto phy = std::make_shared<rbs::umts::UMTSPhy>(rf, cfg);
    assert(phy->start());

    rbs::umts::UMTSMAC mac(phy, cfg);
    assert(mac.start());

    // Assign three DCH channels
    rbs::RNTI ue1 = mac.assignDCH();
    rbs::RNTI ue2 = mac.assignDCH(rbs::SF::SF32);
    rbs::RNTI ue3 = mac.assignDCH();
    assert(ue1 != 0);
    assert(ue2 != 0);
    assert(ue3 != 0);
    assert(ue1 != ue2);
    assert(mac.activeChannelCount() == 3);

    // Enqueue DL data for UE 1
    rbs::ByteBuffer sdu(80, 0xBB);
    assert(mac.enqueueDlData(ue1, sdu));

    // Run 5 TTIs
    for (int i = 0; i < 5; ++i) mac.tick();

    // Enqueue UL data path: deliverPdu via receive side
    rbs::ByteBuffer ulData;
    // Nothing in queue yet — dequeue returns false
    assert(!mac.dequeueUlData(ue2, ulData));

    // Release one channel
    assert(mac.releaseDCH(ue2));
    assert(mac.activeChannelCount() == 2);

    // Re-assign fills the gap
    rbs::RNTI ue4 = mac.assignDCH();
    assert(ue4 != 0);
    assert(mac.activeChannelCount() == 3);

    // Release remaining
    assert(mac.releaseDCH(ue1));
    assert(mac.releaseDCH(ue3));
    assert(mac.releaseDCH(ue4));
    assert(mac.activeChannelCount() == 0);

    // Double-release must fail
    assert(!mac.releaseDCH(ue1));

    mac.stop();
    phy->stop();
    rf->shutdown();

    std::puts("test_umts_mac PASSED");
    return 0;
}

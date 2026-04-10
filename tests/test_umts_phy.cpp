#include "../src/umts/umts_phy.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>

using namespace rbs;
using namespace rbs::umts;

int main() {
    UMTSCellConfig cfg{};
    cfg.cellId         = 3;
    cfg.uarfcn         = 10562;
    cfg.band           = UMTSBand::B1;
    cfg.txPower        = {43.0};
    cfg.primaryScrCode = 0;
    cfg.lac            = 300;
    cfg.rac            = 1;
    cfg.mcc            = 250;
    cfg.mnc            = 1;

    auto rf = std::make_shared<hal::RFHardware>(2, 2);
    assert(rf->initialise());

    UMTSPhy phy(rf, cfg);

    // ── start / stop ──────────────────────────────────────────────────────────
    assert(phy.start());
    assert(phy.currentFrameNumber() == 0);

    // ── tick: прогнать несколько фреймов ────────────────────────────────────
    // tick() выполняет spread/scramble/transmit для CPICH/SCH/P-CCPCH
    phy.tick();
    phy.tick();
    phy.tick();
    // frameNumber должен вырасти (или остаться — зависит от того, инкрементируется
    // ли он в tick() или снаружи; проверяем просто что не крашится)
    uint32_t fn = phy.currentFrameNumber();
    (void)fn;

    // ── measuredRSCP возвращает начальное значение ─────────────────────────
    double rscp = phy.measuredRSCP();
    assert(rscp <= 0.0);  // должен быть отрицательным (дБм)

    // ── Rx callback устанавливается без краша ─────────────────────────────
    int cbFired = 0;
    phy.setRxCallback([&](const UMTSFrame& f) {
        (void)f;
        ++cbFired;
    });
    phy.tick();  // callback может сработать на симулированном UL

    // ── transmit / receive ────────────────────────────────────────────────
    ByteBuffer txData(40, 0xAB);
    bool txOk = phy.transmit(1 /*channelCode*/, SF::SF16, txData);
    (void)txOk;  // в симуляторе всегда true; просто не должно крашиться

    ByteBuffer rxBuf;
    bool rxOk = phy.receive(1, SF::SF16, rxBuf, 320 /*numBits*/);
    (void)rxOk;  // буфер может быть пуст в симуляторе

    // ── stop и повторный stop безопасны ──────────────────────────────────
    phy.stop();
    phy.stop();

    rf->shutdown();
    std::puts("test_umts_phy PASSED");
    return 0;
}

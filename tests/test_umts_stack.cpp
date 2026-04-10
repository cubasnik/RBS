#include "../src/umts/umts_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>

int main() {
    rbs::UMTSCellConfig cfg{};
    cfg.cellId         = 3;
    cfg.uarfcn         = 10562;
    cfg.band           = rbs::UMTSBand::B1;
    cfg.txPower        = {43.0};
    cfg.primaryScrCode = 0;
    cfg.lac            = 300;
    cfg.rac            = 1;
    cfg.mcc            = 250;
    cfg.mnc            = 1;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 2);
    assert(rf->initialise());

    // ── start / stop ──────────────────────────────────────────────────────────
    rbs::umts::UMTSStack stack(rf, cfg);
    assert(!stack.isRunning());
    assert(stack.start());
    assert(stack.isRunning());

    // Дать стеку поработать один радио-фрейм WCDMA = 10 мс
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // ── admitUE ───────────────────────────────────────────────────────────────
    assert(stack.connectedUECount() == 0);
    rbs::RNTI r1 = stack.admitUE(100200300400500ULL, rbs::SF::SF16);
    assert(r1 != 0);
    assert(stack.connectedUECount() == 1);

    rbs::RNTI r2 = stack.admitUE(500400300200100ULL, rbs::SF::SF8);
    assert(r2 != 0);
    assert(r2 != r1);
    assert(stack.connectedUECount() == 2);

    // ── sendData ─────────────────────────────────────────────────────────────
    rbs::ByteBuffer payload(32, 0xAA);
    assert(stack.sendData(r1, payload));
    assert(stack.sendData(r2, rbs::ByteBuffer(16, 0xBB)));

    // ── receiveData не крашится (UL-буфер может быть пуст) ───────────────────
    rbs::ByteBuffer rxData;
    stack.receiveData(r1, rxData);

    // ── releaseUE ─────────────────────────────────────────────────────────────
    stack.releaseUE(r1);
    assert(stack.connectedUECount() == 1);

    stack.releaseUE(r2);
    assert(stack.connectedUECount() == 0);

    // ── printStats не крашится ────────────────────────────────────────────────
    stack.printStats();

    // ── stop ─────────────────────────────────────────────────────────────────
    stack.stop();
    assert(!stack.isRunning());
    stack.stop();   // повторный stop безопасен

    rf->shutdown();
    std::puts("test_umts_stack PASSED");
    return 0;
}

#include "../src/gsm/gsm_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>

int main() {
    rbs::GSMCellConfig cfg{};
    cfg.cellId  = 2;
    cfg.arfcn   = 60;
    cfg.band    = rbs::GSMBand::DCS1800;
    cfg.txPower = {43.0};
    cfg.bsic    = 5;
    cfg.lac     = 200;
    cfg.mcc     = 250;
    cfg.mnc     = 1;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 2);
    assert(rf->initialise());

    // ── start / stop ──────────────────────────────────────────────────────────
    rbs::gsm::GSMStack stack(rf, cfg);
    assert(!stack.isRunning());
    assert(stack.start());
    assert(stack.isRunning());

    // Дать стеку поработать один полный TDMA-фрейм (8 слотов × 577 мкс ≈ 5 мс)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // ── admitUE ───────────────────────────────────────────────────────────────
    assert(stack.connectedUECount() == 0);
    rbs::RNTI r1 = stack.admitUE(111222333444555ULL);
    assert(r1 != 0);
    assert(stack.connectedUECount() == 1);

    rbs::RNTI r2 = stack.admitUE(555444333222111ULL);
    assert(r2 != 0);
    assert(r2 != r1);
    assert(stack.connectedUECount() == 2);

    // ── sendData ─────────────────────────────────────────────────────────────
    rbs::ByteBuffer payload(20, 0xCC);
    assert(stack.sendData(r1, payload));
    assert(stack.sendData(r2, rbs::ByteBuffer(10, 0xDD)));

    // ── receiveData не крашится (UL-буфер может быть пуст) ───────────────────
    rbs::ByteBuffer rxData;
    stack.receiveData(r1, rxData);   // возвращаемое значение не проверяем

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
    std::puts("test_gsm_stack PASSED");
    return 0;
}

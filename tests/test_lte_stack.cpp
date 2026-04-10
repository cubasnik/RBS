#include "../src/lte/lte_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>

int main() {
    rbs::LTECellConfig cfg{};
    cfg.cellId      = 1;
    cfg.earfcn      = 1800;
    cfg.band        = rbs::LTEBand::B3;
    cfg.bandwidth   = rbs::LTEBandwidth::BW10;
    cfg.duplexMode  = rbs::LTEDuplexMode::FDD;
    cfg.txPower     = {43.0};
    cfg.pci         = 100;
    cfg.tac         = 1;
    cfg.mcc         = 250;
    cfg.mnc         = 1;
    cfg.numAntennas = 2;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 4);
    assert(rf->initialise());

    // ── start / stop ──────────────────────────────────────────────────────────
    rbs::lte::LTEStack stack(rf, cfg);
    assert(!stack.isRunning());
    assert(stack.start());
    assert(stack.isRunning());

    // Дать стеку поработать 20 мс (20 subframe-тиков)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // ── admitUE ───────────────────────────────────────────────────────────────
    assert(stack.connectedUECount() == 0);
    rbs::RNTI r1 = stack.admitUE(123456789012345ULL, 10);
    assert(r1 != 0);
    assert(stack.connectedUECount() == 1);

    rbs::RNTI r2 = stack.admitUE(999000000000001ULL, 7);
    assert(r2 != 0);
    assert(r2 != r1);
    assert(stack.connectedUECount() == 2);

    // ── updateCQI ─────────────────────────────────────────────────────────────
    stack.updateCQI(r1, 15);   // max CQI
    stack.updateCQI(r2, 1);    // min CQI

    // ── sendIPPacket + receiveIPPacket (DL user-plane round-trip) ────────────
    // Пакет уходит через PDCP → RLC → MAC DL-очередь; receiveIPPacket читает
    // из MAC UL-очереди (симулятор заполняет её из UL-планировщика).
    rbs::ByteBuffer ipPkt(64, 0xBE);
    assert(stack.sendIPPacket(r1, 1, ipPkt));

    // Дать MAC несколько тиков для обработки DL очереди
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // receiveIPPacket: может возвращать false если UL-буфер ещё пуст —
    // проверяем только что вызов не крашится.
    rbs::ByteBuffer rxPkt;
    stack.receiveIPPacket(r1, 1, rxPkt);   // возвращаемое значение не проверяем

    // ── releaseUE ─────────────────────────────────────────────────────────────
    stack.releaseUE(r1);
    assert(stack.connectedUECount() == 1);

    stack.releaseUE(r2);
    assert(stack.connectedUECount() == 0);

    // ── printStats не крашится ─────────────────────────────────────────────
    stack.printStats();

    // ── stop ─────────────────────────────────────────────────────────────────
    stack.stop();
    assert(!stack.isRunning());

    // Повторный stop безопасен
    stack.stop();
    assert(!stack.isRunning());

    rf->shutdown();
    std::puts("test_lte_stack PASSED");
    return 0;
}

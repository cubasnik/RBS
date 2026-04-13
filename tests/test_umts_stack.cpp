#include "../src/umts/umts_stack.h"
#include "../src/hal/rf_hardware.h"
#include "../src/common/link_registry.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <string>
#include <thread>

static bool traceContains(const std::vector<rbs::LinkMsg>& trace,
                          const std::string& type,
                          const std::string& summaryPart) {
    for (const auto& msg : trace) {
        if (msg.type == type && msg.summary.find(summaryPart) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static size_t traceCount(const std::vector<rbs::LinkMsg>& trace,
                         const std::string& type,
                         const std::string& summaryPart) {
    size_t count = 0;
    for (const auto& msg : trace) {
        if (msg.type == type && msg.summary.find(summaryPart) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

static void test_umts_stack_nbap_negative_lifecycle() {
    rbs::UMTSCellConfig cfg{};
    cfg.cellId         = 7;
    cfg.uarfcn         = 10562;
    cfg.band           = rbs::UMTSBand::B1;
    cfg.txPower        = {43.0};
    cfg.primaryScrCode = 8;
    cfg.lac            = 301;
    cfg.rac            = 1;
    cfg.mcc            = 250;
    cfg.mnc            = 1;
    cfg.rncAddr        = "127.0.0.1";
    cfg.rncPort        = 25471;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 2);
    assert(rf->initialise());

    rbs::umts::UMTSStack stack(rf, cfg);
    assert(stack.start());

    rbs::LinkEntry* iubEntry = rbs::LinkRegistry::instance().getLink("iub");
    assert(iubEntry != nullptr);
    assert(iubEntry->ctrl != nullptr);

    // Admit failure path: block NBAP RADIO_LINK_SETUP (0x50 => "NBAP:80").
    iubEntry->ctrl->blockMsg("NBAP:80");
    rbs::RNTI failedRnti = stack.admitUE(100200300400701ULL, rbs::SF::SF16);
    assert(failedRnti == 0);
    assert(stack.connectedUECount() == 0);
    iubEntry->ctrl->unblockMsg("NBAP:80");
    auto trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:80", "rnti=1"));
    assert(traceContains(trace, "NBAP:80", "blocked=1"));

    // Successful admit after unblock.
    rbs::RNTI rnti = stack.admitUE(100200300400702ULL, rbs::SF::SF16);
    assert(rnti != 0);
    assert(stack.connectedUECount() == 1);

    // Reconfigure failure path #1: block NBAP PREPARE (0x52 => "NBAP:82").
    iubEntry->ctrl->blockMsg("NBAP:82");
    assert(!stack.reconfigureDCH(rnti, rbs::SF::SF8));
    iubEntry->ctrl->unblockMsg("NBAP:82");
    // UE context must remain intact after failed reconfigure.
    assert(stack.connectedUECount() == 1);
    assert(stack.sendData(rnti, rbs::ByteBuffer(8, 0xAA)));
    trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:82", "rnti=" + std::to_string(rnti)));
    assert(traceContains(trace, "NBAP:82", "blocked=1"));

    // Reconfigure failure path #2: block NBAP COMMIT (0x53 => "NBAP:83").
    iubEntry->ctrl->blockMsg("NBAP:83");
    assert(!stack.reconfigureDCH(rnti, rbs::SF::SF4));
    iubEntry->ctrl->unblockMsg("NBAP:83");
    assert(stack.connectedUECount() == 1);
    assert(stack.sendData(rnti, rbs::ByteBuffer(8, 0xBB)));
    trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:83", "rnti=" + std::to_string(rnti)));
    assert(traceContains(trace, "NBAP:83", "blocked=1"));

    // Recovery path: reconfigure succeeds once blocks are removed.
    assert(stack.reconfigureDCH(rnti, rbs::SF::SF8));
    trace = iubEntry->ctrl->getTrace();
    assert(traceCount(trace, "NBAP:82", "rnti=" + std::to_string(rnti)) >= 2);
    assert(traceContains(trace, "NBAP:83", "rnti=" + std::to_string(rnti)));

    stack.releaseUE(rnti);
    assert(stack.connectedUECount() == 0);
    stack.stop();
    rf->shutdown();
    std::puts("  test_umts_stack_nbap_negative_lifecycle PASSED");
}

static void test_umts_stack_multi_ue_negative_reconfigure_isolation() {
    rbs::UMTSCellConfig cfg{};
    cfg.cellId         = 8;
    cfg.uarfcn         = 10562;
    cfg.band           = rbs::UMTSBand::B1;
    cfg.txPower        = {43.0};
    cfg.primaryScrCode = 16;
    cfg.lac            = 302;
    cfg.rac            = 1;
    cfg.mcc            = 250;
    cfg.mnc            = 1;
    cfg.rncAddr        = "127.0.0.1";
    cfg.rncPort        = 25472;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 2);
    assert(rf->initialise());

    rbs::umts::UMTSStack stack(rf, cfg);
    assert(stack.start());

    rbs::LinkEntry* iubEntry = rbs::LinkRegistry::instance().getLink("iub");
    assert(iubEntry != nullptr);
    assert(iubEntry->ctrl != nullptr);

    rbs::RNTI ue1 = stack.admitUE(100200300400801ULL, rbs::SF::SF16);
    rbs::RNTI ue2 = stack.admitUE(100200300400802ULL, rbs::SF::SF16);
    assert(ue1 != 0);
    assert(ue2 != 0);
    assert(ue1 != ue2);
    assert(stack.connectedUECount() == 2);

    // Fail reconfigure for UE1 only while block is active.
    iubEntry->ctrl->blockMsg("NBAP:82");
    assert(!stack.reconfigureDCH(ue1, rbs::SF::SF8));
    iubEntry->ctrl->unblockMsg("NBAP:82");

    // UE2 must remain unaffected and still support user-plane + successful reconfigure.
    assert(stack.connectedUECount() == 2);
    assert(stack.sendData(ue1, rbs::ByteBuffer(8, 0xC1)));
    assert(stack.sendData(ue2, rbs::ByteBuffer(8, 0xC2)));
    assert(stack.reconfigureDCH(ue2, rbs::SF::SF8));
    auto trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:82", "rnti=" + std::to_string(ue1)));
    assert(traceContains(trace, "NBAP:82", "blocked=1"));
    assert(traceContains(trace, "NBAP:82", "rnti=" + std::to_string(ue2)));
    assert(!traceContains(trace, "NBAP:82", "rnti=" + std::to_string(ue2) + " blocked=1"));

    // UE1 should also recover once the temporary block is gone.
    assert(stack.reconfigureDCH(ue1, rbs::SF::SF32));
    trace = iubEntry->ctrl->getTrace();
    assert(traceCount(trace, "NBAP:82", "rnti=" + std::to_string(ue1)) >= 2);

    // Releasing one UE must not disturb the survivor.
    stack.releaseUE(ue1);
    assert(stack.connectedUECount() == 1);
    assert(stack.sendData(ue2, rbs::ByteBuffer(8, 0xD2)));

    stack.releaseUE(ue2);
    assert(stack.connectedUECount() == 0);
    stack.stop();
    rf->shutdown();
    std::puts("  test_umts_stack_multi_ue_negative_reconfigure_isolation PASSED");
}

static void test_umts_stack_multi_ue_admit_failure_isolation() {
    rbs::UMTSCellConfig cfg{};
    cfg.cellId         = 9;
    cfg.uarfcn         = 10562;
    cfg.band           = rbs::UMTSBand::B1;
    cfg.txPower        = {43.0};
    cfg.primaryScrCode = 24;
    cfg.lac            = 303;
    cfg.rac            = 1;
    cfg.mcc            = 250;
    cfg.mnc            = 1;
    cfg.rncAddr        = "127.0.0.1";
    cfg.rncPort        = 25473;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 2);
    assert(rf->initialise());

    rbs::umts::UMTSStack stack(rf, cfg);
    assert(stack.start());

    rbs::LinkEntry* iubEntry = rbs::LinkRegistry::instance().getLink("iub");
    assert(iubEntry != nullptr);
    assert(iubEntry->ctrl != nullptr);

    // One UE admission fails under blocked RL setup.
    iubEntry->ctrl->blockMsg("NBAP:80");
    rbs::RNTI failed = stack.admitUE(100200300400901ULL, rbs::SF::SF16);
    assert(failed == 0);
    assert(stack.connectedUECount() == 0);
    iubEntry->ctrl->unblockMsg("NBAP:80");
    auto trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:80", "rnti=1"));
    assert(traceContains(trace, "NBAP:80", "blocked=1"));

    // Subsequent admissions must remain healthy and isolated from the failure.
    rbs::RNTI survivor1 = stack.admitUE(100200300400902ULL, rbs::SF::SF16);
    rbs::RNTI survivor2 = stack.admitUE(100200300400903ULL, rbs::SF::SF8);
    assert(survivor1 != 0);
    assert(survivor2 != 0);
    assert(survivor1 != survivor2);
    assert(stack.connectedUECount() == 2);
    assert(stack.sendData(survivor1, rbs::ByteBuffer(8, 0xE1)));
    assert(stack.sendData(survivor2, rbs::ByteBuffer(8, 0xE2)));
    trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:80", "rnti=" + std::to_string(survivor1)));
    assert(traceContains(trace, "NBAP:80", "rnti=" + std::to_string(survivor2)));

    stack.releaseUE(survivor1);
    assert(stack.connectedUECount() == 1);
    stack.releaseUE(survivor2);
    assert(stack.connectedUECount() == 0);
    stack.stop();
    rf->shutdown();
    std::puts("  test_umts_stack_multi_ue_admit_failure_isolation PASSED");
}

static void test_umts_stack_multi_ue_release_during_failure_isolation() {
    rbs::UMTSCellConfig cfg{};
    cfg.cellId         = 10;
    cfg.uarfcn         = 10562;
    cfg.band           = rbs::UMTSBand::B1;
    cfg.txPower        = {43.0};
    cfg.primaryScrCode = 32;
    cfg.lac            = 304;
    cfg.rac            = 1;
    cfg.mcc            = 250;
    cfg.mnc            = 1;
    cfg.rncAddr        = "127.0.0.1";
    cfg.rncPort        = 25474;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 2);
    assert(rf->initialise());

    rbs::umts::UMTSStack stack(rf, cfg);
    assert(stack.start());

    rbs::LinkEntry* iubEntry = rbs::LinkRegistry::instance().getLink("iub");
    assert(iubEntry != nullptr);
    assert(iubEntry->ctrl != nullptr);

    rbs::RNTI ue1 = stack.admitUE(100200300401001ULL, rbs::SF::SF16);
    rbs::RNTI ue2 = stack.admitUE(100200300401002ULL, rbs::SF::SF16);
    assert(ue1 != 0);
    assert(ue2 != 0);
    assert(stack.connectedUECount() == 2);

    // Reconfigure UE1 fails, then releasing UE2 must stay isolated and safe.
    iubEntry->ctrl->blockMsg("NBAP:82");
    assert(!stack.reconfigureDCH(ue1, rbs::SF::SF8));
    auto trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:82", "rnti=" + std::to_string(ue1)));
    assert(traceContains(trace, "NBAP:82", "blocked=1"));

    stack.releaseUE(ue2);
    assert(stack.connectedUECount() == 1);
    assert(stack.sendData(ue1, rbs::ByteBuffer(8, 0xF1)));
    trace = iubEntry->ctrl->getTrace();
    assert(traceContains(trace, "NBAP:84", "rnti=" + std::to_string(ue2)));
    assert(!traceContains(trace, "NBAP:84", "rnti=" + std::to_string(ue1)));

    iubEntry->ctrl->unblockMsg("NBAP:82");
    assert(stack.reconfigureDCH(ue1, rbs::SF::SF32));

    stack.releaseUE(ue1);
    assert(stack.connectedUECount() == 0);
    stack.stop();
    rf->shutdown();
    std::puts("  test_umts_stack_multi_ue_release_during_failure_isolation PASSED");
}

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
    cfg.rncAddr        = "127.0.0.1";
    cfg.rncPort        = 25470;

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

    // Завершаем базовый стек перед helper-сценариями, чтобы не держать несколько
    // живых UMTSStack с одним и тем же link registry entry одновременно.
    stack.stop();
    assert(!stack.isRunning());
    stack.stop();   // повторный stop безопасен

    rf->shutdown();

    test_umts_stack_nbap_negative_lifecycle();
    test_umts_stack_multi_ue_negative_reconfigure_isolation();
    test_umts_stack_multi_ue_admit_failure_isolation();
    test_umts_stack_multi_ue_release_during_failure_isolation();

    std::puts("test_umts_stack PASSED");
    return 0;
}

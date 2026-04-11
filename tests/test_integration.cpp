// ─────────────────────────────────────────────────────────────────────────────
// Integration test — exercises all three RAT stacks together with OMS.
//
// Scenario: start GSM + UMTS + LTE, admit 2 UEs per RAT, exchange data,
// raise/clear OMS alarms, verify counters, release UEs and stop.
// ─────────────────────────────────────────────────────────────────────────────
#include "../src/gsm/gsm_stack.h"
#include "../src/umts/umts_stack.h"
#include "../src/lte/lte_stack.h"
#include "../src/oms/oms.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>
#include <atomic>

using namespace rbs;

// ── Cell configs ──────────────────────────────────────────────────────────────
static GSMCellConfig makeGSMCfg() {
    GSMCellConfig c{};
    c.cellId  = 101;
    c.arfcn   = 60;
    c.band    = GSMBand::DCS1800;
    c.txPower = {43.0};
    c.bsic    = 5;
    c.lac     = 500;
    c.mcc     = 250;
    c.mnc     = 1;
    return c;
}

static UMTSCellConfig makeUMTSCfg() {
    UMTSCellConfig c{};
    c.cellId         = 102;
    c.uarfcn         = 10562;
    c.band           = UMTSBand::B1;
    c.txPower        = {43.0};
    c.primaryScrCode = 0;
    c.lac            = 500;
    c.rac            = 1;
    c.mcc            = 250;
    c.mnc            = 1;
    return c;
}

static LTECellConfig makeLTECfg() {
    LTECellConfig c{};
    c.cellId      = 103;
    c.earfcn      = 1800;
    c.band        = LTEBand::B3;
    c.bandwidth   = LTEBandwidth::BW10;
    c.duplexMode  = LTEDuplexMode::FDD;
    c.txPower     = {43.0};
    c.pci         = 100;
    c.tac         = 5;
    c.mcc         = 250;
    c.mnc         = 1;
    c.numAntennas = 2;
    return c;
}

int main() {
    // ── OMS setup ─────────────────────────────────────────────────────────────
    auto& oms = oms::OMS::instance();
    oms.setNodeState(oms::IOMS::NodeState::UNLOCKED);

    std::atomic<int> alarmCbs{0};
    oms.setAlarmCallback([&](const oms::Alarm&) { ++alarmCbs; });

    // ── RF hardware instances (separate per RAT) ──────────────────────────────
    auto gsmRF  = std::make_shared<hal::RFHardware>(2, 2);
    auto umtsRF = std::make_shared<hal::RFHardware>(2, 2);
    auto lteRF  = std::make_shared<hal::RFHardware>(2, 4);

    assert(gsmRF->initialise());
    assert(umtsRF->initialise());
    assert(lteRF->initialise());

    // Hook RF alarms into OMS
    auto rfAlarmCb = [&](HardwareStatus s, const std::string& msg) {
        oms::AlarmSeverity sev = (s == HardwareStatus::FAULT)
                                 ? oms::AlarmSeverity::CRITICAL
                                 : oms::AlarmSeverity::WARNING;
        oms.raiseAlarm("RFHardware", msg, sev);
    };
    gsmRF->setAlarmCallback(rfAlarmCb);
    umtsRF->setAlarmCallback(rfAlarmCb);
    lteRF->setAlarmCallback(rfAlarmCb);

    // ── Stack construction + start ─────────────────────────────────────────────
    gsm::GSMStack   gsmStack  (gsmRF,  makeGSMCfg());
    umts::UMTSStack umtsStack (umtsRF, makeUMTSCfg());
    lte::LTEStack   lteStack  (lteRF,  makeLTECfg());

    assert(gsmStack.start());
    assert(umtsStack.start());
    assert(lteStack.start());

    // Give threads 30 ms to stabilise
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    assert(gsmStack.isRunning());
    assert(umtsStack.isRunning());
    assert(lteStack.isRunning());

    // ── Admit UEs ─────────────────────────────────────────────────────────────
    // GSM: 2 UEs
    RNTI gsmR1 = gsmStack.admitUE(100000000000001ULL);
    RNTI gsmR2 = gsmStack.admitUE(100000000000002ULL);
    assert(gsmR1 != 0 && gsmR2 != 0 && gsmR1 != gsmR2);
    assert(gsmStack.connectedUECount() == 2);

    // UMTS: 2 UEs with different spreading factors
    RNTI umtsR1 = umtsStack.admitUE(200000000000001ULL, SF::SF16);
    RNTI umtsR2 = umtsStack.admitUE(200000000000002ULL, SF::SF8);
    assert(umtsR1 != 0 && umtsR2 != 0 && umtsR1 != umtsR2);
    assert(umtsStack.connectedUECount() == 2);

    // LTE: 2 UEs with different CQIs
    RNTI lteR1 = lteStack.admitUE(300000000000001ULL, 12);
    RNTI lteR2 = lteStack.admitUE(300000000000002ULL, 7);
    assert(lteR1 != 0 && lteR2 != 0 && lteR1 != lteR2);
    assert(lteStack.connectedUECount() == 2);

    // ── OMS performance counters — record UE counts ────────────────────────────
    oms.updateCounter("gsm.connectedUEs",
                      static_cast<double>(gsmStack.connectedUECount()),  "UEs");
    oms.updateCounter("umts.connectedUEs",
                      static_cast<double>(umtsStack.connectedUECount()), "UEs");
    oms.updateCounter("lte.connectedUEs",
                      static_cast<double>(lteStack.connectedUECount()),  "UEs");

    assert(oms.getCounter("gsm.connectedUEs")  == 2.0);
    assert(oms.getCounter("umts.connectedUEs") == 2.0);
    assert(oms.getCounter("lte.connectedUEs")  == 2.0);

    // ── Data flow ─────────────────────────────────────────────────────────────
    // GSM voice frames (13-byte RPE-LTP)
    ByteBuffer voiceFrame(13, 0xAA);
    assert(gsmStack.sendData(gsmR1, voiceFrame));
    assert(gsmStack.sendData(gsmR2, ByteBuffer(13, 0xBB)));

    // UMTS data (variable-size)
    assert(umtsStack.sendData(umtsR1, ByteBuffer(64, 0xCC)));
    assert(umtsStack.sendData(umtsR2, ByteBuffer(32, 0xDD)));

    // LTE IP packets
    assert(lteStack.sendIPPacket(lteR1, 1, ByteBuffer(100, 0xEE)));
    assert(lteStack.sendIPPacket(lteR2, 1, ByteBuffer(100, 0xFF)));

    // CQI updates
    lteStack.updateCQI(lteR1, 15);
    lteStack.updateCQI(lteR2, 3);

    // Let stacks process 50 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // recvData / recvIPPacket — non-crashing (buffers may be empty in sim)
    ByteBuffer rx;
    gsmStack.receiveData(gsmR1, rx);
    umtsStack.receiveData(umtsR1, rx);
    lteStack.receiveIPPacket(lteR1, 1, rx);

    // ── OMS alarm lifecycle ────────────────────────────────────────────────────
    uint32_t a1 = oms.raiseAlarm("GSM-Cell-101",  "VSWR high",        oms::AlarmSeverity::MAJOR);
    [[maybe_unused]] uint32_t a2 = oms.raiseAlarm("UMTS-Cell-102", "Clock unlock",     oms::AlarmSeverity::CRITICAL);
    [[maybe_unused]] uint32_t a3 = oms.raiseAlarm("LTE-Cell-103",  "TX power reduced", oms::AlarmSeverity::WARNING);
    assert(a1 != 0 && a2 != 0 && a3 != 0);
    assert(oms.getActiveAlarms().size() == 3);

    oms.clearAlarm(a1);
    assert(oms.getActiveAlarms().size() == 2);

    oms.clearAllAlarms("UMTS-Cell-102");
    assert(oms.getActiveAlarms().size() == 1);

    oms.clearAllAlarms("LTE-Cell-103");
    assert(oms.getActiveAlarms().empty());

    // ── printStats / printPerformanceReport ───────────────────────────────────
    gsmStack.printStats();
    umtsStack.printStats();
    lteStack.printStats();
    oms.printPerformanceReport();

    // ── Release UEs ───────────────────────────────────────────────────────────
    gsmStack.releaseUE(gsmR1);
    gsmStack.releaseUE(gsmR2);
    assert(gsmStack.connectedUECount() == 0);

    umtsStack.releaseUE(umtsR1);
    umtsStack.releaseUE(umtsR2);
    assert(umtsStack.connectedUECount() == 0);

    lteStack.releaseUE(lteR1);
    lteStack.releaseUE(lteR2);
    assert(lteStack.connectedUECount() == 0);

    // ── Stop sequence ─────────────────────────────────────────────────────────
    oms.setNodeState(oms::IOMS::NodeState::SHUTTING_DOWN);

    lteStack.stop();
    umtsStack.stop();
    gsmStack.stop();

    assert(!lteStack.isRunning());
    assert(!umtsStack.isRunning());
    assert(!gsmStack.isRunning());

    lteRF->shutdown();
    umtsRF->shutdown();
    gsmRF->shutdown();

    oms.setNodeState(oms::IOMS::NodeState::LOCKED);
    assert(oms.getNodeState() == oms::IOMS::NodeState::LOCKED);

    // ── Final counters updated ─────────────────────────────────────────────────
    oms.updateCounter("gsm.connectedUEs",  0.0, "UEs");
    oms.updateCounter("umts.connectedUEs", 0.0, "UEs");
    oms.updateCounter("lte.connectedUEs",  0.0, "UEs");
    assert(oms.getCounter("gsm.connectedUEs")  == 0.0);
    assert(oms.getCounter("umts.connectedUEs") == 0.0);
    assert(oms.getCounter("lte.connectedUEs")  == 0.0);

    std::puts("test_integration PASSED");
    return 0;
}

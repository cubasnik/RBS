#include "common/logger.h"
#include "common/config.h"
#include "common/types.h"
#include "hal/rf_hardware.h"
#include "gsm/gsm_stack.h"
#include "umts/umts_stack.h"
#include "lte/lte_stack.h"
#include "oms/oms.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>

// ────────────────────────────────────────────────────────────────
// Signal handler for graceful shutdown (SIGINT / SIGTERM)
// ────────────────────────────────────────────────────────────────
static std::atomic<bool> gShutdown{false};

static void signalHandler(int sig) {
    RBS_LOG_INFO("RBS", "Signal ", sig, " received – initiating shutdown");
    gShutdown.store(true);
}

// ────────────────────────────────────────────────────────────────
// RBS – Radio Base Station top-level controller
// ────────────────────────────────────────────────────────────────
class RadioBaseStation {
public:
    explicit RadioBaseStation(const std::string& configPath) {
        auto& cfg = rbs::Config::instance();
        if (!configPath.empty()) cfg.loadFile(configPath);

        // Shared RF hardware (one frontend per RAT in a real system;
        // here we share one simulated instance to keep the demo compact)
        gsmRF_  = std::make_shared<rbs::hal::RFHardware>(2, 2);
        umtsRF_ = std::make_shared<rbs::hal::RFHardware>(2, 2);
        lteRF_  = std::make_shared<rbs::hal::RFHardware>(2, 4);

        // Wire hardware alarms into OMS
        auto alarmCb = [](rbs::HardwareStatus s, const std::string& msg) {
            rbs::oms::AlarmSeverity sev = rbs::oms::AlarmSeverity::WARNING;
            if (s == rbs::HardwareStatus::FAULT)
                sev = rbs::oms::AlarmSeverity::CRITICAL;
            rbs::oms::OMS::instance().raiseAlarm("RFHardware", msg, sev);
        };
        gsmRF_->setAlarmCallback(alarmCb);
        umtsRF_->setAlarmCallback(alarmCb);
        lteRF_->setAlarmCallback(alarmCb);

        // Build stacks from config
        gsmStack_  = std::make_unique<rbs::gsm::GSMStack> (gsmRF_,  cfg.buildGSMConfig());
        umtsStack_ = std::make_unique<rbs::umts::UMTSStack>(umtsRF_, cfg.buildUMTSConfig());
        lteStack_  = std::make_unique<rbs::lte::LTEStack>  (lteRF_,  cfg.buildLTEConfig());
    }

    // ── Lifecycle ─────────────────────────────────────────────────
    bool start() {
        rbs::oms::OMS::instance().setNodeState(rbs::oms::OMS::NodeState::UNLOCKED);

        // Initialise and self-test RF frontends
        for (auto& rf : {gsmRF_, umtsRF_, lteRF_}) {
            if (!rf->initialise() || !rf->selfTest()) {
                RBS_LOG_CRITICAL("RBS", "RF hardware initialisation failed – aborting");
                return false;
            }
        }

        // Start protocol stacks
        if (!gsmStack_->start())  { RBS_LOG_CRITICAL("RBS", "GSM stack start failed");  return false; }
        if (!umtsStack_->start()) { RBS_LOG_CRITICAL("RBS", "UMTS stack start failed"); return false; }
        if (!lteStack_->start())  { RBS_LOG_CRITICAL("RBS", "LTE stack start failed");  return false; }

        RBS_LOG_INFO("RBS", "====================================================");
        RBS_LOG_INFO("RBS", "  Radio Base Station ONLINE");
        RBS_LOG_INFO("RBS", "  GSM  cell ", gsmStack_->config().cellId,
                     "  ARFCN=",  gsmStack_->config().arfcn);
        RBS_LOG_INFO("RBS", "  UMTS cell ", umtsStack_->config().cellId,
                     "  UARFCN=", umtsStack_->config().uarfcn);
        RBS_LOG_INFO("RBS", "  LTE  cell ", lteStack_->config().cellId,
                     "  EARFCN=", lteStack_->config().earfcn,
                     "  PCI=",    lteStack_->config().pci);
        RBS_LOG_INFO("RBS", "====================================================");
        return true;
    }

    void stop() {
        RBS_LOG_INFO("RBS", "Stopping all cells...");
        rbs::oms::OMS::instance().setNodeState(rbs::oms::OMS::NodeState::SHUTTING_DOWN);
        lteStack_->stop();
        umtsStack_->stop();
        gsmStack_->stop();
        lteRF_->shutdown();
        umtsRF_->shutdown();
        gsmRF_->shutdown();
        rbs::oms::OMS::instance().setNodeState(rbs::oms::OMS::NodeState::LOCKED);
        RBS_LOG_INFO("RBS", "Radio Base Station OFFLINE");
    }

    // ── Demo: admit test UEs and exchange data ────────────────────
    void runDemo() {
        RBS_LOG_INFO("RBS", "--- Running multi-RAT demo ---");

        // Admit one UE per RAT
        rbs::RNTI gsmRnti  = gsmStack_->admitUE(100000000000001ULL);
        rbs::RNTI umtsRnti = umtsStack_->admitUE(200000000000002ULL);
        rbs::RNTI lteRnti  = lteStack_->admitUE(300000000000003ULL, 12);

        // Simulate CQI feedback for LTE UE
        lteStack_->updateCQI(lteRnti, 12);

        // Send IP packets over LTE
        for (int i = 0; i < 3; ++i) {
            rbs::ByteBuffer pkt(100, static_cast<uint8_t>(i));
            lteStack_->sendIPPacket(lteRnti, 1, pkt);
        }

        // Send voice data over GSM (TCH payload)
        rbs::ByteBuffer voice(13, 0xAA);
        gsmStack_->sendData(gsmRnti, voice);

        // Send user data over UMTS DCH
        rbs::ByteBuffer data(32, 0xBB);
        umtsStack_->sendData(umtsRnti, data);

        // Let stacks run for 2 seconds
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Print statistics
        gsmStack_->printStats();
        umtsStack_->printStats();
        lteStack_->printStats();
        rbs::oms::OMS::instance().printPerformanceReport();

        // Update OMS performance counters
        auto& oms = rbs::oms::OMS::instance();
        oms.updateCounter("gsm.connectedUEs",
                          static_cast<double>(gsmStack_->connectedUECount()));
        oms.updateCounter("umts.connectedUEs",
                          static_cast<double>(umtsStack_->connectedUECount()));
        oms.updateCounter("lte.connectedUEs",
                          static_cast<double>(lteStack_->connectedUECount()));

        // Release UEs
        gsmStack_->releaseUE(gsmRnti);
        umtsStack_->releaseUE(umtsRnti);
        lteStack_->releaseUE(lteRnti);
    }

    // ── Main loop ─────────────────────────────────────────────────
    void mainLoop() {
        auto& oms = rbs::oms::OMS::instance();
        uint32_t tick = 0;
        while (!gShutdown.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            ++tick;
            // Periodic PM report every 10 s
            oms.updateCounter("gsm.connectedUEs",
                              static_cast<double>(gsmStack_->connectedUECount()));
            oms.updateCounter("umts.connectedUEs",
                              static_cast<double>(umtsStack_->connectedUECount()));
            oms.updateCounter("lte.connectedUEs",
                              static_cast<double>(lteStack_->connectedUECount()));
            if (tick % 3 == 0) oms.printPerformanceReport();
        }
    }

private:
    std::shared_ptr<rbs::hal::RFHardware>   gsmRF_;
    std::shared_ptr<rbs::hal::RFHardware>   umtsRF_;
    std::shared_ptr<rbs::hal::RFHardware>   lteRF_;
    std::unique_ptr<rbs::gsm::GSMStack>     gsmStack_;
    std::unique_ptr<rbs::umts::UMTSStack>   umtsStack_;
    std::unique_ptr<rbs::lte::LTEStack>     lteStack_;
};

// ────────────────────────────────────────────────────────────────
// Entry point
// ────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // Set up logging
    rbs::Logger::instance().setLevel(rbs::LogLevel::INFO);
    rbs::Logger::instance().enableFile("rbs.log");

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string configPath = (argc > 1) ? argv[1] : "rbs.conf";

    RBS_LOG_INFO("RBS", "Radio Base Station v1.0.0 starting...");
    RBS_LOG_INFO("RBS", "Config: ", configPath);

    RadioBaseStation node(configPath);

    if (!node.start()) {
        RBS_LOG_CRITICAL("RBS", "Failed to start – exiting");
        return EXIT_FAILURE;
    }

    // Run a quick multi-RAT demonstration
    node.runDemo();

    // Stay alive until SIGINT/SIGTERM
    node.mainLoop();

    node.stop();
    return EXIT_SUCCESS;
}

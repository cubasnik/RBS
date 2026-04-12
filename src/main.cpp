#include "common/logger.h"
#include "common/config.h"
#include "common/types.h"
#include "hal/rf_hardware.h"
#include "gsm/gsm_stack.h"
#include "umts/umts_stack.h"
#include "lte/lte_stack.h"
#include "lte/x2ap_link.h"
#include "nr/nr_stack.h"
#include "oms/oms.h"
#include "api/rest_server.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <string>
#include <algorithm>

// ────────────────────────────────────────────────────────────────
// Signal handler for graceful shutdown (SIGINT / SIGTERM)
// ────────────────────────────────────────────────────────────────
static std::atomic<bool> gShutdown{false};

static void signalHandler(int sig) {
    RBS_LOG_INFO("RBS", "Signal ", sig, " received – initiating shutdown");
    gShutdown.store(true);
}

// ────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────
static const char* lteBwMHz(rbs::LTEBandwidth bw) {
    switch (bw) {
        case rbs::LTEBandwidth::BW1_4: return "1.4";
        case rbs::LTEBandwidth::BW3:   return "3";
        case rbs::LTEBandwidth::BW5:   return "5";
        case rbs::LTEBandwidth::BW10:  return "10";
        case rbs::LTEBandwidth::BW15:  return "15";
        case rbs::LTEBandwidth::BW20:  return "20";
    }
    return "?";
}

// ────────────────────────────────────────────────────────────────
// RAT selection
// ────────────────────────────────────────────────────────────────
enum class SelectedRAT { GSM, UMTS, LTE, NR, ALL };

static SelectedRAT parseRAT(const std::string& arg) {
    std::string s = arg;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "gsm")  return SelectedRAT::GSM;
    if (s == "umts") return SelectedRAT::UMTS;
    if (s == "lte")  return SelectedRAT::LTE;
    if (s == "nr")   return SelectedRAT::NR;
    return SelectedRAT::ALL;
}

static const char* ratName(SelectedRAT r) {
    switch (r) {
        case SelectedRAT::GSM:  return "GSM (2G)";
        case SelectedRAT::UMTS: return "UMTS (3G)";
        case SelectedRAT::LTE:  return "LTE (4G)";
        case SelectedRAT::NR:   return "NR (5G)";
        case SelectedRAT::ALL:  return "GSM + UMTS + LTE + NR";
    }
    return "";
}

// ────────────────────────────────────────────────────────────────
// RBS – Radio Base Station top-level controller
// ────────────────────────────────────────────────────────────────
class RadioBaseStation {
public:
    explicit RadioBaseStation(const std::string& configPath, SelectedRAT rat)
        : rat_(rat)
    {
        auto& cfg = rbs::Config::instance();
        if (!configPath.empty()) cfg.loadFile(configPath);

        restServer_ = std::make_unique<rbs::api::RestServer>(
            cfg.getInt("api", "port", 8080),
            cfg.getString("api", "bind", "127.0.0.1")
        );

        auto alarmCb = [](rbs::HardwareStatus s, const std::string& msg) {
            rbs::oms::AlarmSeverity sev = rbs::oms::AlarmSeverity::WARNING;
            if (s == rbs::HardwareStatus::FAULT)
                sev = rbs::oms::AlarmSeverity::CRITICAL;
            rbs::oms::OMS::instance().raiseAlarm("RFHardware", msg, sev);
        };

        if (rat_ == SelectedRAT::GSM || rat_ == SelectedRAT::ALL) {
            gsmRF_ = std::make_shared<rbs::hal::RFHardware>(2, 2);
            gsmRF_->setAlarmCallback(alarmCb);
            gsmStack_ = std::make_unique<rbs::gsm::GSMStack>(gsmRF_, cfg.buildGSMConfig());
        }
        if (rat_ == SelectedRAT::UMTS || rat_ == SelectedRAT::ALL) {
            umtsRF_ = std::make_shared<rbs::hal::RFHardware>(2, 2);
            umtsRF_->setAlarmCallback(alarmCb);
            umtsStack_ = std::make_unique<rbs::umts::UMTSStack>(umtsRF_, cfg.buildUMTSConfig());
        }
        if (rat_ == SelectedRAT::LTE || rat_ == SelectedRAT::ALL) {
            lteRF_ = std::make_shared<rbs::hal::RFHardware>(2, 4);
            lteRF_->setAlarmCallback(alarmCb);
            lteStack_ = std::make_unique<rbs::lte::LTEStack>(lteRF_, cfg.buildLTEConfig());
        }
        if (rat_ == SelectedRAT::NR || rat_ == SelectedRAT::ALL) {
            nrRF_ = std::make_shared<rbs::hal::RFHardware>(4, 4);
            nrRF_->setAlarmCallback(alarmCb);
            nrStack_ = std::make_unique<rbs::nr::NRStack>(nrRF_, cfg.buildNRConfig());
        }
    }

    // ── Lifecycle ─────────────────────────────────────────────────
    bool start() {
        rbs::oms::OMS::instance().setNodeState(rbs::oms::OMS::NodeState::UNLOCKED);

        if (!restServer_->start()) {
            RBS_LOG_CRITICAL("RBS", "Failed to start REST API server");
            return false;
        }

        auto initRF = [](std::shared_ptr<rbs::hal::RFHardware>& rf) -> bool {
            return rf && rf->initialise() && rf->selfTest();
        };

        if (gsmRF_  && !initRF(gsmRF_))  { RBS_LOG_CRITICAL("RBS", "GSM RF init failed");  return false; }
        if (umtsRF_ && !initRF(umtsRF_)) { RBS_LOG_CRITICAL("RBS", "UMTS RF init failed"); return false; }
        if (lteRF_  && !initRF(lteRF_))  { RBS_LOG_CRITICAL("RBS", "LTE RF init failed");  return false; }
        if (nrRF_   && !initRF(nrRF_))   { RBS_LOG_CRITICAL("RBS", "NR RF init failed");   return false; }

        if (gsmStack_  && !gsmStack_->start())  { RBS_LOG_CRITICAL("RBS", "GSM stack start failed");  return false; }
        if (umtsStack_ && !umtsStack_->start()) { RBS_LOG_CRITICAL("RBS", "UMTS stack start failed"); return false; }
        if (lteStack_  && !lteStack_->start())  { RBS_LOG_CRITICAL("RBS", "LTE stack start failed");  return false; }
        if (nrStack_   && !nrStack_->start())   { RBS_LOG_CRITICAL("RBS", "NR stack start failed");   return false; }

        RBS_LOG_INFO("RBS", "====================================================");
        RBS_LOG_INFO("RBS", "  Radio Base Station ONLINE  [", ratName(rat_), "]");
        if (gsmStack_)
            RBS_LOG_INFO("RBS", "  GSM  cell ", gsmStack_->config().cellId,
                         "  ARFCN=",  gsmStack_->config().arfcn,
                         "  DL=",     gsmStack_->config().arfcn * 0.2 + 935.0, " MHz");
        if (umtsStack_)
            RBS_LOG_INFO("RBS", "  UMTS cell ", umtsStack_->config().cellId,
                         "  UARFCN=", umtsStack_->config().uarfcn,
                         "  PSC=",    umtsStack_->config().primaryScrCode);
        if (lteStack_)
            RBS_LOG_INFO("RBS", "  LTE  cell ", lteStack_->config().cellId,
                         "  EARFCN=", lteStack_->config().earfcn,
                         "  PCI=",    lteStack_->config().pci,
                         "  BW=",     lteBwMHz(lteStack_->config().bandwidth), " MHz");
        if (nrStack_)
            RBS_LOG_INFO("RBS", "  NR   cell ", nrStack_->config().cellId,
                         "  NR-ARFCN=", nrStack_->config().nrArfcn,
                         "  PCI=",      nrStack_->config().nrPci,
                         "  SCS=",      rbs::nrScsKhz(nrStack_->config().scs), " kHz");
        RBS_LOG_INFO("RBS", "====================================================");

        // ── EN-DC wiring: connect LTE(MN) ↔ NR(SN) when both stacks are active ──
        if (lteStack_ && nrStack_) {
            const rbs::ENDCConfig endc = rbs::Config::instance().buildENDCConfig();
            if (endc.enabled) {
                x2endc_ = std::make_unique<rbs::lte::X2APLink>("MN-eNB");
                x2endc_->connect(1, endc.x2Addr, endc.x2Port);
                rbs::DCBearerConfig bearer{};
                bearer.enbBearerId = endc.enbBearerId;
                bearer.type        = (endc.option == rbs::ENDCOption::OPTION_3A)
                                     ? rbs::DCBearerType::SCG
                                     : (endc.option == rbs::ENDCOption::OPTION_3X
                                        ? rbs::DCBearerType::SPLIT_SN
                                        : rbs::DCBearerType::SPLIT_MN);
                bearer.scgLegDrbId = endc.scgDrbId;
                bearer.nrCellId    = nrStack_->config().nrCellIdentity;
                x2endc_->sgNBAdditionRequest(0xFFFF, endc.option, {bearer});
                RBS_LOG_INFO("RBS", "EN-DC Option ", static_cast<int>(endc.option),
                             " active: LTE(MN) + NR(SN) X2=", endc.x2Addr, ":", endc.x2Port);
            }
        }

        return true;
    }

    void stop() {
        RBS_LOG_INFO("RBS", "Stopping cell(s)...");
        rbs::oms::OMS::instance().setNodeState(rbs::oms::OMS::NodeState::SHUTTING_DOWN);
        restServer_->stop();
        if (x2endc_)    { x2endc_->disconnect(1); }
        if (nrStack_)   { nrStack_->stop();   nrRF_->shutdown(); }
        if (lteStack_)  { lteStack_->stop();  lteRF_->shutdown(); }
        if (umtsStack_) { umtsStack_->stop(); umtsRF_->shutdown(); }
        if (gsmStack_)  { gsmStack_->stop();  gsmRF_->shutdown(); }
        rbs::oms::OMS::instance().setNodeState(rbs::oms::OMS::NodeState::LOCKED);
        RBS_LOG_INFO("RBS", "Radio Base Station OFFLINE");
    }

    // ── Demo ─────────────────────────────────────────────────────
    void runDemo() {
        RBS_LOG_INFO("RBS", "--- Running demo [", ratName(rat_), "] ---");
        auto& oms = rbs::oms::OMS::instance();

        if (gsmStack_) {
            rbs::RNTI rnti = gsmStack_->admitUE(100000000000001ULL);
            rbs::ByteBuffer voice(13, 0xAA);
            gsmStack_->sendData(rnti, voice);
            RBS_LOG_INFO("RBS", "[GSM ] UE RNTI=", rnti,
                         " admitted on ARFCN=", gsmStack_->config().arfcn,
                         " BSIC=", +gsmStack_->config().bsic);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            gsmStack_->printStats();
            oms.updateCounter("gsm.connectedUEs",
                              static_cast<double>(gsmStack_->connectedUECount()));
            gsmStack_->releaseUE(rnti);
        }

        if (umtsStack_) {
            rbs::RNTI rnti = umtsStack_->admitUE(200000000000002ULL);
            rbs::ByteBuffer data(32, 0xBB);
            umtsStack_->sendData(rnti, data);
            RBS_LOG_INFO("RBS", "[UMTS] UE RNTI=", rnti,
                         " admitted on UARFCN=", umtsStack_->config().uarfcn,
                         " PSC=", umtsStack_->config().primaryScrCode);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            umtsStack_->printStats();
            oms.updateCounter("umts.connectedUEs",
                              static_cast<double>(umtsStack_->connectedUECount()));
            umtsStack_->releaseUE(rnti);
        }

        if (lteStack_) {
            rbs::RNTI rnti = lteStack_->admitUE(300000000000003ULL, 12);
            lteStack_->updateCQI(rnti, 12);
            for (int i = 0; i < 3; ++i) {
                rbs::ByteBuffer pkt(100, static_cast<uint8_t>(i));
                lteStack_->sendIPPacket(rnti, 1, pkt);
            }
            RBS_LOG_INFO("RBS", "[LTE ] UE RNTI=", rnti,
                         " admitted on EARFCN=", lteStack_->config().earfcn,
                         " PCI=", lteStack_->config().pci,
                         " CQI=12 → MCS=17");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            lteStack_->printStats();
            oms.updateCounter("lte.connectedUEs",
                              static_cast<double>(lteStack_->connectedUECount()));
            lteStack_->releaseUE(rnti);
        }

        if (nrStack_) {
            rbs::RNTI rnti = nrStack_->admitUE(400000000000004ULL);
            RBS_LOG_INFO("RBS", "[NR  ] UE RNTI=", rnti,
                         " admitted on NR-ARFCN=", nrStack_->config().nrArfcn,
                         " PCI=", nrStack_->config().nrPci,
                         " SCS=", rbs::nrScsKhz(nrStack_->config().scs), " kHz");
            // EN-DC Option 3a: wire SCG bearer if LTE is also running
            if (lteStack_) {
                rbs::DCBearerConfig bearer{};
                bearer.enbBearerId = 5;
                bearer.type        = rbs::DCBearerType::SCG;
                bearer.scgLegDrbId = 1;
                bearer.nrCellId    = nrStack_->config().nrCellIdentity;
                uint16_t nrCrnti = nrStack_->acceptSCGBearer(rnti, bearer);
                RBS_LOG_INFO("RBS", "[EN-DC 3a] lteCrnti=", rnti,
                             " nrCrnti=", nrCrnti,
                             " SCG bearer id=", bearer.enbBearerId);
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            nrStack_->printStats();
            oms.updateCounter("nr.connectedUEs",
                              static_cast<double>(nrStack_->connectedUECount()));
            if (lteStack_) nrStack_->releaseSCGBearer(rnti);
            nrStack_->releaseUE(rnti);
        }

        oms.printPerformanceReport();
    }

    // ── Main loop ─────────────────────────────────────────────────
    void mainLoop() {
        auto& oms = rbs::oms::OMS::instance();
        uint32_t tick = 0;
        while (!gShutdown.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            ++tick;
            if (gsmStack_)
                oms.updateCounter("gsm.connectedUEs",
                                  static_cast<double>(gsmStack_->connectedUECount()));
            if (umtsStack_)
                oms.updateCounter("umts.connectedUEs",
                                  static_cast<double>(umtsStack_->connectedUECount()));
            if (lteStack_)
                oms.updateCounter("lte.connectedUEs",
                                  static_cast<double>(lteStack_->connectedUECount()));
            if (nrStack_)
                oms.updateCounter("nr.connectedUEs",
                                  static_cast<double>(nrStack_->connectedUECount()));
            if (tick % 3 == 0) oms.printPerformanceReport();
        }
    }

private:
    SelectedRAT rat_;
    std::shared_ptr<rbs::hal::RFHardware>   gsmRF_;
    std::shared_ptr<rbs::hal::RFHardware>   umtsRF_;
    std::shared_ptr<rbs::hal::RFHardware>   lteRF_;
    std::unique_ptr<rbs::gsm::GSMStack>      gsmStack_;
    std::unique_ptr<rbs::umts::UMTSStack>    umtsStack_;
    std::unique_ptr<rbs::lte::LTEStack>      lteStack_;
    std::shared_ptr<rbs::hal::RFHardware>    nrRF_;
    std::unique_ptr<rbs::nr::NRStack>        nrStack_;
    // EN-DC X2AP link between LTE(MN) and NR(SN)
    std::unique_ptr<rbs::lte::X2APLink>      x2endc_;
    // REST API server
    std::unique_ptr<rbs::api::RestServer>    restServer_;
};

// ────────────────────────────────────────────────────────────────
// Entry point
// ────────────────────────────────────────────────────────────────
static void printUsage(const char* exe) {
    std::cerr << "Usage: " << exe << " [config] [gsm|umts|lte|nr]\n"
              << "  config  path to rbs.conf  (default: rbs.conf)\n"
              << "  gsm     start GSM  (2G) cell only\n"
              << "  umts    start UMTS (3G) cell only\n"
              << "  lte     start LTE  (4G) cell only\n"
              << "  nr      start NR   (5G) cell only\n"
              << "  <none>  start all four RATs (EN-DC active when LTE+NR)\n";
}

int main(int argc, char* argv[]) {
    rbs::Logger::instance().setLevel(rbs::LogLevel::INFO);
    rbs::Logger::instance().enableFile("rbs.log");

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string configPath = "rbs.conf";
    SelectedRAT rat = SelectedRAT::ALL;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::string al = a;
        std::transform(al.begin(), al.end(), al.begin(), ::tolower);
        if (al == "gsm" || al == "umts" || al == "lte" || al == "nr") {
            rat = parseRAT(al);
        } else if (al == "--help" || al == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            configPath = a;
        }
    }

    RBS_LOG_INFO("RBS", "Radio Base Station v1.0.0 starting...");
    RBS_LOG_INFO("RBS", "Config: ", configPath, "  RAT: ", ratName(rat));

    RadioBaseStation node(configPath, rat);

    if (!node.start()) {
        RBS_LOG_CRITICAL("RBS", "Failed to start – exiting");
        return EXIT_FAILURE;
    }

    node.runDemo();
    node.mainLoop();
    node.stop();
    return EXIT_SUCCESS;
}

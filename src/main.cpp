#include "common/logger.h"
#include "common/config.h"
#include "common/types.h"
#include "hal/rf_hardware.h"
#include "gsm/gsm_stack.h"
#include "umts/umts_stack.h"
#include "lte/lte_stack.h"
#include "lte/multi_cell_model.h"
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
#include <vector>

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
            const int cellCount = std::max(1, cfg.getInt("lte", "cell_count", 1));
            const int cellIdStep = std::max(1, cfg.getInt("lte", "cell_id_step", 1));
            const int earfcnStep = std::max(1, cfg.getInt("lte", "earfcn_step", 5));
            const int pciStep = std::max(1, cfg.getInt("lte", "pci_step", 3));
            const int s1uPortBase = std::max(1, cfg.getInt("lte", "s1u_port_base", 2152));
            lteInterSiteDistanceM_ = std::max(10.0, cfg.getDouble("lte", "inter_site_distance_m", 500.0));

            const auto base = cfg.buildLTEConfig();
            for (int i = 0; i < cellCount; ++i) {
                auto rf = std::make_shared<rbs::hal::RFHardware>(2, 4);
                rf->setAlarmCallback(alarmCb);

                rbs::LTECellConfig c = base;
                c.cellId = static_cast<rbs::CellId>(base.cellId + i * cellIdStep);
                c.earfcn = static_cast<rbs::EARFCN>(base.earfcn + i * earfcnStep);
                c.pci = static_cast<uint16_t>((base.pci + i * pciStep) % 504);
                c.s1uLocalPort = static_cast<uint16_t>(s1uPortBase + i);

                lteRFs_.push_back(rf);
                lteStacks_.push_back(std::make_unique<rbs::lte::LTEStack>(rf, c));
            }
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

        auto& cfg = rbs::Config::instance();
        if (cfg.getBool("oms", "prometheus_enabled", true)) {
            const auto bind = cfg.getString("oms", "prometheus_bind", "127.0.0.1");
            const int port = cfg.getInt("oms", "prometheus_port", 9108);
            rbs::oms::OMS::instance().exportPrometheus(port, bind);
        }

        auto initRF = [](std::shared_ptr<rbs::hal::RFHardware>& rf) -> bool {
            return rf && rf->initialise() && rf->selfTest();
        };

        if (gsmRF_  && !initRF(gsmRF_))  { RBS_LOG_CRITICAL("RBS", "GSM RF init failed");  return false; }
        if (umtsRF_ && !initRF(umtsRF_)) { RBS_LOG_CRITICAL("RBS", "UMTS RF init failed"); return false; }
        for (size_t i = 0; i < lteRFs_.size(); ++i) {
            if (!initRF(lteRFs_[i])) {
                RBS_LOG_CRITICAL("RBS", "LTE RF init failed for cell index ", i);
                return false;
            }
        }
        if (nrRF_   && !initRF(nrRF_))   { RBS_LOG_CRITICAL("RBS", "NR RF init failed");   return false; }

        if (gsmStack_  && !gsmStack_->start())  { RBS_LOG_CRITICAL("RBS", "GSM stack start failed");  return false; }
        if (umtsStack_ && !umtsStack_->start()) { RBS_LOG_CRITICAL("RBS", "UMTS stack start failed"); return false; }
        for (size_t i = 0; i < lteStacks_.size(); ++i) {
            if (!lteStacks_[i]->start()) {
                RBS_LOG_CRITICAL("RBS", "LTE stack start failed for cell index ", i);
                return false;
            }
        }
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
        for (const auto& lte : lteStacks_) {
            RBS_LOG_INFO("RBS", "  LTE  cell ", lte->config().cellId,
                         "  EARFCN=", lte->config().earfcn,
                         "  PCI=",    lte->config().pci,
                         "  BW=",     lteBwMHz(lte->config().bandwidth), " MHz");
        }
        if (nrStack_)
            RBS_LOG_INFO("RBS", "  NR   cell ", nrStack_->config().cellId,
                         "  NR-ARFCN=", nrStack_->config().nrArfcn,
                         "  PCI=",      nrStack_->config().nrPci,
                         "  SCS=",      rbs::nrScsKhz(nrStack_->config().scs), " kHz");
        RBS_LOG_INFO("RBS", "====================================================");

        // ── EN-DC wiring: connect LTE(MN) ↔ NR(SN) when both stacks are active ──
        if (!lteStacks_.empty() && nrStack_) {
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
        for (size_t i = 0; i < lteStacks_.size(); ++i) {
            lteStacks_[i]->stop();
            lteRFs_[i]->shutdown();
        }
        if (umtsStack_) { umtsStack_->stop(); umtsRF_->shutdown(); }
        if (gsmStack_)  { gsmStack_->stop();  gsmRF_->shutdown(); }
        rbs::oms::OMS::instance().stopPrometheus();
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

        if (!lteStacks_.empty()) {
            uint64_t baseImsi = 300000000000003ULL;
            for (size_t i = 0; i < lteStacks_.size(); ++i) {
                auto& lte = lteStacks_[i];
                rbs::RNTI rnti = lte->admitUE(baseImsi + i, 10);
                const double sinrDb = rbs::lte::estimateSinrDb(
                    lteInterSiteDistanceM_ * (1.0 + static_cast<double>(i) * 0.25),
                    static_cast<uint32_t>(lteStacks_.size() - 1),
                    lte->config().txPower.dBm);
                const uint8_t cqi = rbs::lte::sinrToCqi(sinrDb);
                lte->updateCQI(rnti, cqi);

                for (int k = 0; k < 3; ++k) {
                    rbs::ByteBuffer pkt(100, static_cast<uint8_t>(k));
                    lte->sendIPPacket(rnti, 1, pkt);
                }
                lte->setupVoLTEBearer(rnti);
                lte->sendVoLteRtpBurst(rnti, 2, 120);

                RBS_LOG_INFO("RBS", "[LTE ] cell=", lte->config().cellId,
                             " UE RNTI=", rnti,
                             " EARFCN=", lte->config().earfcn,
                             " PCI=", lte->config().pci,
                             " SINR=", sinrDb,
                             " dB CQI=", static_cast<int>(cqi));
                const std::string cellBase = "lte.cell." + std::to_string(lte->config().cellId) + ".";
                oms.updateCounter(cellBase + "sinr.db", sinrDb, "dB");
                oms.updateCounter(cellBase + "connectedUEs", static_cast<double>(lte->connectedUECount()));
                updateSinrHistogram(oms, lte->config().cellId, sinrDb);
                const double dlBits = static_cast<double>(3 * 100 + 2 * 120) * 8.0;
                oms.updateCounter(cellBase + "throughput.dl.kbps", dlBits / 200.0, "kbps");

                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                lte->printStats();
                lte->releaseUE(rnti);
            }

            double total = 0.0;
            for (const auto& lte : lteStacks_) total += static_cast<double>(lte->connectedUECount());
            oms.updateCounter("lte.connectedUEs", total);
        }

        if (nrStack_) {
            rbs::RNTI rnti = nrStack_->admitUE(400000000000004ULL);
            RBS_LOG_INFO("RBS", "[NR  ] UE RNTI=", rnti,
                         " admitted on NR-ARFCN=", nrStack_->config().nrArfcn,
                         " PCI=", nrStack_->config().nrPci,
                         " SCS=", rbs::nrScsKhz(nrStack_->config().scs), " kHz");
            // EN-DC Option 3a: wire SCG bearer if LTE is also running
            if (!lteStacks_.empty()) {
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
            updateSliceCounters(oms, nrStack_->currentSliceMetrics());
            if (!lteStacks_.empty()) nrStack_->releaseSCGBearer(rnti);
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
            if (!lteStacks_.empty()) {
                double total = 0.0;
                for (const auto& lte : lteStacks_)
                    total += static_cast<double>(lte->connectedUECount());
                oms.updateCounter("lte.connectedUEs", total);
            }
            if (nrStack_) {
                oms.updateCounter("nr.connectedUEs",
                                  static_cast<double>(nrStack_->connectedUECount()));
                updateSliceCounters(oms, nrStack_->currentSliceMetrics());
            }
            if (tick % 3 == 0) oms.printPerformanceReport();

            if (!lteStacks_.empty()) {
                for (size_t i = 0; i < lteStacks_.size(); ++i) {
                    const auto& lte = lteStacks_[i];
                    const double sinrDb = rbs::lte::estimateSinrDb(
                        lteInterSiteDistanceM_ * (1.0 + static_cast<double>(i) * 0.25),
                        static_cast<uint32_t>(lteStacks_.size() - 1),
                        lte->config().txPower.dBm);
                    const std::string cellBase = "lte.cell." + std::to_string(lte->config().cellId) + ".";
                    oms.updateCounter(cellBase + "sinr.db", sinrDb, "dB");
                    updateSinrHistogram(oms, lte->config().cellId, sinrDb);
                }
            }
        }
    }

private:
    static void updateSliceCounters(rbs::oms::OMS& oms,
                                    const std::vector<rbs::nr::NRSliceMetrics>& metrics) {
        for (const auto& metric : metrics) {
            const std::string name = rbs::nr::NRMac::sliceName(metric.slice);
            const std::string base = "slice." + name + ".";
            oms.updateCounter(base + "prb_used", static_cast<double>(metric.usedPrbs));
            oms.updateCounter(base + "max_prb", static_cast<double>(metric.maxPrbs));
            oms.updateCounter(base + "connectedUEs", static_cast<double>(metric.activeUes));
            oms.updateCounter(base + "pending_bytes", static_cast<double>(metric.pendingBytes));
        }
    }

    static void updateSinrHistogram(rbs::oms::OMS& oms, rbs::CellId cellId, double sinrDb) {
        const std::string base = "lte.cell." + std::to_string(cellId) + ".sinr.";
        oms.updateCounter(base + "samples", oms.getCounter(base + "samples") + 1.0);
        if (sinrDb < 0.0) {
            oms.updateCounter(base + "bucket.lt0", oms.getCounter(base + "bucket.lt0") + 1.0);
        } else if (sinrDb < 5.0) {
            oms.updateCounter(base + "bucket.0_5", oms.getCounter(base + "bucket.0_5") + 1.0);
        } else if (sinrDb < 10.0) {
            oms.updateCounter(base + "bucket.5_10", oms.getCounter(base + "bucket.5_10") + 1.0);
        } else if (sinrDb < 15.0) {
            oms.updateCounter(base + "bucket.10_15", oms.getCounter(base + "bucket.10_15") + 1.0);
        } else {
            oms.updateCounter(base + "bucket.ge15", oms.getCounter(base + "bucket.ge15") + 1.0);
        }
    }

    SelectedRAT rat_;
    std::shared_ptr<rbs::hal::RFHardware>   gsmRF_;
    std::shared_ptr<rbs::hal::RFHardware>   umtsRF_;
    std::vector<std::shared_ptr<rbs::hal::RFHardware>> lteRFs_;
    std::unique_ptr<rbs::gsm::GSMStack>      gsmStack_;
    std::unique_ptr<rbs::umts::UMTSStack>    umtsStack_;
    std::vector<std::unique_ptr<rbs::lte::LTEStack>> lteStacks_;
    std::shared_ptr<rbs::hal::RFHardware>    nrRF_;
    std::unique_ptr<rbs::nr::NRStack>        nrStack_;
    // EN-DC X2AP link between LTE(MN) and NR(SN)
    std::unique_ptr<rbs::lte::X2APLink>      x2endc_;
    // REST API server
    std::unique_ptr<rbs::api::RestServer>    restServer_;
    double lteInterSiteDistanceM_ = 500.0;
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

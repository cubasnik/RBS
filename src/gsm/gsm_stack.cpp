#include "gsm_stack.h"
#include "../common/logger.h"
#include "../common/link_registry.h"
#include "../common/config.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace rbs::gsm {

GSMStack::GSMStack(std::shared_ptr<hal::IRFHardware> rf, const GSMCellConfig& cfg)
    : cfg_(cfg)
    , rf_(std::move(rf))
{
    phy_  = std::make_shared<GSMPhy>(rf_, cfg_);
    mac_  = std::make_shared<GSMMAC>(phy_, cfg_);
    rr_   = std::make_shared<GSMRr>();
    rlc_  = std::make_shared<GSMRlc>();
    abis_ = std::make_unique<AbisOml>("BTS-" + std::to_string(cfg_.cellId));

    // Register Abis link in global registry
    rbs::LinkEntry entry;
    entry.name         = "abis";
    entry.rat          = "GSM";
    entry.peerAddr     = cfg_.bscAddr;
    entry.peerPort     = cfg_.bscPort;
    entry.ctrl         = abis_.get();
    entry.isConnected  = [this]() { return abis_->isConnected(); };
    entry.reconnect    = [this]() { abis_->reconnect(); };
    entry.disconnect   = [this]() { abis_->disconnect(); };
    entry.injectableProcs  = [this]() { return abis_->injectableProcs(); };
    entry.injectProcedure  = [this](const std::string& p) { return abis_->injectProcedure(p); };
    entry.healthJson       = [this]() { return abis_->healthJson(); };
    rbs::LinkRegistry::instance().registerLink(std::move(entry));
}

GSMStack::~GSMStack() { stop(); }

// ────────────────────────────────────────────────────────────────
bool GSMStack::start() {
    if (running_.load()) return true;
    if (!phy_->start()) {
        RBS_LOG_ERROR("GSMStack", "PHY start failed");
        return false;
    }
    if (!mac_->start()) {
        RBS_LOG_ERROR("GSMStack", "MAC start failed");
        return false;
    }
    running_.store(true);
    clockThread_ = std::thread(&GSMStack::clockLoop, this);

    // Option A: config-gated Abis transport mode (sim | ipa_tcp)
    std::string abisTransport = rbs::Config::instance().getString("gsm", "abis_transport", "sim");
    std::transform(abisTransport.begin(), abisTransport.end(), abisTransport.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool useIpaTcp = (abisTransport == "ipa_tcp" || abisTransport == "ipa" || abisTransport == "tcp");

    const auto hbMs = static_cast<uint32_t>(rbs::Config::instance().getInt("gsm", "abis_hb_interval_ms", 1000));
    const auto staleMs = static_cast<uint32_t>(rbs::Config::instance().getInt("gsm", "abis_rx_stale_ms", 10000));
    const bool keepaliveEnabled = rbs::Config::instance().getBool("gsm", "abis_keepalive_enabled", true);
    const auto keepaliveIdleMs = static_cast<uint32_t>(rbs::Config::instance().getInt("gsm", "abis_keepalive_idle_ms", 3000));
    std::string interopProfile = rbs::Config::instance().getString("gsm", "abis_interop_profile", "default");
    std::transform(interopProfile.begin(), interopProfile.end(), interopProfile.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    uint32_t hbCfg = hbMs;
    uint32_t staleCfg = staleMs;
    uint32_t keepaliveIdleCfg = keepaliveIdleMs;
    if (interopProfile == "osmocom") {
        hbCfg = std::min<uint32_t>(hbCfg, 1000);
        staleCfg = std::max<uint32_t>(staleCfg, 5000);
        keepaliveIdleCfg = std::min<uint32_t>(keepaliveIdleCfg, 2500);
    }

    abis_->setUseRealTransport(useIpaTcp);
    abis_->setInteropProfile(interopProfile);
    abis_->setHealthTiming(hbCfg, staleCfg);
    abis_->setKeepaliveConfig(keepaliveEnabled, keepaliveIdleCfg);

    RBS_LOG_INFO("GSMStack", "Abis mode={}, profile={}, bsc={}:{}, hb={}ms, stale={}ms, ka={} idle={}ms",
                 useIpaTcp ? "ipa_tcp" : "sim", interopProfile, cfg_.bscAddr, cfg_.bscPort,
                 hbCfg, staleCfg, keepaliveEnabled ? "on" : "off", keepaliveIdleCfg);

    // Connect Abis/OML to BSC if configured
    if (!cfg_.bscAddr.empty()) {
        if (!abis_->connect(cfg_.bscAddr, cfg_.bscPort))
            RBS_LOG_WARNING("GSMStack", "Abis/OML: не удалось подключиться к BSC {}:{}",
                            cfg_.bscAddr, cfg_.bscPort);
    } else {
        RBS_LOG_INFO("GSMStack", "Abis/OML: BSC не настроен, режим симуляции");
    }

    RBS_LOG_INFO("GSMStack", "GSM cell ", cfg_.cellId, " started");
    return true;
}

void GSMStack::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (clockThread_.joinable()) clockThread_.join();
    mac_->stop();
    phy_->stop();
    ueMap_.clear();
    RBS_LOG_INFO("GSMStack", "GSM cell ", cfg_.cellId, " stopped");
}

// ────────────────────────────────────────────────────────────────
// TDMA clock: each tick = one time-slot ≈ 577 µs (simulated faster)
void GSMStack::clockLoop() {
    using namespace std::chrono_literals;
    // Simulation: run at 10× real speed (57.7 µs per slot)
    const auto slotDuration = std::chrono::microseconds(577);
    while (running_.load()) {
        phy_->tick();
        mac_->tick();
        std::this_thread::sleep_for(slotDuration);
    }
}

// ────────────────────────────────────────────────────────────────
RNTI GSMStack::admitUE(IMSI imsi) {
    RNTI rnti = mac_->assignChannel(0, GSMChannelType::TCH_F);
    if (rnti != 0) {
        RNTI rrRnti;
        rr_->handleChannelRequest(0x50 /*TCH/F cause*/, rrRnti);
        rlc_->requestLink(rnti, SAPI::RR_MM_CC);
        ueMap_[rnti] = imsi;
        RBS_LOG_INFO("GSMStack", "UE admitted IMSI=", imsi, " RNTI=", rnti);
    }
    return rnti;
}

void GSMStack::releaseUE(RNTI rnti) {
    rr_->releaseChannel(rnti);
    rlc_->releaseLink(rnti, SAPI::RR_MM_CC);
    mac_->releaseChannel(rnti);
    ueMap_.erase(rnti);
    RBS_LOG_INFO("GSMStack", "UE released RNTI=", rnti);
}

bool GSMStack::sendData(RNTI rnti, ByteBuffer data) {
    rlc_->sendSdu(rnti, SAPI::RR_MM_CC, ByteBuffer(data));  // LAPDm I-frame tracking
    return mac_->enqueueDlData(rnti, std::move(data));
}

bool GSMStack::receiveData(RNTI rnti, ByteBuffer& data) {
    // Try LAPDm SDU queue first, then raw MAC buffer
    if (rlc_->receiveSdu(rnti, SAPI::RR_MM_CC, data)) return true;
    return mac_->dequeueUlData(rnti, data);
}

size_t GSMStack::connectedUECount() const {
    return mac_->activeChannelCount();
}

void GSMStack::printStats() const {
    RBS_LOG_INFO("GSMStack", "Cell=", cfg_.cellId,
                 " ARFCN=", cfg_.arfcn,
                 " UEs=", mac_->activeChannelCount(),
                 " Frame=", phy_->currentFrameNumber());
}

}  // namespace rbs::gsm

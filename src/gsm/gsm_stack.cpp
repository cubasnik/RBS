#include "gsm_stack.h"
#include "../common/logger.h"
#include <chrono>
#include <thread>

namespace rbs::gsm {

GSMStack::GSMStack(std::shared_ptr<hal::IRFHardware> rf, const GSMCellConfig& cfg)
    : cfg_(cfg)
    , rf_(std::move(rf))
{
    phy_ = std::make_shared<GSMPhy>(rf_, cfg_);
    mac_ = std::make_shared<GSMMAC>(phy_, cfg_);
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
        ueMap_[rnti] = imsi;
        RBS_LOG_INFO("GSMStack", "UE admitted IMSI=", imsi, " RNTI=", rnti);
    }
    return rnti;
}

void GSMStack::releaseUE(RNTI rnti) {
    mac_->releaseChannel(rnti);
    ueMap_.erase(rnti);
    RBS_LOG_INFO("GSMStack", "UE released RNTI=", rnti);
}

bool GSMStack::sendData(RNTI rnti, ByteBuffer data) {
    return mac_->enqueueDlData(rnti, std::move(data));
}

bool GSMStack::receiveData(RNTI rnti, ByteBuffer& data) {
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

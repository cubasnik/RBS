#include "umts_stack.h"
#include "../common/logger.h"
#include <chrono>
#include <thread>

namespace rbs::umts {

UMTSStack::UMTSStack(std::shared_ptr<hal::IRFHardware> rf, const UMTSCellConfig& cfg)
    : cfg_(cfg), rf_(std::move(rf))
{
    phy_ = std::make_shared<UMTSPhy>(rf_, cfg_);
    mac_ = std::make_shared<UMTSMAC>(phy_, cfg_);
}

UMTSStack::~UMTSStack() { stop(); }

// ────────────────────────────────────────────────────────────────
bool UMTSStack::start() {
    if (running_.load()) return true;
    if (!phy_->start()) {
        RBS_LOG_ERROR("UMTSStack", "PHY start failed");
        return false;
    }
    if (!mac_->start()) {
        RBS_LOG_ERROR("UMTSStack", "MAC start failed");
        return false;
    }
    running_.store(true);
    frameThread_ = std::thread(&UMTSStack::frameLoop, this);
    RBS_LOG_INFO("UMTSStack", "UMTS cell ", cfg_.cellId, " started");
    return true;
}

void UMTSStack::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (frameThread_.joinable()) frameThread_.join();
    mac_->stop();
    phy_->stop();
    ueMap_.clear();
    RBS_LOG_INFO("UMTSStack", "UMTS cell ", cfg_.cellId, " stopped");
}

// ────────────────────────────────────────────────────────────────
void UMTSStack::frameLoop() {
    // WCDMA radio frame = 10 ms; simulated at compressed speed
    using namespace std::chrono_literals;
    while (running_.load()) {
        phy_->tick();
        mac_->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ────────────────────────────────────────────────────────────────
RNTI UMTSStack::admitUE(IMSI imsi, SF sf) {
    RNTI rnti = mac_->assignDCH(sf);
    if (rnti != 0) {
        ueMap_[rnti] = imsi;
        RBS_LOG_INFO("UMTSStack", "UE admitted IMSI=", imsi, " RNTI=", rnti);
    }
    return rnti;
}

void UMTSStack::releaseUE(RNTI rnti) {
    mac_->releaseDCH(rnti);
    ueMap_.erase(rnti);
    RBS_LOG_INFO("UMTSStack", "UE released RNTI=", rnti);
}

bool UMTSStack::sendData(RNTI rnti, ByteBuffer data) {
    return mac_->enqueueDlData(rnti, std::move(data));
}

bool UMTSStack::receiveData(RNTI rnti, ByteBuffer& data) {
    return mac_->dequeueUlData(rnti, data);
}

size_t UMTSStack::connectedUECount() const {
    return mac_->activeChannelCount();
}

void UMTSStack::printStats() const {
    RBS_LOG_INFO("UMTSStack", "Cell=", cfg_.cellId,
                 " UARFCN=", cfg_.uarfcn,
                 " UEs=", mac_->activeChannelCount(),
                 " Frame=", phy_->currentFrameNumber());
}

}  // namespace rbs::umts

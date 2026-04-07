#include "umts_mac.h"
#include "../common/logger.h"

namespace rbs::umts {

UMTSMAC::UMTSMAC(std::shared_ptr<UMTSPhy> phy, const UMTSCellConfig& cfg)
    : phy_(std::move(phy)), cfg_(cfg) {}

// ────────────────────────────────────────────────────────────────
bool UMTSMAC::start() {
    if (running_) return true;
    phy_->setRxCallback([this](const UMTSFrame& f) { onRxFrame(f); });
    running_ = true;
    RBS_LOG_INFO("UMTSMAC", "Started – CellId=", cfg_.cellId,
                 " PSC=", cfg_.primaryScrCode);
    return true;
}

void UMTSMAC::stop() {
    channels_.clear();
    running_ = false;
    RBS_LOG_INFO("UMTSMAC", "Stopped");
}

// ────────────────────────────────────────────────────────────────
void UMTSMAC::tick() {
    if (!running_) return;
    scheduleDlTransmissions();
}

// ────────────────────────────────────────────────────────────────
RNTI UMTSMAC::assignDCH(SF sf) {
    RNTI rnti = nextRnti_++;
    UMTSUEContext ctx{};
    ctx.rnti           = rnti;
    ctx.channelType    = UMTSChannelType::DCH;
    ctx.channelCode    = nextCode_++;
    ctx.spreadingFactor = sf;
    ctx.active          = true;
    channels_[rnti]     = std::move(ctx);
    RBS_LOG_INFO("UMTSMAC", "DCH assigned RNTI=", rnti,
                 " code=", channels_[rnti].channelCode,
                 " SF=", static_cast<int>(sf));
    return rnti;
}

bool UMTSMAC::releaseDCH(RNTI rnti) {
    auto it = channels_.find(rnti);
    if (it == channels_.end()) return false;
    channels_.erase(it);
    RBS_LOG_INFO("UMTSMAC", "DCH released RNTI=", rnti);
    return true;
}

// ────────────────────────────────────────────────────────────────
bool UMTSMAC::enqueueDlData(RNTI rnti, ByteBuffer data) {
    auto it = channels_.find(rnti);
    if (it == channels_.end()) return false;
    it->second.txQueue.push(std::move(data));
    return true;
}

bool UMTSMAC::dequeueUlData(RNTI rnti, ByteBuffer& data) {
    auto it = channels_.find(rnti);
    if (it == channels_.end() || it->second.rxQueue.empty()) return false;
    data = std::move(it->second.rxQueue.front());
    it->second.rxQueue.pop();
    return true;
}

// ────────────────────────────────────────────────────────────────
void UMTSMAC::onRxFrame(const UMTSFrame& frame) {
    // Demultiplex received data to all active UL channels (simplified)
    for (auto& [rnti, ctx] : channels_) {
        if (!ctx.active) continue;
        ByteBuffer data;
        if (phy_->receive(ctx.channelCode, ctx.spreadingFactor, data, 168)) {
            ctx.rxQueue.push(std::move(data));
        }
    }
}

void UMTSMAC::scheduleDlTransmissions() {
    for (auto& [rnti, ctx] : channels_) {
        if (ctx.txQueue.empty()) continue;
        ByteBuffer& data = ctx.txQueue.front();
        if (phy_->transmit(ctx.channelCode, ctx.spreadingFactor, data)) {
            ctx.txQueue.pop();
            RBS_LOG_DEBUG("UMTSMAC", "DL TX RNTI=", rnti,
                          " bytes=", data.size());
        }
    }
}

}  // namespace rbs::umts

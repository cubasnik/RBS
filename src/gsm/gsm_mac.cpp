#include "gsm_mac.h"
#include "../common/logger.h"
#include <cstring>

namespace rbs::gsm {

GSMMAC::GSMMAC(std::shared_ptr<GSMPhy> phy, const GSMCellConfig& cfg)
    : phy_(std::move(phy)), cfg_(cfg) {}

// ────────────────────────────────────────────────────────────────
bool GSMMAC::start() {
    if (running_) return true;
    phy_->setRxCallback([this](const GSMBurst& b) { onRxBurst(b); });
    // Pre-queue SI messages
    siQueue_.push({1, buildSIType1()});
    siQueue_.push({3, buildSIType3()});
    running_ = true;
    RBS_LOG_INFO("GSMMAC", "Started – CellId=", cfg_.cellId,
                 " LAC=", cfg_.lac, " BSIC=", static_cast<int>(cfg_.bsic));
    return true;
}

void GSMMAC::stop() {
    channels_.clear();
    running_ = false;
    RBS_LOG_INFO("GSMMAC", "Stopped");
}

// ────────────────────────────────────────────────────────────────
void GSMMAC::tick() {
    if (!running_) return;
    ++tickCount_;
    processBroadcast();
    scheduleDownlinkBursts();
    // PHY ticks separately; uplink handled via RxCallback
}

// ────────────────────────────────────────────────────────────────
RNTI GSMMAC::assignChannel(uint8_t preferredSlot, GSMChannelType type) {
    // Slot 0 is reserved for broadcast; pick 1–7
    uint8_t slot = (preferredSlot == 0) ? 1 : preferredSlot;
    // Find a free slot
    for (uint8_t s = 1; s < GSM_SLOTS_PER_FRAME; ++s) {
        if (slotMap_.find(s) == slotMap_.end()) {
            slot = s;
            break;
        }
    }
    if (slotMap_.count(slot) && slotMap_[slot] != 0) {
        RBS_LOG_WARNING("GSMMAC", "No free slots available");
        return 0;   // 0 = invalid RNTI
    }
    RNTI rnti = nextRnti_++;
    GSMUEChannel ch{};
    ch.rnti        = rnti;
    ch.timeSlot    = slot;
    ch.channelType = type;
    ch.active      = true;
    ch.seqNum      = 0;
    channels_[rnti]  = std::move(ch);
    slotMap_[slot]   = rnti;
    RBS_LOG_INFO("GSMMAC", "Assigned channel RNTI=", rnti, " slot=",
                 static_cast<int>(slot), " type=",
                 static_cast<int>(type));
    return rnti;
}

bool GSMMAC::releaseChannel(RNTI rnti) {
    auto it = channels_.find(rnti);
    if (it == channels_.end()) return false;
    slotMap_.erase(it->second.timeSlot);
    channels_.erase(it);
    RBS_LOG_INFO("GSMMAC", "Released channel RNTI=", rnti);
    return true;
}

// ────────────────────────────────────────────────────────────────
bool GSMMAC::enqueueDlData(RNTI rnti, ByteBuffer data) {
    auto it = channels_.find(rnti);
    if (it == channels_.end()) return false;
    it->second.txQueue.push(std::move(data));
    return true;
}

bool GSMMAC::dequeueUlData(RNTI rnti, ByteBuffer& data) {
    auto it = channels_.find(rnti);
    if (it == channels_.end() || it->second.rxQueue.empty()) return false;
    data = std::move(it->second.rxQueue.front());
    it->second.rxQueue.pop();
    return true;
}

void GSMMAC::broadcastSIMessage(uint8_t siType, const ByteBuffer& payload) {
    siQueue_.push({siType, payload});
}

// ────────────────────────────────────────────────────────────────
// Internal helpers
// ────────────────────────────────────────────────────────────────
void GSMMAC::onRxBurst(const GSMBurst& burst) {
    auto sit = slotMap_.find(burst.timeSlot);
    if (sit == slotMap_.end()) return;
    auto cit = channels_.find(sit->second);
    if (cit == channels_.end()) return;
    // Extract payload (bits 3..60 + 87..144)
    ByteBuffer payload(14, 0);
    for (int i = 0; i < 56; ++i) {
        int bitIdx = 3 + i;
        payload[i / 8] |= (burst.bits[bitIdx] & 1) << (7 - (i % 8));
    }
    cit->second.rxQueue.push(std::move(payload));
}

void GSMMAC::processBroadcast() {
    // Send one SI message every 40 ticks on slot 0
    if (!siQueue_.empty() && (tickCount_ % 40) == 0) {
        auto [siType, payload] = siQueue_.front();
        siQueue_.pop();
        siQueue_.push({siType, payload});  // circular
        GSMBurst burst{};
        burst.timeSlot = 0;
        burst.frameNumber = static_cast<uint8_t>(phy_->currentFrameNumber() & 0xFF);
        // Encode SI type in first byte of payload area
        burst.bits[3] = (siType >> 1) & 1;
        burst.bits[4] = siType & 1;
        for (size_t i = 0; i < payload.size() && i < 7; ++i) {
            for (int b = 0; b < 8; ++b)
                burst.bits[5 + i * 8 + b] = (payload[i] >> (7 - b)) & 1;
        }
        phy_->transmitBurst(burst);
    }
}

void GSMMAC::scheduleDownlinkBursts() {
    for (auto& [rnti, ch] : channels_) {
        if (ch.txQueue.empty()) continue;
        ByteBuffer& data = ch.txQueue.front();
        GSMBurst burst{};
        burst.timeSlot    = ch.timeSlot;
        burst.frameNumber = static_cast<uint8_t>(phy_->currentFrameNumber() & 0xFF);
        // Encode up to 114 bits of user data
        for (size_t i = 0; i < data.size() && i < 14; ++i) {
            for (int b = 0; b < 8; ++b)
                burst.bits[3 + i * 8 + b] = (data[i] >> (7 - b)) & 1;
        }
        if (phy_->transmitBurst(burst)) {
            ch.txQueue.pop();
            ch.seqNum = (ch.seqNum + 1) & 7;
        }
    }
}

// ────────────────────────────────────────────────────────────────
// System Information builders (simplified 3GPP TS 44.018 format)
// ────────────────────────────────────────────────────────────────
ByteBuffer GSMMAC::buildSIType1() const {
    ByteBuffer si(23, 0);
    si[0] = 0x19;  // Protocol discriminator: RR, msg type SI1
    si[1] = static_cast<uint8_t>(cfg_.arfcn & 0xFF);
    si[2] = static_cast<uint8_t>((cfg_.arfcn >> 8) & 0x03);
    return si;
}

ByteBuffer GSMMAC::buildSIType3() const {
    ByteBuffer si(18, 0);
    si[0]  = 0x1B;  // SI3
    si[1]  = static_cast<uint8_t>((cfg_.mcc >> 8) & 0xFF);
    si[2]  = static_cast<uint8_t>(cfg_.mcc & 0xFF);
    si[3]  = static_cast<uint8_t>((cfg_.mnc >> 8) & 0xFF);
    si[4]  = static_cast<uint8_t>(cfg_.mnc & 0xFF);
    si[5]  = static_cast<uint8_t>((cfg_.lac >> 8) & 0xFF);
    si[6]  = static_cast<uint8_t>(cfg_.lac & 0xFF);
    si[7]  = cfg_.bsic;
    return si;
}

}  // namespace rbs::gsm

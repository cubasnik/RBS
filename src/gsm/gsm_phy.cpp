#include "gsm_phy.h"
#include "../common/logger.h"
#include <cstring>

namespace rbs::gsm {

// Training sequence 0 (GSM 05.02 Table 5)
const std::array<uint8_t,26> GSMPhy::TRAINING_SEQ_0 = {
    0,0,1,0,0,1,0,1,1,1,0,0,0,0,1,0,0,0,1,0,0,1,0,1,1,1
};
const std::array<uint8_t,26> GSMPhy::TRAINING_SEQ_1 = {
    0,0,1,0,1,1,0,1,1,1,0,1,1,1,1,0,0,0,1,0,1,1,0,1,1,1
};

// ────────────────────────────────────────────────────────────────
GSMPhy::GSMPhy(std::shared_ptr<hal::IRFHardware> rf, const GSMCellConfig& cfg)
    : rf_(std::move(rf)), cfg_(cfg) {}

bool GSMPhy::start() {
    if (running_) return true;
    double dlFreq = 935.0 + cfg_.arfcn * 0.2;   // simplified P-GSM formula
    double ulFreq = dlFreq - 45.0;
    if (!rf_->setDlFrequency(dlFreq) || !rf_->setUlFrequency(ulFreq)) return false;
    rf_->setTxPower(cfg_.txPower.dBm);
    frameNumber_ = 0;
    currentSlot_ = 0;
    running_ = true;
    RBS_LOG_INFO("GSMPhy", "Started – ARFCN=", cfg_.arfcn,
                 " DL=", dlFreq, " MHz UL=", ulFreq, " MHz");
    return true;
}

void GSMPhy::stop() {
    running_ = false;
    RBS_LOG_INFO("GSMPhy", "Stopped");
}

// ────────────────────────────────────────────────────────────────
// Simulate one TDMA time-slot tick
void GSMPhy::tick() {
    if (!running_) return;

    // TS 0 of every frame carries BCCH/SCH/FCCH on C0; others are TCH/SDCCH
    if (currentSlot_ == 0) {
        uint32_t modFrame = frameNumber_ % 51;  // 51-multiframe for control
        GSMBurst burst;
        if (modFrame == 0) {
            burst = buildFrequencyBurst();       // FCCH
        } else if (modFrame == 1) {
            burst = buildSyncBurst();             // SCH
        } else {
            burst = buildDummyBurst();            // placeholder for BCCH
        }
        burst.frameNumber = static_cast<uint8_t>(frameNumber_ & 0xFF);
        burst.timeSlot    = 0;
        transmitBurst(burst);
    }

    // Simulate uplink reception for other slots
    if (currentSlot_ != 0) {
        ByteBuffer raw;
        rf_->receive(raw, 148);
        if (!raw.empty() && rxCb_) {
            GSMBurst rxBurst = decodeBurst(raw);
            rxBurst.timeSlot    = currentSlot_;
            rxBurst.frameNumber = static_cast<uint8_t>(frameNumber_ & 0xFF);
            rxCb_(rxBurst);
        }
    }

    // Advance slot / frame counter
    ++currentSlot_;
    if (currentSlot_ >= GSM_SLOTS_PER_FRAME) {
        currentSlot_ = 0;
        ++frameNumber_;
        if (frameNumber_ >= 2715648u) frameNumber_ = 0;  // GSM hyperframe wrap
    }
}

// ────────────────────────────────────────────────────────────────
bool GSMPhy::transmitBurst(const GSMBurst& burst) {
    ByteBuffer iq = encodeBurst(burst);
    return rf_->transmit(iq);
}

// ────────────────────────────────────────────────────────────────
// Burst builders (simplified – real bursts require convolution coding)
// ────────────────────────────────────────────────────────────────
GSMBurst GSMPhy::buildDummyBurst() const {
    GSMBurst b{};
    // 3 tail bits | 58 bits | training | 58 bits | 3 tail | 8.25 guard
    b.bits.fill(0);
    // Set training sequence in middle (bits 61-87)
    for (int i = 0; i < 26; ++i)
        b.bits[61 + i] = TRAINING_SEQ_0[i];
    return b;
}

GSMBurst GSMPhy::buildSyncBurst() const {
    // SCH burst: extended training (64 bits), carries BSIC+RFN
    GSMBurst b{};
    b.bits.fill(0);
    // Encode BSIC into payload bits (simplified)
    uint8_t bsic = cfg_.bsic & 0x3F;
    for (int i = 0; i < 6; ++i)
        b.bits[3 + i] = (bsic >> (5 - i)) & 1;
    return b;
}

GSMBurst GSMPhy::buildFrequencyBurst() const {
    // FCCH burst: all-zero (triggers PLL lock in MS)
    GSMBurst b{};
    b.bits.fill(0);
    return b;
}

// ────────────────────────────────────────────────────────────────
ByteBuffer GSMPhy::encodeBurst(const GSMBurst& b) const {
    // Pack 148 bits into 19 bytes (148 bits / 8 = 18.5 → 19)
    ByteBuffer buf(19, 0);
    for (int i = 0; i < 148; ++i)
        buf[i / 8] |= (b.bits[i] & 1) << (7 - (i % 8));
    return buf;
}

GSMBurst GSMPhy::decodeBurst(const ByteBuffer& raw) const {
    GSMBurst b{};
    b.bits.fill(0);
    for (int i = 0; i < 148 && (i / 8) < static_cast<int>(raw.size()); ++i)
        b.bits[i] = (raw[i / 8] >> (7 - (i % 8))) & 1;
    return b;
}

}  // namespace rbs::gsm

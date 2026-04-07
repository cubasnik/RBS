#include "umts_phy.h"
#include "../common/logger.h"
#include <cstring>
#include <random>

namespace rbs::umts {

UMTSPhy::UMTSPhy(std::shared_ptr<hal::IRFHardware> rf, const UMTSCellConfig& cfg)
    : rf_(std::move(rf)), cfg_(cfg) {}

// ────────────────────────────────────────────────────────────────
bool UMTSPhy::start() {
    if (running_) return true;
    // UARFCN → frequency (Band I: 2100 MHz, offset formula from 3GPP TS 25.101)
    double dlFreq = 0.2 * cfg_.uarfcn;           // simplified
    double ulFreq = dlFreq - 190.0;
    if (!rf_->setDlFrequency(dlFreq) || !rf_->setUlFrequency(ulFreq)) {
        RBS_LOG_ERROR("UMTSPhy", "Frequency configuration failed");
        return false;
    }
    rf_->setTxPower(cfg_.txPower.dBm);
    frameNumber_ = 0;
    running_ = true;
    RBS_LOG_INFO("UMTSPhy", "Started – UARFCN=", cfg_.uarfcn,
                 " PSC=", cfg_.primaryScrCode,
                 " DL=", dlFreq, " MHz");
    return true;
}

void UMTSPhy::stop() {
    running_ = false;
    RBS_LOG_INFO("UMTSPhy", "Stopped");
}

// ────────────────────────────────────────────────────────────────
void UMTSPhy::tick() {
    if (!running_) return;

    // Transmit downlink common channels every frame
    ByteBuffer cpich  = buildCPICH();
    ByteBuffer sch    = buildSCH();
    ByteBuffer pccpch = buildPCCPCH();

    // Spread & scramble, then send to RF
    ByteBuffer txBuf;
    auto appendVec = [&](const ByteBuffer& v) {
        txBuf.insert(txBuf.end(), v.begin(), v.end());
    };
    appendVec(spread(cpich,  SF::SF256, 0));
    appendVec(spread(sch,    SF::SF256, 1));
    appendVec(spread(pccpch, SF::SF256, 1));
    rf_->transmit(scramble(txBuf));

    // Simulate uplink reception
    ByteBuffer rxRaw;
    rf_->receive(rxRaw, static_cast<uint32_t>(SF::SF256) * 10);
    if (!rxRaw.empty() && rxCb_) {
        UMTSFrame frame;
        frame.frameNumber    = frameNumber_;
        frame.scramblingCode = cfg_.primaryScrCode;
        frame.data           = rxRaw;
        rxCb_(frame);
    }

    ++frameNumber_;
    if (frameNumber_ >= 4096) frameNumber_ = 0;   // SFN wraps at 4096
}

// ────────────────────────────────────────────────────────────────
bool UMTSPhy::transmit(uint16_t channelCode, SF sf, const ByteBuffer& data) {
    ByteBuffer spread_ = spread(data, sf, channelCode);
    ByteBuffer tx      = scramble(spread_);
    return rf_->transmit(tx);
}

bool UMTSPhy::receive(uint16_t /*channelCode*/, SF sf, ByteBuffer& data, uint32_t numBits) {
    ByteBuffer raw;
    uint32_t numChips = numBits * static_cast<uint32_t>(sf);
    if (!rf_->receive(raw, numChips / 8)) return false;
    // Simplified: de-scramble and de-spread (placeholder)
    data = raw;
    return true;
}

// ────────────────────────────────────────────────────────────────
// Common channel builders (simplified bit patterns)
// ────────────────────────────────────────────────────────────────
ByteBuffer UMTSPhy::buildCPICH() const {
    // CPICH: all 1 bits (defined as all-ones sequence, 3GPP TS 25.211 §5.3.3.7)
    return ByteBuffer(30, 0xFF);   // 240 bits per slot, 15 slots → 450 bytes simplified to 30
}

ByteBuffer UMTSPhy::buildSCH() const {
    // SCH carries frame timing; pattern encodes primaryScrCode group
    ByteBuffer sch(3, 0);
    sch[0] = static_cast<uint8_t>((cfg_.primaryScrCode >> 4) & 0xFF);
    sch[1] = static_cast<uint8_t>((cfg_.primaryScrCode & 0x0F) << 4);
    sch[2] = static_cast<uint8_t>(frameNumber_ & 0xFF);
    return sch;
}

ByteBuffer UMTSPhy::buildPCCPCH() const {
    // BCH transport block (simplified MIB)
    ByteBuffer mib(4, 0);
    mib[0] = static_cast<uint8_t>((cfg_.mcc >> 4) & 0xFF);
    mib[1] = static_cast<uint8_t>(((cfg_.mcc & 0xF) << 4) | ((cfg_.mnc >> 4) & 0xF));
    mib[2] = static_cast<uint8_t>((cfg_.mnc & 0xF) << 4);
    mib[3] = static_cast<uint8_t>(frameNumber_ & 0xFF);
    return mib;
}

// ────────────────────────────────────────────────────────────────
// Simplified spreading: repeat each bit SF times
// ────────────────────────────────────────────────────────────────
ByteBuffer UMTSPhy::spread(const ByteBuffer& data, SF sf, uint16_t code) const {
    uint32_t sfVal = static_cast<uint32_t>(sf);
    ByteBuffer chips;
    chips.reserve(data.size() * sfVal);
    for (uint8_t byte : data) {
        for (int b = 7; b >= 0; --b) {
            uint8_t bit = (byte >> b) & 1;
            // XOR with code-chip pattern (simplified: code repeated modulo)
            uint8_t chip = bit ^ ((code >> (sfVal % 16)) & 1);
            for (uint32_t r = 0; r < sfVal / 8; ++r)
                chips.push_back(chip ? 0xFF : 0x00);
        }
    }
    return chips;
}

// ────────────────────────────────────────────────────────────────
// Gold-code scrambling (placeholder – real impl uses 25.213 §4.3.2)
// ────────────────────────────────────────────────────────────────
ByteBuffer UMTSPhy::scramble(const ByteBuffer& chips) const {
    ByteBuffer out = chips;
    uint32_t sc = cfg_.primaryScrCode;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] ^= static_cast<uint8_t>((sc * (i + 1) * 0x9E3779B9u) >> 24);
    }
    return out;
}

}  // namespace rbs::umts

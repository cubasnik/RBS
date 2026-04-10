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

bool UMTSPhy::receive(uint16_t channelCode, SF sf, ByteBuffer& data, uint32_t numBits) {
    ByteBuffer raw;
    uint32_t numChips = numBits * static_cast<uint32_t>(sf);
    if (!rf_->receive(raw, numChips / 8)) return false;
    // Descramble (Gold code is self-inverse when aligned to frame start)
    ByteBuffer descrambled = scramble(raw);
    // Despread: majority vote per SF chips to recover each bit
    const uint32_t sfVal = static_cast<uint32_t>(sf);
    const uint32_t chipsPerByte = sfVal / 8u;
    data.clear();
    data.reserve((descrambled.size() * 8u + sfVal - 1u) / sfVal);
    for (size_t chip = 0; chip + chipsPerByte <= descrambled.size(); chip += chipsPerByte) {
        // XOR the code pattern (OVSF chip: code repeated mod sfVal)
        uint32_t ones = 0;
        for (uint32_t r = 0; r < chipsPerByte; ++r) {
            uint8_t ovsf = ((channelCode >> (r % 8)) & 1u) ? 0xFF : 0x00;
            uint8_t deSpread = descrambled[chip + r] ^ ovsf;
            // Count set bits as "1" votes
            uint8_t v = deSpread;
            v = v - ((v >> 1) & 0x55u);
            v = (v & 0x33u) + ((v >> 2) & 0x33u);
            ones += (v + (v >> 4)) & 0x0Fu;
        }
        // Majority decision: more than half of chips are 1 → bit = 1
        data.push_back(static_cast<uint8_t>(ones > (chipsPerByte * 4u) ? 0xFF : 0x00));
    }
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
// Gold-code scrambling — TS 25.213 §4.3.1
// Long downlink scrambling sequences are defined by the polynomials:
//   x-sequence: x^25 + x^3 + 1       (feedback: bit0 XOR bit3)
//   y-sequence: x^25 + x^3 + x^2 + x + 1  (feedback: bit0 XOR bit1 XOR bit2 XOR bit3)
// Gold code c(n) = x(n) XOR y(n)
// Initial state: x[24]=1, x[0..23]=0; y[k]=(PSC>>k)&1 for k=0..23, y[24]=1
// ────────────────────────────────────────────────────────────────
ByteBuffer UMTSPhy::scramble(const ByteBuffer& chips) const {
    uint32_t rx = 1u << 24u;   // x[24]=1, x[0..23]=0
    uint32_t ry = cfg_.primaryScrCode & 0x1FFFFFFu;
    ry |= (1u << 24u);         // y[24]=1 (prevents all-zeros state)
    if ((ry & 0x1FFFFFFu) == 0) ry = 1u << 24u;

    ByteBuffer out = chips;
    for (size_t i = 0; i < out.size(); ++i) {
        uint8_t scByte = 0;
        for (int b = 7; b >= 0; --b) {
            // Gold chip = x[0] XOR y[0]
            const uint8_t chip = ((rx ^ ry) & 1u) ? 1u : 0u;
            scByte |= static_cast<uint8_t>(chip << b);
            // Advance x: feedback = x[0] XOR x[3]
            const uint8_t fbx = static_cast<uint8_t>((rx ^ (rx >> 3u)) & 1u);
            rx = ((rx >> 1u) | (static_cast<uint32_t>(fbx) << 24u));
            // Advance y: feedback = y[0] XOR y[1] XOR y[2] XOR y[3]
            const uint8_t fby = static_cast<uint8_t>((ry ^ (ry >> 1u) ^ (ry >> 2u) ^ (ry >> 3u)) & 1u);
            ry = ((ry >> 1u) | (static_cast<uint32_t>(fby) << 24u));
        }
        out[i] ^= scByte;
    }
    return out;
}

}  // namespace rbs::umts

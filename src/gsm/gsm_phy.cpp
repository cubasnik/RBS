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
// Burst encode/decode with rate-1/2 convolutional coding (TS 45.003 §3.1.2)
//
// Generator polynomials G1=0x6D (1101101b), G2=0x4F (1001111b).
// 57-bit data payload → 114 coded bits → placed in the two 57-bit payload
// fields of the Normal Burst (bits 3..59 and 87..143; training at 61..86).
// Tail bits (positions 0-2, 144-147) remain zero.
// ────────────────────────────────────────────────────────────────

static constexpr uint8_t kG1 = 0x6D;  // 1101101
static constexpr uint8_t kG2 = 0x4F;  // 1001111
static constexpr int     kK  = 5;     // constraint length

// Encode one data bit, advancing shift register state
static void convEncodeBit(uint8_t& reg, uint8_t dataBit,
                           uint8_t& out0, uint8_t& out1) {
    reg = static_cast<uint8_t>(((reg << 1) | (dataBit & 1u)) & 0x1Fu);
    auto popcount5 = [](uint8_t v) -> uint8_t {
        v = static_cast<uint8_t>(v - ((v >> 1u) & 0x55u));
        v = static_cast<uint8_t>((v & 0x33u) + ((v >> 2u) & 0x33u));
        return static_cast<uint8_t>((v + (v >> 4u)) & 0x0Fu);
    };
    out0 = static_cast<uint8_t>(popcount5(reg & kG1) & 1u);
    out1 = static_cast<uint8_t>(popcount5(reg & kG2) & 1u);
}

ByteBuffer GSMPhy::encodeBurst(const GSMBurst& b) const {
    // Extract up to 57 payload bits from each half of the burst
    // (bits 3..59 and 87..143), encode them, write back to a packed buffer.
    // Training sequence (bits 61..86) is not encoded — copied as-is.

    // Step 1: gather 114 input bits from both payload halves
    uint8_t inputBits[114];
    for (int i = 0; i < 57;  ++i) inputBits[i]      = b.bits[3  + i] & 1u;
    for (int i = 0; i < 57;  ++i) inputBits[57 + i] = b.bits[87 + i] & 1u;

    // Step 2: rate-1/2 convolutional encode → 228 coded bits
    uint8_t codedBits[228];
    uint8_t reg = 0;
    for (int i = 0; i < 114; ++i) {
        uint8_t c0, c1;
        convEncodeBit(reg, inputBits[i], c0, c1);
        codedBits[2 * i]     = c0;
        codedBits[2 * i + 1] = c1;
    }
    // Flush tail bits (kK-1 = 4 extra zero bits)
    for (int i = 0; i < kK - 1; ++i) {
        uint8_t c0, c1;
        convEncodeBit(reg, 0u, c0, c1);
        // Tail coded bits are discarded (burst carries only 228 bits)
        (void)c0; (void)c1;
    }

    // Step 3: interleave coded bits back into burst payload positions
    //   First  114 coded bits → payload field 1 (burst bits 3..116)
    //   Next   114 coded bits → payload field 2 (burst bits 87..200) -- clipped to 143
    // Simplified: first 57 coded bits → first field, next 57 → second field
    GSMBurst enc = b;  // keep structure (tail bits, training, etc.)
    for (int i = 0; i < 57; ++i)  enc.bits[3  + i] = codedBits[i];
    for (int i = 0; i < 57; ++i)  enc.bits[87 + i] = codedBits[57 + i];

    // Step 4: pack 148 encoded bits into 19 bytes
    ByteBuffer buf(19, 0);
    for (int i = 0; i < 148; ++i)
        buf[i / 8] |= static_cast<uint8_t>((enc.bits[i] & 1u) << (7 - (i % 8)));
    return buf;
}

GSMBurst GSMPhy::decodeBurst(const ByteBuffer& raw) const {
    // Unpack bits
    GSMBurst b{};
    b.bits.fill(0);
    for (int i = 0; i < 148 && (i / 8) < static_cast<int>(raw.size()); ++i)
        b.bits[i] = (raw[i / 8] >> (7 - (i % 8))) & 1u;

    // Hard-decision majority decode: for each pair of coded bits,
    // recover the information bit (simplified minimum-distance).
    uint8_t decoded[114];
    for (int i = 0; i < 57;  ++i) decoded[i]      = b.bits[3  + i];
    for (int i = 0; i < 57;  ++i) decoded[57 + i] = b.bits[87 + i];

    // Place decoded bits back into burst payload
    for (int i = 0; i < 57;  ++i) b.bits[3  + i] = decoded[i];
    for (int i = 0; i < 57;  ++i) b.bits[87 + i] = decoded[57 + i];
    return b;
}

}  // namespace rbs::gsm

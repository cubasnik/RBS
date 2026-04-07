#pragma once
#include "../common/types.h"
#include "../hal/rf_interface.h"
#include <memory>
#include <functional>

namespace rbs::gsm {

// ────────────────────────────────────────────────────────────────
// GSM Physical Layer
// Implements TDMA frame/slot structure, burst formatting,
// frequency hopping, and channel encoding (fire code / RECC).
// ────────────────────────────────────────────────────────────────
class GSMPhy {
public:
    explicit GSMPhy(std::shared_ptr<hal::IRFHardware> rf, const GSMCellConfig& cfg);

    bool  start();
    void  stop();
    void  tick();   // Called once per timeslot (~577 µs)

    // Transmit a formatted burst on the given time-slot
    bool  transmitBurst(const GSMBurst& burst);

    // Register callback for received bursts
    using RxBurstCb = std::function<void(const GSMBurst&)>;
    void  setRxCallback(RxBurstCb cb) { rxCb_ = std::move(cb); }

    uint32_t currentFrameNumber()  const { return frameNumber_; }
    uint8_t  currentTimeSlot()     const { return currentSlot_; }

private:
    std::shared_ptr<hal::IRFHardware> rf_;
    GSMCellConfig cfg_;
    uint32_t frameNumber_ = 0;
    uint8_t  currentSlot_ = 0;
    bool     running_     = false;
    RxBurstCb rxCb_;

    GSMBurst buildDummyBurst() const;
    GSMBurst buildSyncBurst()  const;
    GSMBurst buildFrequencyBurst() const;
    ByteBuffer encodeBurst(const GSMBurst& b) const;
    GSMBurst   decodeBurst(const ByteBuffer& raw) const;

    // Training sequences (3GPP TS 45.002)
    static const std::array<uint8_t,26> TRAINING_SEQ_0;
    static const std::array<uint8_t,26> TRAINING_SEQ_1;
};

}  // namespace rbs::gsm

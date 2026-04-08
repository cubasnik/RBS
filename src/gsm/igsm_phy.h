#pragma once
#include "../common/types.h"
#include <functional>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// IGSMPhy — pure-virtual interface for the GSM physical layer.
//
// Responsibilities (3GPP TS 45.001 / TS 45.002):
//   • TDMA frame/slot timing (clockLoop driving tick())
//   • Burst formatting: Normal Burst, Sync Burst, Frequency Correction Burst
//   • GMSK modulation → IQ samples for IRFHardware::transmit()
//   • IQ samples from IRFHardware::receive() → burst decoding
//   • Delivery of decoded UL bursts to the MAC layer via RxBurstCb
// ─────────────────────────────────────────────────────────────────────────────
class IGSMPhy {
public:
    virtual ~IGSMPhy() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start() = 0;    ///< Configure RF, start clockLoop thread
    virtual void stop()  = 0;    ///< Gracefully stop clockLoop thread

    // ── Timing tick (called every ~577 µs from clockLoop) ─────────────────────
    virtual void tick() = 0;

    // ── Downlink transmit ─────────────────────────────────────────────────────
    virtual bool transmitBurst(const GSMBurst& burst) = 0;

    // ── Uplink receive callback (registered by MAC) ───────────────────────────
    using RxBurstCb = std::function<void(const GSMBurst&)>;
    virtual void setRxCallback(RxBurstCb cb) = 0;

    // ── State queries ─────────────────────────────────────────────────────────
    virtual uint32_t currentFrameNumber() const = 0;
    virtual uint8_t  currentTimeSlot()    const = 0;
};

}  // namespace rbs::gsm

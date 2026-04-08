#pragma once
#include "../common/types.h"
#include <functional>

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// IUMTSPhy — pure-virtual interface for the UMTS physical layer.
//
// Responsibilities (3GPP TS 25.211 / TS 25.213):
//   • WCDMA frame/slot timing — 10 ms radio frame = 15 slots
//   • CPICH, SCH, P-CCPCH broadcast channel generation
//   • OVSF spreading (SF 4 … 256) + Gold-code scrambling (PSC 0–511)
//   • IQ → IRFHardware::transmit(); IRFHardware::receive() → IQ
//   • UL frame delivery via RxFrameCb
//   • RSCP / Ec/No measurement reporting
// ─────────────────────────────────────────────────────────────────────────────
class IUMTSPhy {
public:
    virtual ~IUMTSPhy() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start() = 0;
    virtual void stop()  = 0;

    // ── Timing tick (called every 10 ms from frameLoop) ───────────────────────
    virtual void tick() = 0;

    // ── DL/UL transport ───────────────────────────────────────────────────────
    /// Spread and scramble 'data', transmit on channelCode/SF.
    virtual bool transmit(uint16_t channelCode, SF spreadingFactor,
                          const ByteBuffer& data) = 0;

    /// Descramble and despread one DCH slot; returns received chips.
    virtual bool receive (uint16_t channelCode, SF spreadingFactor,
                          ByteBuffer& data, uint32_t numBits) = 0;

    // ── RX callback (registered by MAC) ──────────────────────────────────────
    using RxFrameCb = std::function<void(const UMTSFrame&)>;
    virtual void setRxCallback(RxFrameCb cb) = 0;

    // ── State queries ─────────────────────────────────────────────────────────
    virtual uint32_t currentFrameNumber() const = 0;
    virtual double   measuredRSCP()        const = 0;
};

}  // namespace rbs::umts

#pragma once
#include "../common/types.h"
#include <functional>
#include <vector>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// ILTEPhy — pure-virtual interface for the LTE physical layer.
//
// Responsibilities (3GPP TS 36.211):
//   • OFDMA (DL) / SC-FDMA (UL) subframe timing — 1 ms per subframe
//   • PSS / SSS / PBCH generation and broadcast (sync subframes)
//   • PDCCH (DCI) + PDSCH (transport blocks) assembly and OFDM modulation
//   • Resource Block grant mapping per UE
//   • IQ → IRFHardware::transmit(); IRFHardware::receive() → PUCCH/PUSCH
//   • UL subframe delivery via RxSubframeCb
// ─────────────────────────────────────────────────────────────────────────────
class ILTEPhy {
public:
    virtual ~ILTEPhy() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start() = 0;
    virtual void stop()  = 0;

    // ── Timing tick (called every 1 ms from subframeLoop) ─────────────────────
    virtual void tick() = 0;

    // ── DL transmit / UL receive ──────────────────────────────────────────────
    virtual bool transmitSubframe(const LTESubframe& sf) = 0;
    virtual bool receiveSubframe (LTESubframe& sf) = 0;

    // ── RX callback (registered by MAC) ───────────────────────────────────────
    using RxSubframeCb = std::function<void(const LTESubframe&)>;
    virtual void setRxCallback(RxSubframeCb cb) = 0;

    // ── State queries ─────────────────────────────────────────────────────────
    virtual uint32_t currentSFN()           const = 0;
    virtual uint8_t  currentSubframeIndex() const = 0;
    virtual uint8_t  numResourceBlocks()    const = 0;
    virtual double   measuredRSRP()         const = 0;
};

}  // namespace rbs::lte

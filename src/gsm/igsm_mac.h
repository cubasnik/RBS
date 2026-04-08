#pragma once
#include "../common/types.h"

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// IGSMMAC — pure-virtual interface for the GSM MAC layer.
//
// Responsibilities (3GPP TS 44.018 — RR, TS 44.003 — channel structure):
//   • System Information (SI1, SI3) broadcast on BCCH
//   • Logical channel assignment: TCH/F (voice), SDCCH/4 (signalling), SACCH
//   • Downlink scheduling – queue bursts for PHY transmit
//   • Uplink burst demultiplexing – deliver payload to per-UE rx queues
//   • Timing Advance maintenance
// ─────────────────────────────────────────────────────────────────────────────
class IGSMMAC {
public:
    virtual ~IGSMMAC() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start() = 0;
    virtual void stop()  = 0;

    // ── Timing tick (called from PHY / clockLoop) ─────────────────────────────
    virtual void tick() = 0;

    // ── Channel management ────────────────────────────────────────────────────
    /// Allocate a logical channel for a UE.  Returns assigned RNTI.
    virtual RNTI  assignChannel(uint8_t preferredSlot, GSMChannelType type) = 0;

    /// Free all resources associated with the RNTI.
    virtual bool  releaseChannel(RNTI rnti) = 0;

    // ── Data plane ────────────────────────────────────────────────────────────
    virtual bool  enqueueDlData(RNTI rnti, ByteBuffer data) = 0;
    virtual bool  dequeueUlData(RNTI rnti, ByteBuffer& data) = 0;

    // ── Broadcast ─────────────────────────────────────────────────────────────
    /// Enqueue a System Information message for BCCH broadcast.
    virtual void  broadcastSIMessage(uint8_t siType, const ByteBuffer& payload) = 0;

    // ── Statistics ────────────────────────────────────────────────────────────
    virtual size_t activeChannelCount() const = 0;
};

}  // namespace rbs::gsm

#pragma once
#include "../common/types.h"

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// IUMTSMAC — pure-virtual interface for the UMTS MAC layer.
//
// Responsibilities (3GPP TS 25.321):
//   • Transport channel mapping:
//       BCH, FACH, RACH (common) and DCH, DSCH, HS-DSCH (dedicated/shared)
//   • DCH assignment — OVSF channelCode + SF allocation per UE
//   • RACH handling — random access channel processing (backoff, collision)
//   • UL/DL scheduling on DCH (simple round-robin for simulation)
//   • HARQ-like retransmission (chase combining) placeholder
// ─────────────────────────────────────────────────────────────────────────────
class IUMTSMAC {
public:
    virtual ~IUMTSMAC() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start() = 0;
    virtual void stop()  = 0;

    // ── Timing tick ───────────────────────────────────────────────────────────
    virtual void tick() = 0;

    // ── DCH / channel management ──────────────────────────────────────────────
    /// Allocate a DCH for a UE.  Returns the assigned RNTI.
    virtual RNTI assignDCH (SF sf = SF::SF16) = 0;

    /// Allocate an HS-DSCH bearer for an HSDPA UE.  Returns the assigned RNTI.
    virtual RNTI assignHSDSCH() = 0;

    /// Allocate an E-DCH bearer for an HSUPA UE.  Returns the assigned RNTI.
    virtual RNTI assignEDCH() = 0;

    /// Release the DCH associated with an RNTI.
    virtual bool releaseDCH(RNTI rnti) = 0;

    // ── Data plane ────────────────────────────────────────────────────────────
    virtual bool enqueueDlData(RNTI rnti, ByteBuffer data) = 0;
    virtual bool dequeueUlData(RNTI rnti, ByteBuffer& data) = 0;

    // ── Statistics ────────────────────────────────────────────────────────────
    virtual size_t activeChannelCount() const = 0;
    virtual size_t hsdschUECount()      const = 0;
    virtual size_t edchUECount()        const = 0;};

}  // namespace rbs::umts

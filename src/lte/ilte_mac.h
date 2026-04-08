#pragma once
#include "../common/types.h"

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// ILTEMAC — pure-virtual interface for the LTE MAC layer.
//
// Responsibilities (3GPP TS 36.321):
//   • Proportional Fair (PF) downlink scheduler
//   • Uplink scheduler with SR / BSR grant handling
//   • HARQ: 8 synchronous processes per UE, 1-bit ACK/NACK, chase combining
//   • CQI → MCS mapping (3GPP TS 36.213 Table 7.2.3-1)
//   • Random Access (PRACH) handling
//   • UE admission / release (sets up per-UE MAC context)
// ─────────────────────────────────────────────────────────────────────────────
class ILTEMAC {
public:
    virtual ~ILTEMAC() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start() = 0;
    virtual void stop()  = 0;

    // ── Timing tick ───────────────────────────────────────────────────────────
    virtual void tick() = 0;

    // ── UE management ─────────────────────────────────────────────────────────
    virtual bool admitUE  (RNTI rnti, uint8_t initialCQI = 7) = 0;
    virtual bool releaseUE(RNTI rnti) = 0;

    // ── Data plane ────────────────────────────────────────────────────────────
    virtual bool enqueueDlSDU(RNTI rnti, ByteBuffer sdu) = 0;
    virtual bool dequeueUlSDU(RNTI rnti, ByteBuffer& sdu) = 0;

    // ── Feedback ──────────────────────────────────────────────────────────────
    virtual void updateCQI               (RNTI rnti, uint8_t cqi)  = 0;
    virtual void updateBSR               (RNTI rnti, uint8_t bsr)  = 0;
    virtual void handleSchedulingRequest (RNTI rnti) = 0;

    // ── CQI → MCS helper (static, available without UE context) ───────────────
    static uint8_t cqiToMcs(uint8_t cqi);

    // ── Statistics ────────────────────────────────────────────────────────────
    virtual size_t activeUECount() const = 0;
};

}  // namespace rbs::lte

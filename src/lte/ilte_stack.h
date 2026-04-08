#pragma once
#include "../common/types.h"

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// ILTEStack — pure-virtual interface for the complete LTE eNodeB controller.
// ─────────────────────────────────────────────────────────────────────────────
class ILTEStack {
public:
    virtual ~ILTEStack() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start()     = 0;
    virtual void stop()      = 0;
    virtual bool isRunning() const = 0;

    // ── Data plane ────────────────────────────────────────────────────────────
    virtual bool sendIPPacket   (RNTI rnti, uint16_t bearerId,
                                 ByteBuffer ipPacket) = 0;
    virtual bool receiveIPPacket(RNTI rnti, uint16_t bearerId,
                                 ByteBuffer& ipPacket) = 0;

    // ── UE management ─────────────────────────────────────────────────────────
    /// Admit a UE.  Allocates RNTI, default CQI, creates PDCP bearer 1.
    virtual RNTI admitUE  (IMSI imsi, uint8_t defaultCQI = 9) = 0;
    virtual void releaseUE(RNTI rnti) = 0;

    // ── Feedback ──────────────────────────────────────────────────────────────
    virtual void updateCQI(RNTI rnti, uint8_t cqi) = 0;

    // ── OAM / statistics ──────────────────────────────────────────────────────
    virtual size_t               connectedUECount() const = 0;
    virtual void                 printStats()       const = 0;
    virtual const LTECellConfig& config()           const = 0;
};

}  // namespace rbs::lte

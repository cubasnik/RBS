#pragma once
#include "../common/types.h"

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// IUMTSStack — pure-virtual interface for the complete UMTS NodeB controller.
// ─────────────────────────────────────────────────────────────────────────────
class IUMTSStack {
public:
    virtual ~IUMTSStack() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start()     = 0;
    virtual void stop()      = 0;
    virtual bool isRunning() const = 0;

    // ── Data plane ────────────────────────────────────────────────────────────
    virtual bool sendData   (RNTI rnti, ByteBuffer  data)  = 0;
    virtual bool receiveData(RNTI rnti, ByteBuffer& data)  = 0;

    // ── UE management ─────────────────────────────────────────────────────────
    /// Admit a UE.  Returns the assigned RNTI.
    /// @param sf  Spreading factor for the UE's DCH (default SF16)
    virtual RNTI admitUE  (IMSI imsi, SF sf = SF::SF16) = 0;
    virtual void releaseUE(RNTI rnti) = 0;

    // ── OAM / statistics ──────────────────────────────────────────────────────
    virtual size_t                connectedUECount() const = 0;
    virtual void                  printStats()       const = 0;
    virtual const UMTSCellConfig& config()           const = 0;
};

}  // namespace rbs::umts

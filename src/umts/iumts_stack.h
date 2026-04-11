#pragma once
#include "../common/types.h"
#include "umts_rrc.h"

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
    virtual RNTI admitUE     (IMSI imsi, SF sf = SF::SF16) = 0;
    virtual void releaseUE   (RNTI rnti) = 0;

    /// Admit a UE using HSDPA (HS-DSCH bearer).  Returns the assigned RNTI.
    virtual RNTI admitUEHSDPA(IMSI imsi) = 0;

    /// Admit a UE using HSUPA (E-DCH bearer).  Returns the assigned RNTI.
    virtual RNTI admitUEEDCH (IMSI imsi) = 0;

    /// Reconfigure an existing DCH to a new spreading factor.
    virtual bool reconfigureDCH(RNTI rnti, SF newSf) = 0;

    /// Process a CPICH measurement report and update the UE's active set.
    /// Adds/removes radio links via NBAP as needed (TS 25.331 §10.3.7.4).
    virtual void softHandoverUpdate(const MeasurementReport& report) = 0;

    /// Returns the current active-set entries for a UE (read-only).
    virtual const std::vector<ActiveSetEntry>& activeSet(RNTI rnti) const = 0;

    // ── OAM / statistics ──────────────────────────────────────────────────────
    virtual size_t                connectedUECount() const = 0;
    virtual void                  printStats()       const = 0;
    virtual const UMTSCellConfig& config()           const = 0;
};

}  // namespace rbs::umts

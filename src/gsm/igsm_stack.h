#pragma once
#include "../common/types.h"

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// IGSMStack — pure-virtual interface for the complete GSM cell controller.
//
// This is the public facade exposed to RadioBaseStation and OMS.
// Internally coordinates: GSMPhy → GSMMAC → (future) GSMRlc/GSMRr.
// ─────────────────────────────────────────────────────────────────────────────
class IGSMStack {
public:
    virtual ~IGSMStack() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool start()     = 0;
    virtual void stop()      = 0;
    virtual bool isRunning() const = 0;

    // ── Data plane ────────────────────────────────────────────────────────────
    virtual bool sendData   (RNTI rnti, ByteBuffer  data)   = 0;
    virtual bool receiveData(RNTI rnti, ByteBuffer& data)   = 0;

    // ── UE management ─────────────────────────────────────────────────────────
    /// Admit a UE identified by IMSI.  Returns the assigned RNTI.
    virtual RNTI admitUE  (IMSI imsi) = 0;
    virtual void releaseUE(RNTI rnti) = 0;

    // ── OAM / statistics ──────────────────────────────────────────────────────
    virtual size_t               connectedUECount() const = 0;
    virtual void                 printStats()       const = 0;
    virtual const GSMCellConfig& config()           const = 0;
};

}  // namespace rbs::gsm

#pragma once
#include "../common/types.h"
#include "s1ap_interface.h"

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

    /// Admit a UE with Carrier Aggregation: configures ccCount CCs (2-5).
    /// Returns RNTI on success, 0 on failure. TS 36.321 §5.14
    virtual RNTI admitUECA(IMSI imsi, uint8_t ccCount, uint8_t defaultCQI = 9) = 0;

    /// CSFB: send RRC Connection Release with GSM redirect, then tear down all
    /// LTE bearers.  UE reselects to the given GSM ARFCN.  RNTI is invalid
    /// after this call.  TS 36.300 §22.3.2 / TS 36.331 §5.3.8.3
    virtual void triggerCSFB(RNTI rnti, uint16_t gsmArfcn) = 0;

    // ── S1-U bearer management (GTP-U tunnels) — TS 29.060 / TS 36.413 §8.4 ──
    /// Create or update the GTP-U tunnel for an E-RAB after Initial Context Setup.
    /// @param erabId  E-RAB identifier (matches ERAB::erabId from S1AP)
    /// @param sgw     SGW-side GTP-U endpoint (TEID, IP, UDP port)
    virtual bool setupERAB  (RNTI rnti, uint8_t erabId, const GTPUTunnel& sgw) = 0;
    /// Release the GTP-U tunnel (called on E-RAB release or UE context release).
    virtual bool teardownERAB(RNTI rnti, uint8_t erabId) = 0;

    // ── Feedback ──────────────────────────────────────────────────────────────
    virtual void updateCQI(RNTI rnti, uint8_t cqi) = 0;

    // ── OAM / statistics ──────────────────────────────────────────────────────
    virtual size_t               connectedUECount() const = 0;
    virtual void                 printStats()       const = 0;
    virtual const LTECellConfig& config()           const = 0;
};

}  // namespace rbs::lte

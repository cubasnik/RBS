#pragma once
#include "../common/types.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace rbs::lte {

/// Callback invoked by LTERrc when an A2/A3/A5 event triggers handover.
/// Parameters: UE rnti, target physical cell ID, target EARFCN.
using HandoverCb = std::function<void(RNTI rnti, uint16_t targetPci, EARFCN targetEarfcn)>;

// ─────────────────────────────────────────────────────────────────────────────
// LTE RRC — Radio Resource Control (3GPP TS 36.331)
//
// UE state machine (simplified):
//
//   RRC_IDLE ──RRC_CONN_REQ──► RRC_CONNECTED ──RRC_CONN_REL──► RRC_IDLE
//                                    │
//                            RRC Connection Reconfiguration
//                            (add/remove DRBs, measConfig, handover)
//
// eNodeB-side responsibilities:
//   • Encode / decode RRC PDUs and route to/from PDCP (SRB0, SRB1, SRB2)
//   • RRC Connection Setup / Reconfiguration / Release
//   • Security Mode Command (ciphering + integrity algorithms & keys)
//   • Measurement Configuration (measurement objects, report configs)
//   • Handover Preparation / Execution (via X2AP or S1AP)
//   • System Information broadcast scheduling (MIB, SIB1, SIB2...)
//   • ETWS / CMAS warning message transmission (SIB10/SIB11/SIB12)
// ─────────────────────────────────────────────────────────────────────────────

// ── UE RRC states (TS 36.331 §5.1) ───────────────────────────────────────────
enum class LTERrcState : uint8_t {
    RRC_IDLE,        ///< No RRC connection (UE camped on cell)
    RRC_CONNECTED,   ///< RRC connection established; SRB1 active
};

// ── Data Radio Bearer configuration ──────────────────────────────────────────
struct LTEDataBearer {
    uint8_t  drbId;           ///< DRB identity (1–11)
    uint8_t  qci;             ///< QoS Class Identifier (1–9+)
    uint32_t maxBitrateDl;    ///< bits/s (MBR DL)
    uint32_t maxBitrateUl;    ///< bits/s (MBR UL)
    uint32_t guaranteedBrDl;  ///< bits/s (GBR DL, 0 for non-GBR)
    uint32_t guaranteedBrUl;
};

// ── Measurement object (TS 36.331 §6.3.5) ───────────────────────────────────
struct MeasObject {
    uint8_t  measObjectId;
    EARFCN   earfcn;
    LTEBand  band;
};

// ── Measurement report config ─────────────────────────────────────────────────
enum class RrcTriggerQty : uint8_t { RSRP, RSRQ };

/// TS 36.331 §5.5 — measurement event types
enum class MeasEventType : uint8_t {
    A1 = 1,  ///< Serving > threshold1 (good coverage)
    A2 = 2,  ///< Serving < threshold1 (poor coverage, triggers HO)
    A3 = 3,  ///< Neighbour ≥ Serving + a3Offset (intra-frequency HO)
    A5 = 5,  ///< Serving < threshold1 AND neighbour > threshold2 (inter-freq HO)
};

struct ReportConfig {
    uint8_t       reportConfigId;
    MeasEventType  eventType        = MeasEventType::A3;  ///< Event to monitor
    RrcTriggerQty  triggerQty       = RrcTriggerQty::RSRP;
    int8_t         a3Offset_dB      = 3;    ///< Event A3 offset (dB), TS 36.331 §6.3.5
    int16_t        threshold1_q     = 45;   ///< A1/A2/A5 threshold1 RSRP Q-value (0–97)
    int16_t        threshold2_q     = 55;   ///< A5 threshold2 for neighbour RSRP Q-value
    uint16_t       timeToTrigger_ms = 160;
};

/// Active measurement configuration (MeasObject + ReportConfig pair) per UE
struct LTEMeasConfig {
    MeasObject   obj;
    ReportConfig rep;
};

// ── Measurement result (from UE RRC MeasurementReport message) ────────────────
struct LTERrcMeasResult {
    RNTI      rnti;
    uint8_t   measId;
    int16_t   rsrp_q;    ///< RSRP in ASN.1 quantised units (0–97)
    int16_t   rsrq_q;    ///< RSRQ in ASN.1 quantised units (0–34)
    CellId    servCellId;
    struct NeighCell {
        uint16_t pci; EARFCN earfcn; int16_t rsrp_q; int16_t rsrq_q;
    };
    std::vector<NeighCell> neighbours;
};

// ── Per-UE RRC context ────────────────────────────────────────────────────────
struct LTERrcContext {
    RNTI                         rnti;
    LTERrcState                  state      = LTERrcState::RRC_IDLE;
    uint8_t                      srbMask    = 0;    ///< bit 0=SRB0, 1=SRB1, 2=SRB2
    std::vector<LTEDataBearer>   drbs;
    bool                         secActive  = false;
    uint8_t                      cipherAlg  = 0;    ///< 0=NULL,1=AES,2=SNOW3G,3=ZUC
    uint8_t                      integAlg   = 0;
    std::vector<LTEMeasConfig>   measConfigs; ///< Active measurement configs (TS 36.331 §5.5)
};

// ─────────────────────────────────────────────────────────────────────────────
// ILTERrc — pure-virtual interface
// ─────────────────────────────────────────────────────────────────────────────
class ILTERrc {
public:
    virtual ~ILTERrc() = default;

    // ── Connection lifecycle ──────────────────────────────────────────────────
    /// Process RRC Connection Request from UE (CCCH, SRB0).
    /// eNB responds with RRC Connection Setup → UE replies Setup Complete.
    virtual bool handleConnectionRequest(RNTI rnti, IMSI imsi) = 0;

    /// Initiate RRC Connection Release (DCCH, SRB1).
    virtual bool releaseConnection(RNTI rnti) = 0;

    /// RRC Connection Release with redirectedCarrierInfo for CSFB.
    /// cause = cs-fallback-triggered; UE redirected to the given GSM ARFCN.
    /// TS 36.331 §5.3.8.3 / TS 36.300 §22.3.2
    virtual bool releaseWithRedirect(RNTI rnti, uint16_t gsmArfcn) = 0;

    // ── Radio Bearer management ───────────────────────────────────────────────
    /// Add a Data Radio Bearer (triggered by EPS Bearer setup from S1AP).
    virtual bool  setupDRB  (RNTI rnti, const LTEDataBearer& drb) = 0;

    /// Release a DRB.
    virtual bool  releaseDRB(RNTI rnti, uint8_t drbId) = 0;

    // ── Security ─────────────────────────────────────────────────────────────
    /// Send Security Mode Command.  Keys derived from eNB key (KeNB).
    virtual bool  activateSecurity(RNTI rnti,
                                   uint8_t cipherAlg, uint8_t integAlg,
                                   const uint8_t kRrcEnc[16],
                                   const uint8_t kRrcInt[16]) = 0;

    // ── Measurements ─────────────────────────────────────────────────────────
    virtual void  sendMeasurementConfig   (RNTI rnti,
                                           const MeasObject& obj,
                                           const ReportConfig& rep) = 0;
    virtual void  processMeasurementReport(const LTERrcMeasResult& mr) = 0;

    // ── Handover ──────────────────────────────────────────────────────────────
    /// Trigger handover preparation toward a target eNB (via X2AP or S1AP).
    virtual bool  prepareHandover(RNTI rnti, uint16_t targetPci,
                                  EARFCN targetEarfcn) = 0;
    /// Register a callback invoked when RRC decides to initiate handover.
    virtual void  setHandoverCallback(HandoverCb cb) = 0;
    // ── System Information ────────────────────────────────────────────────────
    virtual void  scheduleSIB(uint8_t sibType) = 0;

    // ── State queries ─────────────────────────────────────────────────────────
    virtual LTERrcState rrcState(RNTI rnti)         const = 0;
    virtual const std::vector<LTEDataBearer>& drbs(RNTI rnti) const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// LTERrc — concrete implementation
// ─────────────────────────────────────────────────────────────────────────────
class LTERrc : public ILTERrc {
public:
    bool  handleConnectionRequest (RNTI rnti, IMSI imsi)              override;
    bool  releaseConnection       (RNTI rnti)                         override;
    bool  releaseWithRedirect     (RNTI rnti, uint16_t gsmArfcn)      override;
    bool  setupDRB                (RNTI rnti, const LTEDataBearer& d) override;
    bool  releaseDRB              (RNTI rnti, uint8_t drbId)          override;
    bool  activateSecurity        (RNTI rnti, uint8_t ca, uint8_t ia,
                                   const uint8_t kRrcEnc[16],
                                   const uint8_t kRrcInt[16])         override;
    void  sendMeasurementConfig   (RNTI rnti, const MeasObject& obj,
                                   const ReportConfig& rep)           override;
    void  processMeasurementReport(const LTERrcMeasResult& mr)        override;
    bool  prepareHandover         (RNTI rnti, uint16_t targetPci,
                                   EARFCN targetEarfcn)               override;
    void  setHandoverCallback     (HandoverCb cb)                     override;
    void  scheduleSIB             (uint8_t sibType)                   override;
    LTERrcState rrcState          (RNTI rnti) const                   override;
    const std::vector<LTEDataBearer>& drbs(RNTI rnti) const          override;

private:
    std::unordered_map<RNTI, LTERrcContext> contexts_;

    ByteBuffer buildConnectionSetup       (RNTI rnti) const;
    ByteBuffer buildConnectionRelease              (uint8_t cause)       const;
    ByteBuffer buildConnectionReleaseWithRedirect  (uint16_t gsmArfcn)   const;
    ByteBuffer buildRrcReconfiguration    (RNTI rnti,
                                           const LTEDataBearer& drb) const;
    ByteBuffer buildSecurityModeCommand   (uint8_t cipherAlg,
                                           uint8_t integAlg) const;
    ByteBuffer buildMeasurementConfig     (const MeasObject& obj,
                                           const ReportConfig& rep) const;
    ByteBuffer buildHandoverCommand       (uint16_t targetPci,
                                           EARFCN targetEarfcn) const;
    ByteBuffer buildSystemInformation     (uint8_t sibType) const;

    void evaluateMeasEvent(const LTERrcMeasResult& mr, const ReportConfig& rep);

    HandoverCb hoCallback_;  ///< Called when HO is triggered by event evaluation
};

}  // namespace rbs::lte

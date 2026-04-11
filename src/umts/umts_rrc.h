#pragma once
#include "../common/types.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// UMTS RRC — Radio Resource Control (3GPP TS 25.331)
//
// The RRC layer manages the radio connection lifecycle and resource allocation.
// UE state machine (simplified):
//
//   IDLE ──RACH/RRC_CONN_REQ──► CELL_DCH ──RRC_CONN_REL──► IDLE
//             │                     │
//             │              RRC_CONN_RECONF
//             │                     │ (add/remove RBs, active set update)
//    CELL_PCH ◄── RRC_TO_PCH ────────┘
//
// For the NodeB (B-Node) side, RRC handles:
//   • RRC Connection Setup / Reconfiguration / Release
//   • Radio Bearer (RB) setup and teardown
//   • Active set management (soft handover)
//   • Measurement Control (periodic/event-triggered)
//   • Security Mode Command (integrity + ciphering keys from RNC)
//   • System Information (MIB, SIB1…SIB18) scheduling on BCH
// ─────────────────────────────────────────────────────────────────────────────

// ── UE RRC states (TS 25.331 §8.1.3) ─────────────────────────────────────────
enum class UMTSRrcState : uint8_t {
    IDLE,           ///< UE is not connected (camping on cell)
    CELL_DCH,       ///< Dedicated channel — full DL/UL radio bearer active
    CELL_FACH,      ///< Forward Access Channel — low-activity state
    CELL_PCH,       ///< Paging Channel — only paging, no user data
    URA_PCH,        ///< URA paging — UE registered at UTRAN Registration Area level
};

// ── Radio Bearer configuration ────────────────────────────────────────────────
struct RadioBearer {
    uint8_t  rbId;
    uint8_t  rlcMode;      ///< 0=TM, 1=UM, 2=AM
    uint16_t maxBitrate;   ///< kbps
    bool     ul;           ///< UL bearer present
    bool     dl;           ///< DL bearer present
};

// ── Active set member (for soft/softer handover) ───────────────────────────────
struct ActiveSetEntry {
    ScrCode primaryScrCode;
    ARFCN   uarfcn;
    int8_t  ecNo_dB;       ///< Measured Ec/No (dB) from pilot
    bool    primaryCell;
};

// ── Measurement event types (TS 25.331 §14.1.2) ───────────────────────────────
enum class RrcMeasEvent : uint8_t {
    EVENT_1A,   ///< Primary CPICH becomes better than threshold (add to active set)
    EVENT_1B,   ///< Primary CPICH becomes worse than threshold (remove from active set)
    EVENT_1C,   ///< Non-active set CPICH better than active set member
    EVENT_1D,   ///< Change of best cell in active set
    EVENT_6A,   ///< UL TX power above threshold (power headroom)
};

struct MeasurementReport {
    RNTI          rnti;
    RrcMeasEvent  event;
    ScrCode       triggeringScrCode;
    int8_t        cpichEcNo_dB;     ///< Measured Ec/No
    int16_t       cpichRscp_dBm;    ///< Measured RSCP
};

// ── Per-UE RRC context ────────────────────────────────────────────────────────
struct UMTSRrcContext {
    RNTI                        rnti;
    UMTSRrcState                state     = UMTSRrcState::IDLE;
    std::vector<RadioBearer>    bearers;
    std::vector<ActiveSetEntry> activeSet;
    uint8_t                     rlcConfig = 0;   ///< bitmap of configured RBs
    bool                        securityEnabled = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// IUMTSRrc — pure-virtual interface
// ─────────────────────────────────────────────────────────────────────────────
class IUMTSRrc {
public:
    virtual ~IUMTSRrc() = default;

    // ── Connection management ─────────────────────────────────────────────────
    /// Handle an RRC Connection Request from a UE (received on RACH/CCCH).
    /// @param rnti    RNTI assigned by MAC to this UE
    /// @returns true if the connection setup was accepted
    virtual bool handleConnectionRequest(RNTI rnti, IMSI imsi) = 0;

    /// Release the RRC connection (send RRC Connection Release to UE).
    virtual bool releaseConnection(RNTI rnti) = 0;

    // ── Radio bearer management ───────────────────────────────────────────────
    virtual bool setupRadioBearer  (RNTI rnti, const RadioBearer& rb) = 0;
    virtual bool releaseRadioBearer(RNTI rnti, uint8_t rbId) = 0;

    // ── Active set / handover ─────────────────────────────────────────────────
    virtual bool addToActiveSet    (RNTI rnti, const ActiveSetEntry& entry) = 0;
    virtual bool removeFromActiveSet(RNTI rnti, ScrCode scrCode) = 0;

    // ── Measurements ─────────────────────────────────────────────────────────
    virtual void processMeasurementReport(const MeasurementReport& report) = 0;

    // ── Security ─────────────────────────────────────────────────────────────
    /// Activate ciphering and integrity protection (keys from RNC via Iub).
    virtual bool activateSecurity(RNTI rnti,
                                  const uint8_t ck[16],
                                  const uint8_t ik[16]) = 0;

    // ── System Information ────────────────────────────────────────────────────
    virtual void scheduleSIB(uint8_t sibType) = 0;

    // ── State queries ─────────────────────────────────────────────────────────
    virtual UMTSRrcState rrcState(RNTI rnti) const = 0;
    virtual const std::vector<RadioBearer>&    bearers  (RNTI rnti) const = 0;
    virtual const std::vector<ActiveSetEntry>& activeSet(RNTI rnti) const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// UMTSRrc — concrete implementation
// ─────────────────────────────────────────────────────────────────────────────
class UMTSRrc : public IUMTSRrc {
public:
    bool handleConnectionRequest (RNTI rnti, IMSI imsi)              override;
    bool releaseConnection       (RNTI rnti)                         override;
    bool setupRadioBearer        (RNTI rnti, const RadioBearer& rb)  override;
    bool releaseRadioBearer      (RNTI rnti, uint8_t rbId)           override;
    bool addToActiveSet          (RNTI rnti,
                                  const ActiveSetEntry& entry)       override;
    bool removeFromActiveSet     (RNTI rnti, ScrCode scrCode)        override;
    void processMeasurementReport(const MeasurementReport& report)   override;
    bool activateSecurity        (RNTI rnti,
                                  const uint8_t ck[16],
                                  const uint8_t ik[16])              override;
    void scheduleSIB             (uint8_t sibType)                   override;
    UMTSRrcState rrcState        (RNTI rnti) const                   override;
    const std::vector<RadioBearer>&    bearers  (RNTI rnti) const   override;
    const std::vector<ActiveSetEntry>& activeSet(RNTI rnti) const   override;

private:
    std::unordered_map<RNTI, UMTSRrcContext> contexts_;

    ByteBuffer buildConnectionSetup    (RNTI rnti) const;
    ByteBuffer buildConnectionRelease  (uint8_t cause) const;
    ByteBuffer buildRbSetup            (const RadioBearer& rb) const;
    ByteBuffer buildSecurityModeCommand() const;
    ByteBuffer buildMeasurementControl (RrcMeasEvent event) const;
};

}  // namespace rbs::umts

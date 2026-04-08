#pragma once
#include "../common/types.h"
#include "gsm_rlc.h"
#include <functional>
#include <unordered_map>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// GSM Radio Resource (RR) sublayer — TS 44.018
//
// The RR layer sits above LAPDm and manages:
//   • Cell selection and reselection criteria (C1/C2)
//   • System Information broadcast scheduling (SI1 … SI13)
//   • RR connection establishment (RACH → AGCH → Immediate Assignment)
//   • Channel mode modification (TCH_F ↔ SDCCH)
//   • Measurement reports (SACCH) and handover decisions
//   • Cell Broadcast (SMSCB)
// ─────────────────────────────────────────────────────────────────────────────

// ── RR connection states (per-UE) ─────────────────────────────────────────────
enum class RRState : uint8_t {
    IDLE,                    ///< No RR connection
    CONNECTION_PENDING,      ///< Immediate Assignment sent, waiting for CM Serv Req
    DEDICATED,               ///< RR connection active on TCH or SDCCH
    HANDOVER_INITIATED,      ///< Handover Command sent
    RELEASE_PENDING,         ///< Channel Release sent
};

// ── Handover type ─────────────────────────────────────────────────────────────
enum class HandoverType : uint8_t {
    INTRA_CELL,   ///< Same BTS, different channel/slot
    INTRA_BSC,    ///< Different BTS under same BSC (via Abis)
    INTER_BSC,    ///< Different BSC (via A-interface, involves MSC)
};

// ── Measurement report (from UE over SACCH, TS 44.018 §10.5.2.20) ─────────────
struct MeasurementReport {
    RNTI    rnti;
    int8_t  rxlev_full;     ///< RxLev full (0–63, units: dBm + 110)
    int8_t  rxlev_sub;      ///< RxLev sub (averaged over sub-band)
    uint8_t rxqual_full;    ///< RxQual full (0–7, BER estimate)
    uint8_t rxqual_sub;
    struct NeighbourMeas {
        ARFCN   arfcn;
        int8_t  rxlev;
        uint8_t bsic;
    };
    std::array<NeighbourMeas, 6> neighbours;
    uint8_t numNeighbours = 0;
};

// ── Per-UE RR context ─────────────────────────────────────────────────────────
struct RRContext {
    RNTI        rnti;
    RRState     state          = RRState::IDLE;
    GSMChannelType channelType = GSMChannelType::SDCCH;
    uint8_t     timingAdvance  = 0;        ///< TA in bit periods (0–63)
    uint8_t     powerControl   = 0;        ///< MS TX power level (0 = max)
    MeasurementReport lastMeas = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// IGSMRr — pure-virtual interface (Radio Resource layer)
// ─────────────────────────────────────────────────────────────────────────────
class IGSMRr {
public:
    virtual ~IGSMRr() = default;

    // ── Cell broadcast ────────────────────────────────────────────────────────
    /// Trigger broadcast of the given System Information type.
    virtual void broadcastSI(uint8_t siType) = 0;

    // ── Connection procedures ─────────────────────────────────────────────────
    /// Process an incoming CHANNEL REQUEST (from RACH burst decoded by PHY).
    /// BTS responds with IMMEDIATE ASSIGNMENT on AGCH.
    virtual bool handleChannelRequest(uint8_t rachBurst, RNTI& assignedRnti) = 0;

    /// Send CHANNEL RELEASE to the UE and free the channel.
    virtual bool releaseChannel(RNTI rnti) = 0;

    // ── Measurements & power control ─────────────────────────────────────────
    /// Process a Measurement Report received on SACCH.
    virtual void processMeasurementReport(const MeasurementReport& mr) = 0;

    /// Retrieve the latest Measurement Report for a UE.
    virtual bool getMeasurementReport(RNTI rnti, MeasurementReport& mr) const = 0;

    // ── Handover ──────────────────────────────────────────────────────────────
    /// Initiate handover for the given UE to a target ARFCN/BSIC.
    virtual bool initiateHandover(RNTI rnti, ARFCN targetArfcn,
                                  uint8_t targetBsic, HandoverType type) = 0;

    // ── State ─────────────────────────────────────────────────────────────────
    virtual RRState rrState(RNTI rnti) const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GSMRr — concrete implementation
// ─────────────────────────────────────────────────────────────────────────────
class GSMRr : public IGSMRr {
public:
    void  broadcastSI        (uint8_t siType)                                   override;
    bool  handleChannelRequest(uint8_t rachBurst, RNTI& assignedRnti)           override;
    bool  releaseChannel     (RNTI rnti)                                        override;
    void  processMeasurementReport(const MeasurementReport& mr)                 override;
    bool  getMeasurementReport(RNTI rnti, MeasurementReport& mr) const         override;
    bool  initiateHandover   (RNTI rnti, ARFCN targetArfcn,
                               uint8_t targetBsic, HandoverType type)           override;
    RRState rrState          (RNTI rnti) const                                  override;

private:
    std::unordered_map<RNTI, RRContext> contexts_;
    RNTI nextRnti_ = 1;

    ByteBuffer buildImmediateAssignment(RNTI rnti, uint8_t timeSlot,
                                        GSMChannelType type) const;
    ByteBuffer buildChannelRelease     (uint8_t cause) const;
    ByteBuffer buildHandoverCommand    (ARFCN targetArfcn, uint8_t bsic,
                                        uint8_t timeSlot) const;
};

}  // namespace rbs::gsm

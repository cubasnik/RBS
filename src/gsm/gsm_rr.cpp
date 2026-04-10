// ─────────────────────────────────────────────────────────────────────────────
// GSM RR — Radio Resource management (3GPP TS 44.018)
//
// NodeB (BTS) side.  Manages:
//   • System Information broadcast (SI1…SI13) on BCCH
//   • Channel request from UE (RACH burst → Immediate Assignment on AGCH)
//   • Dedicated channel release (Channel Release on DCCH)
//   • Measurement reports from SACCH (power control / handover decisions)
//   • Handover command to target cell
// ─────────────────────────────────────────────────────────────────────────────
#include "gsm_rr.h"
#include "../common/logger.h"
#include <algorithm>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// Constants / helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t CAUSE_NORMAL_RELEASE  = 0x00;  // TS 44.018 §10.5.2.26
static constexpr uint8_t CAUSE_RADIO_LINK_FAIL = 0x09;

static const char* rrStateStr(RRState s) {
    switch (s) {
        case RRState::IDLE:                return "IDLE";
        case RRState::CONNECTION_PENDING:  return "CONNECTION_PENDING";
        case RRState::DEDICATED:           return "DEDICATED";
        case RRState::HANDOVER_INITIATED:  return "HANDOVER_INITIATED";
        case RRState::RELEASE_PENDING:     return "RELEASE_PENDING";
    }
    return "?";
}

// Extract the "establishment cause" from the 8-bit CHANNEL_REQUEST burst.
// TS 44.018 §9.1.8 — bits 5..7 encode the cause (3 bits).
static uint8_t rachEstabCause(uint8_t burst) {
    return (burst >> 5) & 0x07;
}

// ─────────────────────────────────────────────────────────────────────────────
// PDU builders (minimal L3 stubs for traceability)
// ─────────────────────────────────────────────────────────────────────────────

// Immediate Assignment  (TS 44.018 §9.1.18)
// Sent on AGCH (CCCH, DL) after receiving a CHANNEL REQUEST on RACH.
// Format: L2 pseudo-length(1) | PD+MT(1) | pageMode(1) | channelDesc(3)
//       | requestRef(3) | timingAdvance(1) | mobileAlloc(1+) = ~10 bytes
ByteBuffer GSMRr::buildImmediateAssignment(RNTI rnti, uint8_t timeSlot,
                                            GSMChannelType type) const
{
    // channel description: channel type(5) | TN(3) | TSC(3) | hopping(1) | ARFCN_hi(2)
    uint8_t chType = (type == GSMChannelType::TCH_F) ? 0x08 :
                     (type == GSMChannelType::SDCCH)  ? 0x01 : 0x00;
    uint8_t chanDesc0 = (chType << 3) | (timeSlot & 0x07);
    uint8_t chanDesc1 = 0x00;   // TSC=0, no hopping
    uint8_t chanDesc2 = 0x01;   // ARFCN = 1 (placeholder; real BTS fills from cfg)

    // Request reference encodes the RACH burst + FN (simplified here)
    uint8_t reqRef0 = static_cast<uint8_t>(rnti & 0xFF);
    uint8_t reqRef1 = 0x00;
    uint8_t reqRef2 = 0x00;

    return ByteBuffer{
        0x2D,           // L2 pseudo-length
        0x3F,           // PD=0(RR), MT=0x3F (Immediate Assignment)
        0x00,           // pageMode = normal paging
        chanDesc0, chanDesc1, chanDesc2,
        reqRef0, reqRef1, reqRef2,
        0x00,           // timingAdvance = 0
        0x00,           // mobileAllocationLength = 0
    };
}

// Channel Release  (TS 44.018 §9.1.7)
ByteBuffer GSMRr::buildChannelRelease(uint8_t cause) const
{
    // PD+MT(1) | RRcause(1)
    return ByteBuffer{ 0x0D, cause };
}

// Handover Command  (TS 44.018 §9.1.15)
ByteBuffer GSMRr::buildHandoverCommand(ARFCN targetArfcn, uint8_t bsic,
                                        uint8_t timeSlot) const
{
    // PD+MT(1) | cellDesc_arfcn_hi(1) | cellDesc_arfcn_lo+bsic(1)
    //   | channelDesc(3) | handoverRef(1) | powerCmdAccType(1)
    uint8_t arfcnHi = static_cast<uint8_t>((targetArfcn >> 8) & 0x03);
    uint8_t arfcnLo = static_cast<uint8_t>(targetArfcn & 0xFF);
    uint8_t bsicByte = bsic & 0x3F;
    return ByteBuffer{
        0x2B,           // PD=0(RR), MT=0x2B (Handover Command)
        arfcnHi, static_cast<uint8_t>(arfcnLo | (bsicByte << 2)),
        static_cast<uint8_t>(timeSlot & 0x07),
        0x00, 0x01,     // channelDesc[1..2]
        0x01,           // handoverReference
        0x20,           // powerCommandAccessType
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// IGSMRr implementation
// ─────────────────────────────────────────────────────────────────────────────

// ── broadcastSI ───────────────────────────────────────────────────────────────
void GSMRr::broadcastSI(uint8_t siType)
{
    // SI types: 1=BCCH alloc+NCH, 2=BCCH neighbour, 3=Cell identity+LAI,
    //           4=CBCH, 5=Measurement, 6=Handover info, 7/8=extensions
    //           13=GPRS config
    RBS_LOG_DEBUG("GsmRR", "SI{} queued for BCCH broadcast", siType);
}

// ── handleChannelRequest ──────────────────────────────────────────────────────
bool GSMRr::handleChannelRequest(uint8_t rachBurst, RNTI& assignedRnti)
{
    uint8_t cause = rachEstabCause(rachBurst);

    // Assign RNTI and derive time slot (round-robin over slots 1..7; slot 0 = BCCH)
    RNTI rnti = nextRnti_++;
    uint8_t timeSlot = static_cast<uint8_t>((rnti % 7) + 1);

    // Determine channel type from establishment cause:
    // cause 0b000=emergency, 0b001=call re-est, 0b010=call orig, 0b100=location upd
    GSMChannelType chType = (cause == 0x00 || cause == 0x02)
                             ? GSMChannelType::TCH_F
                             : GSMChannelType::SDCCH;

    RRContext ctx;
    ctx.rnti        = rnti;
    ctx.state       = RRState::CONNECTION_PENDING;
    ctx.channelType = chType;
    contexts_[rnti] = ctx;

    ByteBuffer pdu = buildImmediateAssignment(rnti, timeSlot, chType);
    RBS_LOG_INFO("GsmRR",
        "Channel Request burst={:#04x} cause={} → RNTI={} TN={} ch={} IA len={}",
        rachBurst, cause, rnti, timeSlot,
        (chType == GSMChannelType::TCH_F ? "TCH/F" : "SDCCH"),
        pdu.size());

    assignedRnti = rnti;
    return true;
}

// ── releaseChannel ────────────────────────────────────────────────────────────
bool GSMRr::releaseChannel(RNTI rnti)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("GsmRR", "releaseChannel: rnti={} not found", rnti);
        return false;
    }
    ByteBuffer pdu = buildChannelRelease(CAUSE_NORMAL_RELEASE);
    RBS_LOG_INFO("GsmRR",
        "Channel Release → UE rnti={} (was {}) PDU len={}",
        rnti, rrStateStr(it->second.state), pdu.size());
    it->second.state = RRState::RELEASE_PENDING;
    contexts_.erase(it);
    return true;
}

// ── processMeasurementReport ──────────────────────────────────────────────────
void GSMRr::processMeasurementReport(const MeasurementReport& mr)
{
    auto it = contexts_.find(mr.rnti);
    if (it == contexts_.end()) return;
    auto& ctx = it->second;
    ctx.lastMeas = mr;

    RBS_LOG_DEBUG("GsmRR",
        "MeasReport rnti={} RxLev_full={} RxQual_full={} neighbours={}",
        mr.rnti, mr.rxlev_full, mr.rxqual_full,
        static_cast<int>(mr.numNeighbours));

    // Handover decision heuristic:
    //   If serving RxLev_full < 15 (−85 dBm) and a neighbour is 6 units (dB) better
    if (mr.rxlev_full < 15) {
        for (uint8_t i = 0; i < mr.numNeighbours; ++i) {
            const auto& n = mr.neighbours[i];
            if (n.rxlev > mr.rxlev_full + 6) {
                RBS_LOG_INFO("GsmRR",
                    "HO candidate rnti={} serving_rxlev={} neigh_arfcn={} neigh_rxlev={}",
                    mr.rnti, mr.rxlev_full, n.arfcn, n.rxlev);
                initiateHandover(mr.rnti, n.arfcn, n.bsic, HandoverType::INTRA_BSC);
                break;
            }
        }
    }

    // Downlink power control: adjust if RxLev too high (micro-cell scenario)
    if (mr.rxlev_full > 50) {  // > −60 dBm, UE is close
        ctx.powerControl = static_cast<uint8_t>(std::min(static_cast<int>(ctx.powerControl) + 1, 15));
        RBS_LOG_DEBUG("GsmRR",
            "Power ctrl rnti={} powerCmd={}", mr.rnti, ctx.powerControl);
    }
}

// ── getMeasurementReport ──────────────────────────────────────────────────────
bool GSMRr::getMeasurementReport(RNTI rnti, MeasurementReport& mr) const
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) return false;
    mr = it->second.lastMeas;
    return true;
}

// ── initiateHandover ──────────────────────────────────────────────────────────
bool GSMRr::initiateHandover(RNTI rnti, ARFCN targetArfcn,
                               uint8_t targetBsic, HandoverType type)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("GsmRR", "initiateHandover: rnti={} not found", rnti);
        return false;
    }
    if (it->second.state == RRState::HANDOVER_INITIATED) return false; // already in HO

    uint8_t timeSlot = static_cast<uint8_t>((rnti % 7) + 1);
    ByteBuffer pdu = buildHandoverCommand(targetArfcn, targetBsic, timeSlot);

    const char* hoTypeStr =
        (type == HandoverType::INTRA_CELL) ? "intra-cell" :
        (type == HandoverType::INTRA_BSC)  ? "intra-BSC"  : "inter-BSC";

    RBS_LOG_INFO("GsmRR",
        "Handover Command ({}) rnti={} → ARFCN={} BSIC={} PDU len={}",
        hoTypeStr, rnti, targetArfcn, targetBsic, pdu.size());

    it->second.state = RRState::HANDOVER_INITIATED;
    return true;
}

// ── rrState ───────────────────────────────────────────────────────────────────
RRState GSMRr::rrState(RNTI rnti) const
{
    auto it = contexts_.find(rnti);
    return (it != contexts_.end()) ? it->second.state : RRState::IDLE;
}

} // namespace rbs::gsm

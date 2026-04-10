// ─────────────────────────────────────────────────────────────────────────────
// UMTS RRC — Radio Resource Control  (3GPP TS 25.331)
//
// NodeB-side RRC entity.  Manages per-UE state machines, radio bearers,
// active-set (soft handover), measurement control, and security mode.
//
// In the simulator there is no real Um/Iub air-interface; PDU byte buffers
// are built for traceability/correctness but are only logged, not sent over
// a transport.  The state machine and bearer tables are fully maintained.
// ─────────────────────────────────────────────────────────────────────────────
#include "umts_rrc.h"
#include "../common/logger.h"
#include <algorithm>
#include <cstring>

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static const char* stateStr(UMTSRrcState s) {
    switch (s) {
        case UMTSRrcState::IDLE:      return "IDLE";
        case UMTSRrcState::CELL_DCH:  return "CELL_DCH";
        case UMTSRrcState::CELL_FACH: return "CELL_FACH";
        case UMTSRrcState::CELL_PCH:  return "CELL_PCH";
        case UMTSRrcState::URA_PCH:   return "URA_PCH";
    }
    return "?";
}

static UMTSRrcContext& getOrCreate(std::unordered_map<RNTI, UMTSRrcContext>& map,
                                   RNTI rnti)
{
    auto it = map.find(rnti);
    if (it == map.end()) {
        UMTSRrcContext ctx;
        ctx.rnti = rnti;
        map[rnti] = ctx;
    }
    return map.at(rnti);
}

// ─────────────────────────────────────────────────────────────────────────────
// PDU builders
// (Minimal encoding — message type tag + key IEs, for logging / test)
// ─────────────────────────────────────────────────────────────────────────────

// RRC Connection Setup  (TS 25.331 §10.2.33)
// DL-CCCH message, IE: rrcTransactionIdentifier, ue-RadioAccessCapabilityInfo
ByteBuffer UMTSRrc::buildConnectionSetup(RNTI rnti) const
{
    // messageType[1] | txId[1] | rnti_high[1] | rnti_low[1]
    return ByteBuffer{
        0x28,                                           // DL-CCCH: rrcConnectionSetup
        0x00,                                           // rrcTransactionIdentifier = 0
        static_cast<uint8_t>(rnti >> 8),
        static_cast<uint8_t>(rnti & 0xFF),
    };
}

// RRC Connection Release  (TS 25.331 §10.2.35)
ByteBuffer UMTSRrc::buildConnectionRelease(uint8_t cause) const
{
    // messageType[1] | txId[1] | releaseCause[1]
    // releaseCause: 0=normal, 1=pre-emptiveRelease, 2=congestion, 3=re-establishment
    return ByteBuffer{ 0x2C, 0x00, cause };
}

// Radio Bearer Setup  (TS 25.331 §10.2.6)
ByteBuffer UMTSRrc::buildRbSetup(const RadioBearer& rb) const
{
    // messageType[1] | txId[1] | rbId[1] | rlcMode[1] | maxBr_high[1] | maxBr_low[1]
    return ByteBuffer{
        0x09,                                           // DL-DCCH: radioBearerSetup
        0x00,
        rb.rbId,
        rb.rlcMode,
        static_cast<uint8_t>(rb.maxBitrate >> 8),
        static_cast<uint8_t>(rb.maxBitrate & 0xFF),
    };
}

// Security Mode Command  (TS 25.331 §10.2.48)
ByteBuffer UMTSRrc::buildSecurityModeCommand() const
{
    // messageType[1] | txId[1] | cipheringAlgo[1] | integrityAlgo[1]
    // cipheringAlgo: 0=UEA0(no cipher), 1=UEA1(Kasumi), 2=UEA2(SNOW3G)
    // integrityAlgo: 0=UIA1(Kasumi), 1=UIA2(SNOW3G)
    return ByteBuffer{ 0x31, 0x00,
                       0x01,  // UEA1 Kasumi ciphering
                       0x00   // UIA1 Kasumi integrity
    };
}

// Measurement Control  (TS 25.331 §10.2.17)
ByteBuffer UMTSRrc::buildMeasurementControl(RrcMeasEvent event) const
{
    // messageType[1] | measId[1] | eventId[1]
    return ByteBuffer{
        0x16,                                             // DL-DCCH: measurementControl
        0x01,                                             // measId
        static_cast<uint8_t>(event),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// IUMTSRrc implementation
// ─────────────────────────────────────────────────────────────────────────────

// ── handleConnectionRequest ───────────────────────────────────────────────────
bool UMTSRrc::handleConnectionRequest(RNTI rnti, IMSI imsi)
{
    auto& ctx = getOrCreate(contexts_, rnti);
    if (ctx.state != UMTSRrcState::IDLE) {
        RBS_LOG_WARNING("RRC", "ConnReq rnti={} already in state {}", rnti, stateStr(ctx.state));
        return false;
    }

    ctx.rnti  = rnti;
    ctx.state = UMTSRrcState::CELL_DCH;

    ByteBuffer pdu = buildConnectionSetup(rnti);
    RBS_LOG_INFO("RRC",
        "RRC Connection Setup → UE rnti={} imsi={} PDU len={}",
        rnti, imsi, pdu.size());

    // Send RRC_MEASUREMENT_CONTROL to start intra-frequency measurement
    ByteBuffer mctrl = buildMeasurementControl(RrcMeasEvent::EVENT_1A);
    RBS_LOG_DEBUG("RRC", "MeasControl → UE rnti={} event=1A PDU len={}", rnti, mctrl.size());

    return true;
}

// ── releaseConnection ─────────────────────────────────────────────────────────
bool UMTSRrc::releaseConnection(RNTI rnti)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("RRC", "releaseConnection: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;
    ByteBuffer pdu = buildConnectionRelease(0 /*normal*/);
    RBS_LOG_INFO("RRC",
        "RRC Connection Release → UE rnti={} (was {}) PDU len={}",
        rnti, stateStr(ctx.state), pdu.size());

    ctx.state   = UMTSRrcState::IDLE;
    ctx.bearers.clear();
    ctx.activeSet.clear();
    ctx.securityEnabled = false;
    contexts_.erase(it);
    return true;
}

// ── setupRadioBearer ──────────────────────────────────────────────────────────
bool UMTSRrc::setupRadioBearer(RNTI rnti, const RadioBearer& rb)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_ERROR("RRC", "setupRadioBearer: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;

    // Replace existing bearer with same rbId, or append
    auto bIt = std::find_if(ctx.bearers.begin(), ctx.bearers.end(),
                             [&rb](const RadioBearer& b){ return b.rbId == rb.rbId; });
    if (bIt != ctx.bearers.end()) {
        *bIt = rb;
    } else {
        ctx.bearers.push_back(rb);
        ctx.rlcConfig |= static_cast<uint8_t>(1u << rb.rbId);
    }

    ByteBuffer pdu = buildRbSetup(rb);
    RBS_LOG_INFO("RRC",
        "Radio Bearer Setup rnti={} rbId={} rlcMode={} maxBr={}kbps PDU len={}",
        rnti, rb.rbId, rb.rlcMode, rb.maxBitrate, pdu.size());
    return true;
}

// ── releaseRadioBearer ────────────────────────────────────────────────────────
bool UMTSRrc::releaseRadioBearer(RNTI rnti, uint8_t rbId)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("RRC", "releaseRadioBearer: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;
    auto bIt = std::find_if(ctx.bearers.begin(), ctx.bearers.end(),
                             [rbId](const RadioBearer& b){ return b.rbId == rbId; });
    if (bIt == ctx.bearers.end()) {
        RBS_LOG_WARNING("RRC", "releaseRadioBearer: rnti={} rbId={} not found", rnti, rbId);
        return false;
    }
    ctx.bearers.erase(bIt);
    ctx.rlcConfig &= ~static_cast<uint8_t>(1u << rbId);
    RBS_LOG_INFO("RRC", "Radio Bearer Release rnti={} rbId={}", rnti, rbId);
    return true;
}

// ── addToActiveSet ────────────────────────────────────────────────────────────
bool UMTSRrc::addToActiveSet(RNTI rnti, const ActiveSetEntry& entry)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_ERROR("RRC", "addToActiveSet: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;

    // Max active set size per TS 25.331: 3 cells (FDD)
    if (ctx.activeSet.size() >= 3) {
        RBS_LOG_WARNING("RRC",
            "addToActiveSet: rnti={} active set full (3 cells), PSC={} not added",
            rnti, entry.primaryScrCode);
        return false;
    }

    // Don't add duplicates
    for (const auto& e : ctx.activeSet) {
        if (e.primaryScrCode == entry.primaryScrCode) return true;
    }

    ctx.activeSet.push_back(entry);
    RBS_LOG_INFO("RRC",
        "Active Set ADD rnti={} PSC={} Ec/No={}dB size={}",
        rnti, entry.primaryScrCode, entry.ecNo_dB,
        ctx.activeSet.size());

    // Send MeasurementControl to UE for the new cell
    ByteBuffer mc = buildMeasurementControl(RrcMeasEvent::EVENT_1A);
    RBS_LOG_DEBUG("RRC", "MeasControl → UE rnti={} (active set add) len={}", rnti, mc.size());
    return true;
}

// ── removeFromActiveSet ───────────────────────────────────────────────────────
bool UMTSRrc::removeFromActiveSet(RNTI rnti, ScrCode scrCode)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("RRC", "removeFromActiveSet: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;
    auto eIt = std::find_if(ctx.activeSet.begin(), ctx.activeSet.end(),
                             [scrCode](const ActiveSetEntry& e){
                                 return e.primaryScrCode == scrCode;
                             });
    if (eIt == ctx.activeSet.end()) {
        RBS_LOG_WARNING("RRC", "removeFromActiveSet: rnti={} PSC={} not in set", rnti, scrCode);
        return false;
    }
    ctx.activeSet.erase(eIt);
    RBS_LOG_INFO("RRC",
        "Active Set REMOVE rnti={} PSC={} remaining={}",
        rnti, scrCode, ctx.activeSet.size());
    return true;
}

// ── processMeasurementReport ──────────────────────────────────────────────────
void UMTSRrc::processMeasurementReport(const MeasurementReport& rep)
{
    RBS_LOG_INFO("RRC",
        "MeasReport rnti={} event={} PSC={} Ec/No={}dB RSCP={}dBm",
        rep.rnti,
        static_cast<int>(rep.event),
        rep.triggeringScrCode,
        rep.cpichEcNo_dB,
        rep.cpichRscp_dBm);

    auto it = contexts_.find(rep.rnti);
    if (it == contexts_.end()) return;
    auto& ctx = it->second;

    switch (rep.event) {
        case RrcMeasEvent::EVENT_1A: {
            // New cell better than threshold — add to active set
            ActiveSetEntry entry;
            entry.primaryScrCode = rep.triggeringScrCode;
            entry.uarfcn         = 0;   // kept as zero in simulator
            entry.ecNo_dB        = rep.cpichEcNo_dB;
            entry.primaryCell    = ctx.activeSet.empty();
            addToActiveSet(rep.rnti, entry);
            break;
        }
        case RrcMeasEvent::EVENT_1B:
            // Cell dropped below threshold — remove from active set
            removeFromActiveSet(rep.rnti, rep.triggeringScrCode);
            break;
        case RrcMeasEvent::EVENT_1C: {
            // Non-active cell better than weakest active set member — replace
            if (!ctx.activeSet.empty()) {
                // Find weakest member
                auto weakest = std::min_element(
                    ctx.activeSet.begin(), ctx.activeSet.end(),
                    [](const ActiveSetEntry& a, const ActiveSetEntry& b){
                        return a.ecNo_dB < b.ecNo_dB;
                    });
                ScrCode old = weakest->primaryScrCode;
                removeFromActiveSet(rep.rnti, old);
                ActiveSetEntry entry;
                entry.primaryScrCode = rep.triggeringScrCode;
                entry.uarfcn         = 0;
                entry.ecNo_dB        = rep.cpichEcNo_dB;
                entry.primaryCell    = false;
                addToActiveSet(rep.rnti, entry);
            }
            break;
        }
        case RrcMeasEvent::EVENT_6A:
            RBS_LOG_WARNING("RRC",
                "UL power headroom low rnti={} — consider handover or rate reduction",
                rep.rnti);
            break;
        default:
            break;
    }
}

// ── activateSecurity ──────────────────────────────────────────────────────────
bool UMTSRrc::activateSecurity(RNTI rnti, const uint8_t ck[16], const uint8_t ik[16])
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_ERROR("RRC", "activateSecurity: rnti={} not found", rnti);
        return false;
    }
    it->second.securityEnabled = true;

    ByteBuffer pdu = buildSecurityModeCommand();
    // Log first 4 bytes of CK/IK as fingerprint (not the full key)
    RBS_LOG_INFO("RRC",
        "Security Mode Command → UE rnti={} CK[0..3]={:02X}{:02X}{:02X}{:02X} "
        "IK[0..3]={:02X}{:02X}{:02X}{:02X} PDU len={}",
        rnti,
        ck[0], ck[1], ck[2], ck[3],
        ik[0], ik[1], ik[2], ik[3],
        pdu.size());
    return true;
}

// ── scheduleSIB ───────────────────────────────────────────────────────────────
void UMTSRrc::scheduleSIB(uint8_t sibType)
{
    // SIB scheduling on BCH.  In the simulator we log the request;
    // a real implementation would queue the SIB for the BCH formatter.
    //   SIB1: NAS system information (PLMN, LAC, ...)
    //   SIB3: Cell (re)selection parameters
    //   SIB5: FDD individual cell information
    //   SIB7: Fast changing parameters (UL interference, allowed UL TX power)
    RBS_LOG_DEBUG("RRC", "SIB{} scheduled for BCH broadcast", sibType);
}

// ── State queries ─────────────────────────────────────────────────────────────

UMTSRrcState UMTSRrc::rrcState(RNTI rnti) const
{
    auto it = contexts_.find(rnti);
    return (it != contexts_.end()) ? it->second.state : UMTSRrcState::IDLE;
}

const std::vector<RadioBearer>& UMTSRrc::bearers(RNTI rnti) const
{
    static const std::vector<RadioBearer> empty;
    auto it = contexts_.find(rnti);
    return (it != contexts_.end()) ? it->second.bearers : empty;
}

} // namespace rbs::umts

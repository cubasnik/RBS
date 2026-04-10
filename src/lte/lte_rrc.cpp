// ─────────────────────────────────────────────────────────────────────────────
// LTE RRC — Radio Resource Control  (3GPP TS 36.331)
//
// eNodeB-side RRC entity.  Manages UE state machines (RRC_IDLE /
// RRC_CONNECTED), SRB/DRB setup, security mode, measurement configuration,
// handover preparation, and SIB scheduling.
//
// PDU byte buffers carry minimal stub encoding (message-type tag + key IEs)
// for traceability; in the simulator they are only logged, not transported.
// ─────────────────────────────────────────────────────────────────────────────
#include "lte_rrc.h"
#include "../common/logger.h"
#include <algorithm>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static const char* cipherAlgStr(uint8_t alg) {
    switch (alg) {
        case 0: return "NULL";
        case 1: return "AES(EEA1)";
        case 2: return "SNOW3G(EEA2)";
        case 3: return "ZUC(EEA3)";
    }
    return "?";
}

static const char* integAlgStr(uint8_t alg) {
    switch (alg) {
        case 0: return "NULL";
        case 1: return "AES(EIA1)";
        case 2: return "SNOW3G(EIA2)";
        case 3: return "ZUC(EIA3)";
    }
    return "?";
}

static LTERrcContext& getOrCreate(std::unordered_map<RNTI, LTERrcContext>& map,
                                   RNTI rnti)
{
    if (map.find(rnti) == map.end()) {
        LTERrcContext ctx;
        ctx.rnti = rnti;
        map[rnti] = ctx;
    }
    return map.at(rnti);
}

// ─────────────────────────────────────────────────────────────────────────────
// PDU builders
// (Stub DCCH/CCCH encoding — message-type byte + key IEs, for logging/test)
// ─────────────────────────────────────────────────────────────────────────────

// RRC Connection Setup  (TS 36.331 §6.2.2, DL-CCCH)
// MessageType: c1 → rrcConnectionSetup (0x00 in DL-CCCH)
ByteBuffer LTERrc::buildConnectionSetup(RNTI rnti) const
{
    // messageType[1] | rrcTransactionIdentifier[1] | rnti_hi[1] | rnti_lo[1]
    // srbToAddModList: SRB1 (id=1, default cfg)
    return ByteBuffer{
        0x40,                                           // DL-CCCH: rrcConnectionSetup
        0x00,                                           // rrcTransactionIdentifier
        static_cast<uint8_t>(rnti >> 8),
        static_cast<uint8_t>(rnti & 0xFF),
        0x01,                                           // SRB1 present
    };
}

// RRC Connection Release  (TS 36.331 §6.2.2, DL-DCCH)
ByteBuffer LTERrc::buildConnectionRelease(uint8_t cause) const
{
    // messageType[1] | txId[1] | releaseCause[1]
    // releaseCause: 0=other, 1=loadBalancingTAUrequired, 2=other, 3=cs-FallbackHighPriority
    return ByteBuffer{ 0x48, 0x00, cause };
}

// RRC Connection Reconfiguration  (TS 36.331 §6.2.2, DL-DCCH)
// Used both for DRB setup and MeasConfig delivery
ByteBuffer LTERrc::buildRrcReconfiguration(RNTI rnti,
                                            const LTEDataBearer& drb) const
{
    // messageType[1] | txId[1] | drbId[1] | qci[1] | maxBrDl_hi[1] | maxBrDl_lo[1]
    (void)rnti;
    return ByteBuffer{
        0x04,                                             // DL-DCCH: rrcConnectionReconfiguration
        0x00,
        drb.drbId,
        drb.qci,
        static_cast<uint8_t>(drb.maxBitrateDl >> 8),
        static_cast<uint8_t>(drb.maxBitrateDl & 0xFF),
    };
}

// Security Mode Command  (TS 36.331 §6.2.2, DL-DCCH)
ByteBuffer LTERrc::buildSecurityModeCommand(uint8_t cipherAlg,
                                             uint8_t integAlg) const
{
    // messageType[1] | txId[1] | cipheringAlg[1] | integrityProtAlg[1]
    return ByteBuffer{ 0x60, 0x00, cipherAlg, integAlg };
}

// MeasurementConfig (embedded in RRCConnectionReconfiguration)
ByteBuffer LTERrc::buildMeasurementConfig(const MeasObject& obj,
                                           const ReportConfig& rep) const
{
    // messageType[1] | txId[1] | measObjectId[1] | earfcn_hi[1] | earfcn_lo[1]
    //   | reportConfigId[1] | triggerQty[1] | a3Offset[1]
    return ByteBuffer{
        0x04,                                                 // rrcConnectionReconfiguration
        0x00,
        obj.measObjectId,
        static_cast<uint8_t>(obj.earfcn >> 8),
        static_cast<uint8_t>(obj.earfcn & 0xFF),
        rep.reportConfigId,
        static_cast<uint8_t>(rep.triggerQty),
        static_cast<uint8_t>(rep.a3Offset_dB),
    };
}

// Handover Command (RRCConnectionReconfiguration with mobilityControlInfo)
ByteBuffer LTERrc::buildHandoverCommand(uint16_t targetPci,
                                         EARFCN targetEarfcn) const
{
    // messageType[1] | txId[1] | targetPci_hi[1] | targetPci_lo[1]
    //   | earfcn_hi[1] | earfcn_lo[1]
    return ByteBuffer{
        0x04,                                                 // rrcConnectionReconfiguration
        0x00,
        static_cast<uint8_t>(targetPci >> 8),
        static_cast<uint8_t>(targetPci & 0xFF),
        static_cast<uint8_t>(targetEarfcn >> 8),
        static_cast<uint8_t>(targetEarfcn & 0xFF),
    };
}

// SystemInformation (TS 36.331 §6.2.1, BCCH)
ByteBuffer LTERrc::buildSystemInformation(uint8_t sibType) const
{
    // messageType[1] | sibType[1]
    return ByteBuffer{ 0x00, sibType };
}

// ─────────────────────────────────────────────────────────────────────────────
// ILTERrc implementation
// ─────────────────────────────────────────────────────────────────────────────

// ── handleConnectionRequest ───────────────────────────────────────────────────
bool LTERrc::handleConnectionRequest(RNTI rnti, IMSI imsi)
{
    auto& ctx = getOrCreate(contexts_, rnti);
    if (ctx.state != LTERrcState::RRC_IDLE) {
        RBS_LOG_WARNING("LteRRC", "ConnReq rnti={} already RRC_CONNECTED", rnti);
        return false;
    }

    ctx.rnti    = rnti;
    ctx.state   = LTERrcState::RRC_CONNECTED;
    ctx.srbMask = 0x03;   // SRB0 + SRB1 active

    ByteBuffer pdu = buildConnectionSetup(rnti);
    RBS_LOG_INFO("LteRRC",
        "RRC Connection Setup → UE rnti={} imsi={} SRB1 active PDU len={}",
        rnti, imsi, pdu.size());

    // Queue default intra-frequency MeasConfig (A3 event)
    MeasObject obj0{ 1, 0 /*earfcn filled later by caller*/, LTEBand::B1 };
    ReportConfig rep0{ 1, RrcTriggerQty::RSRP, 3, 160 };
    ByteBuffer mc = buildMeasurementConfig(obj0, rep0);
    RBS_LOG_DEBUG("LteRRC",
        "MeasConfig (A3, RSRP) → UE rnti={} PDU len={}", rnti, mc.size());

    return true;
}

// ── releaseConnection ─────────────────────────────────────────────────────────
bool LTERrc::releaseConnection(RNTI rnti)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("LteRRC", "releaseConnection: rnti={} not found", rnti);
        return false;
    }

    ByteBuffer pdu = buildConnectionRelease(0 /*other*/);
    RBS_LOG_INFO("LteRRC",
        "RRC Connection Release → UE rnti={} PDU len={}", rnti, pdu.size());

    it->second.state     = LTERrcState::RRC_IDLE;
    it->second.drbs.clear();
    it->second.srbMask   = 0;
    it->second.secActive = false;
    contexts_.erase(it);
    return true;
}

// ── setupDRB ──────────────────────────────────────────────────────────────────
bool LTERrc::setupDRB(RNTI rnti, const LTEDataBearer& drb)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_ERROR("LteRRC", "setupDRB: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;

    // Replace existing DRB with same id, or append
    auto dIt = std::find_if(ctx.drbs.begin(), ctx.drbs.end(),
                             [&drb](const LTEDataBearer& d){ return d.drbId == drb.drbId; });
    if (dIt != ctx.drbs.end()) {
        *dIt = drb;
    } else {
        ctx.drbs.push_back(drb);
    }

    // Also activate SRB2 when first DRB is configured (TS 36.331 §5.3.3.4)
    if (!(ctx.srbMask & 0x04)) {
        ctx.srbMask |= 0x04;
        RBS_LOG_DEBUG("LteRRC", "SRB2 activated for rnti={}", rnti);
    }

    ByteBuffer pdu = buildRrcReconfiguration(rnti, drb);
    RBS_LOG_INFO("LteRRC",
        "RRC Reconfiguration (DRB setup) → UE rnti={} drbId={} QCI={} "
        "maxDL={}bps PDU len={}",
        rnti, drb.drbId, drb.qci, drb.maxBitrateDl, pdu.size());
    return true;
}

// ── releaseDRB ────────────────────────────────────────────────────────────────
bool LTERrc::releaseDRB(RNTI rnti, uint8_t drbId)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("LteRRC", "releaseDRB: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;
    auto dIt = std::find_if(ctx.drbs.begin(), ctx.drbs.end(),
                             [drbId](const LTEDataBearer& d){ return d.drbId == drbId; });
    if (dIt == ctx.drbs.end()) {
        RBS_LOG_WARNING("LteRRC", "releaseDRB: rnti={} drbId={} not found", rnti, drbId);
        return false;
    }
    ctx.drbs.erase(dIt);
    RBS_LOG_INFO("LteRRC", "DRB released rnti={} drbId={} remaining={}", rnti, drbId, ctx.drbs.size());
    return true;
}

// ── activateSecurity ──────────────────────────────────────────────────────────
bool LTERrc::activateSecurity(RNTI rnti,
                               uint8_t cipherAlg, uint8_t integAlg,
                               const uint8_t kRrcEnc[16],
                               const uint8_t kRrcInt[16])
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_ERROR("LteRRC", "activateSecurity: rnti={} not found", rnti);
        return false;
    }
    auto& ctx = it->second;
    ctx.secActive  = true;
    ctx.cipherAlg  = cipherAlg;
    ctx.integAlg   = integAlg;

    ByteBuffer pdu = buildSecurityModeCommand(cipherAlg, integAlg);
    RBS_LOG_INFO("LteRRC",
        "Security Mode Command → UE rnti={} cipher={} integ={} "
        "kRrcEnc[0..3]={:02X}{:02X}{:02X}{:02X} kRrcInt[0..3]={:02X}{:02X}{:02X}{:02X} PDU len={}",
        rnti,
        cipherAlgStr(cipherAlg), integAlgStr(integAlg),
        kRrcEnc[0], kRrcEnc[1], kRrcEnc[2], kRrcEnc[3],
        kRrcInt[0], kRrcInt[1], kRrcInt[2], kRrcInt[3],
        pdu.size());
    return true;
}

// ── sendMeasurementConfig ─────────────────────────────────────────────────────
void LTERrc::sendMeasurementConfig(RNTI rnti,
                                    const MeasObject& obj,
                                    const ReportConfig& rep)
{
    ByteBuffer pdu = buildMeasurementConfig(obj, rep);
    RBS_LOG_INFO("LteRRC",
        "MeasConfig → UE rnti={} measObjId={} EARFCN={} repCfgId={} "
        "triggerQty={} A3offset={}dB ttt={}ms PDU len={}",
        rnti, obj.measObjectId, obj.earfcn, rep.reportConfigId,
        (rep.triggerQty == RrcTriggerQty::RSRP ? "RSRP" : "RSRQ"),
        rep.a3Offset_dB, rep.timeToTrigger_ms,
        pdu.size());
}

// ── processMeasurementReport ──────────────────────────────────────────────────
void LTERrc::processMeasurementReport(const LTERrcMeasResult& mr)
{
    RBS_LOG_INFO("LteRRC",
        "MeasReport rnti={} measId={} servRSRP={} servRSRQ={} neighbours={}",
        mr.rnti, mr.measId, mr.rsrp_q, mr.rsrq_q,
        mr.neighbours.size());

    // A3: if any neighbour's RSRP > serving + margin → trigger HO preparation
    for (const auto& n : mr.neighbours) {
        // RSRP quantised: margin of 6 units corresponds to ~6 dB
        if (n.rsrp_q > mr.rsrp_q + 6) {
            RBS_LOG_INFO("LteRRC",
                "A3 event rnti={}: neighbour PCI={} EARFCN={} RSRP={} > serving RSRP={} → HO candidate",
                mr.rnti, n.pci, n.earfcn, n.rsrp_q, mr.rsrp_q);
            prepareHandover(mr.rnti, n.pci, n.earfcn);
            break;   // one HO at a time
        }
    }
}

// ── prepareHandover ───────────────────────────────────────────────────────────
bool LTERrc::prepareHandover(RNTI rnti, uint16_t targetPci, EARFCN targetEarfcn)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("LteRRC", "prepareHandover: rnti={} not found", rnti);
        return false;
    }

    ByteBuffer pdu = buildHandoverCommand(targetPci, targetEarfcn);
    // In a real eNB: forward HO Request to target via X2AP (TS 36.423 §8.3.1)
    // or via MME/S1AP (TS 36.413 §8.4.1) for inter-X2 scenarios.
    RBS_LOG_INFO("LteRRC",
        "Handover Command → UE rnti={} targetPCI={} targetEARFCN={} PDU len={}",
        rnti, targetPci, targetEarfcn, pdu.size());
    return true;
}

// ── scheduleSIB ───────────────────────────────────────────────────────────────
void LTERrc::scheduleSIB(uint8_t sibType)
{
    // SIB1 period = 80 ms; SIB2..N carried in SIs on BCCH/DL-SCH
    // Simulator logs the schedule; real implementation would queue for BCH formatter.
    ByteBuffer pdu = buildSystemInformation(sibType);
    RBS_LOG_DEBUG("LteRRC",
        "SIB{} scheduled for BCCH broadcast PDU len={}", sibType, pdu.size());
}

// ── State queries ─────────────────────────────────────────────────────────────

LTERrcState LTERrc::rrcState(RNTI rnti) const
{
    auto it = contexts_.find(rnti);
    return (it != contexts_.end()) ? it->second.state : LTERrcState::RRC_IDLE;
}

const std::vector<LTEDataBearer>& LTERrc::drbs(RNTI rnti) const
{
    static const std::vector<LTEDataBearer> empty;
    auto it = contexts_.find(rnti);
    return (it != contexts_.end()) ? it->second.drbs : empty;
}

} // namespace rbs::lte

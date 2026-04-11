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

// TS 36.331 §6.2.2 / §5.3.8.3 — RRC Connection Release with redirectedCarrierInfo (GERAN)
ByteBuffer LTERrc::buildConnectionReleaseWithRedirect(uint16_t gsmArfcn) const
{
    // messageType[1] | txId[1] | cause[1] | redirectType[1] | arfcn_hi[1] | arfcn_lo[1]
    // cause 0x03 = cs-fallback-triggered
    // redirectType 0x01 = geran-ARFCN
    return ByteBuffer{
        0x48,                                              // DL-DCCH: rrcConnectionRelease
        0x00,                                              // transaction ID
        0x03,                                              // cause: cs-fallback-triggered
        0x01,                                              // redirectedCarrierInfo: geran
        static_cast<uint8_t>(gsmArfcn >> 8),
        static_cast<uint8_t>(gsmArfcn & 0xFF),
    };
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
    ReportConfig rep0{};
    rep0.reportConfigId = 1;
    rep0.eventType      = MeasEventType::A3;
    rep0.triggerQty     = RrcTriggerQty::RSRP;
    rep0.a3Offset_dB    = 3;
    rep0.timeToTrigger_ms = 160;
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

// ── releaseWithRedirect (CSFB) ──────────────────────────────────────────────
bool LTERrc::releaseWithRedirect(RNTI rnti, uint16_t gsmArfcn)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("LteRRC", "releaseWithRedirect: rnti={} not found", rnti);
        return false;
    }
    ByteBuffer pdu = buildConnectionReleaseWithRedirect(gsmArfcn);
    RBS_LOG_INFO("LteRRC",
        "RRC Connection Release (CSFB) → UE rnti={} GSM-ARFCN={} PDU len={}",
        rnti, gsmArfcn, pdu.size());
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
    // Store or update the measurement config in the UE context (TS 36.331 §5.5.2)
    auto it = contexts_.find(rnti);
    if (it != contexts_.end()) {
        auto& cfgs = it->second.measConfigs;
        auto cit = std::find_if(cfgs.begin(), cfgs.end(),
            [&rep](const LTEMeasConfig& mc){ return mc.rep.reportConfigId == rep.reportConfigId; });
        if (cit != cfgs.end())
            *cit = LTEMeasConfig{obj, rep};
        else
            cfgs.push_back(LTEMeasConfig{obj, rep});
    }
    ByteBuffer pdu = buildMeasurementConfig(obj, rep);
    const char* evStr = []( MeasEventType e ) -> const char* {
        switch (e) {
            case MeasEventType::A1: return "A1"; case MeasEventType::A2: return "A2";
            case MeasEventType::A3: return "A3"; case MeasEventType::A5: return "A5";
        } return "?";
    }(rep.eventType);
    RBS_LOG_INFO("LteRRC",
        "MeasConfig ", evStr, " → UE rnti=", rnti,
        " measObjId=", static_cast<int>(obj.measObjectId),
        " EARFCN=", obj.earfcn,
        " repCfgId=", static_cast<int>(rep.reportConfigId),
        " ttt=", rep.timeToTrigger_ms, "ms PDU len=", pdu.size());
}

// ── processMeasurementReport ──────────────────────────────────────────────────
// TS 36.331 §5.5.5 — evaluate event conditions and trigger HO if needed
void LTERrc::processMeasurementReport(const LTERrcMeasResult& mr)
{
    RBS_LOG_INFO("LteRRC",
        "MeasReport rnti=", mr.rnti,
        " measId=", static_cast<int>(mr.measId),
        " servRSRP=", mr.rsrp_q,
        " servRSRQ=", mr.rsrq_q,
        " neighbours=", mr.neighbours.size());

    auto it = contexts_.find(mr.rnti);
    if (it == contexts_.end()) return;  // unknown UE — ignore

    for (const auto& mc : it->second.measConfigs) {
        if (mc.rep.reportConfigId != mr.measId) continue;
        evaluateMeasEvent(mr, mc.rep);
        break;
    }
}

// ── evaluateMeasEvent ─────────────────────────────────────────────────────
void LTERrc::evaluateMeasEvent(const LTERrcMeasResult& mr, const ReportConfig& rep)
{
    switch (rep.eventType) {
        case MeasEventType::A1:
            // A1: serving RSRP exceeds threshold1 — coverage OK (TS 36.331 §5.5.4.2)
            if (mr.rsrp_q > rep.threshold1_q) {
                RBS_LOG_INFO("LteRRC",
                    "A1 event rnti=", mr.rnti,
                    " servRSRP=", mr.rsrp_q, " > thr1=", rep.threshold1_q,
                    " — coverage OK");
            }
            break;

        case MeasEventType::A2:
            // A2: serving RSRP falls below threshold1 — poor coverage (TS 36.331 §5.5.4.3)
            if (mr.rsrp_q < rep.threshold1_q && !mr.neighbours.empty()) {
                auto best = std::max_element(mr.neighbours.begin(), mr.neighbours.end(),
                    [](const auto& a, const auto& b){ return a.rsrp_q < b.rsrp_q; });
                RBS_LOG_INFO("LteRRC",
                    "A2 event rnti=", mr.rnti,
                    " servRSRP=", mr.rsrp_q, " < thr1=", rep.threshold1_q,
                    " → HO to PCI=", best->pci);
                prepareHandover(mr.rnti, best->pci, best->earfcn);
            }
            break;

        case MeasEventType::A3:
            // A3: neighbour offset better than serving (TS 36.331 §5.5.4.4)
            for (const auto& n : mr.neighbours) {
                if (n.rsrp_q >= mr.rsrp_q + rep.a3Offset_dB) {
                    RBS_LOG_INFO("LteRRC",
                        "A3 event rnti=", mr.rnti,
                        " neighRSRP=", n.rsrp_q,
                        " ≥ servRSRP=", mr.rsrp_q,
                        " + offset=", static_cast<int>(rep.a3Offset_dB),
                        " → HO to PCI=", n.pci);
                    prepareHandover(mr.rnti, n.pci, n.earfcn);
                    break;
                }
            }
            break;

        case MeasEventType::A5:
            // A5: serving < thr1 AND neighbour > thr2 (TS 36.331 §5.5.4.6)
            if (mr.rsrp_q < rep.threshold1_q) {
                for (const auto& n : mr.neighbours) {
                    if (n.rsrp_q > rep.threshold2_q) {
                        RBS_LOG_INFO("LteRRC",
                            "A5 event rnti=", mr.rnti,
                            " servRSRP=", mr.rsrp_q, " < thr1=", rep.threshold1_q,
                            " neighRSRP=", n.rsrp_q, " > thr2=", rep.threshold2_q,
                            " → HO to PCI=", n.pci);
                        prepareHandover(mr.rnti, n.pci, n.earfcn);
                        break;
                    }
                }
            }
            break;
    }
}

// ── prepareHandover ───────────────────────────────────────────────────────────
bool LTERrc::prepareHandover(RNTI rnti, uint16_t targetPci, EARFCN targetEarfcn)
{
    auto it = contexts_.find(rnti);
    if (it == contexts_.end()) {
        RBS_LOG_WARNING("LteRRC", "prepareHandover: rnti=", rnti, " not found");
        return false;
    }

    ByteBuffer pdu = buildHandoverCommand(targetPci, targetEarfcn);
    RBS_LOG_INFO("LteRRC",
        "Handover Command → UE rnti=", rnti,
        " targetPCI=", targetPci,
        " targetEARFCN=", targetEarfcn,
        " PDU len=", pdu.size());

    // Invoke registered handover callback (X2AP or S1AP path)
    if (hoCallback_)
        hoCallback_(rnti, targetPci, targetEarfcn);

    return true;
}

// ── setHandoverCallback ─────────────────────────────────────────────────────────
void LTERrc::setHandoverCallback(HandoverCb cb)
{
    hoCallback_ = std::move(cb);
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

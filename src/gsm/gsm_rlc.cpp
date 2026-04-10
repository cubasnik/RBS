// ─────────────────────────────────────────────────────────────────────────────
// GSM RLC — LAPDm data link layer  (3GPP TS 44.006)
//
// LAPDm provides reliable L2 on the radio interface:
//   UI frames    — unacknowledged (BCCH/PCH/AGCH broadcasts)
//   I  frames    — sequenced, acknowledged (SDCCH, SACCH)
//   S  frames    — supervisory: RR (ack), REJ (retransmit), RNR (flow stop)
//   U  frames    — SABM (connect), UA (accept), DISC (release), DM (refuse)
//
// Parameters (TS 44.006 §5.8.1):
//   k    = 1  (window size — one unacknowledged I-frame in flight)
//   N200 = 3  (max retransmissions before link failure)
//   T200    depends on channel type; simulated not timed here
// ─────────────────────────────────────────────────────────────────────────────
#include "gsm_rlc.h"
#include "../common/logger.h"

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// LAPDm frame encoding constants
// ─────────────────────────────────────────────────────────────────────────────

// Address octet (TS 44.006 §3.2):
//   bit 8: EA=1 (single-octet address)
//   bit 7: C/R (command/response, 1=command from BTS)
//   bits 5..6: SAPI
//   bits 3..4: "000" (LPD)
//   bits 1..2: spare
static uint8_t makeAddr(SAPI sapi, bool command) {
    uint8_t s = static_cast<uint8_t>(sapi);
    return static_cast<uint8_t>(0x01                     // EA=1
                                | (command ? 0x02 : 0x00) // C/R
                                | (s << 2));               // SAPI
}

// U-frame control octets (TS 44.006 §3.4, format: M3M2M1 P/F M4M5 0 1)
//   SABM: 001 P 1111 → 0b_001P_1111 = 0x2F | (P<<4)
//   UA  : 011 F 0011 → 0b_011F_0011 = 0x63 | (F<<4)
//   DM  : 000 F 1111 → 0b_000F_1111 = 0x0F | (F<<4)
//   DISC: 010 P 0011 → 0b_010P_0011 = 0x43 | (P<<4)
//   UI  : 000 P 0011 → 0b_000P_0011 = 0x03 | (P<<4)
static constexpr uint8_t CTL_SABM_BASE = 0x2F;
static constexpr uint8_t CTL_UA_BASE   = 0x63;
static constexpr uint8_t CTL_DM_BASE   = 0x0F;
static constexpr uint8_t CTL_DISC_BASE = 0x43;
static constexpr uint8_t CTL_UI_BASE   = 0x03;

// I-frame control: N(S)(3) | P/F(1) | N(R)(3) | 0
static uint8_t iFrameCtl(uint8_t ns, uint8_t nr, bool pf) {
    return static_cast<uint8_t>((ns & 0x07) << 5
                               | (pf ? 0x10 : 0x00)
                               | ((nr & 0x07) << 1)
                               | 0x00);
}

// S-frame control: 00 | type(2) | P/F(1) | N(R)(3) | 1
// RR=00, REJ=10
static uint8_t sFrameCtlRR(uint8_t nr, bool pf) {
    return static_cast<uint8_t>(0x01 | ((nr & 0x07) << 1) | (pf ? 0x10 : 0x00));
}

// LI octet: length of info field in bits 7..2; bit 1 = M (more); bit 0 = EL
static uint8_t makeLI(uint8_t len, bool more = false) {
    return static_cast<uint8_t>((len << 2) | (more ? 0x02 : 0x00) | 0x01);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame builders
// ─────────────────────────────────────────────────────────────────────────────

LAPDmFrame GSMRlc::buildUA(uint8_t address) const
{
    LAPDmFrame f;
    f.address = address;
    f.control = CTL_UA_BASE;    // F=0
    f.length  = makeLI(0);
    return f;
}

LAPDmFrame GSMRlc::buildDM(uint8_t address) const
{
    LAPDmFrame f;
    f.address = address;
    f.control = CTL_DM_BASE;    // F=0
    f.length  = makeLI(0);
    return f;
}

LAPDmFrame GSMRlc::buildRR(uint8_t address, uint8_t nr) const
{
    LAPDmFrame f;
    f.address = address;
    f.control = sFrameCtlRR(nr, false);
    f.length  = makeLI(0);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity management
// ─────────────────────────────────────────────────────────────────────────────

LAPDmEntity& GSMRlc::getOrCreate(RNTI rnti, SAPI sapi)
{
    EntityKey key = makeKey(rnti, sapi);
    if (!entities_.count(key)) {
        LAPDmEntity e;
        e.rnti  = rnti;
        e.sapi  = sapi;
        entities_[key] = e;
    }
    return entities_.at(key);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame processors
// ─────────────────────────────────────────────────────────────────────────────

void GSMRlc::processUnnumb(LAPDmEntity& e, const LAPDmFrame& f)
{
    uint8_t ftype = f.control & 0xEF;  // mask out P/F bit

    if (ftype == (CTL_SABM_BASE & 0xEF)) {
        // UE requests link establishment
        if (e.state == LAPDmState::IDLE ||
            e.state == LAPDmState::AWAITING_EST) {
            e.state = LAPDmState::MULTIPLE_FRAME_EST;
            e.vs = e.vr = e.va = 0;
            RBS_LOG_DEBUG("GsmRLC",
                "SABM rnti={} SAPI={} → UA sent, MULTIPLE_FRAME_EST",
                e.rnti, static_cast<int>(e.sapi));
            // UA response would be queued for DL; log it
            (void)buildUA(f.address);
        }
        return;
    }

    if (ftype == (CTL_DISC_BASE & 0xEF)) {
        // UE releases link
        bool wasEst = (e.state == LAPDmState::MULTIPLE_FRAME_EST);
        e.state = LAPDmState::IDLE;
        e.vs = e.vr = e.va = 0;
        RBS_LOG_DEBUG("GsmRLC",
            "DISC rnti={} SAPI={} → {}", e.rnti,
            static_cast<int>(e.sapi),
            wasEst ? "UA sent" : "DM sent");
        return;
    }

    if (ftype == (CTL_UA_BASE & 0xEF)) {
        // Response to our SABM
        if (e.state == LAPDmState::AWAITING_EST) {
            e.state = LAPDmState::MULTIPLE_FRAME_EST;
            RBS_LOG_DEBUG("GsmRLC",
                "UA rnti={} SAPI={} → MULTIPLE_FRAME_EST",
                e.rnti, static_cast<int>(e.sapi));
        }
        return;
    }

    if (ftype == (CTL_UI_BASE & 0xEF)) {
        // UI: unacknowledged data → deliver to upper layer
        if (!f.info.empty()) {
            e.rxSduQueue.push(f.info);
        }
        return;
    }
}

void GSMRlc::processIFrame(LAPDmEntity& e, const LAPDmFrame& f)
{
    if (e.state != LAPDmState::MULTIPLE_FRAME_EST) return;

    uint8_t ns = (f.control >> 5) & 0x07;
    uint8_t nr = (f.control >> 1) & 0x07;

    // Accept in-sequence frames
    if (ns == e.vr) {
        if (!f.info.empty()) {
            e.rxSduQueue.push(f.info);
        }
        e.vr = (e.vr + 1) & 0x07;
        e.va = nr & 0x07;   // acknowledge sender's frames up to N(R)
        // Send RR
        RBS_LOG_DEBUG("GsmRLC",
            "I-frame rnti={} SAPI={} N(S)={} → RR N(R)={}",
            e.rnti, static_cast<int>(e.sapi), ns, e.vr);
    } else {
        // Out-of-order: send REJ
        RBS_LOG_WARNING("GsmRLC",
            "I-frame rnti={} SAPI={} N(S)={} expected {} → REJ",
            e.rnti, static_cast<int>(e.sapi), ns, e.vr);
    }
}

void GSMRlc::processSuperv(LAPDmEntity& e, const LAPDmFrame& f)
{
    if (e.state != LAPDmState::MULTIPLE_FRAME_EST &&
        e.state != LAPDmState::TIMER_RECOVERY) return;

    uint8_t stype = (f.control >> 3) & 0x03;  // 00=RR, 01=RNR, 10=REJ
    uint8_t nr    = (f.control >> 1) & 0x07;
    e.va = nr;

    if (stype == 0x00) {
        // RR: peer acknowledges up to N(R)
        if (e.state == LAPDmState::TIMER_RECOVERY)
            e.state = LAPDmState::MULTIPLE_FRAME_EST;
    } else if (stype == 0x02) {
        // REJ: retransmit from V(A)
        RBS_LOG_WARNING("GsmRLC",
            "REJ rnti={} SAPI={} N(R)={} → retransmit from {}",
            e.rnti, static_cast<int>(e.sapi), nr, e.va);
        e.retx = 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IGSMRlc implementation
// ─────────────────────────────────────────────────────────────────────────────

bool GSMRlc::requestLink(RNTI rnti, SAPI sapi)
{
    auto& e = getOrCreate(rnti, sapi);
    if (e.state == LAPDmState::MULTIPLE_FRAME_EST) return true;
    e.state = LAPDmState::AWAITING_EST;
    e.vs = e.vr = e.va = e.retx = 0;
    // SABM would be transmitted on DCCH; log it
    uint8_t addr = makeAddr(sapi, true);
    (void)addr;
    RBS_LOG_DEBUG("GsmRLC",
        "requestLink rnti={} SAPI={} → SABM sent",
        rnti, static_cast<int>(sapi));
    return true;
}

bool GSMRlc::releaseLink(RNTI rnti, SAPI sapi)
{
    EntityKey key = makeKey(rnti, sapi);
    auto it = entities_.find(key);
    if (it == entities_.end()) return false;
    auto& e = it->second;
    if (e.state == LAPDmState::IDLE) return true;
    e.state = LAPDmState::AWAITING_REL;
    RBS_LOG_DEBUG("GsmRLC",
        "releaseLink rnti={} SAPI={} → DISC sent", rnti, static_cast<int>(sapi));
    return true;
}

bool GSMRlc::sendSdu(RNTI rnti, SAPI sapi, ByteBuffer sdu)
{
    auto& e = getOrCreate(rnti, sapi);

    if (e.state == LAPDmState::MULTIPLE_FRAME_EST) {
        // Send as I-frame; for simplicity treat one SDU = one I-frame
        LAPDmFrame f;
        f.address = makeAddr(sapi, true /*command*/);
        f.control = iFrameCtl(e.vs, e.vr, false);
        f.length  = makeLI(static_cast<uint8_t>(sdu.size()));
        f.info    = std::move(sdu);
        e.vs = (e.vs + 1) & 0x07;
        e.unackedTx.push(f);
        RBS_LOG_DEBUG("GsmRLC",
            "sendSdu rnti={} SAPI={} I-frame N(S)={} len={}",
            rnti, static_cast<int>(sapi), (e.vs - 1) & 0x07, f.info.size());
    } else {
        // Not in multi-frame mode: send as UI (e.g. paging, broadcast)
        LAPDmFrame f;
        f.address = makeAddr(sapi, true);
        f.control = CTL_UI_BASE;
        f.length  = makeLI(static_cast<uint8_t>(sdu.size()));
        f.info    = std::move(sdu);
        RBS_LOG_DEBUG("GsmRLC",
            "sendSdu rnti={} SAPI={} UI len={}", rnti, static_cast<int>(sapi), f.info.size());
    }
    return true;
}

bool GSMRlc::receiveSdu(RNTI rnti, SAPI sapi, ByteBuffer& sdu)
{
    EntityKey key = makeKey(rnti, sapi);
    auto it = entities_.find(key);
    if (it == entities_.end()) return false;
    auto& e = it->second;
    if (e.rxSduQueue.empty()) return false;
    sdu = std::move(e.rxSduQueue.front());
    e.rxSduQueue.pop();
    return true;
}

void GSMRlc::tick(const LAPDmFrame& rxFrame, RNTI rnti)
{
    // Determine frame category from control byte
    // U-frames: bit 0 = 1 AND bit 2 = 1
    // I-frames: bit 0 = 0
    // S-frames: bit 0 = 1 AND bit 2 = 0
    uint8_t ctl = rxFrame.control;
    SAPI sapi   = static_cast<SAPI>((rxFrame.address >> 2) & 0x03);
    auto& e     = getOrCreate(rnti, sapi);

    if ((ctl & 0x01) == 0) {
        processIFrame(e, rxFrame);
    } else if ((ctl & 0x05) == 0x01) {
        processSuperv(e, rxFrame);
    } else {
        processUnnumb(e, rxFrame);
    }
}

LAPDmState GSMRlc::linkState(RNTI rnti, SAPI sapi) const
{
    auto it = entities_.find(makeKey(rnti, sapi));
    return (it != entities_.end()) ? it->second.state : LAPDmState::IDLE;
}

} // namespace rbs::gsm

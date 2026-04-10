// ─────────────────────────────────────────────────────────────────────────────
// LTE RLC — Radio Link Control  (3GPP TS 36.322)
//
// Implements TM / UM / AM modes per radio bearer entity.
//
// TM: pass-through, no header (BCCH, CCCH, PMCH)
// UM: 1- or 2-byte SN header; reordering, no retransmit
// AM: 2-byte header; windowed ARQ, STATUS PDU (ACK/NACK), segmentation
//
// AM PDU header (10-bit SN):
//   Byte 0: D/C(1) | RF(1) | P(1) | FI(2) | E(1) | SN[9:8](2)
//   Byte 1: SN[7:0]
//
// UM PDU header (10-bit SN, sn10Bit=true):
//   Byte 0: FI(2) | E(1) | SN[9:8](2) | pad(3)
//   Byte 1: SN[7:0]
//
// STATUS PDU:
//   Byte 0: D/C=0 | CPT=000 | ACK_SN[9:5]
//   Byte 1: ACK_SN[4:0] | E1(1) | pad(2)
//   (optional NACK_SN pairs follow if E1=1)
// ─────────────────────────────────────────────────────────────────────────────
#include "lte_rlc.h"
#include "../common/logger.h"
#include <algorithm>
#include <cstring>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint16_t AM_SN_MODULUS = 1024;   // 2^10
static constexpr uint16_t UM_SN_MASK_10 = 0x3FF;
static constexpr uint16_t AM_SN_MASK    = 0x3FF;

static inline bool snLt(uint16_t a, uint16_t b, uint16_t mod = AM_SN_MODULUS) {
    return ((b - a) & (mod - 1)) < (mod / 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// PDU header builders
// ─────────────────────────────────────────────────────────────────────────────

ByteBuffer LTERlc::addUMHeader(const ByteBuffer& payload, uint16_t sn) const
{
    ByteBuffer out;
    out.reserve(2 + payload.size());
    // FI=0b00 (first+last), E=0, SN[9:8]
    out.push_back(static_cast<uint8_t>((sn >> 8) & 0x03));
    out.push_back(static_cast<uint8_t>(sn & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

ByteBuffer LTERlc::addAMHeader(const ByteBuffer& payload, uint16_t sn,
                                bool poll, LTEAMPduType type) const
{
    ByteBuffer out;
    out.reserve(2 + payload.size());
    uint8_t dc  = (type == LTEAMPduType::DATA) ? 0x80 : 0x00;
    uint8_t p   = poll ? 0x20 : 0x00;
    uint8_t fi  = 0x00;   // complete SDU in this PDU
    uint8_t snh = static_cast<uint8_t>((sn >> 8) & 0x03);
    out.push_back(dc | p | fi | snh);
    out.push_back(static_cast<uint8_t>(sn & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

ByteBuffer LTERlc::buildStatusPDU(const LTERlcEntity& e) const
{
    ByteBuffer out;
    uint16_t ack = e.rxExpSN & AM_SN_MASK;
    // D/C=0, CPT=000, ACK_SN[9:5]
    out.push_back(static_cast<uint8_t>((ack >> 5) & 0x1F));
    // ACK_SN[4:0], E1=0 (no NACKs in this minimal implementation)
    out.push_back(static_cast<uint8_t>((ack & 0x1F) << 3));
    return out;
}

bool LTERlc::parseStatusPDU(const ByteBuffer& pdu, LTEStatusPDU& status) const
{
    if (pdu.size() < 2) return false;
    uint16_t ack = (static_cast<uint16_t>(pdu[0] & 0x1F) << 5)
                 |  static_cast<uint16_t>((pdu[1] >> 3) & 0x1F);
    status.ack_sn = ack;
    status.nacks.clear();
    // Parse NACK_SN fields if E1 bit set
    bool e1 = (pdu[1] & 0x04) != 0;
    size_t off = 2;
    while (e1 && off + 2 <= pdu.size()) {
        NackEntry ne;
        ne.nack_sn = (static_cast<uint16_t>(pdu[off] & 0x1F) << 5)
                   |  static_cast<uint16_t>((pdu[off+1] >> 3) & 0x1F);
        ne.hasSegmentOffset = false;
        ne.soStart = ne.soEnd = 0;
        e1 = (pdu[off+1] & 0x04) != 0;
        status.nacks.push_back(ne);
        off += 2;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Segmentation
// ─────────────────────────────────────────────────────────────────────────────

ByteBuffer LTERlc::segmentAM(LTERlcEntity& e, uint16_t maxBytes)
{
    // Prefer retransmissions first
    if (!e.retxQueue.empty()) {
        ByteBuffer pdu = std::move(e.retxQueue.front());
        e.retxQueue.pop();
        return pdu;
    }
    if (e.txSduQueue.empty()) return {};

    const uint16_t headerSz = 2;
    const uint16_t payloadMaxBytes = (maxBytes > headerSz) ? maxBytes - headerSz : 0;
    if (payloadMaxBytes == 0) return {};

    ByteBuffer sdu = std::move(e.txSduQueue.front());
    e.txSduQueue.pop();

    // If SDU fits in one PDU
    if (sdu.size() <= payloadMaxBytes) {
        uint16_t sn = e.txSN;
        e.txSN = (e.txSN + 1) & AM_SN_MASK;
        bool poll = e.txSduQueue.empty();   // set poll on last PDU
        return addAMHeader(sdu, sn, poll, LTEAMPduType::DATA);
    }

    // Segment: first payloadMaxBytes bytes become this PDU, rest back in queue
    ByteBuffer firstSeg(sdu.begin(), sdu.begin() + payloadMaxBytes);
    ByteBuffer remainder(sdu.begin() + payloadMaxBytes, sdu.end());

    // Put remainder back at front — use a fresh queue
    std::queue<ByteBuffer> newQ;
    newQ.push(std::move(remainder));
    while (!e.txSduQueue.empty()) {
        newQ.push(std::move(e.txSduQueue.front()));
        e.txSduQueue.pop();
    }
    e.txSduQueue = std::move(newQ);

    uint16_t sn = e.txSN;
    e.txSN = (e.txSN + 1) & AM_SN_MASK;
    return addAMHeader(firstSeg, sn, false, LTEAMPduType::DATA);
}

// ─────────────────────────────────────────────────────────────────────────────
// UL PDU processors
// ─────────────────────────────────────────────────────────────────────────────

void LTERlc::processTM(LTERlcEntity& e, const ByteBuffer& pdu)
{
    // TM: no header — full PDU is the SDU
    e.rxSduQueue.push(pdu);
}

void LTERlc::processUM(LTERlcEntity& e, const ByteBuffer& pdu)
{
    if (pdu.size() < 2) return;
    uint16_t sn = (static_cast<uint16_t>(pdu[0] & 0x03) << 8)
                |  static_cast<uint16_t>(pdu[1]);
    sn &= UM_SN_MASK_10;

    // Duplicate / reorder check (simplified: accept in-order only)
    if (sn == (e.rxExpSN & UM_SN_MASK_10)) {
        ByteBuffer sdu(pdu.begin() + 2, pdu.end());
        e.rxSduQueue.push(std::move(sdu));
        e.rxExpSN = (e.rxExpSN + 1) & UM_SN_MASK_10;
    }
    // Out-of-order silently dropped in this simulator (no reorder window)
}

void LTERlc::processAM(LTERlcEntity& e, const ByteBuffer& pdu)
{
    if (pdu.empty()) return;
    bool isControl = (pdu[0] & 0x80) == 0;

    if (isControl) {
        // STATUS PDU
        LTEStatusPDU status;
        if (!parseStatusPDU(pdu, status)) return;
        // Advance VT(A) to ACK_SN
        e.vtA = status.ack_sn & AM_SN_MASK;
        // For each NACK, push copy of original PDU into retxQueue
        // (simplified: we don't keep TX window, so just log)
        for (const auto& nack : status.nacks) {
            RBS_LOG_WARNING("LteRLC",
                "AM NACK rnti={} rbId={} nack_sn={}",
                e.rnti, e.rbId, nack.nack_sn);
        }
        RBS_LOG_DEBUG("LteRLC",
            "STATUS PDU rnti={} rbId={} ack_sn={} nacks={}",
            e.rnti, e.rbId, status.ack_sn, status.nacks.size());
        return;
    }

    // Data PDU
    if (pdu.size() < 2) return;
    uint16_t sn = (static_cast<uint16_t>(pdu[0] & 0x03) << 8)
                |  static_cast<uint16_t>(pdu[1]);
    sn &= AM_SN_MASK;

    // Accept in-order SNs (simplified: no full reorder window)
    if (sn == e.rxExpSN) {
        ByteBuffer sdu(pdu.begin() + 2, pdu.end());
        e.rxSduQueue.push(std::move(sdu));
        e.rxExpSN = (e.rxExpSN + 1) & AM_SN_MASK;
    }

    // Poll bit set → send STATUS PDU immediately
    bool pollSet = (pdu[0] & 0x20) != 0;
    if (pollSet) {
        ByteBuffer status = buildStatusPDU(e);
        // In the real stack this goes to MAC scheduler; log it here
        RBS_LOG_DEBUG("LteRLC",
            "AM poll received rnti={} rbId={} sn={} → STATUS len={}",
            e.rnti, e.rbId, sn, status.size());
        e.retxQueue.push(std::move(status));  // enqueue STATUS for DL path
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ILTERlc implementation
// ─────────────────────────────────────────────────────────────────────────────

bool LTERlc::addRB(RNTI rnti, uint8_t rbId, LTERlcMode mode)
{
    EntityKey key = makeKey(rnti, rbId);
    if (entities_.count(key)) return true;   // already exists
    LTERlcEntity e;
    e.rnti  = rnti;
    e.rbId  = rbId;
    e.mode  = mode;
    entities_[key] = std::move(e);
    RBS_LOG_DEBUG("LteRLC",
        "addRB rnti={} rbId={} mode={}",
        rnti, rbId, static_cast<int>(mode));
    return true;
}

bool LTERlc::removeRB(RNTI rnti, uint8_t rbId)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    entities_.erase(it);
    RBS_LOG_DEBUG("LteRLC", "removeRB rnti={} rbId={}", rnti, rbId);
    return true;
}

bool LTERlc::sendSdu(RNTI rnti, uint8_t rbId, ByteBuffer sdu)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    it->second.txSduQueue.push(std::move(sdu));
    return true;
}

bool LTERlc::pollPdu(RNTI rnti, uint8_t rbId,
                      ByteBuffer& pdu, uint16_t maxBytes)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    auto& e = it->second;

    switch (e.mode) {
        case LTERlcMode::TM: {
            if (e.txSduQueue.empty()) return false;
            pdu = std::move(e.txSduQueue.front());
            e.txSduQueue.pop();
            if (pdu.size() > maxBytes) pdu.resize(maxBytes);
            return true;
        }
        case LTERlcMode::UM: {
            if (e.txSduQueue.empty()) return false;
            ByteBuffer sdu = std::move(e.txSduQueue.front());
            e.txSduQueue.pop();
            uint16_t sn = e.txSN;
            e.txSN = (e.txSN + 1) & UM_SN_MASK_10;
            pdu = addUMHeader(sdu, sn);
            if (pdu.size() > maxBytes) pdu.resize(maxBytes);
            return true;
        }
        case LTERlcMode::AM: {
            pdu = segmentAM(e, maxBytes);
            return !pdu.empty();
        }
    }
    return false;
}

void LTERlc::deliverPdu(RNTI rnti, uint8_t rbId, const ByteBuffer& pdu)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return;
    auto& e = it->second;

    switch (e.mode) {
        case LTERlcMode::TM: processTM(e, pdu); break;
        case LTERlcMode::UM: processUM(e, pdu); break;
        case LTERlcMode::AM: processAM(e, pdu); break;
    }
}

bool LTERlc::receiveSdu(RNTI rnti, uint8_t rbId, ByteBuffer& sdu)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    auto& e = it->second;
    if (e.rxSduQueue.empty()) return false;
    sdu = std::move(e.rxSduQueue.front());
    e.rxSduQueue.pop();
    return true;
}

uint16_t LTERlc::txSN(RNTI rnti, uint8_t rbId) const
{
    auto it = entities_.find(makeKey(rnti, rbId));
    return (it != entities_.end()) ? it->second.txSN : 0;
}

uint16_t LTERlc::rxSN(RNTI rnti, uint8_t rbId) const
{
    auto it = entities_.find(makeKey(rnti, rbId));
    return (it != entities_.end()) ? it->second.rxExpSN : 0;
}

} // namespace rbs::lte

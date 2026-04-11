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
                                bool poll, LTEAMPduType type, uint8_t fi) const
{
    ByteBuffer out;
    out.reserve(2 + payload.size());
    uint8_t dc  = (type == LTEAMPduType::DATA) ? 0x80 : 0x00;
    uint8_t p   = poll ? 0x20 : 0x00;
    uint8_t fiB = static_cast<uint8_t>((fi & 0x03) << 3);  // FI → bits [4:3]
    uint8_t snh = static_cast<uint8_t>((sn >> 8) & 0x03);
    out.push_back(dc | p | fiB | snh);
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
    // Prefer retransmissions / pending STATUS PDUs first
    if (!e.retxQueue.empty()) {
        ByteBuffer pdu = std::move(e.retxQueue.front());
        e.retxQueue.pop();
        return pdu;
    }
    if (e.txSduQueue.empty()) return {};

    const uint16_t headerSz    = 2;
    const uint16_t payloadMax  = (maxBytes > headerSz) ? maxBytes - headerSz : 0;
    if (payloadMax == 0) return {};

    ByteBuffer& front = e.txSduQueue.front();  // modify in-place, no pop yet

    if (front.size() <= payloadMax) {
        // Entire front item fits → complete or last segment
        ByteBuffer sdu = std::move(front);
        e.txSduQueue.pop();
        // FI: 10 = last segment (txMidSdu was true), 00 = complete SDU
        uint8_t fi = e.txMidSdu ? 0x02 : 0x00;
        e.txMidSdu = false;
        bool poll = e.txSduQueue.empty() && e.retxQueue.empty();
        uint16_t sn = e.txSN;
        e.txSN = (e.txSN + 1) & AM_SN_MASK;
        ByteBuffer pdu = addAMHeader(sdu, sn, poll, LTEAMPduType::DATA, fi);
        e.txWindow[sn] = pdu;  // store for potential NACK retransmit
        return pdu;
    }

    // Need to split: take first payloadMax bytes, leave remainder in queue
    ByteBuffer seg(front.begin(), front.begin() + payloadMax);
    front.erase(front.begin(), front.begin() + payloadMax);  // remainder stays at front

    // FI: 01 = first segment, 11 = middle segment
    uint8_t fi = e.txMidSdu ? 0x03 : 0x01;
    e.txMidSdu = true;
    uint16_t sn = e.txSN;
    e.txSN = (e.txSN + 1) & AM_SN_MASK;
    ByteBuffer pdu = addAMHeader(seg, sn, false, LTEAMPduType::DATA, fi);
    e.txWindow[sn] = pdu;
    return pdu;
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
        uint16_t ack = status.ack_sn & AM_SN_MASK;

        // Re-enqueue NACKed PDUs BEFORE pruning the TX window
        for (const auto& nack : status.nacks) {
            uint16_t nsn = nack.nack_sn & AM_SN_MASK;
            auto txIt = e.txWindow.find(nsn);
            if (txIt != e.txWindow.end()) {
                e.retxQueue.push(txIt->second);  // copy PDU into retx queue
                RBS_LOG_WARNING("LteRLC",
                    "AM NACK rnti={} rbId={} nack_sn={} → retx queued",
                    e.rnti, e.rbId, nsn);
            }
        }
        // Advance VT(A) and prune ACK'd PDUs from TX window
        e.vtA = ack;
        for (auto it = e.txWindow.begin(); it != e.txWindow.end(); ) {
            if (snLt(it->first, ack))
                it = e.txWindow.erase(it);
            else
                ++it;
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

    // FI bits [4:3] of byte 0 (TS 36.322 §6.2.3.3)
    uint8_t fi = (pdu[0] >> 3) & 0x03;

    // Accept in-order SNs; out-of-order dropped (no reorder window in sim)
    if (sn == e.rxExpSN) {
        e.rxExpSN = (e.rxExpSN + 1) & AM_SN_MASK;
        ByteBuffer payload(pdu.begin() + 2, pdu.end());

        if (fi == 0x00) {
            // Complete SDU — deliver directly
            if (e.rxMidSdu) { e.rxPartialSdu.clear(); e.rxMidSdu = false; }
            e.rxSduQueue.push(std::move(payload));
        } else if (fi == 0x01) {
            // First segment — start accumulator
            e.rxPartialSdu = std::move(payload);
            e.rxMidSdu     = true;
        } else if (fi == 0x03) {
            // Middle segment — append
            if (e.rxMidSdu)
                e.rxPartialSdu.insert(e.rxPartialSdu.end(),
                                      payload.begin(), payload.end());
        } else {  // fi == 0x02
            // Last segment — append and deliver complete SDU
            if (e.rxMidSdu) {
                e.rxPartialSdu.insert(e.rxPartialSdu.end(),
                                      payload.begin(), payload.end());
                e.rxSduQueue.push(std::move(e.rxPartialSdu));
                e.rxPartialSdu.clear();
                e.rxMidSdu = false;
            }
        }
    }

    // Poll bit set → enqueue STATUS PDU for the peer
    bool pollSet = (pdu[0] & 0x20) != 0;
    if (pollSet) {
        ByteBuffer statusPdu = buildStatusPDU(e);
        RBS_LOG_DEBUG("LteRLC",
            "AM poll rnti={} rbId={} sn={} → STATUS len={}",
            e.rnti, e.rbId, sn, statusPdu.size());
        e.retxQueue.push(std::move(statusPdu));
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

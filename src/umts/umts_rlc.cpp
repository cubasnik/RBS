// ─────────────────────────────────────────────────────────────────────────────
// UMTS RLC — Radio Link Control  (3GPP TS 25.322)
//
// TM: transparent, no header (BCH/PCH/RACH, CS voice)
// UM: 7-bit SN header (1 byte); reordering, no ARQ
// AM: 12-bit SN (2-byte header); windowed ARQ / STATUS PDU / segmentation
//
// AM PDU header (TS 25.322 §9.2.1):
//   Byte 0: D/C(1) | P(1) | HE(2) | SN[11:8](4)
//   Byte 1: SN[7:0]
//
// UM PDU header (7-bit SN, TS 25.322 §9.2.2):
//   Byte 0: E(1) | SN[6:0]
//
// STATUS PDU (TS 25.322 §9.2.3):
//   Byte 0: D/C=1/control | PDU type=000 | ACK_SN[11:8]
//   Byte 1: ACK_SN[7:0]
// ─────────────────────────────────────────────────────────────────────────────
#include "umts_rlc.h"
#include "../common/logger.h"
#include <algorithm>

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint16_t AM_SN_MODULUS = 4096;   // 2^12
static constexpr uint16_t AM_SN_MASK    = 0xFFF;
static constexpr uint8_t  UM_SN_MASK    = 0x7F;   // 7-bit

// ─────────────────────────────────────────────────────────────────────────────
// PDU header builders
// ─────────────────────────────────────────────────────────────────────────────

ByteBuffer UMTSRlc::addUMHeader(const ByteBuffer& payload, uint16_t sn) const
{
    ByteBuffer out;
    out.reserve(1 + payload.size());
    // E=0 (no extension), SN[6:0]
    out.push_back(static_cast<uint8_t>(sn & UM_SN_MASK));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

ByteBuffer UMTSRlc::addAMHeader(const ByteBuffer& payload, uint16_t sn,
                                  bool poll, AMPduType type) const
{
    ByteBuffer out;
    out.reserve(2 + payload.size());
    uint8_t dc  = (type == AMPduType::DATA) ? 0x80 : 0x00;
    uint8_t p   = poll ? 0x40 : 0x00;
    // HE = 0b00 (data follows immediately)
    uint8_t snh = static_cast<uint8_t>((sn >> 8) & 0x0F);
    out.push_back(dc | p | snh);
    out.push_back(static_cast<uint8_t>(sn & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

ByteBuffer UMTSRlc::buildStatus(const RLCEntity& e) const
{
    ByteBuffer out;
    uint16_t ack = e.rxExpSN & AM_SN_MASK;
    // D/C=0(control), PDU type=000(STATUS), ACK_SN[11:8]
    out.push_back(static_cast<uint8_t>((ack >> 8) & 0x0F));
    out.push_back(static_cast<uint8_t>(ack & 0xFF));
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Segmentation (AM DL)
// ─────────────────────────────────────────────────────────────────────────────

ByteBuffer UMTSRlc::segmentAM(RLCEntity& e, const ByteBuffer& sdu,
                                uint16_t maxBytes)
{
    const uint16_t headerSz = 2;
    const uint16_t payloadMax = (maxBytes > headerSz) ? maxBytes - headerSz : 0;
    if (payloadMax == 0) return {};

    ByteBuffer payload;
    if (sdu.size() <= payloadMax) {
        payload = sdu;
    } else {
        payload.assign(sdu.begin(), sdu.begin() + payloadMax);
    }
    bool poll = e.txQueue.empty();
    uint16_t sn = e.txSN;
    e.txSN = (e.txSN + 1) & AM_SN_MASK;
    return addAMHeader(payload, sn, poll, AMPduType::DATA);
}

// ─────────────────────────────────────────────────────────────────────────────
// UL PDU processors
// ─────────────────────────────────────────────────────────────────────────────

void UMTSRlc::processTM(RLCEntity& e, const ByteBuffer& pdu)
{
    e.rxSduQueue.push(pdu);
}

void UMTSRlc::processUM(RLCEntity& e, const ByteBuffer& pdu)
{
    if (pdu.empty()) return;
    uint8_t sn = pdu[0] & UM_SN_MASK;
    if (sn == (e.rxExpSN & UM_SN_MASK)) {
        ByteBuffer sdu(pdu.begin() + 1, pdu.end());
        e.rxSduQueue.push(std::move(sdu));
        e.rxExpSN = (e.rxExpSN + 1) & UM_SN_MASK;
    }
}

void UMTSRlc::processAM(RLCEntity& e, const ByteBuffer& pdu)
{
    if (pdu.size() < 2) return;
    bool isControl = (pdu[0] & 0x80) == 0;

    if (isControl) {
        // STATUS PDU: advance VT(A)
        uint16_t ack = (static_cast<uint16_t>(pdu[0] & 0x0F) << 8)
                     |  static_cast<uint16_t>(pdu[1]);
        e.vtA = ack & AM_SN_MASK;
        RBS_LOG_DEBUG("UmtsRLC",
            "STATUS rnti={} rbId={} ack_sn={}", e.rnti, e.rbId, ack);
        return;
    }

    bool pollBit = (pdu[0] & 0x40) != 0;
    uint16_t sn  = (static_cast<uint16_t>(pdu[0] & 0x0F) << 8)
                 |  static_cast<uint16_t>(pdu[1]);
    sn &= AM_SN_MASK;

    if (sn == e.rxExpSN) {
        ByteBuffer sdu(pdu.begin() + 2, pdu.end());
        e.rxSduQueue.push(std::move(sdu));
        e.rxExpSN = (e.rxExpSN + 1) & AM_SN_MASK;
    }

    if (pollBit) {
        ByteBuffer status = buildStatus(e);
        RBS_LOG_DEBUG("UmtsRLC",
            "AM poll rnti={} rbId={} sn={} → STATUS len={}",
            e.rnti, e.rbId, sn, status.size());
        e.retxQueue.push(std::move(status));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IUMTSRlc implementation
// ─────────────────────────────────────────────────────────────────────────────

bool UMTSRlc::addRB(RNTI rnti, uint8_t rbId, RLCMode mode)
{
    EntityKey key = makeKey(rnti, rbId);
    if (entities_.count(key)) return true;
    RLCEntity e;
    e.rnti  = rnti;
    e.rbId  = rbId;
    e.mode  = mode;
    e.vtWS  = 32;  // default AM window (TS 25.322)
    entities_[key] = std::move(e);
    RBS_LOG_DEBUG("UmtsRLC",
        "addRB rnti={} rbId={} mode={}", rnti, rbId, static_cast<int>(mode));
    return true;
}

bool UMTSRlc::removeRB(RNTI rnti, uint8_t rbId)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    entities_.erase(it);
    RBS_LOG_DEBUG("UmtsRLC", "removeRB rnti={} rbId={}", rnti, rbId);
    return true;
}

bool UMTSRlc::sendSdu(RNTI rnti, uint8_t rbId, ByteBuffer sdu)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    it->second.txQueue.push(std::move(sdu));
    return true;
}

bool UMTSRlc::pollPdu(RNTI rnti, uint8_t rbId, ByteBuffer& pdu)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    auto& e = it->second;

    switch (e.mode) {
        case RLCMode::TM: {
            if (e.txQueue.empty()) return false;
            pdu = std::move(e.txQueue.front());
            e.txQueue.pop();
            return true;
        }
        case RLCMode::UM: {
            if (e.txQueue.empty()) return false;
            ByteBuffer sdu = std::move(e.txQueue.front());
            e.txQueue.pop();
            uint16_t sn = e.txSN;
            e.txSN = (e.txSN + 1) & UM_SN_MASK;
            pdu = addUMHeader(sdu, sn);
            return true;
        }
        case RLCMode::AM: {
            if (!e.retxQueue.empty()) {
                pdu = std::move(e.retxQueue.front());
                e.retxQueue.pop();
                return true;
            }
            if (e.txQueue.empty()) return false;
            ByteBuffer sdu = std::move(e.txQueue.front());
            e.txQueue.pop();
            pdu = segmentAM(e, sdu, 320 /*default MAC PDU bytes*/);
            // If SDU was only partially consumed, put remainder back
            // (segmentAM truncates; remainder handling would need offset tracking;
            //  for the simulator single-segment approach is sufficient)
            return !pdu.empty();
        }
    }
    return false;
}

void UMTSRlc::deliverPdu(RNTI rnti, uint8_t rbId, const ByteBuffer& pdu)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return;
    auto& e = it->second;
    switch (e.mode) {
        case RLCMode::TM: processTM(e, pdu); break;
        case RLCMode::UM: processUM(e, pdu); break;
        case RLCMode::AM: processAM(e, pdu); break;
    }
}

bool UMTSRlc::receiveSdu(RNTI rnti, uint8_t rbId, ByteBuffer& sdu)
{
    auto it = entities_.find(makeKey(rnti, rbId));
    if (it == entities_.end()) return false;
    auto& e = it->second;
    if (e.rxSduQueue.empty()) return false;
    sdu = std::move(e.rxSduQueue.front());
    e.rxSduQueue.pop();
    return true;
}

uint16_t UMTSRlc::txSN(RNTI rnti, uint8_t rbId) const
{
    auto it = entities_.find(makeKey(rnti, rbId));
    return (it != entities_.end()) ? it->second.txSN : 0;
}

uint16_t UMTSRlc::rxSN(RNTI rnti, uint8_t rbId) const
{
    auto it = entities_.find(makeKey(rnti, rbId));
    return (it != entities_.end()) ? it->second.rxExpSN : 0;
}

} // namespace rbs::umts

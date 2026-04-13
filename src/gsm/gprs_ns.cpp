#include "gprs_ns.h"
#include "../common/logger.h"

namespace rbs::gsm {

namespace {
// ── NS IE identifiers (TS 48.016 §10.3) ─────────────────────────────────────
constexpr uint8_t IEI_NS_CAUSE = 0x00;
constexpr uint8_t IEI_NSVCI    = 0x01;
constexpr uint8_t IEI_NSEI     = 0x02;
constexpr uint8_t IEI_BVCI     = 0x03;
constexpr uint8_t IEI_NS_SDU   = 0x04;
}  // namespace

GprsNs::GprsNs(uint16_t nsei, uint16_t nsvci)
    : nsei_(nsei), nsvci_(nsvci) {}

void GprsNs::appendTlv(ByteBuffer& buf, uint8_t iei, const ByteBuffer& val) {
    buf.push_back(iei);
    buf.push_back(static_cast<uint8_t>(val.size()));
    buf.insert(buf.end(), val.begin(), val.end());
}

bool GprsNs::findTlv(const ByteBuffer& pdu, size_t start,
                     uint8_t iei, ByteBuffer& out) {
    for (size_t i = start; i + 1 < pdu.size(); ) {
        const uint8_t t = pdu[i];
        const uint8_t l = pdu[i + 1];
        if (i + 2u + l > pdu.size()) {
            break;
        }
        if (t == iei) {
            out.assign(pdu.begin() + static_cast<ptrdiff_t>(i + 2),
                       pdu.begin() + static_cast<ptrdiff_t>(i + 2 + l));
            return true;
        }
        i += 2u + l;
    }
    return false;
}

uint16_t GprsNs::decodeBe16(const ByteBuffer& v) {
    if (v.size() < 2) {
        return 0;
    }
    return static_cast<uint16_t>((v[0] << 8) | v[1]);
}

// ── Encode ──────────────────────────────────────────────────────────────────

ByteBuffer GprsNs::encodeReset() const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(NsPduType::NS_RESET));
    appendTlv(pdu, IEI_NS_CAUSE, {static_cast<uint8_t>(NsCause::OM_INTERVENTION)});
    appendTlv(pdu, IEI_NSVCI, {static_cast<uint8_t>(nsvci_ >> 8),
                                static_cast<uint8_t>(nsvci_)});
    appendTlv(pdu, IEI_NSEI,  {static_cast<uint8_t>(nsei_ >> 8),
                                static_cast<uint8_t>(nsei_)});
    return pdu;
}

ByteBuffer GprsNs::encodeResetAck() const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(NsPduType::NS_RESET_ACK));
    appendTlv(pdu, IEI_NSVCI, {static_cast<uint8_t>(nsvci_ >> 8),
                                static_cast<uint8_t>(nsvci_)});
    appendTlv(pdu, IEI_NSEI,  {static_cast<uint8_t>(nsei_ >> 8),
                                static_cast<uint8_t>(nsei_)});
    return pdu;
}

ByteBuffer GprsNs::encodeAlive() const {
    return {static_cast<uint8_t>(NsPduType::NS_ALIVE)};
}

ByteBuffer GprsNs::encodeAliveAck() const {
    return {static_cast<uint8_t>(NsPduType::NS_ALIVE_ACK)};
}

ByteBuffer GprsNs::encodeUnblock() const {
    return {static_cast<uint8_t>(NsPduType::NS_UNBLOCK)};
}

ByteBuffer GprsNs::encodeUnblockAck() const {
    return {static_cast<uint8_t>(NsPduType::NS_UNBLOCK_ACK)};
}

ByteBuffer GprsNs::encodeUnitdata(uint16_t bvci, const ByteBuffer& bssgpSdu) const {
    ByteBuffer pdu;
    pdu.push_back(static_cast<uint8_t>(NsPduType::NS_UNITDATA));
    appendTlv(pdu, IEI_BVCI,
              {static_cast<uint8_t>(bvci >> 8), static_cast<uint8_t>(bvci)});
    appendTlv(pdu, IEI_NS_SDU, bssgpSdu);
    return pdu;
}

// ── Handle ──────────────────────────────────────────────────────────────────

ByteBuffer GprsNs::handlePdu(const ByteBuffer& pdu, ByteBuffer& response) {
    response.clear();
    if (pdu.empty()) {
        return {};
    }
    ++stats_.rxFrames;
    const auto type = static_cast<NsPduType>(pdu[0]);

    switch (type) {
    case NsPduType::NS_RESET:
        RBS_LOG_INFO("NS", "NS-RESET rx NSEI={}", nsei_);
        state_    = NsVcState::BLOCKED;
        response  = encodeResetAck();
        ++stats_.txFrames;
        break;

    case NsPduType::NS_RESET_ACK:
        RBS_LOG_INFO("NS", "NS-RESET-ACK rx NSEI={}", nsei_);
        state_   = NsVcState::BLOCKED;
        // Initiate VC unblocking per TS 48.016 §7.3.
        response = encodeUnblock();
        ++stats_.txFrames;
        break;

    case NsPduType::NS_UNBLOCK:
        RBS_LOG_DEBUG("NS", "NS-UNBLOCK rx NSEI={}", nsei_);
        state_   = NsVcState::ALIVE;
        response = encodeUnblockAck();
        ++stats_.txFrames;
        break;

    case NsPduType::NS_UNBLOCK_ACK:
        RBS_LOG_DEBUG("NS", "NS-UNBLOCK-ACK rx NSEI={}", nsei_);
        state_ = NsVcState::ALIVE;
        break;

    case NsPduType::NS_ALIVE:
        RBS_LOG_DEBUG("NS", "NS-ALIVE rx NSEI={}", nsei_);
        ++stats_.rxAlive;
        response = encodeAliveAck();
        ++stats_.txFrames;
        ++stats_.txAlive;
        break;

    case NsPduType::NS_ALIVE_ACK:
        RBS_LOG_DEBUG("NS", "NS-ALIVE-ACK rx NSEI={}", nsei_);
        ++stats_.rxAlive;
        break;

    case NsPduType::NS_UNITDATA: {
        ByteBuffer sdu;
        if (findTlv(pdu, 1, IEI_NS_SDU, sdu)) {
            return sdu;
        }
        RBS_LOG_ERROR("NS", "NS-UNITDATA: missing NS-SDU IE, NSEI={}", nsei_);
        break;
    }

    default:
        RBS_LOG_DEBUG("NS", "Unknown NS PDU type={}", static_cast<unsigned>(pdu[0]));
        break;
    }

    return {};
}

void GprsNs::forceAlive() {
    state_ = NsVcState::ALIVE;
}

}  // namespace rbs::gsm

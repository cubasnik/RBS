#include "nr_pdcp.h"

namespace rbs::nr {

ByteBuffer NRPDCP::encodeDataPdu(RNTI crnti, uint8_t drbId, const ByteBuffer& payload) {
    uint32_t& sn = snState_[crnti][drbId];
    const uint32_t sn18 = (sn & 0x3FFFFu);

    ByteBuffer out;
    out.reserve(payload.size() + 3);
    out.push_back(static_cast<uint8_t>((sn18 >> 16) & 0x03));
    out.push_back(static_cast<uint8_t>((sn18 >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(sn18 & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());

    sn = (sn + 1u) & 0x3FFFFu;
    return out;
}

bool NRPDCP::decodeDataPdu(const ByteBuffer& pdu, uint32_t& sn18, ByteBuffer& payload) const {
    if (pdu.size() < 3) {
        return false;
    }
    sn18 = ((static_cast<uint32_t>(pdu[0] & 0x03) << 16) |
            (static_cast<uint32_t>(pdu[1]) << 8) |
            static_cast<uint32_t>(pdu[2]));
    payload.assign(pdu.begin() + 3, pdu.end());
    return true;
}

uint32_t NRPDCP::currentSn(RNTI crnti, uint8_t drbId) const {
    const auto ueIt = snState_.find(crnti);
    if (ueIt == snState_.end()) {
        return 0;
    }
    const auto drbIt = ueIt->second.find(drbId);
    return drbIt != ueIt->second.end() ? drbIt->second : 0;
}

}  // namespace rbs::nr

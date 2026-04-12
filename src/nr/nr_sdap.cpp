#include "nr_sdap.h"

namespace rbs::nr {

bool NRSDAP::mapQfiToDrb(RNTI crnti, uint8_t qfi, uint8_t drbId) {
    if (crnti == 0 || qfi == 0 || qfi > 63 || drbId == 0) {
        return false;
    }
    qfiMap_[crnti][qfi] = drbId;
    return true;
}

void NRSDAP::clearMappings(RNTI crnti) {
    qfiMap_.erase(crnti);
}

uint8_t NRSDAP::resolveDrb(RNTI crnti, uint8_t qfi) const {
    const auto ueIt = qfiMap_.find(crnti);
    if (ueIt == qfiMap_.end()) {
        return 0;
    }
    const auto mapIt = ueIt->second.find(qfi);
    return mapIt != ueIt->second.end() ? mapIt->second : 0;
}

ByteBuffer NRSDAP::encodeDataPdu(RNTI crnti, uint8_t qfi, const ByteBuffer& payload) const {
    (void)crnti;
    ByteBuffer out;
    out.reserve(payload.size() + 1);
    out.push_back(static_cast<uint8_t>(qfi & 0x3F));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool NRSDAP::decodeDataPdu(const ByteBuffer& pdu, uint8_t& qfi, ByteBuffer& payload) const {
    if (pdu.empty()) {
        return false;
    }
    qfi = static_cast<uint8_t>(pdu[0] & 0x3F);
    payload.assign(pdu.begin() + 1, pdu.end());
    return qfi >= 1 && qfi <= 63;
}

}  // namespace rbs::nr

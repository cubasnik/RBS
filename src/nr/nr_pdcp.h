#pragma once

#include "../common/types.h"
#include <cstdint>
#include <unordered_map>

namespace rbs::nr {

// Minimal NR PDCP model with 18-bit SN progression.
class NRPDCP {
public:
    // Encode a PDCP PDU as [SN(3 bytes, low 18 bits used)] + payload.
    ByteBuffer encodeDataPdu(RNTI crnti, uint8_t drbId, const ByteBuffer& payload);

    // Returns false for malformed PDUs.
    bool decodeDataPdu(const ByteBuffer& pdu, uint32_t& sn18, ByteBuffer& payload) const;

    uint32_t currentSn(RNTI crnti, uint8_t drbId) const;

private:
    using DrbSnMap = std::unordered_map<uint8_t, uint32_t>;
    std::unordered_map<RNTI, DrbSnMap> snState_;
};

}  // namespace rbs::nr

#pragma once

#include "../common/types.h"
#include <cstdint>
#include <unordered_map>

namespace rbs::nr {

// Minimal SDAP entity (TS 37.324): QoS Flow (QFI) to DRB mapping
// and lightweight SDAP header add/remove for simulation.
class NRSDAP {
public:
    // Configure mapping for a UE: QFI (1..63) -> DRB ID.
    bool mapQfiToDrb(RNTI crnti, uint8_t qfi, uint8_t drbId);

    // Remove all QFI mappings for a UE.
    void clearMappings(RNTI crnti);

    // Resolve DRB for a UE/QFI pair. Returns 0 if mapping is absent.
    uint8_t resolveDrb(RNTI crnti, uint8_t qfi) const;

    // SDAP PDU format used here (1 byte header + payload):
    // [bits7..6 flags=0][bits5..0 QFI]
    ByteBuffer encodeDataPdu(RNTI crnti, uint8_t qfi, const ByteBuffer& payload) const;

    // Returns false for malformed SDAP PDU.
    bool decodeDataPdu(const ByteBuffer& pdu, uint8_t& qfi, ByteBuffer& payload) const;

private:
    std::unordered_map<RNTI, std::unordered_map<uint8_t, uint8_t>> qfiMap_;
};

}  // namespace rbs::nr

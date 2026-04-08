#pragma once
#include "../common/types.h"

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// IPDCP — pure-virtual interface for the LTE PDCP layer.
//
// Responsibilities (3GPP TS 36.323):
//   DL: IP packet → [ROHC compress] → add PDCP header (SN) → cipher → MAC
//   UL: MAC → decipher → strip PDCP header → [ROHC decompress] → IP packet
//
// Ciphering algorithms (TS 33.401):
//   NULL_ALG (stub), AES-128-CTR, SNOW3G, ZUC
//
// Integrity protection (SRBs only):
//   NULL_ALG, AES-128-CMAC, SNOW3G, ZUC
// ─────────────────────────────────────────────────────────────────────────────
class IPDCP {
public:
    virtual ~IPDCP() = default;

    // ── Radio Bearer management ───────────────────────────────────────────────
    virtual bool addBearer   (RNTI rnti, const struct PDCPConfig& cfg) = 0;
    virtual bool removeBearer(RNTI rnti, uint16_t bearerId) = 0;

    // ── Data plane ────────────────────────────────────────────────────────────
    /// DL: process an IP packet from upper layer → returns PDCP PDU for MAC.
    virtual ByteBuffer processDlPacket(RNTI rnti, uint16_t bearerId,
                                       const ByteBuffer& ipPacket) = 0;

    /// UL: process a PDCP PDU from MAC → returns reconstructed IP packet.
    virtual ByteBuffer processUlPDU   (RNTI rnti, uint16_t bearerId,
                                       const ByteBuffer& pdcpPDU) = 0;
};

}  // namespace rbs::lte

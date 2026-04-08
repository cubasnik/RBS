#pragma once
#include "../common/types.h"
#include "ilte_pdcp.h"
#include <unordered_map>
#include <queue>

namespace rbs::lte {

// ────────────────────────────────────────────────────────────────
// PDCP Layer (Packet Data Convergence Protocol)
// Provides: IP header compression (ROHC), ciphering (AES-128),
// integrity protection (HMAC-SHA-256), and sequence numbering.
// References: 3GPP TS 36.323
// ────────────────────────────────────────────────────────────────

enum class PDCPCipherAlg : uint8_t { NULL_ALG = 0, AES = 1, SNOW3G = 2, ZUC = 3 };
enum class PDCPIntegAlg  : uint8_t { NULL_ALG = 0, AES = 1, SNOW3G = 2, ZUC = 3 };

struct PDCPConfig {
    uint16_t      bearerId;
    PDCPCipherAlg cipherAlg   = PDCPCipherAlg::NULL_ALG;
    PDCPIntegAlg  integAlg    = PDCPIntegAlg::NULL_ALG;
    uint8_t       cipherKey[16]{};
    uint8_t       integKey [16]{};
    bool          headerCompression = false;
    bool          statusReport      = true;
};

struct PDCPEntity {
    PDCPConfig config;
    uint32_t   txSN  = 0;
    uint32_t   rxSN  = 0;
    std::queue<ByteBuffer> txQueue;
    std::queue<ByteBuffer> rxQueue;
};

class PDCP : public IPDCP {
public:
    bool addBearer(RNTI rnti, const PDCPConfig& cfg)                override;
    bool removeBearer(RNTI rnti, uint16_t bearerId)                  override;

    ByteBuffer processDlPacket(RNTI rnti, uint16_t bearerId,
                               const ByteBuffer& ipPacket)           override;

    ByteBuffer processUlPDU(RNTI rnti, uint16_t bearerId,
                            const ByteBuffer& pdcpPDU)               override;

private:
    // key: (rnti << 16) | bearerId
    std::unordered_map<uint32_t, PDCPEntity> entities_;

    static uint32_t makeKey(RNTI rnti, uint16_t bid) {
        return (static_cast<uint32_t>(rnti) << 16) | bid;
    }

    ByteBuffer addHeader(const ByteBuffer& payload, uint32_t sn) const;
    ByteBuffer stripHeader(const ByteBuffer& pdu, uint32_t& sn) const;
    ByteBuffer cipher   (const ByteBuffer& data,
                         const uint8_t key[16], uint32_t count) const;
    ByteBuffer decipher (const ByteBuffer& data,
                         const uint8_t key[16], uint32_t count) const;
    // ROHC header compression (stub)
    ByteBuffer rohcCompress  (const ByteBuffer& ipPkt) const;
    ByteBuffer rohcDecompress(const ByteBuffer& rohcPkt) const;
};

}  // namespace rbs::lte

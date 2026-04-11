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

// ROHC profiles per RFC 3095 / RFC 4995 / TS 36.323 §6.2.13
enum class ROHCProfile : uint16_t {
    UNCOMPRESSED = 0x0000,  ///< No compression
    RTP_UDP_IP   = 0x0001,  ///< IP/UDP/RTP VoLTE profile (RFC 3095 §5)
    UDP_IP       = 0x0002,  ///< IP/UDP profile
    ESP_IP       = 0x0003,  ///< IP/ESP profile
    IP_ONLY      = 0x0004,  ///< IP-only profile
};

// ROHC compressor state machine  (RFC 3095 §4.3.1)
enum class ROHCState : uint8_t { IR = 0, FO = 1, SO = 2 };
constexpr uint8_t ROHC_IR_TO_FO_PKTS = 3;   ///< packets in IR before moving to FO
constexpr uint8_t ROHC_FO_TO_SO_PKTS = 3;   ///< packets in FO before moving to SO

struct PDCPConfig {
    uint16_t      bearerId;
    PDCPCipherAlg cipherAlg   = PDCPCipherAlg::NULL_ALG;
    PDCPIntegAlg  integAlg    = PDCPIntegAlg::NULL_ALG;
    uint8_t       cipherKey[16]{};
    uint8_t       integKey [16]{};
    bool          headerCompression = false;
    ROHCProfile   rohcProfile       = ROHCProfile::UNCOMPRESSED;  ///< active only when headerCompression=true
    bool          statusReport      = true;
};

// ROHC compressor context — one per PDCP entity when headerCompression=true
struct ROHCContext {
    ROHCProfile profile    = ROHCProfile::UNCOMPRESSED;
    ROHCState   txState    = ROHCState::IR;   ///< compressor state (RFC 3095 §4.3.1)
    ROHCState   rxState    = ROHCState::IR;   ///< decompressor state (RFC 3095 §4.3.2)
    uint8_t     txStatePkts = 0;              ///< packets transmitted in current TX state
    // Last-seen IP/UDP/RTP static fields (used to detect changes in FO/SO)
    uint32_t    lastSrcIp   = 0;
    uint32_t    lastDstIp   = 0;
    uint16_t    lastSrcPort = 0;
    uint16_t    lastDstPort = 0;
    uint16_t    lastRtpSsrc = 0;  ///< low 16 bits (SSRC is 32-bit; store low half)
    uint16_t    lastRtpSn   = 0;  ///< last RTP sequence number
    uint8_t     lastRtpPt   = 0;  ///< last RTP payload type
};

struct PDCPEntity {
    PDCPConfig config;
    uint32_t   txSN  = 0;
    uint32_t   rxSN  = 0;
    uint32_t   txHFN = 0;  ///< Hyper Frame Number for TX (TS 36.323 §7.1)
    uint32_t   rxHFN = 0;  ///< Hyper Frame Number for RX
    bool       wrapWarned = false; ///< Set once CRITICAL wrap warning emitted
    std::queue<ByteBuffer> txQueue;
    std::queue<ByteBuffer> rxQueue;
    ROHCContext rohc;  ///< ROHC compressor/decompressor state

    /// Full 32-bit COUNT value: (HFN << 12) | (SN & 0xFFF)  (TS 36.323 §7.1)
    uint32_t txCOUNT() const { return (txHFN << 12) | (txSN & 0xFFFu); }
    uint32_t rxCOUNT() const { return (rxHFN << 12) | (rxSN & 0xFFFu); }
};

class PDCP : public IPDCP {
public:
    bool addBearer(RNTI rnti, const PDCPConfig& cfg)                override;
    bool removeBearer(RNTI rnti, uint16_t bearerId)                  override;

    ByteBuffer processDlPacket(RNTI rnti, uint16_t bearerId,
                               const ByteBuffer& ipPacket)           override;

    ByteBuffer processUlPDU(RNTI rnti, uint16_t bearerId,
                            const ByteBuffer& pdcpPDU)               override;

    // ── Integrity protection (EIA2 AES-128-CMAC, TS 33.401 §6.4.2b) ─────────
    /// Appends 4-byte MAC-I to buf using the entity's integKey.
    ByteBuffer applyIntegrity(RNTI rnti, uint16_t bearerId,
                              const ByteBuffer& buf) const;

    /// Strips and verifies MAC-I.  Returns payload if OK, empty if failed.
    ByteBuffer verifyIntegrity(RNTI rnti, uint16_t bearerId,
                               const ByteBuffer& buf) const;

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
    // ROHC header compression — RFC 3095 / RFC 4995 profile 0x0001 (IP/UDP/RTP)
    ByteBuffer rohcCompressWithCtx  (const ByteBuffer& ipPkt,  ROHCContext& ctx) const;
    ByteBuffer rohcDecompressWithCtx(const ByteBuffer& rohcPkt, ROHCContext& ctx) const;
    // Legacy pass-through overloads (kept for callers that don't have ctx)
    ByteBuffer rohcCompress  (const ByteBuffer& ipPkt) const;
    ByteBuffer rohcDecompress(const ByteBuffer& rohcPkt) const;

    /// AES-128-CMAC per RFC 4493.
    /// Writes 16-byte tag into out[0..15].
    void computeCMAC_(const uint8_t key[16],
                      const uint8_t* msg, size_t msgLen,
                      uint8_t out[16]) const;
};

}  // namespace rbs::lte

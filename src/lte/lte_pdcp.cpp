#include "lte_pdcp.h"
#include "../common/logger.h"
#include <cstring>

namespace rbs::lte {

// ────────────────────────────────────────────────────────────────
bool PDCP::addBearer(RNTI rnti, const PDCPConfig& cfg) {
    uint32_t key = makeKey(rnti, cfg.bearerId);
    if (entities_.count(key)) return false;
    PDCPEntity e{};
    e.config = cfg;
    entities_[key] = std::move(e);
    RBS_LOG_DEBUG("PDCP", "Bearer added RNTI=", rnti, " BID=", cfg.bearerId);
    return true;
}

bool PDCP::removeBearer(RNTI rnti, uint16_t bearerId) {
    return entities_.erase(makeKey(rnti, bearerId)) > 0;
}

// ────────────────────────────────────────────────────────────────
// DL path: IP → [ROHC] → [cipher] → PDCP PDU
// ────────────────────────────────────────────────────────────────
ByteBuffer PDCP::processDlPacket(RNTI rnti, uint16_t bearerId,
                                  const ByteBuffer& ipPacket) {
    auto it = entities_.find(makeKey(rnti, bearerId));
    if (it == entities_.end()) return {};

    PDCPEntity& e = it->second;
    ByteBuffer payload = ipPacket;

    if (e.config.headerCompression)
        payload = rohcCompress(payload);

    if (e.config.cipherAlg != PDCPCipherAlg::NULL_ALG)
        payload = cipher(payload, e.config.cipherKey, e.txSN);

    ByteBuffer pdu = addHeader(payload, e.txSN);
    ++e.txSN;
    return pdu;
}

// ────────────────────────────────────────────────────────────────
// UL path: PDCP PDU → [decipher] → [ROHC decompress] → IP
// ────────────────────────────────────────────────────────────────
ByteBuffer PDCP::processUlPDU(RNTI rnti, uint16_t bearerId,
                               const ByteBuffer& pdcpPDU) {
    auto it = entities_.find(makeKey(rnti, bearerId));
    if (it == entities_.end()) return {};

    PDCPEntity& e = it->second;
    uint32_t rxSN = 0;
    ByteBuffer payload = stripHeader(pdcpPDU, rxSN);

    if (e.config.cipherAlg != PDCPCipherAlg::NULL_ALG)
        payload = decipher(payload, e.config.cipherKey, rxSN);

    if (e.config.headerCompression)
        payload = rohcDecompress(payload);

    e.rxSN = rxSN + 1;
    return payload;
}

// ────────────────────────────────────────────────────────────────
// PDCP header: D/C bit | SN (12 bits) per 3GPP TS 36.323 §6.2.3
// ────────────────────────────────────────────────────────────────
ByteBuffer PDCP::addHeader(const ByteBuffer& payload, uint32_t sn) const {
    ByteBuffer pdu;
    pdu.reserve(2 + payload.size());
    pdu.push_back(static_cast<uint8_t>(0x80 | ((sn >> 8) & 0x0F)));  // D/C=1, SN[11:8]
    pdu.push_back(static_cast<uint8_t>(sn & 0xFF));                   // SN[7:0]
    pdu.insert(pdu.end(), payload.begin(), payload.end());
    return pdu;
}

ByteBuffer PDCP::stripHeader(const ByteBuffer& pdu, uint32_t& sn) const {
    if (pdu.size() < 2) return {};
    sn = ((static_cast<uint32_t>(pdu[0]) & 0x0F) << 8) | pdu[1];
    return ByteBuffer(pdu.begin() + 2, pdu.end());
}

// ────────────────────────────────────────────────────────────────
// AES-128 CTR mode ciphering
// NOTE: This is a PLACEHOLDER implementation using XOR with a
// counter-derived keystream.  In production, replace with an
// FIPS 197 / ETSI TS 135.202 compliant AES implementation
// (e.g., via OpenSSL EVP_aes_128_ctr).
// ────────────────────────────────────────────────────────────────
ByteBuffer PDCP::cipher(const ByteBuffer& data,
                         const uint8_t key[16], uint32_t count) const {
    ByteBuffer out = data;
    for (size_t i = 0; i < out.size(); ++i) {
        // Very simplified: XOR with key byte rotated by count
        out[i] ^= key[(i + count) % 16];
    }
    return out;
}

ByteBuffer PDCP::decipher(const ByteBuffer& data,
                           const uint8_t key[16], uint32_t count) const {
    // CTR mode is self-inverse
    return cipher(data, key, count);
}

// ────────────────────────────────────────────────────────────────
// ROHC stubs (real impl would use rfc3095 / rfc4995)
// ────────────────────────────────────────────────────────────────
ByteBuffer PDCP::rohcCompress(const ByteBuffer& ipPkt) const {
    // In a real system: compress 40-byte IPv4/UDP/RTP → ~3 bytes
    return ipPkt;   // pass-through in this simulation
}

ByteBuffer PDCP::rohcDecompress(const ByteBuffer& rohcPkt) const {
    return rohcPkt;
}

}  // namespace rbs::lte

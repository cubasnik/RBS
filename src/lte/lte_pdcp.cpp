#include "lte_pdcp.h"
#include "../common/logger.h"
#include <algorithm>
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

// ── AES-128 (FIPS 197) compact implementation — TS 33.401 §6.4.4 / EEA2 ──────
namespace {

static const uint8_t kSBox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t kRCon[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static inline uint8_t xtime(uint8_t a) {
    return static_cast<uint8_t>((a << 1) ^ (a & 0x80 ? 0x1b : 0));
}

// AES ShiftRows: cyclic left shift of rows 1-3 (column-major state)
static void aesShiftRows(uint8_t s[16]) {
    uint8_t t;
    t=s[1];  s[1]=s[5];  s[5]=s[9];  s[9]=s[13]; s[13]=t;
    t=s[2];  s[2]=s[10]; s[10]=t;
    t=s[6];  s[6]=s[14]; s[14]=t;
    t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3];  s[3]=t;
}

// AES MixColumns: matrix multiply each 4-byte column over GF(2^8)
static void aesMixColumns(uint8_t s[16]) {
    for (int c = 0; c < 4; ++c) {
        uint8_t* col = s + c * 4;
        const uint8_t a=col[0], b=col[1], cc=col[2], d=col[3];
        col[0] = xtime(a) ^ (xtime(b)^b) ^ cc           ^ d;
        col[1] = a         ^ xtime(b)     ^ (xtime(cc)^cc) ^ d;
        col[2] = a         ^ b             ^ xtime(cc)      ^ (xtime(d)^d);
        col[3] = (xtime(a)^a) ^ b         ^ cc             ^ xtime(d);
    }
}

// AES-128 key schedule: produces 11 round keys of 16 bytes each
static void aesKeyExpand(const uint8_t key[16], uint8_t rk[11][16]) {
    std::memcpy(rk[0], key, 16);
    for (int r = 1; r <= 10; ++r) {
        const uint8_t* p = rk[r-1];
        uint8_t*       c = rk[r];
        // W[4r]: RotWord + SubWord + Rcon
        c[0] = kSBox[p[13]] ^ kRCon[r];
        c[1] = kSBox[p[14]];
        c[2] = kSBox[p[15]];
        c[3] = kSBox[p[12]];
        for (int b = 0; b < 4; ++b) c[b] ^= p[b];
        // W[4r+1 .. 4r+3]
        for (int w = 1; w < 4; ++w)
            for (int b = 0; b < 4; ++b)
                c[w*4+b] = p[w*4+b] ^ c[(w-1)*4+b];
    }
}

// AES-128 encrypt one 16-byte block in-place
static void aesEncryptBlock(uint8_t blk[16], const uint8_t rk[11][16]) {
    for (int i = 0; i < 16; ++i) blk[i] ^= rk[0][i];
    for (int r = 1; r < 10; ++r) {
        for (int i = 0; i < 16; ++i) blk[i] = kSBox[blk[i]];
        aesShiftRows(blk);
        aesMixColumns(blk);
        for (int i = 0; i < 16; ++i) blk[i] ^= rk[r][i];
    }
    for (int i = 0; i < 16; ++i) blk[i] = kSBox[blk[i]];
    aesShiftRows(blk);
    for (int i = 0; i < 16; ++i) blk[i] ^= rk[10][i];
}

// AES-128-CTR: encrypt/decrypt data using counter-mode keystream.
// Counter block (128-bit): bytes 0-11 = zeros, bytes 12-15 = COUNT big-endian.
// This matches the simplified EEA2 counter block per TS 33.401 §6.4.4.
static ByteBuffer aes128Ctr(const uint8_t key[16], uint32_t count,
                             const ByteBuffer& data) {
    if (data.empty()) return {};
    uint8_t rk[11][16];
    aesKeyExpand(key, rk);

    uint8_t ctr[16] = {};
    ctr[12] = static_cast<uint8_t>(count >> 24);
    ctr[13] = static_cast<uint8_t>(count >> 16);
    ctr[14] = static_cast<uint8_t>(count >>  8);
    ctr[15] = static_cast<uint8_t>(count);

    ByteBuffer out = data;
    for (size_t i = 0; i < out.size(); i += 16) {
        uint8_t ks[16];
        std::memcpy(ks, ctr, 16);
        aesEncryptBlock(ks, rk);
        const size_t chunk = std::min(static_cast<size_t>(16), out.size() - i);
        for (size_t j = 0; j < chunk; ++j)
            out[i + j] ^= ks[j];
        // Increment counter (big-endian 128-bit)
        for (int k = 15; k >= 0 && ++ctr[k] == 0; --k) {}
    }
    return out;
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────

// ────────────────────────────────────────────────────────────────
// AES-128-CTR ciphering (TS 33.401 §6.4.4 / EEA2)
// ────────────────────────────────────────────────────────────────
ByteBuffer PDCP::cipher(const ByteBuffer& data,
                         const uint8_t key[16], uint32_t count) const {
    return aes128Ctr(key, count, data);
}

ByteBuffer PDCP::decipher(const ByteBuffer& data,
                           const uint8_t key[16], uint32_t count) const {
    return aes128Ctr(key, count, data);  // CTR mode is self-inverse
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

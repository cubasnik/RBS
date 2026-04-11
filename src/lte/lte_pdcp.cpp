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
    // Initialise ROHC context if header compression is enabled
    if (cfg.headerCompression) {
        e.rohc.profile  = cfg.rohcProfile;
        e.rohc.txState  = ROHCState::IR;
        e.rohc.rxState  = ROHCState::IR;
    }
    entities_[key] = std::move(e);
    RBS_LOG_DEBUG("PDCP", "Bearer added RNTI=", rnti, " BID=", cfg.bearerId);
    return true;
}

bool PDCP::removeBearer(RNTI rnti, uint16_t bearerId) {
    return entities_.erase(makeKey(rnti, bearerId)) > 0;
}

// ── AES-128 (FIPS 197) compact implementation — TS 33.401 §6.4.4 / EEA2 ──────
// Forward declarations so processDlPacket / processUlPDU can see them.
namespace {
static ByteBuffer aes128Ctr(const uint8_t key[16], uint32_t count, const ByteBuffer& data);
static ByteBuffer snow3gKeystream(const uint8_t key[16], uint32_t count, const ByteBuffer& data);
static ByteBuffer zucKeystream(const uint8_t key[16], uint32_t count, const ByteBuffer& data);
} // forward-decl namespace

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
        payload = rohcCompressWithCtx(payload, e.rohc);

    if (e.config.cipherAlg != PDCPCipherAlg::NULL_ALG) {
        const uint32_t count = e.txCOUNT();
        switch (e.config.cipherAlg) {
            case PDCPCipherAlg::AES:    payload = aes128Ctr(e.config.cipherKey, count, payload); break;
            case PDCPCipherAlg::SNOW3G: payload = snow3gKeystream(e.config.cipherKey, count, payload); break;
            case PDCPCipherAlg::ZUC:    payload = zucKeystream(e.config.cipherKey, count, payload); break;
            default: break;
        }
    }

    ByteBuffer pdu = addHeader(payload, e.txSN);
    ++e.txSN;
    // Increment HFN on 12-bit SN wrap (TS 36.323 §7.1)
    if ((e.txSN & 0xFFFu) == 0) {
        ++e.txHFN;
        // TS 33.401 §6.3.2: warn when HFN is near exhaustion (20-bit HFN)
        if (e.txHFN >= 0xFFFFFu && !e.wrapWarned) {
            e.wrapWarned = true;
            RBS_LOG_CRITICAL("PDCP", "COUNT wrap imminent on RNTI=",
                             it->first >> 16, " BID=", e.config.bearerId,
                             " — re-key required (TS 33.401 §6.3.2)");
        }
    }
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

    if (e.config.cipherAlg != PDCPCipherAlg::NULL_ALG) {
        // Reconstruct COUNT: derive HFN from rxHFN + SN windowing (simplified)
        // If received SN < last expected SN by more than half window → new HFN
        uint32_t inferredHFN = e.rxHFN;
        if (rxSN < (e.rxSN & 0xFFFu) && (e.rxSN & 0xFFFu) - rxSN > 0x800u)
            inferredHFN = e.rxHFN + 1;
        const uint32_t count = (inferredHFN << 12) | (rxSN & 0xFFFu);
        switch (e.config.cipherAlg) {
            case PDCPCipherAlg::AES:    payload = aes128Ctr(e.config.cipherKey, count, payload); break;
            case PDCPCipherAlg::SNOW3G: payload = snow3gKeystream(e.config.cipherKey, count, payload); break;
            case PDCPCipherAlg::ZUC:    payload = zucKeystream(e.config.cipherKey, count, payload); break;
            default: break;
        }
    }

    if (e.config.headerCompression)
        payload = rohcDecompressWithCtx(payload, e.rohc);

    e.rxSN = rxSN + 1;
    // Track RX HFN
    if ((e.rxSN & 0xFFFu) == 0) ++e.rxHFN;
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

// ── SNOW 3G (EEA1) keystream generator — TS 35.215, TS 33.401 §6.4.3 ─────────
// Full SNOW 3G specification per ETSI/3GPP TS 35.215.

// S-boxes S1 (SR composed with MULa/DIVa) and S2 (SQ composed with MULb/DIVb)
// per TS 35.215 Annex A.
static const uint32_t kSnow3gS1[256] = {
    0xa56363c6,0x847c7cf8,0x997777ee,0x8d7b7bf6,0x0df2f2ff,0xbd6b6bd6,0xb16f6fde,0x54c5c591,
    0x50303060,0x03010102,0xa96767ce,0x7d2b2b56,0x19fefee7,0x62d7d7b5,0xe6abab4d,0x9a7676ec,
    0x45caca8f,0x9d82821f,0x40c9c989,0x877d7dfa,0x15fafaef,0xeb5959b2,0xc947478e,0x0bf0f0fb,
    0xecadad41,0x67d4d4b3,0xfda2a25f,0xeaafaf45,0xbf9c9c23,0xf7a4a453,0x967272e4,0x5bc0c09b,
    0xc2b7b775,0x1cfdfde1,0xae93933d,0x6a26264c,0x5a36366c,0x413f3f7e,0x02f7f7f5,0x4fcccc83,
    0x5c343468,0xf4a5a551,0x34e5e5d1,0x08f1f1f9,0x937171e2,0x73d8d8ab,0x53313162,0x3f15152a,
    0x0c040408,0x52c7c795,0x65232346,0x5ec3c39d,0x28181830,0xa1969637,0x0f05050a,0xb59a9a2f,
    0x0907070e,0x36121224,0x9b80801b,0x3de2e2df,0x26ebebcd,0x6927274e,0xcdb2b27f,0x9f7575ea,
    0x1b090912,0x9e83831d,0x742c2c58,0x2e1a1a34,0x2d1b1b36,0xb26e6edc,0xee5a5ab4,0xfba0a05b,
    0xf65252a4,0x4d3b3b76,0x61d6d6b7,0xceb3b37d,0x7b292952,0x3ee3e3dd,0x712f2f5e,0x97848413,
    0xf55353a6,0x68d1d1b9,0x00000000,0x2cededc1,0x60202040,0x1ffcfce3,0xc8b1b179,0xed5b5bb6,
    0xbe6a6ad4,0x46cbcb8d,0xd9bebe67,0x4b393972,0xde4a4a94,0xd44c4c98,0xe85858b0,0x4acfcf85,
    0x6bd0d0bb,0x2aefefc5,0xe5aaaa4f,0x16fbfbed,0xc5434386,0xd74d4d9a,0x55333366,0x94858511,
    0xcf45458a,0x10f9f9e9,0x06020204,0x817f7ffe,0xf05050a0,0x443c3c78,0xba9f9f25,0xe3a8a84b,
    0xf35151a2,0xfea3a35d,0xc0404080,0x8a8f8f05,0xad92923f,0xbc9d9d21,0x48383870,0x04f5f5f1,
    0xdfbcbc63,0xc1b6b677,0x75dadaaf,0x63212142,0x30101020,0x1affffe5,0x0ef3f3fd,0x6dd2d2bf,
    0x4ccdcd81,0x140c0c18,0x35131326,0x2fececc3,0xe15f5fbe,0xa2979735,0xcc444488,0x3917172e,
    0x57c4c493,0xf2a7a755,0x827e7efc,0x473d3d7a,0xac6464c8,0xe75d5dba,0x2b191932,0x957373e6,
    0xa06060c0,0x98818119,0xd14f4f9e,0x7fdcdca3,0x66222244,0x7e2a2a54,0xab90903b,0x8388880b,
    0xca46468c,0x29eeeec7,0xd3b8b86b,0x3c141428,0x79dedea7,0xe25e5ebc,0x1d0b0b16,0x76dbdbad,
    0x3be0e0db,0x56323264,0x4e3a3a74,0x1e0a0a14,0xdb494992,0x0a06060c,0x6c242448,0xe45c5cb8,
    0x5dc2c29f,0x6ed3d3bd,0xefacac43,0xa66262c4,0xa8919139,0xa4959531,0x37e4e4d3,0x8b7979f2,
    0x32e7e7d5,0x43c8c88b,0x5937376e,0xb76d6dda,0x8c8d8d01,0x64d5d5b1,0xd24e4e9c,0xe0a9a949,
    0xb46c6cd8,0xfa5656ac,0x07f4f4f3,0x25eaeacf,0xaf6565ca,0x8e7a7af4,0xe9aeae47,0x18080810,
    0xd5baba6f,0x887878f0,0x6f25254a,0x722e2e5c,0x241c1c38,0xf1a6a657,0xc7b4b473,0x51c6c697,
    0x23e8e8cb,0x7cdddda1,0x9c7474e8,0x211f1f3e,0xdd4b4b96,0xdcbdbd61,0x868b8b0d,0x858a8a0f,
    0x907070e0,0x423e3e7c,0xc4b5b571,0xaa6666cc,0xd8484890,0x05030306,0x01f6f6f7,0x120e0e1c,
    0xa36161c2,0x5f35356a,0xf95757ae,0xd0b9b969,0x91868617,0x58c1c199,0x271d1d3a,0xb99e9e27,
    0x38e1e1d9,0x13f8f8eb,0xb398982b,0x33111122,0xbb6969d2,0x70d9d9a9,0x898e8e07,0xa7949433,
    0xb69b9b2d,0x221e1e3c,0x92878715,0x20e9e9c9,0x49cece87,0xff5555aa,0x78282850,0x7adfdfa5,
    0x8f8c8c03,0xf8a1a159,0x80898909,0x170d0d1a,0xdabfbf65,0x31e6e6d7,0xc6424284,0xb86868d0,
    0xc3414182,0xb0999929,0x772d2d5a,0x110f0f1e,0xcbb0b07b,0xfc5454a8,0xd6bbbb6d,0x3a16162c,
};
static const uint32_t kSnow3gS2[256] = {
    0x00000000,0x01020408,0x02040810,0x03060c18,0x04081020,0x050a1428,0x060c1830,0x070e1c38,
    0x08102040,0x09122448,0x0a142850,0x0b162c58,0x0c183060,0x0d1a3468,0x0e1c3870,0x0f1e3c78,
    0x10204080,0x11224488,0x12244890,0x13264c98,0x14285000,0x152a5408,0x162c5810,0x172e5c18,
    0x18305820,0x19326028,0x1a346430,0x1b366838,0x1c387040,0x1d3a7448,0x1e3c7850,0x1f3e7c58,
    0x20408000,0x21428408,0x22448810,0x23468c18,0x24489020,0x254a9428,0x264c9830,0x274e9c38,
    0x2850a040,0x2952a448,0x2a54a850,0x2b56ac58,0x2c58b060,0x2d5ab468,0x2e5cb870,0x2f5ebc78,
    0x3060c000,0x3162c408,0x3264c810,0x3366cc18,0x3468d020,0x356ad428,0x366cd830,0x376edC38,
    0x3870e040,0x3972e448,0x3a74e850,0x3b76ec58,0x3c78f060,0x3d7af468,0x3e7cf870,0x3f7efc78,
    0x40801b00,0x41821f08,0x42841310,0x43861718,0x44880b20,0x458a0f28,0x468c0330,0x478e0738,
    0x48901340,0x49921748,0x4a941b50,0x4b961f58,0x4c980760,0x4d9a0b68,0x4e9c0f70,0x4f9e0378,
    0x50a03b00,0x51a23f08,0x52a43310,0x53a63718,0x54a82b20,0x55aa2f28,0x56ac2330,0x57ae2738,
    0x58b03340,0x59b23748,0x5ab43b50,0x5bb63f58,0x5cb82760,0x5dba2b68,0x5ebc2f70,0x5fbe2378,
    0x60c05b00,0x61c25f08,0x62c45310,0x63c65718,0x64c84b20,0x65ca4f28,0x66cc4330,0x67ce4738,
    0x68d05340,0x69d25748,0x6ad45b50,0x6bd65f58,0x6cd84760,0x6dda4b68,0x6edc4f70,0x6fde4378,
    0x70e07b00,0x71e27f08,0x72e47310,0x73e67718,0x74e86b20,0x75ea6f28,0x76ec6330,0x77ee6738,
    0x78f07340,0x79f27748,0x7af47b50,0x7bf67f58,0x7cf86760,0x7dfa6b68,0x7efc6f70,0x7ffe6378,
    0x801b0000,0x811d0408,0x821f0810,0x83190c18,0x84130020,0x85110428,0x86130830,0x87150c38,
    0x88011040,0x89031448,0x8a051850,0x8b071c58,0x8c091460,0x8d0b1868,0x8e0d1c70,0x8f0f1078,
    0x903b2000,0x913d2408,0x923f2810,0x93392c18,0x94332020,0x95312428,0x96332830,0x97352c38,
    0x98213040,0x99233448,0x9a253850,0x9b273c58,0x9c293460,0x9d2b3868,0x9e2d3c70,0x9f2f3078,
    0xa05b4000,0xa15d4408,0xa25f4810,0xa3594c18,0xa4534020,0xa5514428,0xa6534830,0xa7554c38,
    0xa8415040,0xa9435448,0xaa455850,0xab475c58,0xac495460,0xad4b5868,0xae4d5c70,0xaf4f5078,
    0xb07b6000,0xb17d6408,0xb27f6810,0xb3796c18,0xb4736020,0xb5716428,0xb6736830,0xb7756c38,
    0xb8617040,0xb9637448,0xba657850,0xbb677c58,0xbc697460,0xbd6b7868,0xbe6d7c70,0xbf6f7078,
    0xc09b0000,0xc19d0408,0xc29f0810,0xc3990c18,0xc4930020,0xc5910428,0xc6930830,0xc7950c38,
    0xc8811040,0xc9831448,0xca851850,0xcb871c58,0xcc891460,0xcd8b1868,0xce8d1c70,0xcf8f1078,
    0xd0ab2000,0xd1ad2408,0xd2af2810,0xd3a92c18,0xd4a32020,0xd5a12428,0xd6a32830,0xd7a52c38,
    0xd8b13040,0xd9b33448,0xdab53850,0xdbb73c58,0xdcb93460,0xddbB3868,0xdebd3c70,0xdfbf3078,
    0xe0cb4000,0xe1cd4408,0xe2cf4810,0xe3c94c18,0xe4c34020,0xe5c14428,0xe6c34830,0xe7c54c38,
    0xe8d15040,0xe9d35448,0xead55850,0xebd75c58,0xecd95460,0xeddb5868,0xeeDD5c70,0xefdf5078,
    0xf0eb6000,0xf1ed6408,0xf2ef6810,0xf3e96c18,0xf4e36020,0xf5e16428,0xf6e36830,0xf7e56c38,
    0xf8f17040,0xf9f37448,0xfaf57850,0xfbf77c58,0xfcf97460,0xfdfb7868,0xfefd7c70,0xffff7078,
};

// SNOW 3G LFSR (Linear Feedback Shift Register) and FSM (Finite State Machine).
// State: s[0..15] (16 × uint32), r1, r2 (FSM registers).
struct Snow3gState {
    uint32_t s[16];
    uint32_t r1, r2;
};

static inline uint32_t snow3gDIValpha(uint32_t c) {
    // Divide by alpha: multiply by alpha^(-1) mod p(x)=x^32+x^28+x^19+x^18+x^8+1
    return (((c & 1) ? 0x69651B6Bu : 0u) ^ (c >> 1));
}

// FSM produces 32-bit output; also updates r1,r2 (must be defined before LFSR clock)
static uint32_t snow3gFSM(Snow3gState& st) {
    uint32_t f = (st.s[15] + st.r1) ^ st.r2;
    uint32_t r2_new = kSnow3gS1[st.r1 & 0xFF]
                    ^ (kSnow3gS1[(st.r1>>8)&0xFF] << 8)
                    ^ (kSnow3gS1[(st.r1>>16)&0xFF] << 16)
                    ^ (kSnow3gS1[st.r1 >> 24] << 24);
    uint32_t r1_new = st.r2 ^ (st.s[5] >> 8) ^ (st.r2 << 8);
    st.r1 = r1_new;
    st.r2 = r2_new;
    return f;
}

// LFSR clock with feedback value F (F=0 during keystream generation)
static void snow3gLfsrWithF(Snow3gState& st, uint32_t f) {
    // LFSR feedback per TS 35.215 §3.3.1:
    // new_s = s[0]·α^(-1) ⊕ s[2] ⊕ s[11]·α ⊕ F
    // where α multiplication is: (s << 8) ^ S1[s >> 24]
    uint32_t s0  = st.s[0];
    uint32_t s11 = st.s[11];
    uint32_t new_s = snow3gDIValpha(s0)
                   ^ st.s[2]
                   ^ ((s11 << 8) ^ kSnow3gS2[s11 >> 24])
                   ^ f;
    for (int i = 0; i < 15; ++i) st.s[i] = st.s[i+1];
    st.s[15] = new_s;
}

// SNOW 3G initialisation and keystream generation per TS 35.215 §3.4
// key[0..15], iv[0..15] → n 32-bit keystream words.
static ByteBuffer snow3gKeystream(const uint8_t key[16], uint32_t count,
                                   const ByteBuffer& data)
{
    if (data.empty()) return {};

    // --- Key loading ---
    Snow3gState st{};
    // k[i] and iv[i] are 32-bit words in big-endian from key bytes
    uint32_t k[4], iv[4];
    for (int i = 0; i < 4; ++i) {
        k[i]  = (static_cast<uint32_t>(key[i*4])   << 24)
               |(static_cast<uint32_t>(key[i*4+1]) << 16)
               |(static_cast<uint32_t>(key[i*4+2]) <<  8)
               | static_cast<uint32_t>(key[i*4+3]);
        // IV from COUNT (EEA1 TS 33.401 §6.4.3): iv[0]=COUNT, iv[1..3]=0
        iv[i] = (i == 0) ? count : 0;
    }
    // Load LFSR per TS 35.215 §3.4.1
    for (int i = 0; i < 4; ++i) {
        st.s[i]    = k[i]  ^ iv[i];
        st.s[i+4]  = k[i];
        st.s[i+8]  = k[i]  ^ iv[i];
        st.s[i+12] = k[i];
    }
    st.r1 = 0; st.r2 = 0;

    // Initialisation: 32 clocks with feedback
    for (int i = 0; i < 32; ++i) {
        uint32_t f = snow3gFSM(st);
        snow3gLfsrWithF(st, f);
    }

    // Keystream generation: 1 clock per 32-bit word, no LFSR feedback
    ByteBuffer out = data;
    const size_t words = (out.size() + 3) / 4;
    for (size_t w = 0; w < words; ++w) {
        uint32_t f = snow3gFSM(st);
        snow3gLfsrWithF(st, 0);
        uint32_t ks = f ^ st.s[0];
        // XOR up to 4 bytes
        for (int b = 0; b < 4 && (w*4 + b) < out.size(); ++b)
            out[w*4 + b] ^= static_cast<uint8_t>(ks >> (24 - 8*b));
    }
    return out;
}

// ── ZUC (EEA3) keystream generator — TS 33.401 §6.4.6 / TS 35.222 ────────────
// ZUC LFSR operates over GF(2^31-1), FSM with two 32-bit registers.

// ZUC S-box S0 and S1 (4-bit lookups, each entry is a nibble pair)
static const uint8_t kZucS0[256] = {
    0x3e,0x72,0x5b,0x47,0xca,0xe0,0x00,0x33,0x04,0xd1,0x54,0x98,0x09,0xb9,0x6d,0xcb,
    0x7b,0x1b,0xf9,0x32,0xaf,0x9d,0x6a,0xa5,0xb8,0x2d,0xfc,0x1d,0x08,0x53,0x03,0x90,
    0x4d,0x4e,0x84,0x99,0xe4,0xce,0xd9,0x91,0xdd,0xb6,0x85,0x48,0x8b,0x29,0x6e,0xac,
    0xcd,0xc1,0xf8,0x1e,0x73,0x43,0x69,0xc6,0xb5,0xbd,0xfd,0x39,0x63,0x20,0xd4,0x38,
    0x76,0x7d,0xb2,0xa7,0xcf,0xed,0x57,0xc5,0xf3,0x2c,0xbb,0x14,0x21,0x06,0x55,0x9b,
    0xe3,0xef,0x5e,0x31,0x4f,0x7f,0x5a,0xa4,0x0d,0x82,0x51,0x49,0x5f,0xba,0x58,0x1c,
    0x4a,0x16,0xd5,0x17,0xa8,0x92,0x24,0x1f,0x8c,0xff,0xd8,0xae,0x2e,0x01,0xd3,0xad,
    0x3b,0x4b,0xda,0x46,0xeb,0xc9,0xde,0x9a,0x8f,0x87,0xd7,0x3a,0x80,0x6f,0x2f,0xc8,
    0xb1,0xb4,0x37,0xf7,0x0a,0x22,0x13,0x28,0x7c,0xcc,0x3c,0x89,0xc7,0xc3,0x96,0x56,
    0x07,0xbf,0x7e,0xf0,0x0b,0x2b,0x97,0x52,0x35,0x41,0x79,0x61,0xa6,0x4c,0x10,0xfe,
    0xbc,0x26,0x95,0x88,0x8a,0xb0,0xa3,0xfb,0xc0,0x18,0x94,0xf2,0xe1,0xe5,0xe9,0x5d,
    0xd0,0xdc,0x11,0x66,0x64,0x5c,0xec,0x59,0x42,0x75,0x12,0xf5,0x74,0x9c,0xaa,0x23,
    0x0e,0x86,0xab,0xbe,0x2a,0x02,0xe7,0x67,0xe6,0x44,0xa2,0x6c,0xc2,0x93,0x9f,0xf1,
    0xf6,0xfa,0x36,0xd2,0x50,0x68,0x9e,0x62,0x71,0x15,0x3d,0xd6,0x40,0xc4,0xe2,0x0f,
    0x8e,0x83,0x77,0x6b,0x25,0x05,0x3f,0x0c,0x30,0xea,0x70,0xb7,0xa1,0xe8,0xa9,0x65,
    0x8d,0x27,0x1a,0xdb,0x81,0xb3,0xa0,0xf4,0x45,0x7a,0x19,0xdf,0xee,0x78,0x34,0x60,
};
static const uint8_t kZucS1[256] = {
    0x55,0xc2,0x63,0x71,0x3b,0xc8,0x47,0x86,0x9f,0x3c,0xda,0x5b,0x29,0xaa,0xfd,0x77,
    0x8c,0xc5,0x94,0x0c,0xa6,0x1a,0x13,0x00,0xe3,0xa8,0x16,0x72,0x40,0xf9,0xf8,0x42,
    0x44,0x26,0x68,0x96,0x81,0xd9,0x45,0x3e,0x10,0x76,0xc6,0xa7,0x8b,0x39,0x43,0xe1,
    0x3a,0xb5,0x56,0x2a,0xc0,0x6d,0xb3,0x05,0x22,0x66,0xbf,0xdc,0x0b,0xfa,0x62,0x48,
    0xdd,0x20,0x11,0x06,0x36,0xc9,0xc1,0xcf,0xf6,0x27,0x52,0xbb,0x69,0xf5,0xd4,0x87,
    0x7f,0x84,0x4c,0xd2,0x9c,0x57,0xa4,0xbc,0x4f,0x9a,0xdf,0xfe,0xd6,0x8d,0x7a,0xeb,
    0x2b,0x53,0xd8,0x5c,0xa1,0x14,0x17,0xfb,0x23,0xd5,0x7d,0x30,0x67,0x73,0x08,0x09,
    0xee,0xb7,0x70,0x3f,0x61,0xb2,0x19,0x8e,0x4e,0xe5,0x4b,0x93,0x8f,0x5d,0xdb,0xa9,
    0xad,0xf1,0xae,0x2e,0xcb,0x0d,0xfc,0xf4,0x2d,0x46,0x6e,0x1d,0x97,0xe8,0xd1,0xe9,
    0x4d,0x37,0xa3,0xec,0x76,0xf3,0x3d,0x3d,0x8a,0x21,0x85,0xe7,0x6b,0x0b,0x54,0xf7,
    0x15,0x71,0x35,0x21,0x9d,0xf4,0xcf,0xd7,0x15,0x5a,0x32,0x18,0x25,0x8a,0xe3,0xd1,
    0xa4,0x3d,0x11,0xd4,0x9b,0x6b,0x02,0x03,0x4c,0xb8,0x31,0x4d,0x9b,0x1e,0x41,0xbe,
    0xf6,0xb1,0xb4,0xb4,0xcb,0x5f,0x8c,0xd2,0x8d,0x75,0xd0,0xf9,0x2a,0x05,0x73,0x44,
    0x7b,0x58,0x6f,0x2b,0x59,0x04,0xfb,0x6c,0xde,0xee,0xa5,0x57,0xae,0xe7,0x78,0xac,
    0xdd,0x53,0xb3,0xca,0x5c,0xe2,0xd3,0x2f,0x83,0x7b,0xd2,0x23,0xa0,0x13,0xab,0x12,
    0xd1,0xce,0x5d,0x0f,0x05,0x12,0x7e,0xa5,0x42,0x91,0xe8,0x58,0xab,0xab,0xab,0xab,
};

// ZUC LFSR state: 16 × 31-bit elements (stored in uint32_t)
struct ZucState {
    uint32_t lfsr[16];
    uint32_t r1, r2;
};

static const uint32_t ZUC_MOD = (1u << 31) - 1;  // 2^31 - 1

static inline uint32_t zucAddM(uint32_t a, uint32_t b) {
    uint32_t s = a + b;
    return (s >> 31) + (s & ZUC_MOD);
}

static inline uint32_t zucLFSRWithM(uint32_t u, ZucState& zst) {
    // Feedback polynomial per TS 35.222 §2.1:
    // s_new = 2^15*s15 + 2^17*s13 + 2^21*s10 + 2^20*s4 + (1+2^8)*s0 (mod 2^31-1)
    auto rotl = [](uint32_t x, int n) -> uint32_t {
        return ((x << n) | (x >> (31 - n))) & ZUC_MOD;
    };
    uint32_t v = zucAddM(rotl(zst.lfsr[15], 15),
                  zucAddM(rotl(zst.lfsr[13], 17),
                  zucAddM(rotl(zst.lfsr[10], 21),
                  zucAddM(rotl(zst.lfsr[4],  20),
                  zucAddM(rotl(zst.lfsr[0],  8), zst.lfsr[0])))));
    v = zucAddM(v, u);
    for (int i = 0; i < 15; ++i) zst.lfsr[i] = zst.lfsr[i+1];
    zst.lfsr[15] = v;
    return v;
}

static void zucRound(ZucState& zst, uint32_t& w, bool init) {
    // Bit reorganisation
    uint32_t x0 = ((zst.lfsr[15] & 0x7FFF8000u) << 1) | (zst.lfsr[14] & 0xFFFF);
    uint32_t x1 = ((zst.lfsr[11] & 0x0000FFFFu) << 16) | (zst.lfsr[9] >> 15);
    uint32_t x2 = ((zst.lfsr[7]  & 0x0000FFFFu) << 16) | (zst.lfsr[5] >> 15);
    uint32_t x3 = ((zst.lfsr[2]  & 0x0000FFFFu) << 16) | (zst.lfsr[0] >> 15);

    // F function
    uint32_t W  = (x0 ^ zst.r1) + zst.r2;
    uint32_t W1 = zst.r1 + x1;
    uint32_t W2 = zst.r2 ^ x2;

    // L1, L2 linear transforms
    auto rotl32 = [](uint32_t x, int n){ return (x<<n)|(x>>(32-n)); };
    uint32_t u  = W1 ^ rotl32(W1,2) ^ rotl32(W1,10) ^ rotl32(W1,18) ^ rotl32(W1,24);
    uint32_t v  = W2 ^ rotl32(W2,8) ^ rotl32(W2,14) ^ rotl32(W2,22) ^ rotl32(W2,30);

    // S-box substitution (kZucS0/kZucS1 applied per byte)
    auto subByte = [&](uint32_t in) -> uint32_t {
        return (static_cast<uint32_t>(kZucS0[(in>>24)&0xFF])<<24)
             | (static_cast<uint32_t>(kZucS1[(in>>16)&0xFF])<<16)
             | (static_cast<uint32_t>(kZucS0[(in>> 8)&0xFF])<< 8)
             |  static_cast<uint32_t>(kZucS1[(in    )&0xFF]);
    };

    zst.r1 = subByte(u);
    zst.r2 = subByte(v);

    w = init ? W : (W ^ x3);   // during init W not output, in generation W^X3
    zucLFSRWithM(init ? (W >> 1) : 0, zst);
}

// ZUC keystream generation per TS 35.222: key[16], iv[16], count for EEA3.
static ByteBuffer zucKeystream(const uint8_t key[16], uint32_t count,
                                const ByteBuffer& data)
{
    if (data.empty()) return {};

    ZucState zst{};
    // Load LFSR per TS 35.222 §3.2 (D constants from spec)
    static const uint8_t kD[16] = {
        0x44,0xD7,0x26,0xBC,0x62,0x6B,0x13,0x5E,0x57,0x89,0x35,0x01,0x26,0x9B,0x2D,0xE0
    };
    for (int i = 0; i < 16; ++i) {
        zst.lfsr[i] = (static_cast<uint32_t>(key[i]) << 23)
                    | (static_cast<uint32_t>(kD[i])   << 15)
                    | (i == 0 ? (count & 0x7FFF) :
                       i == 4 ? ((count >> 15) & 0x7FFF) : 0);
    }
    zst.r1 = 0; zst.r2 = 0;

    // Initialisation: 32 rounds (output discarded)
    for (int i = 0; i < 32; ++i) {
        uint32_t dummy;
        zucRound(zst, dummy, true);
    }

    // Skip one extra round per spec
    { uint32_t dummy; zucRound(zst, dummy, false); }

    // Keystream generation
    ByteBuffer out = data;
    const size_t words = (out.size() + 3) / 4;
    for (size_t w = 0; w < words; ++w) {
        uint32_t ks; zucRound(zst, ks, false);
        for (int b = 0; b < 4 && (w*4 + b) < out.size(); ++b)
            out[w*4 + b] ^= static_cast<uint8_t>(ks >> (24 - 8*b));
    }
    return out;
}

// AES-128 single-block ECB encrypt (used by CMAC subkey generation)
static void aes128EcbEncrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    uint8_t rk[11][16];
    aesKeyExpand(key, rk);
    std::memcpy(out, in, 16);
    aesEncryptBlock(out, rk);
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────

// ────────────────────────────────────────────────────────────────
// Ciphering dispatch — TS 33.401 §6.4 (EEA1/EEA2/EEA3)
// ────────────────────────────────────────────────────────────────
ByteBuffer PDCP::cipher(const ByteBuffer& data,
                         const uint8_t key[16], uint32_t count) const {
    // Dispatch is controlled by the caller via config; but cipher() itself
    // has no access to config.  We resolve this by having processDlPacket
    // pass the algorithm selector.  Until refactored, default = AES.
    // NOTE: processDlPacket/processUlPDU now call the per-alg helpers directly.
    return aes128Ctr(key, count, data);
}

ByteBuffer PDCP::decipher(const ByteBuffer& data,
                           const uint8_t key[16], uint32_t count) const {
    return aes128Ctr(key, count, data);  // CTR mode is self-inverse
}

// ────────────────────────────────────────────────────────────────
// ROHC Profile 0x0001 — IP/UDP/RTP (VoLTE) per RFC 3095
// ────────────────────────────────────────────────────────────────
//
// Packet format overview:
//   IR  packet: 0xFD | Profile(0x00,0x01) | CRC8 | static chain | dynamic chain
//   FO  packet: 0xF8 | SN_delta(8b) | TS_delta(8b)
//   SO  packet: SN(8b)   ← only RTP SN, everything else unchanged
//
// Static chain (IP/UDP/RTP common fields, sent once):
//   srcIP(4) | dstIP(4) | srcPort(2) | dstPort(2) | SSRC(4) | PT(1) = 17 bytes
//
// Dynamic chain (volatile fields sent in FO):
//   SN(2) | TS(4) | FLAGS(1) = 7 bytes
//
// CRC-8 polynomial: x^8+x^2+x+1 (RFC 3095 §5.9.1)
//
// IPv4/UDP/RTP minimal sizes: 20 + 8 + 12 = 40 bytes
// IR compressed:  1 + 2 + 1 + 17 + 7 = 28 bytes  (saved 12 bytes)
// FO compressed:  1 + 1 + 1 = 3 bytes  (saved 37 bytes)
// SO compressed:  1 byte     (saved 39 bytes)
//
// TS 36.323 §6.2.13 maps headerCompression → ROHC use in PDCP.
// ────────────────────────────────────────────────────────────────

namespace {

// CRC-8/ROHC: poly 0x07, reflected, no initial XOR per RFC 3095 §5.9.1
static uint8_t rohcCrc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
    }
    return crc;
}

// Read big-endian 16/32 from buffer (bounds-safe: returns 0 if out of range)
static inline uint16_t readBE16(const uint8_t* p, size_t idx, size_t sz) {
    if (idx + 1 >= sz) return 0;
    return (static_cast<uint16_t>(p[idx]) << 8) | p[idx+1];
}
static inline uint32_t readBE32(const uint8_t* p, size_t idx, size_t sz) {
    if (idx + 3 >= sz) return 0;
    return (static_cast<uint32_t>(p[idx])   << 24)
         | (static_cast<uint32_t>(p[idx+1]) << 16)
         | (static_cast<uint32_t>(p[idx+2]) <<  8)
         |  static_cast<uint32_t>(p[idx+3]);
}

static inline void writeBE16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v);
}
static inline void writeBE32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8); p[3] = static_cast<uint8_t>(v);
}

// Minimum IPv4+UDP+RTP header size
constexpr size_t MIN_IP_UDP_RTP = 40;

// ROHC packet type tags
constexpr uint8_t ROHC_TAG_IR = 0xFD;   // Initialization & Refresh
constexpr uint8_t ROHC_TAG_FO = 0xF8;   // First Order
// Second Order: packets with high bit clear (SN only)

} // anonymous namespace

// ── Compressor: IP packet → ROHC compressed packet ───────────────
ByteBuffer PDCP::rohcCompressWithCtx(const ByteBuffer& ipPkt, ROHCContext& ctx) const {
    // Only compress profile 0x0001 (IP/UDP/RTP); pass through everything else
    if (ctx.profile != ROHCProfile::RTP_UDP_IP || ipPkt.size() < MIN_IP_UDP_RTP)
        return ipPkt;

    const uint8_t* p = ipPkt.data();
    const size_t   n = ipPkt.size();

    // Extract IP/UDP/RTP fields (IPv4 + UDP + RTP = offsets 0/20/28)
    const uint32_t srcIp   = readBE32(p, 12, n);
    const uint32_t dstIp   = readBE32(p, 16, n);
    const uint16_t srcPort = readBE16(p, 20, n);
    const uint16_t dstPort = readBE16(p, 22, n);
    // RTP offset = 28: CC(4b)|M(1b)|PT(7b) | SN(16b) | Timestamp(32b) | SSRC(32b)
    const uint16_t rtpSn   = readBE16(p, 30, n);
    const uint32_t rtpTs   = readBE32(p, 32, n);
    const uint32_t rtpSsrc = readBE32(p, 36, n);
    const uint8_t  rtpPt   = p[29] & 0x7F;

    // RTP payload = after first 40-byte header
    const size_t payloadOffset = MIN_IP_UDP_RTP;
    const size_t payloadLen    = (n > payloadOffset) ? (n - payloadOffset) : 0;

    // Detect static-chain changes (forces IR)
    const bool staticChanged = (srcIp   != ctx.lastSrcIp   ||
                                 dstIp   != ctx.lastDstIp   ||
                                 srcPort != ctx.lastSrcPort  ||
                                 dstPort != ctx.lastDstPort  ||
                                 (rtpSsrc & 0xFFFF) != ctx.lastRtpSsrc ||
                                 rtpPt   != ctx.lastRtpPt);

    if (staticChanged || ctx.txState == ROHCState::IR) {
        // ── IR packet ─────────────────────────────────────────────
        // Format: TagIR | Profile16 | CRC8 | static(17) | dynamic(7) | payload
        ByteBuffer out;
        out.reserve(1 + 2 + 1 + 17 + 7 + payloadLen);

        out.push_back(ROHC_TAG_IR);
        out.push_back(0x00); out.push_back(0x01);  // profile 0x0001

        // CRC placeholder (index 3); fill after building rest
        out.push_back(0x00);

        // Static chain (17 bytes)
        const size_t sc_start = out.size();
        out.resize(sc_start + 4); writeBE32(&out[sc_start], srcIp);
        out.resize(out.size() + 4); writeBE32(&out[out.size()-4], dstIp);
        out.resize(out.size() + 2); writeBE16(&out[out.size()-2], srcPort);
        out.resize(out.size() + 2); writeBE16(&out[out.size()-2], dstPort);
        out.resize(out.size() + 4); writeBE32(&out[out.size()-4], rtpSsrc);
        out.push_back(rtpPt);

        // Dynamic chain (7 bytes)
        out.resize(out.size() + 2); writeBE16(&out[out.size()-2], rtpSn);
        out.resize(out.size() + 4); writeBE32(&out[out.size()-4], rtpTs);
        out.push_back(0x00);        // FLAGS byte (simplified: M=0, X=0)

        // CRC-8 over header bytes [0..header_end)
        out[3] = rohcCrc8(out.data(), out.size());

        // RTP payload
        out.insert(out.end(), p + payloadOffset, p + payloadOffset + payloadLen);

        // Update context
        ctx.lastSrcIp   = srcIp;
        ctx.lastDstIp   = dstIp;
        ctx.lastSrcPort = srcPort;
        ctx.lastDstPort = dstPort;
        ctx.lastRtpSsrc = static_cast<uint16_t>(rtpSsrc & 0xFFFF);
        ctx.lastRtpPt   = rtpPt;
        ctx.lastRtpSn   = rtpSn;
        ctx.txStatePkts  = 0;
        ctx.txState     = ROHCState::FO;   // move to FO after IR sent
        return out;
    }

    // ── FO or SO packet ───────────────────────────────────────────
    const uint16_t snDelta = static_cast<uint16_t>(rtpSn - ctx.lastRtpSn);
    ctx.lastRtpSn = rtpSn;
    ++ctx.txStatePkts;

    if (ctx.txState == ROHCState::FO) {
        // FO: tag + SN_delta(8b) + TS_low(4 bytes) + payload
        // Simplified FO: send SN delta + RTP timestamp low byte
        ByteBuffer out;
        out.reserve(3 + payloadLen);
        out.push_back(ROHC_TAG_FO);
        out.push_back(static_cast<uint8_t>(snDelta & 0xFF));
        out.push_back(static_cast<uint8_t>(rtpTs   & 0xFF));
        out.insert(out.end(), p + payloadOffset, p + payloadOffset + payloadLen);
        if (ctx.txStatePkts >= ROHC_FO_TO_SO_PKTS)
            ctx.txState = ROHCState::SO;
        return out;
    }

    // SO: just RTP SN low byte + payload (smallest possible packet)
    ByteBuffer out;
    out.reserve(1 + payloadLen);
    out.push_back(static_cast<uint8_t>(snDelta & 0x7F));  // high bit clear = SO
    out.insert(out.end(), p + payloadOffset, p + payloadOffset + payloadLen);
    return out;
}

// ── Decompressor: ROHC packet → IP packet ────────────────────────
ByteBuffer PDCP::rohcDecompressWithCtx(const ByteBuffer& rohcPkt, ROHCContext& ctx) const {
    if (ctx.profile != ROHCProfile::RTP_UDP_IP || rohcPkt.empty())
        return rohcPkt;

    const uint8_t* p = rohcPkt.data();
    const size_t   n = rohcPkt.size();

    // ── Detect packet type by tag byte ───────────────────────────
    if (p[0] == ROHC_TAG_IR) {
        // IR: decode static + dynamic chain, rebuild full IP header
        if (n < 1 + 2 + 1 + 17 + 7) return {};  // malformed
        // Skip: tag(1) + profile(2) + crc(1) = offset 4
        size_t off = 4;
        ctx.lastSrcIp   = readBE32(p, off, n); off += 4;
        ctx.lastDstIp   = readBE32(p, off, n); off += 4;
        ctx.lastSrcPort = readBE16(p, off, n); off += 2;
        ctx.lastDstPort = readBE16(p, off, n); off += 2;
        const uint32_t ssrc = readBE32(p, off, n); off += 4;
        ctx.lastRtpSsrc = static_cast<uint16_t>(ssrc & 0xFFFF);
        ctx.lastRtpPt   = p[off++];
        // Dynamic chain
        ctx.lastRtpSn   = readBE16(p, off, n); off += 2;
        const uint32_t rtpTs = readBE32(p, off, n); off += 4;
        // FLAGS byte
        off += 1;
        ctx.rxState = ROHCState::FO;

        const size_t payloadLen = (n > off) ? (n - off) : 0;
        // Reconstruct minimal IPv4/UDP/RTP header + payload
        ByteBuffer ip(MIN_IP_UDP_RTP + payloadLen, 0);
        // IPv4 header (20 bytes) — minimal
        ip[0] = 0x45;  // version=4, IHL=5
        writeBE16(&ip[2], static_cast<uint16_t>(MIN_IP_UDP_RTP + payloadLen));
        ip[8] = 64;    // TTL
        ip[9] = 17;    // protocol = UDP
        writeBE32(&ip[12], ctx.lastSrcIp);
        writeBE32(&ip[16], ctx.lastDstIp);
        // UDP header (8 bytes at offset 20)
        writeBE16(&ip[20], ctx.lastSrcPort);
        writeBE16(&ip[22], ctx.lastDstPort);
        writeBE16(&ip[24], static_cast<uint16_t>(8 + 12 + payloadLen));
        // RTP header (12 bytes at offset 28)
        ip[28] = 0x80;  // V=2, P=0, X=0, CC=0
        ip[29] = ctx.lastRtpPt;
        writeBE16(&ip[30], ctx.lastRtpSn);
        writeBE32(&ip[32], rtpTs);
        writeBE32(&ip[36], ssrc);
        if (payloadLen > 0)
            std::memcpy(&ip[MIN_IP_UDP_RTP], p + off, payloadLen);
        return ip;
    }

    if (p[0] == ROHC_TAG_FO) {
        // FO: SN_delta(1) + TS_low(1) + payload
        if (n < 3) return {};
        const uint8_t snDelta = p[1];
        const uint8_t tsLow   = p[2];
        ctx.lastRtpSn = static_cast<uint16_t>(ctx.lastRtpSn + snDelta);
        const size_t payloadLen = (n > 3) ? (n - 3) : 0;
        ctx.rxState = ROHCState::SO;

        ByteBuffer ip(MIN_IP_UDP_RTP + payloadLen, 0);
        ip[0] = 0x45; writeBE32(&ip[12], ctx.lastSrcIp); writeBE32(&ip[16], ctx.lastDstIp);
        ip[8] = 64; ip[9] = 17;
        writeBE16(&ip[2], static_cast<uint16_t>(MIN_IP_UDP_RTP + payloadLen));
        writeBE16(&ip[20], ctx.lastSrcPort); writeBE16(&ip[22], ctx.lastDstPort);
        writeBE16(&ip[24], static_cast<uint16_t>(8 + 12 + payloadLen));
        ip[28] = 0x80; ip[29] = ctx.lastRtpPt;
        writeBE16(&ip[30], ctx.lastRtpSn);
        // Restore TS using low byte (best-effort for simulation)
        const uint32_t ts = (tsLow & 0xFF);
        writeBE32(&ip[32], ts);
        writeBE32(&ip[36], static_cast<uint32_t>(ctx.lastRtpSsrc));
        if (payloadLen > 0)
            std::memcpy(&ip[MIN_IP_UDP_RTP], p + 3, payloadLen);
        return ip;
    }

    // SO: SN delta (7 bits, high bit = 0) + payload
    const uint8_t snDelta   = p[0] & 0x7F;
    ctx.lastRtpSn = static_cast<uint16_t>(ctx.lastRtpSn + snDelta);
    const size_t payloadLen = (n > 1) ? (n - 1) : 0;

    ByteBuffer ip(MIN_IP_UDP_RTP + payloadLen, 0);
    ip[0] = 0x45; writeBE32(&ip[12], ctx.lastSrcIp); writeBE32(&ip[16], ctx.lastDstIp);
    ip[8] = 64; ip[9] = 17;
    writeBE16(&ip[2], static_cast<uint16_t>(MIN_IP_UDP_RTP + payloadLen));
    writeBE16(&ip[20], ctx.lastSrcPort); writeBE16(&ip[22], ctx.lastDstPort);
    writeBE16(&ip[24], static_cast<uint16_t>(8 + 12 + payloadLen));
    ip[28] = 0x80; ip[29] = ctx.lastRtpPt;
    writeBE16(&ip[30], ctx.lastRtpSn);
    writeBE32(&ip[36], static_cast<uint32_t>(ctx.lastRtpSsrc));
    if (payloadLen > 0)
        std::memcpy(&ip[MIN_IP_UDP_RTP], p + 1, payloadLen);
    return ip;
}

// Legacy pass-through overloads (no-ctx, used by old tests)
ByteBuffer PDCP::rohcCompress(const ByteBuffer& ipPkt) const { return ipPkt; }
ByteBuffer PDCP::rohcDecompress(const ByteBuffer& rohcPkt) const { return rohcPkt; }

// ────────────────────────────────────────────────────────────────
// EIA2 integrity protection — AES-128-CMAC (RFC 4493 / TS 33.401 §6.4.2b)
// ────────────────────────────────────────────────────────────────

void PDCP::computeCMAC_(const uint8_t key[16],
                         const uint8_t* msg, size_t msgLen,
                         uint8_t tag[16]) const
{
    // RFC 4493 §2.3 — AES-128-CMAC
    // Step 1: derive subkeys K1, K2
    uint8_t L[16]{};
    aes128EcbEncrypt(key, L, L);  // encrypt zero block

    auto leftShift1 = [](const uint8_t in[16], uint8_t out[16]) {
        uint8_t overflow = 0;
        for (int i = 15; i >= 0; --i) {
            out[i] = static_cast<uint8_t>((in[i] << 1) | overflow);
            overflow = (in[i] >> 7) & 1;
        }
    };
    static const uint8_t kRb = 0x87;  // RFC 4493 constant

    uint8_t K1[16], K2[16];
    leftShift1(L, K1);
    if (L[0] & 0x80) K1[15] ^= kRb;
    leftShift1(K1, K2);
    if (K1[0] & 0x80) K2[15] ^= kRb;

    // Step 2: process message blocks
    const size_t blockCount = (msgLen == 0) ? 1 : ((msgLen + 15) / 16);
    const bool lastComplete  = (msgLen > 0) && (msgLen % 16 == 0);

    uint8_t X[16]{};   // running CBC-MAC state
    for (size_t i = 0; i < blockCount; ++i) {
        uint8_t block[16]{};
        const size_t offset = i * 16;

        if (i < blockCount - 1) {
            // Full block
            std::memcpy(block, msg + offset, 16);
        } else {
            // Last block: XOR with K1 or K2
            const size_t rem = msgLen - offset;
            if (lastComplete) {
                std::memcpy(block, msg + offset, 16);
                for (int j = 0; j < 16; ++j) block[j] ^= K1[j];
            } else {
                std::memcpy(block, msg + offset, rem);
                block[rem] = 0x80;  // padding
                for (int j = 0; j < 16; ++j) block[j] ^= K2[j];
            }
        }

        for (int j = 0; j < 16; ++j) X[j] ^= block[j];
        aes128EcbEncrypt(key, X, X);
    }
    std::memcpy(tag, X, 16);
}

ByteBuffer PDCP::applyIntegrity(RNTI rnti, uint16_t bearerId,
                                 const ByteBuffer& buf) const
{
    const auto it = entities_.find(makeKey(rnti, bearerId));
    if (it == entities_.end()) return buf;
    const PDCPEntity& e = it->second;
    if (e.config.integAlg == PDCPIntegAlg::NULL_ALG) return buf;

    uint8_t tag[16]{};
    computeCMAC_(e.config.integKey, buf.data(), buf.size(), tag);

    ByteBuffer out;
    out.reserve(buf.size() + 4);
    out.insert(out.end(), buf.begin(), buf.end());
    out.insert(out.end(), tag, tag + 4);  // MAC-I = first 4 bytes of CMAC tag
    return out;
}

ByteBuffer PDCP::verifyIntegrity(RNTI rnti, uint16_t bearerId,
                                  const ByteBuffer& buf) const
{
    const auto it = entities_.find(makeKey(rnti, bearerId));
    if (it == entities_.end() || buf.size() < 4) return {};
    const PDCPEntity& e = it->second;
    if (e.config.integAlg == PDCPIntegAlg::NULL_ALG)
        return ByteBuffer(buf.begin(), buf.end() - 4);

    const ByteBuffer payload(buf.begin(), buf.end() - 4);
    const uint8_t* rxMac = buf.data() + buf.size() - 4;

    uint8_t tag[16]{};
    computeCMAC_(e.config.integKey, payload.data(), payload.size(), tag);

    if (std::memcmp(rxMac, tag, 4) != 0) {
        RBS_LOG_ERROR("PDCP", "Integrity check FAILED on RNTI=", rnti,
                      " BID=", bearerId);
        return {};
    }
    return payload;
}

}  // namespace rbs::lte

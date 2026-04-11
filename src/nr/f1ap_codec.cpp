// F1AP codec — simulation APER encoding for F1 Setup procedure
// TS 38.473 §8.7.1 (F1 Setup), §9.3 (IE definitions)
//
// Binary layout (all integers big-endian):
//
//  F1 Setup Request PDU:
//    [0..1]  magic  = 0x38 0x47
//    [2]     proc   = 0x01  (F1Setup procedure code)
//    [3]     pduType= 0x00  (initiatingMessage)
//    [4..5]  transactionId  (uint16)
//    [6..13] gnbDuId        (uint64, only 36 LSBs used)
//    [14]    nameLen        (uint8, 0 if absent)
//    [15..14+nameLen] gnbDuName
//    [15+nameLen]     numCells (uint8, 1-8)
//    per cell:
//      [8B] nrCellIdentity (uint64, 36-bit NCI)
//      [4B] nrArfcn        (uint32)
//      [1B] scs            (NRScs enum)
//      [2B] pci            (uint16)
//      [2B] tac            (uint16)
//
//  F1 Setup Response PDU:
//    [0..1]  magic  = 0x38 0x47
//    [2]     proc   = 0x01
//    [3]     pduType= 0x01  (successfulOutcome)
//    [4..5]  transactionId
//    [6]     nameLen
//    [7..6+nameLen] gnbCuName
//    [7+nameLen]    numActivated (uint8)
//    per activated cell: [8B] nrCellIdentity
//
//  F1 Setup Failure PDU:
//    [0..1]  magic  = 0x38 0x47
//    [2]     proc   = 0x01
//    [3]     pduType= 0x02  (unsuccessfulOutcome)
//    [4..5]  transactionId
//    [6]     causeType
//    [7]     causeValue

#include "f1ap_codec.h"
#include <cstring>

namespace rbs::nr {

// ── Encoding helpers ─────────────────────────────────────────────
static void pushU8 (ByteBuffer& b, uint8_t  v) { b.push_back(v); }
static void pushU16(ByteBuffer& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
static void pushU32(ByteBuffer& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >>  8));
    b.push_back(static_cast<uint8_t>(v));
}
static void pushU64(ByteBuffer& b, uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8)
        b.push_back(static_cast<uint8_t>(v >> shift));
}
static void pushStr(ByteBuffer& b, const std::string& s) {
    const uint8_t len = static_cast<uint8_t>(s.size() > 255 ? 255 : s.size());
    pushU8(b, len);
    for (uint8_t i = 0; i < len; ++i)
        b.push_back(static_cast<uint8_t>(s[i]));
}

// ── Decoding helpers ─────────────────────────────────────────────
struct Reader {
    const ByteBuffer& buf;
    size_t pos = 0;
    bool ok = true;

    uint8_t  u8()  { if (pos + 1 > buf.size()) { ok=false; return 0; } return buf[pos++]; }
    uint16_t u16() {
        if (pos + 2 > buf.size()) { ok=false; return 0; }
        uint16_t v = static_cast<uint16_t>(buf[pos] << 8) | buf[pos+1];
        pos += 2; return v;
    }
    uint32_t u32() {
        if (pos + 4 > buf.size()) { ok=false; return 0; }
        uint32_t v = (static_cast<uint32_t>(buf[pos])   << 24)
                   | (static_cast<uint32_t>(buf[pos+1]) << 16)
                   | (static_cast<uint32_t>(buf[pos+2]) <<  8)
                   |  static_cast<uint32_t>(buf[pos+3]);
        pos += 4; return v;
    }
    uint64_t u64() {
        if (pos + 8 > buf.size()) { ok=false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | buf[pos++];
        return v;
    }
    std::string str() {
        const uint8_t len = u8();
        if (pos + len > buf.size()) { ok=false; return {}; }
        std::string s(reinterpret_cast<const char*>(&buf[pos]), len);
        pos += len; return s;
    }
};

// ── Magic / procedure header ─────────────────────────────────────
static const uint8_t F1AP_MAGIC0 = 0x38;
static const uint8_t F1AP_MAGIC1 = 0x47;
static const uint8_t F1AP_PROC_SETUP = 0x01;
static const uint8_t F1AP_PDU_INIT  = 0x00;
static const uint8_t F1AP_PDU_SUCC  = 0x01;
static const uint8_t F1AP_PDU_FAIL  = 0x02;

static void pushHeader(ByteBuffer& b, uint8_t pduType) {
    pushU8(b, F1AP_MAGIC0);
    pushU8(b, F1AP_MAGIC1);
    pushU8(b, F1AP_PROC_SETUP);
    pushU8(b, pduType);
}
static bool checkHeader(Reader& r, uint8_t expectedPduType) {
    if (r.u8() != F1AP_MAGIC0) return false;
    if (r.u8() != F1AP_MAGIC1) return false;
    if (r.u8() != F1AP_PROC_SETUP) return false;
    if (r.u8() != expectedPduType) return false;
    return r.ok;
}

// ────────────────────────────────────────────────────────────────
ByteBuffer encodeF1SetupRequest(const F1SetupRequest& req) {
    ByteBuffer b;
    pushHeader(b, F1AP_PDU_INIT);
    pushU16(b, req.transactionId);
    pushU64(b, req.gnbDuId & 0xFFFFFFFFFULL);  // 36-bit mask
    pushStr(b, req.gnbDuName);
    const uint8_t nc = static_cast<uint8_t>(
        req.servedCells.size() > 8 ? 8 : req.servedCells.size());
    pushU8(b, nc);
    for (uint8_t i = 0; i < nc; ++i) {
        const auto& c = req.servedCells[i];
        pushU64(b, c.nrCellIdentity & 0xFFFFFFFFFULL);
        pushU32(b, c.nrArfcn);
        pushU8 (b, static_cast<uint8_t>(c.scs));
        pushU16(b, c.pci);
        pushU16(b, c.tac);
    }
    return b;
}

bool decodeF1SetupRequest(const ByteBuffer& pdu, F1SetupRequest& out) {
    Reader r{pdu};
    if (!checkHeader(r, F1AP_PDU_INIT)) return false;
    out.transactionId = r.u16();
    out.gnbDuId       = r.u64();
    out.gnbDuName     = r.str();
    const uint8_t nc  = r.u8();
    out.servedCells.clear();
    for (uint8_t i = 0; i < nc && r.ok; ++i) {
        F1ServedCell c;
        c.nrCellIdentity = r.u64();
        c.nrArfcn        = r.u32();
        c.scs            = static_cast<NRScs>(r.u8());
        c.pci            = r.u16();
        c.tac            = r.u16();
        out.servedCells.push_back(c);
    }
    return r.ok;
}

// ────────────────────────────────────────────────────────────────
ByteBuffer encodeF1SetupResponse(const F1SetupResponse& rsp) {
    ByteBuffer b;
    pushHeader(b, F1AP_PDU_SUCC);
    pushU16(b, rsp.transactionId);
    pushStr(b, rsp.gnbCuName);
    const uint8_t na = static_cast<uint8_t>(
        rsp.activatedCells.size() > 255 ? 255 : rsp.activatedCells.size());
    pushU8(b, na);
    for (uint8_t i = 0; i < na; ++i)
        pushU64(b, rsp.activatedCells[i] & 0xFFFFFFFFFULL);
    return b;
}

bool decodeF1SetupResponse(const ByteBuffer& pdu, F1SetupResponse& out) {
    Reader r{pdu};
    if (!checkHeader(r, F1AP_PDU_SUCC)) return false;
    out.transactionId = r.u16();
    out.gnbCuName     = r.str();
    const uint8_t na  = r.u8();
    out.activatedCells.clear();
    for (uint8_t i = 0; i < na && r.ok; ++i)
        out.activatedCells.push_back(r.u64());
    return r.ok;
}

// ────────────────────────────────────────────────────────────────
ByteBuffer encodeF1SetupFailure(const F1SetupFailure& fail) {
    ByteBuffer b;
    pushHeader(b, F1AP_PDU_FAIL);
    pushU16(b, fail.transactionId);
    pushU8 (b, fail.causeType);
    pushU8 (b, fail.causeValue);
    return b;
}

bool decodeF1SetupFailure(const ByteBuffer& pdu, F1SetupFailure& out) {
    Reader r{pdu};
    if (!checkHeader(r, F1AP_PDU_FAIL)) return false;
    out.transactionId = r.u16();
    out.causeType     = r.u8();
    out.causeValue    = r.u8();
    return r.ok;
}

}  // namespace rbs::nr

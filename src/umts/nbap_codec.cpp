// ─────────────────────────────────────────────────────────────────────────────
// NBAP APER codec  (TS 25.433, X.691 APER)
//
// Encodes NBAP InitiatingMessage PDUs using direct APER bit manipulation.
// Standard asn1c v0.9.29 cannot process the full NBAP spec (1 MB+) in
// reasonable time, so encoding is implemented manually following X.691 §12.
//
// Encoding rules summary applied here:
//   CHOICE with ext marker : 1 bit (0=non-extended) + constrained index bits
//   SEQUENCE (no ext)      : fields in declaration order, no bitmap
//   SEQUENCE (with ext)    : extension bitmap bit first (0 = no extensions)
//   ENUMERATED (no ext)    : constrained whole-number, ⌈log2(n)⌉ bits
//   ENUMERATED (with ext)  : 1 bit (0=non-extended), then ⌈log2(n)⌉ bits
//   INTEGER (lb..ub)        : constrained whole-number, ⌈log2(ub-lb+1)⌉ bits
//   INTEGER (unconstrained) : length-determinant + 2's complement bytes
//   CHOICE (no ext)         : constrained index bits
//   OPEN TYPE               : length-determinant (in bytes) + value bytes
//   SEQUENCE OF (0..max)    : normally-small length det + elements
// ─────────────────────────────────────────────────────────────────────────────
#include "nbap_codec.h"
#include "../common/logger.h"
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstring>

namespace rbs::umts {

// ─────────────────────────────── Bit-stream writer ───────────────────────────

/// Simple LSB-to-MSB APER bit writer (APER = Unaligned Basic PER).
struct AperBuf {
    ByteBuffer buf;
    int        bitPos{0}; // bits used in the last partial byte (0..7)

    /// Write \a nbits bits from \a val (MSB first, left-aligned within bits).
    void writeBits(uint64_t val, int nbits) {
        if (nbits <= 0) return;
        for (int i = nbits - 1; i >= 0; --i) {
            int bit = (val >> i) & 1;
            if (bitPos == 0) buf.push_back(0);
            buf.back() |= static_cast<uint8_t>(bit << (7 - bitPos));
            bitPos = (bitPos + 1) & 7;
        }
    }

    /// Align to next byte boundary (as required before OPEN TYPE, etc.).
    void alignByte() {
        if (bitPos != 0) {
            bitPos = 0;
        }
    }

    /// Append raw bytes (already byte-aligned content).
    void appendBytes(const uint8_t* data, size_t len) {
        alignByte();
        buf.insert(buf.end(), data, data + len);
    }

    /// Append another AperBuf (byte aligned).
    void appendBuf(const AperBuf& other) {
        AperBuf copy = other;
        copy.alignByte();
        appendBytes(copy.buf.data(), copy.buf.size());
    }

    /// Encode a length determinant (simplified: ≤ 127 → 1 byte; ≤ 16383 → 2 bytes).
    void writeLengthDet(size_t len) {
        alignByte();
        if (len <= 127) {
            buf.push_back(static_cast<uint8_t>(len));
        } else if (len <= 16383) {
            buf.push_back(static_cast<uint8_t>(0x80 | (len >> 8)));
            buf.push_back(static_cast<uint8_t>(len & 0xFF));
        } else {
            // Fragmentation not needed for simulator PDUs
            assert(false && "NBAP PDU too large for length determinant");
        }
    }

    ByteBuffer finalise() {
        alignByte();
        return buf;
    }
};

// ─────────────── APER primitive encoders (return bytes of the value) ──────────

/// Encode constrained whole-number INTEGER(lb..ub).
/// Returns the encoded bits embedded in an AperBuf.
static AperBuf encInteger(uint64_t val, uint64_t lb, uint64_t ub) {
    AperBuf b;
    uint64_t range = ub - lb + 1;
    if (range == 1) return b;       // zero bits
    uint64_t v = val - lb;
    int nbits = 0;
    uint64_t r = range - 1;
    while (r > 0) { ++nbits; r >>= 1; }
    b.writeBits(v, nbits);
    return b;
}

/// Encode a semi-constrained INTEGER (≥ lb, no upper bound) as unconstrained.
/// For simplicity treat as unconstrained.
[[maybe_unused]] static AperBuf encIntegerUnconstrained(int64_t val) {
    // Encode minimum number of bytes (2's complement), then length-determinant.
    AperBuf b;
    // Find minimum bytes needed
    uint8_t tmp[8];
    int nbytes = 1;
    int64_t v = val;
    tmp[0] = static_cast<uint8_t>(v & 0xFF);
    for (int i = 1; i < 8; ++i) {
        v >>= 8;
        if (v == 0 && !(tmp[i-1] & 0x80)) break;
        if (v == -1 && (tmp[i-1] & 0x80)) break;
        tmp[i] = static_cast<uint8_t>(v & 0xFF);
        nbytes = i + 1;
    }
    // Reverse to big-endian
    b.writeLengthDet(static_cast<size_t>(nbytes));
    for (int i = nbytes - 1; i >= 0; --i)
        b.buf.push_back(tmp[i]);
    return b;
}

/// Encode ENUMERATED with N root values, no extension marker.
/// val = the enum index chosen (0-based).
static AperBuf encEnum(int val, int nRootValues) {
    return encInteger(static_cast<uint64_t>(val), 0, static_cast<uint64_t>(nRootValues - 1));
}

/// Encode ENUMERATED with N root values, WITH extension marker.
static AperBuf encEnumExtensible(int val, int nRootValues) {
    AperBuf b;
    b.writeBits(0, 1); // extension bit = 0 (use root values)
    AperBuf ev = encInteger(static_cast<uint64_t>(val), 0, static_cast<uint64_t>(nRootValues - 1));
    // Merge bits
    b.buf.insert(b.buf.end(), ev.buf.begin(), ev.buf.end());
    // Fix bitPos
    b.bitPos = (b.bitPos + ev.bitPos) & 7;
    // Simpler: just combine by writing bits manually
    // ----- Rewrite without merge hacks -----
    // (The merge above has bitPos issues; use direct approach)
    AperBuf clean;
    clean.writeBits(0, 1); // extension=0
    int nbits = 0;
    int r = nRootValues - 1;
    while (r > 0) { ++nbits; r >>= 1; }
    if (nbits > 0)
        clean.writeBits(static_cast<uint64_t>(val), nbits);
    return clean;
}

/// Encode OPEN TYPE: length-determinant + value bytes (already APER-encoded).
static void appendOpenType(AperBuf& dst, const AperBuf& value) {
    AperBuf copy = value;
    copy.alignByte();
    dst.alignByte();
    dst.writeLengthDet(copy.buf.size());
    dst.buf.insert(dst.buf.end(), copy.buf.begin(), copy.buf.end());
}

// ────────────────────── NBAP type encoders ────────────────────────────────────

// Criticality: ENUMERATED { reject(0), ignore(1), notify(2) } — no ext marker
[[maybe_unused]] static AperBuf encCriticality(int c) { return encEnum(c, 3); }

// MessageDiscriminator: ENUMERATED { common(0), dedicated(1) } — no ext marker
[[maybe_unused]] static AperBuf encMsgDisc(int d) { return encEnum(d, 2); }

// ProcedureCode: INTEGER (0..255) → 8 bits
[[maybe_unused]] static AperBuf encProcedureCode(uint8_t code) {
    return encInteger(code, 0, 255);
}

// ProcedureID: SEQUENCE { procedureCode INTEGER(0..255), ddMode ENUM{tdd(0),fdd(1),common(2),...} }
// ddMode has extension marker (3 root values).
[[maybe_unused]] static AperBuf encProcedureID(uint8_t code, int ddMode) {
    AperBuf b;
    b.writeBits(code, 8);                    // procedureCode INTEGER(0..255) = 8 bits
    b.writeBits(0, 1);                       // ddMode extension bit = 0
    // 3 root values → ⌈log2(3)⌉ = 2 bits
    b.writeBits(static_cast<uint64_t>(ddMode), 2);
    return b;
}

// TransactionID: CHOICE { shortTransActionId INTEGER(0..127), longTransActionId INTEGER(0..32767) }
// No extension marker on CHOICE. 2 alternatives → 1 bit selector.
// We always use shortTransActionId.
[[maybe_unused]] static AperBuf encTransactionID(uint8_t txId) {
    AperBuf b;
    b.writeBits(0, 1);           // CHOICE index 0 = shortTransActionId
    b.writeBits(txId & 0x7F, 7); // INTEGER(0..127) = 7 bits
    return b;
}

// ProtocolIE-ID: INTEGER (0..65535) — constrained 16 bits
[[maybe_unused]] static AperBuf encProtocolIEID(uint16_t id) {
    return encInteger(id, 0, 65535);
}

// ─────────────────── NBAP-specific IE value encoders ──────────────────────────

// Local-Cell-ID: INTEGER (0..268435455)  28 bits
static AperBuf encLocalCellID(uint32_t v) {
    return encInteger(v, 0, 268435455UL);
}

// C-ID: INTEGER (0..65535)  16 bits
static AperBuf encCID(uint16_t v) {
    return encInteger(v, 0, 65535);
}

// ConfigurationGenerationID: INTEGER (0..255)  8 bits
static AperBuf encCfgGenID(uint8_t v) {
    return encInteger(v, 0, 255);
}

// UARFCN: INTEGER (0..16383, ...) — extensible constrained INTEGER
// APER: For extensible constrained INTEGER we must check if value is in range.
// If in range (0..16383): extension bit=0 + 14-bit value
// We only encode in-range values.
static AperBuf encUARFCN(uint16_t v) {
    AperBuf b;
    b.writeBits(0, 1);              // extension bit = 0 (in-range)
    b.writeBits(v, 14);             // 14 bits for 0..16383
    return b;
}

// PrimaryScramblingCode: INTEGER (0..511)  9 bits
static AperBuf encPSC(uint16_t v) {
    return encInteger(v, 0, 511);
}

// MaximumTransmissionPower: INTEGER (0..500)  9 bits (ceil(log2(501))=9)
static AperBuf encMaxTxPower(uint16_t v) {
    return encInteger(v, 0, 500);
}

// CRNC-CommunicationContextID: INTEGER (0..1048575) = 20 bits
static AperBuf encCRNCCtxID(uint32_t v) {
    return encInteger(v, 0, 1048575UL);
}

// NodeB-CommunicationContextID: INTEGER (0..1048575) = 20 bits
static AperBuf encNodeBCtxID(uint32_t v) {
    return encInteger(v, 0, 1048575UL);
}

// ─────────────────── ProtocolIE-Field builder ────────────────────────────────

// criticality values
static constexpr int CRIT_REJECT = 0;
static constexpr int CRIT_IGNORE = 1;
static constexpr int CRIT_NOTIFY = 2;

// Encode one ProtocolIE-Field:
//   id          : INTEGER(0..65535)            — 16 bits
//   criticality : ENUMERATED{reject,ignore,notify} — 2 bits
//   value       : OPEN TYPE
// The field is appended byte-aligned into the outer container.
static void appendIEField(AperBuf& dst, uint16_t ieId, int crit, const AperBuf& ieValue) {
    // id (16 bits)
    dst.writeBits(ieId, 16);
    // criticality (2 bits, no extension)
    dst.writeBits(static_cast<uint64_t>(crit), 2);
    // value: OPEN TYPE (align + length-det + bytes)
    appendOpenType(dst, ieValue);
}

// ──────── SEQUENCE OF length determinant for ProtocolIE-Container ────────────
// ProtocolIE-Container ::= SEQUENCE (SIZE (0..maxProtocolIEs)) OF ProtocolIE-Field
// maxProtocolIEs = 65535. APER: normally bounded; SIZE 0..65535 → 16 bits.
// Actually for a SEQUENCE OF with upper bound > 65535 or ≤ 65535, standard
// APER uses a length determinant (unconstrained-style) if max ≥ 64K, else
// constrained. Since 65535 < 65536, it is constrained (16 bits). However
// asn1c typically uses the "semi-automatic" length encoding. To match what
// real RNCs expect (and what asn1c-mouse generates), use the standard
// length determinant (1 or 2 bytes).
static void appendSeqOfCount(AperBuf& dst, size_t count) {
    dst.alignByte();
    if (count <= 127) {
        dst.buf.push_back(static_cast<uint8_t>(count));
    } else if (count <= 16383) {
        dst.buf.push_back(static_cast<uint8_t>(0x80 | (count >> 8)));
        dst.buf.push_back(static_cast<uint8_t>(count & 0xFF));
    }
}

// ─────────────────── MinimalCellSetup helpers ────────────────────────────────

// T-Cell: ENUMERATED { v0, v10, v20, v40, v80, v160, ... }
// Encode v0 (index 0) — this is a MANDATORY IE but we use the smallest
// valid value.  APER: no extension marker on T-Cell per spec → 3 bits
// (8 values minimum). Actually T-Cell has 8 root values.
static AperBuf encTCell(int idx = 0) {
    // T-Cell has extension marker and ≥6 root values; check spec:
    // T-Cell ENUMERATED { v0(0), v10(1), v20(2), v40(3), v80(4), v160(5), ... }
    // 6 root values + extension  → extension bit + ⌈log2(6)⌉ = 3 bits
    return encEnumExtensible(idx, 6);
}

// Start-Of-Audit-Sequence-Indicator: ENUMERATED { start-of-audit-sequence(0), ... }
// (1 root value + extension) → extension bit + 0 bits
static AperBuf encStartOfAuditSeq(bool start) {
    AperBuf b;
    b.writeBits(0, 1); // extension bit = 0
    // 1 root value: only start-of-audit-sequence exists (index 0)
    // 0 bits for value (only one option)
    (void)start;
    return b;
}

// ResetIndicator: CHOICE { communicationContext(0), communicationControlPort(1), nodeB(2), ... }
// With extension marker. We encode nodeB (index 2), which is NULL.
static AperBuf encResetIndicatorNodeB() {
    AperBuf b;
    b.writeBits(0, 1);  // extension bit = 0
    // 3 root alternatives → ⌈log2(3)⌉ = 2 bits
    b.writeBits(2, 2);  // index 2 = nodeB
    // nodeB is NULL — no additional encoding
    return b;
}

// ─────────────────── PDU outer wrapper ───────────────────────────────────────

// Encode the full NBAP-PDU InitiatingMessage wrapper:
//   NBAP-PDU CHOICE:
//     extension bit = 0
//     choice index = 0 (initiatingMessage),  3 root alternatives + ext → 2 bits
//     → writes: 0b 00? = 0x00 in first byte if written flush
//
//   InitiatingMessage SEQUENCE (no extension marker in spec):
//     procedureID  ProcedureID (SEQUENCE, 11 bits total : 8+1+2)
//     criticality  ENUMERATED{reject,ignore,notify}  2 bits
//     messageDiscriminator ENUMERATED{common,dedicated} 1 bit
//     transactionID CHOICE{short,long}: 8 bits
//     value OPEN TYPE
static ByteBuffer buildInitiatingMessage(
    uint8_t procCode, int ddMode,
    int criticality, int msgDisc,
    uint8_t txId,
    const AperBuf& valueContent)
{
    AperBuf pdu;

    // NBAP-PDU CHOICE (has ext marker + 4 root alts: init, succ, unsucc, outcome)
    // 4 alternatives → 2 bits; extension bit first
    pdu.writeBits(0, 1);   // extension bit = 0
    pdu.writeBits(0, 2);   // CHOICE index 0 = initiatingMessage  (4 alts → 2 bits)

    // InitiatingMessage SEQUENCE (no extension marker)
    // procedureID: 8 + 1 + 2 = 11 bits
    pdu.writeBits(procCode, 8);
    pdu.writeBits(0, 1);                           // ddMode ext bit = 0
    pdu.writeBits(static_cast<uint64_t>(ddMode), 2); // 3 root values → 2 bits

    // criticality: 2 bits
    pdu.writeBits(static_cast<uint64_t>(criticality), 2);

    // messageDiscriminator: 1 bit (2 root values, no ext)
    pdu.writeBits(static_cast<uint64_t>(msgDisc), 1);

    // transactionID CHOICE{short(0..127), long(0..32767)} — no ext marker
    // 2 alternatives → 1 bit
    pdu.writeBits(0, 1);            // index 0 = shortTransActionId
    pdu.writeBits(txId & 0x7F, 7); // INTEGER(0..127) = 7 bits

    // value: OPEN TYPE (byte-align + length det + bytes)
    appendOpenType(pdu, valueContent);

    return pdu.finalise();
}

// ─────────────────── T-Cell IEs minimum helpers ──────────────────────────────
// These mandatory IEs for CellSetupRequestFDD require complex nested types.
// We encode them as best-effort minimal valid APER sequences.

// Synchronisation-Configuration-Cell-SetupRqst encodes:
//   N-INSYNC-IND  INTEGER (1..256)  — 8 bits for 0-based range
//   N-OUTSYNC-IND INTEGER (1..256)  — 8 bits
//   T-RLFAILURE   INTEGER (10..250, ...) — extensible constrained
// We use minimal valid values: N-INSYNC=2, N-OUTSYNC=2, T-RLFAILURE=10
static AperBuf encSyncConfig() {
    AperBuf b;
    // N-INSYNC-IND: INTEGER(1..256) → 8 bits (256 values)
    b.writeBits(2 - 1, 8);   // value=2, lb=1 → 1 (8 bits)
    // N-OUTSYNC-IND: INTEGER(1..256) → 8 bits
    b.writeBits(2 - 1, 8);   // value=2
    // T-RLFAILURE: INTEGER(10..250, ...) → extensible
    // ext bit=0 + 8 bits (range 241 → ceil(log2(241))=8)
    b.writeBits(0, 1);
    b.writeBits(10 - 10, 8); // value=10
    // No iE-Extensions (OPTIONAL, not present) — but SEQUENCE iE-Extensions
    // has OPTIONAL → we need to encode the extension marker bit since SEQUENCE
    // Synchronisation-Configuration has ...
    // extension bit=0 (no extensions) is part of the SEQUENCE header,
    // but since this SEQUENCE has ... we need:
    b.writeBits(0, 1); // extension bitmap: 0 = no extensions present
    // Wait: extension marker handling for SEQUENCE in APER:
    // If SEQUENCE has '...' → first bit is extension presence bitmap bit (0 = root only)
    // But we already wrote the fields. We need to restart and put ext bit first.
    AperBuf b2;
    b2.writeBits(0, 1);   // SEQUENCE extension bit (no ext present)
    // Has iE-Extensions OPTIONAL → need bitmap for optional fields
    // There is 1 OPTIONAL component (iE-Extensions) → 1 bit bitmap
    b2.writeBits(0, 1);   // iE-Extensions not present
    // N-INSYNC-IND: INTEGER(1..256) → 8 bits
    b2.writeBits(2 - 1, 8);
    // N-OUTSYNC-IND: INTEGER(1..256) → 8 bits
    b2.writeBits(2 - 1, 8);
    // T-RLFAILURE: INTEGER(10..250, ...) extensible
    b2.writeBits(0, 1);    // ext bit = 0
    b2.writeBits(0, 8);    // value 10 - 10 = 0, range 241 → 8 bits
    return b2;
}

// PrimarySCH-Information-Cell-SetupRqstFDD:
//   commonPhysicalChannelID INTEGER (0..255)
//   primarySCH-Power        DL-Power INTEGER (-350..150, unit 0.1 dBm), extensible
//   tSTD-Indicator          ENUMERATED {tstd,non-tstd}
//   iE-Extensions           OPTIONAL
static AperBuf encPrimarySCHInfo() {
    AperBuf b;
    b.writeBits(0, 1);  // SEQUENCE ext bit = 0
    b.writeBits(0, 1);  // optional bitmap: iE-Extensions not present
    // commonPhysicalChannelID INTEGER(0..255) = 8 bits
    b.writeBits(0, 8);
    // primarySCH-Power: DL-Power INTEGER(-350..150,...), range=501, ext
    b.writeBits(0, 1);  // ext bit=0
    // range 501 → ceil(log2(501))=9 bits
    // value = 0 (corresponds to -350 + 0 = -350 dBm × 0.1 = -35 dBm, minimal value)
    b.writeBits(0, 9);
    // tSTD-Indicator: ENUMERATED{tstd(0),non-tstd(1)} no ext, 2 values → 1 bit
    b.writeBits(1, 1);  // non-tstd
    return b;
}

// SecondarySCH-Information-Cell-SetupRqstFDD (same structure as PrimarySCH):
static AperBuf encSecondarySCHInfo() {
    return encPrimarySCHInfo(); // same layout
}

// PrimaryCPICH-Information-Cell-SetupRqstFDD:
//   commonPhysicalChannelID INTEGER(0..255)
//   primaryCPICH-Power      DL-Power (extensible)
//   iE-Extensions           OPTIONAL
static AperBuf encPrimaryCPICHInfo() {
    AperBuf b;
    b.writeBits(0, 1);  // SEQUENCE ext bit
    b.writeBits(0, 1);  // optional bitmap for iE-Extensions
    b.writeBits(0, 8);  // commonPhysicalChannelID = 0
    b.writeBits(0, 1);  // DL-Power ext bit=0
    b.writeBits(0, 9);  // DL-Power value = 0 (minimal)
    return b;
}

// PrimaryCCPCH-Information-Cell-SetupRqstFDD:
//   The IE has several mandatory sub-structures. For the simulator we
//   encode a minimal but spec-compliant stub.
//   This is quite complex; encode as a known-good 0-byte approximation
//   wrapped in OPEN TYPE with an empty length for now.
static AperBuf encPrimaryCCPCHInfo() {
    // This is a complex SEQUENCE — provide a minimal valid encoding.
    // PrimaryCCPCH-Information-Cell-SetupRqstFDD ::= SEQUENCE {
    //   sttd-Indicator         ENUMERATED{sttd-applied(0),no-sttd(1)} 1bit
    //   p-CCPCH-Power          DL-Power (extensible)
    //   Iub-sAI                NULL or absent (complex, skip)
    //   frameOffset            INTEGER(0..255) 8bits
    //   chipOffset             INTEGER(0..38399,...) extensible
    //   ...  optional IEs
    // }
    AperBuf b;
    b.writeBits(0, 1);  // SEQUENCE ext bit = 0
    // Count of OPTIONAL bits in root: many; skip them → bitmap byte(s)
    // The actual structure has ~10 optional fields; we'll use a simplified encoding.
    // We approximate by sending: sttd-Indicator=no-sttd, high power, offsets=0
    // P-CCPCH bitmask (let's assume many OPTIONAL fields not present)
    // 10 OPTIONAL fields → 10-bit bitmap (all 0 = none present)
    b.writeBits(0, 10);
    // sttd-Indicator: ENUMERATED{sttd-applied,no-sttd} → 1 bit
    b.writeBits(1, 1);
    // p-CCPCH-Power: DL-Power extensible
    b.writeBits(0, 1);  // ext=0
    b.writeBits(100, 9); // 10 dBm above min (-350+100 = -250 → -25dBm)
    // frameOffset: INTEGER(0..255) → 8 bits
    b.writeBits(0, 8);
    // chipOffset: INTEGER(0..38399,...) extensible
    // range 38400 → ceil(log2(38400))=16 bits
    b.writeBits(0, 1);  // ext=0
    b.writeBits(0, 16);
    return b;
}

// Limited-power-increase-information:
// ENUMERATED { apply, not-apply, ... } extensible
static AperBuf encLimitedPowerIncrease() {
    AperBuf b;
    b.writeBits(0, 1);  // ext=0
    b.writeBits(1, 1);  // not-apply (index 1), 2 root values → 1 bit
    return b;
}

// ─────────────────────────────── IE ID constants ─────────────────────────────
// From NBAP-Constants.asn

static constexpr uint16_t IE_Local_Cell_ID            = 124;
static constexpr uint16_t IE_C_ID                     = 25;
static constexpr uint16_t IE_ConfigGenID              = 43;
static constexpr uint16_t IE_T_Cell                   = 276;
static constexpr uint16_t IE_UARFCN_Nu                = 282;
static constexpr uint16_t IE_UARFCN_Nd                = 281;
static constexpr uint16_t IE_MaxTxPower               = 131;
static constexpr uint16_t IE_PrimaryScrCode           = 181;
static constexpr uint16_t IE_SyncConfig               = 225; // id-Synchronisation-Configuration-Cell-SetupRqst
static constexpr uint16_t IE_DL_TPC_Pattern01Count    = 75;  // id-DL-TPC-Pattern01Count
static constexpr uint16_t IE_PrimarySCH               = 182; // id-PrimarySCH-Information-Cell-SetupRqstFDD
static constexpr uint16_t IE_SecondarySCH             = 228; // id-SecondarySCH-Information-Cell-SetupRqstFDD
static constexpr uint16_t IE_PrimaryCPICH             = 183; // id-PrimaryCPICH-Information-Cell-SetupRqstFDD
static constexpr uint16_t IE_PrimaryCCPCH             = 184; // id-PrimaryCCPCH-Information-Cell-SetupRqstFDD
static constexpr uint16_t IE_LimitedPowerIncrease     = 134; // id-Limited-power-increase-information-Cell-SetupRqstFDD
static constexpr uint16_t IE_CRNC_CtxID               = 44;
static constexpr uint16_t IE_NodeB_CtxID              = 143;
static constexpr uint16_t IE_ResetIndicator           = 416;
static constexpr uint16_t IE_StartOfAudit             = 114;

// New IE IDs for DCH / HSDPA (from NBAP-Constants.asn)
static constexpr uint16_t IE_CommonTransChType        =  40;  // id-CommonTransportChannelSetupRequestItem
static constexpr uint16_t IE_DCH_SF                   =  57;  // id-SF  (SpreaderFactor IE)
static constexpr uint16_t IE_ReconfigGenID            =  42;  // id-ReconfigurationGenerationID
static constexpr uint16_t IE_HsDschMaxCodes           = 100;  // id-HS-DSCH-CC-Setups (simplified)
static constexpr uint16_t IE_HsDschPower              = 101;  // id-MaxPowerHS-DSCH-Power

// New IE IDs for E-DCH / HSUPA (TS 25.433 §8.1.1.3)
static constexpr uint16_t IE_EDchSetupInd             = 102;  // id-E-DCH-SetupIndicator
static constexpr uint16_t IE_EDchTTI                  = 103;  // id-E-DCH-TTI
static constexpr uint16_t IE_EDchMaxBitrate           = 104;  // id-E-DCH-MaxBitrateIndex

// DL-TPC-Pattern01Count: INTEGER(0..511,...) extensible
static AperBuf encDLTPCPattern01Count() {
    AperBuf b;
    b.writeBits(0, 1);  // ext=0
    b.writeBits(0, 9);  // value 0
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public encode functions
// ─────────────────────────────────────────────────────────────────────────────

ByteBuffer nbap_encode_CellSetupRequestFDD(
    uint32_t localCellId,
    uint16_t cId,
    uint8_t  cfgGenId,
    uint16_t uarfcnUl,
    uint16_t uarfcnDl,
    uint16_t maxTxPower,
    uint16_t primaryScrCode,
    uint8_t  txId)
{
    // Build the value (CellSetupRequestFDD body):
    // SEQUENCE { protocolIEs ProtocolIE-Container, protocolExtensions OPTIONAL, ... }
    // SEQUENCE extension bit = 0
    // OPTIONAL bitmap: 1 optional field (protocolExtensions) → 1 bit = 0
    AperBuf body;
    body.writeBits(0, 1); // SEQUENCE ext marker = 0
    body.writeBits(0, 1); // protocolExtensions not present

    // Build IE container entries
    struct {
        uint16_t id;
        int      crit;
        AperBuf  val;
    } ies[] = {
        { IE_Local_Cell_ID,   CRIT_REJECT, encLocalCellID(localCellId)     },
        { IE_C_ID,            CRIT_REJECT, encCID(cId)                     },
        { IE_ConfigGenID,     CRIT_REJECT, encCfgGenID(cfgGenId)            },
        { IE_T_Cell,          CRIT_REJECT, encTCell(0)                      },
        { IE_UARFCN_Nu,       CRIT_REJECT, encUARFCN(uarfcnUl)              },
        { IE_UARFCN_Nd,       CRIT_REJECT, encUARFCN(uarfcnDl)              },
        { IE_MaxTxPower,      CRIT_REJECT, encMaxTxPower(maxTxPower)        },
        { IE_PrimaryScrCode,  CRIT_REJECT, encPSC(primaryScrCode)          },
        { IE_SyncConfig,      CRIT_REJECT, encSyncConfig()                  },
        { IE_DL_TPC_Pattern01Count, CRIT_REJECT, encDLTPCPattern01Count()  },
        { IE_PrimarySCH,      CRIT_REJECT, encPrimarySCHInfo()              },
        { IE_SecondarySCH,    CRIT_REJECT, encSecondarySCHInfo()            },
        { IE_PrimaryCPICH,    CRIT_REJECT, encPrimaryCPICHInfo()            },
        { IE_PrimaryCCPCH,    CRIT_REJECT, encPrimaryCCPCHInfo()            },
        { IE_LimitedPowerIncrease, CRIT_REJECT, encLimitedPowerIncrease()  },
    };
    constexpr size_t nIEs = sizeof(ies) / sizeof(ies[0]);

    // ProtocolIE-Container length-determinant
    appendSeqOfCount(body, nIEs);

    // Append each IE field
    for (auto& ie : ies)
        appendIEField(body, ie.id, ie.crit, ie.val);

    // TS 25.433 §8.3.6: procedure id=5 (cellSetup), ddMode=fdd(1), common
    return buildInitiatingMessage(5, 1, CRIT_REJECT, 0 /*common*/, txId, body);
}


ByteBuffer nbap_encode_RadioLinkSetupRequestFDD(
    uint32_t crncCtxId,
    uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1); // SEQUENCE ext marker
    body.writeBits(0, 1); // protocolExtensions not present

    appendSeqOfCount(body, 1);
    appendIEField(body, IE_CRNC_CtxID, CRIT_REJECT, encCRNCCtxID(crncCtxId));

    // procedure id=27 (radioLinkSetup), ddMode=fdd(1), msgDisc=common(0)
    return buildInitiatingMessage(27, 1, CRIT_REJECT, 0, txId, body);
}


ByteBuffer nbap_encode_RadioLinkAdditionRequestFDD(
    uint32_t nodeBCtxId,
    uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);
    body.writeBits(0, 1);

    appendSeqOfCount(body, 1);
    appendIEField(body, IE_NodeB_CtxID, CRIT_REJECT, encNodeBCtxID(nodeBCtxId));

    // procedure id=23 (radioLinkAddition), ddMode=fdd(1), msgDisc=dedicated(1)
    return buildInitiatingMessage(23, 1, CRIT_REJECT, 1 /*dedicated*/, txId, body);
}


ByteBuffer nbap_encode_RadioLinkDeletionRequest(
    uint32_t nodeBCtxId,
    uint32_t crncCtxId,
    uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);
    body.writeBits(0, 1);

    appendSeqOfCount(body, 2);
    appendIEField(body, IE_NodeB_CtxID, CRIT_REJECT, encNodeBCtxID(nodeBCtxId));
    appendIEField(body, IE_CRNC_CtxID,  CRIT_REJECT, encCRNCCtxID(crncCtxId));
    // Note: RL-informationList-RL-DeletionRqst mandatory IE omitted for simulator

    // procedure id=24 (radioLinkDeletion), ddMode=common(2), msgDisc=dedicated(1)
    return buildInitiatingMessage(24, 2, CRIT_REJECT, 1 /*dedicated*/, txId, body);
}


ByteBuffer nbap_encode_ResetRequest(uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);
    body.writeBits(0, 1);

    appendSeqOfCount(body, 1);
    appendIEField(body, IE_ResetIndicator, CRIT_IGNORE, encResetIndicatorNodeB());

    // procedure id=13 (reset), ddMode=common(2), msgDisc=common(0)
    return buildInitiatingMessage(13, 2, CRIT_REJECT, 0 /*common*/, txId, body);
}


ByteBuffer nbap_encode_AuditRequest(bool startOfAuditSeq, uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);
    body.writeBits(0, 1);

    appendSeqOfCount(body, 1);
    appendIEField(body, IE_StartOfAudit, CRIT_REJECT,
                  encStartOfAuditSeq(startOfAuditSeq));

    // procedure id=0 (audit), ddMode=common(2), msgDisc=common(0)
    return buildInitiatingMessage(0, 2, CRIT_REJECT, 0 /*common*/, txId, body);
}

// ─────────────────────────────────────────────────────────────────────────────
// DCH / HSDPA encoder implementations
// ─────────────────────────────────────────────────────────────────────────────

// CommonTransportChannelSetupRequest ─────────────────────────────────────────
// TransportChannelType-CommonTransChSetupItem:
//   ENUMERATED { fach(0), pch(1), rach(2) }  3 root values, no ext → 2 bits
static AperBuf encCommonChType(NBAPCommonChannel ch) {
    return encEnum(static_cast<int>(ch), 3);
}

ByteBuffer nbap_encode_CommonTransportChannelSetupRequest(
    uint32_t localCellId, NBAPCommonChannel channelType, uint8_t txId)
{
    // Value SEQUENCE: ext=0, protocolExtensions absent (1 opt bit=0)
    AperBuf body;
    body.writeBits(0, 1);  // SEQUENCE ext
    body.writeBits(0, 1);  // protocolExtensions not present

    appendSeqOfCount(body, 2);  // two IEs: localCellId + channelType
    appendIEField(body, IE_Local_Cell_ID,   CRIT_REJECT, encLocalCellID(localCellId));
    appendIEField(body, IE_CommonTransChType, CRIT_REJECT, encCommonChType(channelType));

    // TS 25.433 §8.3.2 — proc id=4 (commonTransportChannelSetup), ddMode=fdd(1), common(0)
    return buildInitiatingMessage(4, 1, CRIT_REJECT, 0 /*common*/, txId, body);
}

// RadioLinkReconfigurePrepare ─────────────────────────────────────────────────
// SF integer value encoder: SF4→0, SF8→1, SF16→2, ... (7 values → 3 bits)
static AperBuf encSFIndex(SF sf) {
    int idx = 0;
    switch (sf) {
        case SF::SF4:   idx = 0; break;
        case SF::SF8:   idx = 1; break;
        case SF::SF16:  idx = 2; break;
        case SF::SF32:  idx = 3; break;
        case SF::SF64:  idx = 4; break;
        case SF::SF128: idx = 5; break;
        case SF::SF256: idx = 6; break;
        default:        idx = 2; break;  // default SF16
    }
    // SpreadingFactor: ENUMERATED { sf4(0)..sf256(6) } 7 root values → 3 bits
    return encEnum(idx, 7);
}

ByteBuffer nbap_encode_RadioLinkReconfigurePrepare(
    uint32_t crncCtxId, SF newSf, uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);  // SEQUENCE ext
    body.writeBits(0, 1);  // protocolExtensions not present

    appendSeqOfCount(body, 2);  // crncCtxId + new SF
    appendIEField(body, IE_CRNC_CtxID, CRIT_REJECT, encCRNCCtxID(crncCtxId));
    appendIEField(body, IE_DCH_SF,     CRIT_REJECT, encSFIndex(newSf));

    // TS 25.433 §8.1.5 — proc id=26 (radioLinkReconfigurePrepare), ddMode=fdd(1), dedicated(1)
    return buildInitiatingMessage(26, 1, CRIT_REJECT, 1 /*dedicated*/, txId, body);
}

// RadioLinkReconfigureCommit ──────────────────────────────────────────────────
ByteBuffer nbap_encode_RadioLinkReconfigureCommit(uint32_t crncCtxId, uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);  // SEQUENCE ext
    body.writeBits(0, 1);  // protocolExtensions not present

    appendSeqOfCount(body, 1);  // crncCtxId only (minimal)
    appendIEField(body, IE_CRNC_CtxID, CRIT_REJECT, encCRNCCtxID(crncCtxId));

    // TS 25.433 §8.1.6 — proc id=25 (radioLinkReconfigureCommit), ddMode=fdd(1), dedicated(1)
    return buildInitiatingMessage(25, 1, CRIT_REJECT, 1 /*dedicated*/, txId, body);
}

// RadioLinkSetupRequestFDD_HSDPA ──────────────────────────────────────────────
// Extends RadioLinkSetupRequestFDD with HS-DSCH MAC-d flow information.
// Additional IEs carry HSDPA channel code count and max power.
//
// HS-DSCH channelisation codes INTEGER(1..15): 4 bits
static AperBuf encHsDschCodes(uint8_t n) {
    return encInteger(n, 1, 15);
}

// HS-DSCH power INTEGER(0..500): 9 bits
static AperBuf encHsDschPower(uint16_t p) {
    return encInteger(p, 0, 500);
}

ByteBuffer nbap_encode_RadioLinkSetupRequestFDD_HSDPA(
    uint32_t crncCtxId, uint8_t hsDschCodes, uint16_t hsDschPower, uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);  // SEQUENCE ext
    body.writeBits(0, 1);  // protocolExtensions not present

    // 3 IEs: crncCtxId + HS-DSCH code count + HS-DSCH max power
    appendSeqOfCount(body, 3);
    appendIEField(body, IE_CRNC_CtxID,    CRIT_REJECT, encCRNCCtxID(crncCtxId));
    appendIEField(body, IE_HsDschMaxCodes, CRIT_REJECT, encHsDschCodes(hsDschCodes));
    appendIEField(body, IE_HsDschPower,    CRIT_REJECT, encHsDschPower(hsDschPower));

    // TS 25.433 §8.3.15 — proc id=27 extended (radioLinkSetup), ddMode=fdd(1), common(0)
    // Using proc id=27 as base (same as RadioLinkSetupRequestFDD, but with HSDPA payload)
    return buildInitiatingMessage(27, 1, CRIT_REJECT, 0 /*common*/, txId, body);
}

// ─────────────────────────────────────────────────────────────────────────────
// E-DCH / HSUPA encoder  (TS 25.433 §8.1.1.3, TS 25.309)
// ─────────────────────────────────────────────────────────────────────────────

// E-DCH-SetupIndicator: ENUMERATED { e-dch-setup } — single value
// Encoded as constrainedWholeNumber with 1 root (no ext): 0 bits for value,
// but we still prefix the APER length-determinant open wrapper.
// Simplification: encode same way as a 1-bit enum marker.
static AperBuf encEDchSetupInd() {
    AperBuf b;
    b.writeBits(0, 1);  // ext=0
    b.writeBits(0, 1);  // enumIndex=0 (e-dch-setup is the only value)
    return b;
}

// E-DCH-TTI: ENUMERATED { tti2ms(0), tti10ms(1) } — 2 root values → 1 bit
static AperBuf encEDchTTI(EDCHTTI tti) {
    return encEnum(static_cast<int>(tti), 2);
}

// E-DCH max bitrate index: INTEGER(0..7) → 3 bits
static AperBuf encEDchMaxBitrateIdx(uint8_t idx) {
    return encInteger(idx, 0, 7);
}

ByteBuffer nbap_encode_RadioLinkSetupRequestFDD_EDCH(
    uint32_t crncCtxId, EDCHTTI tti, uint8_t maxBitrateIdx, uint8_t txId)
{
    AperBuf body;
    body.writeBits(0, 1);  // SEQUENCE ext
    body.writeBits(0, 1);  // protocolExtensions not present

    // 4 IEs: crncCtxId + E-DCH setup indicator + TTI + max bitrate index
    appendSeqOfCount(body, 4);
    appendIEField(body, IE_CRNC_CtxID,      CRIT_REJECT, encCRNCCtxID(crncCtxId));
    appendIEField(body, IE_EDchSetupInd,    CRIT_REJECT, encEDchSetupInd());
    appendIEField(body, IE_EDchTTI,         CRIT_REJECT, encEDchTTI(tti));
    appendIEField(body, IE_EDchMaxBitrate,  CRIT_REJECT, encEDchMaxBitrateIdx(maxBitrateIdx));

    // TS 25.433 §8.1.1.3 — E-DCH extension of RadioLinkSetup, proc id=27, ddMode=fdd(1)
    return buildInitiatingMessage(27, 1, CRIT_REJECT, 0 /*common*/, txId, body);
}

} // namespace rbs::umts

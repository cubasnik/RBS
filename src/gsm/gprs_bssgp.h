#pragma once

#include "../common/types.h"
#include <cstdint>
#include <vector>

namespace rbs::gsm {

// ── BSSGP PDU types (TS 48.018 §11.3.6) ─────────────────────────────────────
enum class BssgpPduType : uint8_t {
    DL_UNITDATA     = 0x00,
    UL_UNITDATA     = 0x01,
    RADIO_STATUS    = 0x14,
    BVC_RESET       = 0x22,
    BVC_RESET_ACK   = 0x23,
    BVC_BLOCK       = 0x24,
    BVC_BLOCK_ACK   = 0x25,
    BVC_UNBLOCK     = 0x26,
    BVC_UNBLOCK_ACK = 0x27,
    STATUS          = 0x41,
};

/// BSSGP cause values (TS 48.018 §11.3.8).
enum class BssgpCause : uint8_t {
    PROCESSOR_OVERLOAD   = 0x00,
    EQUIPMENT_FAILURE    = 0x01,
    TRANSIT_NETWORK_FAIL = 0x02,
    PROTOCOL_ERROR       = 0x0F,
    CELL_NOT_FOUND       = 0x19,
    OM_INTERVENTION      = 0x20,
    OML_LINK_RESET       = 0x26,
};

/// Cell Identifier (TS 48.018 §11.3.9) — decoded view.
/// Wire encoding uses BCD per TS 24.008 §10.5.1.3 (8 octets).
struct BssgpCellId {
    uint16_t mcc = 0;  ///< Mobile Country Code (0–999)
    uint16_t mnc = 0;  ///< Mobile Network Code (0–999, 2-digit if < 100)
    uint16_t lac = 0;  ///< Location Area Code
    uint8_t  rac = 0;  ///< Routing Area Code
    uint16_t ci  = 0;  ///< Cell Identity
};

/// QoS Profile (TS 48.018 §11.3.28) — 3-byte form.
struct BssgpQoS {
    uint8_t precedenceClass  = 0;  ///< 0–7
    uint8_t delayClass       = 0;  ///< 0–3
    uint8_t reliabilityClass = 0;  ///< 0–5
    uint8_t peakThroughput   = 0;  ///< 0–9
};

/// GPRS trace record written for every DL/UL UNITDATA event.
/// Used for regression/golden-trace testing.
struct GprsBssgpTrace {
    enum class Dir : uint8_t { UL, DL };
    Dir         dir      = Dir::UL;
    uint32_t    tlli     = 0;   ///< Temporary Logical Link Identity
    uint16_t    bvci     = 0;   ///< BSS Virtual Connection Identifier
    uint32_t    llcBytes = 0;   ///< LLC PDU payload length
    BssgpCellId cellId{};       ///< Only populated for UL-UNITDATA
};

// ─────────────────────────────────────────────────────────────────────────────
// GprsBssgp — BSSGP codec + state machine (TS 48.018).
//
// Encodes/decodes DL/UL-UNITDATA, BVC-RESET/ACK, and RADIO-STATUS PDUs.
// Every UNITDATA event is appended to an internal trace log for golden-trace
// regression testing.
//
// BVCI = 0 is the signaling BVC; other BVCI values are data BVCs.
// ─────────────────────────────────────────────────────────────────────────────
class GprsBssgp {
public:
    explicit GprsBssgp(uint16_t bvci);

    uint16_t bvci() const { return bvci_; }

    // ── Encode ────────────────────────────────────────────────────────
    /// UL-UNITDATA PDU (BSS → SGSN), TS 48.018 §10.2.2.
    ByteBuffer encodeUlUnitdata(uint32_t tlli,
                                const BssgpQoS& qos,
                                const BssgpCellId& cellId,
                                const ByteBuffer& llcPdu) const;

    /// DL-UNITDATA PDU (SGSN → BSS), TS 48.018 §10.2.1.
    ByteBuffer encodeDlUnitdata(uint32_t tlli,
                                const BssgpQoS& qos,
                                uint16_t pduLifetimeCs,
                                const ByteBuffer& llcPdu) const;

    /// BVC-RESET (TS 48.018 §10.4.12).
    ByteBuffer encodeBvcReset(BssgpCause cause) const;

    /// BVC-RESET-ACK (TS 48.018 §10.4.13).
    ByteBuffer encodeBvcResetAck() const;

    /// RADIO-STATUS (TS 48.018 §10.2.3).
    ByteBuffer encodeRadioStatus(uint32_t tlli, BssgpCause cause) const;

    // ── Decode / handle ───────────────────────────────────────────────
    /// Parse a raw BSSGP PDU.  Returns the LLC payload for UNITDATA PDUs;
    /// empty for all other types.  Writes an automatic response (e.g.
    /// BVC-RESET-ACK) to @p response when applicable, and appends a trace
    /// entry for each UNITDATA event.
    ByteBuffer handlePdu(const ByteBuffer& pdu,
                         GprsBssgpTrace& trace,
                         ByteBuffer& response);

    // ── Trace log ─────────────────────────────────────────────────────
    const std::vector<GprsBssgpTrace>& traceLog() const { return traceLog_; }
    void clearTrace() { traceLog_.clear(); }

private:
    uint16_t                    bvci_;
    std::vector<GprsBssgpTrace> traceLog_;

    static void       appendTlv(ByteBuffer& buf, uint8_t iei,
                                const ByteBuffer& val);
    static bool       findTlv(const ByteBuffer& pdu, size_t start,
                              uint8_t iei, ByteBuffer& out);
    static void       encodeCellId(ByteBuffer& buf, const BssgpCellId& cell);
    static bool       decodeCellId(const ByteBuffer& data, BssgpCellId& cell);
    static ByteBuffer encodeQoS(const BssgpQoS& qos);
    static uint32_t   decodeBe32(const ByteBuffer& v);
    static uint16_t   decodeBe16(const ByteBuffer& v);
};

}  // namespace rbs::gsm

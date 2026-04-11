#pragma once
#include "../common/types.h"
#include <queue>
#include <unordered_map>
#include <vector>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// LTE RLC — Radio Link Control (3GPP TS 36.322)
//
// Three modes:
//   TM  — Transparent Mode: no header overhead (used for BCCH, DL/UL CCCH)
//   UM  — Unacknowledged Mode: 5-bit or 10-bit SN, reordering, duplicate
//          detection; no ARQ.  Used for VoIP bearer (low latency).
//   AM  — Acknowledged Mode: 10-bit SN, windowed ARQ with STATUS PDU,
//          segmentation/concatenation/reassembly.
//          Default for SRBs (DRB0/DRB1) and interactive PS data.
//
// Key procedures:
//   Segmentation  — SDU split into PDU ≤ maxPduSize for MAC scheduling
//   Concatenation — multiple small SDUs packed into one PDU
//   Reassembly    — UL PDU segments recombined into complete SDU
//   ARQ           — AM retransmissions triggered by STATUS PDU NACK_SN
//   t-Reordering  — UM/AM timer that triggers delivery despite gaps
// ─────────────────────────────────────────────────────────────────────────────

// ── RLC mode ─────────────────────────────────────────────────────────────────
enum class LTERlcMode : uint8_t { TM = 0, UM = 1, AM = 2 };

// ── LTE RLC PDU header fields ─────────────────────────────────────────────────
enum class LTEAMPduType : uint8_t {
    DATA    = 0,   ///< AMD PDU (data)
    CONTROL = 1,   ///< STATUS PDU (ACK/NACK)
};

// ── Per STATUS PDU: NACK entry (may include segment offsets for partial NACKs)
struct NackEntry {
    uint16_t nack_sn;
    bool     hasSegmentOffset;
    uint16_t soStart;   ///< Segment offset start (in bytes)
    uint16_t soEnd;     ///< Segment offset end
};

struct LTEStatusPDU {
    uint16_t             ack_sn;       ///< Next expected SN (highest in-seq + 1)
    std::vector<NackEntry> nacks;      ///< List of NACKed SNs
};

// ── Per-entity context ────────────────────────────────────────────────────────
struct LTERlcEntity {
    RNTI        rnti;
    uint8_t     rbId;
    LTERlcMode  mode    = LTERlcMode::AM;

    // AM sequence numbering (SN size = 10 bits → window = 512)
    uint16_t    txSN    = 0;     ///< VT(S) — next SN to assign
    uint16_t    rxExpSN = 0;     ///< VR(R) — next expected SN
    uint16_t    vtA     = 0;     ///< VT(A) — oldest unacknowledged SN
    uint16_t    windowSz= 512;

    // UM sequence numbering (SN size 5- or 10-bit)
    bool        sn10Bit = false;

    std::queue<ByteBuffer>  txSduQueue;      ///< Input SDUs to segment
    std::queue<ByteBuffer>  retxQueue;       ///< AM: pending retransmit PDUs (data + STATUS)
    std::queue<ByteBuffer>  rxSduQueue;      ///< UL: delivered SDUs for upper layer

    // AM TX window: SN → encoded PDU, kept until ACK'd (for NACK retransmit)
    std::unordered_map<uint16_t, ByteBuffer> txWindow;

    // AM segmentation state: true when the front of txSduQueue is a remainder
    // from a partially segmented SDU (FI[1] must be set)
    bool txMidSdu = false;

    // AM reassembly state
    bool       rxMidSdu     = false;  ///< true while accumulating a multi-segment SDU
    ByteBuffer rxPartialSdu;          ///< accumulator until FI=10 (last segment)
};

// ─────────────────────────────────────────────────────────────────────────────
// ILTERlc — pure-virtual interface
// ─────────────────────────────────────────────────────────────────────────────
class ILTERlc {
public:
    virtual ~ILTERlc() = default;

    // ── Radio Bearer management ───────────────────────────────────────────────
    virtual bool addRB   (RNTI rnti, uint8_t rbId, LTERlcMode mode) = 0;
    virtual bool removeRB(RNTI rnti, uint8_t rbId) = 0;

    // ── DL path (called by PDCP above, feeds MAC below) ───────────────────────
    /// Receive SDU from PDCP; segment and enqueue PDUs for MAC.
    virtual bool  sendSdu (RNTI rnti, uint8_t rbId, ByteBuffer sdu) = 0;

    /// MAC polls for the next PDU to transmit; maxBytes = TB size.
    virtual bool  pollPdu (RNTI rnti, uint8_t rbId,
                            ByteBuffer& pdu, uint16_t maxBytes) = 0;

    // ── UL path (called by MAC; delivers reassembled SDUs to PDCP) ────────────
    /// Deliver a received PDU from MAC; triggers reassembly / STATUS PDU.
    virtual void  deliverPdu(RNTI rnti, uint8_t rbId,
                             const ByteBuffer& pdu) = 0;

    /// Dequeue a reassembled SDU for PDCP.
    virtual bool  receiveSdu(RNTI rnti, uint8_t rbId, ByteBuffer& sdu) = 0;

    // ── Statistics ────────────────────────────────────────────────────────────
    virtual uint16_t txSN(RNTI rnti, uint8_t rbId) const = 0;
    virtual uint16_t rxSN(RNTI rnti, uint8_t rbId) const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// LTERlc — concrete implementation
// ─────────────────────────────────────────────────────────────────────────────
class LTERlc : public ILTERlc {
public:
    bool   addRB     (RNTI rnti, uint8_t rbId, LTERlcMode mode)          override;
    bool   removeRB  (RNTI rnti, uint8_t rbId)                           override;
    bool   sendSdu   (RNTI rnti, uint8_t rbId, ByteBuffer sdu)           override;
    bool   pollPdu   (RNTI rnti, uint8_t rbId,
                      ByteBuffer& pdu, uint16_t maxBytes)                 override;
    void   deliverPdu(RNTI rnti, uint8_t rbId, const ByteBuffer& pdu)    override;
    bool   receiveSdu(RNTI rnti, uint8_t rbId, ByteBuffer& sdu)          override;
    uint16_t txSN    (RNTI rnti, uint8_t rbId) const                     override;
    uint16_t rxSN    (RNTI rnti, uint8_t rbId) const                     override;

private:
    using EntityKey = uint32_t;  // (rnti << 8) | rbId
    static EntityKey makeKey(RNTI r, uint8_t b) {
        return (static_cast<uint32_t>(r) << 8) | b;
    }

    std::unordered_map<EntityKey, LTERlcEntity> entities_;

    void  processTM       (LTERlcEntity& e, const ByteBuffer& pdu);
    void  processUM       (LTERlcEntity& e, const ByteBuffer& pdu);
    void  processAM       (LTERlcEntity& e, const ByteBuffer& pdu);

    ByteBuffer segmentAM      (LTERlcEntity& e, uint16_t maxBytes);
    ByteBuffer buildStatusPDU (const LTERlcEntity& e) const;
    ByteBuffer addUMHeader    (const ByteBuffer& payload, uint16_t sn) const;
    // fi: Framing Info per TS 36.322 §6.2.3.3
    //   00 = complete SDU   01 = first segment
    //   10 = last segment   11 = middle segment
    ByteBuffer addAMHeader    (const ByteBuffer& payload, uint16_t sn,
                               bool poll, LTEAMPduType type, uint8_t fi = 0) const;
    bool       parseStatusPDU (const ByteBuffer& pdu, LTEStatusPDU& status) const;
};

}  // namespace rbs::lte

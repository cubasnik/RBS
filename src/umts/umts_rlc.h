#pragma once
#include "../common/types.h"
#include <queue>
#include <unordered_map>

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// UMTS RLC — Radio Link Control sublayer (3GPP TS 25.322)
//
// Three operating modes:
//   TM — Transparent Mode: no header, no retransmission (used for BCH/PCH/RACH,
//        also Circuit-Switched voice on DCH)
//
//   UM — Unacknowledged Mode: sequence number (7-bit or 10-bit SN), reordering,
//        duplicate detection; no ARQ.  Used for streaming & interactive services.
//
//   AM — Acknowledged Mode: 12-bit SN, windowed ARQ (STATUS PDU),
//        segmentation/reassembly, in-sequence delivery.  Used for PS data.
//
// Each RLC entity is bound to a logical channel (SRB or DRB) identified by
// (RNTI, rb_id).
// ─────────────────────────────────────────────────────────────────────────────

// ── RLC mode ─────────────────────────────────────────────────────────────────
enum class RLCMode : uint8_t { TM, UM, AM };

// ── AM PDU types (TS 25.322 §9.2.1) ──────────────────────────────────────────
enum class AMPduType : uint8_t {
    DATA    = 0,   ///< Regular data PDU
    CONTROL = 1,   ///< STATUS / RESET / RESET_ACK
};

// ── STATUS PDU (ACK/NACK bitmap for AM) ───────────────────────────────────────
struct StatusPDU {
    uint16_t ack_sn;                         ///< Next expected in-sequence SN
    std::vector<uint16_t> nack_sn_list;      ///< Individual NACKed SNs
};

// ── Per-RLC entity context ────────────────────────────────────────────────────
struct RLCEntity {
    RNTI      rnti;
    uint8_t   rbId;
    RLCMode   mode = RLCMode::AM;

    // AM / UM windows
    uint16_t txSN    = 0;    ///< Next SN to assign on DL
    uint16_t rxExpSN = 0;    ///< Next expected SN from UE
    uint16_t vtA     = 0;    ///< AM: VT(A) — oldest unacknowledged DL SN
    uint16_t vtWS    = 32;   ///< AM: window size (default 32)

    std::queue<ByteBuffer>  txQueue;          ///< DL: SDUs waiting to be segmented
    std::queue<ByteBuffer>  retxQueue;        ///< DL: PDUs pending retransmission (AM)
    std::queue<ByteBuffer>  rxReassembly;     ///< UL: received PDU segments
    std::queue<ByteBuffer>  rxSduQueue;       ///< UL: reassembled SDUs for upper layer
};

// ─────────────────────────────────────────────────────────────────────────────
// IUMTSRlc — pure-virtual interface
// ─────────────────────────────────────────────────────────────────────────────
class IUMTSRlc {
public:
    virtual ~IUMTSRlc() = default;

    // ── Radio Bearer management ───────────────────────────────────────────────
    virtual bool addRB   (RNTI rnti, uint8_t rbId, RLCMode mode) = 0;
    virtual bool removeRB(RNTI rnti, uint8_t rbId) = 0;

    // ── Data plane (called by RRC/user-plane above, MAC below) ───────────────
    /// DL: receive an SDU from upper layer and segment it into PDUs for MAC.
    virtual bool  sendSdu    (RNTI rnti, uint8_t rbId, ByteBuffer sdu) = 0;

    /// UL: dequeue a reassembled SDU delivered from MAC.
    virtual bool  receiveSdu (RNTI rnti, uint8_t rbId, ByteBuffer& sdu) = 0;

    /// MAC calls this with a received PDU (UL or STATUS PDU).
    virtual void  deliverPdu (RNTI rnti, uint8_t rbId, const ByteBuffer& pdu) = 0;

    /// Poll the next PDU to pass to MAC for DL transmission.
    virtual bool  pollPdu    (RNTI rnti, uint8_t rbId, ByteBuffer& pdu) = 0;

    // ── Statistics ────────────────────────────────────────────────────────────
    virtual uint16_t txSN(RNTI rnti, uint8_t rbId) const = 0;
    virtual uint16_t rxSN(RNTI rnti, uint8_t rbId) const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// UMTSRlc — concrete implementation
// ─────────────────────────────────────────────────────────────────────────────
class UMTSRlc : public IUMTSRlc {
public:
    bool   addRB     (RNTI rnti, uint8_t rbId, RLCMode mode)        override;
    bool   removeRB  (RNTI rnti, uint8_t rbId)                      override;
    bool   sendSdu   (RNTI rnti, uint8_t rbId, ByteBuffer sdu)      override;
    bool   receiveSdu(RNTI rnti, uint8_t rbId, ByteBuffer& sdu)     override;
    void   deliverPdu(RNTI rnti, uint8_t rbId, const ByteBuffer& p) override;
    bool   pollPdu   (RNTI rnti, uint8_t rbId, ByteBuffer& pdu)     override;
    uint16_t txSN    (RNTI rnti, uint8_t rbId) const                override;
    uint16_t rxSN    (RNTI rnti, uint8_t rbId) const                override;

private:
    using EntityKey = uint32_t;  // (rnti << 8) | rbId
    static EntityKey makeKey(RNTI r, uint8_t b) {
        return (static_cast<uint32_t>(r) << 8) | b;
    }

    std::unordered_map<EntityKey, RLCEntity> entities_;

    void   processTM (RLCEntity& e, const ByteBuffer& pdu);
    void   processUM (RLCEntity& e, const ByteBuffer& pdu);
    void   processAM (RLCEntity& e, const ByteBuffer& pdu);

    ByteBuffer segmentAM   (RLCEntity& e, const ByteBuffer& sdu, uint16_t maxBytes);
    ByteBuffer buildStatus (const RLCEntity& e) const;
    ByteBuffer addUMHeader (const ByteBuffer& payload, uint16_t sn) const;
    ByteBuffer addAMHeader (const ByteBuffer& payload, uint16_t sn,
                            bool poll, AMPduType type) const;
};

}  // namespace rbs::umts

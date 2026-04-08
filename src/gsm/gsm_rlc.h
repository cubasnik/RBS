#pragma once
#include "../common/types.h"
#include <functional>
#include <queue>
#include <unordered_map>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// GSM RLC — LAPDm sublayer (TS 44.006)
//
// LAPDm (Link Access Procedure on Dm channel) is the data-link layer that
// sits between the RR (Radio Resource) layer and the physical burst transport.
// It provides:
//   • Unacknowledged mode (UI frames) — used on BCCH, PCH, AGCH
//   • Acknowledged multi-frame mode (I/RR/REJ frames) — used on SDCCH, SACCH
//   • Connection establishment (SABM → UA) and release (DISC → UA/DM)
//
// SAP identifiers (TS 44.005):
//   SAPI 0 — RR / MM / CC (Radio Resource, Mobility Mgmt, Call Control)
//   SAPI 3 — SMS Short Message Service
// ─────────────────────────────────────────────────────────────────────────────

// ── SAP identifier ────────────────────────────────────────────────────────────
enum class SAPI : uint8_t {
    RR_MM_CC = 0,   ///< Radio Resource / Mobility Management / Call Control
    SMS      = 3,   ///< Short Message Service
};

// ── LAPDm frame types ─────────────────────────────────────────────────────────
enum class LAPDmFrameType : uint8_t {
    // Unnumbered
    SABM = 0,   ///< Set Async Balanced Mode  — connection request
    UA   = 1,   ///< Unnumbered Acknowledge   — affirm SABM / DISC
    DM   = 2,   ///< Disconnected Mode        — refuse/report disconnect
    DISC = 3,   ///< Disconnect
    UI   = 4,   ///< Unnumbered Information   — broadcast, unacknowledged
    // Information
    I    = 5,   ///< Information frame        — sequenced, acknowledged
    // Supervisory
    RR   = 6,   ///< Receive Ready
    REJ  = 7,   ///< Reject (retransmit request)
    RNR  = 8,   ///< Receive Not Ready        — flow control
};

// ── Raw LAPDm PDU ─────────────────────────────────────────────────────────────
struct LAPDmFrame {
    uint8_t    address;    ///< EA|C/R|SAPI (octet 1 of header)
    uint8_t    control;    ///< frame type + N(S)/N(R) (octet 2)
    uint8_t    length;     ///< LI = length indicator (octet 3)
    ByteBuffer info;       ///< L3 payload (RR message etc.)
};

// ── Per-entity link state machine ─────────────────────────────────────────────
enum class LAPDmState : uint8_t {
    IDLE,                   ///< No data link
    AWAITING_EST,           ///< SABM sent, waiting for UA
    MULTIPLE_FRAME_EST,     ///< Acknowledged mode active (I frames flowing)
    TIMER_RECOVERY,         ///< Awaiting response after T200 expiry
    AWAITING_REL,           ///< DISC sent, waiting for UA/DM
};

struct LAPDmEntity {
    RNTI         rnti;
    SAPI         sapi;
    LAPDmState   state    = LAPDmState::IDLE;

    uint8_t vs   = 0;    ///< V(S) — next send sequence number
    uint8_t vr   = 0;    ///< V(R) — next expected receive sequence number
    uint8_t va   = 0;    ///< V(A) — last acknowledged send seq number
    uint8_t retx = 0;    ///< Retransmission counter (max N200 = 3)

    std::queue<LAPDmFrame> unackedTx;      ///< Sent but not yet ACK'd I-frames
    std::queue<ByteBuffer> rxSduQueue;     ///< Reassembled L3 SDUs for upper layer
};

// ─────────────────────────────────────────────────────────────────────────────
// IGSMRlc — pure-virtual interface (LAPDm)
// ─────────────────────────────────────────────────────────────────────────────
class IGSMRlc {
public:
    virtual ~IGSMRlc() = default;

    // ── Data link establishment / release ─────────────────────────────────────
    /// Send SABM to initiate multi-frame mode.
    virtual bool requestLink(RNTI rnti, SAPI sapi) = 0;

    /// Send DISC to terminate multi-frame mode.
    virtual bool releaseLink(RNTI rnti, SAPI sapi) = 0;

    // ── Data transfer ─────────────────────────────────────────────────────────
    /// Enqueue an L3 SDU for DL transmission.  Uses UI (SAPI!=0 broadcast) or
    /// I-frames (acknowledged, multi-frame mode).
    virtual bool sendSdu   (RNTI rnti, SAPI sapi, ByteBuffer sdu) = 0;

    /// Dequeue a received and reassembled L3 SDU from the UL path.
    virtual bool receiveSdu(RNTI rnti, SAPI sapi, ByteBuffer& sdu) = 0;

    /// Process one received LAPDm frame (called by MAC on each UL burst).
    virtual void tick      (const LAPDmFrame& rxFrame, RNTI rnti) = 0;

    /// Query the current link state for a given UE / SAPI.
    virtual LAPDmState linkState(RNTI rnti, SAPI sapi) const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GSMRlc — concrete LAPDm implementation
// ─────────────────────────────────────────────────────────────────────────────
class GSMRlc : public IGSMRlc {
public:
    bool       requestLink(RNTI rnti, SAPI sapi)                    override;
    bool       releaseLink(RNTI rnti, SAPI sapi)                    override;
    bool       sendSdu    (RNTI rnti, SAPI sapi, ByteBuffer sdu)    override;
    bool       receiveSdu (RNTI rnti, SAPI sapi, ByteBuffer& sdu)   override;
    void       tick       (const LAPDmFrame& rxFrame, RNTI rnti)    override;
    LAPDmState linkState  (RNTI rnti, SAPI sapi) const              override;

private:
    using EntityKey = uint32_t;  // (rnti << 8) | sapi
    static EntityKey makeKey(RNTI r, SAPI s) {
        return (static_cast<uint32_t>(r) << 8) | static_cast<uint8_t>(s);
    }

    std::unordered_map<EntityKey, LAPDmEntity> entities_;

    LAPDmEntity& getOrCreate(RNTI rnti, SAPI sapi);
    void processIFrame  (LAPDmEntity& e, const LAPDmFrame& f);
    void processSuperv  (LAPDmEntity& e, const LAPDmFrame& f);
    void processUnnumb  (LAPDmEntity& e, const LAPDmFrame& f);

    LAPDmFrame buildUA (uint8_t address) const;
    LAPDmFrame buildDM (uint8_t address) const;
    LAPDmFrame buildRR (uint8_t address, uint8_t nr) const;
};

}  // namespace rbs::gsm

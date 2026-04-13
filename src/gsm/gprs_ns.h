#pragma once

#include "../common/types.h"
#include <cstdint>
#include <vector>

namespace rbs::gsm {

/// NS PDU types per TS 48.016 §10.3.7.
enum class NsPduType : uint8_t {
    NS_UNITDATA    = 0x00,
    NS_RESET       = 0x02,
    NS_RESET_ACK   = 0x03,
    NS_BLOCK       = 0x04,
    NS_BLOCK_ACK   = 0x05,
    NS_UNBLOCK     = 0x06,
    NS_UNBLOCK_ACK = 0x07,
    NS_STATUS      = 0x08,
    NS_ALIVE       = 0x0A,
    NS_ALIVE_ACK   = 0x0B,
};

/// NS cause values per TS 48.016 §10.3.2.
enum class NsCause : uint8_t {
    TRANSIT_NETWORK_FAILURE = 0x00,
    OM_INTERVENTION         = 0x01,
    EQUIPMENT_FAILURE       = 0x02,
    NS_VC_BLOCKED           = 0x03,
    NS_VC_UNKNOWN           = 0x04,
    BSSGP_LAYER_RESTART     = 0x05,
    PROTOCOL_ERROR          = 0x20,
};

/// NS Virtual Circuit operational state.
enum class NsVcState : uint8_t {
    IDLE    = 0,  ///< Not yet reset.
    BLOCKED = 1,  ///< After NS-RESET before NS-UNBLOCK.
    ALIVE   = 2,  ///< Fully operative.
};

/// Counters per NS virtual circuit.
struct NsVcStats {
    uint32_t txFrames = 0;
    uint32_t rxFrames = 0;
    uint32_t txAlive  = 0;
    uint32_t rxAlive  = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GprsNs — NS Virtual Circuit codec + state machine (TS 48.016).
//
// Encodes/decodes NS PDUs and runs the mandatory state machine:
//   IDLE → (send NS-RESET) → BLOCKED ←→ ALIVE
//
// Usage (same-process simulation):
//   ByteBuffer req = nsA.encodeReset();
//   ByteBuffer ack;
//   nsB.handlePdu(req, ack);           // nsB → BLOCKED, ack = NS-RESET-ACK
//   ByteBuffer unblock;
//   nsA.handlePdu(ack, unblock);       // nsA → BLOCKED, unblock = NS-UNBLOCK
//   ByteBuffer unblockAck;
//   nsB.handlePdu(unblock, unblockAck);// nsB → ALIVE
//   nsA.handlePdu(unblockAck, …);      // nsA → ALIVE
// ─────────────────────────────────────────────────────────────────────────────
class GprsNs {
public:
    explicit GprsNs(uint16_t nsei, uint16_t nsvci);

    NsVcState state()  const { return state_; }
    uint16_t  nsei()   const { return nsei_;  }
    uint16_t  nsvci()  const { return nsvci_; }
    NsVcStats stats()  const { return stats_; }

    // ── Encode NS PDUs ─────────────────────────────────────────────────
    ByteBuffer encodeReset()      const;  ///< TS 48.016 §9.2.6
    ByteBuffer encodeResetAck()   const;  ///< TS 48.016 §9.2.7
    ByteBuffer encodeAlive()      const;  ///< TS 48.016 §9.2.11
    ByteBuffer encodeAliveAck()   const;  ///< TS 48.016 §9.2.12
    ByteBuffer encodeUnblock()    const;  ///< TS 48.016 §9.2.8
    ByteBuffer encodeUnblockAck() const;  ///< TS 48.016 §9.2.9

    /// Wrap a BSSGP SDU in an NS-UNITDATA PDU (TS 48.016 §9.2.10).
    ByteBuffer encodeUnitdata(uint16_t bvci, const ByteBuffer& bssgpSdu) const;

    // ── Handle an incoming NS PDU ──────────────────────────────────────
    /// Advance the state machine from a received PDU.
    /// @param pdu      Raw received NS PDU bytes.
    /// @param response Populated with a reply PDU to transmit, if any.
    /// @return         Embedded BSSGP SDU for NS-UNITDATA; empty for all
    ///                 other PDU types.
    ByteBuffer handlePdu(const ByteBuffer& pdu, ByteBuffer& response);

    /// Directly set state to ALIVE (test setup shortcut).
    void forceAlive();

private:
    uint16_t  nsei_;
    uint16_t  nsvci_;
    NsVcState state_ = NsVcState::IDLE;
    NsVcStats stats_{};

    static void     appendTlv(ByteBuffer& buf, uint8_t iei, const ByteBuffer& val);
    static bool     findTlv(const ByteBuffer& pdu, size_t start,
                            uint8_t iei, ByteBuffer& out);
    static uint16_t decodeBe16(const ByteBuffer& v);
};

}  // namespace rbs::gsm

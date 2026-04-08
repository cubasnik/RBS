#pragma once
#include "../common/types.h"
#include <string>
#include <cstdint>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// Abis interface — BTS ↔ BSC
//
// The Abis interface carries two protocol streams:
//   OML  — Operation and Maintenance Link  (TS 12.21 / TS 08.59)
//   RSL  — Radio Signalling Link           (TS 08.58)
//
// Transport: E1/T1 TDM (classic) or IP (Abis over IP, 3GPP R99+).
// ─────────────────────────────────────────────────────────────────────────────

// ── OML message discriminators (TS 12.21 §8.6, representative subset) ────────
enum class OMLMsgType : uint8_t {
    SET_BTS_ATTR              = 0x04,
    GET_BTS_ATTR              = 0x05,
    SET_RADIO_CARRIER_ATTR    = 0x11,
    CHANNEL_ACTIVATION        = 0x20,
    CHANNEL_ACTIVATION_ACK    = 0x21,
    CHANNEL_ACTIVATION_NACK   = 0x22,
    RF_CHAN_REL               = 0x28,
    OPSTART                   = 0x41,
    OPSTART_ACK               = 0x42,
    OPSTART_NACK              = 0x43,
    FAILURE_EVENT_REPORT      = 0x61,
    SOFTWARE_ACTIVATE_NOTICE  = 0x71,
};

// ── RSL message discriminators (TS 08.58 §9.1, representative subset) ────────
enum class RSLMsgType : uint8_t {
    DATA_REQUEST              = 0x10,
    DATA_INDICATION           = 0x11,
    DATA_CONFIRM              = 0x12,
    ERROR_INDICATION          = 0x13,
    CHANNEL_ACTIVATION        = 0x30,
    CHANNEL_ACTIVATION_ACK    = 0x31,
    CHANNEL_ACTIVATION_NACK   = 0x32,
    CHANNEL_RELEASE           = 0x34,
    RF_CHANNEL_RELEASE        = 0x35,
    RF_CHANNEL_RELEASE_ACK    = 0x36,
    MEASUREMENT_RES           = 0x3D,
    HANDOVER_CMD              = 0x3E,
    CIPHER_MODE_CMD           = 0x40,
    CIPHER_MODE_COMPLETE      = 0x41,
    CIPHER_MODE_REJECT        = 0x42,
    PAGING_CMD                = 0x51,
};

struct AbisMessage {
    uint8_t    entity;    ///< OML object class or RSL channel number
    ByteBuffer payload;   ///< raw message octets
};

// ── IAbisOml — Operation and Maintenance Link (BTS ↔ BSC) ────────────────────
class IAbisOml {
public:
    virtual ~IAbisOml() = default;

    // ── Link management ───────────────────────────────────────────────────────
    virtual bool connect   (const std::string& bscAddr, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // ── OML message exchange ──────────────────────────────────────────────────
    /// Send an OML message towards the BSC.
    virtual bool sendOmlMsg(OMLMsgType type, const AbisMessage& msg) = 0;

    /// Receive an OML message from the BSC (non-blocking; false if empty).
    virtual bool recvOmlMsg(OMLMsgType& type, AbisMessage& msg) = 0;

    // ── BTS management ────────────────────────────────────────────────────────
    /// Configure ARFCN and transmit power for a TRX.
    virtual bool configureTRX(uint8_t trxId, uint16_t arfcn,
                              int8_t txPower_dBm) = 0;

    /// Report a hardware fault event to the BSC.
    virtual void reportHwFailure(uint8_t objectClass,
                                 const std::string& cause) = 0;
};

// ── IAbisRsl — Radio Signalling Link (BTS ↔ BSC) ─────────────────────────────
class IAbisRsl {
public:
    virtual ~IAbisRsl() = default;

    // ── RSL message exchange ──────────────────────────────────────────────────
    /// Send an RSL message towards the BSC.
    virtual bool sendRslMsg(RSLMsgType type, const AbisMessage& msg) = 0;

    /// Receive an RSL message from the BSC (non-blocking).
    virtual bool recvRslMsg(RSLMsgType& type, AbisMessage& msg) = 0;

    // ── Channel control ───────────────────────────────────────────────────────
    /// Activate a dedicated channel (BSC → BTS → MAC).
    virtual bool activateChannel(uint8_t chanNr, GSMChannelType type,
                                 RNTI rnti, uint8_t timingAdvance) = 0;

    /// Release a dedicated channel.
    virtual bool releaseChannel(uint8_t chanNr) = 0;

    // ── Ciphering ─────────────────────────────────────────────────────────────
    /// Forward ciphering mode command to a UE (from BSC).
    virtual bool sendCipherModeCommand(uint8_t chanNr, uint8_t algorithm,
                                       const ByteBuffer& key) = 0;

    // ── Measurements ──────────────────────────────────────────────────────────
    /// Report Rx level and quality measurements to BSC.
    virtual void sendMeasurementResult(RNTI rnti,
                                       int8_t rxlev, uint8_t rxqual) = 0;

    // ── Handover ──────────────────────────────────────────────────────────────
    /// Forward a Handover Command received from the BSC to a UE.
    virtual bool forwardHandoverCommand(RNTI rnti,
                                        const ByteBuffer& hoCmd) = 0;
};

}  // namespace rbs::gsm

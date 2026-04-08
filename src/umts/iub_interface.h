#pragma once
#include "../common/types.h"
#include <string>
#include <cstdint>

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// Iub interface — UMTS NodeB ↔ RNC
//
// References:
//   TS 25.430 — Iub interface: layer 1
//   TS 25.431 — Iub general aspects and principles
//   TS 25.433 — NBAP (Node B Application Part) — control plane
//   TS 25.435 — FP  (Frame Protocol)             — user-plane DCH transport
// ─────────────────────────────────────────────────────────────────────────────

// ── NBAP Procedure Codes (TS 25.433, representative subset) ──────────────────
enum class NBAPProcedure : uint8_t {
    // Common procedures
    CELL_SETUP                    = 0x01,
    CELL_RECONFIGURE              = 0x02,
    COMMON_TRANSPORT_RECONFIGURE  = 0x03,
    COMMON_MEASUREMENT_INITIATE   = 0x10,
    COMMON_MEASUREMENT_REPORT     = 0x11,
    COMMON_MEASUREMENT_TERMINATE  = 0x12,
    BLOCK                         = 0x20,
    UNBLOCK                       = 0x21,
    RESET                         = 0x30,
    RESET_RESPONSE                = 0x31,
    // Dedicated procedures
    RADIO_LINK_SETUP              = 0x50,
    RADIO_LINK_ADDITION           = 0x51,
    RADIO_LINK_RECONFIGURE_PREP   = 0x52,
    RADIO_LINK_RECONFIGURE_COMMIT = 0x53,
    RADIO_LINK_DELETION           = 0x54,
    DEDICATED_MEAS_INITIATE       = 0x60,
    DEDICATED_MEAS_REPORT         = 0x61,
    DEDICATED_MEAS_TERMINATE      = 0x62,
};

struct NBAPMessage {
    NBAPProcedure procedure;
    uint16_t      transactionId;
    ByteBuffer    payload;
};

// ── IIubNbap — Node B Application Part ───────────────────────────────────────
class IIubNbap {
public:
    virtual ~IIubNbap() = default;

    // ── Link management ───────────────────────────────────────────────────────
    virtual bool connect   (const std::string& rncAddr, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // ── Common NBAP ───────────────────────────────────────────────────────────
    /// Cell Setup Request — NodeB → RNC at power-up (TS 25.433 §8.2).
    virtual bool sendCellSetup(uint16_t cellId,
                               uint16_t primaryScrCode,
                               uint16_t uarfcnDl,
                               uint16_t uarfcnUl) = 0;

    /// Initiate common measurements (CPICH RSCP, Ec/No).
    virtual bool commonMeasurementInitiation(uint16_t measId,
                                             const std::string& measObject) = 0;

    /// Terminate previously initiated common measurements.
    virtual bool commonMeasurementTermination(uint16_t measId) = 0;

    // ── Dedicated NBAP ────────────────────────────────────────────────────────
    /// Radio Link Setup for a newly admitted UE.
    virtual bool radioLinkSetup  (RNTI rnti, uint16_t scrCode, SF sf) = 0;

    /// Radio Link Deletion when a UE is released.
    virtual bool radioLinkDeletion(RNTI rnti) = 0;

    /// Initiate dedicated UE measurements (CQI, Ec/No, path loss).
    virtual bool dedicatedMeasurementInitiation(RNTI rnti,
                                                uint16_t measId) = 0;

    // ── Raw message pump ──────────────────────────────────────────────────────
    virtual bool sendNbapMsg(const NBAPMessage& msg) = 0;
    virtual bool recvNbapMsg(NBAPMessage& msg) = 0;
};

// ── IIubFp — Frame Protocol: DCH user-plane transport ────────────────────────
class IIubFp {
public:
    virtual ~IIubFp() = default;

    /// Deliver an uplink DCH Transport Block Set to the RNC.
    virtual bool sendDchData(RNTI rnti, uint16_t scrCode,
                             const ByteBuffer& tbs) = 0;

    /// Retrieve a downlink DCH Transport Block Set from the RNC.
    virtual bool receiveDchData(RNTI rnti, uint16_t scrCode,
                                ByteBuffer& tbs) = 0;

    /// Report in-sync / out-of-sync status to the RNC.
    virtual void reportSyncStatus(RNTI rnti, bool inSync) = 0;
};

}  // namespace rbs::umts

// F1AP codec — F1 Setup Request / Response
// TS 38.473 §8.7.1 (F1 Setup), §9.3 (IEs)
//
// Encoding: simulation APER — a compact TLV binary format that
// correctly round-trips for all IEs required by the F1 Setup
// procedure.  Byte layout documented inline.
#pragma once
#include "../common/types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace rbs::nr {

/// F1 Setup IE: one served NR cell (TS 38.473 §9.3.1.10)
struct F1ServedCell {
    uint64_t nrCellIdentity;  ///< 36-bit NCI (gNB-ID || Cell-ID)
    uint32_t nrArfcn;         ///< NR-ARFCN of the DL carrier
    NRScs    scs;             ///< Subcarrier spacing
    uint16_t pci;             ///< Physical Cell Identity 0-1007
    uint16_t tac;             ///< Tracking Area Code
};

/// F1 Setup Request (gNB-DU → gNB-CU, TS 38.473 §8.7.1.2)
struct F1SetupRequest {
    uint16_t             transactionId;   ///< 0-255 (TS 38.473 §9.3.1.74)
    uint64_t             gnbDuId;         ///< 36-bit gNB-DU ID
    std::string          gnbDuName;       ///< optional, max 150 chars
    std::vector<F1ServedCell> servedCells;///< at least 1 cell
};

/// F1 Setup Response (gNB-CU → gNB-DU, TS 38.473 §8.7.1.3)
struct F1SetupResponse {
    uint16_t    transactionId;
    std::string gnbCuName;               ///< optional
    std::vector<uint64_t> activatedCells; ///< NCI of cells to activate
};

/// F1 Setup Failure (gNB-CU → gNB-DU, TS 38.473 §8.7.1.4)
struct F1SetupFailure {
    uint16_t transactionId;
    uint8_t  causeType;   ///< 0=radio, 1=transport, 2=protocol, 3=misc
    uint8_t  causeValue;
};

// ── Encoder / Decoder ──────────────────────────────────────────

/// Encode F1 Setup Request → binary PDU.
/// Returns empty buffer on error.
ByteBuffer encodeF1SetupRequest(const F1SetupRequest& req);

/// Decode F1 Setup Request from binary PDU.
/// Returns false if the PDU is malformed.
bool decodeF1SetupRequest(const ByteBuffer& pdu, F1SetupRequest& out);

/// Encode F1 Setup Response → binary PDU.
ByteBuffer encodeF1SetupResponse(const F1SetupResponse& rsp);

/// Decode F1 Setup Response from binary PDU.
bool decodeF1SetupResponse(const ByteBuffer& pdu, F1SetupResponse& out);

/// Encode F1 Setup Failure → binary PDU.
ByteBuffer encodeF1SetupFailure(const F1SetupFailure& fail);

/// Decode F1 Setup Failure from binary PDU.
bool decodeF1SetupFailure(const ByteBuffer& pdu, F1SetupFailure& out);

}  // namespace rbs::nr

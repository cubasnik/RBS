#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// S1AP APER codec helpers  (TS 36.413, APER per X.691)
//
// Wraps the asn1c-generated rbs_asn1_s1ap library.
// All encode functions return a ByteBuffer with the APER-encoded S1AP-PDU.
// The decode function returns a heap-allocated S1AP_PDU_t (caller must call
// s1ap_pdu_free() when done).
//
// Include guard against the asn1c-generated headers (extern "C") is handled
// inside the .cpp; these declarations use only rbs::lte types.
// ─────────────────────────────────────────────────────────────────────────────
#include "../common/types.h"
#include "s1ap_interface.h"
#include <string>
#include <vector>
#include <cstdint>

// Forward-declare the C PDU type so callers can hold a pointer without pulling
// in all of the generated headers.
extern "C" { struct S1AP_PDU; }

namespace rbs::lte {

// ── Opaque PDU handle ─────────────────────────────────────────────────────────
using S1APPduHandle = S1AP_PDU*;

/// Free a PDU returned by s1ap_decode(). Safe to call on nullptr.
void s1ap_pdu_free(S1APPduHandle pdu);

// ── Encode helpers (build PDU + APER-encode to ByteBuffer) ────────────────────

/// S1 Setup Request  (TS 36.413 §8.7.3)
/// plmnId: 3 bytes packed as MCC/MNC (0x123456 → MCC=123, MNC=456)
ByteBuffer s1ap_encode_S1SetupRequest(
    uint32_t    enbId,          ///< 20-bit macro eNB ID
    const std::string& enbName, ///< optional, empty → omit IE
    uint32_t    plmnId,         ///< 3-byte packed PLMN
    uint16_t    tac             ///< Tracking Area Code
);

/// Initial UE Message  (TS 36.413 §8.6.2.1)
ByteBuffer s1ap_encode_InitialUEMessage(
    uint32_t           enbUeS1apId,
    const ByteBuffer&  nasPdu,
    uint32_t           plmnId,
    uint16_t           tac,
    uint32_t           cellId,        ///< 28-bit ECGI cell identity
    uint8_t            rrcEstCause    ///< CauseRadioNetwork enum value
);

/// Uplink NAS Transport  (TS 36.413 §8.6.2.3)
ByteBuffer s1ap_encode_UplinkNASTransport(
    uint32_t           mmeUeS1apId,
    uint32_t           enbUeS1apId,
    const ByteBuffer&  nasPdu,
    uint32_t           plmnId,
    uint16_t           tac,
    uint32_t           cellId
);

/// Downlink NAS Transport  (TS 36.413 §8.6.2.2)
ByteBuffer s1ap_encode_DownlinkNASTransport(
    uint32_t           mmeUeS1apId,
    uint32_t           enbUeS1apId,
    const ByteBuffer&  nasPdu
);

/// Initial Context Setup Response  (TS 36.413 §8.3.1)
ByteBuffer s1ap_encode_InitialContextSetupResponse(
    uint32_t                  mmeUeS1apId,
    uint32_t                  enbUeS1apId,
    const std::vector<ERAB>&  erabs   ///< admitted E-RABs with local TEID
);

/// UE Context Release Request  (TS 36.413 §8.3.3)
/// causeGroup: 0=radioNetwork 1=transport 2=nas 3=protocol 4=misc
ByteBuffer s1ap_encode_UEContextReleaseRequest(
    uint32_t mmeUeS1apId,
    uint32_t enbUeS1apId,
    uint8_t  causeGroup,
    uint8_t  causeValue
);

/// UE Context Release Complete  (TS 36.413 §8.3.4)
ByteBuffer s1ap_encode_UEContextReleaseComplete(
    uint32_t mmeUeS1apId,
    uint32_t enbUeS1apId
);

/// E-RAB Setup Response  (TS 36.413 §8.4.1)
ByteBuffer s1ap_encode_ERABSetupResponse(
    uint32_t                  mmeUeS1apId,
    uint32_t                  enbUeS1apId,
    const std::vector<ERAB>&  setupErabs,
    const std::vector<uint8_t>& failedErabIds   ///< empty → all succeeded
);

/// E-RAB Release Response  (TS 36.413 §8.4.3)
ByteBuffer s1ap_encode_ERABReleaseResponse(
    uint32_t                    mmeUeS1apId,
    uint32_t                    enbUeS1apId,
    const std::vector<uint8_t>& releasedErabIds
);

/// Path Switch Request  (TS 36.413 §8.5.4)
ByteBuffer s1ap_encode_PathSwitchRequest(
    uint32_t                  mmeUeS1apId,
    uint32_t                  enbUeS1apId,
    uint32_t                  targetEnbId,
    const std::vector<ERAB>&  erabs,
    uint32_t                  plmnId,
    uint32_t                  cellId,
    uint16_t                  tac
);

/// Handover Required  (TS 36.413 §8.5.2.1 — S1-based HO source side)
ByteBuffer s1ap_encode_HandoverRequired(
    uint32_t           mmeUeS1apId,
    uint32_t           enbUeS1apId,
    uint32_t           targetEnbId,    ///< target eNB ID (macro)
    uint32_t           targetPlmnId,
    uint32_t           targetCellId,
    const ByteBuffer&  rrcContainer    ///< Source-to-Target transparent container
);

/// Handover Notify  (TS 36.413 §8.5.1 — target side, after UE arrives)
ByteBuffer s1ap_encode_HandoverNotify(
    uint32_t mmeUeS1apId,
    uint32_t enbUeS1apId,
    uint32_t plmnId,
    uint16_t tac,
    uint32_t cellId
);

// ── Decode ────────────────────────────────────────────────────────────────────

/// APER-decode bytes into an S1AP-PDU.
/// Returns nullptr on failure. The caller MUST call s1ap_pdu_free().
S1APPduHandle s1ap_decode(const void* buf, size_t len);

/// Extract procedure code from a decoded PDU (returns -1 on error).
int  s1ap_procedure_code(S1APPduHandle pdu);

/// Extract the raw NAS-PDU OCTET STRING from an UplinkNASTransport
/// or DownlinkNASTransport message. Returns empty ByteBuffer on failure.
ByteBuffer s1ap_extract_nas_pdu(S1APPduHandle pdu);

}  // namespace rbs::lte

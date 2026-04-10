#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// X2AP APER codec  (TS 36.423, X.691 APER)
//
// Encoders build X2AP-PDU C structures and encode them using the asn1c-mouse
// generated library (rbs_asn1_x2ap, ATS_UNALIGNED_BASIC_PER).
// Decoded PDUs are returned as opaque handles (X2APPduHandle) and freed via
// x2ap_pdu_free().
//
// References:  TS 36.423 §8 — Elementary Procedures
// ─────────────────────────────────────────────────────────────────────────────
#include "../common/types.h"
#include "x2ap_interface.h"   // X2HORequest, X2HORequestAck, SNStatusItem, ERAB
#include <cstdint>
#include <vector>

// Forward-declare the C PDU type so callers can hold a pointer without pulling
// in all of the generated headers.
extern "C" { struct X2AP_PDU; }

namespace rbs::lte {

// ── Opaque handle for a decoded X2AP PDU ─────────────────────────────────────
using X2APPduHandle = X2AP_PDU*;

// ── Encoders ─────────────────────────────────────────────────────────────────
// TS 36.423 §8.3.4 — X2 Setup
ByteBuffer x2ap_encode_X2SetupRequest (uint32_t enbId, uint32_t plmnId,
                                       uint16_t pci, uint32_t earfcnDl,
                                       uint16_t tac);

ByteBuffer x2ap_encode_X2SetupResponse(uint32_t enbId, uint32_t plmnId,
                                       uint16_t pci, uint32_t earfcnDl,
                                       uint16_t tac);

// TS 36.423 §8.5.1 — Handover Preparation
ByteBuffer x2ap_encode_HandoverRequest(uint32_t srcUeX2apId,
                                       uint32_t mmeUeS1apId,
                                       uint8_t  causeType,
                                       uint32_t plmnId,
                                       const std::vector<ERAB>& erabs,
                                       const ByteBuffer& rrcContainer);

ByteBuffer x2ap_encode_HandoverRequestAck(uint32_t srcUeX2apId,
                                          uint32_t tgtUeX2apId,
                                          const std::vector<ERAB>& admittedErabs,
                                          const std::vector<uint8_t>& notAdmitted,
                                          const ByteBuffer& rrcContainer);

// TS 36.423 §8.5.3 — SN Status Transfer
ByteBuffer x2ap_encode_SNStatusTransfer(uint32_t srcUeX2apId,
                                        uint32_t tgtUeX2apId,
                                        const std::vector<SNStatusItem>& items);

// TS 36.423 §8.5.5 — UE Context Release
ByteBuffer x2ap_encode_UEContextRelease(uint32_t srcUeX2apId,
                                        uint32_t tgtUeX2apId);

// ── Decoder / utilities ───────────────────────────────────────────────────────
/// Decode raw APER bytes into an X2AP-PDU.  Returns nullptr on failure.
X2APPduHandle x2ap_decode   (const uint8_t* data, size_t size);
void          x2ap_pdu_free (X2APPduHandle pdu);
int           x2ap_procedure_code(X2APPduHandle pdu);

} // namespace rbs::lte

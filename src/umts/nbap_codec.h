#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NBAP APER codec helpers  (TS 25.433, APER per X.691)
//
// Encodes NBAP InitiatingMessage PDUs as APER-encoded ByteBuffers using
// direct bit manipulation.  Full asn1c code generation for the NBAP spec
// (>1 MB) is deferred; encoding is based on the wire format derived from
// the spec.
//
// PDU structure (all messages are InitiatingMessage):
//
//   NBAP-PDU  ::= CHOICE { initiatingMessage ... }   — outer CHOICE [0]
//   InitiatingMessage ::= SEQUENCE {
//       procedureID         ProcedureID (procedureCode + ddMode)
//       criticality         ENUMERATED { reject(0), ignore(1), notify(2) }
//       messageDiscriminator ENUMERATED { common(0), dedicated(1) }
//       transactionID       CHOICE { shortTransActionId INTEGER(0..127) |
//                                    longTransActionId  INTEGER(0..32767) }
//       value               OPEN TYPE (SEQUENCE of ProtocolIE-Fields)
//   }
//
//   ProtocolIE-Field ::= SEQUENCE {
//       id          INTEGER(0..65535)   — unconstrained 16-bit
//       criticality ENUMERATED { reject, ignore, notify }
//       value       OPEN TYPE           — length-determinant + encoded bytes
//   }
// ─────────────────────────────────────────────────────────────────────────────
#include "../common/types.h"
#include <cstdint>

namespace rbs::umts {

// ── Encode helpers ────────────────────────────────────────────────────────────

/// NBAP Cell Setup Request FDD  (TS 25.433 §8.3.6.1, procedure id=5, ddMode=fdd)
/// localCellId : Local-Cell-ID  INTEGER (0..268435455)
/// cId         : C-ID           INTEGER (0..65535)
/// cfgGenId    : ConfigurationGenerationID INTEGER (0..255)  — use 1 for new cell
/// uarfcnUl    : UARFCN for Nu  INTEGER (0..16383)
/// uarfcnDl    : UARFCN for Nd  INTEGER (0..16383)
/// maxTxPower  : MaximumTransmissionPower INTEGER (0..500, unit 0.1dBm)
/// primaryScrCode : PrimaryScramblingCode INTEGER (0..511)
ByteBuffer nbap_encode_CellSetupRequestFDD(
    uint32_t localCellId,
    uint16_t cId,
    uint8_t  cfgGenId,
    uint16_t uarfcnUl,
    uint16_t uarfcnDl,
    uint16_t maxTxPower,
    uint16_t primaryScrCode,
    uint8_t  txId = 0
);

/// NBAP Radio Link Setup Request FDD  (TS 25.433 §8.1.1, procedure id=27, ddMode=fdd)
/// crncCtxId   : CRNC-CommunicationContextID INTEGER (0..1048575)
ByteBuffer nbap_encode_RadioLinkSetupRequestFDD(
    uint32_t crncCtxId,
    uint8_t  txId = 0
);

/// NBAP Radio Link Addition Request FDD  (TS 25.433 §8.1.4, procedure id=23, ddMode=fdd)
/// nodeBCtxId  : NodeB-CommunicationContextID INTEGER (0..1048575)
ByteBuffer nbap_encode_RadioLinkAdditionRequestFDD(
    uint32_t nodeBCtxId,
    uint8_t  txId = 0
);

/// NBAP Radio Link Deletion Request  (TS 25.433 §8.1.6, procedure id=24, ddMode=common)
/// nodeBCtxId  : NodeB-CommunicationContextID INTEGER (0..1048575)
/// crncCtxId   : CRNC-CommunicationContextID  INTEGER (0..1048575)
ByteBuffer nbap_encode_RadioLinkDeletionRequest(
    uint32_t nodeBCtxId,
    uint32_t crncCtxId,
    uint8_t  txId = 0
);

/// NBAP Reset Request  (TS 25.433 §8.7.1, procedure id=13, ddMode=common)
/// Encodes ResetIndicator as nodeB (NULL alternative).
ByteBuffer nbap_encode_ResetRequest(uint8_t txId = 0);

/// NBAP Audit Request  (TS 25.433 §8.6, procedure id=0, ddMode=common)
/// startOfAuditSeq = 0 → start-of-audit-sequence; anything else → not-start
ByteBuffer nbap_encode_AuditRequest(bool startOfAuditSeq = true, uint8_t txId = 0);

} // namespace rbs::umts

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

// ─────────────────────────────────────────────────────────────────────────────
// DCH / HSDPA extensions  (TS 25.433 §8.3.2, §8.1.5, §8.1.6, §8.3.15)
// ─────────────────────────────────────────────────────────────────────────────

/// Channel types for CommonTransportChannelSetup
enum class NBAPCommonChannel : uint8_t {
    FACH = 0,   ///< Forward Access Channel
    PCH  = 1,   ///< Paging Channel
    RACH = 2,   ///< Random Access Channel
};

/// NBAP Common Transport Channel Setup Request FDD  (TS 25.433 §8.3.2, proc id=4)
/// Sets up a common transport channel (FACH / PCH / RACH) in the cell.
/// localCellId : Local-Cell-ID
/// channelType : FACH | PCH | RACH
ByteBuffer nbap_encode_CommonTransportChannelSetupRequest(
    uint32_t          localCellId,
    NBAPCommonChannel channelType,
    uint8_t           txId = 0
);

/// NBAP Radio Link Reconfiguration Prepare  (TS 25.433 §8.1.5, proc id=26, ddMode=fdd)
/// Prepared DCH reconfiguration for a UE (change spreading factor / bitrate).
/// crncCtxId : CRNC-CommunicationContextID
/// newSf     : New spreading factor (encoded as UE capability index 0–6 for SF4..256)
ByteBuffer nbap_encode_RadioLinkReconfigurePrepare(
    uint32_t crncCtxId,
    SF       newSf,
    uint8_t  txId = 0
);

/// NBAP Radio Link Reconfiguration Commit  (TS 25.433 §8.1.6, proc id=25, ddMode=fdd)
/// Commits a previously prepared DCH reconfiguration.
/// crncCtxId : CRNC-CommunicationContextID
ByteBuffer nbap_encode_RadioLinkReconfigureCommit(
    uint32_t crncCtxId,
    uint8_t  txId = 0
);

/// NBAP Radio Link Setup Request FDD with HS-DSCH info  (TS 25.433 §8.3.15)
/// Sets up a radio link and attaches an HS-DSCH MAC-d flow (HSDPA bearer).
/// crncCtxId   : CRNC-CommunicationContextID
/// hsDschCodes : Number of HS-DSCH channelisation codes (1..15)
/// hsDschPower : HS-DSCH maximum power (0.1 dBm units, 0..500)
ByteBuffer nbap_encode_RadioLinkSetupRequestFDD_HSDPA(
    uint32_t crncCtxId,
    uint8_t  hsDschCodes = 5,
    uint16_t hsDschPower = 300,
    uint8_t  txId = 0
);


// ─────────────────────────────────────────────────────────────────────────────
// E-DCH (HSUPA) extensions  (TS 25.433 §8.1.1.3, TS 25.309)
// ─────────────────────────────────────────────────────────────────────────────

/// E-DCH Transmission Time Interval
enum class EDCHTTI : uint8_t {
    TTI_2MS  = 0,   ///< 2 ms TTI (sub-frame granularity)
    TTI_10MS = 1,   ///< 10 ms TTI (one radio frame)
};

/// NBAP Radio Link Setup Request FDD with E-DCH info  (TS 25.433 §8.1.1.3)
/// Sets up a radio link and attaches an E-DCH MAC-d flow (HSUPA bearer).
/// crncCtxId    : CRNC-CommunicationContextID
/// tti          : E-DCH TTI (2 ms or 10 ms)
/// maxBitrateIdx: E-DCH max bitrate index (0..7 → 64 kbps..5.76 Mbps)
ByteBuffer nbap_encode_RadioLinkSetupRequestFDD_EDCH(
    uint32_t crncCtxId,
    EDCHTTI  tti           = EDCHTTI::TTI_10MS,
    uint8_t  maxBitrateIdx = 4,   // index 4 → ~2 Mbps peak UL
    uint8_t  txId          = 0
);

} // namespace rbs::umts

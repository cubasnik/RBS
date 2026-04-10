// ─────────────────────────────────────────────────────────────────────────────
// X2AP APER codec  (TS 36.423, X.691 APER)
//
// Encodes/decodes X2AP-PDU using asn1c-mouse generated library (rbs_asn1_x2ap).
// All choice members in X2AP_PDU_t are POINTERS — must calloc before access.
// ─────────────────────────────────────────────────────────────────────────────

// Include C++ math before generated headers to avoid _logb redeclaration
// conflict on MSVC.
#include <cmath>

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif

// Generated X2AP headers (C linkage)
#include "../../src/generated/x2ap/X2AP-PDU.h"
#include "../../src/generated/x2ap/InitiatingMessage.h"
#include "../../src/generated/x2ap/SuccessfulOutcome.h"
#include "../../src/generated/x2ap/ProcedureCode.h"
#include "../../src/generated/x2ap/Criticality.h"
#include "../../src/generated/x2ap/ProtocolIE-Field.h"
#include "../../src/generated/x2ap/ProtocolIE-Container.h"
#include "../../src/generated/x2ap/ProtocolIE-Single-Container.h"
#include "../../src/generated/x2ap/ProtocolIE-ID.h"
#include "../../src/generated/x2ap/GlobalENB-ID.h"
#include "../../src/generated/x2ap/ENB-ID.h"
#include "../../src/generated/x2ap/PLMN-Identity.h"
#include "../../src/generated/x2ap/ServedCells.h"
#include "../../src/generated/x2ap/ServedCell-Information.h"
#include "../../src/generated/x2ap/ECGI.h"
#include "../../src/generated/x2ap/EUTRANCellIdentifier.h"
#include "../../src/generated/x2ap/BroadcastPLMNs-Item.h"
#include "../../src/generated/x2ap/EUTRA-Mode-Info.h"
#include "../../src/generated/x2ap/FDD-Info.h"
#include "../../src/generated/x2ap/Transmission-Bandwidth.h"
#include "../../src/generated/x2ap/EARFCN.h"
#include "../../src/generated/x2ap/TAC.h"
#include "../../src/generated/x2ap/PCI.h"
#include "../../src/generated/x2ap/X2SetupRequest.h"
#include "../../src/generated/x2ap/X2SetupResponse.h"
#include "../../src/generated/x2ap/HandoverRequest.h"
#include "../../src/generated/x2ap/HandoverRequestAcknowledge.h"
#include "../../src/generated/x2ap/SNStatusTransfer.h"
#include "../../src/generated/x2ap/UEContextRelease.h"
#include "../../src/generated/x2ap/UE-ContextInformation.h"
#include "../../src/generated/x2ap/UESecurityCapabilities.h"
#include "../../src/generated/x2ap/EncryptionAlgorithms.h"
#include "../../src/generated/x2ap/IntegrityProtectionAlgorithms.h"
#include "../../src/generated/x2ap/AS-SecurityInformation.h"
#include "../../src/generated/x2ap/Key-eNodeB-Star.h"
#include "../../src/generated/x2ap/NextHopChainingCount.h"
#include "../../src/generated/x2ap/UEAggregateMaximumBitRate.h"
#include "../../src/generated/x2ap/BitRate.h"
#include "../../src/generated/x2ap/E-RABs-ToBeSetup-List.h"
#include "../../src/generated/x2ap/E-RABs-ToBeSetup-Item.h"
#include "../../src/generated/x2ap/E-RAB-Level-QoS-Parameters.h"
#include "../../src/generated/x2ap/AllocationAndRetentionPriority.h"
#include "../../src/generated/x2ap/GTPtunnelEndpoint.h"
#include "../../src/generated/x2ap/GTP-TEI.h"
#include "../../src/generated/x2ap/TransportLayerAddress.h"
#include "../../src/generated/x2ap/E-RABs-Admitted-Item.h"
#include "../../src/generated/x2ap/E-RABs-Admitted-List.h"
#include "../../src/generated/x2ap/E-RABs-SubjectToStatusTransfer-Item.h"
#include "../../src/generated/x2ap/E-RABs-SubjectToStatusTransfer-List.h"
#include "../../src/generated/x2ap/COUNTvalue.h"
#include "../../src/generated/x2ap/PDCP-SN.h"
#include "../../src/generated/x2ap/HFN.h"
#include "../../src/generated/x2ap/RRC-Context.h"
#include "../../src/generated/x2ap/Cause.h"
#include "../../src/generated/x2ap/CauseRadioNetwork.h"
#include "../../src/generated/x2ap/CauseTransport.h"
#include "../../src/generated/x2ap/CauseProtocol.h"
#include "../../src/generated/x2ap/TargeteNBtoSource-eNBTransparentContainer.h"
#include "../../src/generated/x2ap/UE-X2AP-ID.h"
#include "../../src/generated/x2ap/asn_application.h"

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "x2ap_codec.h"
#include "../common/logger.h"

#include <cstdlib>
#include <cstring>

namespace rbs::lte {

// ═════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═════════════════════════════════════════════════════════════════════════════

/// Encode a fully constructed PDU to APER bytes.
static ByteBuffer encode_pdu(X2AP_PDU_t& pdu)
{
    asn_encode_to_new_buffer_result_t res =
        asn_encode_to_new_buffer(nullptr, ATS_UNALIGNED_BASIC_PER,
                                 &asn_DEF_X2AP_PDU, &pdu);
    if (!res.buffer || res.result.encoded < 0) {
        RBS_LOG_ERROR("X2AP-CODEC", "APER encode failed (errno={})", errno);
        free(res.buffer);
        return {};
    }
    ByteBuffer out(static_cast<const uint8_t*>(res.buffer),
                   static_cast<const uint8_t*>(res.buffer) + res.result.encoded);
    free(res.buffer);
    return out;
}

/// Build a 3-byte X2AP PLMN identity from uint32 packed as 0x00MCCMNC.
static PLMN_Identity_t make_plmn(uint32_t plmnId)
{
    PLMN_Identity_t p{};
    p.buf  = static_cast<uint8_t*>(malloc(3));
    p.size = 3;
    // MCC+MNC encoding: same as S1AP PLMNidentity
    p.buf[0] = static_cast<uint8_t>((plmnId >> 16) & 0xFF);
    p.buf[1] = static_cast<uint8_t>((plmnId >>  8) & 0xFF);
    p.buf[2] = static_cast<uint8_t>( plmnId        & 0xFF);
    return p;
}

/// Fill a 20-bit macro eNB-ID BIT_STRING (3 bytes, 4 unused bits in last byte).
static void fill_macro_enb_id(ENB_ID_t& eid, uint32_t enbId)
{
    eid.present = ENB_ID_PR_macro_eNB_ID;
    eid.choice.macro_eNB_ID.buf         = static_cast<uint8_t*>(malloc(3));
    eid.choice.macro_eNB_ID.size        = 3;
    eid.choice.macro_eNB_ID.bits_unused = 4;
    uint32_t v = (enbId & 0x000FFFFF) << 4;
    eid.choice.macro_eNB_ID.buf[0] = static_cast<uint8_t>(v >> 16);
    eid.choice.macro_eNB_ID.buf[1] = static_cast<uint8_t>(v >>  8);
    eid.choice.macro_eNB_ID.buf[2] = static_cast<uint8_t>(v);
}

/// Build a 2-byte BIT_STRING type for GTP-TEI (32 bit big-endian OCTET_STRING).
static GTP_TEI_t make_teid(uint32_t teid)
{
    GTP_TEI_t t{};
    t.buf  = static_cast<uint8_t*>(malloc(4));
    t.size = 4;
    t.buf[0] = static_cast<uint8_t>(teid >> 24);
    t.buf[1] = static_cast<uint8_t>(teid >> 16);
    t.buf[2] = static_cast<uint8_t>(teid >>  8);
    t.buf[3] = static_cast<uint8_t>(teid);
    return t;
}

/// Build a TransportLayerAddress BIT_STRING from a packed IPv4 address.
static TransportLayerAddress_t make_tla(uint32_t ipv4)
{
    TransportLayerAddress_t t{};
    t.buf         = static_cast<uint8_t*>(malloc(4));
    t.size        = 4;
    t.bits_unused = 0;
    t.buf[0] = static_cast<uint8_t>(ipv4 >> 24);
    t.buf[1] = static_cast<uint8_t>(ipv4 >> 16);
    t.buf[2] = static_cast<uint8_t>(ipv4 >>  8);
    t.buf[3] = static_cast<uint8_t>(ipv4);
    return t;
}

/// Allocate and fill a GTPtunnelEndpoint.
static GTPtunnelEndpoint_t* make_gtp_ep(uint32_t ipv4, uint32_t teid)
{
    auto* ep = static_cast<GTPtunnelEndpoint_t*>(calloc(1, sizeof(GTPtunnelEndpoint_t)));
    ep->transportLayerAddress = make_tla(ipv4);
    ep->gTP_TEID              = make_teid(teid);
    return ep;
}

/// Allocate a long (64-bit) INTEGER from uint64_t.
static void set_integer_u64(INTEGER_t& dst, uint64_t val)
{
    // Encode as big-endian variable-length integer (DER/BER style)
    uint8_t buf[8];
    int len = 0;
    uint64_t v = val;
    do {
        buf[7 - len++] = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    } while (v);
    dst.buf  = static_cast<uint8_t*>(malloc(len));
    dst.size = static_cast<int>(len);
    memcpy(dst.buf, buf + 8 - len, len);
}

// ═════════════════════════════════════════════════════════════════════════════
// X2 Setup Request  (TS 36.423 §8.3.4)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer x2ap_encode_X2SetupRequest(uint32_t enbId, uint32_t plmnId,
                                      uint16_t pci, uint32_t earfcnDl,
                                      uint16_t tac)
{
    X2AP_PDU_t pdu{};
    pdu.present = X2AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_x2Setup;
    im->criticality   = Criticality_reject;
    im->value.present = InitiatingMessage__value_PR_X2SetupRequest;
    auto& req = im->value.choice.X2SetupRequest;

    auto* ies = static_cast<ProtocolIE_Container_111P10_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_111P10_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // ── IE: GlobalENB-ID (id=21, crit=reject) ────────────────────────────────
    {
        auto* ie = static_cast<X2SetupRequest_IEs_t*>(calloc(1, sizeof(X2SetupRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_GlobalENB_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = X2SetupRequest_IEs__value_PR_GlobalENB_ID;
        ie->value.choice.GlobalENB_ID.pLMN_Identity = make_plmn(plmnId);
        ie->value.choice.GlobalENB_ID.eNB_ID =
            static_cast<ENB_ID_t*>(calloc(1, sizeof(ENB_ID_t)));
        fill_macro_enb_id(*ie->value.choice.GlobalENB_ID.eNB_ID, enbId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: ServedCells (id=20, crit=reject) — one FDD cell ──────────────────
    {
        auto* ie = static_cast<X2SetupRequest_IEs_t*>(calloc(1, sizeof(X2SetupRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_ServedCells;
        ie->criticality   = Criticality_reject;
        ie->value.present = X2SetupRequest_IEs__value_PR_ServedCells;
        auto& cells       = ie->value.choice.ServedCells;

        auto* member = static_cast<ServedCells__Member*>(calloc(1, sizeof(ServedCells__Member)));
        auto* info   = static_cast<ServedCell_Information_t*>(
                           calloc(1, sizeof(ServedCell_Information_t)));
        member->servedCellInfo = info;

        info->pCI  = pci;

        // TAC (2 bytes)
        info->tAC.buf  = static_cast<uint8_t*>(malloc(2));
        info->tAC.size = 2;
        info->tAC.buf[0] = static_cast<uint8_t>(tac >> 8);
        info->tAC.buf[1] = static_cast<uint8_t>(tac);

        // cellId (ECGI)
        info->cellId = static_cast<ECGI_t*>(calloc(1, sizeof(ECGI_t)));
        info->cellId->pLMN_Identity = make_plmn(plmnId);
        info->cellId->eUTRANcellIdentifier.buf         = static_cast<uint8_t*>(malloc(4));
        info->cellId->eUTRANcellIdentifier.size        = 4;
        info->cellId->eUTRANcellIdentifier.bits_unused = 4;
        uint32_t eci = (enbId << 8) & 0x0FFFFFFF;
        info->cellId->eUTRANcellIdentifier.buf[0] = static_cast<uint8_t>(eci >> 24);
        info->cellId->eUTRANcellIdentifier.buf[1] = static_cast<uint8_t>(eci >> 16);
        info->cellId->eUTRANcellIdentifier.buf[2] = static_cast<uint8_t>(eci >>  8);
        info->cellId->eUTRANcellIdentifier.buf[3] = static_cast<uint8_t>(eci);

        // BroadcastPLMNs (one entry)
        info->broadcastPLMNs = static_cast<BroadcastPLMNs_Item_t*>(
                                    calloc(1, sizeof(BroadcastPLMNs_Item_t)));
        auto* plmn_entry = static_cast<PLMN_Identity_t*>(calloc(1, sizeof(PLMN_Identity_t)));
        *plmn_entry = make_plmn(plmnId);
        ASN_SEQUENCE_ADD(&info->broadcastPLMNs->list, plmn_entry);

        // EUTRA-Mode-Info: FDD with earfcnDl, bw100
        info->eUTRA_Mode_Info = static_cast<EUTRA_Mode_Info_t*>(
                                    calloc(1, sizeof(EUTRA_Mode_Info_t)));
        info->eUTRA_Mode_Info->present = EUTRA_Mode_Info_PR_fDD;
        auto* fdd = static_cast<FDD_Info_t*>(calloc(1, sizeof(FDD_Info_t)));
        info->eUTRA_Mode_Info->choice.fDD = fdd;
        fdd->dL_EARFCN                    = earfcnDl;
        fdd->uL_EARFCN                    = earfcnDl + 18000; // typical UL offset
        fdd->dL_Transmission_Bandwidth    = Transmission_Bandwidth_bw100;
        fdd->uL_Transmission_Bandwidth    = Transmission_Bandwidth_bw100;

        ASN_SEQUENCE_ADD(&cells.list, member);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_X2AP_PDU, &pdu);
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// X2 Setup Response  (TS 36.423 §8.3.4)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer x2ap_encode_X2SetupResponse(uint32_t enbId, uint32_t plmnId,
                                       uint16_t pci, uint32_t earfcnDl,
                                       uint16_t tac)
{
    X2AP_PDU_t pdu{};
    pdu.present = X2AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
                                       calloc(1, sizeof(SuccessfulOutcome_t)));
    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_x2Setup;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_X2SetupResponse;
    auto& resp = so->value.choice.X2SetupResponse;

    auto* ies = static_cast<ProtocolIE_Container_111P11_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_111P11_t)));
    resp.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // ── IE: GlobalENB-ID (id=21, crit=reject) ────────────────────────────────
    {
        auto* ie = static_cast<X2SetupResponse_IEs_t*>(calloc(1, sizeof(X2SetupResponse_IEs_t)));
        ie->id            = ProtocolIE_ID_id_GlobalENB_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = X2SetupResponse_IEs__value_PR_GlobalENB_ID;
        ie->value.choice.GlobalENB_ID.pLMN_Identity = make_plmn(plmnId);
        ie->value.choice.GlobalENB_ID.eNB_ID =
            static_cast<ENB_ID_t*>(calloc(1, sizeof(ENB_ID_t)));
        fill_macro_enb_id(*ie->value.choice.GlobalENB_ID.eNB_ID, enbId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: ServedCells (id=20, crit=reject) — one FDD cell ──────────────────
    {
        auto* ie = static_cast<X2SetupResponse_IEs_t*>(calloc(1, sizeof(X2SetupResponse_IEs_t)));
        ie->id            = ProtocolIE_ID_id_ServedCells;
        ie->criticality   = Criticality_reject;
        ie->value.present = X2SetupResponse_IEs__value_PR_ServedCells;
        auto& cells       = ie->value.choice.ServedCells;

        auto* member = static_cast<ServedCells__Member*>(calloc(1, sizeof(ServedCells__Member)));
        auto* info   = static_cast<ServedCell_Information_t*>(
                           calloc(1, sizeof(ServedCell_Information_t)));
        member->servedCellInfo = info;
        info->pCI = pci;

        info->tAC.buf  = static_cast<uint8_t*>(malloc(2));
        info->tAC.size = 2;
        info->tAC.buf[0] = static_cast<uint8_t>(tac >> 8);
        info->tAC.buf[1] = static_cast<uint8_t>(tac);

        info->cellId = static_cast<ECGI_t*>(calloc(1, sizeof(ECGI_t)));
        info->cellId->pLMN_Identity = make_plmn(plmnId);
        info->cellId->eUTRANcellIdentifier.buf         = static_cast<uint8_t*>(malloc(4));
        info->cellId->eUTRANcellIdentifier.size        = 4;
        info->cellId->eUTRANcellIdentifier.bits_unused = 4;
        uint32_t eci = (enbId << 8) & 0x0FFFFFFF;
        info->cellId->eUTRANcellIdentifier.buf[0] = static_cast<uint8_t>(eci >> 24);
        info->cellId->eUTRANcellIdentifier.buf[1] = static_cast<uint8_t>(eci >> 16);
        info->cellId->eUTRANcellIdentifier.buf[2] = static_cast<uint8_t>(eci >>  8);
        info->cellId->eUTRANcellIdentifier.buf[3] = static_cast<uint8_t>(eci);

        info->broadcastPLMNs = static_cast<BroadcastPLMNs_Item_t*>(
                                    calloc(1, sizeof(BroadcastPLMNs_Item_t)));
        auto* plmn_entry = static_cast<PLMN_Identity_t*>(calloc(1, sizeof(PLMN_Identity_t)));
        *plmn_entry = make_plmn(plmnId);
        ASN_SEQUENCE_ADD(&info->broadcastPLMNs->list, plmn_entry);

        info->eUTRA_Mode_Info = static_cast<EUTRA_Mode_Info_t*>(
                                    calloc(1, sizeof(EUTRA_Mode_Info_t)));
        info->eUTRA_Mode_Info->present = EUTRA_Mode_Info_PR_fDD;
        auto* fdd = static_cast<FDD_Info_t*>(calloc(1, sizeof(FDD_Info_t)));
        info->eUTRA_Mode_Info->choice.fDD = fdd;
        fdd->dL_EARFCN                    = earfcnDl;
        fdd->uL_EARFCN                    = earfcnDl + 18000;
        fdd->dL_Transmission_Bandwidth    = Transmission_Bandwidth_bw100;
        fdd->uL_Transmission_Bandwidth    = Transmission_Bandwidth_bw100;

        ASN_SEQUENCE_ADD(&cells.list, member);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_X2AP_PDU, &pdu);
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// Handover Request  (TS 36.423 §8.5.1)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer x2ap_encode_HandoverRequest(uint32_t srcUeX2apId,
                                       uint32_t mmeUeS1apId,
                                       uint8_t  causeType,
                                       uint32_t plmnId,
                                       const std::vector<ERAB>& erabs,
                                       const ByteBuffer& rrcContainer)
{
    X2AP_PDU_t pdu{};
    (void)plmnId;   // reserved for future PLMN-scoped filtering
    pdu.present = X2AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_handoverPreparation;
    im->criticality   = Criticality_reject;
    im->value.present = InitiatingMessage__value_PR_HandoverRequest;
    auto& req = im->value.choice.HandoverRequest;

    auto* ies = static_cast<ProtocolIE_Container_111P0_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_111P0_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // ── IE: Old-eNB-UE-X2AP-ID (id=10, crit=reject) ─────────────────────────
    {
        auto* ie = static_cast<HandoverRequest_IEs_t*>(calloc(1, sizeof(HandoverRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_Old_eNB_UE_X2AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverRequest_IEs__value_PR_UE_X2AP_ID;
        ie->value.choice.UE_X2AP_ID = static_cast<UE_X2AP_ID_t>(srcUeX2apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: Cause (id=5, crit=ignore) ────────────────────────────────────────
    {
        auto* ie = static_cast<HandoverRequest_IEs_t*>(calloc(1, sizeof(HandoverRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_Cause;
        ie->criticality   = Criticality_ignore;
        ie->value.present = HandoverRequest_IEs__value_PR_Cause;
        switch (causeType) {
            case 1:
                ie->value.choice.Cause.present = Cause_PR_transport;
                ie->value.choice.Cause.choice.transport = CauseTransport_transport_resource_unavailable;
                break;
            case 2:
                ie->value.choice.Cause.present = Cause_PR_protocol;
                ie->value.choice.Cause.choice.protocol = CauseProtocol_unspecified;
                break;
            default:
                ie->value.choice.Cause.present = Cause_PR_radioNetwork;
                ie->value.choice.Cause.choice.radioNetwork =
                    CauseRadioNetwork_handover_desirable_for_radio_reasons;
                break;
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: UE-ContextInformation (id=14, crit=reject) ───────────────────────
    {
        auto* ie = static_cast<HandoverRequest_IEs_t*>(calloc(1, sizeof(HandoverRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_UE_ContextInformation;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverRequest_IEs__value_PR_UE_ContextInformation;

        auto* ctx = static_cast<UE_ContextInformation_t*>(
                        calloc(1, sizeof(UE_ContextInformation_t)));
        ie->value.choice.UE_ContextInformation = ctx;

        ctx->mME_UE_S1AP_ID = static_cast<UE_S1AP_ID_t>(mmeUeS1apId);

        // UE Security Capabilities (mandatory) — zero/default values
        ctx->uESecurityCapabilities = static_cast<UESecurityCapabilities_t*>(
                                          calloc(1, sizeof(UESecurityCapabilities_t)));
        auto& sec = *ctx->uESecurityCapabilities;
        sec.encryptionAlgorithms.buf         = static_cast<uint8_t*>(calloc(2, 1));
        sec.encryptionAlgorithms.size        = 2;
        sec.encryptionAlgorithms.bits_unused = 0;
        sec.integrityProtectionAlgorithms.buf         = static_cast<uint8_t*>(calloc(2, 1));
        sec.integrityProtectionAlgorithms.size        = 2;
        sec.integrityProtectionAlgorithms.bits_unused = 0;

        // AS Security Information (mandatory) — zero key + NCC=0
        ctx->aS_SecurityInformation = static_cast<AS_SecurityInformation_t*>(
                                          calloc(1, sizeof(AS_SecurityInformation_t)));
        auto& asi = *ctx->aS_SecurityInformation;
        asi.key_eNodeB_star.buf         = static_cast<uint8_t*>(calloc(32, 1)); // 256-bit key
        asi.key_eNodeB_star.size        = 32;
        asi.key_eNodeB_star.bits_unused = 0;
        asi.nextHopChainingCount = 0;

        // UE-Aggregate-Maximum-Bit-Rate (mandatory)
        ctx->uEaggregateMaximumBitRate = static_cast<UEAggregateMaximumBitRate_t*>(
                                             calloc(1, sizeof(UEAggregateMaximumBitRate_t)));
        set_integer_u64(ctx->uEaggregateMaximumBitRate->uEaggregateMaximumBitRateDownlink,
                        1000000000ULL);  // 1 Gbps
        set_integer_u64(ctx->uEaggregateMaximumBitRate->uEaggregateMaximumBitRateUplink,
                        100000000ULL);   // 100 Mbps

        // E-RABs-ToBeSetup-List (mandatory)
        ctx->e_RABs_ToBeSetup_List = static_cast<E_RABs_ToBeSetup_List_t*>(
                                         calloc(1, sizeof(E_RABs_ToBeSetup_List_t)));
        for (const auto& e : erabs) {
            auto* sc = static_cast<ProtocolIE_Single_Container_114P3_t*>(
                           calloc(1, sizeof(ProtocolIE_Single_Container_114P3_t)));
            sc->id            = ProtocolIE_ID_id_E_RABs_ToBeSetup_Item;
            sc->criticality   = Criticality_reject;
            sc->value.present = E_RABs_ToBeSetup_ItemIEs__value_PR_E_RABs_ToBeSetup_Item;
            auto& item        = sc->value.choice.E_RABs_ToBeSetup_Item;
            item.e_RAB_ID     = e.erabId;

            // E-RAB Level QoS Parameters (mandatory)
            item.e_RAB_Level_QoS_Parameters = static_cast<E_RAB_Level_QoS_Parameters_t*>(
                                                  calloc(1, sizeof(E_RAB_Level_QoS_Parameters_t)));
            item.e_RAB_Level_QoS_Parameters->qCI = e.qci;
            item.e_RAB_Level_QoS_Parameters->allocationAndRetentionPriority =
                static_cast<AllocationAndRetentionPriority_t*>(
                    calloc(1, sizeof(AllocationAndRetentionPriority_t)));
            item.e_RAB_Level_QoS_Parameters->allocationAndRetentionPriority->priorityLevel = 1;
            item.e_RAB_Level_QoS_Parameters->allocationAndRetentionPriority->pre_emptionCapability =
                Pre_emptionCapability_shall_not_trigger_pre_emption;
            item.e_RAB_Level_QoS_Parameters->allocationAndRetentionPriority->pre_emptionVulnerability =
                Pre_emptionVulnerability_not_pre_emptable;

            // UL GTP tunnel endpoint (toward SGW)
            item.uL_GTPtunnelEndpoint = make_gtp_ep(e.sgwTunnel.remoteIPv4,
                                                     e.sgwTunnel.teid);

            ASN_SEQUENCE_ADD(&ctx->e_RABs_ToBeSetup_List->list, sc);
        }

        // RRC Context (mandatory, OCTET STRING)
        ctx->rRC_Context.buf  = static_cast<uint8_t*>(malloc(
                                    rrcContainer.empty() ? 1 : rrcContainer.size()));
        ctx->rRC_Context.size = rrcContainer.empty() ? 1 : rrcContainer.size();
        if (!rrcContainer.empty())
            memcpy(ctx->rRC_Context.buf, rrcContainer.data(), rrcContainer.size());

        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_X2AP_PDU, &pdu);
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// Handover Request Acknowledge  (TS 36.423 §8.5.1)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer x2ap_encode_HandoverRequestAck(uint32_t srcUeX2apId,
                                          uint32_t tgtUeX2apId,
                                          const std::vector<ERAB>& admittedErabs,
                                          const std::vector<uint8_t>& notAdmitted,
                                          const ByteBuffer& rrcContainer)
{
    X2AP_PDU_t pdu{};
    (void)notAdmitted;  // not-admitted E-RAB list omitted for simplicity
    pdu.present = X2AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
                                       calloc(1, sizeof(SuccessfulOutcome_t)));
    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_handoverPreparation;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_HandoverRequestAcknowledge;
    auto& ack = so->value.choice.HandoverRequestAcknowledge;

    auto* ies = static_cast<ProtocolIE_Container_111P1_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_111P1_t)));
    ack.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // ── IE: Old-eNB-UE-X2AP-ID (id=10, crit=ignore) ─────────────────────────
    {
        auto* ie = static_cast<HandoverRequestAcknowledge_IEs_t*>(
                       calloc(1, sizeof(HandoverRequestAcknowledge_IEs_t)));
        ie->id            = ProtocolIE_ID_id_Old_eNB_UE_X2AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = HandoverRequestAcknowledge_IEs__value_PR_UE_X2AP_ID;
        ie->value.choice.UE_X2AP_ID = static_cast<UE_X2AP_ID_t>(srcUeX2apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: New-eNB-UE-X2AP-ID (id=9, crit=ignore) ──────────────────────────
    {
        auto* ie = static_cast<HandoverRequestAcknowledge_IEs_t*>(
                       calloc(1, sizeof(HandoverRequestAcknowledge_IEs_t)));
        ie->id            = ProtocolIE_ID_id_New_eNB_UE_X2AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = HandoverRequestAcknowledge_IEs__value_PR_UE_X2AP_ID_1;
        ie->value.choice.UE_X2AP_ID_1 = static_cast<UE_X2AP_ID_t>(tgtUeX2apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: E-RABs-Admitted-List (id=1, crit=ignore) ─────────────────────────
    if (!admittedErabs.empty()) {
        auto* ie = static_cast<HandoverRequestAcknowledge_IEs_t*>(
                       calloc(1, sizeof(HandoverRequestAcknowledge_IEs_t)));
        ie->id            = ProtocolIE_ID_id_E_RABs_Admitted_List;
        ie->criticality   = Criticality_ignore;
        ie->value.present = HandoverRequestAcknowledge_IEs__value_PR_E_RABs_Admitted_List;
        auto& lst         = ie->value.choice.E_RABs_Admitted_List;

        for (const auto& e : admittedErabs) {
            auto* sc = static_cast<ProtocolIE_Single_Container_114P4_t*>(
                           calloc(1, sizeof(ProtocolIE_Single_Container_114P4_t)));
            sc->id            = 0; // wrapper container, id unused
            sc->criticality   = Criticality_ignore;
            sc->value.present = E_RABs_Admitted_ItemIEs__value_PR_E_RABs_Admitted_Item;
            auto& item        = sc->value.choice.E_RABs_Admitted_Item;
            item.e_RAB_ID     = e.erabId;
            item.uL_GTP_TunnelEndpoint = make_gtp_ep(e.sgwTunnel.remoteIPv4, e.sgwTunnel.teid);
            ASN_SEQUENCE_ADD(&lst.list, sc);
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: TargeteNBtoSource-eNBTransparentContainer (id=12, crit=reject) ───
    if (!rrcContainer.empty()) {
        auto* ie = static_cast<HandoverRequestAcknowledge_IEs_t*>(
                       calloc(1, sizeof(HandoverRequestAcknowledge_IEs_t)));
        ie->id            = ProtocolIE_ID_id_TargeteNBtoSource_eNBTransparentContainer;
        ie->criticality   = Criticality_reject;
        ie->value.present =
            HandoverRequestAcknowledge_IEs__value_PR_TargeteNBtoSource_eNBTransparentContainer;
        auto& tc  = ie->value.choice.TargeteNBtoSource_eNBTransparentContainer;
        tc.buf    = static_cast<uint8_t*>(malloc(rrcContainer.size()));
        tc.size   = rrcContainer.size();
        memcpy(tc.buf, rrcContainer.data(), rrcContainer.size());
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_X2AP_PDU, &pdu);
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// SN Status Transfer  (TS 36.423 §8.5.3)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer x2ap_encode_SNStatusTransfer(uint32_t srcUeX2apId,
                                        uint32_t tgtUeX2apId,
                                        const std::vector<SNStatusItem>& items)
{
    X2AP_PDU_t pdu{};
    pdu.present = X2AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_snStatusTransfer;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_SNStatusTransfer;
    auto& msg = im->value.choice.SNStatusTransfer;

    auto* ies = static_cast<ProtocolIE_Container_111P4_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_111P4_t)));
    msg.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // ── IE: Old-eNB-UE-X2AP-ID (id=10, crit=reject) ─────────────────────────
    {
        auto* ie = static_cast<SNStatusTransfer_IEs_t*>(calloc(1, sizeof(SNStatusTransfer_IEs_t)));
        ie->id            = ProtocolIE_ID_id_Old_eNB_UE_X2AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = SNStatusTransfer_IEs__value_PR_UE_X2AP_ID;
        ie->value.choice.UE_X2AP_ID = static_cast<UE_X2AP_ID_t>(srcUeX2apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: New-eNB-UE-X2AP-ID (id=9, crit=reject) ──────────────────────────
    {
        auto* ie = static_cast<SNStatusTransfer_IEs_t*>(calloc(1, sizeof(SNStatusTransfer_IEs_t)));
        ie->id            = ProtocolIE_ID_id_New_eNB_UE_X2AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = SNStatusTransfer_IEs__value_PR_UE_X2AP_ID_1;
        ie->value.choice.UE_X2AP_ID_1 = static_cast<UE_X2AP_ID_t>(tgtUeX2apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: E-RABs-SubjectToStatusTransfer-List (id=18, crit=reject) ─────────
    {
        auto* ie = static_cast<SNStatusTransfer_IEs_t*>(calloc(1, sizeof(SNStatusTransfer_IEs_t)));
        ie->id            = ProtocolIE_ID_id_E_RABs_SubjectToStatusTransfer_List;
        ie->criticality   = Criticality_reject;
        ie->value.present = SNStatusTransfer_IEs__value_PR_E_RABs_SubjectToStatusTransfer_List;
        auto& lst         = ie->value.choice.E_RABs_SubjectToStatusTransfer_List;

        for (const auto& it : items) {
            auto* sc = static_cast<ProtocolIE_Single_Container_114P5_t*>(
                           calloc(1, sizeof(ProtocolIE_Single_Container_114P5_t)));
            sc->id            = ProtocolIE_ID_id_E_RABs_SubjectToStatusTransfer_Item;
            sc->criticality   = Criticality_reject;
            sc->value.present =
                E_RABs_SubjectToStatusTransfer_ItemIEs__value_PR_E_RABs_SubjectToStatusTransfer_Item;
            auto& sitem       = sc->value.choice.E_RABs_SubjectToStatusTransfer_Item;
            sitem.e_RAB_ID    = it.drbId;

            sitem.uL_COUNTvalue = static_cast<COUNTvalue_t*>(calloc(1, sizeof(COUNTvalue_t)));
            sitem.uL_COUNTvalue->pDCP_SN = it.ulPdcpSN;
            sitem.uL_COUNTvalue->hFN     = static_cast<HFN_t>(it.ulHfn);

            sitem.dL_COUNTvalue = static_cast<COUNTvalue_t*>(calloc(1, sizeof(COUNTvalue_t)));
            sitem.dL_COUNTvalue->pDCP_SN = it.dlPdcpSN;
            sitem.dL_COUNTvalue->hFN     = static_cast<HFN_t>(it.dlHfn);

            ASN_SEQUENCE_ADD(&lst.list, sc);
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_X2AP_PDU, &pdu);
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// UE Context Release  (TS 36.423 §8.5.5)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer x2ap_encode_UEContextRelease(uint32_t srcUeX2apId,
                                        uint32_t tgtUeX2apId)
{
    X2AP_PDU_t pdu{};
    pdu.present = X2AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_uEContextRelease;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_UEContextRelease;
    auto& rel = im->value.choice.UEContextRelease;

    auto* ies = static_cast<ProtocolIE_Container_111P5_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_111P5_t)));
    rel.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // ── IE: Old-eNB-UE-X2AP-ID (id=10, crit=ignore) ─────────────────────────
    {
        auto* ie = static_cast<UEContextRelease_IEs_t*>(calloc(1, sizeof(UEContextRelease_IEs_t)));
        ie->id            = ProtocolIE_ID_id_Old_eNB_UE_X2AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = UEContextRelease_IEs__value_PR_UE_X2AP_ID;
        ie->value.choice.UE_X2AP_ID = static_cast<UE_X2AP_ID_t>(srcUeX2apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: New-eNB-UE-X2AP-ID (id=9, crit=ignore) ──────────────────────────
    {
        auto* ie = static_cast<UEContextRelease_IEs_t*>(calloc(1, sizeof(UEContextRelease_IEs_t)));
        ie->id            = ProtocolIE_ID_id_New_eNB_UE_X2AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = UEContextRelease_IEs__value_PR_UE_X2AP_ID_1;
        ie->value.choice.UE_X2AP_ID_1 = static_cast<UE_X2AP_ID_t>(tgtUeX2apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_X2AP_PDU, &pdu);
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// Decoder / utilities
// ═════════════════════════════════════════════════════════════════════════════
X2APPduHandle x2ap_decode(const uint8_t* data, size_t size)
{
    X2AP_PDU_t* pdu = nullptr;
    asn_dec_rval_t rv = asn_decode(nullptr,
                                   ATS_UNALIGNED_BASIC_PER,
                                   &asn_DEF_X2AP_PDU,
                                   reinterpret_cast<void**>(&pdu),
                                   data, size);
    if (rv.code != RC_OK) {
        RBS_LOG_ERROR("X2AP-CODEC", "APER decode failed code={}",
                      static_cast<int>(rv.code));
        ASN_STRUCT_FREE(asn_DEF_X2AP_PDU, pdu);
        return nullptr;
    }
    return pdu;
}

void x2ap_pdu_free(X2APPduHandle pdu)
{
    if (pdu) ASN_STRUCT_FREE(asn_DEF_X2AP_PDU,
                             reinterpret_cast<X2AP_PDU_t*>(pdu));
}

int x2ap_procedure_code(X2APPduHandle pdu)
{
    if (!pdu) return -1;
    auto* p = reinterpret_cast<const X2AP_PDU_t*>(pdu);
    switch (p->present) {
        case X2AP_PDU_PR_initiatingMessage:
            return p->choice.initiatingMessage
                       ? static_cast<int>(p->choice.initiatingMessage->procedureCode) : -1;
        case X2AP_PDU_PR_successfulOutcome:
            return p->choice.successfulOutcome
                       ? static_cast<int>(p->choice.successfulOutcome->procedureCode) : -1;
        case X2AP_PDU_PR_unsuccessfulOutcome:
            return p->choice.unsuccessfulOutcome
                       ? static_cast<int>(p->choice.unsuccessfulOutcome->procedureCode) : -1;
        default:
            return -1;
    }
}

} // namespace rbs::lte

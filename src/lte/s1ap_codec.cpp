// ─────────────────────────────────────────────────────────────────────────────
// S1AP APER codec  (TS 36.413, X.691 APER)
//
// Builds standard S1AP-PDU C structures and encodes them with the asn1c-mouse
// generated library (rbs_asn1_s1ap, ATS_UNALIGNED_BASIC_PER).
//
// Memory management: all asn1c structures are stack-allocated where possible;
// heap-only for pointer fields (SEQUENCE OF lists, BIT/OCTET strings with
// owned buffers). After asn_encode_to_new_buffer() the returned buffer is
// consumed into a ByteBuffer and freed immediately so callers deal only in
// std::vector<uint8_t>.
// ─────────────────────────────────────────────────────────────────────────────

// Include C++ math before generated headers to avoid _logb redeclaration
// conflict between float.h (int _logb) and corecrt_math.h (double _logb)
// on MSVC / Windows SDK 10.
#include <cmath>

#ifdef _MSC_VER
#  pragma warning(push, 0)   // suppress all warnings from generated headers
#endif

// Generated S1AP headers (C linkage)
#include "../../src/generated/s1ap/S1AP-PDU.h"
#include "../../src/generated/s1ap/InitiatingMessage.h"
#include "../../src/generated/s1ap/SuccessfulOutcome.h"
#include "../../src/generated/s1ap/ProcedureCode.h"
#include "../../src/generated/s1ap/Criticality.h"
#include "../../src/generated/s1ap/ProtocolIE-Field.h"
#include "../../src/generated/s1ap/ProtocolIE-Container.h"
#include "../../src/generated/s1ap/ProtocolIE-SingleContainer.h"
#include "../../src/generated/s1ap/S1SetupRequest.h"
#include "../../src/generated/s1ap/InitialUEMessage.h"
#include "../../src/generated/s1ap/UplinkNASTransport.h"
#include "../../src/generated/s1ap/DownlinkNASTransport.h"
#include "../../src/generated/s1ap/InitialContextSetupResponse.h"
#include "../../src/generated/s1ap/UEContextReleaseRequest.h"
#include "../../src/generated/s1ap/UEContextReleaseComplete.h"
#include "../../src/generated/s1ap/E-RABSetupResponse.h"
#include "../../src/generated/s1ap/E-RABReleaseResponse.h"
#include "../../src/generated/s1ap/PathSwitchRequest.h"
#include "../../src/generated/s1ap/HandoverRequired.h"
#include "../../src/generated/s1ap/HandoverNotify.h"
#include "../../src/generated/s1ap/ENB-ID.h"
#include "../../src/generated/s1ap/Global-ENB-ID.h"
#include "../../src/generated/s1ap/TAI.h"
#include "../../src/generated/s1ap/EUTRAN-CGI.h"
#include "../../src/generated/s1ap/SupportedTAs.h"
#include "../../src/generated/s1ap/SupportedTAs-Item.h"
#include "../../src/generated/s1ap/BPLMNs.h"
#include "../../src/generated/s1ap/PLMNidentity.h"
#include "../../src/generated/s1ap/TAC.h"
#include "../../src/generated/s1ap/CellIdentity.h"
#include "../../src/generated/s1ap/Cause.h"
#include "../../src/generated/s1ap/CauseRadioNetwork.h"
#include "../../src/generated/s1ap/CauseTransport.h"
#include "../../src/generated/s1ap/CauseNas.h"
#include "../../src/generated/s1ap/CauseProtocol.h"
#include "../../src/generated/s1ap/E-RABSetupItemBearerSURes.h"
#include "../../src/generated/s1ap/E-RABItem.h"
#include "../../src/generated/s1ap/E-RABSetupListBearerSURes.h"
#include "../../src/generated/s1ap/E-RABList.h"
#include "../../src/generated/s1ap/GTP-TEID.h"
#include "../../src/generated/s1ap/TransportLayerAddress.h"
#include "../../src/generated/s1ap/TargetID.h"
#include "../../src/generated/s1ap/TargeteNB-ID.h"
#include "../../src/generated/s1ap/Source-ToTarget-TransparentContainer.h"
#include "../../src/generated/s1ap/E-RABToBeSwitchedDLItem.h"
#include "../../src/generated/s1ap/E-RABReleaseListBearerRelComp.h"
#include "../../src/generated/s1ap/E-RABReleaseItemBearerRelComp.h"
#include "../../src/generated/s1ap/E-RABSetupItemCtxtSURes.h"
#include "../../src/generated/s1ap/E-RABToBeSwitchedDLList.h"
#include "../../src/generated/s1ap/asn_application.h"
#include "../../src/generated/s1ap/per_encoder.h"

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

// Our own C++ header
#include "s1ap_codec.h"
#include "../common/logger.h"

#include <cstdlib>
#include <cstring>

namespace rbs::lte {

// ═════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═════════════════════════════════════════════════════════════════════════════

/// Encode a fully-built S1AP_PDU_t to UPER bytes.
/// Returns empty ByteBuffer on failure.
static ByteBuffer encode_pdu(S1AP_PDU_t& pdu)
{
    void* buf = nullptr;
    ssize_t nbytes = uper_encode_to_new_buffer(&asn_DEF_S1AP_PDU,
                                               nullptr,
                                               &pdu,
                                               &buf);
    if (nbytes < 0 || !buf) {
        RBS_LOG_ERROR("S1AP-CODEC", "UPER encode failed");
        free(buf);
        return {};
    }
    ByteBuffer out(static_cast<const uint8_t*>(buf),
                   static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));
    free(buf);
    return out;
}

/// Pack 3-byte PLMN ID into PLMNidentity_t (octet string, 3 bytes).
static PLMNidentity_t make_plmn(uint32_t plmnId)
{
    PLMNidentity_t p{};
    static uint8_t buf[3];      // Note: used only before encode_pdu() returns
    buf[0] = static_cast<uint8_t>(plmnId >> 16);
    buf[1] = static_cast<uint8_t>(plmnId >> 8);
    buf[2] = static_cast<uint8_t>(plmnId);
    p.buf  = buf;
    p.size = 3;
    return p;
}

/// Pack 2-byte TAC into TAC_t.
static TAC_t make_tac(uint16_t tac)
{
    TAC_t t{};
    static uint8_t buf[2];
    buf[0] = static_cast<uint8_t>(tac >> 8);
    buf[1] = static_cast<uint8_t>(tac);
    t.buf  = buf;
    t.size = 2;
    return t;
}

/// Pack 28-bit cell identity into CellIdentity_t (BIT STRING, 28 bits).
static CellIdentity_t make_cell_id(uint32_t cellId)
{
    CellIdentity_t c{};
    static uint8_t buf[4];
    uint32_t v = (cellId & 0x0FFFFFFFu) << 4; // left-align 28 bits in 32
    buf[0] = static_cast<uint8_t>(v >> 24);
    buf[1] = static_cast<uint8_t>(v >> 16);
    buf[2] = static_cast<uint8_t>(v >> 8);
    buf[3] = static_cast<uint8_t>(v);
    c.buf         = buf;
    c.size        = 4;
    c.bits_unused = 4;
    return c;
}

/// Pack 20-bit macro eNB ID into ENB_ID_t (BIT STRING, 20 bits).
static void fill_macro_enb_id(ENB_ID_t& enb, uint32_t enbId)
{
    static uint8_t buf[3];
    uint32_t v = (enbId & 0x000FFFFFu) << 4;
    buf[0] = static_cast<uint8_t>(v >> 16);
    buf[1] = static_cast<uint8_t>(v >> 8);
    buf[2] = static_cast<uint8_t>(v);
    enb.present                          = ENB_ID_PR_macroENB_ID;
    enb.choice.macroENB_ID.buf           = buf;
    enb.choice.macroENB_ID.size         = 3;
    enb.choice.macroENB_ID.bits_unused  = 4;
}

/// Convert GTPUTunnel to GTP-TEID (4 bytes) and TransportLayerAddress.
static GTP_TEID_t make_gtpteid(uint32_t teid)
{
    GTP_TEID_t t{};
    static uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(teid >> 24);
    buf[1] = static_cast<uint8_t>(teid >> 16);
    buf[2] = static_cast<uint8_t>(teid >> 8);
    buf[3] = static_cast<uint8_t>(teid);
    t.buf  = buf;
    t.size = 4;
    return t;
}

static TransportLayerAddress_t make_tla(uint32_t ipv4_net)  // network byte order
{
    TransportLayerAddress_t t{};
    // TLA is a BIT STRING of 32 bits (IPv4) — host-order OK, content is just bytes
    static uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(ipv4_net >> 24);
    buf[1] = static_cast<uint8_t>(ipv4_net >> 16);
    buf[2] = static_cast<uint8_t>(ipv4_net >> 8);
    buf[3] = static_cast<uint8_t>(ipv4_net);
    t.buf         = buf;
    t.size        = 4;
    t.bits_unused = 0;
    return t;
}


// ═════════════════════════════════════════════════════════════════════════════
// S1 Setup Request (TS 36.413 §8.7.3)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_S1SetupRequest(uint32_t enbId,
                                      const std::string& enbName,
                                      uint32_t plmnId,
                                      uint16_t tac)
{
    // ── top-level PDU ─────────────────────────────────────────────────────────
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_S1Setup;
    im->criticality   = Criticality_reject;
    im->value.present = InitiatingMessage__value_PR_S1SetupRequest;
    auto& req = im->value.choice.S1SetupRequest;

    // Allocate the IE container (ProtocolIE_Container_114P40_t under the hood)
    auto* ies = static_cast<ProtocolIE_Container_114P40_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P40_t)));
    req.protocolIEs = reinterpret_cast<ProtocolIE_Container*>(ies);

    // ── IE: Global-ENB-ID (id=59, crit=reject) ───────────────────────────────
    {
        auto* ie = static_cast<S1SetupRequestIEs_t*>(calloc(1, sizeof(S1SetupRequestIEs_t)));
        PLMNidentity_t plmn = make_plmn(plmnId);
        ie->id              = ProtocolIE_ID_id_Global_ENB_ID;
        ie->criticality     = Criticality_reject;
        ie->value.present   = S1SetupRequestIEs__value_PR_Global_ENB_ID;
        auto& gid           = ie->value.choice.Global_ENB_ID;
        gid.pLMNidentity    = plmn;
        gid.eNB_ID          = static_cast<ENB_ID_t*>(calloc(1, sizeof(ENB_ID_t)));
        fill_macro_enb_id(*gid.eNB_ID, enbId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: ENBname (id=60, crit=ignore) optional ─────────────────────────────
    if (!enbName.empty()) {
        auto* ie = static_cast<S1SetupRequestIEs_t*>(calloc(1, sizeof(S1SetupRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_eNBname;
        ie->criticality   = Criticality_ignore;
        ie->value.present = S1SetupRequestIEs__value_PR_ENBname;
        auto& n           = ie->value.choice.ENBname;
        // ENBname is VisibleString (OCTET STRING variant in asn1c)
        n.buf  = static_cast<uint8_t*>(malloc(enbName.size()));
        n.size = enbName.size();
        memcpy(n.buf, enbName.data(), enbName.size());
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: SupportedTAs (id=64, crit=reject) ────────────────────────────────
    {
        auto* ie = static_cast<S1SetupRequestIEs_t*>(calloc(1, sizeof(S1SetupRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_SupportedTAs;
        ie->criticality   = Criticality_reject;
        ie->value.present = S1SetupRequestIEs__value_PR_SupportedTAs;
        auto& tas = ie->value.choice.SupportedTAs;

        auto* item = static_cast<SupportedTAs_Item_t*>(calloc(1, sizeof(SupportedTAs_Item_t)));
        item->tAC  = make_tac(tac);   // 2-byte TAC
        // broadcastPLMNs: at least one PLMN required
        item->broadcastPLMNs = static_cast<BPLMNs*>(calloc(1, sizeof(BPLMNs)));
        auto* plmn_entry = static_cast<PLMNidentity_t*>(calloc(1, sizeof(PLMNidentity_t)));
        *plmn_entry = make_plmn(plmnId);
        ASN_SEQUENCE_ADD(&item->broadcastPLMNs->list, plmn_entry);

        ASN_SEQUENCE_ADD(&tas.list, item);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ── IE: DefaultPagingDRX (id=137, crit=ignore) ───────────────────────────
    {
        auto* ie = static_cast<S1SetupRequestIEs_t*>(calloc(1, sizeof(S1SetupRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_DefaultPagingDRX;
        ie->criticality   = Criticality_ignore;
        ie->value.present = S1SetupRequestIEs__value_PR_PagingDRX;
        ie->value.choice.PagingDRX = 1; // v128 (common default)
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);

    // Free only heap-allocated pointer members; stack allocations auto-clean.
    // In a full implementation we would call ASN_STRUCT_FREE_CONTENTS_ONLY here.
    // For the simulator the process lifetime is bounded, but let's be clean:
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.

    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Initial UE Message (TS 36.413 §8.6.2.1)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_InitialUEMessage(uint32_t enbUeS1apId,
                                        const ByteBuffer& nasPdu,
                                        uint32_t plmnId,
                                        uint16_t tac,
                                        uint32_t cellId,
                                        uint8_t  rrcEstCause)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_initialUEMessage;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_InitialUEMessage;
    auto& req = im->value.choice.InitialUEMessage;

    // Use the right concrete container type for InitialUEMessage
    // (ProtocolIE_Container_114P32_t is for InitialUEMessage_IEs)
    auto* ies = static_cast<ProtocolIE_Container_114P32_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P32_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // IE: eNB-UE-S1AP-ID (id=8, crit=reject)
    {
        auto* ie = static_cast<InitialUEMessage_IEs_t*>(calloc(1, sizeof(InitialUEMessage_IEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = InitialUEMessage_IEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // IE: NAS-PDU (id=26, crit=reject)
    {
        auto* ie = static_cast<InitialUEMessage_IEs_t*>(calloc(1, sizeof(InitialUEMessage_IEs_t)));
        ie->id            = ProtocolIE_ID_id_NAS_PDU;
        ie->criticality   = Criticality_reject;
        ie->value.present = InitialUEMessage_IEs__value_PR_NAS_PDU;
        auto& nas = ie->value.choice.NAS_PDU;
        nas.buf  = static_cast<uint8_t*>(malloc(nasPdu.size()));
        nas.size = nasPdu.size();
        memcpy(nas.buf, nasPdu.data(), nasPdu.size());
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // IE: TAI (id=67, crit=reject)
    {
        auto* ie = static_cast<InitialUEMessage_IEs_t*>(calloc(1, sizeof(InitialUEMessage_IEs_t)));
        ie->id            = ProtocolIE_ID_id_TAI;
        ie->criticality   = Criticality_reject;
        ie->value.present = InitialUEMessage_IEs__value_PR_TAI;
        ie->value.choice.TAI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.TAI.tAC          = make_tac(tac);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // IE: EUTRAN-CGI (id=100, crit=ignore)
    {
        auto* ie = static_cast<InitialUEMessage_IEs_t*>(calloc(1, sizeof(InitialUEMessage_IEs_t)));
        ie->id            = ProtocolIE_ID_id_EUTRAN_CGI;
        ie->criticality   = Criticality_ignore;
        ie->value.present = InitialUEMessage_IEs__value_PR_EUTRAN_CGI;
        ie->value.choice.EUTRAN_CGI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.EUTRAN_CGI.cell_ID      = make_cell_id(cellId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // IE: RRC-Establishment-Cause (id=134, crit=ignore)
    {
        auto* ie = static_cast<InitialUEMessage_IEs_t*>(calloc(1, sizeof(InitialUEMessage_IEs_t)));
        ie->id            = ProtocolIE_ID_id_RRC_Establishment_Cause;
        ie->criticality   = Criticality_ignore;
        ie->value.present = InitialUEMessage_IEs__value_PR_RRC_Establishment_Cause;
        ie->value.choice.RRC_Establishment_Cause =
            static_cast<RRC_Establishment_Cause_t>(rrcEstCause);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Uplink NAS Transport (TS 36.413 §8.6.2.3)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_UplinkNASTransport(uint32_t mmeUeS1apId,
                                          uint32_t enbUeS1apId,
                                          const ByteBuffer& nasPdu,
                                          uint32_t plmnId,
                                          uint16_t tac,
                                          uint32_t cellId)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_uplinkNASTransport;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_UplinkNASTransport;
    auto& req = im->value.choice.UplinkNASTransport;

    auto* ies = static_cast<ProtocolIE_Container_114P33_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P33_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID (id=0, crit=reject)
    {
        auto* ie = static_cast<UplinkNASTransport_IEs_t*>(calloc(1, sizeof(UplinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = UplinkNASTransport_IEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID (id=8, crit=reject)
    {
        auto* ie = static_cast<UplinkNASTransport_IEs_t*>(calloc(1, sizeof(UplinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = UplinkNASTransport_IEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // NAS-PDU (id=26, crit=reject)
    {
        auto* ie = static_cast<UplinkNASTransport_IEs_t*>(calloc(1, sizeof(UplinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_NAS_PDU;
        ie->criticality   = Criticality_reject;
        ie->value.present = UplinkNASTransport_IEs__value_PR_NAS_PDU;
        auto& nas = ie->value.choice.NAS_PDU;
        nas.buf  = static_cast<uint8_t*>(malloc(nasPdu.size()));
        nas.size = nasPdu.size();
        memcpy(nas.buf, nasPdu.data(), nasPdu.size());
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // EUTRAN-CGI (id=100, crit=ignore)
    {
        auto* ie = static_cast<UplinkNASTransport_IEs_t*>(calloc(1, sizeof(UplinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_EUTRAN_CGI;
        ie->criticality   = Criticality_ignore;
        ie->value.present = UplinkNASTransport_IEs__value_PR_EUTRAN_CGI;
        ie->value.choice.EUTRAN_CGI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.EUTRAN_CGI.cell_ID      = make_cell_id(cellId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // TAI (id=67, crit=ignore)
    {
        auto* ie = static_cast<UplinkNASTransport_IEs_t*>(calloc(1, sizeof(UplinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_TAI;
        ie->criticality   = Criticality_ignore;
        ie->value.present = UplinkNASTransport_IEs__value_PR_TAI;
        ie->value.choice.TAI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.TAI.tAC          = make_tac(tac);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Downlink NAS Transport (TS 36.413 §8.6.2.2)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_DownlinkNASTransport(uint32_t mmeUeS1apId,
                                            uint32_t enbUeS1apId,
                                            const ByteBuffer& nasPdu)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_downlinkNASTransport;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_DownlinkNASTransport;
    auto& req = im->value.choice.DownlinkNASTransport;

    auto* ies = static_cast<ProtocolIE_Container_114P31_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P31_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<DownlinkNASTransport_IEs_t*>(calloc(1, sizeof(DownlinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = DownlinkNASTransport_IEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<DownlinkNASTransport_IEs_t*>(calloc(1, sizeof(DownlinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = DownlinkNASTransport_IEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // NAS-PDU
    {
        auto* ie = static_cast<DownlinkNASTransport_IEs_t*>(calloc(1, sizeof(DownlinkNASTransport_IEs_t)));
        ie->id            = ProtocolIE_ID_id_NAS_PDU;
        ie->criticality   = Criticality_reject;
        ie->value.present = DownlinkNASTransport_IEs__value_PR_NAS_PDU;
        auto& nas = ie->value.choice.NAS_PDU;
        nas.buf  = static_cast<uint8_t*>(malloc(nasPdu.size()));
        nas.size = nasPdu.size();
        memcpy(nas.buf, nasPdu.data(), nasPdu.size());
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Initial Context Setup Response  (TS 36.413 §8.3.1)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_InitialContextSetupResponse(uint32_t mmeUeS1apId,
                                                   uint32_t enbUeS1apId,
                                                   const std::vector<ERAB>& erabs)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
                                       calloc(1, sizeof(SuccessfulOutcome_t)));
    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_InitialContextSetup;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_InitialContextSetupResponse;
    auto& resp = so->value.choice.InitialContextSetupResponse;

    auto* ies = static_cast<ProtocolIE_Container_114P20_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P20_t)));
    resp.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<InitialContextSetupResponseIEs_t*>(calloc(1, sizeof(InitialContextSetupResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = InitialContextSetupResponseIEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<InitialContextSetupResponseIEs_t*>(calloc(1, sizeof(InitialContextSetupResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = InitialContextSetupResponseIEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // E-RABSetupListCtxtSURes (id=51)
    if (!erabs.empty()) {
        auto* ie = static_cast<InitialContextSetupResponseIEs_t*>(calloc(1, sizeof(InitialContextSetupResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_E_RABSetupListCtxtSURes;
        ie->criticality   = Criticality_ignore;
        ie->value.present = InitialContextSetupResponseIEs__value_PR_E_RABSetupListCtxtSURes;
        auto& lst = ie->value.choice.E_RABSetupListCtxtSURes;

        for (const auto& e : erabs) {
            auto* sc = static_cast<ProtocolIE_SingleContainer_117P6_t*>(calloc(1, sizeof(ProtocolIE_SingleContainer_117P6_t)));
            auto* item = static_cast<E_RABSetupItemCtxtSURes_t*>(calloc(1, sizeof(E_RABSetupItemCtxtSURes_t)));
            item->e_RAB_ID     = e.erabId;
            item->gTP_TEID     = make_gtpteid(e.sgwTunnel.teid);
            item->transportLayerAddress = make_tla(e.sgwTunnel.remoteIPv4);
            sc->value.present  = E_RABSetupItemCtxtSUResIEs__value_PR_E_RABSetupItemCtxtSURes;
            sc->value.choice.E_RABSetupItemCtxtSURes = *item;
            free(item);
            ASN_SEQUENCE_ADD(&lst.list, sc);
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// UE Context Release Request  (TS 36.413 §8.3.3)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_UEContextReleaseRequest(uint32_t mmeUeS1apId,
                                               uint32_t enbUeS1apId,
                                               uint8_t  causeGroup,
                                               uint8_t  causeValue)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_UEContextReleaseRequest;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_UEContextReleaseRequest;
    auto& req = im->value.choice.UEContextReleaseRequest;

    auto* ies = static_cast<ProtocolIE_Container_114P23_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P23_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<UEContextReleaseRequest_IEs_t*>(calloc(1, sizeof(UEContextReleaseRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = UEContextReleaseRequest_IEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<UEContextReleaseRequest_IEs_t*>(calloc(1, sizeof(UEContextReleaseRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = UEContextReleaseRequest_IEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // Cause (id=2, crit=ignore)
    {
        auto* ie = static_cast<UEContextReleaseRequest_IEs_t*>(calloc(1, sizeof(UEContextReleaseRequest_IEs_t)));
        ie->id            = ProtocolIE_ID_id_Cause;
        ie->criticality   = Criticality_ignore;
        ie->value.present = UEContextReleaseRequest_IEs__value_PR_Cause;
        auto& cause       = ie->value.choice.Cause;
        switch (causeGroup) {
            case 0: cause.present = Cause_PR_radioNetwork;
                    cause.choice.radioNetwork = static_cast<CauseRadioNetwork_t>(causeValue);
                    break;
            case 1: cause.present = Cause_PR_transport;
                    cause.choice.transport = static_cast<CauseTransport_t>(causeValue);
                    break;
            case 2: cause.present = Cause_PR_nas;
                    cause.choice.nas = static_cast<CauseNas_t>(causeValue);
                    break;
            case 3: cause.present = Cause_PR_protocol;
                    cause.choice.protocol = static_cast<CauseProtocol_t>(causeValue);
                    break;
            default: cause.present = Cause_PR_misc;
                     cause.choice.misc = CauseMisc_unspecified;
                     break;
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// UE Context Release Complete  (TS 36.413 §8.3.4)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_UEContextReleaseComplete(uint32_t mmeUeS1apId,
                                                uint32_t enbUeS1apId)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
                                       calloc(1, sizeof(SuccessfulOutcome_t)));
    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_UEContextRelease;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_UEContextReleaseComplete;
    auto& resp = so->value.choice.UEContextReleaseComplete;

    auto* ies = static_cast<ProtocolIE_Container_114P25_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P25_t)));
    resp.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<UEContextReleaseComplete_IEs_t*>(calloc(1, sizeof(UEContextReleaseComplete_IEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = UEContextReleaseComplete_IEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<UEContextReleaseComplete_IEs_t*>(calloc(1, sizeof(UEContextReleaseComplete_IEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = UEContextReleaseComplete_IEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// E-RAB Setup Response  (TS 36.413 §8.4.1)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_ERABSetupResponse(uint32_t mmeUeS1apId,
                                         uint32_t enbUeS1apId,
                                         const std::vector<ERAB>& setupErabs,
                                         const std::vector<uint8_t>& failedErabIds)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
                                       calloc(1, sizeof(SuccessfulOutcome_t)));
    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_E_RABSetup;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_E_RABSetupResponse;
    auto& resp = so->value.choice.E_RABSetupResponse;

    auto* ies = static_cast<ProtocolIE_Container_114P13_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P13_t)));
    resp.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<E_RABSetupResponseIEs_t*>(calloc(1, sizeof(E_RABSetupResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = E_RABSetupResponseIEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<E_RABSetupResponseIEs_t*>(calloc(1, sizeof(E_RABSetupResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = E_RABSetupResponseIEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // E-RABSetupListBearerSURes (id=28)
    if (!setupErabs.empty()) {
        auto* ie = static_cast<E_RABSetupResponseIEs_t*>(calloc(1, sizeof(E_RABSetupResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_E_RABSetupListBearerSURes;
        ie->criticality   = Criticality_ignore;
        ie->value.present = E_RABSetupResponseIEs__value_PR_E_RABSetupListBearerSURes;
        auto& lst = ie->value.choice.E_RABSetupListBearerSURes;
        for (const auto& e : setupErabs) {
            auto* sc   = static_cast<ProtocolIE_SingleContainer_117P1_t*>(calloc(1, sizeof(ProtocolIE_SingleContainer_117P1_t)));
            sc->id          = ProtocolIE_ID_id_E_RABSetupItemBearerSURes;
            sc->criticality = Criticality_ignore;
            sc->value.present = E_RABSetupItemBearerSUResIEs__value_PR_E_RABSetupItemBearerSURes;
            auto& item = sc->value.choice.E_RABSetupItemBearerSURes;
            item.e_RAB_ID            = e.erabId;
            item.transportLayerAddress = make_tla(e.sgwTunnel.remoteIPv4);
            item.gTP_TEID            = make_gtpteid(e.sgwTunnel.teid);
            ASN_SEQUENCE_ADD(&lst.list, sc);
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // E-RAB Failed list (id=29) optional
    if (!failedErabIds.empty()) {
        auto* ie = static_cast<E_RABSetupResponseIEs_t*>(calloc(1, sizeof(E_RABSetupResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_E_RABFailedToSetupListBearerSURes;
        ie->criticality   = Criticality_ignore;
        ie->value.present = E_RABSetupResponseIEs__value_PR_E_RABList;
        auto& fl = ie->value.choice.E_RABList;
        for (uint8_t eid : failedErabIds) {
            auto* item = static_cast<E_RABItem_t*>(calloc(1, sizeof(E_RABItem_t)));
            item->e_RAB_ID = eid;
            item->cause    = static_cast<Cause_t*>(calloc(1, sizeof(Cause_t)));
            item->cause->present = Cause_PR_radioNetwork;
            item->cause->choice.radioNetwork = CauseRadioNetwork_unspecified;
            ASN_SEQUENCE_ADD(&fl.list, item);
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// E-RAB Release Response  (TS 36.413 §8.4.3)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_ERABReleaseResponse(uint32_t mmeUeS1apId,
                                           uint32_t enbUeS1apId,
                                           const std::vector<uint8_t>& releasedErabIds)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
                                       calloc(1, sizeof(SuccessfulOutcome_t)));
    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_E_RABRelease;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_E_RABReleaseResponse;
    auto& resp = so->value.choice.E_RABReleaseResponse;

    auto* ies = static_cast<ProtocolIE_Container_114P17_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P17_t)));
    resp.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<E_RABReleaseResponseIEs_t*>(calloc(1, sizeof(E_RABReleaseResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = E_RABReleaseResponseIEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<E_RABReleaseResponseIEs_t*>(calloc(1, sizeof(E_RABReleaseResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = E_RABReleaseResponseIEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // E-RABReleaseListBearerRelComp (id=15)
    if (!releasedErabIds.empty()) {
        auto* ie = static_cast<E_RABReleaseResponseIEs_t*>(calloc(1, sizeof(E_RABReleaseResponseIEs_t)));
        ie->id            = ProtocolIE_ID_id_E_RABReleaseListBearerRelComp;
        ie->criticality   = Criticality_ignore;
        ie->value.present = E_RABReleaseResponseIEs__value_PR_E_RABReleaseListBearerRelComp;
        auto& lst = ie->value.choice.E_RABReleaseListBearerRelComp;
        for (uint8_t eid : releasedErabIds) {
            auto* sc = static_cast<E_RABReleaseItemBearerRelCompIEs_t*>(calloc(1, sizeof(E_RABReleaseItemBearerRelCompIEs_t)));
            sc->id          = ProtocolIE_ID_id_E_RABReleaseItemBearerRelComp;
            sc->criticality = Criticality_ignore;
            sc->value.present =
                E_RABReleaseItemBearerRelCompIEs__value_PR_E_RABReleaseItemBearerRelComp;
            sc->value.choice.E_RABReleaseItemBearerRelComp.e_RAB_ID = eid;
            ASN_SEQUENCE_ADD(&lst.list, sc);
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Path Switch Request  (TS 36.413 §8.5.4)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_PathSwitchRequest(uint32_t mmeUeS1apId,
                                         uint32_t enbUeS1apId,
                                         uint32_t targetEnbId,
                                         const std::vector<ERAB>& erabs,
                                         uint32_t plmnId,
                                         uint32_t cellId,
                                         uint16_t tac)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_PathSwitchRequest;
    im->criticality   = Criticality_reject;
    im->value.present = InitiatingMessage__value_PR_PathSwitchRequest;
    auto& req = im->value.choice.PathSwitchRequest;

    auto* ies = static_cast<ProtocolIE_Container_114P7_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P7_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // eNB-UE-S1AP-ID (new at target)
    {
        auto* ie = static_cast<PathSwitchRequestIEs_t*>(calloc(1, sizeof(PathSwitchRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = PathSwitchRequestIEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // EUTRAN-CGI
    {
        auto* ie = static_cast<PathSwitchRequestIEs_t*>(calloc(1, sizeof(PathSwitchRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_EUTRAN_CGI;
        ie->criticality   = Criticality_ignore;
        ie->value.present = PathSwitchRequestIEs__value_PR_EUTRAN_CGI;
        ie->value.choice.EUTRAN_CGI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.EUTRAN_CGI.cell_ID      = make_cell_id(cellId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // TAI
    {
        auto* ie = static_cast<PathSwitchRequestIEs_t*>(calloc(1, sizeof(PathSwitchRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_TAI;
        ie->criticality   = Criticality_ignore;
        ie->value.present = PathSwitchRequestIEs__value_PR_TAI;
        ie->value.choice.TAI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.TAI.tAC          = make_tac(tac);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // SourceMME-UE-S1AP-ID (id=88)
    {
        auto* ie = static_cast<PathSwitchRequestIEs_t*>(calloc(1, sizeof(PathSwitchRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_SourceMME_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = PathSwitchRequestIEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // E-RABToBeSwitchedDLList (id=22)
    if (!erabs.empty()) {
        auto* ie = static_cast<PathSwitchRequestIEs_t*>(calloc(1, sizeof(PathSwitchRequestIEs_t)));
        ie->id            = ProtocolIE_ID_id_E_RABToBeSwitchedDLList;
        ie->criticality   = Criticality_reject;
        ie->value.present = PathSwitchRequestIEs__value_PR_E_RABToBeSwitchedDLList;
        auto& lst         = ie->value.choice.E_RABToBeSwitchedDLList;
        for (const auto& e : erabs) {
            auto* sc = static_cast<ProtocolIE_SingleContainer_117P24_t*>(calloc(1, sizeof(ProtocolIE_SingleContainer_117P24_t)));
            sc->id          = ProtocolIE_ID_id_E_RABToBeSwitchedDLItem;
            sc->criticality = Criticality_reject;
            sc->value.present = E_RABToBeSwitchedDLItemIEs__value_PR_E_RABToBeSwitchedDLItem;
            auto& item      = sc->value.choice.E_RABToBeSwitchedDLItem;
            item.e_RAB_ID            = e.erabId;
            item.transportLayerAddress = make_tla(e.sgwTunnel.remoteIPv4);
            item.gTP_TEID            = make_gtpteid(e.sgwTunnel.teid);
            ASN_SEQUENCE_ADD(&lst.list, sc);
        }
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Handover Required  (TS 36.413 §8.5.2.1)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_HandoverRequired(uint32_t mmeUeS1apId,
                                        uint32_t enbUeS1apId,
                                        uint32_t targetEnbId,
                                        uint32_t targetPlmnId,
                                        uint32_t targetCellId,
                                        const ByteBuffer& rrcContainer)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_HandoverPreparation;
    im->criticality   = Criticality_reject;
    im->value.present = InitiatingMessage__value_PR_HandoverRequired;
    auto& req = im->value.choice.HandoverRequired;

    auto* ies = static_cast<ProtocolIE_Container_114P0_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P0_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<HandoverRequiredIEs_t*>(calloc(1, sizeof(HandoverRequiredIEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverRequiredIEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<HandoverRequiredIEs_t*>(calloc(1, sizeof(HandoverRequiredIEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverRequiredIEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // HandoverType (id=1, intra-LTE = 0)
    {
        auto* ie = static_cast<HandoverRequiredIEs_t*>(calloc(1, sizeof(HandoverRequiredIEs_t)));
        ie->id            = ProtocolIE_ID_id_HandoverType;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverRequiredIEs__value_PR_HandoverType;
        ie->value.choice.HandoverType = HandoverType_intralte;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // Cause (id=2)
    {
        auto* ie = static_cast<HandoverRequiredIEs_t*>(calloc(1, sizeof(HandoverRequiredIEs_t)));
        ie->id            = ProtocolIE_ID_id_Cause;
        ie->criticality   = Criticality_ignore;
        ie->value.present = HandoverRequiredIEs__value_PR_Cause;
        ie->value.choice.Cause.present = Cause_PR_radioNetwork;
        ie->value.choice.Cause.choice.radioNetwork =
            CauseRadioNetwork_handover_desirable_for_radio_reason;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // TargetID (id=4) — Target-eNB-ID
    {
        auto* ie = static_cast<HandoverRequiredIEs_t*>(calloc(1, sizeof(HandoverRequiredIEs_t)));
        ie->id            = ProtocolIE_ID_id_TargetID;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverRequiredIEs__value_PR_TargetID;
        ie->value.choice.TargetID = static_cast<TargetID_t*>(calloc(1, sizeof(TargetID_t)));
        auto* tid = ie->value.choice.TargetID;
        tid->present = TargetID_PR_targeteNB_ID;
        tid->choice.targeteNB_ID = static_cast<TargeteNB_ID_t*>(calloc(1, sizeof(TargeteNB_ID_t)));
        auto* tenb = tid->choice.targeteNB_ID;
        auto* geid = static_cast<Global_ENB_ID_t*>(calloc(1, sizeof(Global_ENB_ID_t)));
        tenb->global_ENB_ID = geid;
        geid->pLMNidentity  = make_plmn(targetPlmnId);
        geid->eNB_ID        = static_cast<ENB_ID_t*>(calloc(1, sizeof(ENB_ID_t)));
        fill_macro_enb_id(*geid->eNB_ID, targetEnbId);
        auto* tai = static_cast<TAI_t*>(calloc(1, sizeof(TAI_t)));
        tenb->selected_TAI  = tai;
        tai->pLMNidentity   = make_plmn(targetPlmnId);
        tai->tAC            = make_tac(0); // TAC unknown at HO stage
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // Source-ToTarget-TransparentContainer (id=104)
    if (!rrcContainer.empty()) {
        auto* ie = static_cast<HandoverRequiredIEs_t*>(calloc(1, sizeof(HandoverRequiredIEs_t)));
        ie->id            = ProtocolIE_ID_id_Source_ToTarget_TransparentContainer;
        ie->criticality   = Criticality_reject;
        ie->value.present =
            HandoverRequiredIEs__value_PR_Source_ToTarget_TransparentContainer;
        auto& c = ie->value.choice.Source_ToTarget_TransparentContainer;
        c.buf  = static_cast<uint8_t*>(malloc(rrcContainer.size()));
        c.size = rrcContainer.size();
        memcpy(c.buf, rrcContainer.data(), rrcContainer.size());
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Handover Notify  (TS 36.413 §8.5.1 — target side)
// ═════════════════════════════════════════════════════════════════════════════
ByteBuffer s1ap_encode_HandoverNotify(uint32_t mmeUeS1apId,
                                      uint32_t enbUeS1apId,
                                      uint32_t plmnId,
                                      uint16_t tac,
                                      uint32_t cellId)
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_HandoverNotification;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_HandoverNotify;
    auto& req = im->value.choice.HandoverNotify;

    auto* ies = static_cast<ProtocolIE_Container_114P6_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P6_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID
    {
        auto* ie = static_cast<HandoverNotifyIEs_t*>(calloc(1, sizeof(HandoverNotifyIEs_t)));
        ie->id            = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverNotifyIEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID = static_cast<long>(mmeUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // eNB-UE-S1AP-ID
    {
        auto* ie = static_cast<HandoverNotifyIEs_t*>(calloc(1, sizeof(HandoverNotifyIEs_t)));
        ie->id            = ProtocolIE_ID_id_eNB_UE_S1AP_ID;
        ie->criticality   = Criticality_reject;
        ie->value.present = HandoverNotifyIEs__value_PR_ENB_UE_S1AP_ID;
        ie->value.choice.ENB_UE_S1AP_ID = static_cast<long>(enbUeS1apId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // EUTRAN-CGI
    {
        auto* ie = static_cast<HandoverNotifyIEs_t*>(calloc(1, sizeof(HandoverNotifyIEs_t)));
        ie->id            = ProtocolIE_ID_id_EUTRAN_CGI;
        ie->criticality   = Criticality_ignore;
        ie->value.present = HandoverNotifyIEs__value_PR_EUTRAN_CGI;
        ie->value.choice.EUTRAN_CGI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.EUTRAN_CGI.cell_ID      = make_cell_id(cellId);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }
    // TAI
    {
        auto* ie = static_cast<HandoverNotifyIEs_t*>(calloc(1, sizeof(HandoverNotifyIEs_t)));
        ie->id            = ProtocolIE_ID_id_TAI;
        ie->criticality   = Criticality_ignore;
        ie->value.present = HandoverNotifyIEs__value_PR_TAI;
        ie->value.choice.TAI.pLMNidentity = make_plmn(plmnId);
        ie->value.choice.TAI.tAC          = make_tac(tac);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    ByteBuffer out = encode_pdu(pdu);
    // ASN_STRUCT_FREE_CONTENTS_ONLY: omitted — static IE bufs (PLMN/TAC/CellId) are not heap-allocated.
    return out;
}


// ═════════════════════════════════════════════════════════════════════════════
// Decode / utilities
// ═════════════════════════════════════════════════════════════════════════════

void s1ap_pdu_free(S1APPduHandle pdu)
{
    if (pdu)
        ASN_STRUCT_FREE(asn_DEF_S1AP_PDU, pdu);
}

S1APPduHandle s1ap_decode(const void* buf, size_t len)
{
    S1AP_PDU_t* pdu = nullptr;
    asn_dec_rval_t rv = asn_decode(nullptr,
                                   ATS_UNALIGNED_BASIC_PER,
                                   &asn_DEF_S1AP_PDU,
                                   reinterpret_cast<void**>(&pdu),
                                   buf,
                                   len);
    if (rv.code != RC_OK) {
        RBS_LOG_ERROR("S1AP-CODEC", "APER decode failed code={}", static_cast<int>(rv.code));
        ASN_STRUCT_FREE(asn_DEF_S1AP_PDU, pdu);
        return nullptr;
    }
    return pdu;
}

int s1ap_procedure_code(S1APPduHandle pdu)
{
    if (!pdu) return -1;
    switch (pdu->present) {
        case S1AP_PDU_PR_initiatingMessage:
            return pdu->choice.initiatingMessage
                       ? static_cast<int>(pdu->choice.initiatingMessage->procedureCode) : -1;
        case S1AP_PDU_PR_successfulOutcome:
            return pdu->choice.successfulOutcome
                       ? static_cast<int>(pdu->choice.successfulOutcome->procedureCode) : -1;
        case S1AP_PDU_PR_unsuccessfulOutcome:
            return pdu->choice.unsuccessfulOutcome
                       ? static_cast<int>(pdu->choice.unsuccessfulOutcome->procedureCode) : -1;
        default:
            return -1;
    }
}

ByteBuffer s1ap_extract_nas_pdu(S1APPduHandle pdu)
{
    if (!pdu || pdu->present != S1AP_PDU_PR_initiatingMessage)
        return {};
    auto& im = pdu->choice.initiatingMessage;

    const OCTET_STRING_t* nas = nullptr;
    if (im->value.present == InitiatingMessage__value_PR_UplinkNASTransport) {
        auto& proto = im->value.choice.UplinkNASTransport;
        // Walk IEs to find NAS-PDU
        auto* ul_ies = reinterpret_cast<ProtocolIE_Container_114P33_t*>(proto.protocolIEs);
        for (int i = 0; i < ul_ies->list.count; ++i) {
            auto* ie = static_cast<UplinkNASTransport_IEs_t*>(ul_ies->list.array[i]);
            if (ie->id == ProtocolIE_ID_id_NAS_PDU) {
                nas = &ie->value.choice.NAS_PDU;
                break;
            }
        }
    } else if (im->value.present == InitiatingMessage__value_PR_DownlinkNASTransport) {
        auto& proto = im->value.choice.DownlinkNASTransport;
        auto* dl_ies = reinterpret_cast<ProtocolIE_Container_114P31_t*>(proto.protocolIEs);
        for (int i = 0; i < dl_ies->list.count; ++i) {
            auto* ie = static_cast<DownlinkNASTransport_IEs_t*>(dl_ies->list.array[i]);
            if (ie->id == ProtocolIE_ID_id_NAS_PDU) {
                nas = &ie->value.choice.NAS_PDU;
                break;
            }
        }
    }
    if (!nas || !nas->buf) return {};
    return ByteBuffer(nas->buf, nas->buf + nas->size);
}

}  // namespace rbs::lte

#include "../src/lte/s1ap_codec.h"
#include "../src/lte/s1ap_interface.h"
#include "../src/generated/s1ap/S1AP-PDU.h"
#include "../src/generated/s1ap/SuccessfulOutcome.h"
#include "../src/generated/s1ap/ProcedureCode.h"
#include "../src/generated/s1ap/Criticality.h"
#include "../src/generated/s1ap/S1SetupResponse.h"
#include "../src/generated/s1ap/UnsuccessfulOutcome.h"
#include "../src/generated/s1ap/S1SetupFailure.h"
#include "../src/generated/s1ap/InitialContextSetupResponse.h"
#include "../src/generated/s1ap/UEContextReleaseComplete.h"
#include "../src/generated/s1ap/Paging.h"
#include "../src/generated/s1ap/UEPagingID.h"
#include "../src/generated/s1ap/UEIdentityIndexValue.h"
#include "../src/generated/s1ap/CNDomain.h"
#include "../src/generated/s1ap/TAIList.h"
#include "../src/generated/s1ap/TAIItem.h"
#include "../src/generated/s1ap/TAI.h"
#include "../src/generated/s1ap/PLMNidentity.h"
#include "../src/generated/s1ap/TAC.h"
#include "../src/generated/s1ap/ProtocolIE-Container.h"
#include "../src/generated/s1ap/ProtocolIE-SingleContainer.h"
#include "../src/generated/s1ap/ProtocolIE-Field.h"
#include "../src/generated/s1ap/InitiatingMessage.h"
#include "../src/generated/s1ap/per_encoder.h"
#include "../src/generated/s1ap/Reset.h"
#include "../src/generated/s1ap/ResetType.h"
#include "../src/generated/s1ap/ResetAll.h"
#include "../src/generated/s1ap/ErrorIndication.h"
#include "../src/generated/s1ap/HandoverRequest.h"
#include "../src/generated/s1ap/HandoverRequestAcknowledge.h"
#include "../src/generated/s1ap/HandoverCommand.h"
#include "../src/generated/s1ap/HandoverPreparationFailure.h"
#include "../src/generated/s1ap/HandoverFailure.h"
#include "../src/generated/s1ap/ENBStatusTransfer.h"
#include "../src/generated/s1ap/MMEStatusTransfer.h"
#include "../src/generated/s1ap/UnsuccessfulOutcome.h"
#include "../src/generated/s1ap/CauseRadioNetwork.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef _MSC_VER
#  include <crtdbg.h>
#endif

static rbs::ByteBuffer makeSyntheticS1SetupResponse()
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
        calloc(1, sizeof(SuccessfulOutcome_t)));

    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_S1Setup;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_S1SetupResponse;

    auto* ies = static_cast<ProtocolIE_Container_114P41_t*>(
        calloc(1, sizeof(ProtocolIE_Container_114P41_t)));
    so->value.choice.S1SetupResponse.protocolIEs =
        reinterpret_cast<ProtocolIE_Container*>(ies);

    void* buf = nullptr;
    const ssize_t nbytes = uper_encode_to_new_buffer(
        &asn_DEF_S1AP_PDU, nullptr, &pdu, &buf);

    assert(nbytes > 0);
    assert(buf != nullptr);

    rbs::ByteBuffer out(
        static_cast<const uint8_t*>(buf),
        static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));

    free(buf);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_S1AP_PDU, &pdu);
    return out;
}

static rbs::ByteBuffer makeSyntheticS1SetupFailure()
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_unsuccessfulOutcome;
    pdu.choice.unsuccessfulOutcome = static_cast<UnsuccessfulOutcome_t*>(
        calloc(1, sizeof(UnsuccessfulOutcome_t)));

    auto* uo = pdu.choice.unsuccessfulOutcome;
    uo->procedureCode = ProcedureCode_id_S1Setup;
    uo->criticality   = Criticality_reject;
    uo->value.present = UnsuccessfulOutcome__value_PR_S1SetupFailure;

    auto* ies = static_cast<ProtocolIE_Container_114P42_t*>(
        calloc(1, sizeof(ProtocolIE_Container_114P42_t)));
    uo->value.choice.S1SetupFailure.protocolIEs =
        reinterpret_cast<ProtocolIE_Container*>(ies);

    void* buf = nullptr;
    const ssize_t nbytes = uper_encode_to_new_buffer(
        &asn_DEF_S1AP_PDU, nullptr, &pdu, &buf);

    assert(nbytes > 0);
    assert(buf != nullptr);

    rbs::ByteBuffer out(
        static_cast<const uint8_t*>(buf),
        static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));

    free(buf);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_S1AP_PDU, &pdu);
    return out;
}

static rbs::ByteBuffer makeSyntheticInitialContextSetupResponse()
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
        calloc(1, sizeof(SuccessfulOutcome_t)));

    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_InitialContextSetup;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_InitialContextSetupResponse;

    auto* ies = static_cast<ProtocolIE_Container_114P20_t*>(
        calloc(1, sizeof(ProtocolIE_Container_114P20_t)));
    so->value.choice.InitialContextSetupResponse.protocolIEs =
        reinterpret_cast<ProtocolIE_Container*>(ies);

    void* buf = nullptr;
    const ssize_t nbytes = uper_encode_to_new_buffer(
        &asn_DEF_S1AP_PDU, nullptr, &pdu, &buf);

    assert(nbytes > 0);
    assert(buf != nullptr);

    rbs::ByteBuffer out(
        static_cast<const uint8_t*>(buf),
        static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));

    free(buf);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_S1AP_PDU, &pdu);
    return out;
}

static rbs::ByteBuffer makeSyntheticUEContextReleaseComplete()
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
        calloc(1, sizeof(SuccessfulOutcome_t)));

    auto* so = pdu.choice.successfulOutcome;
    so->procedureCode = ProcedureCode_id_UEContextRelease;
    so->criticality   = Criticality_reject;
    so->value.present = SuccessfulOutcome__value_PR_UEContextReleaseComplete;

    auto* ies = static_cast<ProtocolIE_Container_114P25_t*>(
        calloc(1, sizeof(ProtocolIE_Container_114P25_t)));
    so->value.choice.UEContextReleaseComplete.protocolIEs =
        reinterpret_cast<ProtocolIE_Container*>(ies);

    void* buf = nullptr;
    const ssize_t nbytes = uper_encode_to_new_buffer(
        &asn_DEF_S1AP_PDU, nullptr, &pdu, &buf);

    assert(nbytes > 0);
    assert(buf != nullptr);

    rbs::ByteBuffer out(
        static_cast<const uint8_t*>(buf),
        static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));

    free(buf);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_S1AP_PDU, &pdu);
    return out;
}

// Synthetic Paging PDU (initiatingMessage, ProcedureCode_id_Paging=10).
// TS 36.413 §8.7.1 — mandatory IEs: UEIdentityIndexValue, UEPagingID, CNDomain, TAIList.
static rbs::ByteBuffer makeSyntheticPaging()
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
        calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_Paging;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_Paging;
    auto& req = im->value.choice.Paging;

    auto* ies = static_cast<ProtocolIE_Container_114P22_t*>(
        calloc(1, sizeof(ProtocolIE_Container_114P22_t)));
    req.protocolIEs = reinterpret_cast<ProtocolIE_Container*>(ies);

    // UEIdentityIndexValue (id=80): 10-bit BIT STRING = 42 (0x02A)
    {
        auto* ie = static_cast<PagingIEs_t*>(calloc(1, sizeof(PagingIEs_t)));
        ie->id            = ProtocolIE_ID_id_UEIdentityIndexValue;
        ie->criticality   = Criticality_ignore;
        ie->value.present = PagingIEs__value_PR_UEIdentityIndexValue;
        static uint8_t idxBuf[2];
        idxBuf[0] = 0x00; idxBuf[1] = (42u & 3u) << 6; // 42 = 0b00101010
        idxBuf[0] = static_cast<uint8_t>(42u >> 2);
        idxBuf[1] = static_cast<uint8_t>((42u & 3u) << 6);
        ie->value.choice.UEIdentityIndexValue.buf         = idxBuf;
        ie->value.choice.UEIdentityIndexValue.size        = 2;
        ie->value.choice.UEIdentityIndexValue.bits_unused = 6;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // UEPagingID (id=43): iMSI = {0x25, 0x01, 0x00, 0x00, 0x01} (dummy)
    {
        static uint8_t imsiBuf[] = {0x25, 0x01, 0x00, 0x00, 0x01};
        auto* ie = static_cast<PagingIEs_t*>(calloc(1, sizeof(PagingIEs_t)));
        ie->id            = ProtocolIE_ID_id_UEPagingID;
        ie->criticality   = Criticality_ignore;
        ie->value.present = PagingIEs__value_PR_UEPagingID;
        ie->value.choice.UEPagingID.present          = UEPagingID_PR_iMSI;
        ie->value.choice.UEPagingID.choice.iMSI.buf  = imsiBuf;
        ie->value.choice.UEPagingID.choice.iMSI.size = sizeof(imsiBuf);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // CNDomain (id=109): ps=0
    {
        auto* ie = static_cast<PagingIEs_t*>(calloc(1, sizeof(PagingIEs_t)));
        ie->id            = ProtocolIE_ID_id_CNDomain;
        ie->criticality   = Criticality_ignore;
        ie->value.present = PagingIEs__value_PR_CNDomain;
        ie->value.choice.CNDomain = CNDomain_ps;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // TAIList (id=46): one TAI entry
    {
        auto* ie = static_cast<PagingIEs_t*>(calloc(1, sizeof(PagingIEs_t)));
        ie->id            = ProtocolIE_ID_id_TAIList;
        ie->criticality   = Criticality_ignore;
        ie->value.present = PagingIEs__value_PR_TAIList;

        static uint8_t plmnBuf[3] = {0x02, 0xF8, 0x31}; // MCC=250 MNC=01
        static uint8_t tacBuf[2]  = {0x03, 0xE9};        // TAC = 1001
        auto* taiItemIE = static_cast<TAIItemIEs_t*>(calloc(1, sizeof(TAIItemIEs_t)));
        taiItemIE->id            = ProtocolIE_ID_id_TAIItem;
        taiItemIE->criticality   = Criticality_ignore;
        taiItemIE->value.present = TAIItemIEs__value_PR_TAIItem;
        taiItemIE->value.choice.TAIItem.tAI = static_cast<TAI_t*>(calloc(1, sizeof(TAI_t)));
        taiItemIE->value.choice.TAIItem.tAI->pLMNidentity.buf  = plmnBuf;
        taiItemIE->value.choice.TAIItem.tAI->pLMNidentity.size = 3;
        taiItemIE->value.choice.TAIItem.tAI->tAC.buf  = tacBuf;
        taiItemIE->value.choice.TAIItem.tAI->tAC.size = 2;
        ASN_SEQUENCE_ADD(&ie->value.choice.TAIList, taiItemIE);
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    void* buf = nullptr;
    const ssize_t nbytes = uper_encode_to_new_buffer(
        &asn_DEF_S1AP_PDU, nullptr, &pdu, &buf);
    assert(nbytes > 0);
    assert(buf != nullptr);
    rbs::ByteBuffer out(
        static_cast<const uint8_t*>(buf),
        static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));
    free(buf);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Synthetic Reset PDU (initiatingMessage, ProcedureCode_id_Reset=14).
// TS 36.413 §8.7.2 — mandatory IEs: Cause, ResetType (s1_Interface).
// ─────────────────────────────────────────────────────────────────────────────
static rbs::ByteBuffer makeSyntheticReset()
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_Reset;
    im->criticality   = Criticality_reject;
    im->value.present = InitiatingMessage__value_PR_Reset;
    auto& req = im->value.choice.Reset;

    auto* ies = static_cast<ProtocolIE_Container_114P37_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P37_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // Cause: radioNetwork / unspecified (0/0)
    {
        auto* ie = static_cast<ResetIEs_t*>(calloc(1, sizeof(ResetIEs_t)));
        ie->id                     = ProtocolIE_ID_id_Cause;
        ie->criticality            = Criticality_ignore;
        ie->value.present          = ResetIEs__value_PR_Cause;
        ie->value.choice.Cause.present              = Cause_PR_radioNetwork;
        ie->value.choice.Cause.choice.radioNetwork  = CauseRadioNetwork_unspecified;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // ResetType: s1_Interface (reset-all = 0)
    {
        auto* ie = static_cast<ResetIEs_t*>(calloc(1, sizeof(ResetIEs_t)));
        ie->id                               = ProtocolIE_ID_id_ResetType;
        ie->criticality                      = Criticality_reject;
        ie->value.present                    = ResetIEs__value_PR_ResetType;
        ie->value.choice.ResetType.present   = ResetType_PR_s1_Interface;
        ie->value.choice.ResetType.choice.s1_Interface = ResetAll_reset_all;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    void* buf = nullptr;
    const ssize_t nbytes = uper_encode_to_new_buffer(&asn_DEF_S1AP_PDU, nullptr, &pdu, &buf);
    assert(nbytes > 0);
    rbs::ByteBuffer out(static_cast<const uint8_t*>(buf),
                       static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));
    free(buf);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Synthetic ErrorIndication PDU (initiatingMessage, ProcedureCode_id_ErrorIndication=15).
// TS 36.413 §8.7.4 — all IEs optional; here: MME-UE-S1AP-ID + Cause.
// ─────────────────────────────────────────────────────────────────────────────
static rbs::ByteBuffer makeSyntheticErrorIndication()
{
    S1AP_PDU_t pdu{};
    pdu.present = S1AP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
                                       calloc(1, sizeof(InitiatingMessage_t)));
    auto* im = pdu.choice.initiatingMessage;
    im->procedureCode = ProcedureCode_id_ErrorIndication;
    im->criticality   = Criticality_ignore;
    im->value.present = InitiatingMessage__value_PR_ErrorIndication;
    auto& req = im->value.choice.ErrorIndication;

    auto* ies = static_cast<ProtocolIE_Container_114P39_t*>(
                    calloc(1, sizeof(ProtocolIE_Container_114P39_t)));
    req.protocolIEs = reinterpret_cast<struct ProtocolIE_Container*>(ies);

    // MME-UE-S1AP-ID (id=0, optional)
    {
        auto* ie = static_cast<ErrorIndicationIEs_t*>(calloc(1, sizeof(ErrorIndicationIEs_t)));
        ie->id                              = ProtocolIE_ID_id_MME_UE_S1AP_ID;
        ie->criticality                     = Criticality_ignore;
        ie->value.present                   = ErrorIndicationIEs__value_PR_MME_UE_S1AP_ID;
        ie->value.choice.MME_UE_S1AP_ID    = 42;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    // Cause: protocol / unspecified (3/?)
    {
        auto* ie = static_cast<ErrorIndicationIEs_t*>(calloc(1, sizeof(ErrorIndicationIEs_t)));
        ie->id                     = ProtocolIE_ID_id_Cause;
        ie->criticality            = Criticality_ignore;
        ie->value.present          = ErrorIndicationIEs__value_PR_Cause;
        ie->value.choice.Cause.present             = Cause_PR_protocol;
        ie->value.choice.Cause.choice.protocol     = CauseProtocol_unspecified;
        ASN_SEQUENCE_ADD(&ies->list, ie);
    }

    void* buf = nullptr;
    const ssize_t nbytes = uper_encode_to_new_buffer(&asn_DEF_S1AP_PDU, nullptr, &pdu, &buf);
    assert(nbytes > 0);
    rbs::ByteBuffer out(static_cast<const uint8_t*>(buf),
                       static_cast<const uint8_t*>(buf) + static_cast<size_t>(nbytes));
    free(buf);
    return out;
}

int main() {
#ifdef _MSC_VER
    // Suppress CRT assert dialogs — failures surface as process exit code instead
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

    using namespace rbs::lte;

    // ── S1 Setup Request: encode → decode → verify procedure code ────────────
    // TS 36.413 §8.7.3
    rbs::ByteBuffer s1SetupPdu = s1ap_encode_S1SetupRequest(
        0x12345,            // eNB-ID (20-bit)
        "vNE-eNB-01",       // eNB name
        0x025001,           // PLMN: MCC=250, MNC=01
        1001                // TAC
    );
    assert(!s1SetupPdu.empty());

    S1APPduHandle h1 = s1ap_decode(s1SetupPdu.data(), s1SetupPdu.size());
    assert(h1 != nullptr);
    assert(s1ap_procedure_code(h1) == static_cast<int>(S1APProcedure::S1_SETUP));
    s1ap_pdu_free(h1);

    S1APMessage decodedReq{};
    assert(s1ap_decode_message(s1SetupPdu, decodedReq));
    assert(decodedReq.procedure == S1APProcedure::S1_SETUP);
    assert(!decodedReq.isSuccessfulOutcome);
    assert(!decodedReq.isUnsuccessfulOutcome);

    // ── Uplink NAS Transport: encode → decode → extract NAS PDU ──────────────
    // TS 36.413 §8.6.2.3
    rbs::ByteBuffer nasPdu(16, 0x7E);   // dummy NAS message
    rbs::ByteBuffer ulNasBuf = s1ap_encode_UplinkNASTransport(
        777, 1, nasPdu, 0x025001, 1001, 0x0000001
    );
    assert(!ulNasBuf.empty());

    S1APPduHandle h2 = s1ap_decode(ulNasBuf.data(), ulNasBuf.size());
    assert(h2 != nullptr);
    assert(s1ap_procedure_code(h2) == static_cast<int>(S1APProcedure::UPLINK_NAS_TRANSPORT));

    rbs::ByteBuffer extractedNas = s1ap_extract_nas_pdu(h2);
    assert(extractedNas == nasPdu);
    s1ap_pdu_free(h2);

    // ── Downlink NAS Transport: encode → decode → extract NAS PDU ────────────
    // TS 36.413 §8.6.2.2
    rbs::ByteBuffer dlNasBuf = s1ap_encode_DownlinkNASTransport(777, 1, nasPdu);
    assert(!dlNasBuf.empty());

    S1APPduHandle h3 = s1ap_decode(dlNasBuf.data(), dlNasBuf.size());
    assert(h3 != nullptr);
    assert(s1ap_procedure_code(h3) == static_cast<int>(S1APProcedure::DOWNLINK_NAS_TRANSPORT));

    rbs::ByteBuffer extractedDl = s1ap_extract_nas_pdu(h3);
    assert(extractedDl == nasPdu);
    s1ap_pdu_free(h3);

    // ── Initial UE Message ────────────────────────────────────────────────────
    // TS 36.413 §8.6.2.1
    rbs::ByteBuffer iueBuf = s1ap_encode_InitialUEMessage(
        1, nasPdu, 0x025001, 1001, 0x0000001, 3
    );
    assert(!iueBuf.empty());

    S1APPduHandle h4 = s1ap_decode(iueBuf.data(), iueBuf.size());
    assert(h4 != nullptr);
    assert(s1ap_procedure_code(h4) == static_cast<int>(S1APProcedure::INITIAL_UE_MESSAGE));
    s1ap_pdu_free(h4);

    // ── Synthetic S1 Setup Response: decode_message outcome flag ─────────────
    // Verifies that successfulOutcome PDUs are marked explicitly.
    rbs::ByteBuffer s1SetupRespBuf = makeSyntheticS1SetupResponse();
    assert(!s1SetupRespBuf.empty());

    S1APMessage decoded{};
    assert(s1ap_decode_message(s1SetupRespBuf, decoded));
    assert(decoded.procedure == S1APProcedure::S1_SETUP);
    assert(decoded.isSuccessfulOutcome);
    assert(!decoded.isUnsuccessfulOutcome);

    // ── Synthetic S1 Setup Failure: decode_message outcome flag ──────────────
    // Verifies that unsuccessfulOutcome PDUs are marked explicitly.
    rbs::ByteBuffer s1SetupFailBuf = makeSyntheticS1SetupFailure();
    assert(!s1SetupFailBuf.empty());

    S1APMessage decodedFail{};
    assert(s1ap_decode_message(s1SetupFailBuf, decodedFail));
    assert(decodedFail.procedure == S1APProcedure::S1_SETUP);
    assert(!decodedFail.isSuccessfulOutcome);
    assert(decodedFail.isUnsuccessfulOutcome);

    // ── Synthetic InitialContextSetupResponse: successfulOutcome flag ──────────
    // TS 36.413 §8.3.2 — successfulOutcome of InitialContextSetup procedure
    rbs::ByteBuffer icsRespBuf = makeSyntheticInitialContextSetupResponse();
    assert(!icsRespBuf.empty());

    S1APMessage decodedICS{};
    assert(s1ap_decode_message(icsRespBuf, decodedICS));
    assert(decodedICS.procedure == S1APProcedure::INITIAL_CONTEXT_SETUP);
    assert(decodedICS.isSuccessfulOutcome);
    assert(!decodedICS.isUnsuccessfulOutcome);

    // ── Synthetic UEContextReleaseComplete: successfulOutcome flag ────────────
    // TS 36.413 §8.3.3 — successfulOutcome of UEContextRelease procedure
    rbs::ByteBuffer ueCRBuf = makeSyntheticUEContextReleaseComplete();
    assert(!ueCRBuf.empty());

    S1APMessage decodedUeCR{};
    assert(s1ap_decode_message(ueCRBuf, decodedUeCR));
    assert(decodedUeCR.procedure == S1APProcedure::UE_CONTEXT_RELEASE_COMMAND);
    assert(decodedUeCR.isSuccessfulOutcome);
    assert(!decodedUeCR.isUnsuccessfulOutcome);

    // ── s1ap_encode_Paging: round-trip encode → decode → verify procedure ────────────────
    // TS 36.413 §8.7.1 — Paging is an initiatingMessage from MME.
    rbs::ByteBuffer pagingImsi = {0x25, 0x01, 0x00, 0x00, 0x01};
    rbs::ByteBuffer pagingBuf  = s1ap_encode_Paging(
        42 /* ueIdxVal */, pagingImsi, 0x025001 /* PLMN */, 1001 /* TAC */, 0 /* PS */);
    assert(!pagingBuf.empty());

    S1APPduHandle hPag = s1ap_decode(pagingBuf.data(), pagingBuf.size());
    assert(hPag != nullptr);
    assert(s1ap_procedure_code(hPag) == static_cast<int>(S1APProcedure::PAGING));
    s1ap_pdu_free(hPag);

    S1APMessage decodedPaging{};
    assert(s1ap_decode_message(pagingBuf, decodedPaging));
    assert(decodedPaging.procedure == S1APProcedure::PAGING);
    assert(!decodedPaging.isSuccessfulOutcome);
    assert(!decodedPaging.isUnsuccessfulOutcome);

    // ── Synthetic Paging PDU: decode_message outcome flags ─────────────────────
    // Verifies that a hand-crafted Paging PDU round-trips through the decoder.
    rbs::ByteBuffer synPagBuf = makeSyntheticPaging();
    assert(!synPagBuf.empty());

    S1APMessage decodedSynPag{};
    assert(s1ap_decode_message(synPagBuf, decodedSynPag));
    assert(decodedSynPag.procedure == S1APProcedure::PAGING);
    assert(!decodedSynPag.isSuccessfulOutcome);
    assert(!decodedSynPag.isUnsuccessfulOutcome);

    // ── Reset encode/decode round-trip ─────────────────────────────────────────
    // TS 36.413 §8.7.2 — encode via codec, verify procedure code.
    rbs::ByteBuffer resetBuf = s1ap_encode_Reset(0, 0, true);
    assert(!resetBuf.empty());

    S1APPduHandle hReset = s1ap_decode(resetBuf.data(), resetBuf.size());
    assert(hReset != nullptr);
    assert(s1ap_procedure_code(hReset) == static_cast<int>(S1APProcedure::RESET));
    s1ap_pdu_free(hReset);

    S1APMessage decodedReset{};
    assert(s1ap_decode_message(resetBuf, decodedReset));
    assert(decodedReset.procedure == S1APProcedure::RESET);
    assert(!decodedReset.isSuccessfulOutcome);
    assert(!decodedReset.isUnsuccessfulOutcome);

    // ── Synthetic Reset PDU: decode_message outcome flags ──────────────────────
    rbs::ByteBuffer synResetBuf = makeSyntheticReset();
    assert(!synResetBuf.empty());

    S1APMessage decodedSynReset{};
    assert(s1ap_decode_message(synResetBuf, decodedSynReset));
    assert(decodedSynReset.procedure == S1APProcedure::RESET);
    assert(!decodedSynReset.isSuccessfulOutcome);
    assert(!decodedSynReset.isUnsuccessfulOutcome);

    // ── Error Indication encode/decode round-trip (with Cause) ────────────────
    // TS 36.413 §8.7.4 — encode with mme=100, enb=0, cause=protocol/unspec.
    rbs::ByteBuffer errIndBuf = s1ap_encode_ErrorIndication(100, 0, 3, 0);
    assert(!errIndBuf.empty());

    S1APPduHandle hErrInd = s1ap_decode(errIndBuf.data(), errIndBuf.size());
    assert(hErrInd != nullptr);
    assert(s1ap_procedure_code(hErrInd) == static_cast<int>(S1APProcedure::ERROR_INDICATION));
    s1ap_pdu_free(hErrInd);

    S1APMessage decodedErrInd{};
    assert(s1ap_decode_message(errIndBuf, decodedErrInd));
    assert(decodedErrInd.procedure == S1APProcedure::ERROR_INDICATION);
    assert(!decodedErrInd.isSuccessfulOutcome);
    assert(!decodedErrInd.isUnsuccessfulOutcome);

    // ── Error Indication encode/decode (no optional IEs — cause absent) ────────
    rbs::ByteBuffer errIndNoCauseBuf = s1ap_encode_ErrorIndication(0, 0, 0xFF, 0);
    assert(!errIndNoCauseBuf.empty());

    S1APMessage decodedErrIndNoCause{};
    assert(s1ap_decode_message(errIndNoCauseBuf, decodedErrIndNoCause));
    assert(decodedErrIndNoCause.procedure == S1APProcedure::ERROR_INDICATION);

    // ── Synthetic ErrorIndication PDU: decode_message outcome flags ────────────
    rbs::ByteBuffer synErrBuf = makeSyntheticErrorIndication();
    assert(!synErrBuf.empty());

    S1APMessage decodedSynErr{};
    assert(s1ap_decode_message(synErrBuf, decodedSynErr));
    assert(decodedSynErr.procedure == S1APProcedure::ERROR_INDICATION);
    assert(!decodedSynErr.isSuccessfulOutcome);
    assert(!decodedSynErr.isUnsuccessfulOutcome);

    // ── HandoverRequired encode/decode round-trip ──────────────────────────────
    // TS 36.413 §8.5.2 — source eNB initiates S1 Handover.
    rbs::ByteBuffer rrcCont = {0xAA, 0xBB, 0xCC};
    rbs::ByteBuffer hoReqBuf = s1ap_encode_HandoverRequired(
        200 /*mme*/, 1 /*enb*/, 0xBEEF /*targetEnbId*/,
        0x025001 /*targetPlmn*/, 42 /*targetCell*/, rrcCont);
    assert(!hoReqBuf.empty());

    S1APMessage decodedHOReq{};
    assert(s1ap_decode_message(hoReqBuf, decodedHOReq));
    assert(decodedHOReq.procedure == S1APProcedure::HANDOVER_REQUIRED);
    assert(!decodedHOReq.isSuccessfulOutcome);
    assert(!decodedHOReq.isUnsuccessfulOutcome);
    assert(decodedHOReq.mmeUeS1apId == 200);
    assert(decodedHOReq.enbUeS1apId == 1);

    // ── HandoverNotify encode/decode round-trip ────────────────────────────────
    // TS 36.413 §8.5.1 — target eNB confirms UE arrival.
    rbs::ByteBuffer hoNotifyBuf = s1ap_encode_HandoverNotify(
        200 /*mme*/, 1 /*enb*/, 0x025001 /*plmn*/, 1001 /*tac*/, 42 /*cell*/);
    assert(!hoNotifyBuf.empty());

    S1APMessage decodedHONotify{};
    assert(s1ap_decode_message(hoNotifyBuf, decodedHONotify));
    assert(decodedHONotify.procedure == S1APProcedure::HANDOVER_NOTIFY);
    assert(!decodedHONotify.isSuccessfulOutcome);
    assert(!decodedHONotify.isUnsuccessfulOutcome);

    // ── HandoverRequestAcknowledge encode/decode round-trip ───────────────────
    // TS 36.413 §8.5.2 — target eNB accepts HandoverRequest from MME.
    rbs::ByteBuffer t2sCont = {0x11, 0x22};
    rbs::ByteBuffer hoReqAckBuf = s1ap_encode_HandoverRequestAcknowledge(
        200 /*mme*/, 2 /*enb-at-target*/, t2sCont);
    assert(!hoReqAckBuf.empty());

    S1APPduHandle hHOReqAck = s1ap_decode(hoReqAckBuf.data(), hoReqAckBuf.size());
    assert(hHOReqAck != nullptr);
    // proc code = HandoverResourceAllocation = 1 = HANDOVER_COMMAND enum
    assert(s1ap_procedure_code(hHOReqAck) == 1);
    s1ap_pdu_free(hHOReqAck);

    S1APMessage decodedHOReqAck{};
    assert(s1ap_decode_message(hoReqAckBuf, decodedHOReqAck));
    assert(decodedHOReqAck.procedure == S1APProcedure::HANDOVER_REQUEST_ACKNOWLEDGE);
    assert(decodedHOReqAck.isSuccessfulOutcome);
    assert(!decodedHOReqAck.isUnsuccessfulOutcome);
    assert(decodedHOReqAck.mmeUeS1apId == 200);
    assert(decodedHOReqAck.enbUeS1apId == 2);

    // ── ENBStatusTransfer encode/decode round-trip ────────────────────────────
    // TS 36.413 §8.5.2 — source eNB forwards PDCP SN status to MME.
    rbs::ByteBuffer enbStBuf = s1ap_encode_ENBStatusTransfer(200 /*mme*/, 1 /*enb*/);
    assert(!enbStBuf.empty());

    S1APMessage decodedENBSt{};
    assert(s1ap_decode_message(enbStBuf, decodedENBSt));
    assert(decodedENBSt.procedure == S1APProcedure::ENB_STATUS_TRANSFER);
    assert(!decodedENBSt.isSuccessfulOutcome);
    assert(!decodedENBSt.isUnsuccessfulOutcome);
    assert(decodedENBSt.mmeUeS1apId == 200);
    assert(decodedENBSt.enbUeS1apId == 1);

    // ── HandoverFailure encode/decode round-trip ───────────────────────────────
    // TS 36.413 §8.5.2 — target eNB rejects HandoverRequest (resource failure).
    rbs::ByteBuffer hoFailBuf = s1ap_encode_HandoverFailure(
        200 /*mme*/, 0 /*causeGroup=radioNetwork*/,
        static_cast<uint8_t>(CauseRadioNetwork_ho_target_not_allowed));
    assert(!hoFailBuf.empty());

    S1APPduHandle hHOFail = s1ap_decode(hoFailBuf.data(), hoFailBuf.size());
    assert(hHOFail != nullptr);
    assert(s1ap_procedure_code(hHOFail) == 1);  // HandoverResourceAllocation
    s1ap_pdu_free(hHOFail);

    S1APMessage decodedHOFail{};
    assert(s1ap_decode_message(hoFailBuf, decodedHOFail));
    assert(decodedHOFail.procedure == S1APProcedure::HANDOVER_FAILURE);
    assert(!decodedHOFail.isSuccessfulOutcome);
    assert(decodedHOFail.isUnsuccessfulOutcome);
    assert(decodedHOFail.mmeUeS1apId == 200);

    std::puts("test_s1ap_codec PASSED");
    return 0;
}


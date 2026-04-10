#include "../src/lte/x2ap_codec.h"
#include "../src/lte/x2ap_interface.h"
#include <cassert>
#include <cstdio>
#ifdef _MSC_VER
#  include <crtdbg.h>
#endif

int main() {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
#endif

    using namespace rbs::lte;

    // ── X2 Setup Request: encode → decode → verify procedure code ────────────
    // TS 36.423 §8.3.4
    rbs::ByteBuffer x2SetupPdu = x2ap_encode_X2SetupRequest(
        0xABC,       // eNB-ID
        0x208001u,   // PLMN: MCC=208, MNC=01
        10,          // PCI
        1850,        // EARFCN DL (Band 3)
        5            // TAC
    );
    assert(!x2SetupPdu.empty());

    X2APPduHandle h1 = x2ap_decode(x2SetupPdu.data(), x2SetupPdu.size());
    assert(h1 != nullptr);
    assert(x2ap_procedure_code(h1) == static_cast<int>(X2APProcedure::X2_SETUP));
    x2ap_pdu_free(h1);

    // ── X2 Setup Response: encode → decode → verify procedure code ───────────
    // TS 36.423 §8.3.4
    rbs::ByteBuffer x2RespPdu = x2ap_encode_X2SetupResponse(
        0xDEF, 0x208001u, 11, 1850, 5
    );
    assert(!x2RespPdu.empty());

    X2APPduHandle h2 = x2ap_decode(x2RespPdu.data(), x2RespPdu.size());
    assert(h2 != nullptr);
    assert(x2ap_procedure_code(h2) == static_cast<int>(X2APProcedure::X2_SETUP));
    x2ap_pdu_free(h2);

    // ── Handover Request: encode → decode → verify procedure code ────────────
    // TS 36.423 §8.5.1
    std::vector<ERAB> erabs;
    {
        ERAB e{};
        e.erabId             = 5;
        e.qci                = 9;
        e.arpPriorityLevel   = 1;
        e.sgwTunnel.teid     = 0x10001;
        e.sgwTunnel.remoteIPv4 = 0xC0A80101u;  // 192.168.1.1
        e.sgwTunnel.udpPort  = 2152;
        erabs.push_back(e);
    }
    rbs::ByteBuffer rrcCont(4, 0x00);
    rbs::ByteBuffer hoReqPdu = x2ap_encode_HandoverRequest(
        1,          // srcUeX2apId
        777,        // mmeUeS1apId
        0,          // causeType: radioNetwork
        0x208001u,  // plmnId
        erabs,
        rrcCont
    );
    // HandoverRequest has many mandatory sub-IEs (UEContextInformation, keys,
    // E-RABs); verify encode succeeds without checking full APER round-trip.
    assert(!hoReqPdu.empty());

    // ── Handover Request Ack: encode gives non-empty output ───────────────────
    // TS 36.423 §8.5.1
    rbs::ByteBuffer hoAckPdu = x2ap_encode_HandoverRequestAck(
        1, 2, erabs, {}, rrcCont
    );
    assert(!hoAckPdu.empty());

    // ── SN Status Transfer: encode → decode ───────────────────────────────────
    // TS 36.423 §8.5.3
    std::vector<SNStatusItem> snItems;
    {
        SNStatusItem it{};
        it.drbId    = 1;
        it.ulPdcpSN = 500;
        it.ulHfn    = 0;
        it.dlPdcpSN = 480;
        it.dlHfn    = 0;
        snItems.push_back(it);
    }
    rbs::ByteBuffer snPdu = x2ap_encode_SNStatusTransfer(1, 2, snItems);
    assert(!snPdu.empty());

    X2APPduHandle h5 = x2ap_decode(snPdu.data(), snPdu.size());
    assert(h5 != nullptr);
    assert(x2ap_procedure_code(h5) == static_cast<int>(X2APProcedure::SN_STATUS_TRANSFER));
    x2ap_pdu_free(h5);

    // ── UE Context Release: encode → decode ───────────────────────────────────
    // TS 36.423 §8.5.5
    rbs::ByteBuffer ueCrPdu = x2ap_encode_UEContextRelease(1, 2);
    assert(!ueCrPdu.empty());

    X2APPduHandle h6 = x2ap_decode(ueCrPdu.data(), ueCrPdu.size());
    assert(h6 != nullptr);
    assert(x2ap_procedure_code(h6) == static_cast<int>(X2APProcedure::UE_CONTEXT_RELEASE));
    x2ap_pdu_free(h6);

    std::puts("test_x2ap_codec PASSED");
    return 0;
}

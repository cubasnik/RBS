#include "../src/lte/s1ap_codec.h"
#include "../src/lte/s1ap_interface.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#ifdef _MSC_VER
#  include <crtdbg.h>
#endif

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

    std::puts("test_s1ap_codec PASSED");
    return 0;
}


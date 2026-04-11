// test_f1ap_codec.cpp — п.17 F1AP codec (F1 Setup Request/Response/Failure)
// TS 38.473 §8.7.1 (F1 Setup procedure), §9.3 (IEs)
#include "../src/nr/f1ap_codec.h"
#include "../src/nr/nr_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::nr;
using namespace rbs::hal;

static NRCellConfig makeStackCfg() {
    NRCellConfig cfg{};
    cfg.cellId         = 200;
    cfg.nrArfcn        = 630000;
    cfg.scs            = NRScs::SCS30;
    cfg.band           = 78;
    cfg.gnbDuId        = 0x111111111ULL & 0xFFFFFFFFFULL;
    cfg.gnbCuId        = 0x222222222ULL & 0xFFFFFFFFFULL;
    cfg.nrCellIdentity = 0x333333333ULL & 0xFFFFFFFFFULL;
    cfg.nrPci          = 100;
    cfg.ssbPeriodMs    = 20;
    cfg.tac            = 5;
    cfg.mcc            = 1;
    cfg.mnc            = 1;
    return cfg;
}

// ── Test 1: F1 Setup Request encode / decode round-trip ──────────
static void test_f1_setup_request_roundtrip() {
    F1SetupRequest req{};
    req.transactionId = 7;
    req.gnbDuId       = 0xABCDEF012ULL & 0xFFFFFFFFFULL;
    req.gnbDuName     = "gNB-DU-Test";

    F1ServedCell cell{};
    cell.nrCellIdentity = 0x123456789ULL & 0xFFFFFFFFFULL;
    cell.nrArfcn        = 640000;
    cell.scs            = NRScs::SCS15;
    cell.pci            = 42;
    cell.tac            = 10;
    req.servedCells.push_back(cell);

    ByteBuffer pdu = encodeF1SetupRequest(req);
    assert(!pdu.empty());

    F1SetupRequest out{};
    assert(decodeF1SetupRequest(pdu, out));
    assert(out.transactionId              == req.transactionId);
    assert(out.gnbDuId                    == req.gnbDuId);
    assert(out.gnbDuName                  == req.gnbDuName);
    assert(out.servedCells.size()         == 1);
    assert(out.servedCells[0].nrCellIdentity == cell.nrCellIdentity);
    assert(out.servedCells[0].nrArfcn        == cell.nrArfcn);
    assert(out.servedCells[0].scs            == cell.scs);
    assert(out.servedCells[0].pci            == cell.pci);
    assert(out.servedCells[0].tac            == cell.tac);
    std::puts("  test_f1_setup_request_roundtrip PASSED");
}

// ── Test 2: Multiple served cells ────────────────────────────────
static void test_f1_setup_request_multiple_cells() {
    F1SetupRequest req{};
    req.transactionId = 1;
    req.gnbDuId  = 0x1;
    req.gnbDuName = "multi-cell-DU";
    for (int i = 0; i < 3; ++i) {
        F1ServedCell c{};
        c.nrCellIdentity = static_cast<uint64_t>(i + 1);
        c.nrArfcn        = static_cast<uint32_t>(600000 + i * 5000);
        c.scs            = NRScs::SCS30;
        c.pci            = static_cast<uint16_t>(i * 3);
        c.tac            = static_cast<uint16_t>(i + 1);
        req.servedCells.push_back(c);
    }
    ByteBuffer pdu = encodeF1SetupRequest(req);
    F1SetupRequest out{};
    assert(decodeF1SetupRequest(pdu, out));
    assert(out.servedCells.size() == 3);
    for (int i = 0; i < 3; ++i) {
        assert(out.servedCells[i].nrArfcn == static_cast<uint32_t>(600000 + i * 5000));
        assert(out.servedCells[i].pci     == static_cast<uint16_t>(i * 3));
    }
    std::puts("  test_f1_setup_request_multiple_cells PASSED");
}

// ── Test 3: Empty DU name (absent optional IE) ───────────────────
static void test_f1_setup_request_no_name() {
    F1SetupRequest req{};
    req.transactionId = 0;
    req.gnbDuId       = 0x999;
    req.gnbDuName     = "";
    F1ServedCell c{};
    c.nrCellIdentity = 1; c.nrArfcn = 620000; c.scs = NRScs::SCS60;
    c.pci = 501; c.tac = 2;
    req.servedCells.push_back(c);

    ByteBuffer pdu = encodeF1SetupRequest(req);
    F1SetupRequest out{};
    assert(decodeF1SetupRequest(pdu, out));
    assert(out.gnbDuName.empty());
    assert(out.gnbDuId == 0x999);
    std::puts("  test_f1_setup_request_no_name PASSED");
}

// ── Test 4: F1 Setup Response encode / decode round-trip ─────────
static void test_f1_setup_response_roundtrip() {
    F1SetupResponse rsp{};
    rsp.transactionId = 7;
    rsp.gnbCuName     = "gNB-CU-Core";
    rsp.activatedCells.push_back(0x123456789ULL & 0xFFFFFFFFFULL);
    rsp.activatedCells.push_back(0x987654321ULL & 0xFFFFFFFFFULL);

    ByteBuffer pdu = encodeF1SetupResponse(rsp);
    assert(!pdu.empty());

    F1SetupResponse out{};
    assert(decodeF1SetupResponse(pdu, out));
    assert(out.transactionId           == rsp.transactionId);
    assert(out.gnbCuName               == rsp.gnbCuName);
    assert(out.activatedCells.size()   == 2);
    assert(out.activatedCells[0]       == rsp.activatedCells[0]);
    assert(out.activatedCells[1]       == rsp.activatedCells[1]);
    std::puts("  test_f1_setup_response_roundtrip PASSED");
}

// ── Test 5: F1 Setup Response with no activated cells ────────────
static void test_f1_setup_response_empty_cells() {
    F1SetupResponse rsp{};
    rsp.transactionId = 3;
    rsp.gnbCuName     = "";
    ByteBuffer pdu = encodeF1SetupResponse(rsp);
    F1SetupResponse out{};
    assert(decodeF1SetupResponse(pdu, out));
    assert(out.activatedCells.empty());
    std::puts("  test_f1_setup_response_empty_cells PASSED");
}

// ── Test 6: F1 Setup Failure encode / decode ─────────────────────
static void test_f1_setup_failure_roundtrip() {
    F1SetupFailure fail{};
    fail.transactionId = 5;
    fail.causeType     = 2;
    fail.causeValue    = 7;

    ByteBuffer pdu = encodeF1SetupFailure(fail);
    assert(!pdu.empty());

    F1SetupFailure out{};
    assert(decodeF1SetupFailure(pdu, out));
    assert(out.transactionId == fail.transactionId);
    assert(out.causeType     == fail.causeType);
    assert(out.causeValue    == fail.causeValue);
    std::puts("  test_f1_setup_failure_roundtrip PASSED");
}

// ── Test 7: Malformed PDU → decode returns false ─────────────────
static void test_f1_decode_malformed() {
    ByteBuffer empty{};
    F1SetupRequest req{};
    assert(!decodeF1SetupRequest(empty, req));

    ByteBuffer garbage{0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01};
    assert(!decodeF1SetupRequest(garbage, req));

    F1SetupResponse rsp{};
    assert(!decodeF1SetupResponse(garbage, rsp));
    std::puts("  test_f1_decode_malformed PASSED");
}

// ── Test 8: Cross-decode guard (request PDU ≠ response) ──────────
static void test_f1_cross_decode_fails() {
    F1SetupRequest req{};
    req.transactionId = 1; req.gnbDuId = 1;
    F1ServedCell c{}; c.nrCellIdentity = 1; c.nrArfcn = 600000;
    c.scs = NRScs::SCS15; c.pci = 0; c.tac = 1;
    req.servedCells.push_back(c);

    ByteBuffer reqPdu = encodeF1SetupRequest(req);
    F1SetupResponse rsp{};
    assert(!decodeF1SetupResponse(reqPdu, rsp));
    std::puts("  test_f1_cross_decode_fails PASSED");
}

// ── Test 9: NRStack builds valid F1 Setup Request ─────────────────
static void test_nr_stack_f1_setup_request() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeStackCfg());

    ByteBuffer pdu = stack.buildF1SetupRequest();
    assert(!pdu.empty());

    F1SetupRequest decoded{};
    assert(decodeF1SetupRequest(pdu, decoded));
    assert(decoded.gnbDuId == (0x111111111ULL & 0xFFFFFFFFFULL));
    assert(!decoded.servedCells.empty());
    assert(decoded.servedCells[0].nrArfcn == 630000);
    std::puts("  test_nr_stack_f1_setup_request PASSED");
}

// ── Test 10: NRStack handles F1 Setup Response ────────────────────
static void test_nr_stack_handle_f1_response() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeStackCfg());

    F1SetupResponse rsp{};
    rsp.transactionId = 0;
    rsp.gnbCuName     = "CU-Core";
    rsp.activatedCells.push_back(0x333333333ULL & 0xFFFFFFFFFULL);
    ByteBuffer pdu = encodeF1SetupResponse(rsp);

    assert(stack.handleF1SetupResponse(pdu));
    std::puts("  test_nr_stack_handle_f1_response PASSED");
}

// ── Test 11: NRStack admit / release UE ──────────────────────────
static void test_nr_stack_ue_admission() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeStackCfg());

    uint16_t c1 = stack.admitUE(100000000001ULL);
    uint16_t c2 = stack.admitUE(100000000002ULL);
    assert(c1 != 0);
    assert(c2 != 0);
    assert(c1 != c2);
    assert(stack.connectedUECount() == 2);
    stack.releaseUE(c1);
    assert(stack.connectedUECount() == 1);
    stack.releaseUE(c2);
    assert(stack.connectedUECount() == 0);
    std::puts("  test_nr_stack_ue_admission PASSED");
}

// ── Test 12: NCI 36-bit boundary ─────────────────────────────────
static void test_f1_nci_36bit_boundary() {
    F1SetupRequest req{};
    req.transactionId = 0;
    req.gnbDuId       = 0xFFFFFFFFFULL;
    F1ServedCell c{};
    c.nrCellIdentity = 0xFFFFFFFFFULL;
    c.nrArfcn        = 0xFFFFFFFFu;
    c.scs            = NRScs::SCS120;
    c.pci            = 1007;
    c.tac            = 0xFFFF;
    req.servedCells.push_back(c);

    ByteBuffer pdu = encodeF1SetupRequest(req);
    F1SetupRequest out{};
    assert(decodeF1SetupRequest(pdu, out));
    assert(out.gnbDuId == 0xFFFFFFFFFULL);
    assert(out.servedCells[0].nrCellIdentity == 0xFFFFFFFFFULL);
    assert(out.servedCells[0].pci == 1007);
    std::puts("  test_f1_nci_36bit_boundary PASSED");
}

int main() {
    std::puts("=== test_f1ap_codec ===");
    test_f1_setup_request_roundtrip();
    test_f1_setup_request_multiple_cells();
    test_f1_setup_request_no_name();
    test_f1_setup_response_roundtrip();
    test_f1_setup_response_empty_cells();
    test_f1_setup_failure_roundtrip();
    test_f1_decode_malformed();
    test_f1_cross_decode_fails();
    test_nr_stack_f1_setup_request();
    test_nr_stack_handle_f1_response();
    test_nr_stack_ue_admission();
    test_f1_nci_36bit_boundary();
    std::puts("test_f1ap_codec PASSED");
    return 0;
}

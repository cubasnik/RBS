// test_endc.cpp — EN-DC NSA Option 3/3a/3x tests (TS 37.340)
#include "../src/lte/x2ap_link.h"
#include "../src/nr/nr_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <optional>

using namespace rbs;
using namespace rbs::lte;
using namespace rbs::nr;

// ── helpers ──────────────────────────────────────────────────────────────────

static NRCellConfig makeNRCfg(CellId id = 4, uint16_t pci = 400) {
    NRCellConfig c{};
    c.cellId         = id;
    c.nrArfcn        = 627264;
    c.scs            = NRScs::SCS30;
    c.band           = 78;
    c.gnbDuId        = 1;
    c.gnbCuId        = 1;
    c.nrCellIdentity = 1;
    c.nrPci          = pci;
    c.ssbPeriodMs    = 20;
    c.tac            = 1;
    c.mcc            = 250;
    c.mnc            = 1;
    c.numTxRx        = 4;
    return c;
}

static DCBearerConfig makeScgBearer(uint8_t bearerId = 5) {
    DCBearerConfig b{};
    b.enbBearerId = bearerId;
    b.type        = DCBearerType::SCG;
    b.scgLegDrbId = 1;
    b.nrCellId    = 1;
    return b;
}

static DCBearerConfig makeSplitMNBearer(uint8_t bearerId = 6) {
    DCBearerConfig b{};
    b.enbBearerId  = bearerId;
    b.type         = DCBearerType::SPLIT_MN;
    b.mcgLegDrbId  = 1;
    b.scgLegDrbId  = 2;
    b.nrCellId     = 1;
    return b;
}

static DCBearerConfig makeSplitSNBearer(uint8_t bearerId = 7) {
    DCBearerConfig b{};
    b.enbBearerId  = bearerId;
    b.type         = DCBearerType::SPLIT_SN;
    b.mcgLegDrbId  = 1;
    b.scgLegDrbId  = 2;
    b.nrCellId     = 1;
    return b;
}

// ── test 1: X2AP SgNB Addition Request (Option 3a) ───────────────────────────
static void test_option3a_sgnb_addition_request() {
    X2APLink x2("MN-eNB");
    assert(x2.connect(1, "127.0.0.1", 36422));

    std::vector<DCBearerConfig> bearers = {makeScgBearer()};
    bool ok = x2.sgNBAdditionRequest(0x0001, ENDCOption::OPTION_3A, bearers);
    assert(ok);

    // Verify message is in tx queue (sendX2APMsg succeeds)
    printf("  test_option3a_sgnb_addition_request: PASS\n");
}

// ── test 2: X2AP SgNB Addition Ack assigns NR C-RNTIs ────────────────────────
static void test_option3a_sgnb_addition_ack() {
    X2APLink x2("MN-eNB");
    x2.connect(1, "127.0.0.1", 36422);

    std::vector<DCBearerConfig> bearers = {makeScgBearer()};
    x2.sgNBAdditionRequest(0x0002, ENDCOption::OPTION_3A, bearers);

    // Ack: should assign snCrnti to each bearer
    bool ok = x2.sgNBAdditionRequestAck(0x0002, bearers);
    assert(ok);
    assert(bearers[0].snCrnti != 0);

    printf("  test_option3a_sgnb_addition_ack: PASS (snCrnti=0x%04X)\n",
           bearers[0].snCrnti);
}

// ── test 3: SgNB Addition Ack on unknown RNTI fails ──────────────────────────
static void test_sgnb_ack_unknown_rnti_fails() {
    X2APLink x2("MN-eNB");
    x2.connect(1, "127.0.0.1", 36422);

    std::vector<DCBearerConfig> bearers = {makeScgBearer()};
    // Ack for rnti 0x0099 without prior Request
    bool ok = x2.sgNBAdditionRequestAck(0x0099, bearers);
    assert(!ok);

    printf("  test_sgnb_ack_unknown_rnti_fails: PASS\n");
}

// ── test 4: SgNB Reject clears state ─────────────────────────────────────────
static void test_sgnb_rejection() {
    X2APLink x2("MN-eNB");
    x2.connect(1, "127.0.0.1", 36422);

    std::vector<DCBearerConfig> bearers = {makeScgBearer()};
    x2.sgNBAdditionRequest(0x0003, ENDCOption::OPTION_3A, bearers);

    bool ok = x2.sgNBAdditionRequestReject(0x0003, "capacity-overload");
    assert(ok);

    // After reject, Ack for same rnti must fail
    bool ackOk = x2.sgNBAdditionRequestAck(0x0003, bearers);
    assert(!ackOk);

    printf("  test_sgnb_rejection: PASS\n");
}

// ── test 5: SgNB Release ─────────────────────────────────────────────────────
static void test_sgnb_release() {
    X2APLink x2("MN-eNB");
    x2.connect(1, "127.0.0.1", 36422);

    std::vector<DCBearerConfig> bearers = {makeScgBearer()};
    x2.sgNBAdditionRequest(0x0004, ENDCOption::OPTION_3A, bearers);
    x2.sgNBAdditionRequestAck(0x0004, bearers);

    assert(x2.sgNBReleaseRequest(0x0004));
    assert(x2.sgNBReleaseRequestAck(0x0004));

    printf("  test_sgnb_release: PASS\n");
}

// ── test 6: SgNB Modification ────────────────────────────────────────────────
static void test_sgnb_modification() {
    X2APLink x2("MN-eNB");
    x2.connect(1, "127.0.0.1", 36422);

    std::vector<DCBearerConfig> bearers = {makeScgBearer()};
    x2.sgNBAdditionRequest(0x0005, ENDCOption::OPTION_3, bearers);

    DCBearerConfig modified = makeScgBearer();
    modified.scgLegDrbId = 3;  // change DRB ID
    assert(x2.sgNBModificationRequest(0x0005, modified));
    assert(x2.sgNBModificationRequestAck(0x0005, modified));

    printf("  test_sgnb_modification: PASS\n");
}

// ── test 7: NRStack accepts SCG bearer (Option 3a) ───────────────────────────
static void test_nr_accept_scg_bearer() {
    auto rf = std::make_shared<hal::RFHardware>(4, 4);
    NRStack nr(rf, makeNRCfg());
    assert(nr.start());

    DCBearerConfig bearer = makeScgBearer();
    RNTI lteCrnti = 0x0010;
    uint16_t nrCrnti = nr.acceptSCGBearer(lteCrnti, bearer);

    assert(nrCrnti != 0);
    assert(nr.scgCrnti(lteCrnti) == nrCrnti);
    assert(nr.endcOption(lteCrnti).has_value());
    assert(nr.endcOption(lteCrnti).value() == ENDCOption::OPTION_3A);
    assert(nr.endcUECount() == 1);
    // NR UE map also sees this UE
    assert(nr.connectedUECount() >= 1);

    nr.stop();
    printf("  test_nr_accept_scg_bearer: PASS (nrCrnti=%u)\n", nrCrnti);
}

// ── test 8: NRStack releases SCG bearer ──────────────────────────────────────
static void test_nr_release_scg_bearer() {
    auto rf = std::make_shared<hal::RFHardware>(4, 4);
    NRStack nr(rf, makeNRCfg());
    nr.start();

    RNTI lteCrnti = 0x0020;
    nr.acceptSCGBearer(lteCrnti, makeScgBearer());
    assert(nr.endcUECount() == 1);

    nr.releaseSCGBearer(lteCrnti);
    assert(nr.endcUECount() == 0);
    assert(nr.scgCrnti(lteCrnti) == 0);
    assert(!nr.endcOption(lteCrnti).has_value());

    nr.stop();
    printf("  test_nr_release_scg_bearer: PASS\n");
}

// ── test 9: NRStack SCG for non-existing UE returns 0 ────────────────────────
static void test_nr_scg_crnti_missing() {
    auto rf = std::make_shared<hal::RFHardware>(4, 4);
    NRStack nr(rf, makeNRCfg());
    nr.start();

    assert(nr.scgCrnti(0xDEAD) == 0);
    assert(!nr.endcOption(0xDEAD).has_value());

    nr.stop();
    printf("  test_nr_scg_crnti_missing: PASS\n");
}

// ── test 10: Multiple UEs get distinct NR C-RNTIs ────────────────────────────
static void test_nr_multiple_scg_ues() {
    auto rf = std::make_shared<hal::RFHardware>(4, 4);
    NRStack nr(rf, makeNRCfg());
    nr.start();

    uint16_t c1 = nr.acceptSCGBearer(0x0030, makeScgBearer(5));
    uint16_t c2 = nr.acceptSCGBearer(0x0031, makeScgBearer(5));
    uint16_t c3 = nr.acceptSCGBearer(0x0032, makeScgBearer(5));

    assert(c1 != 0 && c2 != 0 && c3 != 0);
    assert(c1 != c2 && c2 != c3 && c1 != c3);
    assert(nr.endcUECount() == 3);

    nr.stop();
    printf("  test_nr_multiple_scg_ues: PASS (crnti=%u/%u/%u)\n", c1, c2, c3);
}

// ── test 11: Option 3 (SPLIT_MN) bearer type stored correctly ────────────────
static void test_nr_option3_split_mn() {
    auto rf = std::make_shared<hal::RFHardware>(4, 4);
    NRStack nr(rf, makeNRCfg());
    nr.start();

    DCBearerConfig bearer = makeSplitMNBearer();
    uint16_t nrCrnti = nr.acceptSCGBearer(0x0040, bearer);
    assert(nrCrnti != 0);
    assert(nr.endcOption(0x0040).value() == ENDCOption::OPTION_3);

    nr.stop();
    printf("  test_nr_option3_split_mn: PASS\n");
}

// ── test 12: Option 3x (SPLIT_SN) bearer type stored correctly ───────────────
static void test_nr_option3x_split_sn() {
    auto rf = std::make_shared<hal::RFHardware>(4, 4);
    NRStack nr(rf, makeNRCfg());
    nr.start();

    DCBearerConfig bearer = makeSplitSNBearer();
    uint16_t nrCrnti = nr.acceptSCGBearer(0x0050, bearer);
    assert(nrCrnti != 0);
    assert(nr.endcOption(0x0050).value() == ENDCOption::OPTION_3X);

    nr.stop();
    printf("  test_nr_option3x_split_sn: PASS\n");
}

// ── test 13: X2AP Option 3 / Option 3x request doesn't break things ──────────
static void test_x2ap_option3_and_3x_requests() {
    X2APLink x2("MN-eNB");
    x2.connect(1, "127.0.0.1", 36422);

    {
        std::vector<DCBearerConfig> bearers = {makeSplitMNBearer()};
        assert(x2.sgNBAdditionRequest(0x0060, ENDCOption::OPTION_3, bearers));
        assert(x2.sgNBAdditionRequestAck(0x0060, bearers));
        assert(bearers[0].snCrnti != 0);
        x2.sgNBReleaseRequest(0x0060);
    }
    {
        std::vector<DCBearerConfig> bearers = {makeSplitSNBearer()};
        assert(x2.sgNBAdditionRequest(0x0061, ENDCOption::OPTION_3X, bearers));
        assert(x2.sgNBAdditionRequestAck(0x0061, bearers));
        assert(bearers[0].snCrnti != 0);
        x2.sgNBReleaseRequest(0x0061);
    }

    printf("  test_x2ap_option3_and_3x_requests: PASS\n");
}

// ── test 14: NRStack acceptSCGBearer on stopped stack returns 0 ───────────────
static void test_nr_scg_on_stopped_stack() {
    auto rf = std::make_shared<hal::RFHardware>(4, 4);
    NRStack nr(rf, makeNRCfg());
    // do NOT call nr.start()

    uint16_t c = nr.acceptSCGBearer(0x0070, makeScgBearer());
    assert(c == 0);

    printf("  test_nr_scg_on_stopped_stack: PASS\n");
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    printf("=== test_endc ===\n");

    test_option3a_sgnb_addition_request();
    test_option3a_sgnb_addition_ack();
    test_sgnb_ack_unknown_rnti_fails();
    test_sgnb_rejection();
    test_sgnb_release();
    test_sgnb_modification();
    test_nr_accept_scg_bearer();
    test_nr_release_scg_bearer();
    test_nr_scg_crnti_missing();
    test_nr_multiple_scg_ues();
    test_nr_option3_split_mn();
    test_nr_option3x_split_sn();
    test_x2ap_option3_and_3x_requests();
    test_nr_scg_on_stopped_stack();

    printf("=== all 14 tests passed ===\n");
    return 0;
}

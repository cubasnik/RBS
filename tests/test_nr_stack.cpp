#include "../src/nr/nr_stack.h"
#include "../src/hal/rf_hardware.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <chrono>
#include <thread>
#include <unordered_map>

using namespace rbs;
using namespace rbs::nr;
using namespace rbs::hal;

static NRCellConfig makeCfg() {
    NRCellConfig cfg{};
    cfg.cellId = 900;
    cfg.nrArfcn = 620000;
    cfg.scs = NRScs::SCS30;
    cfg.band = 78;
    cfg.gnbDuId = 0x1001;
    cfg.gnbCuId = 0x2002;
    cfg.nrCellIdentity = 0xABCDEF;
    cfg.nrPci = 101;
    cfg.ssbPeriodMs = 20;
    cfg.tac = 1;
    cfg.mcc = 250;
    cfg.mnc = 1;
    cfg.cuAddr = "127.0.0.1";
    cfg.cuPort = 38472;
    cfg.numTxRx = 2;
    return cfg;
}

static void test_nr_stack_qos_flow_and_dl_path() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeCfg());
    assert(stack.start());

    const uint16_t crnti = stack.admitUE(250010000000001ULL, 9);
    assert(crnti != 0);
    assert(stack.configureQoSFlow(crnti, 9, 3));
    assert(stack.resolveDrbForQfi(crnti, 9) == 3);

    ByteBuffer payload = {0xAA, 0xBB, 0xCC};
    ByteBuffer pdcp;
    assert(stack.submitDlSdapData(crnti, 9, payload, pdcp));
    assert(pdcp.size() == payload.size() + 4);  // 3B PDCP SN + 1B SDAP hdr + payload
    assert((pdcp[3] & 0x3F) == 9);             // SDAP QFI in first SDAP byte
    assert(pdcp[4] == 0xAA && pdcp[5] == 0xBB && pdcp[6] == 0xCC);

    stack.stop();
    std::puts("  test_nr_stack_qos_flow_and_dl_path PASSED");
}

static void test_nr_stack_scheduler_cqi_update() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeCfg());
    assert(stack.start());

    const uint16_t ueLow = stack.admitUE(250010000000010ULL, 6);
    const uint16_t ueHigh = stack.admitUE(250010000000011ULL, 12);
    assert(ueLow != 0 && ueHigh != 0);

    auto grants = stack.scheduleDl(20);
    assert(grants.size() == 2);
    assert(grants[0].crnti == ueHigh);
    for (const auto& g : grants) {
        assert(stack.reportHarqFeedback(g.crnti, g.dci.harqId, true));
    }

    assert(stack.updateUeCqi(ueLow, 14));
    int guard = 16;
    do {
        grants = stack.scheduleDl(20);
    } while (grants.empty() && guard-- > 0);
    assert(grants.size() == 2);
    assert(grants[0].crnti == ueLow);

    stack.stop();
    std::puts("  test_nr_stack_scheduler_cqi_update PASSED");
}

static void test_nr_stack_dl_queue_drain() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeCfg());
    assert(stack.start());

    const uint16_t crnti = stack.admitUE(250010000000100ULL, 12);
    assert(crnti != 0);
    assert(stack.configureQoSFlow(crnti, 7, 2));

    ByteBuffer payload(2000, 0x5A);
    ByteBuffer pdcp;
    assert(stack.submitDlSdapData(crnti, 7, payload, pdcp));
    assert(stack.pendingDlBytes(crnti) > 0);

    int guard = 64;
    while (stack.pendingDlBytes(crnti) > 0 && guard-- > 0) {
        const auto grants = stack.scheduleDl(20);
        if (grants.empty()) {
            continue;
        }
        for (const auto& g : grants) {
            assert(stack.reportHarqFeedback(g.crnti, g.dci.harqId, true));
        }
    }

    assert(stack.pendingDlBytes(crnti) == 0);
    stack.stop();
    std::puts("  test_nr_stack_dl_queue_drain PASSED");
}

static void test_nr_stack_auto_scheduler_queue_drain() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeCfg());
    assert(stack.start());
    stack.setAutoDlScheduling(true, 20);

    const uint16_t crnti = stack.admitUE(250010000000101ULL, 12);
    assert(crnti != 0);
    assert(stack.configureQoSFlow(crnti, 8, 2));

    ByteBuffer payload(1200, 0x33);
    ByteBuffer pdcp;
    assert(stack.submitDlSdapData(crnti, 8, payload, pdcp));
    assert(stack.pendingDlBytes(crnti) > 0);

    int guard = 200;
    while (stack.pendingDlBytes(crnti) > 0 && guard-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    assert(stack.pendingDlBytes(crnti) == 0);
    stack.stop();
    std::puts("  test_nr_stack_auto_scheduler_queue_drain PASSED");
}

static void test_nr_stack_fairness_and_harq_feedback_loop() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeCfg());
    assert(stack.start());

    const uint16_t ue1 = stack.admitUE(250010000000201ULL, 10);
    const uint16_t ue2 = stack.admitUE(250010000000202ULL, 10);
    assert(ue1 != 0 && ue2 != 0);
    assert(stack.configureQoSFlow(ue1, 9, 3));
    assert(stack.configureQoSFlow(ue2, 9, 3));

    ByteBuffer payload(1800, 0x44);
    ByteBuffer pdcp;
    assert(stack.submitDlSdapData(ue1, 9, payload, pdcp));
    assert(stack.submitDlSdapData(ue2, 9, payload, pdcp));

    bool nackedUe1 = false;
    bool nackedUe2 = false;
    int grantsUe1 = 0;
    int grantsUe2 = 0;

    int guard = 200;
    while ((stack.pendingDlBytes(ue1) > 0 || stack.pendingDlBytes(ue2) > 0) && guard-- > 0) {
        const auto grants = stack.scheduleDl(20);
        if (grants.empty()) {
            continue;
        }

        for (const auto& g : grants) {
            bool ack = true;
            if (g.crnti == ue1 && !nackedUe1 && g.dci.ndi) {
                ack = false;
                nackedUe1 = true;
            }
            if (g.crnti == ue2 && !nackedUe2 && g.dci.ndi) {
                ack = false;
                nackedUe2 = true;
            }
            assert(stack.reportHarqFeedback(g.crnti, g.dci.harqId, ack));

            if (g.crnti == ue1) ++grantsUe1;
            if (g.crnti == ue2) ++grantsUe2;
        }
    }

    assert(stack.pendingDlBytes(ue1) == 0);
    assert(stack.pendingDlBytes(ue2) == 0);
    assert(nackedUe1 && nackedUe2);
    assert(grantsUe1 > 0 && grantsUe2 > 0);
    const int diff = grantsUe1 > grantsUe2 ? (grantsUe1 - grantsUe2) : (grantsUe2 - grantsUe1);
    assert(diff <= 6);

    stack.stop();
    std::puts("  test_nr_stack_fairness_and_harq_feedback_loop PASSED");
}

static void test_nr_stack_long_mixed_traffic_fairness() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeCfg());
    assert(stack.start());

    const uint16_t ueLow = stack.admitUE(250010000000301ULL, 6);
    const uint16_t ueMid = stack.admitUE(250010000000302ULL, 10);
    const uint16_t ueHigh = stack.admitUE(250010000000303ULL, 14);
    assert(ueLow != 0 && ueMid != 0 && ueHigh != 0);

    assert(stack.configureQoSFlow(ueLow,  5, 2));
    assert(stack.configureQoSFlow(ueMid,  7, 3));
    assert(stack.configureQoSFlow(ueHigh, 9, 4));

    std::unordered_map<uint16_t, int> grantCount;
    grantCount[ueLow] = 0;
    grantCount[ueMid] = 0;
    grantCount[ueHigh] = 0;

    std::unordered_map<uint16_t, uint32_t> deliveredBytes;
    deliveredBytes[ueLow] = 0;
    deliveredBytes[ueMid] = 0;
    deliveredBytes[ueHigh] = 0;

    ByteBuffer pdu;
    for (int tick = 0; tick < 120; ++tick) {
        if (tick % 20 == 0) {
            ByteBuffer lowPayload(400, 0x11);
            assert(stack.submitDlSdapData(ueLow, 5, lowPayload, pdu));
        }
        if (tick % 12 == 0) {
            ByteBuffer midPayload(700, 0x22);
            assert(stack.submitDlSdapData(ueMid, 7, midPayload, pdu));
        }
        if (tick % 8 == 0) {
            ByteBuffer highPayload(1000, 0x33);
            assert(stack.submitDlSdapData(ueHigh, 9, highPayload, pdu));
        }

        const auto grants = stack.scheduleDl(24);
        if (grants.empty()) {
            continue;
        }

        for (const auto& g : grants) {
            grantCount[g.crnti] += 1;
            deliveredBytes[g.crnti] += g.dci.tbsBytes;

            bool ack = true;
            // Deterministic stress: occasional NACK to exercise HARQ loop.
            if ((g.crnti == ueLow && (tick % 17 == 0) && g.dci.ndi) ||
                (g.crnti == ueMid && (tick % 29 == 0) && g.dci.ndi)) {
                ack = false;
            }
            assert(stack.reportHarqFeedback(g.crnti, g.dci.harqId, ack));
        }
    }

    // Drain tail queues with ACK feedback.
    int guard = 300;
    while ((stack.pendingDlBytes(ueLow) > 0 ||
            stack.pendingDlBytes(ueMid) > 0 ||
            stack.pendingDlBytes(ueHigh) > 0) && guard-- > 0) {
        const auto grants = stack.scheduleDl(24);
        if (grants.empty()) {
            continue;
        }
        for (const auto& g : grants) {
            grantCount[g.crnti] += 1;
            deliveredBytes[g.crnti] += g.dci.tbsBytes;
            assert(stack.reportHarqFeedback(g.crnti, g.dci.harqId, true));
        }
    }

    assert(stack.pendingDlBytes(ueLow) == 0);
    assert(stack.pendingDlBytes(ueMid) == 0);
    assert(stack.pendingDlBytes(ueHigh) == 0);

    assert(grantCount[ueLow] > 0);
    assert(grantCount[ueMid] > 0);
    assert(grantCount[ueHigh] > 0);

    // Under mixed load and CQI, high-CQI UE should not be starved and should
    // deliver at least as much as low-CQI UE in this synthetic scenario.
    assert(deliveredBytes[ueHigh] >= deliveredBytes[ueLow]);
    assert(deliveredBytes[ueMid] >= deliveredBytes[ueLow] / 2);

    stack.stop();
    std::puts("  test_nr_stack_long_mixed_traffic_fairness PASSED");
}

int main() {
    std::puts("=== test_nr_stack ===");
    test_nr_stack_qos_flow_and_dl_path();
    test_nr_stack_scheduler_cqi_update();
    test_nr_stack_dl_queue_drain();
    test_nr_stack_auto_scheduler_queue_drain();
    test_nr_stack_fairness_and_harq_feedback_loop();
    test_nr_stack_long_mixed_traffic_fairness();
    std::puts("test_nr_stack PASSED");
    return 0;
}

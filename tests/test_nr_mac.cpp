#include "../src/nr/nr_mac.h"
#include "../src/nr/nr_sdap.h"
#include "../src/nr/nr_pdcp.h"
#include "../src/common/types.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::nr;

static void ackAll(NRMac& mac, const std::vector<NRScheduleGrant>& grants) {
    for (const auto& g : grants) {
        assert(mac.reportHarqFeedback(g.crnti, g.dci.harqId, true));
    }
}

static void test_nr_mac_schedule_prioritizes_cqi() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(101, 7));
    assert(mac.addUE(102, 12));
    const auto grants = mac.scheduleDl(24);
    assert(grants.size() == 2);
    assert(grants[0].crnti == 102);
    assert(grants[0].mcs >= grants[1].mcs);
    assert(grants[0].prbs + grants[1].prbs <= 24);
    assert(grants[0].prbs + grants[1].prbs >= 12);
    std::puts("  test_nr_mac_schedule_prioritizes_cqi PASSED");
}

static void test_nr_mac_qfi_to_drb_mapping() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(201, 10));
    assert(mac.setQfiMapping(201, 7, 3));
    assert(mac.resolveDrb(201, 7) == 3);
    assert(mac.resolveDrb(201, 9) == 0);
    std::puts("  test_nr_mac_qfi_to_drb_mapping PASSED");
}

static void test_nr_sdap_header_roundtrip() {
    NRSDAP sdap;
    ByteBuffer payload = {0xDE, 0xAD, 0xBE, 0xEF};
    ByteBuffer pdu = sdap.encodeDataPdu(1, 9, payload);
    assert(!pdu.empty());

    uint8_t qfi = 0;
    ByteBuffer decoded;
    assert(sdap.decodeDataPdu(pdu, qfi, decoded));
    assert(qfi == 9);
    assert(decoded == payload);
    std::puts("  test_nr_sdap_header_roundtrip PASSED");
}

static void test_nr_pdcp_sn18_increment() {
    NRPDCP pdcp;
    ByteBuffer payload = {0x01, 0x02};

    ByteBuffer pdu0 = pdcp.encodeDataPdu(1, 1, payload);
    ByteBuffer pdu1 = pdcp.encodeDataPdu(1, 1, payload);

    uint32_t sn0 = 0;
    uint32_t sn1 = 0;
    ByteBuffer out;
    assert(pdcp.decodeDataPdu(pdu0, sn0, out));
    assert(pdcp.decodeDataPdu(pdu1, sn1, out));
    assert(sn0 == 0);
    assert(sn1 == 1);
    assert(pdcp.currentSn(1, 1) == 2);
    std::puts("  test_nr_pdcp_sn18_increment PASSED");
}

static void test_nr_slots_per_ms_from_scs() {
    NRMac mac15(NRScs::SCS15);
    NRMac mac30(NRScs::SCS30);
    NRMac mac60(NRScs::SCS60);
    assert(mac15.slotsPerMs() == 1);
    assert(mac30.slotsPerMs() == 2);
    assert(mac60.slotsPerMs() == 4);
    std::puts("  test_nr_slots_per_ms_from_scs PASSED");
}

static void test_nr_mac_bwp_switching() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(301, 6));
    auto grants0 = mac.scheduleDl(12);
    assert(!grants0.empty());
    assert(grants0[0].bwpId == 0);
    assert(mac.currentBwp(301) == 0);
    ackAll(mac, grants0);

    assert(mac.updateUECqi(301, 13));
    auto grants1 = mac.scheduleDl(12);
    assert(!grants1.empty());
    assert(grants1[0].bwpId == 1);
    assert(mac.currentBwp(301) == 1);
    ackAll(mac, grants1);

    // Hysteresis: CQI in the middle band should keep BWP1.
    assert(mac.updateUECqi(301, 9));
    auto grants2 = mac.scheduleDl(12);
    assert(!grants2.empty());
    assert(grants2[0].bwpId == 1);
    ackAll(mac, grants2);

    // Switch down only when CQI falls below/down threshold.
    assert(mac.updateUECqi(301, 7));
    auto grants3 = mac.scheduleDl(12);
    assert(!grants3.empty());
    assert(grants3[0].bwpId == 0);
    ackAll(mac, grants3);
    std::puts("  test_nr_mac_bwp_switching PASSED");
}

static void test_nr_mac_dci11_generation() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(401, 11));
    auto grants = mac.scheduleDl(20);
    assert(grants.size() == 1);

    const auto dci = mac.buildDci11(grants);
    assert(dci.size() == 1);
    assert(dci[0].crnti == 401);
    assert(dci[0].rbLen == grants[0].prbs);
    assert(dci[0].mcs == grants[0].mcs);
    assert(dci[0].tbsBytes > 0);
    assert(dci[0].bwpSizePrb > 0);
    assert(dci[0].rbStart >= dci[0].bwpStartPrb);
    assert(dci[0].rbStart + dci[0].rbLen <= dci[0].bwpStartPrb + dci[0].bwpSizePrb);
    assert(dci[0].fdaType1Riv > 0);
    std::puts("  test_nr_mac_dci11_generation PASSED");
}

static void test_nr_mac_queue_aware_scheduling() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(501, 11));
    assert(mac.addUE(502, 11));

    // Only UE 501 has pending traffic, so it should be the only one scheduled.
    assert(mac.enqueueDlBytes(501, 200));
    auto grants = mac.scheduleDl(20);
    assert(grants.size() == 1);
    assert(grants[0].crnti == 501);
    assert(mac.pendingDlBytes(501) < 200);
    assert(mac.pendingDlBytes(502) == 0);
    std::puts("  test_nr_mac_queue_aware_scheduling PASSED");
}

static void test_nr_mac_bwp_capacity_caps() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(601, 6));   // BWP0 expected
    assert(mac.addUE(602, 13));  // BWP1 expected

    assert(mac.enqueueDlBytes(601, 4000));
    assert(mac.enqueueDlBytes(602, 4000));
    auto grants = mac.scheduleDl(80);
    assert(grants.size() == 2);

    for (const auto& g : grants) {
        if (g.crnti == 601) {
            assert(g.bwpId == 0);
            assert(g.prbs <= 10);
        }
        if (g.crnti == 602) {
            assert(g.bwpId == 1);
            assert(g.prbs <= 30);
        }
    }
    std::puts("  test_nr_mac_bwp_capacity_caps PASSED");
}

static void test_nr_mac_harq_retx_progression() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(701, 11));
    assert(mac.enqueueDlBytes(701, 4000));

    auto g1 = mac.scheduleDl(10);
    assert(g1.size() == 1);
    assert(g1[0].dci.ndi == true);
    assert(g1[0].dci.rv == 0);

    // While waiting ACK/NACK there should be no immediate grant.
    auto waitGrants = mac.scheduleDl(10);
    assert(waitGrants.empty());

    // Explicit NACK enables retransmission.
    assert(mac.reportHarqFeedback(701, g1[0].dci.harqId, false));

    auto g2 = mac.scheduleDl(10);
    assert(g2.size() == 1);
    assert(g2[0].dci.ndi == false);
    assert(g2[0].dci.rv == 1);

    // ACK should close HARQ process and advance HARQ ID.
    assert(mac.reportHarqFeedback(701, g2[0].dci.harqId, true));
    auto g3 = mac.scheduleDl(10);
    assert(g3.size() == 1);
    assert(g3[0].dci.ndi == true);
    assert(g3[0].dci.harqId != g2[0].dci.harqId);
    std::puts("  test_nr_mac_harq_retx_progression PASSED");
}

static void test_nr_mac_fairness_equal_ues() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(801, 10));
    assert(mac.addUE(802, 10));

    assert(mac.enqueueDlBytes(801, 2000));
    assert(mac.enqueueDlBytes(802, 2000));

    int grants801 = 0;
    int grants802 = 0;
    for (int i = 0; i < 12; ++i) {
        auto grants = mac.scheduleDl(10);
        if (grants.empty()) {
            continue;
        }
        for (const auto& g : grants) {
            if (g.crnti == 801) ++grants801;
            if (g.crnti == 802) ++grants802;
            assert(mac.reportHarqFeedback(g.crnti, g.dci.harqId, true));
        }
    }

    assert(grants801 > 0);
    assert(grants802 > 0);
    const int diff = grants801 > grants802 ? (grants801 - grants802) : (grants802 - grants801);
    assert(diff <= 2);
    std::puts("  test_nr_mac_fairness_equal_ues PASSED");
}

// HARQ_MAX_RETX = 3: three consecutive NACKs must discard the TB, increment
// harqFailures, reset the process, and re-enable new-data transmission.
static void test_nr_mac_harq_max_retx() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(1101, 11));
    assert(mac.enqueueDlBytes(1101, 4000));

    // Initial new transmission.
    auto g1 = mac.scheduleDl(10);
    assert(g1.size() == 1 && g1[0].dci.ndi == true && g1[0].dci.rv == 0);
    const uint8_t hid = g1[0].dci.harqId;

    // NACK 1 → retransmit #1
    assert(mac.reportHarqFeedback(1101, hid, false));
    auto g2 = mac.scheduleDl(10);
    assert(g2.size() == 1 && g2[0].dci.ndi == false && g2[0].dci.rv == 1);

    // NACK 2 → retransmit #2
    assert(mac.reportHarqFeedback(1101, hid, false));
    auto g3 = mac.scheduleDl(10);
    assert(g3.size() == 1 && g3[0].dci.ndi == false && g3[0].dci.rv == 2);

    // NACK 3 → max-retx exceeded: process discarded, failure logged.
    assert(mac.reportHarqFeedback(1101, hid, false));
    const HarqStats stats = mac.getHarqStats(1101);
    assert(stats.totalRetx == 3);
    assert(stats.failures == 1);

    // Next grant must start a fresh new-data transmission.
    auto g4 = mac.scheduleDl(10);
    assert(g4.size() == 1 && g4[0].dci.ndi == true);
    assert(g4[0].dci.harqId != hid);  // process advanced
    std::puts("  test_nr_mac_harq_max_retx PASSED");
}

// RI=2 (2-layer MIMO) must double the TBS relative to RI=1 for the same PRB/MCS.
static void test_nr_mac_csi_ri_tbs_scaling() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(1201, 10));
    assert(mac.enqueueDlBytes(1201, 8000));

    // Baseline: RI=1 (default).
    auto g1 = mac.scheduleDl(10);
    assert(g1.size() == 1);
    const uint16_t tbs1 = g1[0].dci.tbsBytes;
    assert(tbs1 > 0);
    assert(mac.reportHarqFeedback(1201, g1[0].dci.harqId, true));

    // Upgrade to RI=2.
    assert(mac.reportCsiRi(1201, 2));
    auto g2 = mac.scheduleDl(10);
    assert(g2.size() == 1);
    assert(g2[0].prbs == g1[0].prbs);       // same PRB allocation
    assert(g2[0].mcs  == g1[0].mcs);        // same MCS (CQI unchanged)
    assert(g2[0].dci.tbsBytes == tbs1 * 2); // double throughput
    std::puts("  test_nr_mac_csi_ri_tbs_scaling PASSED");
}

// reportCsi() must atomically update CQI and RI, triggering BWP switch on next
// scheduleDl and preserving the new RI in TBS.
static void test_nr_mac_csi_combined_report() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(1301, 5));  // low CQI → BWP0
    assert(mac.enqueueDlBytes(1301, 8000));

    auto g0 = mac.scheduleDl(10);
    assert(!g0.empty() && g0[0].bwpId == 0);
    assert(mac.reportHarqFeedback(1301, g0[0].dci.harqId, true));

    // Combined CSI: high CQI triggers BWP1, RI=2 doubles TBS.
    assert(mac.reportCsi(1301, CsiReport{12, 2}));

    auto g1 = mac.scheduleDl(10);
    assert(!g1.empty());
    assert(g1[0].bwpId == 1);                       // switched to BWP1
    assert(g1[0].dci.tbsBytes == g1[0].prbs * (g1[0].mcs + 1) * 2);  // RI=2 in TBS
    std::puts("  test_nr_mac_csi_combined_report PASSED");
}

int main() {
    std::puts("=== test_nr_mac ===");
    test_nr_mac_schedule_prioritizes_cqi();
    test_nr_mac_qfi_to_drb_mapping();
    test_nr_sdap_header_roundtrip();
    test_nr_pdcp_sn18_increment();
    test_nr_slots_per_ms_from_scs();
    test_nr_mac_bwp_switching();
    test_nr_mac_dci11_generation();
    test_nr_mac_queue_aware_scheduling();
    test_nr_mac_bwp_capacity_caps();
    test_nr_mac_harq_retx_progression();
    test_nr_mac_fairness_equal_ues();
    test_nr_mac_harq_max_retx();
    test_nr_mac_csi_ri_tbs_scaling();
    test_nr_mac_csi_combined_report();
    std::puts("test_nr_mac PASSED");
    return 0;
}

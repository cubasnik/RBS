#include "../src/nr/nr_mac.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::nr;

static uint16_t sumSlicePrbs(const std::vector<NRScheduleGrant>& grants, const NRMac& mac, NRSlice slice) {
    uint16_t sum = 0;
    for (const auto& g : grants) {
        if (mac.ueSlice(g.crnti) == slice) {
            sum = static_cast<uint16_t>(sum + g.prbs);
        }
    }
    return sum;
}

static void test_slice_quota_enforcement() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(1001, 10));
    assert(mac.addUE(1002, 10));
    assert(mac.addUE(1003, 10));

    assert(mac.setUeSlice(1001, NRSlice::EMBB));
    assert(mac.setUeSlice(1002, NRSlice::URLLC));
    assert(mac.setUeSlice(1003, NRSlice::MMTC));

    assert(mac.enqueueDlBytes(1001, 4000));
    assert(mac.enqueueDlBytes(1002, 4000));
    assert(mac.enqueueDlBytes(1003, 4000));

    const auto grants = mac.scheduleDl(30);
    const uint16_t embbPrb = sumSlicePrbs(grants, mac, NRSlice::EMBB);
    const uint16_t urllcPrb = sumSlicePrbs(grants, mac, NRSlice::URLLC);
    const uint16_t mmtcPrb = sumSlicePrbs(grants, mac, NRSlice::MMTC);

    assert(embbPrb <= 15);
    assert(urllcPrb <= 9);
    assert(mmtcPrb <= 6);

    const auto metrics = mac.currentSliceMetrics();
    assert(metrics.size() == 3);
    assert(metrics[0].usedPrbs == embbPrb);
    assert(metrics[1].usedPrbs == urllcPrb);
    assert(metrics[2].usedPrbs == mmtcPrb);
    std::puts("  test_slice_quota_enforcement PASSED");
}

static void test_slice_metrics_active_ues() {
    NRMac mac(NRScs::SCS30);
    assert(mac.addUE(2001, 11));
    assert(mac.addUE(2002, 11));
    assert(mac.setUeSlice(2001, NRSlice::URLLC));
    assert(mac.setUeSlice(2002, NRSlice::URLLC));
    assert(mac.enqueueDlBytes(2001, 1200));
    assert(mac.enqueueDlBytes(2002, 1200));

    (void)mac.scheduleDl(20);
    const auto metrics = mac.currentSliceMetrics();
    assert(metrics.size() == 3);
    assert(metrics[1].activeUes == 2);
    assert(metrics[1].maxPrbs > 0);
    assert(metrics[1].usedPrbs <= metrics[1].maxPrbs);
    std::puts("  test_slice_metrics_active_ues PASSED");
}

int main() {
    std::puts("=== test_slicing ===");
    test_slice_quota_enforcement();
    test_slice_metrics_active_ues();
    std::puts("test_slicing PASSED");
    return 0;
}

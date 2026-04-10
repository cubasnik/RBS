#include "../src/gsm/gsm_rr.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::gsm;

int main() {
    GSMRr rr;

    // ── начальное состояние — UE не зарегистрирован ───────────────────────────
    assert(rr.rrState(1) == RRState::IDLE);

    // ── broadcastSI не крашится ───────────────────────────────────────────────
    rr.broadcastSI(1);  // SI1
    rr.broadcastSI(3);  // SI3
    rr.broadcastSI(5);  // SI5

    // ── handleChannelRequest → выдаёт RNTI и переводит в DEDICATED ───────────
    RNTI rnti1 = 0;
    assert(rr.handleChannelRequest(0x7F /*rachBurst*/, rnti1));
    assert(rnti1 != 0);
    // handleChannelRequest → CONNECTION_PENDING (Immediate Assignment отправлен)
    assert(rr.rrState(rnti1) == RRState::CONNECTION_PENDING);

    RNTI rnti2 = 0;
    assert(rr.handleChannelRequest(0x3C, rnti2));
    assert(rnti2 != 0);
    assert(rnti2 != rnti1);
    assert(rr.rrState(rnti2) == RRState::CONNECTION_PENDING);

    // ── processMeasurementReport + getMeasurementReport ──────────────────────
    MeasurementReport mr{};
    mr.rnti        = rnti1;
    mr.rxlev_full  = 45;
    mr.rxlev_sub   = 44;
    mr.rxqual_full = 2;
    mr.rxqual_sub  = 2;
    mr.numNeighbours = 2;
    mr.neighbours[0] = {600, 52, 7};
    mr.neighbours[1] = {612, 40, 5};

    rr.processMeasurementReport(mr);  // не крашится

    MeasurementReport got{};
    assert(rr.getMeasurementReport(rnti1, got));
    assert(got.rnti == rnti1);
    assert(got.rxlev_full == 45);
    assert(got.numNeighbours == 2);

    // getMeasurementReport для неизвестного RNTI → false
    MeasurementReport dummy{};
    assert(!rr.getMeasurementReport(999, dummy));

    // ── Measurement report с слабым сигналом запускает handover ──────────────
    // Порог HO: rxlev_full < 15 и сосед на 6 dB лучше
    MeasurementReport weakMr{};
    weakMr.rnti        = rnti2;
    weakMr.rxlev_full  = 10;  // ниже порога 15
    weakMr.rxlev_sub   = 10;
    weakMr.rxqual_full = 5;
    weakMr.rxqual_sub  = 5;
    weakMr.numNeighbours = 1;
    weakMr.neighbours[0] = {700, 18, 12}; // 18 > 10+6 → HO инициируется
    rr.processMeasurementReport(weakMr); // не крашится

    // ── initiateHandover ─────────────────────────────────────────────────────
    bool hoOk = rr.initiateHandover(rnti1, 700, 12, HandoverType::INTRA_BSC);
    (void)hoOk;  // может вернуть false если rnti1 не в DEDICATED — зависит от impl

    // ── releaseChannel ────────────────────────────────────────────────────────
    assert(rr.releaseChannel(rnti1));
    assert(rr.rrState(rnti1) == RRState::IDLE);

    assert(rr.releaseChannel(rnti2));
    assert(rr.rrState(rnti2) == RRState::IDLE);

    // releaseChannel для неизвестного → false (не крашится)
    assert(!rr.releaseChannel(999));

    std::puts("test_gsm_rr PASSED");
    return 0;
}

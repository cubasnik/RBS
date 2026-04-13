// test_perf_scheduler.cpp — LTE MAC scheduler latency benchmark (п.32)
//
// Measures per-tick latency of runDlScheduler() / runUlScheduler() under
// realistic load: 50 UEs, mixed CQI, DL SDUs queued every tick.
//
// KPI definitions (aligned with Criterion of Readiness п.32):
//   mean_us   — mean tick latency            (BUDGET: < 5 000 µs)
//   p95_us    — 95th-percentile tick latency  (BUDGET: < 15 000 µs)
//   max_us    — worst-case tick latency
//   fps       — scheduler throughput in ticks/s  (BUDGET: > 200 /s)
//
// Results are published to OMS (gauges + histogram) so perf_regression_check.py
// can compare them against a saved baseline.
#include "../src/lte/lte_mac.h"
#include "../src/hal/rf_hardware.h"
#include "../src/oms/oms.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>

using Clock = std::chrono::high_resolution_clock;
using Us    = std::chrono::duration<double, std::micro>;

// ── KPI budgets ───────────────────────────────────────────────────────────────
static constexpr double BUDGET_MEAN_US  =  5'000.0;   // 5 ms
static constexpr double BUDGET_P95_US   = 15'000.0;   // 15 ms
static constexpr double BUDGET_MIN_FPS  =    200.0;   // ticks/s

// ── Histogram bucket upper-bounds (µs) ───────────────────────────────────────
static const std::vector<double> HIST_BOUNDS_US = {
    50, 100, 200, 500, 1'000, 2'000, 5'000, 10'000, 20'000
};

// ── Test helpers ──────────────────────────────────────────────────────────────
static int passed = 0, failed = 0;

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL [line %d]: %s\n", __LINE__, #cond); \
        ++failed; \
    } else { \
        ++passed; \
    } \
} while(0)

#define EXPECT_LT(a, b) EXPECT_TRUE((a) < (b))
#define EXPECT_GT(a, b) EXPECT_TRUE((a) > (b))

// ── Benchmark ─────────────────────────────────────────────────────────────────
static void benchScheduler(int numUEs, int numTicks) {
    rbs::LTECellConfig cfg{};
    cfg.cellId      = 1;
    cfg.earfcn      = 1800;
    cfg.band        = rbs::LTEBand::B3;
    cfg.bandwidth   = rbs::LTEBandwidth::BW20;
    cfg.duplexMode  = rbs::LTEDuplexMode::FDD;
    cfg.txPower     = {43.0};
    cfg.pci         = 100;
    cfg.tac         = 1;
    cfg.mcc         = 250;
    cfg.mnc         = 1;
    cfg.numAntennas = 2;

    auto rf  = std::make_shared<rbs::hal::RFHardware>(2, 4);
    assert(rf->initialise());
    auto phy = std::make_shared<rbs::lte::LTEPhy>(rf, cfg);
    assert(phy->start());

    rbs::lte::LTEMAC mac(phy, cfg);
    assert(mac.start());

    // Admit UEs with varying CQI (1-15 round-robin)
    for (int u = 0; u < numUEs; ++u) {
        uint8_t cqi = static_cast<uint8_t>((u % 15) + 1);
        assert(mac.admitUE(static_cast<rbs::RNTI>(u + 1), cqi));
    }

    // Pre-queue DL SDUs for all UEs so scheduler always has work
    for (int u = 0; u < numUEs; ++u) {
        for (int q = 0; q < 4; ++q) {
            rbs::ByteBuffer sdu(200, static_cast<uint8_t>(u & 0xFF));
            mac.enqueueDlSDU(static_cast<rbs::RNTI>(u + 1), sdu);
        }
    }
    // Enable UL scheduling requests
    for (int u = 0; u < numUEs; ++u) {
        mac.updateBSR(static_cast<rbs::RNTI>(u + 1), 10);
        mac.handleSchedulingRequest(static_cast<rbs::RNTI>(u + 1));
    }

    // ── Measurement loop ──────────────────────────────────────────────────────
    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(numTicks));

    const auto wallStart = Clock::now();

    for (int t = 0; t < numTicks; ++t) {
        // Re-queue SDU every tick to keep queues alive
        for (int u = 0; u < numUEs; u += 4) {
            rbs::ByteBuffer sdu(100, 0xAB);
            mac.enqueueDlSDU(static_cast<rbs::RNTI>(u + 1), sdu);
        }

        const auto t0 = Clock::now();
        mac.tick();
        const auto t1 = Clock::now();

        latencies.push_back(Us(t1 - t0).count());
    }

    const auto wallEnd = Clock::now();
    double wallSec = std::chrono::duration<double>(wallEnd - wallStart).count();

    mac.stop();
    phy->stop();
    rf->shutdown();

    // ── Compute KPIs ──────────────────────────────────────────────────────────
    std::sort(latencies.begin(), latencies.end());

    double sum  = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    double mean = sum / static_cast<double>(latencies.size());

    size_t p95idx = static_cast<size_t>(latencies.size() * 0.95);
    if (p95idx >= latencies.size()) p95idx = latencies.size() - 1;
    double p95  = latencies[p95idx];
    double p50  = latencies[latencies.size() / 2];
    double maxL = latencies.back();
    double fps  = static_cast<double>(numTicks) / wallSec;

    std::printf("\n┌─ Scheduler Latency Benchmark (%d UEs, %d ticks) ──────────\n", numUEs, numTicks);
    std::printf("│  mean     : %8.1f µs  (budget < %.0f µs)\n",  mean,  BUDGET_MEAN_US);
    std::printf("│  p50      : %8.1f µs\n",                       p50);
    std::printf("│  p95      : %8.1f µs  (budget < %.0f µs)\n",  p95,   BUDGET_P95_US);
    std::printf("│  max      : %8.1f µs\n",                       maxL);
    std::printf("│  wall     : %8.3f s\n",                        wallSec);
    std::printf("│  fps      : %8.1f ticks/s  (budget > %.0f)\n", fps,   BUDGET_MIN_FPS);
    std::printf("└────────────────────────────────────────────────────────────\n\n");

    // ── Assertions ────────────────────────────────────────────────────────────
    EXPECT_LT(mean, BUDGET_MEAN_US);
    EXPECT_LT(p95,  BUDGET_P95_US);
    EXPECT_GT(fps,  BUDGET_MIN_FPS);

    // ── Publish to OMS ────────────────────────────────────────────────────────
    auto& oms = rbs::oms::OMS::instance();
    oms.updateCounter("perf.scheduler.mean_us",         mean,     "us");
    oms.updateCounter("perf.scheduler.p50_us",          p50,      "us");
    oms.updateCounter("perf.scheduler.p95_us",          p95,      "us");
    oms.updateCounter("perf.scheduler.max_us",          maxL,     "us");
    oms.updateCounter("perf.scheduler.throughput_fps",  fps,      "fps");
    oms.updateCounter("perf.scheduler.ue_count",
                      static_cast<double>(numUEs),      "UEs");
    oms.updateCounter("perf.scheduler.tick_count",
                      static_cast<double>(numTicks),    "ticks");

    // Histogram for latency distribution
    for (double lat : latencies)
        oms.observeHistogram("perf.scheduler.tick_latency_us", lat, HIST_BOUNDS_US);

    // Set KPI thresholds so regressions auto-alarm
    rbs::oms::IOMS::KpiThreshold meanThr{};
    meanThr.threshold    = BUDGET_MEAN_US;
    meanThr.belowIsAlarm = false;
    meanThr.severity     = rbs::oms::AlarmSeverity::MAJOR;
    meanThr.description  = "Scheduler mean latency exceeded budget";
    oms.setKpiThreshold("perf.scheduler.mean_us", meanThr);

    rbs::oms::IOMS::KpiThreshold fpsThr{};
    fpsThr.threshold     = BUDGET_MIN_FPS;
    fpsThr.belowIsAlarm  = true;
    fpsThr.severity      = rbs::oms::AlarmSeverity::MAJOR;
    fpsThr.description   = "Scheduler throughput below minimum FPS";
    oms.setKpiThreshold("perf.scheduler.throughput_fps", fpsThr);
}

// ── CA benchmark ─────────────────────────────────────────────────────────────
static void benchSchedulerCA(int numUEs, int numTicks) {
    rbs::LTECellConfig cfg{};
    cfg.cellId      = 2;
    cfg.earfcn      = 3400;
    cfg.band        = rbs::LTEBand::B7;
    cfg.bandwidth   = rbs::LTEBandwidth::BW20;
    cfg.duplexMode  = rbs::LTEDuplexMode::FDD;
    cfg.txPower     = {43.0};
    cfg.pci         = 200;
    cfg.tac         = 1;
    cfg.mcc         = 250;
    cfg.mnc         = 1;
    cfg.numAntennas = 4;

    auto rf  = std::make_shared<rbs::hal::RFHardware>(4, 4);
    assert(rf->initialise());
    auto phy = std::make_shared<rbs::lte::LTEPhy>(rf, cfg);
    assert(phy->start());

    rbs::lte::LTEMAC mac(phy, cfg);
    assert(mac.start());

    for (int u = 0; u < numUEs; ++u) {
        uint8_t cqi = static_cast<uint8_t>((u % 15) + 1);
        assert(mac.admitUE(static_cast<rbs::RNTI>(u + 1), cqi));
        // Half the UEs get 2-CC CA
        if (u % 2 == 0)
            mac.configureCA(static_cast<rbs::RNTI>(u + 1), 2);
    }

    for (int u = 0; u < numUEs; ++u) {
        rbs::ByteBuffer sdu(200, 0xBB);
        mac.enqueueDlSDU(static_cast<rbs::RNTI>(u + 1), sdu);
    }

    std::vector<double> latencies;
    latencies.reserve(static_cast<size_t>(numTicks));

    const auto wallStart = Clock::now();
    for (int t = 0; t < numTicks; ++t) {
        if (t % 5 == 0) {
            for (int u = 0; u < numUEs; ++u) {
                rbs::ByteBuffer sdu(100, 0xCC);
                mac.enqueueDlSDU(static_cast<rbs::RNTI>(u + 1), sdu);
            }
        }
        const auto t0 = Clock::now();
        mac.tick();
        latencies.push_back(Us(Clock::now() - t0).count());
    }
    const double wallSec =
        std::chrono::duration<double>(Clock::now() - wallStart).count();

    mac.stop();
    phy->stop();
    rf->shutdown();

    std::sort(latencies.begin(), latencies.end());
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0)
                  / static_cast<double>(latencies.size());
    double p95  = latencies[static_cast<size_t>(latencies.size() * 0.95)];
    double fps  = static_cast<double>(numTicks) / wallSec;

    std::printf("┌─ CA Scheduler Benchmark (%d UEs, 50%% 2-CC, %d ticks) ────\n", numUEs, numTicks);
    std::printf("│  mean  : %8.1f µs\n", mean);
    std::printf("│  p95   : %8.1f µs\n", p95);
    std::printf("│  fps   : %8.1f\n",    fps);
    std::printf("└────────────────────────────────────────────────────────────\n\n");

    EXPECT_LT(mean, BUDGET_MEAN_US * 3.0);   // CA overhead: 3× budget
    EXPECT_GT(fps,  BUDGET_MIN_FPS / 2.0);   // CA overhead: half budget

    auto& oms = rbs::oms::OMS::instance();
    oms.updateCounter("perf.scheduler_ca.mean_us",        mean, "us");
    oms.updateCounter("perf.scheduler_ca.p95_us",         p95,  "us");
    oms.updateCounter("perf.scheduler_ca.throughput_fps", fps,  "fps");

    for (double lat : latencies)
        oms.observeHistogram("perf.scheduler_ca.tick_latency_us", lat, HIST_BOUNDS_US);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    benchScheduler(50, 2000);
    benchSchedulerCA(20, 500);

    // Dump a Prometheus snapshot to stdout for regression check
    const std::string prom = rbs::oms::OMS::instance().renderPrometheus();
    const bool hasHistogram = prom.find("histogram") != std::string::npos;
    const bool hasScheduler = prom.find("perf_scheduler") != std::string::npos;
    EXPECT_TRUE(hasHistogram);
    EXPECT_TRUE(hasScheduler);

    std::printf("passed=%d  failed=%d\n", passed, failed);
    if (failed > 0) {
        std::puts("test_perf_scheduler FAILED");
        return 1;
    }
    std::puts("test_perf_scheduler PASSED");
    return 0;
}

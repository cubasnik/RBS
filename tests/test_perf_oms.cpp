// test_perf_oms.cpp — OMS performance metrics: histogram API and per-interface
// error counter tests (п.32).
//
// Tests covered:
//   1. observeHistogram: bucket counting is correct
//   2. observeHistogram: sum/count accumulators
//   3. observeHistogram: second observation without re-initialising bounds
//   4. renderPrometheus: emits "# TYPE ... histogram" line
//   5. renderPrometheus: emits _bucket{le=...}, _sum, _count for each histogram
//   6. renderPrometheus: +Inf bucket equals total count
//   7. Per-interface error counters: naming convention + render
//   8. KPI threshold auto-alarm on error counter spike
//   9. getHistogramNames returns registered histogram
#include "../src/oms/oms.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <algorithm>

using namespace rbs::oms;

static int passed = 0, failed = 0;

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL [line %d]: %s\n", __LINE__, #cond); \
        ++failed; \
    } else { \
        ++passed; \
    } \
} while(0)

#define EXPECT_EQ(a, b)  EXPECT_TRUE((a) == (b))
#define EXPECT_GE(a, b)  EXPECT_TRUE((a) >= (b))

static OMS& oms() { return OMS::instance(); }

// ── helpers ───────────────────────────────────────────────────────────────────
static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

// Use a unique histogram name per test to avoid cross-test state pollution
static std::string histName(const char* t) {
    return std::string("test.hist.") + t;
}

// ── Test 1: bucket count correctness ─────────────────────────────────────────
static void testHistBucketCounting() {
    const std::string name = histName("t1");
    const std::vector<double> bounds = {100.0, 500.0, 1000.0};
    // Observe 3 values: 50, 300, 800
    oms().observeHistogram(name, 50.0,  bounds);
    oms().observeHistogram(name, 300.0, bounds);
    oms().observeHistogram(name, 800.0, bounds);

    const std::string prom = oms().renderPrometheus();
    // le=100 should have 1 observation (50 µs)
    EXPECT_TRUE(contains(prom, "le=\"100\"} 1"));
    // le=500 should have 2 observations (50, 300)
    EXPECT_TRUE(contains(prom, "le=\"500\"} 2"));
    // le=1000 should have 3 observations (50, 300, 800)
    EXPECT_TRUE(contains(prom, "le=\"1000\"} 3"));
}

// ── Test 2: sum and count accumulators ───────────────────────────────────────
static void testHistSumCount() {
    const std::string name = histName("t2");
    const std::vector<double> bounds = {200.0, 1000.0};
    oms().observeHistogram(name, 100.0, bounds);
    oms().observeHistogram(name, 400.0, bounds);

    const std::string prom = oms().renderPrometheus();
    // _count should be 2 (value of +Inf bucket)
    EXPECT_TRUE(contains(prom, "_bucket{le=\"+Inf\"} 2"));
    // _count line
    EXPECT_TRUE(contains(prom, "_count 2"));
    // _sum = 500.0
    EXPECT_TRUE(contains(prom, "_sum 500"));
}

// ── Test 3: subsequent observations re-use bounds ────────────────────────────
static void testHistMultipleObservations() {
    const std::string name = histName("t3");
    const std::vector<double> bounds = {50.0, 500.0};
    for (int i = 0; i < 10; ++i)
        oms().observeHistogram(name, static_cast<double>(i * 30), bounds);

    const std::string prom = oms().renderPrometheus();
    // 10 total observations → +Inf bucket = 10
    EXPECT_TRUE(contains(prom, "_bucket{le=\"+Inf\"} 10"));
    EXPECT_TRUE(contains(prom, "_count 10"));
}

// ── Test 4: renderPrometheus emits histogram TYPE line ───────────────────────
static void testHistTypeInRender() {
    const std::string name = histName("t4");
    oms().observeHistogram(name, 99.0, {100.0, 200.0});
    const std::string prom = oms().renderPrometheus();
    EXPECT_TRUE(contains(prom, "histogram"));
    // Metric name is prefixed with rbs_ and dots converted to _
    EXPECT_TRUE(contains(prom, "rbs_test_hist_t4"));
}

// ── Test 5: renderPrometheus emits _sum and _count lines ─────────────────────
static void testHistSumCountLines() {
    const std::string name = histName("t5");
    oms().observeHistogram(name, 123.0, {500.0});
    const std::string prom = oms().renderPrometheus();
    EXPECT_TRUE(contains(prom, "rbs_test_hist_t5_sum"));
    EXPECT_TRUE(contains(prom, "rbs_test_hist_t5_count"));
}

// ── Test 6: +Inf bucket equals total count ───────────────────────────────────
static void testHistInfBucketEqualsCount() {
    const std::string name = histName("t6");
    const std::vector<double> bounds = {10.0, 100.0};
    // Observe a value larger than all bounds → only +Inf bucket counts it
    oms().observeHistogram(name, 999.0, bounds);
    const std::string prom = oms().renderPrometheus();
    // le=10 and le=100 should be 0; +Inf = 1
    EXPECT_TRUE(contains(prom, "le=\"10\"} 0"));
    EXPECT_TRUE(contains(prom, "le=\"100\"} 0"));
    EXPECT_TRUE(contains(prom, "le=\"+Inf\"} 1"));
    EXPECT_TRUE(contains(prom, "_count 1"));
}

// ── Test 7: per-interface error counters naming and render ────────────────────
//
// Convention (п.32): all per-interface error gauges follow
//   rbs.<rat>.<interface>.tx_errors
//   rbs.<rat>.<interface>.rx_errors
static void testPerInterfaceErrorCounters() {
    // Simulate a few TX/RX errors on each interface
    struct IfEntry { const char* name; double txErr; double rxErr; };
    static const IfEntry interfaces[] = {
        { "rbs.lte.s1.tx_errors",    2.0, 0.0 },
        { "rbs.lte.s1.rx_errors",    0.0, 1.0 },
        { "rbs.gsm.abis.tx_errors",  1.0, 0.0 },
        { "rbs.gsm.abis.rx_errors",  0.0, 3.0 },
        { "rbs.umts.iub.tx_errors",  0.0, 0.0 },
        { "rbs.umts.iub.rx_errors",  0.0, 0.0 },
        { "rbs.nr.ng.tx_errors",     1.0, 0.0 },
        { "rbs.nr.ng.rx_errors",     0.0, 2.0 },
        { "rbs.nr.xn.tx_errors",     0.0, 0.0 },
        { "rbs.nr.xn.rx_errors",     0.0, 1.0 },
    };

    for (const auto& e : interfaces) {
        // Use the single name directly as the counter key
        const double val = (e.txErr > 0.0) ? e.txErr : e.rxErr;
        oms().updateCounter(e.name, val, "errors");
    }

    const std::string prom = oms().renderPrometheus();
    EXPECT_TRUE(contains(prom, "rbs_lte_s1_tx_errors"));
    EXPECT_TRUE(contains(prom, "rbs_gsm_abis_rx_errors"));
    EXPECT_TRUE(contains(prom, "rbs_nr_ng_tx_errors"));
    EXPECT_TRUE(contains(prom, "rbs_nr_xn_rx_errors"));
    EXPECT_TRUE(contains(prom, "rbs_umts_iub_tx_errors"));

    // Verify counter values
    EXPECT_EQ(oms().getCounter("rbs.lte.s1.tx_errors"),    2.0);
    EXPECT_EQ(oms().getCounter("rbs.gsm.abis.rx_errors"),  3.0);
    EXPECT_EQ(oms().getCounter("rbs.nr.xn.rx_errors"),     1.0);
}

// ── Test 8: KPI threshold auto-alarm on error counter spike ──────────────────
static void testErrorCounterKpiAlarm() {
    const std::string counter = "rbs.lte.s1.rx_errors";
    // Set threshold: alarm if rx_errors > 5
    IOMS::KpiThreshold thr{};
    thr.threshold    = 5.0;
    thr.belowIsAlarm = false;
    thr.severity     = AlarmSeverity::MAJOR;
    thr.description  = "S1 RX error rate exceeded threshold";
    oms().setKpiThreshold(counter, thr);

    // Below threshold: no alarm expected
    oms().updateCounter(counter, 3.0, "errors");
    {
        const auto alarms = oms().getActiveAlarms();
        bool found = false;
        for (const auto& a : alarms)
            if (a.source == counter) found = true;
        EXPECT_TRUE(!found);
    }

    // Above threshold: alarm should be raised
    oms().updateCounter(counter, 10.0, "errors");
    {
        const auto alarms = oms().getActiveAlarms();
        bool found = false;
        for (const auto& a : alarms)
            if (a.source == counter && a.active) found = true;
        EXPECT_TRUE(found);
    }

    // Back below threshold: alarm should clear
    oms().updateCounter(counter, 2.0, "errors");
    {
        const auto alarms = oms().getActiveAlarms();
        bool stillActive = false;
        for (const auto& a : alarms)
            if (a.source == counter && a.active) stillActive = true;
        EXPECT_TRUE(!stillActive);
    }

    oms().removeKpiThreshold(counter);
}

// ── Test 9: getHistogramNames ─────────────────────────────────────────────────
static void testGetHistogramNames() {
    const std::string name = histName("t9");
    oms().observeHistogram(name, 77.0, {100.0, 200.0});
    const auto names = oms().getHistogramNames();
    bool found = (std::find(names.begin(), names.end(), name) != names.end());
    EXPECT_TRUE(found);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main() {
    testHistBucketCounting();
    testHistSumCount();
    testHistMultipleObservations();
    testHistTypeInRender();
    testHistSumCountLines();
    testHistInfBucketEqualsCount();
    testPerInterfaceErrorCounters();
    testErrorCounterKpiAlarm();
    testGetHistogramNames();

    std::printf("passed=%d  failed=%d\n", passed, failed);
    if (failed > 0) {
        std::puts("test_perf_oms FAILED");
        return 1;
    }
    std::puts("test_perf_oms PASSED");
    return 0;
}

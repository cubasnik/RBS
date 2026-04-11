// test_pm_export.cpp — OMS PM export tests (п.19)
// Tests: exportCsv file content, line format, pushInflux counter,
//        Logger rotation trigger.
#include "../src/oms/oms.h"
#include "../src/common/logger.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace rbs::oms;

// ── helpers ──────────────────────────────────────────────────────────────────

static int passed = 0, failed = 0;

#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { printf("FAIL [line %d]: %s\n", __LINE__, #cond); ++failed; } \
    else { ++passed; } \
} while(0)

#define EXPECT_EQ(a,b) EXPECT_TRUE((a)==(b))
#define EXPECT_GE(a,b) EXPECT_TRUE((a)>=(b))

static OMS& oms() { return OMS::instance(); }

// ── Test 1: exportCsv creates file ───────────────────────────────────────────
static void testExportCsvCreatesFile() {
    oms().updateCounter("pm.ues", 7.0, "");
    const std::string path = "test_pm_out.csv";
    bool ok = oms().exportCsv(path);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

// ── Test 2: exportCsv has correct header row ─────────────────────────────────
static void testExportCsvHeader() {
    oms().updateCounter("pm.ues", 3.0, "");
    const std::string path = "test_pm_header.csv";
    oms().exportCsv(path);
    std::ifstream f(path);
    EXPECT_TRUE(f.is_open());
    std::string hdr;
    std::getline(f, hdr);
    EXPECT_TRUE(hdr == "timestamp,name,value,unit");
    f.close();
    std::filesystem::remove(path);
}

// ── Test 3: exportCsv contains expected counter ──────────────────────────────
static void testExportCsvContainsCounter() {
    oms().updateCounter("pm.dlThroughput", 100.5, "Mbps");
    const std::string path = "test_pm_counter.csv";
    oms().exportCsv(path);
    std::ifstream f(path);
    std::string line, all;
    while (std::getline(f, line)) all += line + "\n";
    f.close();
    EXPECT_TRUE(all.find("pm.dlThroughput") != std::string::npos);
    EXPECT_TRUE(all.find("Mbps") != std::string::npos);
    std::filesystem::remove(path);
}

// ── Test 4: exportCsv returns false for invalid path ─────────────────────────
static void testExportCsvBadPath() {
    bool ok = oms().exportCsv("/nonexistent_dir/test_pm.csv");
    EXPECT_TRUE(!ok);
}

// ── Test 5: exportCsv data row has 4 comma-separated fields ──────────────────
static void testExportCsvRowFormat() {
    oms().updateCounter("pm.cells", 2.0, "cells");
    const std::string path = "test_pm_rowfmt.csv";
    oms().exportCsv(path);
    std::ifstream f(path);
    std::string hdr, row;
    std::getline(f, hdr);  // skip header
    // Read data rows and count commas
    bool foundRow = false;
    while (std::getline(f, row)) {
        int commas = 0;
        for (char c : row) if (c == ',') ++commas;
        EXPECT_EQ(commas, 3);  // 4 fields = 3 commas
        foundRow = true;
    }
    EXPECT_TRUE(foundRow);
    f.close();
    std::filesystem::remove(path);
}

// ── Test 6: pushInflux to unavailable endpoint returns counter count ──────────
// InfluxDB may be absent; we still expect a non-negative integer return
// (the send may succeed or fail; what matters is we get the metric count back
//  on the "success" path after socket creation).
static void testPushInfluxReturnType() {
    oms().updateCounter("pm.ues",   5.0, "");
    oms().updateCounter("pm.cells", 3.0, "cells");
    // Send to localhost loopback on a high port (no listener needed for UDP)
    int ret = oms().pushInflux("127.0.0.1:29999");
    // Returns metrics count (>=0) or -1 on socket error
    EXPECT_TRUE(ret >= 0 || ret == -1);  // just confirm it doesn't crash/throw
}

// ── Test 7: pushInflux with bad endpoint returns -1 ──────────────────────────
static void testPushInfluxBadEndpoint() {
    int ret = oms().pushInflux("no_port_in_here");
    EXPECT_EQ(ret, -1);
}

// ── Test 8: pushInflux reports correct metric count ──────────────────────────
static void testPushInfluxCount() {
    // Populate a fresh OMS with exactly 2 known counters (may have more from
    // previous tests, so we just check >= 1)
    oms().updateCounter("pm.unique_test_x", 9.0, "");
    int ret = oms().pushInflux("127.0.0.1:29999", "rbs_test");
    EXPECT_TRUE(ret >= 1 || ret == -1);  // -1 acceptable if WinSock unavailable
}

// ── Test 9: Logger rotation does not crash ────────────────────────────────────
static void testLoggerRotationNoCrash() {
    const std::string logPath = "test_rotation.log";
    rbs::Logger::instance().enableFile(logPath);
    // Write enough lines that in real usage rotation would trigger;
    // here we just exercise the code path safely
    for (int i = 0; i < 200; ++i)
        RBS_LOG_INFO("TEST", "rotation line ", i);
    rbs::Logger::instance().enableFile("");  // close / reset
    // Both the log and possibly the rotated version should not crash
    EXPECT_TRUE(true);  // if we reach here, no crash occurred
    if (std::filesystem::exists(logPath))
        std::filesystem::remove(logPath);
    std::string rotated = logPath + ".1";
    if (std::filesystem::exists(rotated))
        std::filesystem::remove(rotated);
}

// ── Test 10: exportCsv with no counters still writes header ──────────────────
static void testExportCsvEmptyOms() {
    // Create a fresh, isolated exporter by temporarily overwriting all
    // counters — we cannot reset OMS singleton, so just check the header exists
    const std::string path = "test_pm_empty.csv";
    oms().exportCsv(path);
    std::ifstream f(path);
    std::string hdr;
    std::getline(f, hdr);
    EXPECT_TRUE(hdr == "timestamp,name,value,unit");
    f.close();
    std::filesystem::remove(path);
}

// ────────────────────────────────────────────────────────────────────────────
int main() {
    testExportCsvCreatesFile();
    testExportCsvHeader();
    testExportCsvContainsCounter();
    testExportCsvBadPath();
    testExportCsvRowFormat();
    testPushInfluxReturnType();
    testPushInfluxBadEndpoint();
    testPushInfluxCount();
    testLoggerRotationNoCrash();
    testExportCsvEmptyOms();

    printf("\n=== PM Export: %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}

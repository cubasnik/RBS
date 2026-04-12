// test_rest_api.cpp
// п.18 — Web Dashboard REST API tests
// 12 tests: server lifecycle, GET /status /pm /alarms, POST /admit, content-type
//
#include "../src/api/rest_server.h"
#include "../src/oms/oms.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

#include <httplib.h>

#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// ── minimal test harness ─────────────────────────────────────────────────────
static int gPass = 0, gFail = 0;
#define CHECK(expr) \
    do { if (expr) { ++gPass; } \
         else { std::cerr << "FAIL: " #expr << "  @ line " << __LINE__ << "\n"; ++gFail; } \
    } while(0)

// ── helpers ───────────────────────────────────────────────────────────────────
static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Port is shared across all tests that use the main server.
static int gPort = 0;

// Wait until the server is actually accepting connections (poll up to 2 s).
static void waitForServer(int port) {
    for (int i = 0; i < 200; ++i) {
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(0, 50000); // 50 ms
        auto r = cli.Get("/api/v1/status");
        if (r) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ── test functions ────────────────────────────────────────────────────────────

static void test_server_starts_and_stops() {
    rbs::api::RestServer srv(0);          // ephemeral port
    CHECK(srv.start());
    CHECK(srv.isRunning());
    CHECK(srv.port() > 0);
    waitForServer(srv.port());
    srv.stop();
    CHECK(!srv.isRunning());
}

static void test_status_returns_200() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/status");
    CHECK(res != nullptr);
    CHECK(res && res->status == 200);
}

static void test_status_json_fields() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/status");
    CHECK(res != nullptr);
    if (res) {
        CHECK(contains(res->body, "version"));
        CHECK(contains(res->body, "nodeState"));
        CHECK(contains(res->body, "1.0.0"));
        CHECK(contains(res->body, "UNLOCKED"));
    }
}

static void test_status_has_four_rats() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/status");
    CHECK(res != nullptr);
    if (res) {
        CHECK(contains(res->body, "GSM"));
        CHECK(contains(res->body, "UMTS"));
        CHECK(contains(res->body, "LTE"));
        CHECK(contains(res->body, "NR"));
    }
}

static void test_pm_returns_200() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/pm");
    CHECK(res != nullptr);
    CHECK(res && res->status == 200);
    CHECK(res && contains(res->body, "counters"));
}

static void test_pm_reflects_oms_counter() {
    rbs::oms::OMS::instance().updateCounter("rest.test.metric", 77.0, "");
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/pm");
    CHECK(res != nullptr);
    if (res) {
        CHECK(contains(res->body, "rest.test.metric"));
        CHECK(contains(res->body, "77"));
    }
}

static void test_slices_returns_200_and_body() {
    auto& oms = rbs::oms::OMS::instance();
    oms.updateCounter("slice.eMBB.max_prb", 12.0);
    oms.updateCounter("slice.eMBB.prb_used", 6.0);
    oms.updateCounter("slice.eMBB.connectedUEs", 2.0);

    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/slices");
    CHECK(res != nullptr);
    CHECK(res && res->status == 200);
    if (res) {
        CHECK(contains(res->body, "eMBB"));
        CHECK(contains(res->body, "URLLC"));
        CHECK(contains(res->body, "mMTC"));
        CHECK(contains(res->body, "maxPrb"));
        CHECK(contains(res->body, "prbUsed"));
    }
}

static void test_alarms_returns_200() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/alarms");
    CHECK(res != nullptr);
    CHECK(res && res->status == 200);
    CHECK(res && contains(res->body, "alarms"));
}

static void test_alarms_reflects_oms_alarm() {
    auto& oms = rbs::oms::OMS::instance();
    uint32_t id = oms.raiseAlarm("REST-TEST", "test alarm for api", rbs::oms::AlarmSeverity::MINOR);

    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/alarms");
    CHECK(res != nullptr);
    if (res) {
        CHECK(contains(res->body, "REST-TEST"));
        CHECK(contains(res->body, "MINOR"));
    }
    oms.clearAlarm(id);
}

static void test_admit_returns_crnti() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Post("/api/v1/admit",
                        R"({"imsi":123456789,"rat":"LTE"})",
                        "application/json");
    CHECK(res != nullptr);
    CHECK(res && res->status == 200);
    if (res) {
        CHECK(contains(res->body, "crnti"));
        CHECK(contains(res->body, "ok"));
    }
}

static void test_admit_missing_fields_returns_400() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Post("/api/v1/admit", "{}", "application/json");
    CHECK(res != nullptr);
    CHECK(res && res->status == 400);
}

static void test_admit_multiple_ues_different_crnti() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto r1 = cli.Post("/api/v1/admit", R"({"imsi":111,"rat":"GSM"})", "application/json");
    auto r2 = cli.Post("/api/v1/admit", R"({"imsi":222,"rat":"NR"})",  "application/json");
    CHECK(r1 != nullptr && r2 != nullptr);
    if (r1 && r2) {
        // Different CRNTIs → different response bodies
        CHECK(r1->body != r2->body);
    }
}

static void test_content_type_is_json() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/api/v1/status");
    CHECK(res != nullptr);
    if (res) {
        const std::string ct = res->get_header_value("Content-Type");
        CHECK(contains(ct, "application/json"));
    }
}

static void test_lte_start_call_returns_503_without_cells() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Post("/api/v1/lte/start_call",
                        R"({"imsi":123456789})",
                        "application/json");
    CHECK(res != nullptr);
    CHECK(res && res->status == 503);
}

static void test_lte_handover_validates_payload() {
    httplib::Client cli("127.0.0.1", gPort);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Post("/api/v1/lte/handover",
                        R"({"cellId":1,"rnti":101})",
                        "application/json");
    CHECK(res != nullptr);
    CHECK(res && res->status == 400);
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== test_rest_api ===\n";

    // Start the shared server on an ephemeral port.
    rbs::api::RestServer server(0);
    if (!server.start()) {
        std::cerr << "FAIL: Could not start REST server\n";
        return 1;
    }
    gPort = server.port();
    std::cout << "Server on port " << gPort << "\n";
    waitForServer(gPort); // ensure the accept loop is ready

    // Tests using the shared server.
    test_status_returns_200();
    test_status_json_fields();
    test_status_has_four_rats();
    test_pm_returns_200();
    test_pm_reflects_oms_counter();
    test_slices_returns_200_and_body();
    test_alarms_returns_200();
    test_alarms_reflects_oms_alarm();
    test_admit_returns_crnti();
    test_admit_missing_fields_returns_400();
    test_admit_multiple_ues_different_crnti();
    test_content_type_is_json();
    test_lte_start_call_returns_503_without_cells();
    test_lte_handover_validates_payload();

    server.stop();

    // Lifecycle test uses its own ephemeral-port server.
    test_server_starts_and_stops();

    std::cout << "Passed: " << gPass << "  Failed: " << gFail << "\n";
    return gFail ? 1 : 0;
}

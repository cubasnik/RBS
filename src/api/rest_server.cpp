#include "rest_server.h"
#include "../oms/oms.h"
#include "../common/logger.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef NOGDI
#    define NOGDI
#  endif
#endif

#include <httplib.h>

#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdlib>

namespace rbs::api {

// ── Simple JSON helpers ──────────────────────────────────────────────────────

static std::string jsonEscStr(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    out += '"';
    return out;
}

static const char* severityToStr(rbs::oms::AlarmSeverity s) {
    using S = rbs::oms::AlarmSeverity;
    switch (s) {
        case S::CLEARED:  return "CLEARED";
        case S::WARNING:  return "WARNING";
        case S::MINOR:    return "MINOR";
        case S::MAJOR:    return "MAJOR";
        case S::CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN";
}

static const char* nodeStateToStr(rbs::oms::IOMS::NodeState s) {
    using NS = rbs::oms::IOMS::NodeState;
    switch (s) {
        case NS::UNLOCKED:      return "UNLOCKED";
        case NS::LOCKED:        return "LOCKED";
        case NS::SHUTTING_DOWN: return "SHUTTING_DOWN";
    }
    return "UNKNOWN";
}

// Minimal JSON field extractors (not a full parser — handles well-formed input).
static std::string extractJsonStr(const std::string& json, const std::string& key) {
    auto it = json.find('"' + key + '"');
    if (it == std::string::npos) return "";
    it = json.find(':', it);
    if (it == std::string::npos) return "";
    it = json.find('"', it);
    if (it == std::string::npos) return "";
    ++it;
    auto end = json.find('"', it);
    if (end == std::string::npos) return "";
    return json.substr(it, end - it);
}

static long long extractJsonInt(const std::string& json, const std::string& key) {
    auto it = json.find('"' + key + '"');
    if (it == std::string::npos) return -1;
    it = json.find(':', it);
    if (it == std::string::npos) return -1;
    ++it;
    while (it < json.size() && (json[it] == ' ' || json[it] == '\t')) ++it;
    if (it >= json.size()) return -1;
    char* end = nullptr;
    long long val = std::strtoll(json.c_str() + it, &end, 10);
    return (end == json.c_str() + it) ? -1 : val;
}

// ── Internal UE admission stub ───────────────────────────────────────────────
struct UEEntry { uint64_t imsi; std::string rat; };

// ── Impl ─────────────────────────────────────────────────────────────────────
struct RestServer::Impl {
    httplib::Server   svr;
    std::thread       thread;
    int               requestedPort{0};
    int               actualPort{0};
    std::atomic<bool> running{false};

    std::mutex                           ueMutex;
    std::unordered_map<uint16_t, UEEntry> ues;
    uint16_t                             nextCrnti{101};

    void setupRoutes() {
        // ── GET /api/v1/status ─────────────────────────────────────────
        svr.Get("/api/v1/status", [](const httplib::Request&, httplib::Response& res) {
            auto& oms = rbs::oms::OMS::instance();
            std::ostringstream j;
            j << "{"
              << "\"version\":\"1.0.0\","
              << "\"nodeState\":" << jsonEscStr(nodeStateToStr(oms.getNodeState())) << ","
              << "\"rats\":[\"GSM\",\"UMTS\",\"LTE\",\"NR\"]"
              << "}";
            res.set_content(j.str(), "application/json");
        });

        // ── GET /api/v1/pm ─────────────────────────────────────────────
        svr.Get("/api/v1/pm", [](const httplib::Request&, httplib::Response& res) {
            auto counters = rbs::oms::OMS::instance().getAllCounters();
            std::ostringstream j;
            j << "{\"counters\":[";
            bool first = true;
            for (const auto& [name, val] : counters) {
                if (!first) j << ',';
                first = false;
                j << "{\"name\":" << jsonEscStr(name) << ",\"value\":" << val << "}";
            }
            j << "]}";
            res.set_content(j.str(), "application/json");
        });

        // ── GET /api/v1/alarms ─────────────────────────────────────────
        svr.Get("/api/v1/alarms", [](const httplib::Request&, httplib::Response& res) {
            auto alarms = rbs::oms::OMS::instance().getActiveAlarms();
            std::ostringstream j;
            j << "{\"alarms\":[";
            bool first = true;
            for (const auto& a : alarms) {
                if (!first) j << ',';
                first = false;
                j << "{"
                  << "\"id\":"          << a.alarmId                         << ","
                  << "\"source\":"      << jsonEscStr(a.source)              << ","
                  << "\"description\":" << jsonEscStr(a.description)         << ","
                  << "\"severity\":"    << jsonEscStr(severityToStr(a.severity))
                  << "}";
            }
            j << "]}";
            res.set_content(j.str(), "application/json");
        });

        // ── POST /api/v1/admit ─────────────────────────────────────────
        svr.Post("/api/v1/admit", [this](const httplib::Request& req, httplib::Response& res) {
            long long imsiRaw = extractJsonInt(req.body, "imsi");
            std::string rat   = extractJsonStr(req.body, "rat");
            if (imsiRaw <= 0 || rat.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"missing or invalid imsi/rat\"}", "application/json");
                return;
            }
            const auto imsi = static_cast<uint64_t>(imsiRaw);
            std::lock_guard<std::mutex> lock(ueMutex);
            uint16_t crnti = nextCrnti++;
            ues[crnti] = {imsi, rat};
            std::ostringstream j;
            j << "{\"status\":\"ok\",\"crnti\":" << crnti << "}";
            res.set_content(j.str(), "application/json");
            RBS_LOG_INFO("REST", "admit imsi=", imsi, " rat=", rat, " crnti=", crnti);
        });
    }
};

// ── RestServer ───────────────────────────────────────────────────────────────
RestServer::RestServer(int port)
    : impl_(std::make_unique<Impl>())
{
    impl_->requestedPort = port;
}

RestServer::~RestServer() {
    stop();
}

bool RestServer::start() {
    impl_->setupRoutes();

    if (impl_->requestedPort == 0) {
        impl_->actualPort = impl_->svr.bind_to_any_port("127.0.0.1");
        if (impl_->actualPort < 0) {
            RBS_LOG_ERROR("REST", "Failed to bind to any port");
            return false;
        }
    } else {
        impl_->actualPort = impl_->requestedPort;
        if (!impl_->svr.bind_to_port("127.0.0.1", impl_->actualPort)) {
            RBS_LOG_ERROR("REST", "Failed to bind to port ", impl_->actualPort);
            return false;
        }
    }

    impl_->running.store(true);
    impl_->thread = std::thread([this]() {
        impl_->svr.listen_after_bind();
    });

    // Brief wait for the accept loop to start.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RBS_LOG_INFO("REST", "HTTP server listening on port ", impl_->actualPort);
    return true;
}

void RestServer::stop() {
    if (!impl_->running.exchange(false)) return;
    impl_->svr.stop();
    if (impl_->thread.joinable()) impl_->thread.join();
    RBS_LOG_INFO("REST", "HTTP server stopped");
}

bool RestServer::isRunning() const { return impl_->running.load(); }
int  RestServer::port()      const { return impl_->actualPort; }

}  // namespace rbs::api

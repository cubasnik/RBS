#include "rest_server.h"
#include "../oms/oms.h"
#include "../common/logger.h"
#include "../common/link_registry.h"
#include "../common/config.h"

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

static const char* endcOptionToStr(rbs::ENDCOption o) {
    switch (o) {
        case rbs::ENDCOption::OPTION_3:  return "3";
        case rbs::ENDCOption::OPTION_3A: return "3a";
        case rbs::ENDCOption::OPTION_3X: return "3x";
    }
    return "3a";
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
    std::string       bindAddr{"127.0.0.1"};
    std::atomic<bool> running{false};

    std::mutex                           ueMutex;
    std::unordered_map<uint16_t, UEEntry> ues;
    uint16_t                             nextCrnti{101};

    void setupRoutes() {
        // ── GET /api/v1/status ─────────────────────────────────────────
        svr.Get("/api/v1/status", [](const httplib::Request&, httplib::Response& res) {
            auto& oms = rbs::oms::OMS::instance();
                        const auto endc = rbs::Config::instance().buildENDCConfig();
            std::ostringstream j;
            j << "{"
              << "\"version\":\"1.0.0\","
              << "\"nodeState\":" << jsonEscStr(nodeStateToStr(oms.getNodeState())) << ","
                            << "\"rats\":[\"GSM\",\"UMTS\",\"LTE\",\"NR\"],"
                            << "\"endcEnabled\":" << (endc.enabled ? "true" : "false") << ","
                            << "\"endcOption\":" << jsonEscStr(endcOptionToStr(endc.option)) << ","
                            << "\"x2Peer\":" << jsonEscStr(endc.x2Addr + ":" + std::to_string(endc.x2Port)) << ","
                            << "\"enbBearerId\":" << static_cast<int>(endc.enbBearerId) << ","
                            << "\"scgDrbId\":" << static_cast<int>(endc.scgDrbId)
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

        // ── GET /api/v1/links ──────────────────────────────────────────
        svr.Get("/api/v1/links", [](const httplib::Request&, httplib::Response& res) {
            auto links = rbs::LinkRegistry::instance().allLinks();
            std::ostringstream j;
            j << "[";
            bool first = true;
            for (const auto* e : links) {
                if (!first) j << ',';
                first = false;
                j << "{"
                  << "\"name\":"      << jsonEscStr(e->name)    << ","
                  << "\"rat\":"       << jsonEscStr(e->rat)     << ","
                  << "\"peer\":"      << jsonEscStr(e->peerAddr + ":" + std::to_string(e->peerPort)) << ","
                  << "\"connected\":" << (e->isConnected ? (e->isConnected() ? "true" : "false") : "false") << ","
                  << "\"blocked\":[";
                if (e->ctrl) {
                    auto bt = e->ctrl->blockedTypes();
                    bool fb = true;
                    for (const auto& t : bt) {
                        if (!fb) j << ',';
                        fb = false;
                        j << jsonEscStr(t);
                    }
                }
                j << "]}";
            }
            j << "]";
            res.set_content(j.str(), "application/json");
            RBS_LOG_INFO("REST", "links list count=", links.size());
        });

        // ── GET /api/v1/links/{name}/trace ─────────────────────────────
        svr.Get(R"(/api/v1/links/([^/]+)/trace)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e || !e->ctrl) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "trace link=", name, " not found");
                return;
            }
            size_t limit = 50;
            if (req.has_param("limit")) {
                try { limit = static_cast<size_t>(std::stoul(req.get_param_value("limit"))); }
                catch (...) {}
            }
            auto msgs = e->ctrl->getTrace(limit);
            std::ostringstream j;
            j << "{\"messages\":[";
            bool first = true;
            for (const auto& m : msgs) {
                if (!first) j << ',';
                first = false;
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    m.timestamp.time_since_epoch()).count();
                j << "{"
                  << "\"tx\":"        << (m.tx ? "true" : "false") << ","
                  << "\"type\":"      << jsonEscStr(m.type)         << ","
                  << "\"summary\":"   << jsonEscStr(m.summary)      << ","
                  << "\"timestampMs\":" << ms
                  << "}";
            }
            j << "]}";
            res.set_content(j.str(), "application/json");
            RBS_LOG_INFO("REST", "trace link=", name, " limit=", limit, " count=", msgs.size());
        });

        // ── POST /api/v1/links/{name}/connect ──────────────────────────
        svr.Post(R"(/api/v1/links/([^/]+)/connect)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "connect link=", name, " not found");
                return;
            }
            if (e->reconnect) e->reconnect();
            res.set_content("{\"status\":\"ok\"}", "application/json");
            RBS_LOG_INFO("REST", "connect link=", name, " status=ok");
        });

        // ── POST /api/v1/links/{name}/disconnect ───────────────────────
        svr.Post(R"(/api/v1/links/([^/]+)/disconnect)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "disconnect link=", name, " not found");
                return;
            }
            if (e->disconnect) e->disconnect();
            res.set_content("{\"status\":\"ok\"}", "application/json");
            RBS_LOG_INFO("REST", "disconnect link=", name, " status=ok");
        });

        // ── POST /api/v1/links/{name}/block ────────────────────────────
        // Body: {"type":"OML:OPSTART"}
        svr.Post(R"(/api/v1/links/([^/]+)/block)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e || !e->ctrl) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "block link=", name, " not found");
                return;
            }
            std::string msgType = extractJsonStr(req.body, "type");
            if (msgType.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"missing type\"}", "application/json");
                RBS_LOG_WARNING("REST", "block link=", name, " missing type");
                return;
            }
            e->ctrl->blockMsg(msgType);
            res.set_content("{\"status\":\"ok\"}", "application/json");
            RBS_LOG_INFO("REST", "block link=", name, " type=", msgType, " status=ok");
        });

        // ── POST /api/v1/links/{name}/unblock ──────────────────────────
        svr.Post(R"(/api/v1/links/([^/]+)/unblock)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e || !e->ctrl) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "unblock link=", name, " not found");
                return;
            }
            std::string msgType = extractJsonStr(req.body, "type");
            if (msgType.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"missing type\"}", "application/json");
                RBS_LOG_WARNING("REST", "unblock link=", name, " missing type");
                return;
            }
            e->ctrl->unblockMsg(msgType);
            res.set_content("{\"status\":\"ok\"}", "application/json");
            RBS_LOG_INFO("REST", "unblock link=", name, " type=", msgType, " status=ok");
        });

        // ── GET /api/v1/links/{name}/inject ────────────────────────────
        svr.Get(R"(/api/v1/links/([^/]+)/inject)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "inject list link=", name, " not found");
                return;
            }
            std::ostringstream j;
            j << "{\"procedures\":[";
            size_t procCount = 0;
            if (e->injectableProcs) {
                auto procs = e->injectableProcs();
                procCount = procs.size();
                bool first = true;
                for (const auto& p : procs) {
                    if (!first) j << ',';
                    first = false;
                    j << jsonEscStr(p);
                }
            }
            j << "]}";
            res.set_content(j.str(), "application/json");
            RBS_LOG_INFO("REST", "inject list link=", name, " count=", procCount);
        });

        // ── POST /api/v1/links/{name}/inject ───────────────────────────
        // Body: {"procedure":"S1AP:S1_SETUP"}
        svr.Post(R"(/api/v1/links/([^/]+)/inject)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "inject link=", name, " not found");
                return;
            }
            std::string proc = extractJsonStr(req.body, "procedure");
            if (proc.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"missing procedure\"}", "application/json");
                RBS_LOG_WARNING("REST", "inject link=", name, " missing procedure");
                return;
            }
            bool ok = e->injectProcedure ? e->injectProcedure(proc) : false;
            if (ok) {
                res.set_content("{\"status\":\"ok\"}", "application/json");
                RBS_LOG_INFO("REST", "inject link=", name, " procedure=", proc, " status=ok");
            } else {
                res.status = 422;
                res.set_content("{\"error\":\"inject failed or unknown procedure\"}", "application/json");
                RBS_LOG_WARNING("REST", "inject link=", name, " procedure=", proc, " status=failed");
            }
        });
    }
};

// ── RestServer ───────────────────────────────────────────────────────────────
RestServer::RestServer(int port, std::string bindAddr)
    : impl_(std::make_unique<Impl>())
{
    impl_->requestedPort = port;
    impl_->bindAddr      = std::move(bindAddr);
}

RestServer::~RestServer() {
    stop();
}

bool RestServer::start() {
    impl_->setupRoutes();

    // Use empty string for 0.0.0.0 (httplib interprets it better)
    std::string bindHost = (impl_->bindAddr == "0.0.0.0") ? "" : impl_->bindAddr;

    if (impl_->requestedPort == 0) {
        impl_->actualPort = impl_->svr.bind_to_any_port(bindHost);
        if (impl_->actualPort < 0) {
            RBS_LOG_ERROR("REST", "Failed to bind to any port");
            return false;
        }
    } else {
        impl_->actualPort = impl_->requestedPort;
        if (!impl_->svr.bind_to_port(bindHost, impl_->actualPort)) {
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
    RBS_LOG_INFO("REST", "HTTP server listening on ", impl_->bindAddr, ":", impl_->actualPort);
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

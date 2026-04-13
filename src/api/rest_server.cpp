#include "rest_server.h"
#include "../oms/oms.h"
#include "../common/logger.h"
#include "../common/link_registry.h"
#include "../common/lte_service_registry.h"
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
#include <vector>
#include <functional>

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

static std::string buildSipInvite(const std::string& callId) {
    std::ostringstream os;
    os << "INVITE sip:peer@ims.local SIP/2.0\r\n"
       << "From: <sip:rbs@ims.local>\r\n"
       << "To: <sip:peer@ims.local>\r\n"
       << "Call-ID: " << callId << "\r\n"
       << "CSeq: 1 INVITE\r\n"
       << "Content-Length: 0\r\n\r\n";
    return os.str();
}

static std::string buildSipBye(const std::string& callId) {
    std::ostringstream os;
    os << "BYE sip:peer@ims.local SIP/2.0\r\n"
       << "From: <sip:rbs@ims.local>\r\n"
       << "To: <sip:peer@ims.local>\r\n"
       << "Call-ID: " << callId << "\r\n"
       << "CSeq: 1 BYE\r\n"
       << "Content-Length: 0\r\n\r\n";
    return os.str();
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

static bool extractJsonBool(const std::string& json, const std::string& key, bool defVal = false) {
    auto it = json.find('"' + key + '"');
    if (it == std::string::npos) return defVal;
    it = json.find(':', it);
    if (it == std::string::npos) return defVal;
    ++it;
    while (it < json.size() && (json[it] == ' ' || json[it] == '\t' || json[it] == '\n' || json[it] == '\r')) ++it;
    if (it >= json.size()) return defVal;
    if (json.compare(it, 4, "true") == 0) return true;
    if (json.compare(it, 5, "false") == 0) return false;
    return defVal;
}

static bool extractJsonU8Array(const std::string& json, const std::string& key, std::vector<uint8_t>& out) {
    out.clear();
    auto it = json.find('"' + key + '"');
    if (it == std::string::npos) return false;
    it = json.find(':', it);
    if (it == std::string::npos) return false;
    it = json.find('[', it);
    if (it == std::string::npos) return false;
    auto endArr = json.find(']', it);
    if (endArr == std::string::npos) return false;

    std::string body = json.substr(it + 1, endArr - (it + 1));
    std::stringstream ss(body);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t p = 0;
        while (p < token.size() && (token[p] == ' ' || token[p] == '\t' || token[p] == '\n' || token[p] == '\r')) ++p;
        if (p >= token.size()) continue;
        char* end = nullptr;
        long v = std::strtol(token.c_str() + p, &end, 0);
        if (end == token.c_str() + p) return false;
        if (v < 0 || v > 255) return false;
        out.push_back(static_cast<uint8_t>(v));
    }
    return true;
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

    std::mutex cfgCbMutex;
    std::string configPath{"rbs.conf"};
    std::function<void()> configApplyCb;

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

        // ── PATCH /api/v1/config ───────────────────────────────────────
        // Body example:
        //   {"reloadFromDisk":true,"path":"rbs.conf"}
        //   {"section":"logging","key":"level","value":"DEBUG"}
        svr.Patch("/api/v1/config", [this](const httplib::Request& req, httplib::Response& res) {
            const bool reloadFromDisk = extractJsonBool(req.body, "reloadFromDisk", false);
            std::string path = extractJsonStr(req.body, "path");
            if (path.empty()) {
                std::lock_guard<std::mutex> lk(cfgCbMutex);
                path = configPath;
            }

            const std::string section = extractJsonStr(req.body, "section");
            const std::string key = extractJsonStr(req.body, "key");
            const bool hasSection = !section.empty() || !key.empty() || req.body.find("\"value\"") != std::string::npos;
            if ((!section.empty() && key.empty()) || (section.empty() && !key.empty())) {
                res.status = 400;
                res.set_content("{\"error\":\"section and key must be provided together\"}", "application/json");
                return;
            }

            bool patched = false;
            bool reloaded = false;

            if (reloadFromDisk) {
                if (!rbs::Config::instance().loadFile(path)) {
                    res.status = 422;
                    res.set_content("{\"error\":\"config reload failed\"}", "application/json");
                    return;
                }
                reloaded = true;
            }

            if (!section.empty()) {
                const std::string value = extractJsonStr(req.body, "value");
                rbs::Config::instance().setString(section, key, value);
                patched = true;
            } else if (hasSection) {
                res.status = 400;
                res.set_content("{\"error\":\"missing section/key/value\"}", "application/json");
                return;
            }

            if (!reloaded && !patched) {
                res.status = 400;
                res.set_content("{\"error\":\"nothing to apply\"}", "application/json");
                return;
            }

            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lk(cfgCbMutex);
                cb = configApplyCb;
            }
            if (cb) cb();

            std::ostringstream j;
            j << "{"
              << "\"status\":\"ok\"," 
              << "\"reloaded\":" << (reloaded ? "true" : "false") << ","
              << "\"patched\":" << (patched ? "true" : "false") << ","
              << "\"path\":" << jsonEscStr(path)
              << "}";
            res.set_content(j.str(), "application/json");
            RBS_LOG_INFO("REST", "config patch reloaded=", reloaded, " patched=", patched, " path=", path);
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

        // ── GET /api/v1/slices ────────────────────────────────────────
        svr.Get("/api/v1/slices", [](const httplib::Request&, httplib::Response& res) {
            auto counters = rbs::oms::OMS::instance().getAllCounters();
            auto get = [&counters](const std::string& key, double fallback = 0.0) {
                for (const auto& kv : counters) {
                    if (kv.first == key) {
                        return kv.second;
                    }
                }
                return fallback;
            };

            std::ostringstream j;
            j << "{\"slices\":["
              << "{\"name\":\"eMBB\",\"maxPrb\":" << get("slice.eMBB.max_prb")
              << ",\"prbUsed\":" << get("slice.eMBB.prb_used")
              << ",\"connectedUEs\":" << get("slice.eMBB.connectedUEs") << "},"
              << "{\"name\":\"URLLC\",\"maxPrb\":" << get("slice.URLLC.max_prb")
              << ",\"prbUsed\":" << get("slice.URLLC.prb_used")
              << ",\"connectedUEs\":" << get("slice.URLLC.connectedUEs") << "},"
              << "{\"name\":\"mMTC\",\"maxPrb\":" << get("slice.mMTC.max_prb")
              << ",\"prbUsed\":" << get("slice.mMTC.prb_used")
              << ",\"connectedUEs\":" << get("slice.mMTC.connectedUEs") << "}"
              << "]}";
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

        // ── GET /api/v1/lte/cells ──────────────────────────────────────
        svr.Get("/api/v1/lte/cells", [](const httplib::Request&, httplib::Response& res) {
            auto cells = rbs::LteServiceRegistry::instance().allCells();
            std::ostringstream j;
            j << "{\"cells\":[";
            bool first = true;
            for (const auto& c : cells) {
                if (!first) j << ',';
                first = false;
                j << "{\"cellId\":" << c.cellId
                  << ",\"earfcn\":" << c.earfcn
                  << ",\"pci\":" << c.pci
                  << ",\"connectedUEs\":" << (c.connectedUeCount ? c.connectedUeCount() : 0)
                  << "}";
            }
            j << "]}";
            res.set_content(j.str(), "application/json");
        });

        // ── POST /api/v1/lte/start_call ───────────────────────────────
        svr.Post("/api/v1/lte/start_call", [](const httplib::Request& req, httplib::Response& res) {
            const long long cellIdIn = extractJsonInt(req.body, "cellId");
            long long rntiIn = extractJsonInt(req.body, "rnti");
            const long long imsiIn = extractJsonInt(req.body, "imsi");
            const long long packetsIn = extractJsonInt(req.body, "rtpPackets");
            const long long payloadIn = extractJsonInt(req.body, "payloadBytes");

            std::optional<rbs::LteCellService> cell;
            if (cellIdIn > 0) {
                cell = rbs::LteServiceRegistry::instance().getCell(static_cast<rbs::CellId>(cellIdIn));
            } else {
                auto all = rbs::LteServiceRegistry::instance().allCells();
                if (!all.empty()) cell = all.front();
            }

            if (!cell.has_value()) {
                res.status = 503;
                res.set_content("{\"error\":\"no LTE cell available\"}", "application/json");
                return;
            }

            rbs::RNTI rnti = 0;
            if (rntiIn > 0) rnti = static_cast<rbs::RNTI>(rntiIn);
            if (rnti == 0) {
                if (imsiIn <= 0 || !cell->admitUe) {
                    res.status = 400;
                    res.set_content("{\"error\":\"missing rnti or valid imsi\"}", "application/json");
                    return;
                }
                rnti = cell->admitUe(static_cast<rbs::IMSI>(imsiIn), 10);
                if (rnti == 0) {
                    res.status = 422;
                    res.set_content("{\"error\":\"UE admission failed\"}", "application/json");
                    return;
                }
            }

            if (!cell->setupVoLteBearer || !cell->handleSipMessage || !cell->sendVoLteRtpBurst) {
                res.status = 500;
                res.set_content("{\"error\":\"LTE VoLTE service unavailable\"}", "application/json");
                return;
            }

            if (!cell->setupVoLteBearer(rnti)) {
                res.status = 422;
                res.set_content("{\"error\":\"setupVoLTEBearer failed\"}", "application/json");
                return;
            }

            const std::string callId = "rest-call-" + std::to_string(static_cast<unsigned>(rnti));
            const std::string invite = buildSipInvite(callId);
            if (!cell->handleSipMessage(rnti, invite)) {
                res.status = 422;
                res.set_content("{\"error\":\"SIP INVITE failed\"}", "application/json");
                return;
            }

            const size_t packets = (packetsIn > 0) ? static_cast<size_t>(packetsIn) : 3;
            const size_t payload = (payloadIn > 0) ? static_cast<size_t>(payloadIn) : 160;
            const size_t sent = cell->sendVoLteRtpBurst(rnti, packets, payload);

            std::ostringstream j;
            j << "{\"status\":\"ok\",\"cellId\":" << cell->cellId
              << ",\"rnti\":" << rnti
              << ",\"rtpSent\":" << sent
              << "}";
            res.set_content(j.str(), "application/json");
        });

        // ── POST /api/v1/lte/end_call ─────────────────────────────────
        svr.Post("/api/v1/lte/end_call", [](const httplib::Request& req, httplib::Response& res) {
            const long long cellIdIn = extractJsonInt(req.body, "cellId");
            const long long rntiIn = extractJsonInt(req.body, "rnti");
            const bool releaseUe = extractJsonBool(req.body, "releaseUe", false);

            if (cellIdIn <= 0 || rntiIn <= 0) {
                res.status = 400;
                res.set_content("{\"error\":\"cellId and rnti are required\"}", "application/json");
                return;
            }

            auto cell = rbs::LteServiceRegistry::instance().getCell(static_cast<rbs::CellId>(cellIdIn));
            if (!cell.has_value() || !cell->handleSipMessage) {
                res.status = 404;
                res.set_content("{\"error\":\"LTE cell not found\"}", "application/json");
                return;
            }

            const rbs::RNTI rnti = static_cast<rbs::RNTI>(rntiIn);
            const std::string bye = buildSipBye("rest-call-" + std::to_string(static_cast<unsigned>(rnti)));
            if (!cell->handleSipMessage(rnti, bye)) {
                res.status = 422;
                res.set_content("{\"error\":\"SIP BYE failed\"}", "application/json");
                return;
            }

            if (releaseUe && cell->releaseUe) cell->releaseUe(rnti);
            res.set_content("{\"status\":\"ok\"}", "application/json");
        });

        // ── POST /api/v1/lte/handover ─────────────────────────────────
        svr.Post("/api/v1/lte/handover", [](const httplib::Request& req, httplib::Response& res) {
            const long long cellIdIn = extractJsonInt(req.body, "cellId");
            const long long rntiIn = extractJsonInt(req.body, "rnti");
            const long long targetPciIn = extractJsonInt(req.body, "targetPci");
            const long long targetEarfcnIn = extractJsonInt(req.body, "targetEarfcn");

            if (cellIdIn <= 0 || rntiIn <= 0 || targetPciIn < 0 || targetEarfcnIn < 0) {
                res.status = 400;
                res.set_content("{\"error\":\"cellId,rnti,targetPci,targetEarfcn are required\"}", "application/json");
                return;
            }

            auto cell = rbs::LteServiceRegistry::instance().getCell(static_cast<rbs::CellId>(cellIdIn));
            if (!cell.has_value() || !cell->requestHandover) {
                res.status = 404;
                res.set_content("{\"error\":\"LTE cell not found\"}", "application/json");
                return;
            }

            const bool ok = cell->requestHandover(static_cast<rbs::RNTI>(rntiIn),
                                                  static_cast<uint16_t>(targetPciIn),
                                                  static_cast<rbs::EARFCN>(targetEarfcnIn));
            if (!ok) {
                res.status = 422;
                res.set_content("{\"error\":\"handover rejected\"}", "application/json");
                return;
            }
            res.set_content("{\"status\":\"ok\"}", "application/json");
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
                j << "]";

                if (e->healthJson) {
                    const std::string h = e->healthJson();
                    if (!h.empty()) {
                        j << ",\"health\":{" << h << "}";
                    }
                }

                j << "}";
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

        // ── GET /api/v1/links/{name}/health ────────────────────────────
        svr.Get(R"(/api/v1/links/([^/]+)/health)", [](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            auto* e = rbs::LinkRegistry::instance().getLink(name);
            if (!e) {
                res.status = 404;
                res.set_content("{\"error\":\"link not found\"}", "application/json");
                RBS_LOG_WARNING("REST", "health link=", name, " not found");
                return;
            }

            std::ostringstream j;
            j << "{"
              << "\"name\":" << jsonEscStr(e->name) << ","
              << "\"connected\":" << (e->isConnected ? (e->isConnected() ? "true" : "false") : "false");

            if (e->healthJson) {
                const std::string h = e->healthJson();
                if (!h.empty()) {
                    j << ",\"health\":{" << h << "}";
                }
            }

            j << "}";
            res.set_content(j.str(), "application/json");
            RBS_LOG_INFO("REST", "health link=", name, " status=ok");
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

            std::string finalProc = proc;
            if (name == "abis") {
                const long long chanNr = extractJsonInt(req.body, "chanNr");
                const long long entity = extractJsonInt(req.body, "entity");
                std::vector<uint8_t> payload;
                const bool hasPayload = extractJsonU8Array(req.body, "payload", payload);

                if (chanNr >= 0 || entity >= 0 || hasPayload) {
                    std::ostringstream spec;
                    spec << proc;
                    if (chanNr >= 0) spec << ";chan=" << chanNr;
                    if (entity >= 0) spec << ";entity=" << entity;
                    if (hasPayload) {
                        spec << ";payload=";
                        for (size_t i = 0; i < payload.size(); ++i) {
                            if (i) spec << ',';
                            spec << static_cast<int>(payload[i]);
                        }
                    }
                    finalProc = spec.str();
                }
            }

            bool ok = e->injectProcedure ? e->injectProcedure(finalProc) : false;
            if (ok) {
                res.set_content("{\"status\":\"ok\"}", "application/json");
                RBS_LOG_INFO("REST", "inject link=", name, " procedure=", finalProc, " status=ok");
            } else {
                res.status = 422;
                res.set_content("{\"error\":\"inject failed or unknown procedure\"}", "application/json");
                RBS_LOG_WARNING("REST", "inject link=", name, " procedure=", finalProc, " status=failed");
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

void RestServer::setConfigPath(std::string path) {
    std::lock_guard<std::mutex> lk(impl_->cfgCbMutex);
    impl_->configPath = std::move(path);
}

void RestServer::setConfigApplyCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(impl_->cfgCbMutex);
    impl_->configApplyCb = std::move(cb);
}

}  // namespace rbs::api

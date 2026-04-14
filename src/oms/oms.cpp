#include "oms.h"
#include "../common/logger.h"
#include <httplib.h>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cstring>
#include <cctype>
#include <thread>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

namespace rbs::oms {

static std::string inferAlarmCode(const std::string& source, const std::string& description) {
    const std::string text = source + " " + description;
    if (text.find("handover") != std::string::npos || text.find("HO") != std::string::npos) {
        return "HO_FAILED";
    }
    if (text.find("RLF") != std::string::npos || text.find("radio link failure") != std::string::npos) {
        return "RLF_DETECTED";
    }
    if (text.find("admission") != std::string::npos || text.find("reject") != std::string::npos) {
        return "ADMISSION_REJECT";
    }
    if (text.find("drop") != std::string::npos) {
        return "UE_DROP";
    }
    return "GENERIC_ALARM";
}

struct OMS::PrometheusExporter {
    std::unique_ptr<httplib::Server> server;
    std::thread thread;
    bool running = false;
};

OMS::OMS() {
    initializeDefaultCorrelationRules();
}

OMS::~OMS() {
    stopPrometheus();
}

// ────────────────────────────────────────────────────────────────
// Fault Management
// ────────────────────────────────────────────────────────────────
uint32_t OMS::raiseAlarm(const std::string& source,
                          const std::string& description,
                          AlarmSeverity severity) {
    uint32_t id = nextAlarmId_++;
    Alarm alarm{};
    alarm.alarmId     = id;
    alarm.source      = source;
    alarm.description = description;
    alarm.severity    = severity;
    alarm.raisedAt    = std::chrono::system_clock::now();
    alarm.active      = true;

    AlarmEvent evt{};
    evt.timestamp = alarm.raisedAt;
    evt.source = source;
    evt.alarmCode = inferAlarmCode(source, description);
    evt.message = description;
    evt.severity = static_cast<int>(severity);

    const bool suppress = correlationEngine_.shouldSuppress(evt.alarmCode);
    const uint64_t gid = correlationEngine_.reportAlarm(evt);
    if (suppress) {
        alarm.active = false;
        alarm.severity = AlarmSeverity::CLEARED;
        RBS_LOG_INFO("OMS", "ALARM[", id, "] suppressed by correlation gid=", gid,
                     " code=", evt.alarmCode, " source=", source);
    }

    alarms_[id]       = alarm;

    const char* sevStr[] = {"CLEARED","WARNING","MINOR","MAJOR","CRITICAL"};
    RBS_LOG_WARNING("OMS", "ALARM[", id, "] ",
                    sevStr[static_cast<int>(severity)],
                    " from ", source, ": ", description);

    if (!suppress && notifyCb_) notifyCb_(alarm);
    return id;
}

void OMS::clearAlarm(uint32_t alarmId) {
    auto it = alarms_.find(alarmId);
    if (it == alarms_.end()) return;
    it->second.active   = false;
    it->second.severity = AlarmSeverity::CLEARED;
    RBS_LOG_INFO("OMS", "ALARM[", alarmId, "] cleared from ", it->second.source);
    if (notifyCb_) notifyCb_(it->second);
}

void OMS::clearAllAlarms(const std::string& source) {
    for (auto& [id, alarm] : alarms_) {
        if (alarm.source == source && alarm.active) {
            alarm.active   = false;
            alarm.severity = AlarmSeverity::CLEARED;
        }
    }
}

std::vector<Alarm> OMS::getActiveAlarms() const {
    std::vector<Alarm> active;
    for (const auto& [id, alarm] : alarms_) {
        if (alarm.active) active.push_back(alarm);
    }
    return active;
}

// ────────────────────────────────────────────────────────────────
// Performance Monitoring
// ────────────────────────────────────────────────────────────────
void OMS::updateCounter(const std::string& name, double value,
                         const std::string& unit) {
    counters_[name] = {name, value, unit};
    const std::string traceId = rbs::Logger::instance().traceId();
    if (!traceId.empty()) {
        counterTraceIds_[name] = traceId;
    }

    // KPI threshold evaluation: auto-raise or auto-clear an alarm.
    auto it = thresholds_.find(name);
    if (it == thresholds_.end()) return;
    auto& thr = it->second;
    const bool triggered = thr.belowIsAlarm ? (value < thr.threshold)
                                             : (value > thr.threshold);
    if (triggered && thr.activeAlarmId == 0) {
        thr.activeAlarmId = raiseAlarm("KPI:" + name, thr.description, thr.severity);
    } else if (!triggered && thr.activeAlarmId != 0) {
        clearAlarm(thr.activeAlarmId);
        thr.activeAlarmId = 0;
    }
}

double OMS::getCounter(const std::string& name) const {
    auto it = counters_.find(name);
    return (it != counters_.end()) ? it->second.value : 0.0;
}

std::vector<std::pair<std::string, double>> OMS::getAllCounters() const {
    std::vector<std::pair<std::string, double>> result;
    result.reserve(counters_.size());
    for (const auto& [name, ctr] : counters_) {
        result.emplace_back(name, ctr.value);
    }
    return result;
}

void OMS::printPerformanceReport() const {
    RBS_LOG_INFO("OMS", "=== Performance Report ===");
    for (const auto& [name, ctr] : counters_) {
        RBS_LOG_INFO("OMS", "  ", name, " = ", ctr.value,
                     ctr.unit.empty() ? "" : (" " + ctr.unit));
    }
    auto activeAlarms = getActiveAlarms();
    RBS_LOG_INFO("OMS", "  Active alarms: ", activeAlarms.size());
}

// ────────────────────────────────────────────────────────────────
// KPI threshold management
// ────────────────────────────────────────────────────────────────
void OMS::setKpiThreshold(const std::string& counterName,
                           const KpiThreshold& thr) {
    // Remove any previously active alarm for this threshold before overwriting.
    auto it = thresholds_.find(counterName);
    if (it != thresholds_.end() && it->second.activeAlarmId != 0)
        clearAlarm(it->second.activeAlarmId);
    ThresholdEntry entry{};
    static_cast<KpiThreshold&>(entry) = thr;
    entry.activeAlarmId = 0;
    thresholds_[counterName] = entry;
    // Immediately evaluate against current counter value, if it exists.
    auto cit = counters_.find(counterName);
    if (cit != counters_.end())
        updateCounter(counterName, cit->second.value, cit->second.unit);
}

void OMS::removeKpiThreshold(const std::string& counterName) {
    auto it = thresholds_.find(counterName);
    if (it == thresholds_.end()) return;
    if (it->second.activeAlarmId != 0)
        clearAlarm(it->second.activeAlarmId);
    thresholds_.erase(it);
}

// ────────────────────────────────────────────────────────────────
// Node state management
// ────────────────────────────────────────────────────────────────
void OMS::setNodeState(NodeState s) {
    const char* states[] = {"UNLOCKED","LOCKED","SHUTTING_DOWN"};
    nodeState_ = s;
    RBS_LOG_INFO("OMS", "Node state → ", states[static_cast<int>(s)]);
}

// ────────────────────────────────────────────────────────────────
// PM Export
// ────────────────────────────────────────────────────────────────
bool OMS::exportCsv(const std::string& filename) const {
    std::ofstream f(filename);
    if (!f.is_open()) {
        RBS_LOG_ERROR("OMS", "exportCsv: cannot open ", filename);
        return false;
    }

    // ISO-8601 timestamp
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t   = system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream ts;
    ts << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    const std::string tsStr = ts.str();

    f << "timestamp,name,value,unit\n";
    for (const auto& [name, ctr] : counters_) {
        f << tsStr << ","
          << ctr.name << ","
          << ctr.value << ","
          << ctr.unit  << "\n";
    }
    f.flush();
    RBS_LOG_INFO("OMS", "exportCsv: wrote ", counters_.size(),
                 " counters to ", filename);
    return true;
}

int OMS::pushInflux(const std::string& endpoint,
                    const std::string& measurement) const
{
    // Parse "host:port"
    auto colon = endpoint.rfind(':');
    if (colon == std::string::npos) {
        RBS_LOG_ERROR("OMS", "pushInflux: invalid endpoint ", endpoint);
        return -1;
    }
    const std::string host = endpoint.substr(0, colon);
    const int         port = std::stoi(endpoint.substr(colon + 1));

    // Build Influx Line Protocol payload (all counters in one datagram):
    //   measurement field1=v1,field2=v2 <unix_ns>
    using namespace std::chrono;
    auto now_ns = duration_cast<nanoseconds>(
                      system_clock::now().time_since_epoch()).count();

    std::ostringstream lp;
    for (const auto& [name, ctr] : counters_) {
        // Sanitise field name: replace '.' with '_'
        std::string fieldName = ctr.name;
        for (auto& ch : fieldName) if (ch == '.') ch = '_';
        lp << measurement << " "
           << fieldName << "=" << ctr.value
           << " " << now_ns << "\n";
    }
    const std::string payload = lp.str();
    if (payload.empty()) return 0;

    // UDP send (fire-and-forget, telemetry grade)
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        RBS_LOG_ERROR("OMS", "pushInflux: socket() failed");
        WSACleanup();
        return -1;
    }
#else
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { RBS_LOG_ERROR("OMS", "pushInflux: socket() failed"); return -1; }
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    int sent = static_cast<int>(sendto(sock, payload.data(),
                static_cast<int>(payload.size()), 0,
                reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    if (sent < 0) {
        RBS_LOG_ERROR("OMS", "pushInflux: send failed to ", endpoint);
        return -1;
    }
    RBS_LOG_INFO("OMS", "pushInflux: sent ", counters_.size(),
                 " metrics to ", endpoint);
    return static_cast<int>(counters_.size());
}

static std::string promName(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 4);
    out += "rbs_";
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out.push_back(c);
        else out.push_back('_');
    }
    return out;
}

static std::string promLabelEsc(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (char c : raw) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string OMS::renderPrometheus() const {
    std::ostringstream os;
    for (const auto& [name, ctr] : counters_) {
        const std::string metric = promName(name);
        os << "# TYPE " << metric << " gauge\n";
        const auto traceIt = counterTraceIds_.find(name);
        if (traceIt != counterTraceIds_.end() && !traceIt->second.empty()) {
            os << metric << "{trace_id=\"" << promLabelEsc(traceIt->second) << "\"} " << ctr.value << "\n";
        } else {
            os << metric << " " << ctr.value << "\n";
        }
    }

    // Latency histograms (Prometheus histogram type)
    for (const auto& [name, h] : histograms_) {
        const std::string metric = promName(name);
        os << "# TYPE " << metric << " histogram\n";
        for (size_t i = 0; i < h.bounds.size(); ++i) {
            os << metric << "_bucket{le=\"" << h.bounds[i] << "\"} " << h.buckCounts[i] << "\n";
        }
        os << metric << "_bucket{le=\"+Inf\"} " << h.count << "\n";
        os << metric << "_sum "   << h.sum   << "\n";
        os << metric << "_count " << h.count << "\n";
    }

    const auto active = getActiveAlarms();
    os << "# TYPE rbs_alarms_active gauge\n";
    os << "rbs_alarms_active " << active.size() << "\n";

    const char* s = "UNLOCKED";
    if (nodeState_ == NodeState::LOCKED) s = "LOCKED";
    if (nodeState_ == NodeState::SHUTTING_DOWN) s = "SHUTTING_DOWN";
    os << "# TYPE rbs_node_state_info gauge\n";
    os << "rbs_node_state_info{state=\"" << s << "\"} 1\n";
    return os.str();
}

void OMS::observeHistogram(const std::string& name, double value,
                            const std::vector<double>& bounds) {
    auto& h = histograms_[name];
    if (h.bounds.empty()) {
        h.bounds     = bounds;
        h.buckCounts.assign(bounds.size(), 0);
    }
    h.sum += value;
    ++h.count;
    for (size_t i = 0; i < h.bounds.size(); ++i) {
        if (value <= h.bounds[i]) ++h.buckCounts[i];
    }
}

std::vector<std::string> OMS::getHistogramNames() const {
    std::vector<std::string> names;
    names.reserve(histograms_.size());
    for (const auto& [n, _] : histograms_) names.push_back(n);
    return names;
}

std::vector<AlarmCorrelationGroup> OMS::getCorrelationGroups() const {
    return correlationEngine_.activeGroups();
}

std::vector<AlarmEvent> OMS::getCorrelatedAlarms() const {
    std::vector<AlarmEvent> out;
    const auto groups = correlationEngine_.activeGroups();
    for (const auto& g : groups) {
        out.push_back(g.primaryAlarm);
        out.insert(out.end(), g.relatedAlarms.begin(), g.relatedAlarms.end());
    }
    return out;
}

void OMS::initializeDefaultCorrelationRules() {
    correlationEngine_.clearRules();
    correlationEngine_.addRule({"HO_FAILED", {"RLF_DETECTED"}, 5000, true});
    correlationEngine_.addRule({"ADMISSION_REJECT", {"UE_DROP"}, 10000, true});
}

uint64_t OMS::getSuppressedAlarmCount() const {
    return correlationEngine_.totalSuppressed();
}

bool OMS::exportPrometheus(int port, const std::string& bindAddr) {
    if (prom_ && prom_->running) {
        return true;
    }

    prom_ = std::make_unique<PrometheusExporter>();
    prom_->server = std::make_unique<httplib::Server>();
    prom_->server->Get("/metrics", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(renderPrometheus(), "text/plain; version=0.0.4");
    });

    prom_->running = true;
    prom_->thread = std::thread([this, bindAddr, port]() {
        RBS_LOG_INFO("OMS", "Prometheus exporter listening on ", bindAddr, ":", port);
        prom_->server->listen(bindAddr, port);
        prom_->running = false;
    });
    return true;
}

void OMS::stopPrometheus() {
    if (!prom_) return;
    if (prom_->server) prom_->server->stop();
    if (prom_->thread.joinable()) prom_->thread.join();
    prom_.reset();
}

}  // namespace rbs::oms

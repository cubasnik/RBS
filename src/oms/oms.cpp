#include "oms.h"
#include "../common/logger.h"
#include <iomanip>
#include <sstream>
#include <fstream>
#include <chrono>
#include <cstring>
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
    alarms_[id]       = alarm;

    const char* sevStr[] = {"CLEARED","WARNING","MINOR","MAJOR","CRITICAL"};
    RBS_LOG_WARNING("OMS", "ALARM[", id, "] ",
                    sevStr[static_cast<int>(severity)],
                    " from ", source, ": ", description);

    if (notifyCb_) notifyCb_(alarm);
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

}  // namespace rbs::oms

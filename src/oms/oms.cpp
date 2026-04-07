#include "oms.h"
#include "../common/logger.h"
#include <iomanip>
#include <sstream>

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
}

double OMS::getCounter(const std::string& name) const {
    auto it = counters_.find(name);
    return (it != counters_.end()) ? it->second.value : 0.0;
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
// Node state management
// ────────────────────────────────────────────────────────────────
void OMS::setNodeState(NodeState s) {
    const char* states[] = {"UNLOCKED","LOCKED","SHUTTING_DOWN"};
    nodeState_ = s;
    RBS_LOG_INFO("OMS", "Node state → ", states[static_cast<int>(s)]);
}

}  // namespace rbs::oms

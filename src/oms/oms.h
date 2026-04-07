#pragma once
#include "../common/types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <chrono>

namespace rbs::oms {

// ────────────────────────────────────────────────────────────────
// Alarm severity levels (aligned with 3GPP TS 32.111)
// ────────────────────────────────────────────────────────────────
enum class AlarmSeverity : uint8_t {
    CLEARED   = 0,
    WARNING   = 1,
    MINOR     = 2,
    MAJOR     = 3,
    CRITICAL  = 4
};

struct Alarm {
    uint32_t    alarmId;
    std::string source;
    std::string description;
    AlarmSeverity severity;
    std::chrono::system_clock::time_point raisedAt;
    bool        active;
};

// ────────────────────────────────────────────────────────────────
// Performance counter
// ────────────────────────────────────────────────────────────────
struct PerfCounter {
    std::string name;
    double      value;
    std::string unit;
};

// ────────────────────────────────────────────────────────────────
// OMS – Operations and Maintenance Subsystem
// Provides: fault management, performance monitoring, config
// management, and software management hooks.
// Aligned with 3GPP TS 32.600 (NRM) and TS 28.623 (IOC).
// ────────────────────────────────────────────────────────────────
class OMS {
public:
    static OMS& instance() {
        static OMS inst;
        return inst;
    }

    // ── Fault Management ──────────────────────────────────────────
    uint32_t raiseAlarm(const std::string& source,
                        const std::string& description,
                        AlarmSeverity severity);
    void     clearAlarm(uint32_t alarmId);
    void     clearAllAlarms(const std::string& source);

    std::vector<Alarm> getActiveAlarms() const;

    // ── Performance Monitoring ────────────────────────────────────
    void  updateCounter(const std::string& name, double value,
                        const std::string& unit = "");
    double getCounter(const std::string& name) const;
    void  printPerformanceReport() const;

    // ── Notification callback ─────────────────────────────────────
    using AlarmNotifyCb = std::function<void(const Alarm&)>;
    void setAlarmCallback(AlarmNotifyCb cb) { notifyCb_ = std::move(cb); }

    // ── Node state ────────────────────────────────────────────────
    enum class NodeState { UNLOCKED, LOCKED, SHUTTING_DOWN };
    void      setNodeState(NodeState s);
    NodeState getNodeState() const { return nodeState_; }

private:
    OMS() = default;
    uint32_t nextAlarmId_ = 1;
    std::unordered_map<uint32_t, Alarm> alarms_;
    std::unordered_map<std::string, PerfCounter> counters_;
    AlarmNotifyCb notifyCb_;
    NodeState nodeState_ = NodeState::UNLOCKED;
};

}  // namespace rbs::oms

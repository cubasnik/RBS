#pragma once
#include "ioms.h"
#include "../common/types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <chrono>

namespace rbs::oms {

// ────────────────────────────────────────────────────────────────
// Performance counter (internal to OMS)
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
class OMS : public IOMS {
public:
    static OMS& instance() {
        static OMS inst;
        return inst;
    }

    // ── Fault Management ──────────────────────────────────────────
    uint32_t raiseAlarm(const std::string& source,
                        const std::string& description,
                        AlarmSeverity severity)                     override;
    void     clearAlarm(uint32_t alarmId)                           override;
    void     clearAllAlarms(const std::string& source)              override;

    std::vector<Alarm> getActiveAlarms() const                      override;

    // ── Performance Monitoring ────────────────────────────────────
    void  updateCounter(const std::string& name, double value,
                        const std::string& unit = "")               override;
    double getCounter(const std::string& name) const                override;
    void  printPerformanceReport() const                            override;

    // ── Notification callback ─────────────────────────────────────
    void setAlarmCallback(AlarmNotifyCb cb) override { notifyCb_ = std::move(cb); }

    // ── Node state ────────────────────────────────────────────────
    void      setNodeState(NodeState s)                             override;
    NodeState getNodeState() const                                  override { return nodeState_; }

private:
    OMS() = default;
    uint32_t nextAlarmId_ = 1;
    std::unordered_map<uint32_t, Alarm> alarms_;
    std::unordered_map<std::string, PerfCounter> counters_;
    AlarmNotifyCb notifyCb_;
    NodeState nodeState_ = NodeState::UNLOCKED;
};

}  // namespace rbs::oms

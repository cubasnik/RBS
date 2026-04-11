#pragma once
#include "../common/types.h"
#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace rbs::oms {

// ── Alarm severity levels (3GPP TS 32.111) ───────────────────────────────────
enum class AlarmSeverity : uint8_t {
    CLEARED   = 0,
    WARNING   = 1,
    MINOR     = 2,
    MAJOR     = 3,
    CRITICAL  = 4
};

// ── Active alarm record ───────────────────────────────────────────────────────
struct Alarm {
    uint32_t    alarmId;
    std::string source;
    std::string description;
    AlarmSeverity severity;
    std::chrono::system_clock::time_point raisedAt;
    bool        active;
};

// ─────────────────────────────────────────────────────────────────────────────
// IOMS — pure-virtual interface for the Operations & Maintenance Subsystem.
//
// Aligned with:
//   3GPP TS 32.111 — Fault Management
//   3GPP TS 32.404 — Performance Measurement Definitions
//   3GPP TS 32.600 — Network Resource Model (NRM)
//   3GPP TS 28.623 — Information Object Classes (IOC)
// ─────────────────────────────────────────────────────────────────────────────
class IOMS {
public:
    virtual ~IOMS() = default;

    // ── Fault Management (TS 32.111) ──────────────────────────────────────────
    /// Raise an alarm.  Returns the assigned alarm ID.
    virtual uint32_t raiseAlarm  (const std::string& source,
                                  const std::string& description,
                                  AlarmSeverity severity) = 0;

    /// Clear a specific alarm by ID.
    virtual void     clearAlarm  (uint32_t alarmId) = 0;

    /// Clear all active alarms from a given source (e.g. "RFHardware").
    virtual void     clearAllAlarms(const std::string& source) = 0;

    /// Get the list of all currently active alarms.
    virtual std::vector<Alarm> getActiveAlarms() const = 0;

    // ── Performance Monitoring (TS 32.404) ────────────────────────────────────
    /// Update or create a named performance counter.
    virtual void   updateCounter(const std::string& name, double value,
                                 const std::string& unit = "") = 0;

    /// Retrieve the latest value of a counter (returns 0.0 if unknown).
    virtual double getCounter   (const std::string& name) const = 0;

    /// Print a human-readable performance report to the log.
    virtual void   printPerformanceReport() const = 0;

    // ── KPI threshold monitoring (TS 32.111) ──────────────────────────────────
    /// Defines an automatic alarm trigger for a named counter.
    ///
    /// When @counterName is updated via updateCounter():
    ///   • if belowIsAlarm=true  and new value < threshold → raiseAlarm()
    ///   • if belowIsAlarm=false and new value > threshold → raiseAlarm()
    /// Alarm auto-clears when the condition is no longer met.
    struct KpiThreshold {
        double        threshold;     ///< Alarm trigger value
        bool          belowIsAlarm;  ///< true = low-value alarm (e.g. success rate)
        AlarmSeverity severity;
        std::string   description;   ///< Alarm description text
    };

    virtual void setKpiThreshold   (const std::string& counterName,
                                     const KpiThreshold& thr) = 0;
    virtual void removeKpiThreshold(const std::string& counterName) = 0;

    // ── Node state management (TS 32.600) ──────────────────────────────────────
    enum class NodeState : uint8_t { UNLOCKED, LOCKED, SHUTTING_DOWN };
    virtual void      setNodeState(NodeState s) = 0;
    virtual NodeState getNodeState() const = 0;

    // ── Alarm notification callback ───────────────────────────────────────────
    using AlarmNotifyCb = std::function<void(const Alarm&)>;
    virtual void setAlarmCallback(AlarmNotifyCb cb) = 0;
};

}  // namespace rbs::oms

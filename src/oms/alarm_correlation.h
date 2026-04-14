#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rbs::oms {

// ────────────────────────────────────────────────────────────────
// Alarm Correlation — Group related alarms + suppress dependents
// ────────────────────────────────────────────────────────────────

struct AlarmEvent {
    std::chrono::system_clock::time_point timestamp;
    std::string source;      ///< e.g., "LTE", "POLICY", "RFHardware"
    std::string alarmCode;   ///< e.g., "HO_FAILED", "RLF_DETECTED", "ADMISSION_REJECT"
    std::string message;
    int severity = 0;        ///< 0=INFO, 1=WARNING, 2=CRITICAL
};

struct AlarmCorrelationGroup {
    uint64_t correlationId;
    AlarmEvent primaryAlarm;
    std::vector<AlarmEvent> relatedAlarms;
    uint32_t suppressedCount = 0;   ///< Count of alarms suppressed due to this group
    std::chrono::system_clock::time_point firstSeen;
    std::chrono::system_clock::time_point lastSeen;
};

// ── Correlation rule: (primary alarm code) → (dependent codes, time window ms) ────
struct AlarmCorrelationRule {
    std::string primaryCode;                  ///< e.g., "HO_FAILED"
    std::vector<std::string> dependentCodes;  ///< e.g., ["RLF_DETECTED", "CSFB_TIMEOUT"]
    uint32_t windowMs = 5000;                 ///< Time to correlate dependent to primary
    bool suppressDependents = true;
};

class AlarmCorrelationEngine {
public:
    AlarmCorrelationEngine() = default;

    /// Register a correlation rule: primary → dependents (within windowMs)
    void addRule(const AlarmCorrelationRule& rule);
    void clearRules();

    /// Report an alarm and check for correlation
    /// Returns correlation group ID if grouped, or 0 if isolated
    uint64_t reportAlarm(const AlarmEvent& alarm);

    /// Get all active correlation groups
    std::vector<AlarmCorrelationGroup> activeGroups() const;

    /// Get group by correlation ID
    std::optional<AlarmCorrelationGroup> getGroup(uint64_t correlationId) const;

    /// Query if secondary alarm should be suppressed (due to active primary)
    bool shouldSuppress(const std::string& alarmCode) const;

    /// Cleanup old groups (older than retentionMs)
    void pruneOldGroups(uint32_t retentionMs = 300000);  // 5 min default

    /// Total suppressed alarms since engine start
    uint64_t totalSuppressed() const;

private:
    bool isCorrelated(const std::string& newAlarmCode,
                      const std::string& existingPrimaryCode,
                      int64_t elapsedMs) const;

    std::vector<AlarmCorrelationRule> rules_;
    std::unordered_map<uint64_t, AlarmCorrelationGroup> groups_;  ///< correlationId → group
    uint64_t nextGroupId_ = 1;
    uint64_t totalSuppressed_ = 0;
    mutable std::mutex mutex_;
};

}  // namespace rbs::oms

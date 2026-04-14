#include "alarm_correlation.h"
#include "../common/logger.h"
#include <algorithm>

namespace rbs::oms {

void AlarmCorrelationEngine::addRule(const AlarmCorrelationRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.push_back(rule);
    RBS_LOG_INFO("ALARM_CORR", "Rule added: ", rule.primaryCode,
                 " → ", rule.dependentCodes.size(), " dependents, ",
                 rule.windowMs, "ms window");
}

void AlarmCorrelationEngine::clearRules() {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.clear();
}

uint64_t AlarmCorrelationEngine::reportAlarm(const AlarmEvent& alarm) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if this alarm correlates with existing active groups
    for (auto& [gid, group] : groups_) {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            alarm.timestamp - group.primaryAlarm.timestamp).count();

        // Find rule for this primary
        auto it = std::find_if(rules_.begin(), rules_.end(),
            [&](const AlarmCorrelationRule& r) { return r.primaryCode == group.primaryAlarm.alarmCode; });

        if (it != rules_.end() && elapsedMs <= it->windowMs) {
            // Check if current alarm is dependent on this group's primary
            if (isCorrelated(alarm.alarmCode, group.primaryAlarm.alarmCode, elapsedMs)) {
                group.relatedAlarms.push_back(alarm);
                group.lastSeen = alarm.timestamp;
                RBS_LOG_INFO("ALARM_CORR", "Alarm correlated: gid=", gid,
                             " primary=", group.primaryAlarm.alarmCode,
                             " related=", alarm.alarmCode);
                return gid;
            }

            // Check if should suppress this alarm as secondary
            if (it->suppressDependents &&
                std::find(it->dependentCodes.begin(), it->dependentCodes.end(),
                          alarm.alarmCode) != it->dependentCodes.end()) {
                ++group.suppressedCount;
                ++totalSuppressed_;
                RBS_LOG_INFO("ALARM_CORR", "Alarm suppressed: gid=", gid,
                             " code=", alarm.alarmCode, " (dependent of ",
                             group.primaryAlarm.alarmCode, ")");
                return gid;  // Return group ID but mark as suppressed
            }
        }
    }

    // Check if this alarm is a primary (start of new correlation group)
    auto ruleIt = std::find_if(rules_.begin(), rules_.end(),
        [&](const AlarmCorrelationRule& r) { return r.primaryCode == alarm.alarmCode; });

    if (ruleIt != rules_.end()) {
        AlarmCorrelationGroup newGroup;
        newGroup.correlationId = nextGroupId_++;
        newGroup.primaryAlarm = alarm;
        newGroup.firstSeen = alarm.timestamp;
        newGroup.lastSeen = alarm.timestamp;
        groups_[newGroup.correlationId] = newGroup;
        RBS_LOG_INFO("ALARM_CORR", "New correlation group: gid=", newGroup.correlationId,
                     " primary=", alarm.alarmCode);
        return newGroup.correlationId;
    }

    // Isolated alarm (no correlation)
    return 0;
}

std::vector<AlarmCorrelationGroup> AlarmCorrelationEngine::activeGroups() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AlarmCorrelationGroup> result;
    for (const auto& kv : groups_) {
        result.push_back(kv.second);
    }
    return result;
}

std::optional<AlarmCorrelationGroup> AlarmCorrelationEngine::getGroup(uint64_t correlationId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = groups_.find(correlationId);
    if (it != groups_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool AlarmCorrelationEngine::shouldSuppress(const std::string& alarmCode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Check if alarmCode is dependent of any active primary
    for (const auto& kv : groups_) {
        const auto& group = kv.second;
        const auto elapsedMs = nowMs - std::chrono::duration_cast<std::chrono::milliseconds>(
            group.primaryAlarm.timestamp.time_since_epoch()).count();

        auto it = std::find_if(rules_.begin(), rules_.end(),
            [&](const AlarmCorrelationRule& r) { return r.primaryCode == group.primaryAlarm.alarmCode; });

        if (it != rules_.end() && elapsedMs <= it->windowMs && it->suppressDependents) {
            if (std::find(it->dependentCodes.begin(), it->dependentCodes.end(),
                          alarmCode) != it->dependentCodes.end()) {
                return true;
            }
        }
    }
    return false;
}

void AlarmCorrelationEngine::pruneOldGroups(uint32_t retentionMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto deadline = std::chrono::milliseconds(nowMs - retentionMs);

    auto it = groups_.begin();
    while (it != groups_.end()) {
        const auto lastSeenMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            it->second.lastSeen.time_since_epoch()).count();
        if (lastSeenMs < deadline.count()) {
            RBS_LOG_INFO("ALARM_CORR", "Pruning old group: gid=", it->first);
            it = groups_.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t AlarmCorrelationEngine::totalSuppressed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalSuppressed_;
}

bool AlarmCorrelationEngine::isCorrelated(const std::string& newAlarmCode,
                                           const std::string& existingPrimaryCode,
                                           int64_t elapsedMs) const {
    // Find the rule for this primary
    auto it = std::find_if(rules_.begin(), rules_.end(),
        [&](const AlarmCorrelationRule& r) { return r.primaryCode == existingPrimaryCode; });

    if (it == rules_.end()) {
        return false;
    }

    // Check if newAlarmCode is in dependents list and within time window
    return std::find(it->dependentCodes.begin(), it->dependentCodes.end(),
                     newAlarmCode) != it->dependentCodes.end()
        && elapsedMs <= it->windowMs;
}

}  // namespace rbs::oms

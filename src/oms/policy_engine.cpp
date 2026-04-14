#include "policy_engine.h"

#include <algorithm>

namespace rbs::oms {

void PolicyEngine::setMetricReader(MetricReader reader) {
    std::lock_guard<std::mutex> lk(mtx_);
    metricReader_ = std::move(reader);
}

void PolicyEngine::setActionApplier(ActionApplier applier) {
    std::lock_guard<std::mutex> lk(mtx_);
    actionApplier_ = std::move(applier);
}

void PolicyEngine::setRules(std::vector<PolicyRule> rules) {
    std::lock_guard<std::mutex> lk(mtx_);
    rules_ = std::move(rules);
    firstMatchTime_.clear();
}

const std::vector<PolicyRule>& PolicyEngine::rules() const {
    return rules_;
}

void PolicyEngine::start() {
    std::lock_guard<std::mutex> lk(mtx_);
    running_ = true;
}

void PolicyEngine::stop() {
    std::lock_guard<std::mutex> lk(mtx_);
    running_ = false;
    firstMatchTime_.clear();
}

bool PolicyEngine::isRunning() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return running_;
}

void PolicyEngine::tick() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!running_ || !metricReader_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (const auto& rule : rules_) {
        if (!rule.enabled) {
            continue;
        }

        auto metric = metricReader_(rule.metricName);
        if (!metric.has_value()) {
            firstMatchTime_.erase(rule.name);
            continue;
        }

        const bool matched = compare(rule.comparison, *metric, rule.threshold);
        if (!matched) {
            firstMatchTime_.erase(rule.name);
            continue;
        }

        auto it = firstMatchTime_.find(rule.name);
        if (it == firstMatchTime_.end()) {
            firstMatchTime_[rule.name] = now;
            continue;
        }

        if (now - it->second < rule.holdTime) {
            continue;
        }

        if (actionApplier_) {
            actionApplier_(rule.action, rule);
        }

        appendEvent(PolicyEvent{
            std::chrono::system_clock::now(),
            rule.name,
            rule.metricName,
            *metric,
            rule.action,
            "rule triggered"
        });

        // Retrigger guard: require condition to clear once before next action.
        firstMatchTime_.erase(rule.name);
    }
}

PolicyStatus PolicyEngine::status() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return PolicyStatus{running_, rules_.size(), events_.size()};
}

std::vector<PolicyEvent> PolicyEngine::recentEvents(size_t limit) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (events_.size() <= limit) {
        return events_;
    }
    return std::vector<PolicyEvent>(events_.end() - static_cast<long long>(limit), events_.end());
}

bool PolicyEngine::compare(PolicyComparison cmp, double lhs, double rhs) const {
    switch (cmp) {
        case PolicyComparison::LESS_THAN:
            return lhs < rhs;
        case PolicyComparison::GREATER_THAN:
            return lhs > rhs;
    }
    return false;
}

void PolicyEngine::appendEvent(const PolicyEvent& evt) {
    events_.push_back(evt);
    constexpr size_t MAX_EVENTS = 200;
    if (events_.size() > MAX_EVENTS) {
        events_.erase(events_.begin(), events_.begin() + static_cast<long long>(events_.size() - MAX_EVENTS));
    }
}

}  // namespace rbs::oms

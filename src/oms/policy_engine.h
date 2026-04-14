#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rbs::oms {

enum class PolicyComparison {
    LESS_THAN,
    GREATER_THAN,
};

enum class PolicyActionType {
    ADJUST_HO_HYSTERESIS,
    ADJUST_TTT,
    TIGHTEN_ADMISSION,
    RELAX_ADMISSION,
};

struct PolicyRule {
    std::string name;
    std::string metricName;
    double threshold = 0.0;
    PolicyComparison comparison = PolicyComparison::LESS_THAN;
    std::chrono::seconds holdTime{0};
    PolicyActionType action = PolicyActionType::ADJUST_HO_HYSTERESIS;
    bool enabled = true;
};

struct PolicyEvent {
    std::chrono::system_clock::time_point timestamp;
    std::string ruleName;
    std::string metricName;
    double metricValue = 0.0;
    PolicyActionType action = PolicyActionType::ADJUST_HO_HYSTERESIS;
    std::string details;
};

struct PolicyStatus {
    bool running = false;
    size_t rules = 0;
    size_t recentEvents = 0;
};

class PolicyEngine {
public:
    using MetricReader = std::function<std::optional<double>(const std::string& metricName)>;
    using ActionApplier = std::function<void(PolicyActionType action, const PolicyRule& rule)>;

    PolicyEngine() = default;

    void setMetricReader(MetricReader reader);
    void setActionApplier(ActionApplier applier);

    void setRules(std::vector<PolicyRule> rules);
    const std::vector<PolicyRule>& rules() const;

    void start();
    void stop();
    bool isRunning() const;

    // Evaluate one cycle of all rules.
    void tick();

    PolicyStatus status() const;
    std::vector<PolicyEvent> recentEvents(size_t limit = 20) const;

private:
    bool compare(PolicyComparison cmp, double lhs, double rhs) const;
    void appendEvent(const PolicyEvent& evt);

    MetricReader metricReader_;
    ActionApplier actionApplier_;

    bool running_ = false;
    std::vector<PolicyRule> rules_;
    std::vector<PolicyEvent> events_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> firstMatchTime_;

    mutable std::mutex mtx_;
};

}  // namespace rbs::oms

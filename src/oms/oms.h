#pragma once
#include "ioms.h"
#include "alarm_correlation.h"
#include "../common/types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <chrono>
#include <memory>

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

    ~OMS();

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

    // ── KPI threshold monitoring ──────────────────────────────────
    void setKpiThreshold   (const std::string& counterName,
                            const KpiThreshold& thr)                override;
    void removeKpiThreshold(const std::string& counterName)         override;

    // ── Notification callback ─────────────────────────────────────
    void setAlarmCallback(AlarmNotifyCb cb) override { notifyCb_ = std::move(cb); }

    // ── Node state ────────────────────────────────────────────────
    void      setNodeState(NodeState s)                             override;
    NodeState getNodeState() const                                  override { return nodeState_; }

    // ── Counter enumeration (for REST API / export) ───────────────
    std::vector<std::pair<std::string, double>> getAllCounters() const;

    // ── PM Export ──────────────────────────────────────────────
    /// Write all PM counters to a CSV file.
    /// Format: timestamp,name,value,unit
    /// Returns true on success.
    bool exportCsv(const std::string& filename) const;

    /// Emit all PM counters as InfluxDB Line Protocol to a UDP endpoint.
    /// endpoint format: "host:port"  (e.g. "127.0.0.1:8089")
    /// Returns the number of metrics sent, or -1 on error.
    int  pushInflux(const std::string& endpoint,
                    const std::string& measurement = "rbs_pm") const;

    /// Start Prometheus/OpenMetrics exporter at /metrics.
    /// Returns true on successful startup.
    bool exportPrometheus(int port, const std::string& bindAddr = "127.0.0.1");

    /// Stop Prometheus exporter if running.
    void stopPrometheus();

    /// Build metrics snapshot in Prometheus text format.
    std::string renderPrometheus() const;

    // ── Latency histograms (Prometheus histogram type) ────────────
    /// Record one observed value into a named histogram.
    /// @param name    Metric name (becomes rbs_<name> in Prometheus).
    /// @param value   Observed value (any unit; us recommended for latency).
    /// @param bounds  Sorted list of bucket upper-bounds.  Must be identical
    ///                across all calls for the same @name.
    void observeHistogram(const std::string& name, double value,
                          const std::vector<double>& bounds);

    /// Return names of all registered histograms.
    std::vector<std::string> getHistogramNames() const;

    // ── Alarm Correlation ────────────────────────────────────────────
    std::vector<AlarmCorrelationGroup> getCorrelationGroups() const;
    std::vector<AlarmEvent> getCorrelatedAlarms() const;
    void initializeDefaultCorrelationRules();
    uint64_t getSuppressedAlarmCount() const;

private:
    OMS();
    AlarmCorrelationEngine correlationEngine_;
    uint32_t nextAlarmId_ = 1;
    std::unordered_map<uint32_t, Alarm>       alarms_;
    std::unordered_map<std::string, PerfCounter> counters_;
    std::unordered_map<std::string, std::string> counterTraceIds_;
    AlarmNotifyCb notifyCb_;
    NodeState nodeState_ = NodeState::UNLOCKED;

    // Threshold entries: KpiThreshold + the ID of any currently active alarm
    struct ThresholdEntry : KpiThreshold {
        uint32_t activeAlarmId = 0;  ///< 0 = no alarm raised yet
    };
    std::unordered_map<std::string, ThresholdEntry> thresholds_;

    // Prometheus histogram storage
    struct HistogramData {
        std::vector<double>  bounds;      ///< sorted upper-bounds
        std::vector<int64_t> buckCounts;  ///< cumulative count <= bounds[i]
        double   sum   = 0.0;
        int64_t  count = 0;
    };
    std::unordered_map<std::string, HistogramData> histograms_;

    struct PrometheusExporter;
    std::unique_ptr<PrometheusExporter> prom_;
};

}  // namespace rbs::oms

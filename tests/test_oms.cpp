#include "../src/oms/oms.h"
#include <cassert>
#include <cstdio>
#include <atomic>

int main() {
    using namespace rbs::oms;
    OMS& oms = OMS::instance();

    // ── Node state ────────────────────────────────────────────────────────────
    assert(oms.getNodeState() == IOMS::NodeState::UNLOCKED);
    oms.setNodeState(IOMS::NodeState::LOCKED);
    assert(oms.getNodeState() == IOMS::NodeState::LOCKED);
    oms.setNodeState(IOMS::NodeState::UNLOCKED);

    // ── Alarm callback ────────────────────────────────────────────────────────
    std::atomic<int> cbCount{0};
    oms.setAlarmCallback([&](const Alarm& a) {
        (void)a;
        ++cbCount;
    });

    // ── raiseAlarm ────────────────────────────────────────────────────────────
    assert(oms.getActiveAlarms().empty());

    uint32_t id1 = oms.raiseAlarm("LTE-Cell-1", "RF power loss", AlarmSeverity::MAJOR);
    assert(id1 != 0);
    assert(cbCount.load() == 1);
    assert(oms.getActiveAlarms().size() == 1);
    assert(oms.getActiveAlarms()[0].severity == AlarmSeverity::MAJOR);

    uint32_t id2 = oms.raiseAlarm("GSM-Cell-2", "Clock sync lost", AlarmSeverity::CRITICAL);
    assert(id2 != 0);
    assert(id2 != id1);
    assert(cbCount.load() == 2);
    assert(oms.getActiveAlarms().size() == 2);

    uint32_t id3 = oms.raiseAlarm("LTE-Cell-1", "Temp warning", AlarmSeverity::WARNING);
    assert(oms.getActiveAlarms().size() == 3);

    // ── clearAlarm ────────────────────────────────────────────────────────────
    oms.clearAlarm(id1);
    assert(cbCount.load() == 4);   // callback fired for clear too
    // Активных теперь 2 (id2 и id3 ещё active)
    auto active = oms.getActiveAlarms();
    assert(active.size() == 2);
    for (const auto& a : active) {
        assert(a.alarmId != id1);
    }

    // ── clearAllAlarms по источнику ───────────────────────────────────────────
    oms.clearAllAlarms("LTE-Cell-1");
    // id3 (LTE-Cell-1) должен стать inactive; id2 (GSM-Cell-2) остаётся
    active = oms.getActiveAlarms();
    assert(active.size() == 1);
    assert(active[0].alarmId == id2);
    assert(active[0].source  == "GSM-Cell-2");

    oms.clearAllAlarms("GSM-Cell-2");
    assert(oms.getActiveAlarms().empty());

    // ── Performance counters ──────────────────────────────────────────────────
    oms.updateCounter("lte.dl.throughput.mbps", 150.5, "Mbps");
    oms.updateCounter("lte.ul.throughput.mbps",  30.2, "Mbps");
    oms.updateCounter("gsm.active.calls",          3.0, "calls");

    assert(oms.getCounter("lte.dl.throughput.mbps") == 150.5);
    assert(oms.getCounter("lte.ul.throughput.mbps") ==  30.2);
    assert(oms.getCounter("gsm.active.calls")       ==   3.0);

    // Несуществующий счётчик возвращает 0
    assert(oms.getCounter("nonexistent") == 0.0);

    // Обновление счётчика
    oms.updateCounter("lte.dl.throughput.mbps", 200.0, "Mbps");
    assert(oms.getCounter("lte.dl.throughput.mbps") == 200.0);

    // printPerformanceReport не крашится
    oms.printPerformanceReport();

    // ── KPI threshold: low-value alarm (RRC success rate) ────────────────────
    oms.setKpiThreshold("lte.rrc.successRate.pct", {
        80.0, true, AlarmSeverity::MAJOR, "RRC setup success rate below 80%"
    });
    // Rate = 100% (above threshold) → no alarm
    oms.updateCounter("lte.rrc.successRate.pct", 100.0, "%");
    assert(oms.getActiveAlarms().empty());

    // Rate drops to 70% → alarm auto-raised
    oms.updateCounter("lte.rrc.successRate.pct", 70.0, "%");
    {
        auto thrAlarms = oms.getActiveAlarms();
        assert(thrAlarms.size() == 1);
        assert(thrAlarms[0].source   == "KPI:lte.rrc.successRate.pct");
        assert(thrAlarms[0].severity == AlarmSeverity::MAJOR);
    }

    // Rate recovers to 85% → alarm auto-cleared
    oms.updateCounter("lte.rrc.successRate.pct", 85.0, "%");
    assert(oms.getActiveAlarms().empty());

    // ── KPI threshold: high-value alarm (E-RAB drop rate) ────────────────────
    oms.setKpiThreshold("lte.erab.dropRate.pct", {
        5.0, false, AlarmSeverity::MINOR, "E-RAB drop rate exceeded 5%"
    });
    oms.updateCounter("lte.erab.dropRate.pct", 3.0, "%");  // below → no alarm
    assert(oms.getActiveAlarms().empty());
    oms.updateCounter("lte.erab.dropRate.pct", 8.0, "%");  // above → alarm
    assert(oms.getActiveAlarms().size() == 1);
    oms.updateCounter("lte.erab.dropRate.pct", 4.0, "%");  // drops back → cleared
    assert(oms.getActiveAlarms().empty());

    // ── removeKpiThreshold clears active alarm ────────────────────────────────
    oms.setKpiThreshold("lte.ho.successRate.pct", {
        90.0, true, AlarmSeverity::WARNING, "HO success rate below 90%"
    });
    oms.updateCounter("lte.ho.successRate.pct", 50.0, "%");  // triggers
    assert(oms.getActiveAlarms().size() == 1);
    oms.removeKpiThreshold("lte.ho.successRate.pct");
    assert(oms.getActiveAlarms().empty());  // alarm cleared on remove

    // ── setKpiThreshold immediately evaluates current value ───────────────────
    oms.updateCounter("lte.rrc.successRate.pct", 60.0, "%");  // already stored
    oms.setKpiThreshold("lte.rrc.successRate.pct", {
        80.0, true, AlarmSeverity::MAJOR, "RRC below 80%"
    });  // should immediately raise alarm on stored value 60 < 80
    assert(oms.getActiveAlarms().size() == 1);
    oms.removeKpiThreshold("lte.rrc.successRate.pct");
    assert(oms.getActiveAlarms().empty());

    std::puts("test_oms PASSED");
    return 0;
}

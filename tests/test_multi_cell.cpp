#include "../src/lte/lte_stack.h"
#include "../src/lte/multi_cell_model.h"
#include "../src/hal/rf_hardware.h"
#include "../src/oms/oms.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    constexpr int kCells = 3;

    std::vector<std::shared_ptr<rbs::hal::RFHardware>> rfs;
    std::vector<std::unique_ptr<rbs::lte::LTEStack>> stacks;
    rfs.reserve(kCells);
    stacks.reserve(kCells);

    for (int i = 0; i < kCells; ++i) {
        rbs::LTECellConfig cfg{};
        cfg.cellId = static_cast<rbs::CellId>(100 + i);
        cfg.earfcn = static_cast<rbs::EARFCN>(1900 + i * 5);
        cfg.band = rbs::LTEBand::B3;
        cfg.bandwidth = rbs::LTEBandwidth::BW10;
        cfg.duplexMode = rbs::LTEDuplexMode::FDD;
        cfg.txPower = {43.0};
        cfg.pci = static_cast<uint16_t>(200 + i);
        cfg.tac = 1;
        cfg.mcc = 250;
        cfg.mnc = 1;
        cfg.numAntennas = 2;
        cfg.mmeAddr = "";
        cfg.s1uLocalPort = static_cast<uint16_t>(3352 + i);

        auto rf = std::make_shared<rbs::hal::RFHardware>(2, 4);
        assert(rf->initialise());
        auto stack = std::make_unique<rbs::lte::LTEStack>(rf, cfg);
        assert(stack->start());

        rfs.push_back(rf);
        stacks.push_back(std::move(stack));
    }

    auto& oms = rbs::oms::OMS::instance();

    double prevSinr = 1e9;
    for (int i = 0; i < kCells; ++i) {
        const rbs::RNTI rnti = stacks[i]->admitUE(790000000000000ULL + static_cast<uint64_t>(i), 10);
        assert(rnti != 0);

        const double sinrDb = rbs::lte::estimateSinrDb(300.0 + i * 120.0, kCells - 1, 43.0);
        const uint8_t cqi = rbs::lte::sinrToCqi(sinrDb);
        stacks[i]->updateCQI(rnti, cqi);

        const std::string base = "lte.cell." + std::to_string(stacks[i]->config().cellId) + ".";
        oms.updateCounter(base + "sinr.db", sinrDb, "dB");
        oms.updateCounter(base + "connectedUEs", static_cast<double>(stacks[i]->connectedUECount()));

        if (i > 0) {
            assert(sinrDb < prevSinr);
        }
        prevSinr = sinrDb;

        stacks[i]->releaseUE(rnti);
    }

    const int promPort = 39108;
    assert(oms.exportPrometheus(promPort, "127.0.0.1"));
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string metrics = oms.renderPrometheus();
    assert(metrics.find("rbs_lte_cell_100_sinr_db") != std::string::npos);
    assert(metrics.find("rbs_alarms_active") != std::string::npos);
    oms.stopPrometheus();

    for (int i = 0; i < kCells; ++i) {
        stacks[i]->stop();
        rfs[i]->shutdown();
    }

    std::puts("test_multi_cell PASSED");
    return 0;
}

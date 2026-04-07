#pragma once
#include "../common/types.h"
#include "gsm_phy.h"
#include "gsm_mac.h"
#include "../hal/rf_interface.h"
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace rbs::gsm {

// ────────────────────────────────────────────────────────────────
// GSM Stack – top-level controller for the 2G cell
// Wires PHY → MAC → RLC layers and drives the TDMA clock.
// ────────────────────────────────────────────────────────────────
class GSMStack {
public:
    explicit GSMStack(std::shared_ptr<hal::IRFHardware> rf,
                      const GSMCellConfig& cfg);
    ~GSMStack();

    bool  start();
    void  stop();
    bool  isRunning() const { return running_.load(); }

    // Send user-plane data to a connected UE
    bool  sendData(RNTI rnti, ByteBuffer data);
    // Receive user-plane data from a connected UE
    bool  receiveData(RNTI rnti, ByteBuffer& data);

    // Admit / release a UE
    RNTI  admitUE(IMSI imsi);
    void  releaseUE(RNTI rnti);

    // Statistics
    size_t connectedUECount() const;
    void   printStats() const;

    const GSMCellConfig& config() const { return cfg_; }

private:
    GSMCellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<GSMPhy>  phy_;
    std::shared_ptr<GSMMAC>  mac_;

    std::atomic<bool>  running_{false};
    std::thread        clockThread_;
    std::unordered_map<RNTI, IMSI> ueMap_;

    void clockLoop();   // simulates TDMA timeslot ticker
};

}  // namespace rbs::gsm

#pragma once
#include "../common/types.h"
#include "lte_phy.h"
#include "lte_mac.h"
#include "lte_pdcp.h"
#include "../hal/rf_interface.h"
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace rbs::lte {

// ────────────────────────────────────────────────────────────────
// LTE Stack – top-level eNodeB cell controller
// Wires PDCP → (RLC stub) → MAC → PHY and drives the 1 ms
// subframe clock.
// ────────────────────────────────────────────────────────────────
class LTEStack {
public:
    explicit LTEStack(std::shared_ptr<hal::IRFHardware> rf,
                      const LTECellConfig& cfg);
    ~LTEStack();

    bool  start();
    void  stop();
    bool  isRunning() const { return running_.load(); }

    // IP-level send / receive (user plane via PDCP)
    bool  sendIPPacket (RNTI rnti, uint16_t bearerId, ByteBuffer ipPacket);
    bool  receiveIPPacket(RNTI rnti, uint16_t bearerId, ByteBuffer& ipPacket);

    // UE admission
    RNTI  admitUE(IMSI imsi, uint8_t defaultCQI = 9);
    void  releaseUE(RNTI rnti);
    void  updateCQI(RNTI rnti, uint8_t cqi);

    size_t connectedUECount() const;
    void   printStats() const;

    const LTECellConfig& config() const { return cfg_; }

private:
    LTECellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<LTEPhy>  phy_;
    std::shared_ptr<LTEMAC>  mac_;
    std::shared_ptr<PDCP>    pdcp_;

    std::atomic<bool>  running_{false};
    std::thread        subframeThread_;
    RNTI nextRnti_ = 1;
    std::unordered_map<RNTI, IMSI> ueMap_;

    void subframeLoop();
};

}  // namespace rbs::lte

#pragma once
#include "../common/types.h"
#include "lte_phy.h"
#include "lte_mac.h"
#include "lte_pdcp.h"
#include "lte_rrc.h"
#include "lte_rlc.h"
#include "../hal/rf_interface.h"
#include "ilte_stack.h"
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
class LTEStack : public ILTEStack {
public:
    explicit LTEStack(std::shared_ptr<hal::IRFHardware> rf,
                      const LTECellConfig& cfg);
    ~LTEStack();

    bool  start()                                                              override;
    void  stop()                                                               override;
    bool  isRunning() const                                                    override { return running_.load(); }

    bool  sendIPPacket  (RNTI rnti, uint16_t bearerId, ByteBuffer ipPacket)    override;
    bool  receiveIPPacket(RNTI rnti, uint16_t bearerId, ByteBuffer& ipPacket)  override;

    RNTI  admitUE(IMSI imsi, uint8_t defaultCQI = 9)                           override;
    void  releaseUE(RNTI rnti)                                                 override;
    void  updateCQI(RNTI rnti, uint8_t cqi)                                   override;

    size_t connectedUECount() const                                            override;
    void   printStats() const                                                  override;

    const LTECellConfig& config() const                                        override { return cfg_; }

private:
    LTECellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<LTEPhy>  phy_;
    std::shared_ptr<LTEMAC>  mac_;
    std::shared_ptr<PDCP>    pdcp_;
    std::shared_ptr<LTERrc>  rrc_;
    std::shared_ptr<LTERlc>  rlc_;

    std::atomic<bool>  running_{false};
    std::thread        subframeThread_;
    RNTI nextRnti_ = 1;
    std::unordered_map<RNTI, IMSI> ueMap_;

    void subframeLoop();
};

}  // namespace rbs::lte

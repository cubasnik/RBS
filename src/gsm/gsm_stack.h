#pragma once
#include "../common/types.h"
#include "gsm_phy.h"
#include "gsm_mac.h"
#include "gsm_rr.h"
#include "gsm_rlc.h"
#include "../hal/rf_interface.h"
#include "igsm_stack.h"
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace rbs::gsm {

// ────────────────────────────────────────────────────────────────
// GSM Stack – top-level controller for the 2G cell
// Wires PHY → MAC → RLC layers and drives the TDMA clock.
// ────────────────────────────────────────────────────────────────
class GSMStack : public IGSMStack {
public:
    explicit GSMStack(std::shared_ptr<hal::IRFHardware> rf,
                      const GSMCellConfig& cfg);
    ~GSMStack();

    bool  start()                                        override;
    void  stop()                                         override;
    bool  isRunning() const                              override { return running_.load(); }

    bool  sendData(RNTI rnti, ByteBuffer data)           override;
    bool  receiveData(RNTI rnti, ByteBuffer& data)       override;

    RNTI  admitUE(IMSI imsi)                             override;
    void  releaseUE(RNTI rnti)                           override;

    size_t connectedUECount() const                      override;
    void   printStats() const                            override;

    const GSMCellConfig& config() const                  override { return cfg_; }

private:
    GSMCellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<GSMPhy>  phy_;
    std::shared_ptr<GSMMAC>  mac_;
    std::shared_ptr<GSMRr>   rr_;
    std::shared_ptr<GSMRlc>  rlc_;

    std::atomic<bool>  running_{false};
    std::thread        clockThread_;
    std::unordered_map<RNTI, IMSI> ueMap_;

    void clockLoop();   // simulates TDMA timeslot ticker
};

}  // namespace rbs::gsm

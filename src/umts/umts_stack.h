#pragma once
#include "../common/types.h"
#include "umts_phy.h"
#include "umts_mac.h"
#include "../hal/rf_interface.h"
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace rbs::umts {

// ────────────────────────────────────────────────────────────────
// UMTS Stack – top-level NodeB cell controller.
// Drives the 10 ms WCDMA radio frame clock and exposes the
// user-plane API used by the RBS scheduler.
// ────────────────────────────────────────────────────────────────
class UMTSStack {
public:
    explicit UMTSStack(std::shared_ptr<hal::IRFHardware> rf,
                       const UMTSCellConfig& cfg);
    ~UMTSStack();

    bool  start();
    void  stop();
    bool  isRunning() const { return running_.load(); }

    bool  sendData(RNTI rnti, ByteBuffer data);
    bool  receiveData(RNTI rnti, ByteBuffer& data);

    RNTI  admitUE(IMSI imsi, SF sf = SF::SF16);
    void  releaseUE(RNTI rnti);

    size_t connectedUECount() const;
    void   printStats() const;

    const UMTSCellConfig& config() const { return cfg_; }

private:
    UMTSCellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<UMTSPhy>  phy_;
    std::shared_ptr<UMTSMAC>  mac_;

    std::atomic<bool>  running_{false};
    std::thread        frameThread_;
    std::unordered_map<RNTI, IMSI> ueMap_;

    void frameLoop();
};

}  // namespace rbs::umts

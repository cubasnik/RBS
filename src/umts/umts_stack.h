#pragma once
#include "../common/types.h"
#include "umts_phy.h"
#include "umts_mac.h"
#include "umts_rrc.h"
#include "umts_rlc.h"
#include "iub_link.h"
#include "../hal/rf_interface.h"
#include "iumts_stack.h"
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
class UMTSStack : public IUMTSStack {
public:
    explicit UMTSStack(std::shared_ptr<hal::IRFHardware> rf,
                       const UMTSCellConfig& cfg);
    ~UMTSStack();

    bool  start()                                              override;
    void  stop()                                               override;
    bool  isRunning() const                                    override { return running_.load(); }

    bool  sendData(RNTI rnti, ByteBuffer data)                 override;
    bool  receiveData(RNTI rnti, ByteBuffer& data)             override;

    RNTI  admitUE     (IMSI imsi, SF sf = SF::SF16)           override;
    void  releaseUE   (RNTI rnti)                              override;
    RNTI  admitUEHSDPA(IMSI imsi)                              override;
    RNTI  admitUEEDCH (IMSI imsi)                              override;
    bool  reconfigureDCH(RNTI rnti, SF newSf)                  override;
    void  softHandoverUpdate(const MeasurementReport& report)  override;
    const std::vector<ActiveSetEntry>& activeSet(RNTI rnti) const override;

    size_t connectedUECount() const                            override;
    void   printStats() const                                  override;

    const UMTSCellConfig& config() const                       override { return cfg_; }

private:
    UMTSCellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<UMTSPhy>  phy_;
    std::shared_ptr<UMTSMAC>  mac_;
    std::shared_ptr<UMTSRrc>  rrc_;
    std::shared_ptr<UMTSRlc>  rlc_;
    std::unique_ptr<IubNbap>  iub_;

    std::atomic<bool>  running_{false};
    std::thread        frameThread_;
    std::unordered_map<RNTI, IMSI> ueMap_;

    void frameLoop();
};

}  // namespace rbs::umts

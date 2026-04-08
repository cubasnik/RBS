#pragma once
#include "../common/types.h"
#include "../hal/rf_interface.h"
#include "ilte_phy.h"
#include <memory>
#include <functional>
#include <vector>

namespace rbs::lte {

// ────────────────────────────────────────────────────────────────
// LTE Physical Layer
// Implements OFDMA (DL) / SC-FDMA (UL) frame structure,
// resource block mapping, reference signals and physical channels.
// References: 3GPP TS 36.211, 36.212, 36.213
// ────────────────────────────────────────────────────────────────
class LTEPhy : public ILTEPhy {
public:
    explicit LTEPhy(std::shared_ptr<hal::IRFHardware> rf, const LTECellConfig& cfg);

    bool start()                                                   override;
    void stop()                                                    override;
    void tick()                                                    override;

    bool transmitSubframe(const LTESubframe& sf)                   override;
    bool receiveSubframe(LTESubframe& sf)                          override;

    using RxSubframeCb = std::function<void(const LTESubframe&)>;
    void setRxCallback(RxSubframeCb cb)                            override { rxCb_ = std::move(cb); }

    uint32_t currentSFN()           const                          override { return sfn_; }
    uint8_t  currentSubframeIndex() const                          override { return sfIdx_; }
    uint8_t  numResourceBlocks()    const                          override {
        return lteBandwidthToRB(cfg_.bandwidth);
    }

    double measuredRSRP() const                                    override { return rsrp_dBm_; }

private:
    std::shared_ptr<hal::IRFHardware> rf_;
    LTECellConfig cfg_;
    uint32_t sfn_        = 0;
    uint8_t  sfIdx_      = 0;
    bool     running_    = false;
    double   rsrp_dBm_   = -85.0;
    RxSubframeCb rxCb_;

    // Channel builders
    ByteBuffer buildPSS()  const;   // Primary Synchronisation Signal
    ByteBuffer buildSSS()  const;   // Secondary Synchronisation Signal
    ByteBuffer buildPBCH() const;   // Physical Broadcast Channel (MIB)
    ByteBuffer buildPDCCH(const std::vector<ResourceBlock>& grants) const;
    ByteBuffer buildPDSCH(const std::vector<ResourceBlock>& grants) const;

    // OFDM modulation helper (simplified IQ generation)
    ByteBuffer ofdmModulate(const ByteBuffer& freqDomain) const;

    bool isSyncSubframe() const;  // subframe 0 contains PSS/SSS/PBCH
};

}  // namespace rbs::lte

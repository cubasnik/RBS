#pragma once
#include "../common/types.h"
#include "../hal/rf_interface.h"
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
class LTEPhy {
public:
    explicit LTEPhy(std::shared_ptr<hal::IRFHardware> rf, const LTECellConfig& cfg);

    bool start();
    void stop();
    void tick();   // Called once per 1 ms subframe

    // Map a grant list onto physical resource blocks and transmit
    bool transmitSubframe(const LTESubframe& sf);

    // Receive UL subframe and decode
    bool receiveSubframe(LTESubframe& sf);

    using RxSubframeCb = std::function<void(const LTESubframe&)>;
    void setRxCallback(RxSubframeCb cb) { rxCb_ = std::move(cb); }

    uint32_t currentSFN()           const { return sfn_; }
    uint8_t  currentSubframeIndex() const { return sfIdx_; }
    uint8_t  numResourceBlocks()    const {
        return lteBandwidthToRB(cfg_.bandwidth);
    }

    // RSRP measurement of reference signal (simulated)
    double measuredRSRP() const { return rsrp_dBm_; }

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

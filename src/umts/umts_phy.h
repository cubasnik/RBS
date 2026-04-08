#pragma once
#include "../common/types.h"
#include "../hal/rf_interface.h"
#include "iumts_phy.h"
#include <memory>
#include <functional>
#include <vector>

namespace rbs::umts {

// ────────────────────────────────────────────────────────────────
// UMTS Physical Layer
// Implements WCDMA spreading, scrambling, CPICH/SCH broadcasting
// and pilot signal generation.
// References: 3GPP TS 25.211, 25.212, 25.213
// ────────────────────────────────────────────────────────────────
class UMTSPhy : public IUMTSPhy {
public:
    explicit UMTSPhy(std::shared_ptr<hal::IRFHardware> rf, const UMTSCellConfig& cfg);

    bool start()                                                           override;
    void stop()                                                            override;
    void tick()                                                            override;

    bool transmit(uint16_t channelCode, SF spreadingFactor,
                  const ByteBuffer& data)                                  override;
    bool receive (uint16_t channelCode, SF spreadingFactor,
                  ByteBuffer& data, uint32_t numBits)                      override;

    using RxFrameCb = std::function<void(const UMTSFrame&)>;
    void setRxCallback(RxFrameCb cb)                                       override { rxCb_ = std::move(cb); }

    uint32_t  currentFrameNumber() const                                   override { return frameNumber_; }
    double    measuredRSCP()        const                                   override { return rscp_dBm_; }

private:
    std::shared_ptr<hal::IRFHardware> rf_;
    UMTSCellConfig cfg_;
    uint32_t frameNumber_ = 0;
    bool     running_     = false;
    double   rscp_dBm_    = -80.0;
    RxFrameCb rxCb_;

    ByteBuffer buildCPICH()  const;   // Common Pilot Channel
    ByteBuffer buildSCH()    const;   // Synchronisation Channel
    ByteBuffer buildPCCPCH() const;   // Primary Common Control Physical Channel (BCH)
    ByteBuffer spread(const ByteBuffer& data, SF sf, uint16_t code) const;
    ByteBuffer scramble(const ByteBuffer& chips) const;
};

}  // namespace rbs::umts

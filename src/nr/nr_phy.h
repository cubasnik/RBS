#pragma once
#include "../common/types.h"
#include "../hal/rf_interface.h"
#include <memory>
#include <functional>
#include <cstdint>

namespace rbs::nr {

// ────────────────────────────────────────────────────────────────
// NRPhy — 5G NR gNB-DU Physical Layer (stub)
//
// Generates Synchronization Signal Blocks (SSB / SS-PBCH, TS 38.211
// §7.4.3) and drives the NR slot clock (1 ms per subframe tick,
// µ=0 reference).  Higher SCS slots are tracked internally.
//
// References: TS 38.211 §4, §5, §7.4.3
//             TS 38.213 §4.1 (SSB periodicity & timing)
// ────────────────────────────────────────────────────────────────
class NRPhy {
public:
    explicit NRPhy(std::shared_ptr<hal::IRFHardware> rf, const NRCellConfig& cfg);

    bool start();
    void stop();

    /// Advance one 1 ms subframe.  Generates SSB when due.
    void tick();

    using SSBCallback = std::function<void(const NRSSBlock&)>;
    /// Register a callback invoked whenever an SSB is transmitted.
    void setSSBCallback(SSBCallback cb) { ssbCb_ = std::move(cb); }

    uint32_t currentSFN()          const { return sfn_; }
    uint8_t  currentSubframeIdx()  const { return sfIdx_; }
    uint32_t ssbTxCount()          const { return ssbCount_; }
    bool     isRunning()           const { return running_; }
    double   measuredSSRSRP()      const { return ssRsrp_dBm_; }

private:
    std::shared_ptr<hal::IRFHardware> rf_;
    NRCellConfig cfg_;
    uint32_t sfn_       = 0;
    uint8_t  sfIdx_     = 0;   ///< subframe index 0-9 within frame
    uint32_t ssbCount_  = 0;
    bool     running_   = false;
    double   ssRsrp_dBm_= -80.0;
    SSBCallback ssbCb_;

    /// Build PSS for given pci (TS 38.211 §7.4.2.2.1)
    ByteBuffer buildPSS(uint16_t pci) const;

    /// Build SSS for given PCI (TS 38.211 §7.4.2.3.1)
    ByteBuffer buildSSS(uint16_t pci) const;

    /// Build PBCH payload: MIB + 24-bit CRC (TS 38.331 §6.2.2)
    ByteBuffer buildPBCH(uint32_t sfn, uint8_t halfFrame) const;

    /// True when this subframe should carry an SSB burst
    bool isSSBSubframe() const;
};

}  // namespace rbs::nr

#pragma once
#include "../common/types.h"
#include "../hal/rf_interface.h"
#include "ilte_phy.h"
#include <memory>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>

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

    // ── UL channel builders (public for testability) ─────────────────────
    /// PUCCH encoder: fmt=FORMAT_1(SR)/FORMAT_1A(ACK-NACK)/FORMAT_2(CQI). TS 36.211 §5.4
    ByteBuffer buildPUCCH(PUCCHFormat fmt, RNTI rnti, uint8_t value) const;

    /// PUSCH SC-FDMA TB with embedded DMRS. TS 36.211 §5.3
    ByteBuffer buildPUSCH(const ResourceBlock& rb, uint32_t tbBytes) const;

    /// SRS CAZAC sequence. TS 36.211 §5.5.3  (bwConfig 0-3 → 8/16/32/64 RBs)
    ByteBuffer buildSRS(RNTI rnti, uint8_t bwConfig = 0) const;

    /// DMRS for one slot of PUSCH. TS 36.211 §5.5.2  (slotIdx = 0 or 1)
    ByteBuffer buildDMRS(RNTI rnti, uint8_t slotIdx) const;

    /// Inject pre-built UL subframe into receive queue (used by MAC for simulation)
    void injectUlSignal(LTESubframe sf);

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

    std::queue<LTESubframe> ulInjectQueue_;  ///< MAC-injected UL subframes for loopback
    mutable std::mutex      ulMtx_;
};

}  // namespace rbs::lte

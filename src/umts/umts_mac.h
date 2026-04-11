#pragma once
#include "../common/types.h"
#include "umts_phy.h"
#include "iumts_mac.h"
#include <unordered_map>
#include <queue>
#include <memory>

namespace rbs::umts {

// ────────────────────────────────────────────────────────────────
// Per-UE UMTS channel descriptor
// ────────────────────────────────────────────────────────────────
struct UMTSUEContext {
    RNTI               rnti;
    UMTSChannelType    channelType;
    uint16_t           channelCode;
    SF                 spreadingFactor;
    bool               active;
    std::queue<ByteBuffer> txQueue;
    std::queue<ByteBuffer> rxQueue;
};

// ────────────────────────────────────────────────────────────────
// UMTS MAC layer
// Handles transport-channel multiplexing, RACH/FACH management,
// and DCH scheduling. References: 3GPP TS 25.321
// ────────────────────────────────────────────────────────────────
class UMTSMAC : public IUMTSMAC {
public:
    explicit UMTSMAC(std::shared_ptr<UMTSPhy> phy, const UMTSCellConfig& cfg);

    bool start()                                       override;
    void stop()                                        override;
    void tick()                                        override;

    RNTI  assignDCH(SF sf = SF::SF16)                  override;
    RNTI  assignHSDSCH()                               override;
    RNTI  assignEDCH()                                 override;
    bool  releaseDCH(RNTI rnti)                        override;

    bool  enqueueDlData(RNTI rnti, ByteBuffer data)    override;
    bool  dequeueUlData(RNTI rnti, ByteBuffer& data)   override;

    size_t activeChannelCount() const override { return channels_.size(); }
    size_t hsdschUECount()      const override { return hsdschCount_; }
    size_t edchUECount()        const override { return edchCount_; }

private:
    std::shared_ptr<UMTSPhy> phy_;
    UMTSCellConfig cfg_;
    bool     running_ = false;
    RNTI     nextRnti_ = 1;
    uint16_t nextCode_ = 4;  // Channel codes 0-3 reserved for common channels

    std::unordered_map<RNTI, UMTSUEContext> channels_;
    size_t   hsdschCount_ = 0;
    size_t   edchCount_   = 0;

    void onRxFrame(const UMTSFrame& frame);
    void scheduleDlTransmissions();
};

}  // namespace rbs::umts

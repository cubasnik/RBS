#pragma once
#include "../common/types.h"
#include "gsm_phy.h"
#include "igsm_mac.h"
#include <unordered_map>
#include <queue>
#include <memory>

namespace rbs::gsm {

// ────────────────────────────────────────────────────────────────
// Logical channel state for one UE
// ────────────────────────────────────────────────────────────────
struct GSMUEChannel {
    RNTI              rnti;
    uint8_t           timeSlot;
    GSMChannelType    channelType;
    bool              active;
    uint8_t           seqNum;          // Layer-2 sequence number (0..7)
    std::queue<ByteBuffer> txQueue;
    std::queue<ByteBuffer> rxQueue;
};

// ────────────────────────────────────────────────────────────────
// GSM MAC layer (TDMA channel assignment, random access, paging)
// References: 3GPP TS 44.003, TS 44.006
// ────────────────────────────────────────────────────────────────
class GSMMAC : public IGSMMAC {
public:
    explicit GSMMAC(std::shared_ptr<GSMPhy> phy, const GSMCellConfig& cfg);

    bool start()                                                      override;
    void stop()                                                       override;
    void tick()                                                       override;

    RNTI  assignChannel(uint8_t preferredSlot, GSMChannelType type)  override;
    bool  releaseChannel(RNTI rnti)                                  override;

    bool  enqueueDlData(RNTI rnti, ByteBuffer data)                  override;
    bool  dequeueUlData(RNTI rnti, ByteBuffer& data)                 override;

    void  broadcastSIMessage(uint8_t siType, const ByteBuffer& payload) override;

    size_t activeChannelCount() const override { return channels_.size(); }

private:
    std::shared_ptr<GSMPhy> phy_;
    GSMCellConfig cfg_;
    bool running_ = false;
    RNTI nextRnti_ = 1;
    uint8_t tickCount_ = 0;

    std::unordered_map<RNTI, GSMUEChannel> channels_;
    // Broadcast (System Information) queue
    std::queue<std::pair<uint8_t, ByteBuffer>> siQueue_;
    // Maps slot → RNTI (for uplink burst allocation)
    std::unordered_map<uint8_t, RNTI> slotMap_;

    void onRxBurst(const GSMBurst& burst);
    void processBroadcast();
    void processUplinkBursts();
    void scheduleDownlinkBursts();
    ByteBuffer buildSIType1() const;
    ByteBuffer buildSIType3() const;
};

}  // namespace rbs::gsm

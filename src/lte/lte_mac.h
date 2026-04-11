#pragma once
#include "../common/types.h"
#include "lte_phy.h"
#include "ilte_mac.h"
#include <unordered_map>
#include <queue>
#include <memory>
#include <vector>

namespace rbs::lte {

// ────────────────────────────────────────────────────────────────
// LTE MAC context per UE
// ────────────────────────────────────────────────────────────────
struct LTEMACContext {
    RNTI     rnti;
    uint8_t  bsr;           // Buffer Status Report (0-63)
    uint8_t  cqi;           // Channel Quality Indicator (1-15)
    uint8_t  harqProcessId; // Active HARQ process (0-7)
    uint8_t  harqRetx;      // Retransmission counter
    bool     srPending;     // Scheduling Request pending
    uint32_t cqiSubframeOffset;  ///< CQI report subframe offset (rnti % CQI_PERIOD)
    uint32_t srsSubframeOffset;  ///< SRS subframe offset (rnti % SRS_PERIOD)
    uint8_t  harqNackBitmap;     ///< Bitmask of HARQ processes with pending NACK
    std::queue<ByteBuffer> dlQueue;
    std::queue<ByteBuffer> ulQueue;
    // Carrier Aggregation (TS 36.321 §5.14)
    uint8_t  activeCCCount = 1;  ///< 1 = PCC only; 2-5 = CA configured
    uint8_t  caRbOffset    = 0;  ///< round-robin CC index for CA DL scheduling
};

// ────────────────────────────────────────────────────────────────
// LTE MAC Layer
// Implements: HARQ, CQI handling, proportional-fair scheduling,
// BSR / SR processing, and transport format selection.
// References: 3GPP TS 36.321
// ────────────────────────────────────────────────────────────────
class LTEMAC : public ILTEMAC {
public:
    explicit LTEMAC(std::shared_ptr<LTEPhy> phy, const LTECellConfig& cfg);

    bool start();
    void stop();
    void tick();   // Called every 1 ms subframe

    // ── UE management ─────────────────────────────────────────────
    bool  admitUE(RNTI rnti, uint8_t initialCQI = 7);
    bool  releaseUE(RNTI rnti);

    // ── Data plane ────────────────────────────────────────────────
    bool  enqueueDlSDU(RNTI rnti, ByteBuffer sdu);
    bool  dequeueUlSDU(RNTI rnti, ByteBuffer& sdu);

    // ── UE feedback ───────────────────────────────────────────────
    void  updateCQI(RNTI rnti, uint8_t cqi);
    void  updateBSR(RNTI rnti, uint8_t bsr);
    void  handleSchedulingRequest(RNTI rnti);

    size_t activeUECount() const { return ueContexts_.size(); }

    // Carrier Aggregation: configure secondary CCs for a UE (TS 36.321 §5.14)
    // ccCount: total component carriers including primary (1 = no CA, 2-5 = CA)
    bool   configureCA(RNTI rnti, uint8_t ccCount);
    uint8_t activeCCCount(RNTI rnti) const;

    // CQI → MCS lookup (simplified)
    static uint8_t cqiToMcs(uint8_t cqi);

private:
    std::shared_ptr<LTEPhy> phy_;
    LTECellConfig cfg_;
    bool  running_ = false;
    uint32_t sfn_  = 0;
    uint8_t  sfIdx_= 0;

    std::unordered_map<RNTI, LTEMACContext> ueContexts_;

    void onRxSubframe(const LTESubframe& sf);
    void runDlScheduler();
    void runDlSchedulerCA();    ///< CA DL: round-robin across active CCs (TS 36.321 §5.14)
    void runUlScheduler();
    void generateUlFeedback();                          ///< Build PUCCH+SRS and inject to PHY
    void processUlFeedback(const LTESubframe& sf);      ///< Apply PUCCH/SRS feedback to UE ctx

    // Proportional-fair metric
    double pfMetric(const LTEMACContext& ctx) const;
};

}  // namespace rbs::lte

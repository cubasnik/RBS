#include "lte_mac.h"
#include "../common/logger.h"
#include <algorithm>
#include <cmath>

namespace rbs::lte {

LTEMAC::LTEMAC(std::shared_ptr<LTEPhy> phy, const LTECellConfig& cfg)
    : phy_(std::move(phy)), cfg_(cfg) {}

// ────────────────────────────────────────────────────────────────
bool LTEMAC::start() {
    if (running_) return true;
    phy_->setRxCallback([this](const LTESubframe& sf) { onRxSubframe(sf); });
    running_ = true;
    RBS_LOG_INFO("LTEMAC", "Started – CellId=", cfg_.cellId,
                 " PCI=", cfg_.pci,
                 " RBs=", static_cast<int>(phy_->numResourceBlocks()));
    return true;
}

void LTEMAC::stop() {
    ueContexts_.clear();
    running_ = false;
    RBS_LOG_INFO("LTEMAC", "Stopped");
}

// ────────────────────────────────────────────────────────────────
void LTEMAC::tick() {
    if (!running_) return;
    runDlScheduler();
    runUlScheduler();
    ++sfIdx_;
    if (sfIdx_ >= LTE_SUBFRAMES_PER_FRAME) {
        sfIdx_ = 0;
        ++sfn_;
        if (sfn_ >= 1024) sfn_ = 0;
    }
}

// ────────────────────────────────────────────────────────────────
bool LTEMAC::admitUE(RNTI rnti, uint8_t initialCQI) {
    if (ueContexts_.count(rnti)) return false;
    LTEMACContext ctx{};
    ctx.rnti           = rnti;
    ctx.cqi            = initialCQI;
    ctx.bsr            = 0;
    ctx.harqProcessId  = 0;
    ctx.harqRetx       = 0;
    ctx.srPending      = false;
    ueContexts_[rnti]  = std::move(ctx);
    RBS_LOG_INFO("LTEMAC", "UE admitted RNTI=", rnti, " CQI=", static_cast<int>(initialCQI));
    return true;
}

bool LTEMAC::releaseUE(RNTI rnti) {
    auto it = ueContexts_.find(rnti);
    if (it == ueContexts_.end()) return false;
    ueContexts_.erase(it);
    RBS_LOG_INFO("LTEMAC", "UE released RNTI=", rnti);
    return true;
}

// ────────────────────────────────────────────────────────────────
bool LTEMAC::enqueueDlSDU(RNTI rnti, ByteBuffer sdu) {
    auto it = ueContexts_.find(rnti);
    if (it == ueContexts_.end()) return false;
    it->second.dlQueue.push(std::move(sdu));
    return true;
}

bool LTEMAC::dequeueUlSDU(RNTI rnti, ByteBuffer& sdu) {
    auto it = ueContexts_.find(rnti);
    if (it == ueContexts_.end() || it->second.ulQueue.empty()) return false;
    sdu = std::move(it->second.ulQueue.front());
    it->second.ulQueue.pop();
    return true;
}

// ────────────────────────────────────────────────────────────────
void LTEMAC::updateCQI(RNTI rnti, uint8_t cqi) {
    auto it = ueContexts_.find(rnti);
    if (it != ueContexts_.end()) {
        it->second.cqi = std::min(cqi, static_cast<uint8_t>(15));
        RBS_LOG_DEBUG("LTEMAC", "CQI update RNTI=", rnti, " CQI=", static_cast<int>(cqi));
    }
}

void LTEMAC::updateBSR(RNTI rnti, uint8_t bsr) {
    auto it = ueContexts_.find(rnti);
    if (it != ueContexts_.end()) it->second.bsr = bsr;
}

void LTEMAC::handleSchedulingRequest(RNTI rnti) {
    auto it = ueContexts_.find(rnti);
    if (it != ueContexts_.end()) it->second.srPending = true;
}

// ────────────────────────────────────────────────────────────────
// DL proportional-fair scheduler
// ────────────────────────────────────────────────────────────────
void LTEMAC::runDlScheduler() {
    if (ueContexts_.empty()) return;

    uint8_t totalRBs = phy_->numResourceBlocks();
    LTESubframe sf;
    sf.sfn           = sfn_;
    sf.subframeIndex = sfIdx_;

    // Sort UEs by PF metric (descending)
    std::vector<RNTI> sorted;
    sorted.reserve(ueContexts_.size());
    for (auto& [rnti, ctx] : ueContexts_) {
        if (!ctx.dlQueue.empty()) sorted.push_back(rnti);
    }
    std::sort(sorted.begin(), sorted.end(), [this](RNTI a, RNTI b) {
        return pfMetric(ueContexts_.at(a)) > pfMetric(ueContexts_.at(b));
    });

    uint8_t rbIdx = 0;
    for (RNTI rnti : sorted) {
        if (rbIdx >= totalRBs) break;
        auto& ctx = ueContexts_.at(rnti);
        ResourceBlock rb;
        rb.rnti    = rnti;
        rb.rbIndex = rbIdx++;
        rb.mcs     = cqiToMcs(ctx.cqi);
        sf.dlGrants.push_back(rb);
        // "Consume" one SDU
        if (!ctx.dlQueue.empty()) ctx.dlQueue.pop();
    }

    if (!sf.dlGrants.empty())
        phy_->transmitSubframe(sf);
}

// ────────────────────────────────────────────────────────────────
// UL grant scheduler
// ────────────────────────────────────────────────────────────────
void LTEMAC::runUlScheduler() {
    const uint8_t totalRBs = phy_->numResourceBlocks();
    LTESubframe sf;
    sf.sfn           = sfn_;
    sf.subframeIndex = sfIdx_;

    uint8_t rbIdx = 0;
    for (auto& [rnti, ctx] : ueContexts_) {
        if (rbIdx >= totalRBs) break;
        if (!ctx.srPending && ctx.bsr == 0) continue;
        ResourceBlock ulgrant;
        ulgrant.rnti    = rnti;
        ulgrant.rbIndex = rbIdx++;
        ulgrant.mcs     = cqiToMcs(ctx.cqi);
        sf.ulGrants.push_back(ulgrant);
        ctx.srPending = false;
        ctx.bsr       = 0;
    }

    if (sf.ulGrants.empty()) return;

    // Transmit DCI0 (UL grants) in PDCCH to UEs (TS 36.321 §5.4.1)
    phy_->transmitSubframe(sf);

    // Simulate immediate PUSCH reception: generate a Transport Block per
    // granted UE sized per TS 36.213 Table 7.1.7.2.1-1 (1 PRB, given MCS).
    // In a real eNB this would arrive 4 ms later after decoding the PUSCH.
    static const uint32_t kTbsTable[29] = {
         16, 24, 32, 40, 56, 72, 88,104,120,136,
        144,176,208,224,256,280,328,336,376,392,
        424,456,488,520,552,584,616,712,744
    };
    for (const auto& rb : sf.ulGrants) {
        auto it = ueContexts_.find(rb.rnti);
        if (it == ueContexts_.end()) continue;
        const uint32_t tbBytes = (rb.mcs < 29) ? kTbsTable[rb.mcs] : 744u;
        ByteBuffer ulData(tbBytes, 0);
        for (size_t i = 0; i < ulData.size(); ++i)
            ulData[i] = static_cast<uint8_t>((it->first ^ (i & 0xFF)) & 0xFF);
        it->second.ulQueue.push(std::move(ulData));
        RBS_LOG_DEBUG("LTEMAC", "UL grant RNTI=", rb.rnti,
                      " MCS=", static_cast<int>(rb.mcs),
                      " TB=", tbBytes, " bytes");
    }
}

// ────────────────────────────────────────────────────────────────
void LTEMAC::onRxSubframe(const LTESubframe& /*sf*/) {
    // UL Transport Blocks are injected by runUlScheduler() to simulate
    // the PUSCH path without DSP. A real eNB would decode PUSCH here.
}

// ────────────────────────────────────────────────────────────────
double LTEMAC::pfMetric(const LTEMACContext& ctx) const {
    // PF = instantaneous_rate / average_rate
    // Simplified: use CQI as proxy for instantaneous rate
    double inst = static_cast<double>(ctx.cqi);
    double avg  = 7.0;   // average CQI = 7, constant for simplicity
    return (avg > 0.0) ? inst / avg : 0.0;
}

// ────────────────────────────────────────────────────────────────
// CQI → MCS mapping (3GPP TS 36.213 Table 7.2.3-1 simplified)
// ────────────────────────────────────────────────────────────────
uint8_t LTEMAC::cqiToMcs(uint8_t cqi) {
    // CQI 1-6 → QPSK, 7-9 → 16QAM, 10-15 → 64QAM
    static const uint8_t table[16] = {
        0,   // CQI 0 (out of range)
        0,2,4,6,8,     // CQI 1-5: QPSK
        9,10,          // CQI 6-7: QPSK high rate
        12,14,         // CQI 8-9: 16QAM
        16,18,20,      // CQI 10-12: 64QAM low
        22,25,28       // CQI 13-15: 64QAM high
    };
    return (cqi < 16) ? table[cqi] : 28;
}

}  // namespace rbs::lte

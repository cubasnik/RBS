#include "nr_mac.h"
#include <algorithm>

namespace rbs::nr {

namespace {
constexpr std::array<uint8_t, 3> kSlicePercent = {50, 30, 20};

uint16_t scaledQuota(uint16_t totalPrbs, uint8_t pct) {
    return static_cast<uint16_t>((static_cast<uint32_t>(totalPrbs) * pct) / 100u);
}
}  // namespace

NRMac::NRMac(NRScs scs) : scs_(scs) {
    lastSliceMetrics_[0].slice = NRSlice::EMBB;
    lastSliceMetrics_[1].slice = NRSlice::URLLC;
    lastSliceMetrics_[2].slice = NRSlice::MMTC;
}

bool NRMac::addUE(RNTI crnti, uint8_t cqi, uint8_t bwpId) {
    if (crnti == 0 || cqi == 0) {
        return false;
    }
    const uint8_t initialBwp = (bwpId <= 1) ? bwpId : selectBwpForCqi(cqi, 0);
    ue_[crnti] = UeState{cqi, initialBwp, tick_, 0, 0, false, 0, 1, false, 0, NRSlice::EMBB};
    return true;
}

bool NRMac::updateUECqi(RNTI crnti, uint8_t cqi) {
    if (cqi == 0) {
        return false;
    }
    auto it = ue_.find(crnti);
    if (it == ue_.end()) {
        return false;
    }
    it->second.cqi = cqi;
    return true;
}

void NRMac::removeUE(RNTI crnti) {
    ue_.erase(crnti);
    qfiToDrb_.erase(crnti);
}

bool NRMac::setQfiMapping(RNTI crnti, uint8_t qfi, uint8_t drbId) {
    if (ue_.find(crnti) == ue_.end() || qfi == 0 || qfi > 63 || drbId == 0) {
        return false;
    }
    qfiToDrb_[crnti][qfi] = drbId;
    return true;
}

uint8_t NRMac::resolveDrb(RNTI crnti, uint8_t qfi) const {
    const auto ueIt = qfiToDrb_.find(crnti);
    if (ueIt == qfiToDrb_.end()) {
        return 0;
    }
    const auto mapIt = ueIt->second.find(qfi);
    return mapIt != ueIt->second.end() ? mapIt->second : 0;
}

uint8_t NRMac::currentBwp(RNTI crnti) const {
    const auto it = ue_.find(crnti);
    return it != ue_.end() ? it->second.bwpId : 0;
}

bool NRMac::enqueueDlBytes(RNTI crnti, uint32_t bytes) {
    auto it = ue_.find(crnti);
    if (it == ue_.end() || bytes == 0) {
        return false;
    }
    it->second.pendingBytes += bytes;
    return true;
}

uint32_t NRMac::pendingDlBytes(RNTI crnti) const {
    const auto it = ue_.find(crnti);
    return it != ue_.end() ? it->second.pendingBytes : 0;
}

bool NRMac::setUeSlice(RNTI crnti, NRSlice slice) {
    const auto it = ue_.find(crnti);
    if (it == ue_.end()) {
        return false;
    }
    it->second.slice = slice;
    return true;
}

NRSlice NRMac::ueSlice(RNTI crnti) const {
    const auto it = ue_.find(crnti);
    return it != ue_.end() ? it->second.slice : NRSlice::EMBB;
}

std::vector<NRSliceMetrics> NRMac::currentSliceMetrics() const {
    std::vector<NRSliceMetrics> out;
    out.reserve(lastSliceMetrics_.size());
    for (const auto& m : lastSliceMetrics_) {
        out.push_back(m);
    }
    return out;
}

bool NRMac::reportHarqFeedback(RNTI crnti, uint8_t harqId, bool ack) {
    auto it = ue_.find(crnti);
    if (it == ue_.end()) {
        return false;
    }

    const uint8_t currentHarqId = it->second.harqId;
    const uint8_t previousHarqId = static_cast<uint8_t>((currentHarqId + 15) % 16);
    if (harqId != currentHarqId && harqId != previousHarqId) {
        return false;
    }

    if (ack) {
        it->second.harqActive = false;
        it->second.waitingFeedback = false;
        it->second.feedbackDeadlineTick = 0;
        it->second.harqRv = 1;
        it->second.harqRetxCount = 0;
        // Advance HARQ process only when ACK refers to the current process.
        if (harqId == currentHarqId) {
            it->second.harqId = static_cast<uint8_t>((it->second.harqId + 1) % 16);
        }
    } else {
        ++it->second.harqRetxCount;
        ++it->second.totalRetxCount;
        if (it->second.harqRetxCount >= HARQ_MAX_RETX) {
            // Max retransmits exceeded: discard TB, reset process (TS 38.321).
            ++it->second.harqFailures;
            it->second.harqActive = false;
            it->second.waitingFeedback = false;
            it->second.feedbackDeadlineTick = 0;
            it->second.harqRetxCount = 0;
            it->second.harqRv = 1;
            it->second.harqId = static_cast<uint8_t>((it->second.harqId + 1) % 16);
        } else {
            it->second.harqActive = true;
            it->second.waitingFeedback = false;
            it->second.feedbackDeadlineTick = 0;
            if (harqId != currentHarqId) {
                it->second.harqId = harqId;
            }
            if (it->second.harqRv == 0) {
                it->second.harqRv = 1;
            }
        }
    }
    return true;
}

std::vector<NRScheduleGrant> NRMac::scheduleDl(uint16_t totalPrbs) {
    ++tick_;
    std::vector<NRScheduleGrant> grants;
    if (ue_.empty() || totalPrbs == 0) {
        return grants;
    }

    std::vector<std::pair<RNTI, UeState*>> ordered;
    ordered.reserve(ue_.size());
    for (auto& kv : ue_) {
        ordered.push_back({kv.first, &kv.second});
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        const bool aRetxReady = a.second->harqActive && !a.second->waitingFeedback;
        const bool bRetxReady = b.second->harqActive && !b.second->waitingFeedback;
        if (aRetxReady != bRetxReady) {
            return aRetxReady && !bRetxReady;
        }
        if (a.second->pendingBytes != b.second->pendingBytes) {
            return a.second->pendingBytes > b.second->pendingBytes;
        }
        if (a.second->cqi != b.second->cqi) {
            return a.second->cqi > b.second->cqi;
        }
        if (a.second->servedTicks != b.second->servedTicks) {
            return a.second->servedTicks < b.second->servedTicks;
        }
        return a.first < b.first;
    });

    uint16_t remaining = totalPrbs;
    const uint16_t perUeCap = std::max<uint16_t>(1, totalPrbs / static_cast<uint16_t>(ordered.size()));
    const bool anyQueued = std::any_of(ordered.begin(), ordered.end(), [](const auto& e) {
        return e.second->pendingBytes > 0;
    });

    std::array<uint16_t, 3> sliceBudget = {
        scaledQuota(totalPrbs, kSlicePercent[0]),
        scaledQuota(totalPrbs, kSlicePercent[1]),
        scaledQuota(totalPrbs, kSlicePercent[2])
    };
    const uint16_t allocated = static_cast<uint16_t>(sliceBudget[0] + sliceBudget[1] + sliceBudget[2]);
    if (allocated < totalPrbs) {
        sliceBudget[0] = static_cast<uint16_t>(sliceBudget[0] + (totalPrbs - allocated));
    }

    for (size_t i = 0; i < lastSliceMetrics_.size(); ++i) {
        lastSliceMetrics_[i].usedPrbs = 0;
        lastSliceMetrics_[i].activeUes = 0;
        lastSliceMetrics_[i].pendingBytes = 0;
    }
    for (const auto& entry : ordered) {
        const size_t idx = sliceIndex(entry.second->slice);
        ++lastSliceMetrics_[idx].activeUes;
        lastSliceMetrics_[idx].pendingBytes += entry.second->pendingBytes;
    }

    uint16_t redistributable = 0;
    uint8_t activeSliceCount = 0;
    for (size_t i = 0; i < lastSliceMetrics_.size(); ++i) {
        if (lastSliceMetrics_[i].activeUes == 0) {
            redistributable = static_cast<uint16_t>(redistributable + sliceBudget[i]);
            sliceBudget[i] = 0;
        } else {
            ++activeSliceCount;
        }
    }
    if (activeSliceCount > 0 && redistributable > 0) {
        const uint16_t perSliceBoost = static_cast<uint16_t>(redistributable / activeSliceCount);
        uint16_t rem = static_cast<uint16_t>(redistributable % activeSliceCount);
        for (size_t i = 0; i < lastSliceMetrics_.size(); ++i) {
            if (lastSliceMetrics_[i].activeUes == 0) {
                continue;
            }
            sliceBudget[i] = static_cast<uint16_t>(sliceBudget[i] + perSliceBoost);
            if (rem > 0) {
                sliceBudget[i] = static_cast<uint16_t>(sliceBudget[i] + 1);
                --rem;
            }
        }
    }

    for (size_t i = 0; i < lastSliceMetrics_.size(); ++i) {
        lastSliceMetrics_[i].maxPrbs = sliceBudget[i];
    }

    uint16_t bwpCursor0 = 0;
    uint16_t bwpCursor1 = 0;

    for (auto& entry : ordered) {
        if (remaining == 0) {
            break;
        }
        if (entry.second->harqActive && entry.second->waitingFeedback) {
            if (tick_ >= entry.second->feedbackDeadlineTick) {
                entry.second->waitingFeedback = false;
            } else {
                continue;
            }
        }

        if (anyQueued && entry.second->pendingBytes == 0 && !(entry.second->harqActive && !entry.second->waitingFeedback)) {
            continue;
        }

        const uint8_t selectedBwp = selectBwpForCqi(entry.second->cqi, entry.second->bwpId);
        if (selectedBwp != entry.second->bwpId) {
            entry.second->bwpId = selectedBwp;
            ++entry.second->bwpSwitches;
        }

        const size_t sidx = sliceIndex(entry.second->slice);
        if (sliceBudget[sidx] == 0) {
            continue;
        }

        uint16_t grantPrbs = std::min<uint16_t>(perUeCap, remaining);
        grantPrbs = std::min<uint16_t>(grantPrbs, sliceBudget[sidx]);
        grantPrbs = std::min<uint16_t>(grantPrbs, bwpPrbCap(entry.second->bwpId));
        if (entry.second->pendingBytes > 0) {
            const uint16_t queueNeedPrbs = static_cast<uint16_t>(std::max<uint32_t>(1, (entry.second->pendingBytes + 31) / 32));
            grantPrbs = std::min<uint16_t>(grantPrbs, queueNeedPrbs);
        }

        if (grantPrbs == 0) {
            continue;
        }

        const uint16_t bwpSize = bwpSizePrb(entry.second->bwpId);
        uint16_t& bwpCursor = (entry.second->bwpId == 0) ? bwpCursor0 : bwpCursor1;
        if (bwpCursor >= bwpSize) {
            continue;
        }
        const uint16_t bwpAvailable = static_cast<uint16_t>(bwpSize - bwpCursor);
        grantPrbs = std::min<uint16_t>(grantPrbs, bwpAvailable);
        if (grantPrbs == 0) {
            continue;
        }

        const uint16_t localRbStart = bwpCursor;
        const uint16_t absoluteRbStart = static_cast<uint16_t>(bwpStartPrb(entry.second->bwpId) + localRbStart);
        bwpCursor = static_cast<uint16_t>(bwpCursor + grantPrbs);

        NRScheduleGrant g{};
        g.crnti = entry.first;
        g.mcs = cqiToMcs(entry.second->cqi);
        g.prbs = grantPrbs;
        g.bwpId = entry.second->bwpId;
        g.dci.crnti = g.crnti;
        g.dci.bwpId = g.bwpId;
        g.dci.bwpStartPrb = bwpStartPrb(g.bwpId);
        g.dci.bwpSizePrb = bwpSizePrb(g.bwpId);
        g.dci.mcs = g.mcs;
        g.dci.rbStart = absoluteRbStart;
        g.dci.rbLen = g.prbs;
        g.dci.fdaType1Riv = encodeType1Riv(g.dci.bwpSizePrb, localRbStart, g.dci.rbLen);
        g.dci.timeDomainAlloc = 2;
        g.dci.dmrsAdditionalPos = 1;
        g.dci.harqId = entry.second->harqId;
        if (!entry.second->harqActive) {
            g.dci.ndi = true;
            g.dci.rv = 0;
            entry.second->harqActive = true;
            entry.second->harqRv = 1;
            entry.second->waitingFeedback = true;
            entry.second->feedbackDeadlineTick = tick_ + HARQ_RTT_TICKS;
        } else {
            g.dci.ndi = false;
            g.dci.rv = entry.second->harqRv;
            entry.second->harqRv = static_cast<uint8_t>((entry.second->harqRv % 3) + 1);
            entry.second->waitingFeedback = true;
            entry.second->feedbackDeadlineTick = tick_ + HARQ_RTT_TICKS;
        }
        // TBS scaled by Rank Indicator: RI=2 (2-layer MIMO) doubles throughput.
        g.dci.tbsBytes = static_cast<uint16_t>(g.prbs * (g.mcs + 1) * entry.second->ri);
        grants.push_back(g);
        remaining = static_cast<uint16_t>(remaining - grantPrbs);
        sliceBudget[sidx] = static_cast<uint16_t>(sliceBudget[sidx] - grantPrbs);
        lastSliceMetrics_[sidx].usedPrbs = static_cast<uint16_t>(lastSliceMetrics_[sidx].usedPrbs + grantPrbs);
        if (entry.second->pendingBytes > 0) {
            const uint32_t consumed = std::min<uint32_t>(entry.second->pendingBytes, g.dci.tbsBytes);
            entry.second->pendingBytes -= consumed;
        }
        entry.second->servedTicks = tick_;
    }

    return grants;
}

bool NRMac::reportCsiRi(RNTI crnti, uint8_t ri) {
    if (ri == 0 || ri > 4) {
        return false;
    }
    auto it = ue_.find(crnti);
    if (it == ue_.end()) {
        return false;
    }
    it->second.ri = ri;
    return true;
}

bool NRMac::reportCsi(RNTI crnti, const CsiReport& csi) {
    if (csi.cqi == 0 || csi.ri == 0 || csi.ri > 4) {
        return false;
    }
    auto it = ue_.find(crnti);
    if (it == ue_.end()) {
        return false;
    }
    it->second.cqi = csi.cqi;
    it->second.ri  = csi.ri;
    return true;
}

HarqStats NRMac::getHarqStats(RNTI crnti) const {
    const auto it = ue_.find(crnti);
    if (it == ue_.end()) {
        return {};
    }
    return {it->second.totalRetxCount, it->second.harqFailures};
}

std::vector<NRDci11> NRMac::buildDci11(const std::vector<NRScheduleGrant>& grants) const {
    std::vector<NRDci11> out;
    out.reserve(grants.size());
    for (const auto& g : grants) {
        out.push_back(g.dci);
    }
    return out;
}

uint8_t NRMac::slotsPerMs() const {
    switch (scs_) {
        case NRScs::SCS15: return 1;
        case NRScs::SCS30: return 2;
        case NRScs::SCS60: return 4;
        case NRScs::SCS120: return 8;
        default: return 1;
    }
}

uint8_t NRMac::cqiToMcs(uint8_t cqi) {
    if (cqi <= 2) {
        return 0;
    }
    if (cqi >= 15) {
        return 27;
    }
    return static_cast<uint8_t>((cqi - 1) * 2);
}

uint8_t NRMac::selectBwpForCqi(uint8_t cqi, uint8_t currentBwp) {
    // Hysteresis policy: switch up only at high CQI and switch down only at low CQI.
    if (currentBwp == 0) {
        return (cqi >= BWP_UP_CQI) ? 1 : 0;
    }
    return (cqi <= BWP_DOWN_CQI) ? 0 : 1;
}

uint16_t NRMac::bwpPrbCap(uint8_t bwpId) {
    return bwpId == 0 ? BWP0_MAX_PRBS : BWP1_MAX_PRBS;
}

uint16_t NRMac::bwpStartPrb(uint8_t bwpId) {
    return bwpId == 0 ? BWP0_START_PRB : BWP1_START_PRB;
}

uint16_t NRMac::bwpSizePrb(uint8_t bwpId) {
    return bwpId == 0 ? BWP0_SIZE_PRB : BWP1_SIZE_PRB;
}

uint16_t NRMac::encodeType1Riv(uint16_t nRbBwp, uint16_t rbStart, uint16_t rbLen) {
    if (nRbBwp == 0 || rbLen == 0 || rbLen > nRbBwp || rbStart >= nRbBwp) {
        return 0;
    }
    if (rbLen - 1 <= nRbBwp / 2) {
        return static_cast<uint16_t>(nRbBwp * (rbLen - 1) + rbStart);
    }
    return static_cast<uint16_t>(nRbBwp * (nRbBwp - rbLen + 1) + (nRbBwp - 1 - rbStart));
}

const char* NRMac::sliceName(NRSlice slice) {
    switch (slice) {
        case NRSlice::EMBB: return "eMBB";
        case NRSlice::URLLC: return "URLLC";
        case NRSlice::MMTC: return "mMTC";
        default: return "unknown";
    }
}

size_t NRMac::sliceIndex(NRSlice slice) {
    switch (slice) {
        case NRSlice::EMBB: return 0;
        case NRSlice::URLLC: return 1;
        case NRSlice::MMTC: return 2;
        default: return 0;
    }
}

}  // namespace rbs::nr

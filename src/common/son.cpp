#include "son.h"
#include "logger.h"
#include <algorithm>

namespace rbs {

// ── ANR ───────────────────────────────────────────────────────────────────────

void SON::addNeighbour(CellId servingCell, const NeighbourCell& nb) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto& list = ncl_[servingCell];
    // Overwrite existing entry with same cellId
    for (auto& entry : list) {
        if (entry.cellId == nb.cellId) {
            entry = nb;
            return;
        }
    }
    list.push_back(nb);
    RBS_LOG_DEBUG("SON/ANR",
                  "Cell ", servingCell, ": added neighbour ", nb.cellId,
                  " (", ratToString(nb.rat), ")",
                  " RSRP=", nb.rsrp_dBm, " dBm");
}

void SON::removeNeighbour(CellId servingCell, CellId nbCellId) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = ncl_.find(servingCell);
    if (it == ncl_.end()) return;
    auto& list = it->second;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [nbCellId](const NeighbourCell& e) {
                                  return e.cellId == nbCellId;
                              }),
               list.end());
}

void SON::reportMeasurement(CellId servingCell, IMSI imsi,
                            const NeighbourCell& meas,
                            float minRsrp_dBm) {
    if (meas.rsrp_dBm < minRsrp_dBm) return;

    std::lock_guard<std::mutex> lk(mutex_);
    auto& list = ncl_[servingCell];
    // Check if already present
    for (auto& entry : list) {
        if (entry.cellId == meas.cellId) {
            // Update RSRP / RSRQ with the latest measurement
            entry.rsrp_dBm = meas.rsrp_dBm;
            entry.rsrq_dB  = meas.rsrq_dB;
            return;
        }
    }
    // Auto-discover: add to NCL
    list.push_back(meas);
    RBS_LOG_INFO("SON/ANR",
                 "Cell ", servingCell, ": auto-discovered neighbour ",
                 meas.cellId, " (", ratToString(meas.rat), ")",
                 " via IMSI=", imsi, " RSRP=", meas.rsrp_dBm, " dBm");
}

std::vector<NeighbourCell> SON::getNeighbours(CellId servingCell) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = ncl_.find(servingCell);
    if (it == ncl_.end()) return {};
    return it->second;
}

size_t SON::neighbourCount(CellId servingCell) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = ncl_.find(servingCell);
    if (it == ncl_.end()) return 0;
    return it->second.size();
}

// ── MLB ───────────────────────────────────────────────────────────────────────

void SON::updateCellLoad(CellId cell, uint8_t dlPrbPct, uint8_t ulPrbPct) {
    std::lock_guard<std::mutex> lk(mutex_);
    loads_[cell] = CellLoad{cell, dlPrbPct, ulPrbPct};
    RBS_LOG_DEBUG("SON/MLB",
                  "Cell ", cell, " load DL=", dlPrbPct, "% UL=", ulPrbPct, "%");
}

std::optional<CellLoad> SON::getCellLoad(CellId cell) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = loads_.find(cell);
    if (it == loads_.end()) return std::nullopt;
    return it->second;
}

bool SON::isOverloaded(CellId cell, uint8_t threshold) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = loads_.find(cell);
    if (it == loads_.end()) return false;
    return it->second.dlPrbPct >= threshold;
}

std::optional<CellId> SON::getBestNeighbourForOffload(CellId servingCell) const {
    std::lock_guard<std::mutex> lk(mutex_);

    auto nclIt = ncl_.find(servingCell);
    if (nclIt == ncl_.end() || nclIt->second.empty()) return std::nullopt;

    CellId   bestCell  = 0;
    uint8_t  bestLoad  = 255;
    bool     found     = false;

    for (const auto& nb : nclIt->second) {
        auto loadIt = loads_.find(nb.cellId);
        if (loadIt == loads_.end()) continue;
        if (!found || loadIt->second.dlPrbPct < bestLoad) {
            bestLoad = loadIt->second.dlPrbPct;
            bestCell = nb.cellId;
            found    = true;
        }
    }
    if (!found) return std::nullopt;
    RBS_LOG_INFO("SON/MLB",
                 "Offload candidate for cell ", servingCell,
                 " → cell ", bestCell, " (DL=", bestLoad, "%)");
    return bestCell;
}

// ── MRO ───────────────────────────────────────────────────────────────────────

void SON::reportHOSuccess(CellId srcCell, CellId dstCell) {
    std::lock_guard<std::mutex> lk(mutex_);
    ++hoStats_[makeHOKey(srcCell, dstCell)].successes;
}

void SON::reportHOFailure(CellId srcCell, CellId dstCell) {
    std::lock_guard<std::mutex> lk(mutex_);
    ++hoStats_[makeHOKey(srcCell, dstCell)].failures;
}

HOStats SON::getHOStats(CellId srcCell, CellId dstCell) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = hoStats_.find(makeHOKey(srcCell, dstCell));
    if (it == hoStats_.end()) return HOStats{};
    return it->second;
}

float SON::getHOSuccessRate(CellId srcCell, CellId dstCell) const {
    return getHOStats(srcCell, dstCell).successRate();
}

int8_t SON::suggestA3OffsetDelta(CellId srcCell, CellId dstCell) const {
    const float rate = getHOSuccessRate(srcCell, dstCell);
    if (rate < 0.80f) {
        RBS_LOG_INFO("SON/MRO",
                     "Cell ", srcCell, "→", dstCell,
                     " HO SR=", rate, " < 0.80 → A3 offset -2 dB");
        return -2;
    }
    if (rate > 0.95f) {
        RBS_LOG_INFO("SON/MRO",
                     "Cell ", srcCell, "→", dstCell,
                     " HO SR=", rate, " > 0.95 → A3 offset +2 dB");
        return +2;
    }
    return 0;
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void SON::reset() {
    std::lock_guard<std::mutex> lk(mutex_);
    ncl_.clear();
    loads_.clear();
    hoStats_.clear();
}

}  // namespace rbs

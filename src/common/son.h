#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Self-Organizing Network (SON) Manager
//
// Implements the three core SON features mandated by 3GPP TS 36.902:
//
//   ANR — Automatic Neighbour Relations
//   MLB — Mobility Load Balancing
//   MRO — Mobility Robustness Optimisation
//
// The class is stack-agnostic: callers feed it measurements and load reports;
// it returns recommendations that the caller applies via X2AP / S1AP.
// ─────────────────────────────────────────────────────────────────────────────
#include "types.h"
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rbs {

// ── Data types ────────────────────────────────────────────────────────────────

// A neighbour cell entry as held in the Neighbour Cell List (NCL).
struct NeighbourCell {
    CellId cellId;
    RAT    rat;
    float  rsrp_dBm;  // Reference Signal Received Power [dBm]
    float  rsrq_dB;   // Reference Signal Received Quality [dB]
};

// Snapshot of PRB utilisation for a cell.
struct CellLoad {
    CellId  cellId;
    uint8_t dlPrbPct;  // DL PRB occupancy 0-100 [%]
    uint8_t ulPrbPct;  // UL PRB occupancy 0-100 [%]
};

// Per (src, dst) cell-pair handover outcome counters.
struct HOStats {
    uint32_t successes{0};
    uint32_t failures{0};

    float successRate() const noexcept {
        const uint32_t total = successes + failures;
        return total == 0u ? 1.0f
                           : static_cast<float>(successes) / static_cast<float>(total);
    }
};

// ── SON ───────────────────────────────────────────────────────────────────────

class SON {
public:
    // ── ANR: Automatic Neighbour Relations ────────────────────────────────────

    // Statically provision a neighbour.  Overwrites an existing entry with
    // the same (servingCell, nbCellId) if present.
    void addNeighbour(CellId servingCell, const NeighbourCell& nb);

    // Remove a neighbour (silent no-op if not present).
    void removeNeighbour(CellId servingCell, CellId nbCellId);

    // Process a UE measurement report.
    // If the measured cell is not yet in the NCL *and* its RSRP >= minRsrp_dBm,
    // it is automatically added (3GPP TS 36.902 §5.2).
    // Default threshold: -110 dBm.
    void reportMeasurement(CellId servingCell, IMSI imsi,
                           const NeighbourCell& meas,
                           float minRsrp_dBm = -110.0f);

    // Return a copy of the Neighbour Cell List for a serving cell.
    // Returns an empty vector if the cell has no NCL entry.
    std::vector<NeighbourCell> getNeighbours(CellId servingCell) const;

    // Number of distinct neighbours known for a serving cell.
    size_t neighbourCount(CellId servingCell) const;

    // ── MLB: Mobility Load Balancing ─────────────────────────────────────────

    // Update (or insert) the PRB occupancy for a cell.
    void updateCellLoad(CellId cell, uint8_t dlPrbPct, uint8_t ulPrbPct);

    // Retrieve last known load, or nullopt if no report has been received yet.
    std::optional<CellLoad> getCellLoad(CellId cell) const;

    // True if dlPrbPct >= threshold (default 80 %).
    bool isOverloaded(CellId cell, uint8_t threshold = 80) const;

    // Among all neighbours of servingCell for which a load report is known,
    // return the CellId of the one with the lowest dlPrbPct.
    // Returns nullopt when no neighbour with load data is found.
    std::optional<CellId> getBestNeighbourForOffload(CellId servingCell) const;

    // ── MRO: Mobility Robustness Optimisation ────────────────────────────────

    // Record a successful handover from srcCell to dstCell.
    void reportHOSuccess(CellId srcCell, CellId dstCell);

    // Record a failed handover from srcCell to dstCell.
    void reportHOFailure(CellId srcCell, CellId dstCell);

    // Return accumulated HO statistics for a (src, dst) cell pair.
    HOStats getHOStats(CellId srcCell, CellId dstCell) const;

    // Convenience wrapper.
    float getHOSuccessRate(CellId srcCell, CellId dstCell) const;

    // Recommend an A3-offset delta (dB) based on recent HO performance:
    //   successRate < 0.80  → return -2  (tighten trigger → fewer early HOs)
    //   successRate > 0.95  → return +2  (relax trigger  → more proactive HO)
    //   otherwise           → return  0  (no change)
    // References: 3GPP TS 36.331 §6.3.5 (A3-Offset), TS 36.902 §5.4.
    int8_t suggestA3OffsetDelta(CellId srcCell, CellId dstCell) const;

    // ── Housekeeping ─────────────────────────────────────────────────────────

    // Clear all state (ANR tables, load reports, HO stats).
    void reset();

private:
    mutable std::mutex mutex_;

    // ANR: servingCellId → neighbour list (keyed internally by cellId)
    std::unordered_map<CellId, std::vector<NeighbourCell>> ncl_;

    // MLB: cellId → latest load snapshot
    std::unordered_map<CellId, CellLoad> loads_;

    // MRO: 64-bit key (srcCell << 32 | dstCell) → HOStats
    using HOKey = uint64_t;
    std::unordered_map<HOKey, HOStats> hoStats_;

    static constexpr HOKey makeHOKey(CellId src, CellId dst) noexcept {
        return (static_cast<HOKey>(src) << 32u) | static_cast<HOKey>(dst);
    }
};

}  // namespace rbs

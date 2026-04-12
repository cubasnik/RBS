#pragma once
#include "logger.h"
#include <string>
#include <deque>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <chrono>

namespace rbs {

// ─────────────────────────────────────────────────────────────────────────────
// LinkMsg — one entry in the per-link message trace.
// ─────────────────────────────────────────────────────────────────────────────
struct LinkMsg {
    bool        tx;        ///< true = sent (BTS→BSC/NB→RNC/eNB→MME), false = received
    std::string type;      ///< e.g. "OML:OPSTART", "NBAP:CELL_SETUP", "S1AP:S1_SETUP"
    std::string summary;   ///< human-readable parameters
    std::chrono::system_clock::time_point timestamp;
};

// ─────────────────────────────────────────────────────────────────────────────
// LinkController — mixin base class for Abis/Iub/S1 link objects.
//
// Provides:
//   • Ring-buffer trace (last LINK_TRACE_MAX messages)
//   • Per-type blocking filter (drops send/recv of blocked types)
// ─────────────────────────────────────────────────────────────────────────────
class LinkController {
public:
    static constexpr size_t LINK_TRACE_MAX = 100;

    explicit LinkController(std::string name) : name_(std::move(name)) {}
    virtual ~LinkController() = default;

    const std::string& linkName() const { return name_; }

    // ── Trace ─────────────────────────────────────────────────────────────────
    void pushTrace(bool tx, std::string type, std::string summary) {
        std::lock_guard<std::mutex> lk(traceMtx_);
        if (traces_.size() >= LINK_TRACE_MAX) traces_.pop_front();
        traces_.push_back({tx, std::move(type), std::move(summary),
                           std::chrono::system_clock::now()});
    }

    /// Returns last `limit` messages (or all if fewer available).
    std::vector<LinkMsg> getTrace(size_t limit = 50) const {
        std::lock_guard<std::mutex> lk(traceMtx_);
        size_t start = (traces_.size() > limit)
                       ? (traces_.size() - limit)
                       : 0;
        return std::vector<LinkMsg>(
            traces_.begin() + static_cast<std::ptrdiff_t>(start),
            traces_.end());
    }

    void clearTrace() {
        std::lock_guard<std::mutex> lk(traceMtx_);
        traces_.clear();
    }

    // ── Block filter ──────────────────────────────────────────────────────────
    void blockMsg(const std::string& type) {
        std::lock_guard<std::mutex> lk(blockMtx_);
        blocked_.insert(type);
        RBS_LOG_WARNING("LinkCtrl", "{} blocked: {}", name_, type);
    }

    void unblockMsg(const std::string& type) {
        std::lock_guard<std::mutex> lk(blockMtx_);
        blocked_.erase(type);
        RBS_LOG_INFO("LinkCtrl", "{} unblocked: {}", name_, type);
    }

    bool isBlocked(const std::string& type) const {
        std::lock_guard<std::mutex> lk(blockMtx_);
        return blocked_.count(type) > 0;
    }

    std::vector<std::string> blockedTypes() const {
        std::lock_guard<std::mutex> lk(blockMtx_);
        return std::vector<std::string>(blocked_.begin(), blocked_.end());
    }

private:
    std::string name_;
    mutable std::mutex traceMtx_;
    std::deque<LinkMsg> traces_;
    mutable std::mutex blockMtx_;
    std::unordered_set<std::string> blocked_;
};

} // namespace rbs

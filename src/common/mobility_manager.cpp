#include "mobility_manager.h"
#include "logger.h"

namespace rbs {

void MobilityManager::registerUE(IMSI imsi, RAT rat, RNTI rnti, CellId cellId) {
    std::lock_guard<std::mutex> lk(mutex_);
    ues_[imsi] = UELocation{rat, rnti, cellId};
    RBS_LOG_DEBUG("Mobility",
            "Registered IMSI=", imsi,
            " RAT=", ratToString(rat),
            " RNTI=", rnti,
            " cell=", cellId);
}

void MobilityManager::unregisterUE(IMSI imsi) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = ues_.find(imsi);
    if (it == ues_.end()) return;
    RBS_LOG_DEBUG("Mobility",
            "Unregistered IMSI=", imsi,
            " RAT=", ratToString(it->second.rat));
    ues_.erase(it);
}

std::optional<UELocation> MobilityManager::getUELocation(IMSI imsi) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = ues_.find(imsi);
    if (it == ues_.end()) return std::nullopt;
    return it->second;
}

size_t MobilityManager::registeredUECount() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return ues_.size();
}

bool MobilityManager::triggerHandover(IMSI imsi, RAT targetRat, CellId targetCellId,
                                      const HandoverCallback& cb) {
    // Validate that the UE is known and not already on the target RAT.
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = ues_.find(imsi);
        if (it == ues_.end()) {
            RBS_LOG_ERROR("Mobility",
                    "triggerHandover: IMSI=", imsi, " not registered");
            return false;
        }
        if (it->second.rat == targetRat) {
            RBS_LOG_DEBUG("Mobility",
                    "triggerHandover: IMSI=", imsi,
                    " already on ", ratToString(targetRat));
            return false;
        }
    }

    // Execute the callback outside the lock so the caller can call back into
    // registerUE / releaseUE without deadlocking.
    const RNTI newRnti = cb(imsi, targetRat);
    if (newRnti == 0) {
        RBS_LOG_ERROR("Mobility",
                "triggerHandover: HO callback returned 0 for IMSI=", imsi);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = ues_.find(imsi);
        if (it == ues_.end()) {
            // UE was unregistered during the callback — treat as failure.
            RBS_LOG_ERROR("Mobility",
                    "triggerHandover: IMSI=", imsi,
                    " disappeared during HO callback");
            return false;
        }
        const RAT srcRat = it->second.rat;
        it->second = UELocation{targetRat, newRnti, targetCellId};
        ++hoCount_;
        RBS_LOG_INFO("Mobility",
                 "Inter-RAT HO complete IMSI=", imsi,
                 " ", ratToString(srcRat),
                 " -> ", ratToString(targetRat),
                 " newRNTI=", newRnti,
                 " (total HOs=", hoCount_, ")");
    }
    return true;
}

uint64_t MobilityManager::handoverCount() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return hoCount_;
}

void MobilityManager::reset() {
    std::lock_guard<std::mutex> lk(mutex_);
    ues_.clear();
    hoCount_ = 0;
}

} // namespace rbs

#include "config_snapshot.h"

namespace rbs {

ConfigSnapshotManager::ConfigSnapshotManager(size_t maxSnapshots)
    : maxSnapshots_(maxSnapshots) {}

uint64_t ConfigSnapshotManager::saveSnapshot(const std::string& rawConfig,
                                             const std::string& source,
                                             const std::string& reason) {
    std::lock_guard<std::mutex> lk(mtx_);

    const uint64_t id = nextVersionId_++;
    snapshots_.push_back(ConfigSnapshot{
        id,
        std::chrono::system_clock::now(),
        source,
        reason,
        rawConfig
    });

    if (snapshots_.size() > maxSnapshots_) {
        snapshots_.erase(snapshots_.begin(),
                         snapshots_.begin() + static_cast<long long>(snapshots_.size() - maxSnapshots_));
    }
    return id;
}

std::vector<ConfigSnapshot> ConfigSnapshotManager::listSnapshots() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return snapshots_;
}

std::optional<std::string> ConfigSnapshotManager::rollbackTo(uint64_t versionId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it) {
        if (it->versionId == versionId) {
            return it->rawConfig;
        }
    }
    return std::nullopt;
}

uint64_t ConfigSnapshotManager::currentVersion() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (snapshots_.empty()) {
        return 0;
    }
    return snapshots_.back().versionId;
}

void ConfigSnapshotManager::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    snapshots_.clear();
}

}  // namespace rbs

#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace rbs {

struct ConfigSnapshot {
    uint64_t versionId = 0;
    std::chrono::system_clock::time_point timestamp;
    std::string source;
    std::string reason;
    std::string rawConfig;
};

class ConfigSnapshotManager {
public:
    explicit ConfigSnapshotManager(size_t maxSnapshots = 5);

    uint64_t saveSnapshot(const std::string& rawConfig,
                          const std::string& source,
                          const std::string& reason);

    std::vector<ConfigSnapshot> listSnapshots() const;

    // Returns raw config payload if version is available.
    std::optional<std::string> rollbackTo(uint64_t versionId) const;

    uint64_t currentVersion() const;
    void clear();

private:
    size_t maxSnapshots_;
    uint64_t nextVersionId_ = 1;
    std::vector<ConfigSnapshot> snapshots_;
    mutable std::mutex mtx_;
};

}  // namespace rbs

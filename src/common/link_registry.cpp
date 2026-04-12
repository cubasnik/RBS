#include "link_registry.h"

namespace rbs {

void LinkRegistry::registerLink(LinkEntry entry) {
    std::lock_guard<std::mutex> lk(mtx_);
    links_[entry.name] = std::move(entry);
}

void LinkRegistry::unregisterLink(const std::string& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    links_.erase(name);
}

const LinkEntry* LinkRegistry::getLink(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = links_.find(name);
    return (it != links_.end()) ? &it->second : nullptr;
}

LinkEntry* LinkRegistry::getLink(const std::string& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = links_.find(name);
    return (it != links_.end()) ? &it->second : nullptr;
}

std::vector<const LinkEntry*> LinkRegistry::allLinks() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<const LinkEntry*> result;
    result.reserve(links_.size());
    for (const auto& [key, entry] : links_)
        result.push_back(&entry);
    return result;
}

} // namespace rbs

#include "lte_service_registry.h"

namespace rbs {

void LteServiceRegistry::registerCell(LteCellService svc) {
    std::lock_guard<std::mutex> lk(mtx_);
    cells_[svc.cellId] = std::move(svc);
}

void LteServiceRegistry::unregisterCell(CellId cellId) {
    std::lock_guard<std::mutex> lk(mtx_);
    cells_.erase(cellId);
}

std::optional<LteCellService> LteServiceRegistry::getCell(CellId cellId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = cells_.find(cellId);
    if (it == cells_.end()) return std::nullopt;
    return it->second;
}

std::vector<LteCellService> LteServiceRegistry::allCells() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<LteCellService> out;
    out.reserve(cells_.size());
    for (const auto& [_, svc] : cells_) out.push_back(svc);
    return out;
}

}  // namespace rbs

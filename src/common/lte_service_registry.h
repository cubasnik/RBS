#pragma once

#include "types.h"

#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rbs {

struct LteCellService {
    CellId  cellId = 0;
    EARFCN  earfcn = 0;
    uint16_t pci = 0;

    std::function<RNTI(IMSI, uint8_t)>               admitUe;
    std::function<void(RNTI)>                        releaseUe;
    std::function<bool(RNTI)>                        setupVoLteBearer;
    std::function<bool(RNTI, const std::string&)>    handleSipMessage;
    std::function<size_t(RNTI, size_t, size_t)>      sendVoLteRtpBurst;
    std::function<bool(RNTI, uint16_t, EARFCN)>      requestHandover;
    std::function<size_t()>                           connectedUeCount;
};

class LteServiceRegistry {
public:
    static LteServiceRegistry& instance() {
        static LteServiceRegistry inst;
        return inst;
    }

    void registerCell(LteCellService svc);
    void unregisterCell(CellId cellId);
    std::optional<LteCellService> getCell(CellId cellId) const;
    std::vector<LteCellService> allCells() const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<CellId, LteCellService> cells_;
};

}  // namespace rbs

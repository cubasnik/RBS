#pragma once

#include "../common/types.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rbs::nr {

struct NRDci11 {
    RNTI     crnti = 0;
    uint8_t  bwpId = 0;
    uint16_t bwpStartPrb = 0;
    uint16_t bwpSizePrb = 0;
    uint8_t  mcs = 0;
    uint16_t rbStart = 0;
    uint16_t rbLen = 0;
    uint16_t fdaType1Riv = 0;
    uint8_t  timeDomainAlloc = 0;
    uint8_t  dmrsAdditionalPos = 0;
    uint8_t  harqId = 0;
    bool     ndi = true;
    uint8_t  rv = 0;
    uint16_t tbsBytes = 0;
};

struct NRScheduleGrant {
    RNTI    crnti = 0;
    uint8_t mcs = 0;
    uint16_t prbs = 0;
    uint8_t bwpId = 0;
    NRDci11 dci{};
};

enum class NRSlice : uint8_t {
    EMBB = 0,
    URLLC = 1,
    MMTC = 2,
};

struct NRSliceMetrics {
    NRSlice slice = NRSlice::EMBB;
    uint16_t maxPrbs = 0;
    uint16_t usedPrbs = 0;
    uint32_t activeUes = 0;
    uint32_t pendingBytes = 0;
};

// Minimal NR MAC scheduler model for simulation tests.
class NRMac {
public:
    explicit NRMac(NRScs scs);

    bool addUE(RNTI crnti, uint8_t cqi, uint8_t bwpId = 0);
    bool updateUECqi(RNTI crnti, uint8_t cqi);
    void removeUE(RNTI crnti);

    bool setQfiMapping(RNTI crnti, uint8_t qfi, uint8_t drbId);
    uint8_t resolveDrb(RNTI crnti, uint8_t qfi) const;
    uint8_t currentBwp(RNTI crnti) const;
    bool enqueueDlBytes(RNTI crnti, uint32_t bytes);
    uint32_t pendingDlBytes(RNTI crnti) const;
    bool reportHarqFeedback(RNTI crnti, uint8_t harqId, bool ack);

    bool setUeSlice(RNTI crnti, NRSlice slice);
    NRSlice ueSlice(RNTI crnti) const;
    std::vector<NRSliceMetrics> currentSliceMetrics() const;

    std::vector<NRScheduleGrant> scheduleDl(uint16_t totalPrbs);
    std::vector<NRDci11> buildDci11(const std::vector<NRScheduleGrant>& grants) const;

    uint8_t slotsPerMs() const;

    static const char* sliceName(NRSlice slice);

private:
    struct UeState {
        uint8_t cqi = 9;
        uint8_t bwpId = 0;
        uint64_t servedTicks = 0;
        uint32_t bwpSwitches = 0;
        uint32_t pendingBytes = 0;
        bool harqActive = false;
        uint8_t harqId = 0;
        uint8_t harqRv = 1;
        bool waitingFeedback = false;
        uint64_t feedbackDeadlineTick = 0;
        NRSlice slice = NRSlice::EMBB;
    };

    std::unordered_map<RNTI, UeState> ue_;
    std::unordered_map<RNTI, std::unordered_map<uint8_t, uint8_t>> qfiToDrb_;
    NRScs scs_;
    uint64_t tick_ = 0;
    std::array<NRSliceMetrics, 3> lastSliceMetrics_{};
    static constexpr uint8_t BWP_UP_CQI = 11;
    static constexpr uint8_t BWP_DOWN_CQI = 8;
    static constexpr uint16_t BWP0_MAX_PRBS = 10;
    static constexpr uint16_t BWP1_MAX_PRBS = 30;
    static constexpr uint8_t HARQ_RTT_TICKS = 4;
    static constexpr uint16_t BWP0_START_PRB = 0;
    static constexpr uint16_t BWP0_SIZE_PRB = 24;
    static constexpr uint16_t BWP1_START_PRB = 24;
    static constexpr uint16_t BWP1_SIZE_PRB = 48;

    static uint8_t cqiToMcs(uint8_t cqi);
    static uint8_t selectBwpForCqi(uint8_t cqi, uint8_t currentBwp);
    static uint16_t bwpPrbCap(uint8_t bwpId);
    static uint16_t bwpStartPrb(uint8_t bwpId);
    static uint16_t bwpSizePrb(uint8_t bwpId);
    static uint16_t encodeType1Riv(uint16_t nRbBwp, uint16_t rbStart, uint16_t rbLen);
    static size_t sliceIndex(NRSlice slice);
};

}  // namespace rbs::nr

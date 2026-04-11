// NRStack — 5G NR gNB-DU cell controller (stub)
// TS 38.300 §6.1, TS 38.401, TS 38.473 §8.7.1
#include "nr_stack.h"
#include "../common/logger.h"
#include <chrono>
#include <thread>
#include <cstdio>

namespace rbs::nr {

NRStack::NRStack(std::shared_ptr<hal::IRFHardware> rf, const NRCellConfig& cfg)
    : cfg_(cfg), rf_(std::move(rf))
{
    phy_ = std::make_shared<NRPhy>(rf_, cfg_);
}

NRStack::~NRStack() { stop(); }

bool NRStack::start() {
    if (running_.load()) return true;
    if (!phy_->start()) {
        RBS_LOG_ERROR("NRStack", "NR PHY start failed");
        return false;
    }
    running_.store(true);
    subframeThread_ = std::thread(&NRStack::subframeLoop, this);
    RBS_LOG_INFO("NRStack", "NR gNB-DU started: cellId=", cfg_.cellId,
             " ARFCN=", cfg_.nrArfcn,
             " DU-ID=", cfg_.gnbDuId);
    return true;
}

void NRStack::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (subframeThread_.joinable()) subframeThread_.join();
    phy_->stop();
    ueMap_.clear();
    RBS_LOG_INFO("NRStack", "NR gNB-DU stopped: cellId=", cfg_.cellId);
}

void NRStack::subframeLoop() {
    while (running_.load()) {
        phy_->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

uint16_t NRStack::admitUE(uint64_t imsi, uint8_t defaultCQI) {
    (void)defaultCQI;
    uint16_t crnti = nextCrnti_++;
    ueMap_[crnti] = imsi;
    RBS_LOG_INFO("NRStack", "NR UE admitted IMSI=", imsi, " C-RNTI=", crnti);
    return crnti;
}

void NRStack::releaseUE(uint16_t crnti) {
    ueMap_.erase(crnti);
    RBS_LOG_INFO("NRStack", "NR UE released C-RNTI=", crnti);
}

void NRStack::printStats() const {
    RBS_LOG_INFO("NRStack", "NR cell=", cfg_.cellId,
             " ARFCN=", cfg_.nrArfcn,
             " PCI=", cfg_.nrPci,
             " SFN=", (phy_ ? phy_->currentSFN() : 0u),
             " UEs=", ueMap_.size(),
             " SSBs=", (phy_ ? phy_->ssbTxCount() : 0u));
}

ByteBuffer NRStack::buildF1SetupRequest() const {
    F1SetupRequest req{};
    req.transactionId = 0;
    req.gnbDuId  = cfg_.gnbDuId;
    req.gnbDuName = "RBS-gNB-DU-" + std::to_string(cfg_.cellId);

    F1ServedCell cell{};
    cell.nrCellIdentity = cfg_.nrCellIdentity;
    cell.nrArfcn        = cfg_.nrArfcn;
    cell.scs            = cfg_.scs;
    cell.pci            = cfg_.nrPci;
    cell.tac            = cfg_.tac;
    req.servedCells.push_back(cell);

    return encodeF1SetupRequest(req);
}

bool NRStack::handleF1SetupResponse(const ByteBuffer& pdu) {
    // Try successful outcome first
    F1SetupResponse rsp{};
    if (decodeF1SetupResponse(pdu, rsp)) {
        f1SetupOk_ = true;
        RBS_LOG_INFO("NRStack", "F1 Setup Response received",
                 " CU-name=", rsp.gnbCuName,
                 " activatedCells=", rsp.activatedCells.size());
        return true;
    }
    // Check failure
    F1SetupFailure fail{};
    if (decodeF1SetupFailure(pdu, fail)) {
        f1SetupOk_ = false;
        RBS_LOG_ERROR("NRStack", "F1 Setup Failure: causeType=",
                static_cast<int>(fail.causeType),
                " causeValue=", static_cast<int>(fail.causeValue));
        return false;
    }
    RBS_LOG_ERROR("NRStack", "F1 Setup: unrecognised response PDU");
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// EN-DC SCG bearer support (TS 37.340)
// ─────────────────────────────────────────────────────────────────────────────

uint16_t NRStack::acceptSCGBearer(RNTI lteCrnti, const rbs::DCBearerConfig& cfg)
{
    if (!running_.load()) {
        RBS_LOG_ERROR("NRStack", "acceptSCGBearer: stack not running");
        return 0;
    }
    // Assign a new NR C-RNTI (reuses the existing UE allocator)
    uint16_t nrCrnti = nextCrnti_++;
    ueMap_[nrCrnti] = 0;  // IMSI unknown on NR side in EN-DC (UE context at MN)

    scgMap_[lteCrnti] = SCGEntry{nrCrnti, cfg.type == rbs::DCBearerType::SCG
                                          ? rbs::ENDCOption::OPTION_3A
                                          : (cfg.type == rbs::DCBearerType::SPLIT_SN
                                             ? rbs::ENDCOption::OPTION_3X
                                             : rbs::ENDCOption::OPTION_3),
                                  {cfg}};

    RBS_LOG_INFO("NRStack", "EN-DC SCG bearer accepted:  lteCrnti=", lteCrnti,
                 " nrCrnti=", nrCrnti,
                 " bearerId=", cfg.enbBearerId,
                 " cell=", cfg_.cellId);
    return nrCrnti;
}

void NRStack::releaseSCGBearer(RNTI lteCrnti)
{
    auto it = scgMap_.find(lteCrnti);
    if (it == scgMap_.end()) return;
    uint16_t nrCrnti = it->second.nrCrnti;
    ueMap_.erase(nrCrnti);
    scgMap_.erase(it);
    RBS_LOG_INFO("NRStack", "EN-DC SCG bearer released:  lteCrnti=", lteCrnti,
                 " nrCrnti=", nrCrnti);
}

uint16_t NRStack::scgCrnti(RNTI lteCrnti) const
{
    auto it = scgMap_.find(lteCrnti);
    return it != scgMap_.end() ? it->second.nrCrnti : 0;
}

std::optional<rbs::ENDCOption> NRStack::endcOption(RNTI lteCrnti) const
{
    auto it = scgMap_.find(lteCrnti);
    if (it == scgMap_.end()) return std::nullopt;
    return it->second.option;
}

}  // namespace rbs::nr

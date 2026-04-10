#include "lte_stack.h"
#include "../common/logger.h"
#include <chrono>
#include <thread>

namespace rbs::lte {

LTEStack::LTEStack(std::shared_ptr<hal::IRFHardware> rf, const LTECellConfig& cfg)
    : cfg_(cfg), rf_(std::move(rf))
{
    phy_  = std::make_shared<LTEPhy>(rf_, cfg_);
    mac_  = std::make_shared<LTEMAC>(phy_, cfg_);
    pdcp_ = std::make_shared<PDCP>();
    rrc_  = std::make_shared<LTERrc>();
}

LTEStack::~LTEStack() { stop(); }

// ────────────────────────────────────────────────────────────────
bool LTEStack::start() {
    if (running_.load()) return true;
    if (!phy_->start()) {
        RBS_LOG_ERROR("LTEStack", "PHY start failed");
        return false;
    }
    if (!mac_->start()) {
        RBS_LOG_ERROR("LTEStack", "MAC start failed");
        return false;
    }
    running_.store(true);
    subframeThread_ = std::thread(&LTEStack::subframeLoop, this);
    RBS_LOG_INFO("LTEStack", "LTE cell ", cfg_.cellId, " started (PCI=",
                 cfg_.pci, ", BW=", static_cast<int>(lteBandwidthToRB(cfg_.bandwidth)),
                 " RBs)");
    return true;
}

void LTEStack::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (subframeThread_.joinable()) subframeThread_.join();
    mac_->stop();
    phy_->stop();
    ueMap_.clear();
    RBS_LOG_INFO("LTEStack", "LTE cell ", cfg_.cellId, " stopped");
}

// ────────────────────────────────────────────────────────────────
// Subframe clock: 1 ms ticks
// ────────────────────────────────────────────────────────────────
void LTEStack::subframeLoop() {
    while (running_.load()) {
        phy_->tick();
        mac_->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ────────────────────────────────────────────────────────────────
RNTI LTEStack::admitUE(IMSI imsi, uint8_t defaultCQI) {
    RNTI rnti = nextRnti_++;
    if (!mac_->admitUE(rnti, defaultCQI)) return 0;
    rrc_->handleConnectionRequest(rnti, imsi);

    // Add default DRB (data radio bearer 1)
    PDCPConfig cfg{};
    cfg.bearerId           = 1;
    cfg.cipherAlg          = PDCPCipherAlg::NULL_ALG;  // upgrade to AES in production
    cfg.headerCompression  = false;
    pdcp_->addBearer(rnti, cfg);
    ueMap_[rnti] = imsi;
    RBS_LOG_INFO("LTEStack", "UE admitted IMSI=", imsi, " RNTI=", rnti,
                 " CQI=", static_cast<int>(defaultCQI));
    return rnti;
}

void LTEStack::releaseUE(RNTI rnti) {
    rrc_->releaseConnection(rnti);
    pdcp_->removeBearer(rnti, 1);
    mac_->releaseUE(rnti);
    ueMap_.erase(rnti);
    RBS_LOG_INFO("LTEStack", "UE released RNTI=", rnti);
}

// ────────────────────────────────────────────────────────────────
bool LTEStack::sendIPPacket(RNTI rnti, uint16_t bearerId, ByteBuffer ipPacket) {
    ByteBuffer pdu = pdcp_->processDlPacket(rnti, bearerId, ipPacket);
    if (pdu.empty()) return false;
    return mac_->enqueueDlSDU(rnti, std::move(pdu));
}

bool LTEStack::receiveIPPacket(RNTI rnti, uint16_t bearerId, ByteBuffer& ipPacket) {
    ByteBuffer pdu;
    if (!mac_->dequeueUlSDU(rnti, pdu)) return false;
    ipPacket = pdcp_->processUlPDU(rnti, bearerId, pdu);
    return !ipPacket.empty();
}

void LTEStack::updateCQI(RNTI rnti, uint8_t cqi) {
    mac_->updateCQI(rnti, cqi);
}

size_t LTEStack::connectedUECount() const {
    return mac_->activeUECount();
}

void LTEStack::printStats() const {
    RBS_LOG_INFO("LTEStack", "Cell=", cfg_.cellId,
                 " EARFCN=", cfg_.earfcn,
                 " PCI=", cfg_.pci,
                 " UEs=", mac_->activeUECount(),
                 " SFN=", phy_->currentSFN());
}

}  // namespace rbs::lte

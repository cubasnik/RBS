#include "lte_stack.h"
#include "../common/logger.h"
#include "../oms/oms.h"
#include <chrono>
#include <thread>
#include <cstdio>

namespace rbs::lte {

LTEStack::LTEStack(std::shared_ptr<hal::IRFHardware> rf, const LTECellConfig& cfg)
    : cfg_(cfg), rf_(std::move(rf))
{
    phy_  = std::make_shared<LTEPhy>(rf_, cfg_);
    mac_  = std::make_shared<LTEMAC>(phy_, cfg_);
    pdcp_ = std::make_shared<PDCP>();
    rrc_  = std::make_shared<LTERrc>();
    rlc_  = std::make_shared<LTERlc>();
    s1ap_ = std::make_unique<S1APLink>("ENB-" + std::to_string(cfg_.cellId));
    s1u_  = std::make_unique<S1ULink>("ENB-" + std::to_string(cfg_.cellId),
                                      cfg_.s1uLocalPort);
    x2ap_ = std::make_unique<X2APLink>("ENB-" + std::to_string(cfg_.cellId));
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

    // Register RRC handover callback → X2AP Handover Request (TS 36.423 §8.3.1)
    rrc_->setHandoverCallback([this](RNTI rnti, uint16_t targetPci, EARFCN targetEarfcn) {
        X2HORequest req{};
        req.rnti         = rnti;
        req.sourceEnbId  = cfg_.cellId;
        req.targetCellId = static_cast<uint32_t>(targetPci);  // PCI used as target ID in sim
        req.causeType    = 0;  // radio
        ByteBuffer rrcCmd{0x04, 0x00,
            static_cast<uint8_t>(targetPci >> 8), static_cast<uint8_t>(targetPci & 0xFF),
            static_cast<uint8_t>(targetEarfcn >> 8), static_cast<uint8_t>(targetEarfcn & 0xFF)};
        req.rrcContainer = std::move(rrcCmd);
        x2ap_->handoverRequest(req);
        auto& oms = rbs::oms::OMS::instance();
        oms.updateCounter("lte.ho.attempts", oms.getCounter("lte.ho.attempts") + 1.0);
        RBS_LOG_INFO("LTEStack", "HO Request via X2AP: rnti=", rnti,
                     " targetPCI=", targetPci, " targetEARFCN=", targetEarfcn);
    });

    if (s1ap_ && !cfg_.mmeAddr.empty()) {
        if (!s1ap_->connect(cfg_.mmeAddr, cfg_.mmePort)) {
            RBS_LOG_ERROR("LTEStack", "S1AP connect failed {}:{}", cfg_.mmeAddr, cfg_.mmePort);
            return false;
        }

        const uint32_t enbId = cfg_.cellId & 0x000FFFFFu;  // 20-bit macro eNB-ID
        const uint32_t plmn = packPlmnHex(cfg_.mcc, cfg_.mnc);
        const std::string enbName = "RBS-ENB-" + std::to_string(cfg_.cellId);
        if (!s1ap_->s1Setup(enbId, enbName, cfg_.tac, plmn)) {
            RBS_LOG_ERROR("LTEStack", "S1AP S1SetupRequest send failed");
            return false;
        }
        RBS_LOG_INFO("LTEStack", "S1AP setup initiated to MME {}:{}", cfg_.mmeAddr, cfg_.mmePort);
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
    if (s1ap_) s1ap_->disconnect();
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
        forwardDlPackets();   // S1-U DL: SGW → GTP-U → air interface
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

uint32_t LTEStack::packPlmnHex(uint16_t mcc, uint16_t mnc)
{
    const uint16_t mcc3 = static_cast<uint16_t>(mcc % 1000);
    const uint16_t mnc3 = static_cast<uint16_t>(mnc % 1000);
    const uint8_t d0 = static_cast<uint8_t>((mcc3 / 100) % 10);
    const uint8_t d1 = static_cast<uint8_t>((mcc3 / 10) % 10);
    const uint8_t d2 = static_cast<uint8_t>(mcc3 % 10);
    const uint8_t d3 = static_cast<uint8_t>((mnc3 / 100) % 10);
    const uint8_t d4 = static_cast<uint8_t>((mnc3 / 10) % 10);
    const uint8_t d5 = static_cast<uint8_t>(mnc3 % 10);
    return (static_cast<uint32_t>(d0) << 20)
         | (static_cast<uint32_t>(d1) << 16)
         | (static_cast<uint32_t>(d2) << 12)
         | (static_cast<uint32_t>(d3) << 8)
         | (static_cast<uint32_t>(d4) << 4)
         |  static_cast<uint32_t>(d5);
}

// ────────────────────────────────────────────────────────────────
RNTI LTEStack::admitUE(IMSI imsi, uint8_t defaultCQI) {
    RNTI rnti = nextRnti_++;

    auto& oms = rbs::oms::OMS::instance();
    const double attempts = oms.getCounter("lte.rrc.attempts") + 1.0;
    oms.updateCounter("lte.rrc.attempts", attempts);

    if (!mac_->admitUE(rnti, defaultCQI)) {
        // RRC setup failure: update rate with unchanged success count
        const double suc = oms.getCounter("lte.rrc.successes");
        oms.updateCounter("lte.rrc.successRate.pct", suc / attempts * 100.0, "%");
        return 0;
    }
    rrc_->handleConnectionRequest(rnti, imsi);
    // SRB1 (AM) + DRB1 (AM) RLC entities
    rlc_->addRB(rnti, 1, LTERlcMode::AM);
    rlc_->addRB(rnti, 3, LTERlcMode::AM);

    // Add default DRB (data radio bearer 1)
    PDCPConfig cfg{};
    cfg.bearerId           = 1;
    cfg.cipherAlg          = PDCPCipherAlg::NULL_ALG;  // upgrade to AES in production
    cfg.headerCompression  = false;
    pdcp_->addBearer(rnti, cfg);
    ueMap_[rnti] = imsi;

    {
        auto& oms = rbs::oms::OMS::instance();
        const double suc      = oms.getCounter("lte.rrc.successes") + 1.0;
        const double attempts = oms.getCounter("lte.rrc.attempts");
        oms.updateCounter("lte.rrc.successes", suc);
        oms.updateCounter("lte.rrc.successRate.pct",
                          attempts > 0 ? suc / attempts * 100.0 : 100.0, "%");
    }

    RBS_LOG_INFO("LTEStack", "UE admitted IMSI=", imsi, " RNTI=", rnti,
                 " CQI=", static_cast<int>(defaultCQI));
    return rnti;
}

// ────────────────────────────────────────────────────────────────
// Carrier Aggregation UE admission (TS 36.321 §5.14, TS 36.300 §10.1)
// Admits the UE on the primary CC, then activates ccCount-1 secondary CCs.
// ────────────────────────────────────────────────────────────────
RNTI LTEStack::admitUECA(IMSI imsi, uint8_t ccCount, uint8_t defaultCQI) {
    const uint8_t clamped = (ccCount < 1) ? 1 : (ccCount > CA_MAX_CC ? CA_MAX_CC : ccCount);
    RNTI rnti = admitUE(imsi, defaultCQI);
    if (rnti == 0) return 0;
    mac_->configureCA(rnti, clamped);
    RBS_LOG_INFO("LTEStack", "CA UE admitted IMSI=", imsi, " RNTI=", rnti,
                 " CCs=", static_cast<int>(clamped));
    return rnti;
}

void LTEStack::releaseUE(RNTI rnti) {
    // Count ERAB drop: UE released while bearer was active.
    {
        auto& oms = rbs::oms::OMS::instance();
        const double drops   = oms.getCounter("lte.erab.drops")   + 1.0;
        const double setups  = oms.getCounter("lte.erab.setups");
        oms.updateCounter("lte.erab.drops", drops);
        oms.updateCounter("lte.erab.dropRate.pct",
                          setups > 0 ? drops / setups * 100.0 : 0.0, "%");
    }
    teardownERAB(rnti, 1);  // release DRB1 GTP-U tunnel (TS 36.413 §8.4.2)
    rrc_->releaseConnection(rnti);
    rlc_->removeRB(rnti, 1);
    rlc_->removeRB(rnti, 3);
    pdcp_->removeBearer(rnti, 1);
    mac_->releaseUE(rnti);
    ueMap_.erase(rnti);
    RBS_LOG_INFO("LTEStack", "UE released RNTI=", rnti);
}

// ─────────────────────────────────────────────────────────────────────────────
// CSFB — Circuit Switched Fallback (TS 36.300 §22.3.2)
// ─────────────────────────────────────────────────────────────────────────────
void LTEStack::triggerCSFB(RNTI rnti, uint16_t gsmArfcn) {
    // Send RRC Connection Release with redirectedCarrierInfo-GERAN
    rrc_->releaseWithRedirect(rnti, gsmArfcn);
    // Tear down PS bearers (not counted as E-RAB drop: this is a controlled fallback)
    teardownERAB(rnti, 1);
    rlc_->removeRB(rnti, 1);
    rlc_->removeRB(rnti, 3);
    pdcp_->removeBearer(rnti, 1);
    mac_->releaseUE(rnti);
    ueMap_.erase(rnti);
    auto& oms = rbs::oms::OMS::instance();
    oms.updateCounter("lte.csfb.count", oms.getCounter("lte.csfb.count") + 1.0);
    RBS_LOG_INFO("LTEStack", "CSFB: RNTI=", rnti, " → GSM ARFCN=", gsmArfcn);
}

// ────────────────────────────────────────────────────────────────
bool LTEStack::sendIPPacket(RNTI rnti, uint16_t bearerId, ByteBuffer ipPacket) {
    ByteBuffer pdcpPdu = pdcp_->processDlPacket(rnti, bearerId, ipPacket);
    if (pdcpPdu.empty()) return false;
    // PDCP SDU → RLC segmentation → MAC
    uint8_t rbId = static_cast<uint8_t>(bearerId) + 2;   // DRB1→RB3, DRB2→RB4
    rlc_->sendSdu(rnti, rbId, pdcpPdu);
    ByteBuffer rlcPdu;
    if (!rlc_->pollPdu(rnti, rbId, rlcPdu, 1500)) return false;
    return mac_->enqueueDlSDU(rnti, std::move(rlcPdu));
}

bool LTEStack::receiveIPPacket(RNTI rnti, uint16_t bearerId, ByteBuffer& ipPacket) {
    ByteBuffer rlcPdu;
    if (!mac_->dequeueUlSDU(rnti, rlcPdu)) return false;
    // MAC PDU → RLC reassembly → PDCP
    uint8_t rbId = static_cast<uint8_t>(bearerId) + 2;
    rlc_->deliverPdu(rnti, rbId, rlcPdu);
    ByteBuffer pdcpPdu;
    if (!rlc_->receiveSdu(rnti, rbId, pdcpPdu)) return false;
    ipPacket = pdcp_->processUlPDU(rnti, bearerId, pdcpPdu);
    if (ipPacket.empty()) return false;
    // Forward UL IP to SGW via S1-U GTP-U (TS 29.060 §6, TS 36.413 §8.1.3)
    s1u_->sendGtpuPdu(rnti, static_cast<uint8_t>(bearerId), ipPacket);
    return true;
}

void LTEStack::updateCQI(RNTI rnti, uint8_t cqi) {
    mac_->updateCQI(rnti, cqi);
}

size_t LTEStack::connectedUECount() const {
    return mac_->activeUECount();
}

// ────────────────────────────────────────────────────────────────
// S1-U GTP-U bearer management (TS 29.060 / TS 36.413 §8.4)
// ────────────────────────────────────────────────────────────────
bool LTEStack::setupERAB(RNTI rnti, uint8_t erabId, const GTPUTunnel& sgw) {
    bool ok = s1u_->createTunnel(rnti, erabId, sgw);
    if (ok) {
        auto& oms = rbs::oms::OMS::instance();
        const double setups = oms.getCounter("lte.erab.setups") + 1.0;
        const double drops  = oms.getCounter("lte.erab.drops");
        oms.updateCounter("lte.erab.setups", setups);
        oms.updateCounter("lte.erab.dropRate.pct",
                          setups > 0 ? drops / setups * 100.0 : 0.0, "%");
        RBS_LOG_INFO("LTEStack", "S1-U ERAB setup rnti=", rnti,
                     " erabId=", erabId, " teid=0x", sgw.teid);
    }
    return ok;
}

bool LTEStack::teardownERAB(RNTI rnti, uint8_t erabId) {
    return s1u_->deleteTunnel(rnti, erabId);
}

// ────────────────────────────────────────────────────────────────
// DL forwarding: drain incoming GTP-U frames each subframe tick.
// SGW → S1-U UDP → GTP-U decode → PDCP → RLC → MAC → UE (air)
// ────────────────────────────────────────────────────────────────
void LTEStack::forwardDlPackets() {
    for (auto& [rnti, imsi] : ueMap_) {
        ByteBuffer ip;
        while (s1u_->recvGtpuPdu(rnti, 1u, ip))
            sendIPPacket(rnti, 1, ip);
    }
}

void LTEStack::printStats() const {
    RBS_LOG_INFO("LTEStack", "Cell=", cfg_.cellId,
                 " EARFCN=", cfg_.earfcn,
                 " PCI=", cfg_.pci,
                 " UEs=", mac_->activeUECount(),
                 " SFN=", phy_->currentSFN());
}

}  // namespace rbs::lte

#include "umts_stack.h"
#include "../common/logger.h"
#include <chrono>
#include <thread>

namespace rbs::umts {

UMTSStack::UMTSStack(std::shared_ptr<hal::IRFHardware> rf, const UMTSCellConfig& cfg)
    : cfg_(cfg), rf_(std::move(rf))
{
    phy_ = std::make_shared<UMTSPhy>(rf_, cfg_);
    mac_ = std::make_shared<UMTSMAC>(phy_, cfg_);
    rrc_ = std::make_shared<UMTSRrc>();
    rlc_ = std::make_shared<UMTSRlc>();
    iub_ = std::make_unique<IubNbap>("NodeB-" + std::to_string(cfg_.cellId));
}

UMTSStack::~UMTSStack() { stop(); }

// ────────────────────────────────────────────────────────────────
bool UMTSStack::start() {
    if (running_.load()) return true;
    if (!phy_->start()) {
        RBS_LOG_ERROR("UMTSStack", "PHY start failed");
        return false;
    }
    if (!mac_->start()) {
        RBS_LOG_ERROR("UMTSStack", "MAC start failed");
        return false;
    }
    running_.store(true);
    frameThread_ = std::thread(&UMTSStack::frameLoop, this);
    RBS_LOG_INFO("UMTSStack", "UMTS cell ", cfg_.cellId, " started");
    return true;
}

void UMTSStack::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (frameThread_.joinable()) frameThread_.join();
    mac_->stop();
    phy_->stop();
    ueMap_.clear();
    RBS_LOG_INFO("UMTSStack", "UMTS cell ", cfg_.cellId, " stopped");
}

// ────────────────────────────────────────────────────────────────
void UMTSStack::frameLoop() {
    // WCDMA radio frame = 10 ms; simulated at compressed speed
    using namespace std::chrono_literals;
    while (running_.load()) {
        phy_->tick();
        mac_->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ────────────────────────────────────────────────────────────────
RNTI UMTSStack::admitUE(IMSI imsi, SF sf) {
    RNTI rnti = mac_->assignDCH(sf);
    if (rnti != 0) {
        ueMap_[rnti] = imsi;
        rrc_->handleConnectionRequest(rnti, imsi);
        rlc_->addRB(rnti, 3, RLCMode::AM);   // DRB1 (AM mode)
        RBS_LOG_INFO("UMTSStack", "UE admitted IMSI=", imsi, " RNTI=", rnti);
    }
    return rnti;
}

void UMTSStack::releaseUE(RNTI rnti) {
    rrc_->releaseConnection(rnti);
    rlc_->removeRB(rnti, 3);
    mac_->releaseDCH(rnti);
    ueMap_.erase(rnti);
    RBS_LOG_INFO("UMTSStack", "UE released RNTI=", rnti);
}

RNTI UMTSStack::admitUEHSDPA(IMSI imsi) {
    RNTI rnti = mac_->assignHSDSCH();
    if (rnti != 0) {
        ueMap_[rnti] = imsi;
        rrc_->handleConnectionRequest(rnti, imsi);
        rlc_->addRB(rnti, 3, RLCMode::AM);
        RBS_LOG_INFO("UMTSStack", "HSDPA UE admitted IMSI=", imsi, " RNTI=", rnti);
    }
    return rnti;
}

RNTI UMTSStack::admitUEEDCH(IMSI imsi) {
    RNTI rnti = mac_->assignEDCH();
    if (rnti != 0) {
        ueMap_[rnti] = imsi;
        rrc_->handleConnectionRequest(rnti, imsi);
        rlc_->addRB(rnti, 3, RLCMode::AM);
        RBS_LOG_INFO("UMTSStack", "E-DCH UE admitted IMSI=", imsi, " RNTI=", rnti);
    }
    return rnti;
}

bool UMTSStack::reconfigureDCH(RNTI rnti, SF newSf) {
    if (ueMap_.find(rnti) == ueMap_.end()) return false;
    RBS_LOG_INFO("UMTSStack", "DCH reconfig RNTI=", rnti,
                 " newSF=", static_cast<int>(newSf));
    // MAC-level SF change is transparent in the simulator
    // (PHY spreading factor update would happen here on real HW)
    return true;
}

bool UMTSStack::sendData(RNTI rnti, ByteBuffer data) {
    // SDU → RLC segmentation → MAC
    rlc_->sendSdu(rnti, 3, data);
    ByteBuffer rlcPdu;
    if (rlc_->pollPdu(rnti, 3, rlcPdu))
        return mac_->enqueueDlData(rnti, std::move(rlcPdu));
    return mac_->enqueueDlData(rnti, std::move(data));
}

bool UMTSStack::receiveData(RNTI rnti, ByteBuffer& data) {
    ByteBuffer rlcPdu;
    if (!mac_->dequeueUlData(rnti, rlcPdu)) return false;
    rlc_->deliverPdu(rnti, 3, rlcPdu);
    if (rlc_->receiveSdu(rnti, 3, data)) return true;
    data = std::move(rlcPdu);
    return true;
}

size_t UMTSStack::connectedUECount() const {
    return mac_->activeChannelCount();
}

const std::vector<ActiveSetEntry>& UMTSStack::activeSet(RNTI rnti) const {
    return rrc_->activeSet(rnti);
}

// ── softHandoverUpdate ────────────────────────────────────────────────────────────
//
// Implements TS 25.331 §10.3.7.4 / TS 25.433 §8.1.4 RLC-based Active Set update:
//
//  Event 1A (cell better than threshold): add leg via NBAP RL-Addition
//  Event 1B (cell worse than threshold) : remove leg via NBAP RL-Deletion
//  Event 1C (non-AS cell beats weakest) : replace worst leg
//
// The IubNbap is connected to a simulated RNC address; in the test
// harness connect() is called implicitly if not yet connected.
void UMTSStack::softHandoverUpdate(const MeasurementReport& report)
{
    RNTI rnti = report.rnti;

    // Guard: UE must be known
    if (ueMap_.find(rnti) == ueMap_.end()) {
        RBS_LOG_WARNING("UMTSStack",
            "softHandoverUpdate: unknown RNTI=", rnti);
        return;
    }

    // Ensure Iub is logically connected (simulated)
    if (!iub_->isConnected())
        iub_->connect("127.0.0.1", 25412);

    // Delegate AS decision to RRC (handles 1A/1B/1C logic, updates context)
    rrc_->processMeasurementReport(report);

    // Mirror the RRC decision to NBAP
    const auto& as = rrc_->activeSet(rnti);
    switch (report.event) {
        case RrcMeasEvent::EVENT_1A: {
            // Last entry added by RRC is the new one
            if (!as.empty()) {
                const auto& newest = as.back();
                if (!newest.primaryCell) {
                    // Secondary leg — add via NBAP RL-Addition (TS 25.433 §8.1.4)
                    iub_->radioLinkAddition(rnti,
                        newest.primaryScrCode,
                        SF::SF16);
                }
            }
            break;
        }
        case RrcMeasEvent::EVENT_1B:
            // RRC already removed it; signal NBAP RL-Deletion for the leg
            if (iub_->isConnected())
                iub_->radioLinkDeletionSHO(rnti, report.triggeringScrCode);
            break;
        case RrcMeasEvent::EVENT_1C:
            // RRC replaced the weakest — find old PSC not in new AS, delete it
            // (approximate: just RL-Addition for triggering cell since we
            //  don't track the old PSC in this path)
            if (!as.empty()) {
                const auto& newest = as.back();
                if (!newest.primaryCell)
                    iub_->radioLinkAddition(rnti, newest.primaryScrCode, SF::SF16);
            }
            break;
        default:
            break;
    }
}

void UMTSStack::printStats() const {
    RBS_LOG_INFO("UMTSStack", "Cell=", cfg_.cellId,
                 " UARFCN=", cfg_.uarfcn,
                 " UEs=", mac_->activeChannelCount(),
                 " Frame=", phy_->currentFrameNumber());
}

}  // namespace rbs::umts

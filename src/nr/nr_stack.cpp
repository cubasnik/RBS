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
    mac_ = std::make_shared<NRMac>(cfg_.scs);
    sdap_ = std::make_shared<NRSDAP>();
    pdcp_ = std::make_shared<NRPDCP>();
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
    if (ngap_ && preferredAmfId_ != 0 && ngap_->isConnected(preferredAmfId_)) {
        (void)ngSetup(preferredAmfId_);
    }
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
    pduSessions_.clear();
    ngPeers_.clear();
    xnHandovers_.clear();
    xnPeers_.clear();
    RBS_LOG_INFO("NRStack", "NR gNB-DU stopped: cellId=", cfg_.cellId);
}

void NRStack::subframeLoop() {
    while (running_.load()) {
        phy_->tick();
        if (autoDlEnabled_.load()) {
            const uint16_t prbs = autoDlPrbs_.load();
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (mac_) {
                (void)mac_->scheduleDl(prbs);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

size_t NRStack::connectedUECount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return ueMap_.size();
}

uint16_t NRStack::admitUE(uint64_t imsi, uint8_t defaultCQI) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    uint16_t crnti = nextCrnti_++;
    ueMap_[crnti] = imsi;
    if (mac_) {
        (void)mac_->addUE(crnti, defaultCQI);
    }
    RBS_LOG_INFO("NRStack", "NR UE admitted IMSI=", imsi, " C-RNTI=", crnti);
    return crnti;
}

void NRStack::releaseUE(uint16_t crnti) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    ueMap_.erase(crnti);
    if (mac_) {
        mac_->removeUE(crnti);
    }
    if (sdap_) {
        sdap_->clearMappings(crnti);
    }
    RBS_LOG_INFO("NRStack", "NR UE released C-RNTI=", crnti);
}

bool NRStack::configureQoSFlow(uint16_t crnti, uint8_t qfi, uint8_t drbId) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!mac_ || !sdap_) {
        return false;
    }
    const bool macOk = mac_->setQfiMapping(crnti, qfi, drbId);
    const bool sdapOk = sdap_->mapQfiToDrb(crnti, qfi, drbId);
    return macOk && sdapOk;
}

uint8_t NRStack::resolveDrbForQfi(uint16_t crnti, uint8_t qfi) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!mac_) {
        return 0;
    }
    return mac_->resolveDrb(crnti, qfi);
}

bool NRStack::updateUeCqi(uint16_t crnti, uint8_t cqi) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!mac_) {
        return false;
    }
    return mac_->updateUECqi(crnti, cqi);
}

bool NRStack::reportHarqFeedback(uint16_t crnti, uint8_t harqId, bool ack) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!mac_) {
        return false;
    }
    return mac_->reportHarqFeedback(crnti, harqId, ack);
}

std::vector<NRScheduleGrant> NRStack::scheduleDl(uint16_t totalPrbs) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!mac_) {
        return {};
    }
    return mac_->scheduleDl(totalPrbs);
}

uint32_t NRStack::pendingDlBytes(uint16_t crnti) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!mac_) {
        return 0;
    }
    return mac_->pendingDlBytes(crnti);
}

void NRStack::setAutoDlScheduling(bool enabled, uint16_t prbsPerTick) {
    autoDlPrbs_.store(prbsPerTick == 0 ? 1 : prbsPerTick);
    autoDlEnabled_.store(enabled);
}

bool NRStack::submitDlSdapData(uint16_t crnti, uint8_t qfi,
                               const ByteBuffer& payload,
                               ByteBuffer& outPdcpPdu) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!mac_ || !sdap_ || !pdcp_) {
        return false;
    }
    const uint8_t drbId = mac_->resolveDrb(crnti, qfi);
    if (drbId == 0) {
        return false;
    }
    const ByteBuffer sdapPdu = sdap_->encodeDataPdu(crnti, qfi, payload);
    outPdcpPdu = pdcp_->encodeDataPdu(crnti, drbId, sdapPdu);
    (void)mac_->enqueueDlBytes(crnti, static_cast<uint32_t>(outPdcpPdu.size()));
    return true;
}

bool NRStack::setUeSlice(uint16_t crnti, NRSlice slice) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return mac_ ? mac_->setUeSlice(crnti, slice) : false;
}

std::vector<NRSliceMetrics> NRStack::currentSliceMetrics() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return mac_ ? mac_->currentSliceMetrics() : std::vector<NRSliceMetrics>{};
}

void NRStack::printStats() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
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

void NRStack::attachNgapLink(std::shared_ptr<NgapLink> ngap) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    ngap_ = std::move(ngap);
}

bool NRStack::connectNgPeer(uint64_t amfId) {
    std::shared_ptr<NgapLink> link;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        link = ngap_;
        preferredAmfId_ = amfId;
    }
    return link ? link->connect(amfId) : false;
}

bool NRStack::ngSetup(uint64_t amfId) {
    std::shared_ptr<NgapLink> link;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        link = ngap_;
    }
    if (!link || !link->isConnected(amfId)) {
        return false;
    }

    NgSetupRequest req{};
    req.transactionId = nextNgTransactionId_++;
    req.ranNodeId = cfg_.gnbDuId;
    req.gnbName = "RBS-gNB-" + std::to_string(cfg_.cellId);
    req.tac = cfg_.tac;
    req.mcc = cfg_.mcc;
    req.mnc = cfg_.mnc;
    return link->ngSetup(amfId, req);
}

bool NRStack::ngSetupComplete(uint64_t amfId) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = ngPeers_.find(amfId);
    return it != ngPeers_.end() && it->second;
}

size_t NRStack::processNgMessages() {
    std::shared_ptr<NgapLink> link;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        link = ngap_;
    }
    if (!link) {
        return 0;
    }

    size_t processed = 0;
    NgapMessage message{};
    while (link->recvNgapMessage(message)) {
        ++processed;
        switch (message.procedure) {
        case NgapProcedure::NG_SETUP_RESPONSE: {
            NgSetupResponse rsp{};
            if (!decodeNgSetupResponse(message.payload, rsp)) {
                break;
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            ngPeers_[rsp.amfId] = true;
            break;
        }
        case NgapProcedure::PDU_SESSION_SETUP_REQUEST: {
            PduSessionSetupRequest req{};
            if (!decodePduSessionSetupRequest(message.payload, req)) {
                break;
            }
            uint32_t teid = 0;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (ueMap_.find(req.ranUeNgapId) == ueMap_.end()) {
                    break;
                }
                auto& session = pduSessions_[req.ranUeNgapId][req.pduSessionId];
                session.amfUeNgapId = req.amfUeNgapId;
                session.pduSessionId = req.pduSessionId;
                session.sst = req.sst;
                session.sd = req.sd;
                session.gtpTeid = 0xB0000000u | (static_cast<uint32_t>(req.ranUeNgapId) << 8) | req.pduSessionId;
                session.active = true;
                teid = session.gtpTeid;
            }

            PduSessionSetupResponse rsp{};
            rsp.transactionId = req.transactionId;
            rsp.amfUeNgapId = req.amfUeNgapId;
            rsp.ranUeNgapId = req.ranUeNgapId;
            rsp.pduSessionId = req.pduSessionId;
            rsp.gtpTeid = teid;
            rsp.transfer = {0x10, 0x20, 0x30};
            (void)link->pduSessionSetupResponse(message.sourceNodeId, rsp);
            break;
        }
        case NgapProcedure::UE_CONTEXT_RELEASE_COMMAND: {
            UeContextReleaseCommand cmd{};
            if (!decodeUeContextReleaseCommand(message.payload, cmd)) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                pduSessions_.erase(cmd.ranUeNgapId);
            }
            releaseUE(cmd.ranUeNgapId);

            UeContextReleaseComplete complete{};
            complete.transactionId = cmd.transactionId;
            complete.amfUeNgapId = cmd.amfUeNgapId;
            complete.ranUeNgapId = cmd.ranUeNgapId;
            (void)link->ueContextReleaseComplete(message.sourceNodeId, complete);
            break;
        }
        case NgapProcedure::NG_SETUP_REQUEST:
        case NgapProcedure::PDU_SESSION_SETUP_RESPONSE:
        case NgapProcedure::UE_CONTEXT_RELEASE_COMPLETE:
            break;
        }
    }

    return processed;
}

bool NRStack::hasActivePduSession(uint16_t crnti, uint8_t pduSessionId) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto ueIt = pduSessions_.find(crnti);
    if (ueIt == pduSessions_.end()) {
        return false;
    }
    const auto sessionIt = ueIt->second.find(pduSessionId);
    return sessionIt != ueIt->second.end() && sessionIt->second.active;
}

void NRStack::attachXnLink(std::shared_ptr<XnAPLink> xnap) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    xnap_ = std::move(xnap);
}

bool NRStack::connectXnPeer(uint64_t targetGnbId) {
    std::shared_ptr<XnAPLink> link;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        link = xnap_;
    }
    return link ? link->connect(targetGnbId) : false;
}

bool NRStack::xnSetup(uint64_t targetGnbId) {
    std::shared_ptr<XnAPLink> link;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        link = xnap_;
    }
    if (!link || !link->isConnected(targetGnbId)) {
        return false;
    }

    XnSetupRequest req{};
    req.transactionId = nextXnTransactionId_++;
    req.localGnbId = cfg_.gnbDuId;
    req.gnbName = "RBS-gNB-" + std::to_string(cfg_.cellId);
    req.servedCells.push_back(XnServedCell{cfg_.nrCellIdentity, cfg_.nrArfcn, cfg_.nrPci, cfg_.tac});
    return link->xnSetup(targetGnbId, req);
}

bool NRStack::xnSetupComplete(uint64_t peerGnbId) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = xnPeers_.find(peerGnbId);
    return it != xnPeers_.end() && it->second;
}

bool NRStack::handoverRequired(uint16_t crnti, uint64_t targetGnbId,
                               uint64_t targetCellId, uint8_t causeType) {
    std::shared_ptr<XnAPLink> link;
    uint64_t imsi = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        const auto ueIt = ueMap_.find(crnti);
        if (ueIt == ueMap_.end()) {
            return false;
        }
        link = xnap_;
        imsi = ueIt->second;
    }
    if (!link || !link->isConnected(targetGnbId)) {
        return false;
    }

    XnHandoverRequest req{};
    req.transactionId = nextXnTransactionId_++;
    req.sourceGnbId = cfg_.gnbDuId;
    req.targetGnbId = targetGnbId;
    req.sourceCellId = cfg_.nrCellIdentity;
    req.targetCellId = targetCellId;
    req.sourceCrnti = crnti;
    req.ueImsi = imsi;
    req.causeType = causeType;
    req.rrcContainer = {0x01, 0x02, 0x03};

    const bool ok = link->handoverRequest(req);
    if (ok) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto& state = xnHandovers_[crnti];
        state.sourceGnbId = cfg_.gnbDuId;
        state.targetGnbId = targetGnbId;
        state.sourceCellId = cfg_.nrCellIdentity;
        state.targetCellId = targetCellId;
        state.sourceCrnti = crnti;
        state.requestSent = true;
    }
    return ok;
}

size_t NRStack::processXnMessages() {
    std::shared_ptr<XnAPLink> link;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        link = xnap_;
    }
    if (!link) {
        return 0;
    }

    size_t processed = 0;
    XnAPMessage message{};
    while (link->recvXnApMessage(message)) {
        ++processed;
        switch (message.procedure) {
        case XnAPProcedure::XN_SETUP_REQUEST: {
            XnSetupRequest req{};
            if (!decodeXnSetupRequest(message.payload, req)) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                xnPeers_[req.localGnbId] = true;
            }
            XnSetupResponse rsp{};
            rsp.transactionId = req.transactionId;
            rsp.respondingGnbId = cfg_.gnbDuId;
            rsp.activatedCells.push_back(cfg_.nrCellIdentity);
            (void)link->xnSetupResponse(req.localGnbId, rsp);
            break;
        }
        case XnAPProcedure::XN_SETUP_RESPONSE: {
            XnSetupResponse rsp{};
            if (!decodeXnSetupResponse(message.payload, rsp)) {
                break;
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            xnPeers_[rsp.respondingGnbId] = true;
            break;
        }
        case XnAPProcedure::HANDOVER_REQUEST: {
            XnHandoverRequest req{};
            if (!decodeXnHandoverRequest(message.payload, req)) {
                break;
            }
            const uint16_t targetCrnti = admitUE(
                req.ueImsi == 0 ? static_cast<uint64_t>(req.sourceCrnti) : req.ueImsi,
                10);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                auto& state = xnHandovers_[req.sourceCrnti];
                state.sourceGnbId = req.sourceGnbId;
                state.targetGnbId = req.targetGnbId;
                state.sourceCellId = req.sourceCellId;
                state.targetCellId = req.targetCellId;
                state.sourceCrnti = req.sourceCrnti;
                state.targetCrnti = targetCrnti;
                state.requestReceived = true;
            }

            XnHandoverNotify notify{};
            notify.transactionId = req.transactionId;
            notify.sourceGnbId = req.sourceGnbId;
            notify.targetGnbId = cfg_.gnbDuId;
            notify.sourceCellId = req.sourceCellId;
            notify.targetCellId = cfg_.nrCellIdentity;
            notify.sourceCrnti = req.sourceCrnti;
            notify.targetCrnti = targetCrnti;
            notify.rrcContainer = {0x0A, 0x0B, 0x0C};
            (void)link->handoverNotify(notify);
            break;
        }
        case XnAPProcedure::HANDOVER_NOTIFY: {
            XnHandoverNotify notify{};
            if (!decodeXnHandoverNotify(message.payload, notify)) {
                break;
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto& state = xnHandovers_[notify.sourceCrnti];
            state.sourceGnbId = notify.sourceGnbId;
            state.targetGnbId = notify.targetGnbId;
            state.sourceCellId = notify.sourceCellId;
            state.targetCellId = notify.targetCellId;
            state.sourceCrnti = notify.sourceCrnti;
            state.targetCrnti = notify.targetCrnti;
            state.notifyReceived = true;
            break;
        }
        }
    }

    return processed;
}

bool NRStack::hasReceivedXnHandover(uint16_t sourceCrnti) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = xnHandovers_.find(sourceCrnti);
    return it != xnHandovers_.end() && it->second.requestReceived;
}

bool NRStack::hasCompletedXnHandover(uint16_t sourceCrnti) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = xnHandovers_.find(sourceCrnti);
    return it != xnHandovers_.end() && it->second.notifyReceived;
}

uint16_t NRStack::handoverTargetCrnti(uint16_t sourceCrnti) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = xnHandovers_.find(sourceCrnti);
    return it == xnHandovers_.end() ? 0 : it->second.targetCrnti;
}

// ─────────────────────────────────────────────────────────────────────────────
// EN-DC SCG bearer support (TS 37.340)
// ─────────────────────────────────────────────────────────────────────────────

uint16_t NRStack::acceptSCGBearer(RNTI lteCrnti, const rbs::DCBearerConfig& cfg)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
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
    std::lock_guard<std::mutex> lock(stateMutex_);
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
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = scgMap_.find(lteCrnti);
    return it != scgMap_.end() ? it->second.nrCrnti : 0;
}

std::optional<rbs::ENDCOption> NRStack::endcOption(RNTI lteCrnti) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = scgMap_.find(lteCrnti);
    if (it == scgMap_.end()) return std::nullopt;
    return it->second.option;
}

}  // namespace rbs::nr

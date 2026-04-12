#include "iub_link.h"
#include "nbap_codec.h"

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
//  IubNbap
// ─────────────────────────────────────────────────────────────────────────────

IubNbap::IubNbap(const std::string& nodeBId)
    : rbs::LinkController("iub-" + nodeBId)
    , nodeBId_(nodeBId)
{}

bool IubNbap::connect(const std::string& rncAddr, uint16_t port)
{
    if (connected_) {
        RBS_LOG_WARNING("IubNbap", "[{}] уже подключён к RNC {}:{}", nodeBId_, rncAddr_, rncPort_);
        return true;
    }
    rncAddr_   = rncAddr;
    rncPort_   = port;
    connected_ = true;
    RBS_LOG_INFO("IubNbap", "[{}] NBAP-соединение с RNC {}:{} установлено", nodeBId_, rncAddr, port);
    return true;
}

void IubNbap::disconnect()
{
    if (!connected_) return;
    connected_ = false;
    links_.clear();
    commonMeas_.clear();
    RBS_LOG_INFO("IubNbap", "[{}] NBAP-соединение закрыто", nodeBId_);
}

bool IubNbap::sendCellSetup(uint16_t cellId, uint16_t primaryScrCode,
                             uint16_t uarfcnDl, uint16_t uarfcnUl)
{
    if (!connected_) {
        RBS_LOG_ERROR("IubNbap", "[{}] sendCellSetup: нет соединения с RNC", nodeBId_);
        return false;
    }
    RBS_LOG_INFO("IubNbap",
                 "[{}] NBAP CELL SETUP cellId={} PSC={} UarfcnDL={} UarfcnUL={}",
                 nodeBId_, cellId, primaryScrCode, uarfcnDl, uarfcnUl);

    // TS 25.433 §8.3.6 — encode as APER CellSetupRequestFDD
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_CellSetupRequestFDD(
        cellId,           // localCellId
        cellId,           // C-ID (same as local cell ID for simulator)
        1,                // cfgGenId
        uarfcnUl,
        uarfcnDl,
        200,              // maxTxPower = 200 (20 dBm in 0.1dBm units)
        primaryScrCode,
        txId
    );
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP CELL SETUP APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::CELL_SETUP, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::commonMeasurementInitiation(uint16_t measId,
                                           const std::string& measObject)
{
    if (!connected_) {
        RBS_LOG_ERROR("IubNbap", "[{}] commonMeasInit: нет соединения с RNC", nodeBId_);
        return false;
    }
    commonMeas_[measId] = measObject;
    RBS_LOG_INFO("IubNbap", "[{}] NBAP COMMON MEAS INIT measId={} object={}",
                 nodeBId_, measId, measObject);

    ByteBuffer payload{static_cast<uint8_t>(measId >> 8), static_cast<uint8_t>(measId)};
    payload.insert(payload.end(), measObject.begin(), measObject.end());
    NBAPMessage msg{NBAPProcedure::COMMON_MEASUREMENT_INITIATE, nextTxId(), std::move(payload)};
    bool ok = sendNbapMsg(msg);

    // Симуляция: RNC сразу шлёт COMMON_MEASUREMENT_REPORT с фиктивными значениями
    if (ok) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        ByteBuffer rep{static_cast<uint8_t>(measId >> 8), static_cast<uint8_t>(measId),
                       0xC8}; // RSCP -80dBm (симуляция)
        rxQueue_.push({NBAPProcedure::COMMON_MEASUREMENT_REPORT, nextTxId(), std::move(rep)});
    }
    return ok;
}

bool IubNbap::commonMeasurementTermination(uint16_t measId)
{
    commonMeas_.erase(measId);
    RBS_LOG_INFO("IubNbap", "[{}] NBAP COMMON MEAS TERMINATE measId={}", nodeBId_, measId);
    ByteBuffer payload{static_cast<uint8_t>(measId >> 8), static_cast<uint8_t>(measId)};
    NBAPMessage msg{NBAPProcedure::COMMON_MEASUREMENT_TERMINATE, nextTxId(), std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::radioLinkSetup(RNTI rnti, uint16_t scrCode, SF sf)
{
    if (!connected_) {
        RBS_LOG_ERROR("IubNbap", "[{}] radioLinkSetup: нет соединения с RNC", nodeBId_);
        return false;
    }
    links_[rnti] = {scrCode, sf};
    RBS_LOG_INFO("IubNbap", "[{}] NBAP RADIO LINK SETUP rnti={} scrCode={} SF={}",
                 nodeBId_, rnti, scrCode, static_cast<int>(sf));

    // TS 25.433 §8.1.1 — encode as APER RadioLinkSetupRequestFDD
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_RadioLinkSetupRequestFDD(
        static_cast<uint32_t>(rnti), // crncCtxId = RNTI for simulator
        txId
    );
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP RL SETUP APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_SETUP, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::radioLinkDeletion(RNTI rnti)
{
    auto it = links_.find(rnti);
    if (it == links_.end()) {
        RBS_LOG_WARNING("IubNbap", "[{}] radioLinkDeletion: rnti={} не найден", nodeBId_, rnti);
        return false;
    }
    links_.erase(it);
    shoLegs_.erase(rnti);  // also clean up any SHO legs
    RBS_LOG_INFO("IubNbap", "[{}] NBAP RADIO LINK DELETION rnti={}", nodeBId_, rnti);

    // TS 25.433 §8.1.6 — encode as APER RadioLinkDeletionRequest
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_RadioLinkDeletionRequest(
        static_cast<uint32_t>(rnti), // nodeBCtxId = RNTI for simulator
        static_cast<uint32_t>(rnti), // crncCtxId  = RNTI for simulator
        txId
    );
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP RL DELETION APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_DELETION, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::radioLinkAddition(RNTI rnti, uint16_t scrCode, SF sf)
{
    if (!connected_) {
        RBS_LOG_WARNING("IubNbap", "[{}] radioLinkAddition: not connected", nodeBId_);
        return false;
    }
    // Primary link must already exist
    if (links_.find(rnti) == links_.end()) {
        RBS_LOG_WARNING("IubNbap",
            "[{}] radioLinkAddition: primary link not found for rnti={}", nodeBId_, rnti);
        return false;
    }
    // Store as a secondary SHO leg
    shoLegs_[rnti][scrCode] = {scrCode, sf, true};
    RBS_LOG_INFO("IubNbap",
        "[{}] NBAP RL ADDITION (SHO) rnti={} newScrCode={} SF={}",
        nodeBId_, rnti, scrCode, static_cast<int>(sf));

    // TS 25.433 §8.1.4 — RadioLinkAdditionRequestFDD
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    // nodeBCtxId encodes rnti; scrCode encoded via upper 16 bits for uniqueness
    uint32_t ctxId = (static_cast<uint32_t>(rnti) << 9) | (scrCode & 0x1FF);
    ByteBuffer payload = nbap_encode_RadioLinkAdditionRequestFDD(ctxId, txId);
    RBS_LOG_DEBUG("IubNbap",
        "[{}] NBAP RL ADDITION APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_ADDITION, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::radioLinkDeletionSHO(RNTI rnti, uint16_t scrCode)
{
    auto ueit = shoLegs_.find(rnti);
    if (ueit == shoLegs_.end() || ueit->second.find(scrCode) == ueit->second.end()) {
        RBS_LOG_WARNING("IubNbap",
            "[{}] radioLinkDeletionSHO: leg rnti={} scrCode={} not found",
            nodeBId_, rnti, scrCode);
        return false;
    }
    ueit->second.erase(scrCode);
    if (ueit->second.empty()) shoLegs_.erase(ueit);
    RBS_LOG_INFO("IubNbap",
        "[{}] NBAP RL DELETION (SHO leg) rnti={} scrCode={}", nodeBId_, rnti, scrCode);

    // TS 25.433 §8.1.6 — reuse RadioLinkDeletionRequest for the leg
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    uint32_t ctxId = (static_cast<uint32_t>(rnti) << 9) | (scrCode & 0x1FF);
    ByteBuffer payload = nbap_encode_RadioLinkDeletionRequest(ctxId, ctxId, txId);
    RBS_LOG_DEBUG("IubNbap",
        "[{}] NBAP RL DELETION (SHO) APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_DELETION, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::dedicatedMeasurementInitiation(RNTI rnti, uint16_t measId)
{
    RBS_LOG_INFO("IubNbap", "[{}] NBAP DEDICATED MEAS INIT rnti={} measId={}",
                 nodeBId_, rnti, measId);
    ByteBuffer payload{
        static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti),
        static_cast<uint8_t>(measId >> 8), static_cast<uint8_t>(measId)
    };
    NBAPMessage msg{NBAPProcedure::DEDICATED_MEAS_INITIATE, nextTxId(), std::move(payload)};
    return sendNbapMsg(msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// DCH / HSDPA extended procedures
// ─────────────────────────────────────────────────────────────────────────────

bool IubNbap::commonTransportChannelSetup(uint16_t cellId,
                                           NBAPCommonChannel channelType)
{
    if (!connected_) {
        RBS_LOG_WARNING("IubNbap", "[{}] commonTransportChannelSetup: not connected", nodeBId_);
        return false;
    }
    RBS_LOG_INFO("IubNbap", "[{}] NBAP COMMON TRANSPORT CH SETUP cellId={} type={}",
                 nodeBId_, cellId, static_cast<int>(channelType));
    // TS 25.433 §8.3.2
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_CommonTransportChannelSetupRequest(
        cellId, channelType, txId);
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP COMMON TRANS CH SETUP APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::COMMON_TRANSPORT_CHANNEL_SETUP, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::radioLinkReconfigurePrepare(RNTI rnti, SF newSf)
{
    auto it = links_.find(rnti);
    if (it == links_.end()) {
        RBS_LOG_WARNING("IubNbap", "[{}] radioLinkReconfigurePrepare: rnti={} not found",
                        nodeBId_, rnti);
        return false;
    }
    RBS_LOG_INFO("IubNbap", "[{}] NBAP RL RECONFIG PREPARE rnti={} newSF={}",
                 nodeBId_, rnti, static_cast<int>(newSf));
    // TS 25.433 §8.1.5 — encode request
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_RadioLinkReconfigurePrepare(
        static_cast<uint32_t>(rnti), newSf, txId);
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP RL RECONFIG PREPARE APER len={}", nodeBId_, payload.size());
    // Simulate RNC ACK: queue a COMMIT response so caller can retrieve it
    {
        std::lock_guard<std::mutex> lk(rxMtx_);
        ByteBuffer ack{static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)};
        rxQueue_.push({NBAPProcedure::RADIO_LINK_RECONFIGURE_COMMIT,
                       static_cast<uint16_t>(nextTxId()), std::move(ack)});
    }
    // Update SF in the link record
    it->second.sf = newSf;
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_RECONFIGURE_PREP, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::radioLinkReconfigureCommit(RNTI rnti)
{
    if (links_.find(rnti) == links_.end()) {
        RBS_LOG_WARNING("IubNbap", "[{}] radioLinkReconfigureCommit: rnti={} not found",
                        nodeBId_, rnti);
        return false;
    }
    RBS_LOG_INFO("IubNbap", "[{}] NBAP RL RECONFIG COMMIT rnti={}", nodeBId_, rnti);
    // TS 25.433 §8.1.6 — encode commit handshake
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_RadioLinkReconfigureCommit(
        static_cast<uint32_t>(rnti), txId);
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP RL RECONFIG COMMIT APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_RECONFIGURE_COMMIT, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::radioLinkSetupHSDPA(RNTI rnti, uint16_t scrCode, uint8_t hsDschCodes)
{
    if (!connected_) {
        RBS_LOG_WARNING("IubNbap", "[{}] radioLinkSetupHSDPA: not connected", nodeBId_);
        return false;
    }
    links_[rnti] = {scrCode, SF::SF16};  // HS-DSCH uses fixed SF16
    RBS_LOG_INFO("IubNbap", "[{}] NBAP RL SETUP HSDPA rnti={} scrCode={} codes={}",
                 nodeBId_, rnti, scrCode, hsDschCodes);
    // TS 25.433 §8.3.15 — RL setup with HS-DSCH MAC-d flow info
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_RadioLinkSetupRequestFDD_HSDPA(
        static_cast<uint32_t>(rnti), hsDschCodes, 300u, txId);
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP RL SETUP HSDPA APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::HS_DSCH_MACD_FLOW_SETUP, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::sendNbapMsg(const NBAPMessage& msg)
{
    const std::string typeStr = "NBAP:" + std::to_string(
        static_cast<int>(msg.procedure));
    if (isBlocked(typeStr)) {
        RBS_LOG_WARNING("IubNbap", "[{}] sendNbapMsg заблокирован: proc=0x{:02X}",
                        nodeBId_, static_cast<uint8_t>(msg.procedure));
        return false;
    }
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP → RNC  proc=0x{:02X} txId={}",
                  nodeBId_, static_cast<uint8_t>(msg.procedure), msg.transactionId);
    pushTrace(true, typeStr,
              "txId=" + std::to_string(msg.transactionId) +
              " len=" + std::to_string(msg.payload.size()));
    return true;  // в симуляции транспорт не нужен
}

bool IubNbap::radioLinkSetupEDCH(RNTI rnti, uint16_t scrCode, EDCHTTI tti)
{
    if (!connected_) {
        RBS_LOG_WARNING("IubNbap", "[{}] radioLinkSetupEDCH: not connected", nodeBId_);
        return false;
    }
    links_[rnti] = {scrCode, SF::SF4};  // E-DPDCH uses SF4 for max UL throughput
    RBS_LOG_INFO("IubNbap", "[{}] NBAP RL SETUP E-DCH rnti={} scrCode={} tti={}ms",
                 nodeBId_, rnti, scrCode,
                 tti == EDCHTTI::TTI_2MS ? 2 : 10);
    // TS 25.433 §8.1.1.3 — RL setup with E-DCH MAC-d flow info
    uint8_t txId = static_cast<uint8_t>(nextTxId());
    ByteBuffer payload = nbap_encode_RadioLinkSetupRequestFDD_EDCH(
        static_cast<uint32_t>(rnti), tti, 4u /*maxBitrateIdx→~2Mbps*/, txId);
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP RL SETUP E-DCH APER len={}", nodeBId_, payload.size());
    NBAPMessage msg{NBAPProcedure::E_DCH_MACD_FLOW_SETUP, txId, std::move(payload)};
    return sendNbapMsg(msg);
}

bool IubNbap::recvNbapMsg(NBAPMessage& msg)
{
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (rxQueue_.empty()) return false;
    msg = rxQueue_.front();
    rxQueue_.pop();
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP ← RNC  proc=0x{:02X}",
                  nodeBId_, static_cast<uint8_t>(msg.procedure));
    pushTrace(false,
              "NBAP:" + std::to_string(static_cast<int>(msg.procedure)),
              "txId=" + std::to_string(msg.transactionId));
    return true;
}

void IubNbap::reconnect()
{
    if (!rncAddr_.empty())
        connect(rncAddr_, rncPort_);
}

std::vector<std::string> IubNbap::injectableProcs() const
{
    return {"NBAP:RESET"};
}

bool IubNbap::injectProcedure(const std::string& proc)
{
    if (proc == "NBAP:RESET") {
        uint8_t txId = static_cast<uint8_t>(nextTxId());
        ByteBuffer payload{0x00, txId};
        NBAPMessage msg{NBAPProcedure::RESET, txId, std::move(payload)};
        return sendNbapMsg(msg);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  IubFp
// ─────────────────────────────────────────────────────────────────────────────

IubFp::IubFp(const std::string& nodeBId)
    : nodeBId_(nodeBId)
{}

bool IubFp::sendDchData(RNTI rnti, uint16_t scrCode, const ByteBuffer& tbs)
{
    RBS_LOG_DEBUG("IubFp", "[{}] FP UL DCH rnti={} scrCode={} len={}",
                  nodeBId_, rnti, scrCode, tbs.size());
    // В симуляции UL-данные «улетают» к RNC — просто логируем
    return true;
}

bool IubFp::receiveDchData(RNTI rnti, uint16_t /*scrCode*/, ByteBuffer& tbs)
{
    std::lock_guard<std::mutex> lk(dlMtx_);
    auto it = dlQueues_.find(rnti);
    if (it == dlQueues_.end() || it->second.empty()) return false;
    tbs = std::move(it->second.front());
    it->second.pop();
    RBS_LOG_DEBUG("IubFp", "[{}] FP DL DCH rnti={} len={}", nodeBId_, rnti, tbs.size());
    return true;
}

void IubFp::reportSyncStatus(RNTI rnti, bool inSync)
{
    RBS_LOG_INFO("IubFp", "[{}] FP SYNC STATUS rnti={} inSync={}",
                 nodeBId_, rnti, inSync ? "ДА" : "НЕТ");
}

} // namespace rbs::umts

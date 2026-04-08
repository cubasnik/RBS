#include "iub_link.h"

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
//  IubNbap
// ─────────────────────────────────────────────────────────────────────────────

IubNbap::IubNbap(const std::string& nodeBId)
    : nodeBId_(nodeBId)
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

    ByteBuffer payload{
        static_cast<uint8_t>(cellId >> 8), static_cast<uint8_t>(cellId),
        static_cast<uint8_t>(primaryScrCode >> 8), static_cast<uint8_t>(primaryScrCode),
        static_cast<uint8_t>(uarfcnDl >> 8), static_cast<uint8_t>(uarfcnDl),
        static_cast<uint8_t>(uarfcnUl >> 8), static_cast<uint8_t>(uarfcnUl)
    };
    NBAPMessage msg{NBAPProcedure::CELL_SETUP, nextTxId(), std::move(payload)};
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

    ByteBuffer payload{
        static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti),
        static_cast<uint8_t>(scrCode >> 8), static_cast<uint8_t>(scrCode),
        static_cast<uint8_t>(sf)
    };
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_SETUP, nextTxId(), std::move(payload)};
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
    RBS_LOG_INFO("IubNbap", "[{}] NBAP RADIO LINK DELETION rnti={}", nodeBId_, rnti);
    ByteBuffer payload{static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)};
    NBAPMessage msg{NBAPProcedure::RADIO_LINK_DELETION, nextTxId(), std::move(payload)};
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

bool IubNbap::sendNbapMsg(const NBAPMessage& msg)
{
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP → RNC  proc=0x{:02X} txId={}",
                  nodeBId_, static_cast<uint8_t>(msg.procedure), msg.transactionId);
    return true;  // в симуляции транспорт не нужен
}

bool IubNbap::recvNbapMsg(NBAPMessage& msg)
{
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (rxQueue_.empty()) return false;
    msg = rxQueue_.front();
    rxQueue_.pop();
    RBS_LOG_DEBUG("IubNbap", "[{}] NBAP ← RNC  proc=0x{:02X}",
                  nodeBId_, static_cast<uint8_t>(msg.procedure));
    return true;
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

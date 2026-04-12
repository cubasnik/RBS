#include "abis_link.h"
#include <sstream>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static const char* omlTypeStr(OMLMsgType t) {
    switch (t) {
        case OMLMsgType::SET_BTS_ATTR:             return "OML:SET_BTS_ATTR";
        case OMLMsgType::GET_BTS_ATTR:             return "OML:GET_BTS_ATTR";
        case OMLMsgType::SET_RADIO_CARRIER_ATTR:   return "OML:SET_RADIO_CARRIER_ATTR";
        case OMLMsgType::CHANNEL_ACTIVATION:       return "OML:CHANNEL_ACTIVATION";
        case OMLMsgType::CHANNEL_ACTIVATION_ACK:   return "OML:CHANNEL_ACTIVATION_ACK";
        case OMLMsgType::CHANNEL_ACTIVATION_NACK:  return "OML:CHANNEL_ACTIVATION_NACK";
        case OMLMsgType::RF_CHAN_REL:               return "OML:RF_CHAN_REL";
        case OMLMsgType::OPSTART:                  return "OML:OPSTART";
        case OMLMsgType::OPSTART_ACK:              return "OML:OPSTART_ACK";
        case OMLMsgType::OPSTART_NACK:             return "OML:OPSTART_NACK";
        case OMLMsgType::FAILURE_EVENT_REPORT:     return "OML:FAILURE_EVENT_REPORT";
        case OMLMsgType::SOFTWARE_ACTIVATE_NOTICE: return "OML:SOFTWARE_ACTIVATE_NOTICE";
        default:                                   return "OML:UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  AbisOml
// ─────────────────────────────────────────────────────────────────────────────

AbisOml::AbisOml(const std::string& btsId)
    : rbs::LinkController("abis-" + btsId)
    , btsId_(btsId)
{}

bool AbisOml::connect(const std::string& bscAddr, uint16_t port)
{
    if (connected_) {
        RBS_LOG_WARNING("AbisOml", "[{}] уже подключён к BSC {}:{}", btsId_, bscAddr_, bscPort_);
        return true;
    }
    bscAddr_   = bscAddr;
    bscPort_   = port;
    connected_ = true;
    RBS_LOG_INFO("AbisOml", "[{}] OML-соединение с BSC {}:{} установлено", btsId_, bscAddr, port);

    // Симуляция: сразу отправляем OPSTART
    AbisMessage opstart{0x00, {}};
    sendOmlMsg(OMLMsgType::OPSTART, opstart);
    return true;
}

void AbisOml::disconnect()
{
    if (!connected_) return;
    connected_ = false;
    RBS_LOG_INFO("AbisOml", "[{}] OML-соединение закрыто", btsId_);
}

bool AbisOml::sendOmlMsg(OMLMsgType type, const AbisMessage& msg)
{
    const char* typeStr = omlTypeStr(type);
    if (isBlocked(typeStr)) {
        RBS_LOG_WARNING("AbisOml", "[{}] sendOmlMsg заблокирован: {}", btsId_, typeStr);
        return false;
    }
    if (!connected_) {
        RBS_LOG_WARNING("AbisOml", "[{}] sendOmlMsg: нет соединения с BSC", btsId_);
        return false;
    }
    RBS_LOG_DEBUG("AbisOml", "[{}] OML → BSC  type=0x{:02X} entity=0x{:02X} len={}",
                  btsId_, static_cast<uint8_t>(type), msg.entity, msg.payload.size());
    pushTrace(true, typeStr, "entity=" + std::to_string(msg.entity) +
                             " len=" + std::to_string(msg.payload.size()));

    // Симуляция автоответа BSC
    if (type == OMLMsgType::OPSTART) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        AbisMessage ack{msg.entity, {}};
        rxQueue_.push({OMLMsgType::OPSTART_ACK, ack});
        RBS_LOG_DEBUG("AbisOml", "[{}] BSC → OML  auto-ACK OPSTART", btsId_);
    }
    if (type == OMLMsgType::CHANNEL_ACTIVATION) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        AbisMessage ack{msg.entity, {}};
        rxQueue_.push({OMLMsgType::CHANNEL_ACTIVATION_ACK, ack});
    }
    return true;
}

bool AbisOml::recvOmlMsg(OMLMsgType& type, AbisMessage& msg)
{
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (rxQueue_.empty()) return false;
    auto& front = rxQueue_.front();
    type = front.type;
    msg  = front.msg;
    rxQueue_.pop();
    const char* typeStr = omlTypeStr(type);
    RBS_LOG_DEBUG("AbisOml", "[{}] OML ← BSC  type=0x{:02X}", btsId_,
                  static_cast<uint8_t>(type));
    pushTrace(false, typeStr, "entity=" + std::to_string(msg.entity));
    return true;
}

void AbisOml::reconnect()
{
    if (!bscAddr_.empty())
        connect(bscAddr_, bscPort_);
}

std::vector<std::string> AbisOml::injectableProcs() const
{
    return {"OML:OPSTART"};
}

bool AbisOml::injectProcedure(const std::string& proc)
{
    if (proc == "OML:OPSTART") {
        AbisMessage msg{0x00, {}};
        if (!connected_) {
            RBS_LOG_INFO("AbisOml", "[{}] injectProcedure: OML:OPSTART in simulation mode", btsId_);
            pushTrace(true, "OML:OPSTART", "entity=0 len=0 SIM");
            std::lock_guard<std::mutex> lk(rxMtx_);
            AbisMessage ack{0x00, {}};
            rxQueue_.push({OMLMsgType::OPSTART_ACK, ack});
            return true;
        }
        return sendOmlMsg(OMLMsgType::OPSTART, msg);
    }
    return false;
}

bool AbisOml::configureTRX(uint8_t trxId, uint16_t arfcn, int8_t txPower_dBm)
{
    trxMap_[trxId] = {arfcn, txPower_dBm};
    RBS_LOG_INFO("AbisOml", "[{}] TRX {} сконфигурирован: ARFCN={} мощность={}dBm",
                 btsId_, trxId, arfcn, txPower_dBm);

    // OML SET_RADIO_CARRIER_ATTR
    ByteBuffer payload{trxId,
                       static_cast<uint8_t>(arfcn >> 8),
                       static_cast<uint8_t>(arfcn & 0xFF),
                       static_cast<uint8_t>(txPower_dBm)};
    AbisMessage msg{trxId, std::move(payload)};
    return sendOmlMsg(OMLMsgType::SET_RADIO_CARRIER_ATTR, msg);
}

void AbisOml::reportHwFailure(uint8_t objectClass, const std::string& cause)
{
    RBS_LOG_ERROR("AbisOml", "[{}] Аппаратный сбой: objectClass=0x{:02X} причина={}",
                  btsId_, objectClass, cause);
    ByteBuffer payload(cause.begin(), cause.end());
    AbisMessage msg{objectClass, std::move(payload)};
    sendOmlMsg(OMLMsgType::FAILURE_EVENT_REPORT, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
//  AbisRsl
// ─────────────────────────────────────────────────────────────────────────────

AbisRsl::AbisRsl(const std::string& btsId)
    : btsId_(btsId)
{}

bool AbisRsl::sendRslMsg(RSLMsgType type, const AbisMessage& msg)
{
    RBS_LOG_DEBUG("AbisRsl", "[{}] RSL → BSC  type=0x{:02X} entity=0x{:02X} len={}",
                  btsId_, static_cast<uint8_t>(type), msg.entity, msg.payload.size());
    return true;  // в симуляции транспорт не нужен
}

bool AbisRsl::recvRslMsg(RSLMsgType& type, AbisMessage& msg)
{
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (rxQueue_.empty()) return false;
    auto& front = rxQueue_.front();
    type = front.type;
    msg  = front.msg;
    rxQueue_.pop();
    RBS_LOG_DEBUG("AbisRsl", "[{}] RSL ← BSC  type=0x{:02X}", btsId_,
                  static_cast<uint8_t>(type));
    return true;
}

bool AbisRsl::activateChannel(uint8_t chanNr, GSMChannelType type,
                               RNTI rnti, uint8_t timingAdvance)
{
    activeChannels_[chanNr] = rnti;
    RBS_LOG_INFO("AbisRsl", "[{}] RSL CHANNEL ACTIVATION chanNr={} rnti={} TA={}",
                 btsId_, chanNr, rnti, timingAdvance);

    // Симуляция: сразу ставим ACK в очередь
    ByteBuffer payload{chanNr};
    AbisMessage ack{chanNr, std::move(payload)};
    {
        std::lock_guard<std::mutex> lk(rxMtx_);
        rxQueue_.push({RSLMsgType::CHANNEL_ACTIVATION_ACK, ack});
    }
    // Отправляем RSL DATA_REQUEST для информирования BSC
    AbisMessage req{chanNr, {static_cast<uint8_t>(type), timingAdvance}};
    sendRslMsg(RSLMsgType::CHANNEL_ACTIVATION, req);
    return true;
}

bool AbisRsl::releaseChannel(uint8_t chanNr)
{
    auto it = activeChannels_.find(chanNr);
    if (it == activeChannels_.end()) {
        RBS_LOG_WARNING("AbisRsl", "[{}] releaseChannel: chanNr={} не активен", btsId_, chanNr);
        return false;
    }
    RBS_LOG_INFO("AbisRsl", "[{}] RSL CHANNEL RELEASE chanNr={} rnti={}",
                 btsId_, chanNr, it->second);
    activeChannels_.erase(it);

    AbisMessage msg{chanNr, {}};
    sendRslMsg(RSLMsgType::CHANNEL_RELEASE, msg);
    return true;
}

bool AbisRsl::sendCipherModeCommand(uint8_t chanNr, uint8_t algorithm,
                                    const ByteBuffer& key)
{
    RBS_LOG_INFO("AbisRsl", "[{}] RSL CIPHER MODE CMD chanNr={} algo={}",
                 btsId_, chanNr, algorithm);
    ByteBuffer payload;
    payload.push_back(chanNr);
    payload.push_back(algorithm);
    payload.insert(payload.end(), key.begin(), key.end());
    AbisMessage msg{chanNr, std::move(payload)};
    sendRslMsg(RSLMsgType::CIPHER_MODE_CMD, msg);

    // Симуляция: BSC немедленно получает CIPHER_MODE_COMPLETE
    {
        std::lock_guard<std::mutex> lk(rxMtx_);
        AbisMessage complete{chanNr, {chanNr, algorithm}};
        rxQueue_.push({RSLMsgType::CIPHER_MODE_COMPLETE, complete});
    }
    return true;
}

void AbisRsl::sendMeasurementResult(RNTI rnti, int8_t rxlev, uint8_t rxqual)
{
    RBS_LOG_DEBUG("AbisRsl", "[{}] RSL MEASUREMENT rnti={} rxlev={} rxqual={}",
                  btsId_, rnti, rxlev, rxqual);
    ByteBuffer payload{static_cast<uint8_t>(rxlev), rxqual};
    AbisMessage msg{static_cast<uint8_t>(rnti & 0xFF), std::move(payload)};
    sendRslMsg(RSLMsgType::MEASUREMENT_RES, msg);
}

bool AbisRsl::forwardHandoverCommand(RNTI rnti, const ByteBuffer& hoCmd)
{
    RBS_LOG_INFO("AbisRsl", "[{}] RSL HANDOVER CMD rnti={} len={}",
                 btsId_, rnti, hoCmd.size());
    AbisMessage msg{static_cast<uint8_t>(rnti & 0xFF), hoCmd};
    sendRslMsg(RSLMsgType::HANDOVER_CMD, msg);
    return true;
}

} // namespace rbs::gsm

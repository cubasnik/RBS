#include "abis_link.h"
#include <sstream>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cctype>

namespace rbs::gsm {

static long long nowEpochMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static const char* healthStatusToStr(uint8_t v)
{
    switch (v) {
        case 2: return "UP";
        case 1: return "DEGRADED";
        default: return "DOWN";
    }
}

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

static const char* rslTypeStr(RSLMsgType t) {
    switch (t) {
        case RSLMsgType::DATA_REQUEST:            return "RSL:DATA_REQUEST";
        case RSLMsgType::DATA_INDICATION:         return "RSL:DATA_INDICATION";
        case RSLMsgType::DATA_CONFIRM:            return "RSL:DATA_CONFIRM";
        case RSLMsgType::ERROR_INDICATION:        return "RSL:ERROR_INDICATION";
        case RSLMsgType::CHANNEL_ACTIVATION:      return "RSL:CHANNEL_ACTIVATION";
        case RSLMsgType::CHANNEL_ACTIVATION_ACK:  return "RSL:CHANNEL_ACTIVATION_ACK";
        case RSLMsgType::CHANNEL_ACTIVATION_NACK: return "RSL:CHANNEL_ACTIVATION_NACK";
        case RSLMsgType::CHANNEL_RELEASE:         return "RSL:CHANNEL_RELEASE";
        case RSLMsgType::RF_CHANNEL_RELEASE:      return "RSL:RF_CHANNEL_RELEASE";
        case RSLMsgType::RF_CHANNEL_RELEASE_ACK:  return "RSL:RF_CHANNEL_RELEASE_ACK";
        case RSLMsgType::MEASUREMENT_RES:         return "RSL:MEASUREMENT_RES";
        case RSLMsgType::HANDOVER_CMD:            return "RSL:HANDOVER_CMD";
        case RSLMsgType::CIPHER_MODE_CMD:         return "RSL:CIPHER_MODE_CMD";
        case RSLMsgType::CIPHER_MODE_COMPLETE:    return "RSL:CIPHER_MODE_COMPLETE";
        case RSLMsgType::CIPHER_MODE_REJECT:      return "RSL:CIPHER_MODE_REJECT";
        case RSLMsgType::PAGING_CMD:              return "RSL:PAGING_CMD";
        default:                                  return "RSL:UNKNOWN";
    }
}

static constexpr uint8_t IPA_FILTER_OML = 0x00;
static constexpr uint8_t IPA_FILTER_RSL = 0x01;

struct InjectSpec {
    std::string base;
    int chan = -1;
    int entity = -1;
    bool hasPayload = false;
    ByteBuffer payload;
};

static InjectSpec parseInjectSpec(const std::string& proc)
{
    InjectSpec out;
    std::stringstream ss(proc);
    std::string token;
    bool first = true;
    while (std::getline(ss, token, ';')) {
        if (first) {
            out.base = token;
            first = false;
            continue;
        }
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = token.substr(0, eq);
        const std::string val = token.substr(eq + 1);
        if (key == "chan") {
            out.chan = static_cast<int>(std::strtol(val.c_str(), nullptr, 0));
        } else if (key == "entity") {
            out.entity = static_cast<int>(std::strtol(val.c_str(), nullptr, 0));
        } else if (key == "payload") {
            out.hasPayload = true;
            out.payload.clear();
            std::stringstream ps(val);
            std::string b;
            while (std::getline(ps, b, ',')) {
                if (b.empty()) continue;
                long v = std::strtol(b.c_str(), nullptr, 0);
                if (v < 0 || v > 255) continue;
                out.payload.push_back(static_cast<uint8_t>(v));
            }
        }
    }
    if (out.base.empty()) out.base = proc;
    return out;
}

static uint8_t clampU8(int v, uint8_t fallback)
{
    if (v < 0 || v > 255) return fallback;
    return static_cast<uint8_t>(v);
}

// ─────────────────────────────────────────────────────────────────────────────
//  AbisOml
// ─────────────────────────────────────────────────────────────────────────────

AbisOml::AbisOml(const std::string& btsId)
    : rbs::LinkController("abis-" + btsId)
    , btsId_(btsId)
    , useRealTransport_(false)
    , tcpSocket_(nullptr)
{}

AbisOml::~AbisOml()
{
    stopHealthMonitor();
    disconnect();
}

void AbisOml::setHealthTiming(uint32_t heartbeatIntervalMs, uint32_t staleRxMs)
{
    heartbeatIntervalMs_.store(heartbeatIntervalMs > 0 ? heartbeatIntervalMs : 1000);
    staleRxMs_.store(staleRxMs > 0 ? staleRxMs : 10000);
}

void AbisOml::setKeepaliveConfig(bool enabled, uint32_t idleMs)
{
    keepaliveEnabled_.store(enabled);
    keepaliveIdleMs_.store(idleMs > 0 ? idleMs : 3000);
}

void AbisOml::setInteropProfile(const std::string& profileName)
{
    if (profileName.empty()) {
        interopProfile_ = "default";
        return;
    }
    std::string p = profileName;
    for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    interopProfile_ = p;
}

bool AbisOml::connect(const std::string& bscAddr, uint16_t port)
{
    if (connected_) {
        RBS_LOG_WARNING("AbisOml", "[{}] уже подключён к BSC {}:{}", btsId_, bscAddr_, bscPort_);
        return true;
    }
    
    bscAddr_   = bscAddr;
    bscPort_   = port;
    lastConnectAttemptEpochMs_.store(nowEpochMs());

    if (useRealTransport_) {
        if (bscAddr.empty()) {
            RBS_LOG_ERROR("AbisOml", "[{}] TCP/IPA mode requires non-empty bsc_addr", btsId_);
            connected_ = false;
            return false;
        }

        tcpSocket_ = std::make_unique<rbs::net::TcpSocket>("AbisOml-" + btsId_);
        if (!tcpSocket_->connect(bscAddr, port)) {
            static constexpr std::array<long long, 4> BACKOFF_MS{1000, 2000, 5000, 10000};
            const auto attempts = reconnectAttempts_.fetch_add(1) + 1;
            const auto idx = std::min<size_t>(attempts - 1, BACKOFF_MS.size() - 1);
            const auto nextAt = nowEpochMs() + BACKOFF_MS[idx];
            nextReconnectEpochMs_.store(nextAt);

            RBS_LOG_ERROR("AbisOml", "[{}] TCP/IPA connect failed to {}:{}", btsId_, bscAddr, port);
            tcpSocket_.reset();
            connected_ = false;
            return false;
        }

        tcpSocket_->startReceive([this](const rbs::net::TcpPacket& pkt) {
            onTcpRxPacket(pkt);
        });

        connected_ = true;
        reconnectAttempts_.store(0);
        nextReconnectEpochMs_.store(0);
        lastConnectEpochMs_.store(nowEpochMs());
        healthStatus_.store(2);
        startHealthMonitor();
        RBS_LOG_INFO("AbisOml", "[{}] OML TCP/IPA connected to {}:{}", btsId_, bscAddr, port);
        return true;
    }

    connected_ = true;
    healthStatus_.store(2);
    RBS_LOG_INFO("AbisOml", "[{}] OML connected to {}:{} (simulation mode)", btsId_, bscAddr, port);

    // Симуляция: сразу отправляем OPSTART
    AbisMessage opstart{0x00, {}};
    sendOmlMsg(OMLMsgType::OPSTART, opstart);
    return true;
}

void AbisOml::disconnect()
{
    stopHealthMonitor();

    if (!connected_ && !tcpSocket_) return;
    
    if (tcpSocket_) {
        tcpSocket_->close();
        tcpSocket_.reset();
    }
    
    connected_ = false;
    nextReconnectEpochMs_.store(0);
    healthStatus_.store(0);
    
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
    omlTxFrames_.fetch_add(1);
    pushTrace(true, typeStr, "entity=" + std::to_string(msg.entity) +
                             " len=" + std::to_string(msg.payload.size()));

    // Соберём OML пакет
    ByteBuffer omlPayload;
    omlPayload.push_back(msg.entity);  // OML entity discriminator
    omlPayload.insert(omlPayload.end(), msg.payload.begin(), msg.payload.end());
    
    if (useRealTransport_) {
        if (!tcpSocket_ || !tcpSocket_->isConnected()) {
            connected_ = false;
            healthStatus_.store(0);
            reconnect();
            if (!tcpSocket_ || !tcpSocket_->isConnected()) {
                RBS_LOG_WARNING("AbisOml", "[{}] TCP/IPA send deferred: transport unavailable", btsId_);
                return false;
            }
        }

        // Отправить через TCP с IPA фреймингом
        ByteBuffer ipaFrame = ipa::encodeFrame(IPA_FILTER_OML, static_cast<uint8_t>(type), omlPayload);
        return tcpSocket_->send(ipaFrame);
    } else {
        // Симуляция: автоответ в очередь
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

bool AbisOml::sendRslMsgInternal(RSLMsgType type, const AbisMessage& msg)
{
    const char* typeStr = rslTypeStr(type);
    if (isBlocked(typeStr)) {
        RBS_LOG_WARNING("AbisOml", "[{}] sendRslMsg заблокирован: {}", btsId_, typeStr);
        return false;
    }
    if (!connected_) {
        RBS_LOG_WARNING("AbisOml", "[{}] sendRslMsg: нет соединения с BSC", btsId_);
        return false;
    }

    RBS_LOG_DEBUG("AbisOml", "[{}] RSL → BSC  type=0x{:02X} chan=0x{:02X} len={}",
                  btsId_, static_cast<uint8_t>(type), msg.entity, msg.payload.size());
    rslTxFrames_.fetch_add(1);
    pushTrace(true, typeStr, "chan=" + std::to_string(msg.entity) +
                             " len=" + std::to_string(msg.payload.size()));

    ByteBuffer rslPayload;
    rslPayload.push_back(msg.entity);
    rslPayload.insert(rslPayload.end(), msg.payload.begin(), msg.payload.end());

    if (useRealTransport_) {
        if (!tcpSocket_ || !tcpSocket_->isConnected()) {
            connected_ = false;
            healthStatus_.store(0);
            reconnect();
            if (!tcpSocket_ || !tcpSocket_->isConnected()) {
                RBS_LOG_WARNING("AbisOml", "[{}] TCP/IPA RSL send deferred: transport unavailable", btsId_);
                return false;
            }
        }

        ByteBuffer ipaFrame = ipa::encodeFrame(IPA_FILTER_RSL, static_cast<uint8_t>(type), rslPayload);
        return tcpSocket_->send(ipaFrame);
    }

    if (type == RSLMsgType::CHANNEL_ACTIVATION) {
        std::lock_guard<std::mutex> lk(rslRxMtx_);
        AbisMessage ack{msg.entity, {}};
        rslRxQueue_.push({RSLMsgType::CHANNEL_ACTIVATION_ACK, ack});
    }
    if (type == RSLMsgType::CHANNEL_RELEASE) {
        std::lock_guard<std::mutex> lk(rslRxMtx_);
        AbisMessage ack{msg.entity, {}};
        rslRxQueue_.push({RSLMsgType::RF_CHANNEL_RELEASE_ACK, ack});
    }
    return true;
}

void AbisOml::onTcpRxPacket(const rbs::net::TcpPacket& pkt)
{
    lastRxEpochMs_.store(nowEpochMs());

    // Парсим IPA фреймы из TCP пакета
    size_t numFrames = ipaParser_.parse(pkt.data);
    
    RBS_LOG_DEBUG("AbisOml", "[{}] TCP RX: {} байт, {} IPA фреймов распарсено", 
                  btsId_, pkt.data.size(), numFrames);
    
    // Обработаем каждый полученный фрейм
    for (size_t i = 0; i < numFrames; ++i) {
        const auto& frame = ipaParser_.frameAt(i);
        
        // frame.payload: entity/channel (1 byte) + protocol payload
        if (frame.payload.empty()) {
            RBS_LOG_WARNING("AbisOml", "[{}] пустой payload в IPA фрейме", btsId_);
            continue;
        }
        
        uint8_t entity = frame.payload[0];
        ByteBuffer protoData(frame.payload.begin() + 1, frame.payload.end());
        AbisMessage msg{entity, protoData};

        if (frame.msgFilter == IPA_FILTER_OML) {
            RBS_LOG_DEBUG("AbisOml", "[{}] IPA OML frame: type=0x{:02X} entity=0x{:02X} len={}",
                          btsId_, frame.msgType, entity, protoData.size());
            omlRxFrames_.fetch_add(1);
            std::lock_guard<std::mutex> lk(rxMtx_);
            rxQueue_.push({static_cast<OMLMsgType>(frame.msgType), msg});
        } else if (frame.msgFilter == IPA_FILTER_RSL) {
            RBS_LOG_DEBUG("AbisOml", "[{}] IPA RSL frame: type=0x{:02X} chan=0x{:02X} len={}",
                          btsId_, frame.msgType, entity, protoData.size());
            rslRxFrames_.fetch_add(1);
            const auto rslType = static_cast<RSLMsgType>(frame.msgType);
            pushTrace(false, rslTypeStr(rslType), "chan=" + std::to_string(entity));
            std::lock_guard<std::mutex> lk(rslRxMtx_);
            rslRxQueue_.push({rslType, msg});
        } else {
            RBS_LOG_WARNING("AbisOml", "[{}] unknown IPA filter=0x{:02X} type=0x{:02X}",
                            btsId_, frame.msgFilter, frame.msgType);
        }
    }
}

void AbisOml::reconnect()
{
    if (!useRealTransport_ || bscAddr_.empty()) return;

    const auto nowMs = nowEpochMs();
    const auto nextAt = nextReconnectEpochMs_.load();
    if (nextAt > 0 && nowMs < nextAt) {
        return;
    }

    connect(bscAddr_, bscPort_);
}

void AbisOml::startHealthMonitor()
{
    if (monitorRunning_.exchange(true)) return;
    monitorThread_ = std::thread([this]() { healthMonitorLoop(); });
}

void AbisOml::stopHealthMonitor()
{
    if (!monitorRunning_.exchange(false)) return;
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

void AbisOml::healthMonitorLoop()
{
    while (monitorRunning_.load()) {
        const auto sleepMs = heartbeatIntervalMs_.load();
        const auto nowMs = nowEpochMs();

        if (!useRealTransport_) {
            healthStatus_.store(2);
        } else {
            const bool sockUp = (tcpSocket_ && tcpSocket_->isConnected());
            if (!sockUp) {
                connected_ = false;
                healthStatus_.store(0);
                reconnect();
            } else {
                connected_ = true;
                const auto lastRx = lastRxEpochMs_.load();
                const auto stale = staleRxMs_.load();
                const auto keepaliveIdle = keepaliveIdleMs_.load();
                const auto refTs = (lastRx > 0) ? lastRx : lastConnectEpochMs_.load();

                if (keepaliveEnabled_.load() && refTs > 0 && keepaliveIdle > 0 && nowMs - refTs > keepaliveIdle) {
                    if (!sendKeepaliveProbe()) {
                        keepaliveFailCount_.fetch_add(1);
                        connected_ = false;
                        healthStatus_.store(0);
                        reconnect();
                    }
                }

                if (lastRx > 0 && stale > 0 && nowMs - lastRx > stale) {
                    healthStatus_.store(1);
                } else {
                    healthStatus_.store(2);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

bool AbisOml::sendKeepaliveProbe()
{
    if (!useRealTransport_ || !tcpSocket_ || !tcpSocket_->isConnected()) {
        return false;
    }

    // B++ active keepalive: OML GET_BTS_ATTR with empty body, entity=0.
    ByteBuffer omlPayload;
    omlPayload.push_back(0x00); // entity discriminator
    ByteBuffer frame = ipa::encodeFrame(0x00, static_cast<uint8_t>(OMLMsgType::GET_BTS_ATTR), omlPayload);

    const bool ok = tcpSocket_->send(frame);
    if (ok) {
        lastKeepaliveTxEpochMs_.store(nowEpochMs());
        keepaliveTxCount_.fetch_add(1);
        pushTrace(true, "OML:KEEPALIVE", "probe=GET_BTS_ATTR entity=0 len=0");
    }
    return ok;
}

std::string AbisOml::healthJson() const
{
    std::ostringstream j;
    j << "\"mode\":" << (useRealTransport_ ? "\"ipa_tcp\"" : "\"sim\"")
            << ",\"interopProfile\":" << "\"" << interopProfile_ << "\""
      << ",\"healthStatus\":" << "\"" << healthStatusToStr(healthStatus_.load()) << "\""
      << ",\"reconnectAttempts\":" << reconnectAttempts_.load()
      << ",\"lastRxEpochMs\":" << lastRxEpochMs_.load()
      << ",\"lastConnectEpochMs\":" << lastConnectEpochMs_.load()
      << ",\"lastConnectAttemptEpochMs\":" << lastConnectAttemptEpochMs_.load()
      << ",\"nextReconnectEpochMs\":" << nextReconnectEpochMs_.load()
      << ",\"heartbeatIntervalMs\":" << heartbeatIntervalMs_.load()
            << ",\"staleRxMs\":" << staleRxMs_.load()
            << ",\"keepaliveEnabled\":" << (keepaliveEnabled_.load() ? "true" : "false")
            << ",\"keepaliveIdleMs\":" << keepaliveIdleMs_.load()
            << ",\"keepaliveTxCount\":" << keepaliveTxCount_.load()
            << ",\"keepaliveFailCount\":" << keepaliveFailCount_.load()
            << ",\"lastKeepaliveTxEpochMs\":" << lastKeepaliveTxEpochMs_.load()
            << ",\"omlTxFrames\":" << omlTxFrames_.load()
            << ",\"omlRxFrames\":" << omlRxFrames_.load()
            << ",\"rslTxFrames\":" << rslTxFrames_.load()
            << ",\"rslRxFrames\":" << rslRxFrames_.load();
    return j.str();
}

std::vector<std::string> AbisOml::injectableProcs() const
{
    return {
        "OML:OPSTART",
        "RSL:CHANNEL_ACTIVATION",
        "RSL:CHANNEL_RELEASE",
        "RSL:PAGING_CMD"
    };
}

bool AbisOml::injectProcedure(const std::string& proc)
{
    const InjectSpec spec = parseInjectSpec(proc);

    if (spec.base == "OML:OPSTART") {
        AbisMessage msg{0x00, {}};
        if (!useRealTransport_) {
            RBS_LOG_INFO("AbisOml", "[{}] injectProcedure: OML:OPSTART в режиме симуляции", btsId_);
            pushTrace(true, "OML:OPSTART", "entity=0 len=0 SIM");
            std::lock_guard<std::mutex> lk(rxMtx_);
            AbisMessage ack{0x00, {}};
            rxQueue_.push({OMLMsgType::OPSTART_ACK, ack});
            return true;
        }
        return sendOmlMsg(OMLMsgType::OPSTART, msg);
    }

    const uint8_t chan = clampU8(spec.chan, 0x01);
    const uint8_t entity = clampU8(spec.entity, chan);

    if (spec.base == "RSL:CHANNEL_ACTIVATION") {
        ByteBuffer payload = spec.hasPayload ? spec.payload : ByteBuffer{0x01, 0x00};
        AbisMessage msg{entity, std::move(payload)};
        return sendRslMsgInternal(RSLMsgType::CHANNEL_ACTIVATION, msg);
    }
    if (spec.base == "RSL:CHANNEL_RELEASE") {
        ByteBuffer payload = spec.hasPayload ? spec.payload : ByteBuffer{};
        AbisMessage msg{entity, std::move(payload)};
        return sendRslMsgInternal(RSLMsgType::CHANNEL_RELEASE, msg);
    }
    if (spec.base == "RSL:PAGING_CMD") {
        ByteBuffer payload = spec.hasPayload ? spec.payload : ByteBuffer{0x21, 0x43};
        AbisMessage msg{entity, std::move(payload)};
        return sendRslMsgInternal(RSLMsgType::PAGING_CMD, msg);
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

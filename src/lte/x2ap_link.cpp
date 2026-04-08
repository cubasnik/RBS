#include "x2ap_link.h"

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
//  X2APLink
// ─────────────────────────────────────────────────────────────────────────────

X2APLink::X2APLink(const std::string& enbId)
    : enbId_(enbId)
{}

bool X2APLink::connect(uint32_t targetEnbId, const std::string& addr, uint16_t port)
{
    auto& p = peers_[targetEnbId];
    if (p.connected) {
        RBS_LOG_WARNING("X2AP", "[{}] уже подключён к eNB 0x{:X}", enbId_, targetEnbId);
        return true;
    }
    p = {addr, port, true};
    RBS_LOG_INFO("X2AP", "[{}] X2AP-соединение с eNB 0x{:X} {}:{} установлено",
                 enbId_, targetEnbId, addr, port);
    return true;
}

void X2APLink::disconnect(uint32_t targetEnbId)
{
    auto it = peers_.find(targetEnbId);
    if (it == peers_.end() || !it->second.connected) return;
    it->second.connected = false;
    RBS_LOG_INFO("X2AP", "[{}] X2AP-соединение с eNB 0x{:X} закрыто", enbId_, targetEnbId);
}

bool X2APLink::isConnected(uint32_t targetEnbId) const
{
    auto it = peers_.find(targetEnbId);
    return it != peers_.end() && it->second.connected;
}

bool X2APLink::x2Setup(uint32_t localEnbId, uint32_t targetEnbId)
{
    if (!isConnected(targetEnbId)) {
        RBS_LOG_ERROR("X2AP", "[{}] x2Setup: нет соединения с eNB 0x{:X}", enbId_, targetEnbId);
        return false;
    }
    RBS_LOG_INFO("X2AP", "[{}] X2 SETUP local=0x{:X} target=0x{:X}",
                 enbId_, localEnbId, targetEnbId);

    ByteBuffer payload{
        static_cast<uint8_t>(localEnbId >> 24), static_cast<uint8_t>(localEnbId >> 16),
        static_cast<uint8_t>(localEnbId >> 8),  static_cast<uint8_t>(localEnbId)
    };
    X2APMessage msg{X2APProcedure::X2_SETUP, 0, 0, std::move(payload)};
    bool ok = sendX2APMsg(msg);

    // Симуляция: target eNB сразу отвечает X2 Setup Response
    if (ok) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        ByteBuffer resp{
            static_cast<uint8_t>(targetEnbId >> 24), static_cast<uint8_t>(targetEnbId >> 16),
            static_cast<uint8_t>(targetEnbId >> 8),  static_cast<uint8_t>(targetEnbId)
        };
        rxQueue_.push({X2APProcedure::X2_SETUP_RESPONSE, 0, 0, std::move(resp)});
    }
    return ok;
}

bool X2APLink::handoverRequest(const X2HORequest& req)
{
    if (!isConnected(req.targetCellId >> 8)) {
        // targetCellId верхние биты = enbId; если нет прямого соединения — ошибка
        RBS_LOG_ERROR("X2AP", "[{}] handoverRequest: нет X2 соединения для rnti={}",
                       enbId_, req.rnti);
        return false;
    }
    uint32_t srcId = nextSrcX2Id_++;
    hoIds_[req.rnti] = srcId;
    RBS_LOG_INFO("X2AP",
                 "[{}] X2AP HO REQUEST rnti={} srcCell=0x{:X} targetCell=0x{:X} erabs={}",
                 enbId_, req.rnti, req.sourceEnbId, req.targetCellId, req.erabs.size());

    ByteBuffer payload{
        static_cast<uint8_t>(req.rnti >> 8), static_cast<uint8_t>(req.rnti),
        static_cast<uint8_t>(req.causeType)
    };
    for (const auto& e : req.erabs) payload.push_back(e.erabId);
    payload.insert(payload.end(), req.rrcContainer.begin(), req.rrcContainer.end());

    X2APMessage msg{X2APProcedure::HANDOVER_REQUEST, srcId, 0, std::move(payload)};
    bool ok = sendX2APMsg(msg);

    // Симуляция: target eNB немедленно принимает все E-RABs
    if (ok) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        ByteBuffer ackPayload{static_cast<uint8_t>(req.rnti >> 8), static_cast<uint8_t>(req.rnti)};
        for (const auto& e : req.erabs) ackPayload.push_back(e.erabId);
        uint32_t tgtId = 0x200 + req.rnti;
        rxQueue_.push({X2APProcedure::HANDOVER_REQUEST_ACK, srcId, tgtId,
                       std::move(ackPayload)});
    }
    return ok;
}

bool X2APLink::handoverRequestAck(const X2HORequestAck& ack)
{
    RBS_LOG_INFO("X2AP", "[{}] X2AP HO REQUEST ACK rnti={} admitted={}",
                 enbId_, ack.rnti, ack.admittedErabs.size());
    ByteBuffer payload{static_cast<uint8_t>(ack.rnti >> 8), static_cast<uint8_t>(ack.rnti)};
    for (const auto& e : ack.admittedErabs)      payload.push_back(e.erabId);
    for (uint8_t id : ack.notAdmittedErabIds)    payload.push_back(id);
    payload.insert(payload.end(), ack.rrcContainer.begin(), ack.rrcContainer.end());

    uint32_t srcId = hoIds_.count(ack.rnti) ? hoIds_[ack.rnti] : 0;
    X2APMessage msg{X2APProcedure::HANDOVER_REQUEST_ACK, srcId, 0, std::move(payload)};
    return sendX2APMsg(msg);
}

bool X2APLink::handoverPreparationFailure(RNTI rnti, const std::string& cause)
{
    RBS_LOG_WARNING("X2AP", "[{}] X2AP HO PREP FAILURE rnti={} причина={}", enbId_, rnti, cause);
    ByteBuffer payload(cause.begin(), cause.end());
    uint32_t srcId = hoIds_.count(rnti) ? hoIds_[rnti] : 0;
    X2APMessage msg{X2APProcedure::HANDOVER_PREPARATION_FAILURE, srcId, 0, std::move(payload)};
    return sendX2APMsg(msg);
}

bool X2APLink::handoverCancel(RNTI rnti, const std::string& cause)
{
    RBS_LOG_INFO("X2AP", "[{}] X2AP HO CANCEL rnti={} причина={}", enbId_, rnti, cause);
    ByteBuffer payload(cause.begin(), cause.end());
    uint32_t srcId = hoIds_.count(rnti) ? hoIds_[rnti] : 0;
    hoIds_.erase(rnti);
    X2APMessage msg{X2APProcedure::HANDOVER_CANCEL, srcId, 0, std::move(payload)};
    return sendX2APMsg(msg);
}

bool X2APLink::snStatusTransfer(RNTI rnti, const std::vector<SNStatusItem>& items)
{
    RBS_LOG_INFO("X2AP", "[{}] X2AP SN STATUS TRANSFER rnti={} drbs={}",
                 enbId_, rnti, items.size());
    ByteBuffer payload{static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)};
    for (const auto& it : items) {
        payload.push_back(it.drbId);
        payload.push_back(static_cast<uint8_t>(it.ulPdcpSN >> 8));
        payload.push_back(static_cast<uint8_t>(it.ulPdcpSN));
        payload.push_back(static_cast<uint8_t>(it.dlPdcpSN >> 8));
        payload.push_back(static_cast<uint8_t>(it.dlPdcpSN));
    }
    uint32_t srcId = hoIds_.count(rnti) ? hoIds_[rnti] : 0;
    X2APMessage msg{X2APProcedure::SN_STATUS_TRANSFER, srcId, 0, std::move(payload)};
    return sendX2APMsg(msg);
}

bool X2APLink::ueContextRelease(RNTI rnti)
{
    RBS_LOG_INFO("X2AP", "[{}] X2AP UE CONTEXT RELEASE rnti={}", enbId_, rnti);
    hoIds_.erase(rnti);
    ByteBuffer payload{static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)};
    X2APMessage msg{X2APProcedure::UE_CONTEXT_RELEASE, 0, 0, std::move(payload)};
    return sendX2APMsg(msg);
}

bool X2APLink::loadIndication(uint32_t targetEnbId,
                               uint8_t dlPrbOccupancy, uint8_t ulPrbOccupancy)
{
    RBS_LOG_DEBUG("X2AP", "[{}] X2AP LOAD INDICATION → eNB 0x{:X} DL={}% UL={}%",
                  enbId_, targetEnbId, dlPrbOccupancy, ulPrbOccupancy);
    ByteBuffer payload{
        static_cast<uint8_t>(targetEnbId >> 8), static_cast<uint8_t>(targetEnbId),
        dlPrbOccupancy, ulPrbOccupancy
    };
    X2APMessage msg{X2APProcedure::LOAD_INDICATION, 0, 0, std::move(payload)};
    return sendX2APMsg(msg);
}

bool X2APLink::sendX2APMsg(const X2APMessage& msg)
{
    RBS_LOG_DEBUG("X2AP", "[{}] X2AP → eNB  proc=0x{:02X} srcId=0x{:X}",
                  enbId_, static_cast<uint8_t>(msg.procedure), msg.sourceEnbUeX2apId);
    return true;  // симуляция
}

bool X2APLink::recvX2APMsg(X2APMessage& msg)
{
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (rxQueue_.empty()) return false;
    msg = rxQueue_.front();
    rxQueue_.pop();
    RBS_LOG_DEBUG("X2AP", "[{}] X2AP ← eNB  proc=0x{:02X}",
                  enbId_, static_cast<uint8_t>(msg.procedure));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  X2ULink
// ─────────────────────────────────────────────────────────────────────────────

X2ULink::X2ULink(const std::string& enbId)
    : enbId_(enbId)
    , socket_("X2U-" + enbId)
{}

bool X2ULink::openForwardingTunnel(RNTI rnti, const std::string& targetAddr, uint32_t teid)
{
    // Лениво привязываем сокет (порт выдаёт ОС) при открытии первого туннеля
    if (!socketReady_) {
        if (!socket_.bind(0)) {
            RBS_LOG_ERROR("X2U", "[{}] openForwardingTunnel: UDP bind ошибка", enbId_);
            return false;
        }
        // Для X2-U входящих пакетов не ждём — только отправка
        socketReady_ = true;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        tunnels_[rnti] = {targetAddr, teid};
    }
    RBS_LOG_INFO("X2U", "[{}] GTP-U forwarding tunnel открыт rnti={} target={} teid=0x{:X}",
                 enbId_, rnti, targetAddr, teid);
    return true;
}

bool X2ULink::forwardPacket(RNTI rnti, const ByteBuffer& pdcpPdu)
{
    ForwardTunnel t;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = tunnels_.find(rnti);
        if (it == tunnels_.end()) {
            RBS_LOG_ERROR("X2U", "[{}] forwardPacket: нет туннеля для rnti={}", enbId_, rnti);
            return false;
        }
        t = it->second;
    }

    ByteBuffer frame = gtpuEncode(t.teid, pdcpPdu);
    bool ok = socket_.send(t.targetAddr, GTPU_PORT, frame);
    RBS_LOG_DEBUG("X2U", "[{}] X2-U пересылка rnti={} → {} teid=0x{:X} len={} {}",
                  enbId_, rnti, t.targetAddr, t.teid, pdcpPdu.size(), ok ? "OK" : "FAIL");
    return ok;
}

void X2ULink::closeForwardingTunnel(RNTI rnti)
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        tunnels_.erase(rnti);
    }
    if (socketReady_ && tunnels_.empty()) {
        socket_.close();
        socketReady_ = false;
    }
    RBS_LOG_INFO("X2U", "[{}] GTP-U forwarding tunnel закрыт rnti={}", enbId_, rnti);
}

} // namespace rbs::lte

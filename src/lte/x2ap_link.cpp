#include "x2ap_link.h"
#include "x2ap_codec.h"
#include "../oms/oms.h"

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
//  X2APLink
// ─────────────────────────────────────────────────────────────────────────────

X2APLink::X2APLink(const std::string& enbId)
    : enbId_(enbId)
    , socket_("X2AP-" + enbId)
{}

bool X2APLink::connect(uint32_t targetEnbId, const std::string& addr, uint16_t port, uint16_t localPort)
{
    auto& p = peers_[targetEnbId];
    if (p.connected) {
        RBS_LOG_WARNING("X2AP", "[{}] уже подключён к eNB 0x{:X}", enbId_, targetEnbId);
        return true;
    }
    p = {addr, port, true};

    if (!socketReady_) {
        net::UdpSocket::wsaInit();
        if (!socket_.bind(localPort)) {
            RBS_LOG_ERROR("X2AP", "[{}] connect: UDP bind failed", enbId_);
            p.connected = false;
            return false;
        }
        socket_.startReceive([this](const net::UdpPacket& pkt) { onRxPacket(pkt); });
        socketReady_ = true;
    }

    RBS_LOG_INFO("X2AP", "[{}] X2AP-соединение с eNB 0x{:X} {}:{} установлено (UDP port {})",
                 enbId_, targetEnbId, addr, port, socket_.localPort());
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

    // TS 36.423 §8.3.4 — encode X2 Setup Request with APER
    // Default PLMN 208-01, PCI=1, EARFCN DL=1850 (Band 3), TAC=1
    ByteBuffer payload = x2ap_encode_X2SetupRequest(
                             localEnbId, 0x208001u, 1, 1850, 1);
    if (payload.empty()) {
        RBS_LOG_ERROR("X2AP", "[{}] x2Setup: encode failed", enbId_);
        return false;
    }
    X2APMessage msg{X2APProcedure::X2_SETUP, 0, 0, std::move(payload)};
    bool ok = sendX2APMsg(msg);

    // Симуляция: target eNB сразу отвечает X2 Setup Response
    if (ok) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        ByteBuffer resp = x2ap_encode_X2SetupResponse(
                              targetEnbId, 0x208001u, 1, 1850, 1);
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

    // TS 36.423 §8.5.1 — Handover Request (APER encoded)
    // mmeUeS1apId proxied from RNTI in simulator; plmnId=208-01
    ByteBuffer payload = x2ap_encode_HandoverRequest(
                             srcId, static_cast<uint32_t>(req.rnti),
                             req.causeType, 0x208001u,
                             req.erabs, req.rrcContainer);
    if (payload.empty()) {
        RBS_LOG_ERROR("X2AP", "[{}] handoverRequest: encode failed rnti={}",
                      enbId_, req.rnti);
        return false;
    }
    X2APMessage msg{X2APProcedure::HANDOVER_REQUEST, srcId, 0, std::move(payload)};
    bool ok = sendX2APMsg(msg);

    if (ok) {
        // Track HO attempt for KPI aggregation
        auto& oms = rbs::oms::OMS::instance();
        const double att = oms.getCounter("lte.ho.attempts") + 1.0;
        const double suc = oms.getCounter("lte.ho.successes");
        oms.updateCounter("lte.ho.attempts", att);
        oms.updateCounter("lte.ho.successRate.pct",
                          att > 0 ? suc / att * 100.0 : 100.0, "%");
    }

    // Симуляция: target eNB немедленно принимает все E-RABs
    if (ok) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        uint32_t tgtId = 0x200 + req.rnti;
        ByteBuffer ackPayload = x2ap_encode_HandoverRequestAck(
                                    srcId, tgtId,
                                    req.erabs,       // all admitted
                                    {},              // none refused
                                    req.rrcContainer);
        rxQueue_.push({X2APProcedure::HANDOVER_REQUEST_ACK, srcId, tgtId,
                       std::move(ackPayload)});
    }
    return ok;
}

bool X2APLink::handoverRequestAck(const X2HORequestAck& ack)
{
    RBS_LOG_INFO("X2AP", "[{}] X2AP HO REQUEST ACK rnti={} admitted={}",
                 enbId_, ack.rnti, ack.admittedErabs.size());

    uint32_t srcId = hoIds_.count(ack.rnti) ? hoIds_[ack.rnti] : 0;
    uint32_t tgtId = nextSrcX2Id_++;

    // TS 36.423 §8.5.1 — Handover Request Acknowledge (APER encoded)
    ByteBuffer payload = x2ap_encode_HandoverRequestAck(
                             srcId, tgtId,
                             ack.admittedErabs, ack.notAdmittedErabIds,
                             ack.rrcContainer);
    if (payload.empty()) {
        RBS_LOG_ERROR("X2AP", "[{}] handoverRequestAck: encode failed rnti={}",
                      enbId_, ack.rnti);
        return false;
    }
    X2APMessage msg{X2APProcedure::HANDOVER_REQUEST_ACK, srcId, tgtId,
                    std::move(payload)};
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

    uint32_t srcId = hoIds_.count(rnti) ? hoIds_[rnti] : 0;

    // TS 36.423 §8.5.3 — SN Status Transfer (APER encoded)
    ByteBuffer payload = x2ap_encode_SNStatusTransfer(srcId, 0, items);
    if (payload.empty()) {
        RBS_LOG_ERROR("X2AP", "[{}] snStatusTransfer: encode failed rnti={}",
                      enbId_, rnti);
        return false;
    }
    X2APMessage msg{X2APProcedure::SN_STATUS_TRANSFER, srcId, 0, std::move(payload)};
    return sendX2APMsg(msg);
}

bool X2APLink::ueContextRelease(RNTI rnti)
{
    RBS_LOG_INFO("X2AP", "[{}] X2AP UE CONTEXT RELEASE rnti={}", enbId_, rnti);

    uint32_t srcId = hoIds_.count(rnti) ? hoIds_[rnti] : 0;
    hoIds_.erase(rnti);

    // TS 36.423 §8.5.5 — UE Context Release (APER encoded)
    ByteBuffer payload = x2ap_encode_UEContextRelease(srcId, 0);
    if (payload.empty()) {
        RBS_LOG_ERROR("X2AP", "[{}] ueContextRelease: encode failed rnti={}",
                      enbId_, rnti);
        return false;
    }
    X2APMessage msg{X2APProcedure::UE_CONTEXT_RELEASE, srcId, 0, std::move(payload)};
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
    // Find the peer address by matching hoIds / last-known peer
    // Use first connected peer as default target if no exact routing
    std::string targetAddr;
    uint16_t    targetPort = 0;
    for (const auto& [id, peer] : peers_) {
        if (peer.connected) { targetAddr = peer.addr; targetPort = peer.port; break; }
    }
    if (targetAddr.empty() || !socketReady_) {
        RBS_LOG_DEBUG("X2AP", "[{}] X2AP → eNB  proc=0x{:02X} (no connected peer, dropped)",
                      enbId_, static_cast<uint8_t>(msg.procedure));
        return false;
    }

    // Frame: [proc:1][srcId:4][tgtId:4][payloadLen:4][payload...]
    const auto& pl = msg.payload;
    const uint32_t plLen = static_cast<uint32_t>(pl.size());
    ByteBuffer frame;
    frame.reserve(13 + plLen);
    frame.push_back(static_cast<uint8_t>(msg.procedure));
    frame.push_back(static_cast<uint8_t>(msg.sourceEnbUeX2apId >> 24));
    frame.push_back(static_cast<uint8_t>(msg.sourceEnbUeX2apId >> 16));
    frame.push_back(static_cast<uint8_t>(msg.sourceEnbUeX2apId >>  8));
    frame.push_back(static_cast<uint8_t>(msg.sourceEnbUeX2apId));
    frame.push_back(static_cast<uint8_t>(msg.targetEnbUeX2apId >> 24));
    frame.push_back(static_cast<uint8_t>(msg.targetEnbUeX2apId >> 16));
    frame.push_back(static_cast<uint8_t>(msg.targetEnbUeX2apId >>  8));
    frame.push_back(static_cast<uint8_t>(msg.targetEnbUeX2apId));
    frame.push_back(static_cast<uint8_t>(plLen >> 24));
    frame.push_back(static_cast<uint8_t>(plLen >> 16));
    frame.push_back(static_cast<uint8_t>(plLen >>  8));
    frame.push_back(static_cast<uint8_t>(plLen));
    frame.insert(frame.end(), pl.begin(), pl.end());

    const std::string traceId = !msg.traceId.empty() ? msg.traceId : rbs::Logger::instance().traceId();
    const uint8_t traceLen = static_cast<uint8_t>(std::min<size_t>(traceId.size(), 64));
    frame.push_back(0x54); // 'T'
    frame.push_back(0x52); // 'R'
    frame.push_back(traceLen);
    if (traceLen > 0) {
        frame.insert(frame.end(), traceId.begin(), traceId.begin() + traceLen);
    }

    bool ok = socket_.send(targetAddr, targetPort, frame);    if (ok && pcap_.isOpen())
        pcap_.writeUdp("127.0.0.1", socket_.localPort(),
                       targetAddr, targetPort, frame);    RBS_LOG_DEBUG("X2AP", "[{}] X2AP → eNB  proc=0x{:02X} len={} {}",
                  enbId_, static_cast<uint8_t>(msg.procedure), plLen,
                  ok ? "OK" : "FAIL");
    return ok;
}

bool X2APLink::recvX2APMsg(X2APMessage& msg)
{
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (rxQueue_.empty()) return false;
    msg = rxQueue_.front();
    rxQueue_.pop();
    if (!msg.traceId.empty()) {
        rbs::Logger::instance().setTraceId(msg.traceId);
    }
    RBS_LOG_DEBUG("X2AP", "[{}] X2AP ← eNB  proc=0x{:02X}",
                  enbId_, static_cast<uint8_t>(msg.procedure));
    return true;
}
void X2APLink::enablePcap(const std::string& path)
{
    pcap_.open(path);
}
void X2APLink::onRxPacket(const net::UdpPacket& pkt)
{
    if (pkt.data.size() < 13) return;
    const auto& d = pkt.data;
    const auto     proc  = static_cast<X2APProcedure>(d[0]);
    const uint32_t srcId = (static_cast<uint32_t>(d[1]) << 24)
                          | (static_cast<uint32_t>(d[2]) << 16)
                          | (static_cast<uint32_t>(d[3]) <<  8)
                          |  static_cast<uint32_t>(d[4]);
    const uint32_t tgtId = (static_cast<uint32_t>(d[5]) << 24)
                          | (static_cast<uint32_t>(d[6]) << 16)
                          | (static_cast<uint32_t>(d[7]) <<  8)
                          |  static_cast<uint32_t>(d[8]);
    const uint32_t plLen = (static_cast<uint32_t>(d[9]) << 24)
                          | (static_cast<uint32_t>(d[10]) << 16)
                          | (static_cast<uint32_t>(d[11]) <<  8)
                          |  static_cast<uint32_t>(d[12]);
    if (plLen > pkt.data.size() - 13) return;
    ByteBuffer payload(d.begin() + 13, d.begin() + 13 + plLen);

    std::string traceId;
    const size_t trailerOffset = 13 + plLen;
    if (pkt.data.size() >= trailerOffset + 3 &&
        pkt.data[trailerOffset] == 0x54 && pkt.data[trailerOffset + 1] == 0x52) {
        const size_t traceLen = pkt.data[trailerOffset + 2];
        if (pkt.data.size() >= trailerOffset + 3 + traceLen && traceLen > 0) {
            traceId.assign(reinterpret_cast<const char*>(pkt.data.data() + trailerOffset + 3), traceLen);
        }
    }

    const std::string rxTrace = traceId.empty()
        ? rbs::Logger::makeTraceId("x2ap-rx", srcId)
        : traceId;
    RBS_TRACE_SCOPE(rxTrace);
    RBS_LOG_DEBUG("X2AP", "[{}] X2AP ← eNB  proc=0x{:02X} srcId=0x{:X} len={}",
                  enbId_, static_cast<uint8_t>(proc), srcId, plLen);
    // Track HO success for KPI aggregation
    if (proc == X2APProcedure::HANDOVER_REQUEST_ACK) {
        auto& oms = rbs::oms::OMS::instance();
        const double suc      = oms.getCounter("lte.ho.successes") + 1.0;
        const double attempts = oms.getCounter("lte.ho.attempts");
        oms.updateCounter("lte.ho.successes", suc);
        oms.updateCounter("lte.ho.successRate.pct",
                          attempts > 0 ? suc / attempts * 100.0 : 100.0, "%");
    }
    if (pcap_.isOpen())
        pcap_.writeUdp(pkt.srcIp, pkt.srcPort,
                       "127.0.0.1", socket_.localPort(), pkt.data);    std::lock_guard<std::mutex> lk(rxMtx_);
    rxQueue_.push({proc, srcId, tgtId, std::move(payload), traceId});
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

// ─────────────────────────────────────────────────────────────────────────────
// EN-DC SgNB procedures (TS 37.340 / TS 36.423 §8.x)
// All methods operate in-memory (simulation); real deployments would encode
// ASN.1 APER payloads and send over SCTP.
// ─────────────────────────────────────────────────────────────────────────────

static const char* endcOptionStr(rbs::ENDCOption opt) {
    switch (opt) {
        case rbs::ENDCOption::OPTION_3:  return "3 (split-MN)";
        case rbs::ENDCOption::OPTION_3A: return "3a (SCG)";
        case rbs::ENDCOption::OPTION_3X: return "3x (split-SN)";
    }
    return "?";
}

bool X2APLink::sgNBAdditionRequest(RNTI rnti, rbs::ENDCOption option,
                                   const std::vector<rbs::DCBearerConfig>& bearers)
{
    RBS_LOG_INFO("X2AP", "[", enbId_, "] SgNB Addition Request  rnti=", rnti,
                 " option=", endcOptionStr(option), "  bearers=", bearers.size());

    endcMap_[rnti] = {option, bearers};

    X2APMessage msg{X2APProcedure::SGNB_ADDITION_REQUEST,
                    nextSrcX2Id_++, 0, {}};
    msg.payload = {
        static_cast<uint8_t>(rnti >> 8),
        static_cast<uint8_t>(rnti),
        static_cast<uint8_t>(option),
        static_cast<uint8_t>(bearers.size())
    };
    return sendX2APMsg(msg);
}

bool X2APLink::sgNBAdditionRequestAck(RNTI rnti,
                                       std::vector<rbs::DCBearerConfig>& bearers)
{
    auto it = endcMap_.find(rnti);
    if (it == endcMap_.end()) {
        RBS_LOG_ERROR("X2AP", "[", enbId_, "] SgNB Addition Ack: unknown rnti=", rnti);
        return false;
    }

    // Simulate SN assigning NR C-RNTIs: first assigned ID = 0x0101 + index
    uint16_t snCrntiBase = 0x0101;
    for (size_t i = 0; i < bearers.size(); ++i) {
        bearers[i].snCrnti = static_cast<uint16_t>(snCrntiBase + i);
    }
    it->second.bearers = bearers;

    RBS_LOG_INFO("X2AP", "[", enbId_, "] SgNB Addition Ack  rnti=", rnti,
                 " bearers=", bearers.size(),
                 " snCrnti=0x", std::hex,
                 (bearers.empty() ? 0u : static_cast<unsigned>(bearers.front().snCrnti)),
                 std::dec);

    X2APMessage msg{X2APProcedure::SGNB_ADDITION_REQUEST_ACK,
                    nextSrcX2Id_++, 0, {}};
    msg.payload = {static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)};
    return sendX2APMsg(msg);
}

bool X2APLink::sgNBAdditionRequestReject(RNTI rnti, const std::string& cause)
{
    RBS_LOG_WARNING("X2AP", "[", enbId_, "] SgNB Addition Reject  rnti=", rnti,
                    "  cause=", cause);
    endcMap_.erase(rnti);

    X2APMessage msg{X2APProcedure::SGNB_ADDITION_REQUEST_REJECT,
                    nextSrcX2Id_++, 0, {}};
    msg.payload = {static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)};
    return sendX2APMsg(msg);
}

bool X2APLink::sgNBModificationRequest(RNTI rnti, const rbs::DCBearerConfig& bearer)
{
    RBS_LOG_INFO("X2AP", "[", enbId_, "] SgNB Modification Request  rnti=", rnti,
                 " bearerId=", bearer.enbBearerId);

    auto it = endcMap_.find(rnti);
    if (it != endcMap_.end()) {
        for (auto& b : it->second.bearers) {
            if (b.enbBearerId == bearer.enbBearerId) { b = bearer; break; }
        }
    }

    X2APMessage msg{X2APProcedure::SGNB_MODIFICATION_REQUEST,
                    nextSrcX2Id_++, 0, {}};
    msg.payload = {static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti),
                   bearer.enbBearerId};
    return sendX2APMsg(msg);
}

bool X2APLink::sgNBModificationRequestAck(RNTI rnti, const rbs::DCBearerConfig& bearer)
{
    RBS_LOG_INFO("X2AP", "[", enbId_, "] SgNB Modification Ack  rnti=", rnti,
                 " bearerId=", bearer.enbBearerId);
    X2APMessage msg{X2APProcedure::SGNB_MODIFICATION_REQUEST_ACK,
                    nextSrcX2Id_++, 0, {}};
    msg.payload = {static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti),
                   bearer.enbBearerId};
    return sendX2APMsg(msg);
}

bool X2APLink::sgNBReleaseRequest(RNTI rnti)
{
    RBS_LOG_INFO("X2AP", "[", enbId_, "] SgNB Release Request  rnti=", rnti);
    endcMap_.erase(rnti);
    X2APMessage msg{X2APProcedure::SGNB_RELEASE_REQUEST,
                    nextSrcX2Id_++, 0,
                    {static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)}};
    return sendX2APMsg(msg);
}

bool X2APLink::sgNBReleaseRequestAck(RNTI rnti)
{
    RBS_LOG_INFO("X2AP", "[", enbId_, "] SgNB Release Ack  rnti=", rnti);
    X2APMessage msg{X2APProcedure::SGNB_RELEASE_REQUEST_ACK,
                    nextSrcX2Id_++, 0,
                    {static_cast<uint8_t>(rnti >> 8), static_cast<uint8_t>(rnti)}};
    return sendX2APMsg(msg);
}

} // namespace rbs::lte

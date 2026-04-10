#include "s1ap_link.h"
#include "s1ap_codec.h"

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
//  S1APLink
// ─────────────────────────────────────────────────────────────────────────────

S1APLink::S1APLink(const std::string& enbId)
    : enbId_(enbId)
    , socket_("S1AP-" + enbId)
{}

uint32_t S1APLink::allocateMmeId(RNTI rnti)
{
    auto it = ueS1apIds_.find(rnti);
    if (it != ueS1apIds_.end()) return it->second;
    uint32_t id = nextMmeId_++;
    ueS1apIds_[rnti] = id;
    return id;
}

bool S1APLink::connect(const std::string& mmeAddr, uint16_t port)
{
    if (connected_) {
        RBS_LOG_WARNING("S1AP", "[{}] уже подключён к MME {}:{}", enbId_, mmeAddr_, mmePort_);
        return true;
    }
    mmeAddr_   = mmeAddr;
    mmePort_   = port;

    if (!socketReady_) {
        net::UdpSocket::wsaInit();
        if (!socket_.bind(0)) {
            RBS_LOG_ERROR("S1AP", "[{}] connect: UDP bind failed", enbId_);
            return false;
        }
        socket_.startReceive([this](const net::UdpPacket& pkt) { onRxPacket(pkt); });
        socketReady_ = true;
    }

    connected_ = true;
    RBS_LOG_INFO("S1AP", "[{}] S1AP-соединение с MME {}:{} установлено (UDP port {})",
                 enbId_, mmeAddr, port, socket_.localPort());
    return true;
}

void S1APLink::disconnect()
{
    if (!connected_) return;
    socket_.close();
    socketReady_ = false;
    connected_ = false;
    ueS1apIds_.clear();
    RBS_LOG_INFO("S1AP", "[{}] S1AP-соединение закрыто", enbId_);
}

bool S1APLink::s1Setup(uint32_t enbId, const std::string& enbName,
                       uint32_t tac, uint32_t plmnId)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] s1Setup: нет соединения с MME", enbId_);
        return false;
    }
    enbIdNum_ = enbId;
    tac_      = static_cast<uint16_t>(tac);
    plmnId_   = plmnId;

    RBS_LOG_INFO("S1AP",
                 "[{}] S1 SETUP enbId=0x{:07X} name={} TAC=0x{:04X} PLMN=0x{:06X}",
                 enbId_, enbId, enbName, tac, plmnId);

    ByteBuffer payload = s1ap_encode_S1SetupRequest(enbId, enbName, plmnId, tac_);
    S1APMessage msg{S1APProcedure::S1_SETUP, 0, 0, std::move(payload)};
    bool ok = sendS1APMsg(msg);

    // Симуляция: MME сразу отвечает успехом
    if (ok) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        ByteBuffer resp{'S','1','_','O','K'};
        rxQueue_.push({S1APProcedure::S1_SETUP, 0, 0, std::move(resp)});
    }
    return ok;
}

bool S1APLink::initialUEMessage(RNTI rnti, IMSI imsi, const ByteBuffer& nasPdu)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] initialUEMessage: нет соединения с MME", enbId_);
        return false;
    }
    uint32_t mmeId = allocateMmeId(rnti);
    RBS_LOG_INFO("S1AP", "[{}] S1AP INITIAL UE MSG rnti={} mmeId=0x{:X} nasLen={}",
                 enbId_, rnti, mmeId, nasPdu.size());

    ByteBuffer payload = s1ap_encode_InitialUEMessage(
        static_cast<uint32_t>(rnti), nasPdu, plmnId_, tac_, cellId_,
        1 /*mo-Signalling*/);
    S1APMessage msg{S1APProcedure::INITIAL_UE_MESSAGE, mmeId,
                    static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::downlinkNASTransport(uint32_t mmeUeS1apId, RNTI rnti,
                                    const ByteBuffer& nasPdu)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP DL NAS TRANSPORT rnti={} nasLen={}",
                 enbId_, rnti, nasPdu.size());
    ByteBuffer payload = s1ap_encode_DownlinkNASTransport(
        mmeUeS1apId, static_cast<uint32_t>(rnti), nasPdu);
    S1APMessage msg{S1APProcedure::DOWNLINK_NAS_TRANSPORT,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::uplinkNASTransport(uint32_t mmeUeS1apId, RNTI rnti,
                                  const ByteBuffer& nasPdu)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP UL NAS TRANSPORT rnti={} nasLen={}",
                 enbId_, rnti, nasPdu.size());
    ByteBuffer payload = s1ap_encode_UplinkNASTransport(
        mmeUeS1apId, static_cast<uint32_t>(rnti), nasPdu, plmnId_, tac_, cellId_);
    S1APMessage msg{S1APProcedure::UPLINK_NAS_TRANSPORT,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::initialContextSetupResponse(uint32_t mmeUeS1apId, RNTI rnti,
                                           const std::vector<ERAB>& erabs)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP INITIAL CONTEXT SETUP RESP rnti={} erabs={}",
                 enbId_, rnti, erabs.size());
    ByteBuffer payload = s1ap_encode_InitialContextSetupResponse(
        mmeUeS1apId, static_cast<uint32_t>(rnti), erabs);
    S1APMessage msg{S1APProcedure::INITIAL_CONTEXT_SETUP,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::ueContextReleaseRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                       const std::string& cause)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP UE CTX RELEASE REQUEST rnti={} причина={}",
                 enbId_, rnti, cause);
    // Map string cause to Cause group/value: default radioNetwork/unspecified
    ByteBuffer payload = s1ap_encode_UEContextReleaseRequest(
        mmeUeS1apId, static_cast<uint32_t>(rnti), 0 /*radioNetwork*/, 0 /*unspecified*/);
    S1APMessage msg{S1APProcedure::UE_CONTEXT_RELEASE_REQUEST,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    bool ok = sendS1APMsg(msg);

    // Симуляция: MME немедленно шлёт RELEASE COMMAND
    if (ok) {
        std::lock_guard<std::mutex> lk(rxMtx_);
        rxQueue_.push({S1APProcedure::UE_CONTEXT_RELEASE_COMMAND,
                       mmeUeS1apId, static_cast<uint32_t>(rnti), {}});
    }
    return ok;
}

bool S1APLink::ueContextReleaseComplete(uint32_t mmeUeS1apId, RNTI rnti)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP UE CTX RELEASE COMPLETE rnti={}", enbId_, rnti);
    ueS1apIds_.erase(rnti);
    ByteBuffer payload = s1ap_encode_UEContextReleaseComplete(
        mmeUeS1apId, static_cast<uint32_t>(rnti));
    S1APMessage msg{S1APProcedure::UE_CONTEXT_RELEASE_COMPLETE,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::pathSwitchRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                 uint32_t targetEnbId,
                                 const std::vector<ERAB>& erabs)
{
    RBS_LOG_INFO("S1AP",
                 "[{}] S1AP PATH SWITCH REQUEST rnti={} targetEnb=0x{:X} erabs={}",
                 enbId_, rnti, targetEnbId, erabs.size());
    ByteBuffer payload = s1ap_encode_PathSwitchRequest(
        mmeUeS1apId, static_cast<uint32_t>(rnti),
        targetEnbId, erabs, plmnId_, cellId_, tac_);
    S1APMessage msg{S1APProcedure::PATH_SWITCH_REQUEST,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::erabSetupResponse(uint32_t mmeUeS1apId, RNTI rnti,
                                 const std::vector<ERAB>& erabs,
                                 const std::vector<uint8_t>& failedErabIds)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP E-RAB SETUP RESP rnti={} ok={} fail={}",
                 enbId_, rnti, erabs.size(), failedErabIds.size());
    ByteBuffer payload = s1ap_encode_ERABSetupResponse(
        mmeUeS1apId, static_cast<uint32_t>(rnti), erabs, failedErabIds);
    S1APMessage msg{S1APProcedure::E_RAB_SETUP,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::erabReleaseResponse(uint32_t mmeUeS1apId, RNTI rnti,
                                   const std::vector<uint8_t>& releasedErabIds)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP E-RAB RELEASE RESP rnti={} released={}",
                 enbId_, rnti, releasedErabIds.size());
    ByteBuffer payload = s1ap_encode_ERABReleaseResponse(
        mmeUeS1apId, static_cast<uint32_t>(rnti), releasedErabIds);
    S1APMessage msg{S1APProcedure::E_RAB_RELEASE,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::handoverRequired(uint32_t mmeUeS1apId, RNTI rnti,
                                uint32_t targetEnbId,
                                const ByteBuffer& rrcContainer)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP HANDOVER REQUIRED rnti={} targetEnb=0x{:X}",
                 enbId_, rnti, targetEnbId);
    ByteBuffer payload = s1ap_encode_HandoverRequired(
        mmeUeS1apId, static_cast<uint32_t>(rnti),
        targetEnbId, plmnId_, cellId_, rrcContainer);
    S1APMessage msg{S1APProcedure::HANDOVER_REQUIRED,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::handoverNotify(uint32_t mmeUeS1apId, RNTI rnti)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP HANDOVER NOTIFY rnti={}", enbId_, rnti);
    ByteBuffer payload = s1ap_encode_HandoverNotify(
        mmeUeS1apId, static_cast<uint32_t>(rnti), plmnId_, tac_, cellId_);
    S1APMessage msg{S1APProcedure::HANDOVER_NOTIFY,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::sendS1APMsg(const S1APMessage& msg)
{
    if (!connected_ || !socketReady_) return false;

    // Frame: [proc:1][mmeUeS1apId:4][enbUeS1apId:4][payloadLen:4][payload...]
    const auto& pl = msg.payload;
    const uint32_t plLen = static_cast<uint32_t>(pl.size());
    ByteBuffer frame;
    frame.reserve(13 + plLen);
    frame.push_back(static_cast<uint8_t>(msg.procedure));
    frame.push_back(static_cast<uint8_t>(msg.mmeUeS1apId >> 24));
    frame.push_back(static_cast<uint8_t>(msg.mmeUeS1apId >> 16));
    frame.push_back(static_cast<uint8_t>(msg.mmeUeS1apId >>  8));
    frame.push_back(static_cast<uint8_t>(msg.mmeUeS1apId));
    frame.push_back(static_cast<uint8_t>(msg.enbUeS1apId >> 24));
    frame.push_back(static_cast<uint8_t>(msg.enbUeS1apId >> 16));
    frame.push_back(static_cast<uint8_t>(msg.enbUeS1apId >>  8));
    frame.push_back(static_cast<uint8_t>(msg.enbUeS1apId));
    frame.push_back(static_cast<uint8_t>(plLen >> 24));
    frame.push_back(static_cast<uint8_t>(plLen >> 16));
    frame.push_back(static_cast<uint8_t>(plLen >>  8));
    frame.push_back(static_cast<uint8_t>(plLen));
    frame.insert(frame.end(), pl.begin(), pl.end());

    bool ok = socket_.send(mmeAddr_, mmePort_, frame);
    RBS_LOG_DEBUG("S1AP", "[{}] S1AP → MME  proc=0x{:02X} len={} {}",
                  enbId_, static_cast<uint8_t>(msg.procedure), plLen,
                  ok ? "OK" : "FAIL");
    return ok;
}

bool S1APLink::recvS1APMsg(S1APMessage& msg)
{
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (rxQueue_.empty()) return false;
    msg = rxQueue_.front();
    rxQueue_.pop();
    RBS_LOG_DEBUG("S1AP", "[{}] S1AP ← MME  proc=0x{:02X}",
                  enbId_, static_cast<uint8_t>(msg.procedure));
    return true;
}

void S1APLink::onRxPacket(const net::UdpPacket& pkt)
{
    // Minimum frame: 13 header bytes
    if (pkt.data.size() < 13) return;
    const auto& d = pkt.data;
    const auto  proc   = static_cast<S1APProcedure>(d[0]);
    const uint32_t mmeId  = (static_cast<uint32_t>(d[1]) << 24)
                           | (static_cast<uint32_t>(d[2]) << 16)
                           | (static_cast<uint32_t>(d[3]) <<  8)
                           |  static_cast<uint32_t>(d[4]);
    const uint32_t enbId  = (static_cast<uint32_t>(d[5]) << 24)
                           | (static_cast<uint32_t>(d[6]) << 16)
                           | (static_cast<uint32_t>(d[7]) <<  8)
                           |  static_cast<uint32_t>(d[8]);
    const uint32_t plLen  = (static_cast<uint32_t>(d[9]) << 24)
                           | (static_cast<uint32_t>(d[10]) << 16)
                           | (static_cast<uint32_t>(d[11]) <<  8)
                           |  static_cast<uint32_t>(d[12]);
    if (plLen > pkt.data.size() - 13) return;  // bounds guard
    ByteBuffer payload(d.begin() + 13, d.begin() + 13 + plLen);
    RBS_LOG_DEBUG("S1AP", "[{}] S1AP ← MME  proc=0x{:02X} mmeId=0x{:X} len={}",
                  enbId_, static_cast<uint8_t>(proc), mmeId, plLen);
    std::lock_guard<std::mutex> lk(rxMtx_);
    rxQueue_.push({proc, mmeId, enbId, std::move(payload)});
}

// ─────────────────────────────────────────────────────────────────────────────
//  S1ULink
// ─────────────────────────────────────────────────────────────────────────────

S1ULink::S1ULink(const std::string& enbId, uint16_t localPort)
    : enbId_(enbId)
    , localPort_(localPort)
    , socket_("S1U-" + enbId)
{}

bool S1ULink::createTunnel(RNTI rnti, uint8_t erabId, const GTPUTunnel& sgwEndpoint)
{
    uint32_t key = tunnelKey(rnti, erabId);

    // Привязать сокет при создании первого туннеля
    if (!socketReady_) {
        if (!socket_.bind(localPort_)) {
            RBS_LOG_ERROR("S1U", "[{}] createTunnel: UDP bind ошибка", enbId_);
            return false;
        }
        socket_.startReceive([this](const net::UdpPacket& pkt) { onRxPacket(pkt); });
        socketReady_ = true;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        tunnels_[key]                = sgwEndpoint;
        teidToKey_[sgwEndpoint.teid] = key;
    }

    // Конвертируем uint32_t (network byte order) → строку для лога
    char ipStr[INET_ADDRSTRLEN] = {};
    struct in_addr addr{ sgwEndpoint.remoteIPv4 };
    ::inet_ntop(AF_INET, &addr, ipStr, sizeof(ipStr));

    RBS_LOG_INFO("S1U", "[{}] GTP-U туннель создан rnti=", rnti, " erabId=", erabId,
                 " teid=0x", sgwEndpoint.teid, " sgw=", ipStr);
    return true;
}

bool S1ULink::deleteTunnel(RNTI rnti, uint8_t erabId)
{
    uint32_t key = tunnelKey(rnti, erabId);
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = tunnels_.find(key);
    if (it == tunnels_.end()) {
        RBS_LOG_WARNING("S1U", "[{}] deleteTunnel: туннель rnti={} erabId={} не найден",
                         enbId_, rnti, erabId);
        return false;
    }
    teidToKey_.erase(it->second.teid);
    tunnels_.erase(it);
    dlQueues_.erase(key);
    RBS_LOG_INFO("S1U", "[{}] GTP-U туннель удалён rnti={} erabId={}", enbId_, rnti, erabId);
    return true;
}

bool S1ULink::sendGtpuPdu(RNTI rnti, uint8_t erabId, const ByteBuffer& ipPacket)
{
    uint32_t key = tunnelKey(rnti, erabId);
    GTPUTunnel t;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = tunnels_.find(key);
        if (it == tunnels_.end()) {
            RBS_LOG_ERROR("S1U", "[", enbId_, "] sendGtpuPdu: нет туннеля rnti=", rnti,
                          " erabId=", erabId);
            return false;
        }
        t = it->second;
    }

    // Конвертируем uint32_t (network byte order) → строку
    char ipStr[INET_ADDRSTRLEN] = {};
    struct in_addr addr{ t.remoteIPv4 };
    ::inet_ntop(AF_INET, &addr, ipStr, sizeof(ipStr));

    ByteBuffer frame = gtpuEncode(t.teid, ipPacket);
    bool ok = socket_.send(std::string(ipStr), GTPU_PORT, frame);
    RBS_LOG_DEBUG("S1U", "[", enbId_, "] GTP-U UL teid=0x", t.teid, " → ", ipStr,
                  " len=", ipPacket.size(), ok ? " OK" : " FAIL");
    return ok;
}

bool S1ULink::recvGtpuPdu(RNTI rnti, uint8_t erabId, ByteBuffer& ipPacket)
{
    uint32_t key = tunnelKey(rnti, erabId);
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = dlQueues_.find(key);
    if (it == dlQueues_.end() || it->second.empty()) return false;
    ipPacket = std::move(it->second.front());
    it->second.pop();
    RBS_LOG_DEBUG("S1U", "[{}] GTP-U DL rnti={} erabId={} len={}",
                  enbId_, rnti, erabId, ipPacket.size());
    return true;
}

void S1ULink::onRxPacket(const net::UdpPacket& pkt)
{
    uint32_t teid;
    ByteBuffer payload;
    if (!gtpuDecode(pkt.data, teid, payload)) return;

    std::lock_guard<std::mutex> lk(mtx_);
    auto it = teidToKey_.find(teid);
    if (it == teidToKey_.end()) {
        RBS_LOG_WARNING("S1U", "[{}] входящий GTP-U: неизвестный teid=0x{:X}", enbId_, teid);
        return;
    }
    dlQueues_[it->second].push(std::move(payload));
}

} // namespace rbs::lte

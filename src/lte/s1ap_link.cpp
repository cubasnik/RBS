#include "s1ap_link.h"

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
//  S1APLink
// ─────────────────────────────────────────────────────────────────────────────

S1APLink::S1APLink(const std::string& enbId)
    : enbId_(enbId)
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
    connected_ = true;
    RBS_LOG_INFO("S1AP", "[{}] S1AP-соединение с MME {}:{} установлено", enbId_, mmeAddr, port);
    return true;
}

void S1APLink::disconnect()
{
    if (!connected_) return;
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
    RBS_LOG_INFO("S1AP",
                 "[{}] S1 SETUP enbId=0x{:07X} name={} TAC=0x{:04X} PLMN=0x{:06X}",
                 enbId_, enbId, enbName, tac, plmnId);

    ByteBuffer payload{
        static_cast<uint8_t>(enbId >> 24), static_cast<uint8_t>(enbId >> 16),
        static_cast<uint8_t>(enbId >> 8),  static_cast<uint8_t>(enbId),
        static_cast<uint8_t>(tac >> 8),    static_cast<uint8_t>(tac),
        static_cast<uint8_t>(plmnId >> 16),static_cast<uint8_t>(plmnId >> 8),
        static_cast<uint8_t>(plmnId)
    };
    payload.insert(payload.end(), enbName.begin(), enbName.end());
    S1APMessage msg{S1APProcedure::S1_SETUP, 0, 0, std::move(payload)};
    bool ok = sendS1APMsg(msg);

    // Симуляция: MME сразу отвечает успехом (S1 Setup Response содержит MME name)
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

    ByteBuffer payload;
    const auto* imsiBytes = reinterpret_cast<const uint8_t*>(&imsi);
    payload.insert(payload.end(), imsiBytes, imsiBytes + sizeof(imsi));
    payload.insert(payload.end(), nasPdu.begin(), nasPdu.end());

    S1APMessage msg{S1APProcedure::INITIAL_UE_MESSAGE, mmeId,
                    static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::downlinkNASTransport(uint32_t mmeUeS1apId, RNTI rnti,
                                    const ByteBuffer& nasPdu)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP DL NAS TRANSPORT rnti={} nasLen={}",
                 enbId_, rnti, nasPdu.size());
    S1APMessage msg{S1APProcedure::DOWNLINK_NAS_TRANSPORT,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), nasPdu};
    return sendS1APMsg(msg);
}

bool S1APLink::uplinkNASTransport(uint32_t mmeUeS1apId, RNTI rnti,
                                  const ByteBuffer& nasPdu)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP UL NAS TRANSPORT rnti={} nasLen={}",
                 enbId_, rnti, nasPdu.size());
    S1APMessage msg{S1APProcedure::UPLINK_NAS_TRANSPORT,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), nasPdu};
    return sendS1APMsg(msg);
}

bool S1APLink::initialContextSetupResponse(uint32_t mmeUeS1apId, RNTI rnti,
                                           const std::vector<ERAB>& erabs)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP INITIAL CONTEXT SETUP RESP rnti={} erabs={}",
                 enbId_, rnti, erabs.size());
    ByteBuffer payload;
    for (const auto& e : erabs) {
        payload.push_back(e.erabId);
        payload.push_back(e.qci);
    }
    S1APMessage msg{S1APProcedure::INITIAL_CONTEXT_SETUP,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::ueContextReleaseRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                       const std::string& cause)
{
    RBS_LOG_INFO("S1AP", "[{}] S1AP UE CTX RELEASE REQUEST rnti={} причина={}",
                 enbId_, rnti, cause);
    ByteBuffer payload(cause.begin(), cause.end());
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
    S1APMessage msg{S1APProcedure::UE_CONTEXT_RELEASE_COMPLETE,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), {}};
    return sendS1APMsg(msg);
}

bool S1APLink::pathSwitchRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                 uint32_t targetEnbId,
                                 const std::vector<ERAB>& erabs)
{
    RBS_LOG_INFO("S1AP",
                 "[{}] S1AP PATH SWITCH REQUEST rnti={} targetEnb=0x{:X} erabs={}",
                 enbId_, rnti, targetEnbId, erabs.size());
    ByteBuffer payload{
        static_cast<uint8_t>(targetEnbId >> 24),
        static_cast<uint8_t>(targetEnbId >> 16),
        static_cast<uint8_t>(targetEnbId >> 8),
        static_cast<uint8_t>(targetEnbId)
    };
    for (const auto& e : erabs) payload.push_back(e.erabId);
    S1APMessage msg{S1APProcedure::PATH_SWITCH_REQUEST,
                    mmeUeS1apId, static_cast<uint32_t>(rnti), std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::sendS1APMsg(const S1APMessage& msg)
{
    RBS_LOG_DEBUG("S1AP", "[{}] S1AP → MME  proc=0x{:02X} mmeId=0x{:X} enbId=0x{:X}",
                  enbId_, static_cast<uint8_t>(msg.procedure),
                  msg.mmeUeS1apId, msg.enbUeS1apId);
    return true;  // симуляция
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

// ─────────────────────────────────────────────────────────────────────────────
//  S1ULink
// ─────────────────────────────────────────────────────────────────────────────

S1ULink::S1ULink(const std::string& enbId)
    : enbId_(enbId)
{}

bool S1ULink::createTunnel(RNTI rnti, uint8_t erabId, const GTPUTunnel& sgwEndpoint)
{
    uint32_t key = tunnelKey(rnti, erabId);
    tunnels_[key] = sgwEndpoint;
    RBS_LOG_INFO("S1U", "[{}] GTP-U туннель создан rnti={} erabId={} teid=0x{:X}",
                 enbId_, rnti, erabId, sgwEndpoint.teid);
    return true;
}

bool S1ULink::deleteTunnel(RNTI rnti, uint8_t erabId)
{
    uint32_t key = tunnelKey(rnti, erabId);
    if (!tunnels_.count(key)) {
        RBS_LOG_WARNING("S1U", "[{}] deleteTunnel: туннель rnti={} erabId={} не найден",
                         enbId_, rnti, erabId);
        return false;
    }
    tunnels_.erase(key);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        dlQueues_.erase(key);
    }
    RBS_LOG_INFO("S1U", "[{}] GTP-U туннель удалён rnti={} erabId={}", enbId_, rnti, erabId);
    return true;
}

bool S1ULink::sendGtpuPdu(RNTI rnti, uint8_t erabId, const ByteBuffer& ipPacket)
{
    uint32_t key = tunnelKey(rnti, erabId);
    if (!tunnels_.count(key)) {
        RBS_LOG_ERROR("S1U", "[{}] sendGtpuPdu: нет туннеля rnti={} erabId={}",
                       enbId_, rnti, erabId);
        return false;
    }
    const auto& t = tunnels_[key];
    RBS_LOG_DEBUG("S1U", "[{}] GTP-U UL teid=0x{:X} len={}", enbId_, t.teid, ipPacket.size());
    return true;  // в симуляции пакет «улетает» к SGW
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

} // namespace rbs::lte

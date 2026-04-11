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
        net::SctpSocket::wsaInit();
        if (!socket_.bind(0)) {
            RBS_LOG_ERROR("S1AP", "[{}] connect: SCTP bind failed", enbId_);
            return false;
        }
        if (!socket_.connect(mmeAddr_, mmePort_)) {
            RBS_LOG_ERROR("S1AP", "[{}] connect: SCTP connect failed {}:{}", enbId_, mmeAddr_, mmePort_);
            socket_.close();
            return false;
        }
        socket_.startReceive([this](const net::SctpPacket& pkt) { onRxPacket(pkt); });
        socketReady_ = true;
    }

    connected_ = true;
    RBS_LOG_INFO("S1AP", "[{}] S1AP-соединение с MME {}:{} установлено (local port {})",
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
    return sendS1APMsg(msg);
}

bool S1APLink::initialUEMessage(RNTI rnti, IMSI imsi, const ByteBuffer& nasPdu)
{
    (void)imsi;
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
    return sendS1APMsg(msg);
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

bool S1APLink::handoverRequestAcknowledge(uint32_t mmeUeS1apId, RNTI rnti,
                                          const ByteBuffer& targetToSrcContainer)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] handoverRequestAcknowledge: нет соединения с MME", enbId_);
        return false;
    }
    const uint32_t enbUeId = static_cast<uint32_t>(rnti);
    RBS_LOG_INFO("S1AP", "[{}] S1AP HANDOVER REQUEST ACK mme={} enb={}",
                 enbId_, mmeUeS1apId, enbUeId);
    ByteBuffer payload = s1ap_encode_HandoverRequestAcknowledge(
        mmeUeS1apId, enbUeId, targetToSrcContainer);
    S1APMessage msg{S1APProcedure::HANDOVER_REQUEST_ACKNOWLEDGE,
                    mmeUeS1apId, enbUeId, std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::enbStatusTransfer(uint32_t mmeUeS1apId, RNTI rnti)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] enbStatusTransfer: нет соединения с MME", enbId_);
        return false;
    }
    const uint32_t enbUeId = static_cast<uint32_t>(rnti);
    RBS_LOG_INFO("S1AP", "[{}] S1AP ENB STATUS TRANSFER mme={} enb={}",
                 enbId_, mmeUeS1apId, enbUeId);
    ByteBuffer payload = s1ap_encode_ENBStatusTransfer(mmeUeS1apId, enbUeId);
    S1APMessage msg{S1APProcedure::ENB_STATUS_TRANSFER,
                    mmeUeS1apId, enbUeId, std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::handoverFailure(uint32_t mmeUeS1apId,
                               uint8_t causeGroup, uint8_t causeValue)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] handoverFailure: нет соединения с MME", enbId_);
        return false;
    }
    RBS_LOG_INFO("S1AP", "[{}] S1AP HANDOVER FAILURE mme={} causeGroup={} causeValue={}",
                 enbId_, mmeUeS1apId, causeGroup, causeValue);
    ByteBuffer payload = s1ap_encode_HandoverFailure(mmeUeS1apId, causeGroup, causeValue);
    S1APMessage msg{S1APProcedure::HANDOVER_FAILURE,
                    mmeUeS1apId, 0, std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::paging(uint16_t ueIdxVal, const ByteBuffer& imsi,
                      uint32_t plmnId, uint16_t tac, uint8_t cnDomain)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] paging: нет соединения с MME", enbId_);
        return false;
    }
    RBS_LOG_INFO("S1AP", "[{}] S1AP PAGING ueIdxVal={} imsiLen={} cnDomain={}",
                 enbId_, ueIdxVal, imsi.size(), cnDomain);
    ByteBuffer payload = s1ap_encode_Paging(ueIdxVal, imsi, plmnId, tac, cnDomain);
    S1APMessage msg{S1APProcedure::PAGING, 0, 0, std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::reset(uint8_t causeGroup, uint8_t causeValue, bool resetAll)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] reset: нет соединения с MME", enbId_);
        return false;
    }
    RBS_LOG_INFO("S1AP", "[{}] S1AP RESET causeGroup={} causeValue={} all={}",
                 enbId_, causeGroup, causeValue, resetAll);
    ByteBuffer payload = s1ap_encode_Reset(causeGroup, causeValue, resetAll);
    S1APMessage msg{S1APProcedure::RESET, 0, 0, std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::errorIndication(uint32_t mmeUeS1apId, uint32_t enbUeS1apId,
                               uint8_t causeGroup, uint8_t causeValue)
{
    if (!connected_) {
        RBS_LOG_ERROR("S1AP", "[{}] errorIndication: нет соединения с MME", enbId_);
        return false;
    }
    RBS_LOG_INFO("S1AP", "[{}] S1AP ERROR_INDICATION mme={} enb={} causeGroup={} causeValue={}",
                 enbId_, mmeUeS1apId, enbUeS1apId, causeGroup, causeValue);
    ByteBuffer payload = s1ap_encode_ErrorIndication(mmeUeS1apId, enbUeS1apId,
                                                     causeGroup, causeValue);
    S1APMessage msg{S1APProcedure::ERROR_INDICATION, 0, 0, std::move(payload)};
    return sendS1APMsg(msg);
}

bool S1APLink::sendS1APMsg(const S1APMessage& msg)
{
    if (!connected_ || !socketReady_) return false;

    const auto& payload = msg.payload;
    const uint32_t plLen = static_cast<uint32_t>(payload.size());
    bool ok = socket_.send(payload);    if (ok && pcap_.isOpen())
        pcap_.writeSctp("127.0.0.1", socket_.localPort(),
                        mmeAddr_, mmePort_,
                        rbs::PcapWriter::PPID_S1AP, payload);    RBS_LOG_DEBUG("S1AP", "[{}] S1AP → MME  proc=0x{:02X} len={} {}",
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

void S1APLink::enablePcap(const std::string& path)
{
    pcap_.open(path);
}

void S1APLink::onRxPacket(const net::SctpPacket& pkt)
{
    S1APMessage msg{};
    if (!s1ap_decode_message(pkt.data, msg)) {
        RBS_LOG_WARNING("S1AP", "[{}] RX decode failed from {}:{} len={}",
                        enbId_, pkt.srcIp, pkt.srcPort, pkt.data.size());
        return;
    }

    RBS_LOG_DEBUG("S1AP", "[{}] S1AP ← MME  proc=0x{:02X} mmeId=0x{:X} len={}",
                  enbId_, static_cast<uint8_t>(msg.procedure),
                  msg.mmeUeS1apId, msg.payload.size());
    if (msg.procedure == S1APProcedure::S1_SETUP && msg.isSuccessfulOutcome) {
        RBS_LOG_INFO("S1AP", "[{}] RX S1SetupResponse decoded from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::S1_SETUP && msg.isUnsuccessfulOutcome) {
        RBS_LOG_INFO("S1AP", "[{}] RX S1SetupFailure decoded from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::PAGING) {
        // TS 36.413 §8.7.1 — Paging is non-acknowledged; log and forward to RX queue.
        RBS_LOG_INFO("S1AP", "[{}] RX Paging from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::RESET) {
        // TS 36.413 §8.7.2 — Reset; log and forward to RX queue.
        RBS_LOG_INFO("S1AP", "[{}] RX Reset from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::ERROR_INDICATION) {
        // TS 36.413 §8.7.4 — Error Indication; log and forward to RX queue.
        RBS_LOG_INFO("S1AP", "[{}] RX ErrorIndication from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::HANDOVER_REQUIRED) {
        // TS 36.413 §8.5.2 — Handover Required (source side, initiating).
        RBS_LOG_INFO("S1AP", "[{}] RX HandoverRequired from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::HANDOVER_COMMAND) {
        // TS 36.413 §8.5.2 — HandoverCommand (successful outcome, source receives).
        RBS_LOG_INFO("S1AP", "[{}] RX HandoverCommand from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::HANDOVER_PREPARATION_FAILURE) {
        // TS 36.413 §8.5.2 — HandoverPreparationFailure (unsuccessful, source receives).
        RBS_LOG_INFO("S1AP", "[{}] RX HandoverPreparationFailure from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::HANDOVER_REQUEST) {
        // TS 36.413 §8.5.2 — HandoverRequest (target receives from MME).
        RBS_LOG_INFO("S1AP", "[{}] RX HandoverRequest from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::HANDOVER_REQUEST_ACKNOWLEDGE) {
        // TS 36.413 §8.5.2 — HandoverRequestAcknowledge (target sent, MME receives).
        RBS_LOG_INFO("S1AP", "[{}] RX HandoverRequestAcknowledge from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::ENB_STATUS_TRANSFER) {
        // TS 36.413 §8.5.2 — eNBStatusTransfer (source eNB → MME).
        RBS_LOG_INFO("S1AP", "[{}] RX ENBStatusTransfer from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::MME_STATUS_TRANSFER) {
        // TS 36.413 §8.5.2 — MMEStatusTransfer (MME → target eNB).
        RBS_LOG_INFO("S1AP", "[{}] RX MMEStatusTransfer from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    } else if (msg.procedure == S1APProcedure::HANDOVER_FAILURE) {
        // TS 36.413 §8.5.2 — HandoverFailure (target eNB → MME, unsuccessful).
        RBS_LOG_INFO("S1AP", "[{}] RX HandoverFailure from {}:{} (len={})",
                     enbId_, pkt.srcIp, pkt.srcPort, msg.payload.size());
    }
    std::lock_guard<std::mutex> lk(rxMtx_);
    if (pcap_.isOpen())
        pcap_.writeSctp(pkt.srcIp, pkt.srcPort,
                        "127.0.0.1", socket_.localPort(),
                        rbs::PcapWriter::PPID_S1AP, pkt.data);
    rxQueue_.push(std::move(msg));
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
    struct in_addr addr{};
    ::memcpy(&addr, &sgwEndpoint.remoteIPv4, 4);
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
    struct in_addr addr{};
    ::memcpy(&addr, &t.remoteIPv4, 4);
    ::inet_ntop(AF_INET, &addr, ipStr, sizeof(ipStr));

    ByteBuffer frame = gtpuEncode(t.teid, ipPacket);
    uint16_t destPort = t.udpPort ? t.udpPort : GTPU_PORT;
    bool ok = socket_.send(std::string(ipStr), destPort, frame);    if (ok && pcap_.isOpen())
        pcap_.writeUdp("127.0.0.1", localPort_,
                       std::string(ipStr), destPort, frame);    RBS_LOG_DEBUG("S1U", "[", enbId_, "] GTP-U UL teid=0x", t.teid, " → ", ipStr,
                  ":", destPort, " len=", ipPacket.size(), ok ? " OK" : " FAIL");
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
{    if (pcap_.isOpen())
        pcap_.writeUdp(pkt.srcIp, pkt.srcPort,
                       "127.0.0.1", localPort_, pkt.data);
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

void S1ULink::enablePcap(const std::string& path)
{
    pcap_.open(path);
}

} // namespace rbs::lte

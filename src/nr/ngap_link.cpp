#include "ngap_link.h"

#include "../common/logger.h"

namespace rbs::nr {

namespace {
ByteBuffer makeNgapTransportFrame(NgapProcedure procedure, const ByteBuffer& payload, const std::string& traceId) {
    ByteBuffer framed;
    const uint8_t traceLen = static_cast<uint8_t>(std::min<size_t>(traceId.size(), 64));
    framed.reserve(payload.size() + 6 + traceLen);
    framed.push_back(0x4E);  // 'N'
    framed.push_back(0x47);  // 'G'
    framed.push_back(static_cast<uint8_t>(procedure));
    framed.push_back(0x54);  // 'T'
    framed.push_back(0x52);  // 'R'
    framed.push_back(traceLen);
    if (traceLen > 0) {
        framed.insert(framed.end(), traceId.begin(), traceId.begin() + traceLen);
    }
    framed.insert(framed.end(), payload.begin(), payload.end());
    return framed;
}
}  // namespace

std::mutex NgapLink::registryMutex_;
std::unordered_map<uint64_t, NgapLink*> NgapLink::registry_;

NgapLink::NgapLink(uint64_t localNodeId)
    : localNodeId_(localNodeId)
    , sctp_(std::make_unique<rbs::net::SctpSocket>("NGAP-" + std::to_string(localNodeId)))
{
    std::lock_guard<std::mutex> lock(registryMutex_);
    registry_[localNodeId_] = this;
}

NgapLink::~NgapLink() {
    std::lock_guard<std::mutex> lock(registryMutex_);
    auto it = registry_.find(localNodeId_);
    if (it != registry_.end() && it->second == this) {
        registry_.erase(it);
    }
}

bool NgapLink::connect(uint64_t targetNodeId) {
    std::lock_guard<std::mutex> lock(registryMutex_);
    const auto it = registry_.find(targetNodeId);
    if (it == registry_.end() || it->second == nullptr) {
        RBS_LOG_ERROR("NGAP", "connect failed: localNodeId=", localNodeId_, " targetNodeId=", targetNodeId);
        return false;
    }
    PeerInfo info{};
    info.connected = true;
    peers_[targetNodeId] = info;
    return true;
}

bool NgapLink::isConnected(uint64_t targetNodeId) const {
    const auto it = peers_.find(targetNodeId);
    return it != peers_.end() && it->second.connected;
}

bool NgapLink::bindTransport(uint16_t localPort) {
    (void)rbs::net::SctpSocket::wsaInit();
    if (!sctp_) {
        sctp_ = std::make_unique<rbs::net::SctpSocket>("NGAP-" + std::to_string(localNodeId_));
    }
    if (localPort_ != 0 || sctp_->isOpen()) {
        return true;
    }
    if (!sctp_->bind(localPort)) {
        RBS_LOG_ERROR("NGAP", "bindTransport failed: localNodeId=", localNodeId_, " localPort=", localPort);
        return false;
    }
    localPort_ = sctp_->localPort();
    if (!rxStarted_) {
        rxStarted_ = sctp_->startReceive([this](const rbs::net::SctpPacket& pkt) {
            handleSctpRx(pkt);
        });
        if (!rxStarted_) {
            RBS_LOG_ERROR("NGAP", "startReceive failed: localNodeId=", localNodeId_);
            return false;
        }
    }
    return true;
}

bool NgapLink::connectSctpPeer(uint64_t targetNodeId, const std::string& targetIp, uint16_t targetPort) {
    if (!bindTransport(0)) {
        return false;
    }
    if (!sctp_->connect(targetIp, targetPort)) {
        RBS_LOG_ERROR("NGAP", "connectSctpPeer failed: localNodeId=", localNodeId_,
                      " targetNodeId=", targetNodeId, " ", targetIp, ":", targetPort);
        return false;
    }

    PeerInfo info{};
    info.connected = true;
    info.useSctp = true;
    info.ip = targetIp;
    info.port = targetPort;
    peers_[targetNodeId] = info;
    endpointToNodeId_[endpointKey(targetIp, targetPort)] = targetNodeId;
    return true;
}

bool NgapLink::bindTransportMulti(const std::vector<std::string>& localAddrs, uint16_t localPort)
{
    (void)rbs::net::SctpSocket::wsaInit();
    if (!sctp_) {
        sctp_ = std::make_unique<rbs::net::SctpSocket>("NGAP-" + std::to_string(localNodeId_));
    }
    if (localPort_ != 0 || sctp_->isOpen()) {
        return true;
    }
    if (!sctp_->bindMulti(localAddrs, localPort)) {
        RBS_LOG_ERROR("NGAP", "bindTransportMulti failed: localNodeId=", localNodeId_);
        return false;
    }
    localPort_ = sctp_->localPort();
    if (!rxStarted_) {
        rxStarted_ = sctp_->startReceive([this](const rbs::net::SctpPacket& pkt) {
            handleSctpRx(pkt);
        });
        if (!rxStarted_) {
            RBS_LOG_ERROR("NGAP", "startReceive (multi) failed: localNodeId=", localNodeId_);
            return false;
        }
    }
    return true;
}

bool NgapLink::connectSctpPeerMulti(uint64_t targetNodeId, 
                                     const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs,
                                     int primaryIdx)
{
    if (!bindTransport(0)) {
        return false;
    }
    if (!sctp_->connectMulti(remoteAddrs, primaryIdx)) {
        RBS_LOG_ERROR("NGAP", "connectSctpPeerMulti failed: localNodeId=", localNodeId_,
                      " targetNodeId=", targetNodeId, " addresses=", remoteAddrs.size());
        return false;
    }

    PeerInfo info{};
    info.connected = true;
    info.useSctp = true;
    info.sctpAddrs = remoteAddrs;
    info.primaryAddrIdx = primaryIdx;
    if (primaryIdx >= 0 && primaryIdx < static_cast<int>(remoteAddrs.size())) {
        info.ip = remoteAddrs[primaryIdx].first;
        info.port = remoteAddrs[primaryIdx].second;
    }
    peers_[targetNodeId] = info;
    
    // Register all endpoints to this target
    for (size_t i = 0; i < remoteAddrs.size(); ++i) {
        endpointToNodeId_[endpointKey(remoteAddrs[i].first, remoteAddrs[i].second)] = targetNodeId;
    }
    
    RBS_LOG_INFO("NGAP", "connectSctpPeerMulti: localNodeId=", localNodeId_, " targetNodeId=", targetNodeId,
                 " addresses=", remoteAddrs.size(), " primary=", primaryIdx);
    return true;
}

bool NgapLink::switchToPath(uint64_t targetNodeId, int pathIdx)
{
    const auto it = peers_.find(targetNodeId);
    if (it == peers_.end()) {
        RBS_LOG_ERROR("NGAP", "switchToPath failed: peer not found, localNodeId=", localNodeId_, 
                      " targetNodeId=", targetNodeId);
        return false;
    }

    auto& info = it->second;
    if (info.sctpAddrs.empty()) {
        RBS_LOG_ERROR("NGAP", "switchToPath failed: not multi-homing, localNodeId=", localNodeId_, 
                      " targetNodeId=", targetNodeId);
        return false;
    }

    if (pathIdx < 0 || pathIdx >= static_cast<int>(info.sctpAddrs.size())) {
        RBS_LOG_ERROR("NGAP", "switchToPath failed: invalid pathIdx, localNodeId=", localNodeId_,
                      " targetNodeId=", targetNodeId, " pathIdx=", pathIdx, " count=", info.sctpAddrs.size());
        return false;
    }

    // Update peer info
    info.primaryAddrIdx = pathIdx;
    info.ip = info.sctpAddrs[pathIdx].first;
    info.port = info.sctpAddrs[pathIdx].second;

    // Notify SCTP layer
    if (!sctp_->setPrimaryPath(pathIdx)) {
        RBS_LOG_WARNING("NGAP", "SCTP setPrimaryPath failed: localNodeId=", localNodeId_,
                        " targetNodeId=", targetNodeId, " pathIdx=", pathIdx);
        return false;
    }

    RBS_LOG_INFO("NGAP", "switchToPath: localNodeId=", localNodeId_, " targetNodeId=", targetNodeId,
                 " switched to path ", pathIdx, " (", info.ip, ":", info.port, ")");
    return true;
}


bool NgapLink::ngSetup(uint64_t targetNodeId, const NgSetupRequest& req) {
    return sendMessage(targetNodeId, NgapProcedure::NG_SETUP_REQUEST, encodeNgSetupRequest(req));
}

bool NgapLink::ngSetupResponse(uint64_t targetNodeId, const NgSetupResponse& rsp) {
    return sendMessage(targetNodeId, NgapProcedure::NG_SETUP_RESPONSE, encodeNgSetupResponse(rsp));
}

bool NgapLink::pduSessionSetupRequest(uint64_t targetNodeId, const PduSessionSetupRequest& req) {
    return sendMessage(targetNodeId, NgapProcedure::PDU_SESSION_SETUP_REQUEST, encodePduSessionSetupRequest(req));
}

bool NgapLink::pduSessionSetupResponse(uint64_t targetNodeId, const PduSessionSetupResponse& rsp) {
    return sendMessage(targetNodeId, NgapProcedure::PDU_SESSION_SETUP_RESPONSE, encodePduSessionSetupResponse(rsp));
}

bool NgapLink::paging(uint64_t targetNodeId, const PagingMessage& paging) {
    return sendMessage(targetNodeId, NgapProcedure::PAGING, encodePagingMessage(paging));
}

bool NgapLink::ueContextReleaseCommand(uint64_t targetNodeId, const UeContextReleaseCommand& cmd) {
    return sendMessage(targetNodeId, NgapProcedure::UE_CONTEXT_RELEASE_COMMAND, encodeUeContextReleaseCommand(cmd));
}

bool NgapLink::ueContextReleaseComplete(uint64_t targetNodeId, const UeContextReleaseComplete& complete) {
    return sendMessage(targetNodeId, NgapProcedure::UE_CONTEXT_RELEASE_COMPLETE, encodeUeContextReleaseComplete(complete));
}

bool NgapLink::recvNgapMessage(NgapMessage& msg) {
    std::lock_guard<std::mutex> lock(rxMutex_);
    if (rxQueue_.empty()) {
        return false;
    }
    msg = std::move(rxQueue_.front());
    rxQueue_.pop();
    if (!msg.traceId.empty()) {
        rbs::Logger::instance().setTraceId(msg.traceId);
    }
    return true;
}

bool NgapLink::sendMessage(uint64_t targetNodeId, NgapProcedure procedure, const ByteBuffer& payload) {
    if (!isConnected(targetNodeId)) {
        RBS_LOG_ERROR("NGAP", "send failed: peer not connected localNodeId=", localNodeId_, " targetNodeId=", targetNodeId);
        return false;
    }

    const auto peerIt = peers_.find(targetNodeId);
    if (peerIt != peers_.end() && peerIt->second.useSctp) {
        if (!sctp_) {
            return false;
        }
        const std::string traceId = rbs::Logger::instance().traceId();
        const ByteBuffer framed = makeNgapTransportFrame(procedure, payload, traceId);
        const bool ok = sctp_->send(framed);
        if (!ok) {
            RBS_LOG_WARNING("NGAP", "SCTP send failed localNodeId=", localNodeId_, " targetNodeId=", targetNodeId);
        }
        return ok;
    }

    NgapLink* peer = nullptr;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        const auto it = registry_.find(targetNodeId);
        if (it == registry_.end()) {
            RBS_LOG_ERROR("NGAP", "send failed: peer missing localNodeId=", localNodeId_, " targetNodeId=", targetNodeId);
            return false;
        }
        peer = it->second;
    }

    peer->enqueue(NgapMessage{procedure, localNodeId_, targetNodeId, payload, rbs::Logger::instance().traceId()});
    return true;
}

void NgapLink::enqueue(NgapMessage&& msg) {
    std::lock_guard<std::mutex> lock(rxMutex_);
    rxQueue_.push(std::move(msg));
}

void NgapLink::handleSctpRx(const rbs::net::SctpPacket& pkt) {
    if (pkt.data.size() < 3) {
        return;
    }
    if (pkt.data[0] != 0x4E || pkt.data[1] != 0x47) {
        return;
    }

    const std::string key = endpointKey(pkt.srcIp, pkt.srcPort);
    uint64_t sourceNodeId = 0;
    const auto it = endpointToNodeId_.find(key);
    if (it != endpointToNodeId_.end()) {
        sourceNodeId = it->second;
    }

    NgapMessage msg{};
    msg.procedure = static_cast<NgapProcedure>(pkt.data[2]);
    msg.sourceNodeId = sourceNodeId;
    msg.targetNodeId = localNodeId_;

    size_t payloadOffset = 3;
    if (pkt.data.size() >= 6 && pkt.data[3] == 0x54 && pkt.data[4] == 0x52) {
        const size_t traceLen = pkt.data[5];
        if (pkt.data.size() >= 6 + traceLen) {
            if (traceLen > 0) {
                msg.traceId.assign(reinterpret_cast<const char*>(pkt.data.data() + 6), traceLen);
            }
            payloadOffset = 6 + traceLen;
        }
    }
    msg.payload = ByteBuffer(pkt.data.begin() + payloadOffset, pkt.data.end());

    const std::string rxTrace = msg.traceId.empty()
        ? rbs::Logger::makeTraceId("ngap-rx", sourceNodeId)
        : msg.traceId;
    RBS_TRACE_SCOPE(rxTrace);
    enqueue(std::move(msg));
}

std::string NgapLink::endpointKey(const std::string& ip, uint16_t port) {
    return ip + ":" + std::to_string(port);
}

}  // namespace rbs::nr
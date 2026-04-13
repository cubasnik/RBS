#include "xnap_link.h"

#include "../common/logger.h"

namespace rbs::nr {

namespace {
ByteBuffer makeXnapTransportFrame(XnAPProcedure procedure, const ByteBuffer& payload, const std::string& traceId) {
    ByteBuffer framed;
    const uint8_t traceLen = static_cast<uint8_t>(std::min<size_t>(traceId.size(), 64));
    framed.reserve(payload.size() + 6 + traceLen);
    framed.push_back(0x38);
    framed.push_back(0x42);
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

std::mutex XnAPLink::registryMutex_;
std::unordered_map<uint64_t, XnAPLink*> XnAPLink::registry_;

XnAPLink::XnAPLink(uint64_t localGnbId, std::string gnbName)
    : localGnbId_(localGnbId)
    , gnbName_(std::move(gnbName))
    , sctp_(std::make_unique<rbs::net::SctpSocket>("XNAP-" + std::to_string(localGnbId)))
{
    std::lock_guard<std::mutex> lock(registryMutex_);
    registry_[localGnbId_] = this;
}

XnAPLink::~XnAPLink() {
    std::lock_guard<std::mutex> lock(registryMutex_);
    auto it = registry_.find(localGnbId_);
    if (it != registry_.end() && it->second == this) {
        registry_.erase(it);
    }
}

bool XnAPLink::connect(uint64_t targetGnbId) {
    std::lock_guard<std::mutex> lock(registryMutex_);
    const auto it = registry_.find(targetGnbId);
    if (it == registry_.end() || it->second == nullptr) {
        RBS_LOG_ERROR("XnAP", "connect failed: localGnbId=", localGnbId_, " targetGnbId=", targetGnbId);
        return false;
    }
    PeerInfo info{};
    info.connected = true;
    peers_[targetGnbId] = info;
    return true;
}

bool XnAPLink::isConnected(uint64_t targetGnbId) const {
    const auto it = peers_.find(targetGnbId);
    return it != peers_.end() && it->second.connected;
}

bool XnAPLink::bindTransport(uint16_t localPort) {
    (void)rbs::net::SctpSocket::wsaInit();
    if (!sctp_) {
        sctp_ = std::make_unique<rbs::net::SctpSocket>("XNAP-" + std::to_string(localGnbId_));
    }
    if (localPort_ != 0 || sctp_->isOpen()) {
        return true;
    }
    if (!sctp_->bind(localPort)) {
        RBS_LOG_ERROR("XnAP", "bindTransport failed: localGnbId=", localGnbId_, " localPort=", localPort);
        return false;
    }
    localPort_ = sctp_->localPort();
    if (!rxStarted_) {
        rxStarted_ = sctp_->startReceive([this](const rbs::net::SctpPacket& pkt) {
            handleSctpRx(pkt);
        });
        if (!rxStarted_) {
            RBS_LOG_ERROR("XnAP", "startReceive failed: localGnbId=", localGnbId_);
            return false;
        }
    }
    return true;
}

bool XnAPLink::connectSctpPeer(uint64_t targetGnbId, const std::string& targetIp, uint16_t targetPort) {
    if (!bindTransport(0)) {
        return false;
    }
    if (!sctp_->connect(targetIp, targetPort)) {
        RBS_LOG_ERROR("XnAP", "connectSctpPeer failed: localGnbId=", localGnbId_,
                      " targetGnbId=", targetGnbId, " ", targetIp, ":", targetPort);
        return false;
    }

    PeerInfo info{};
    info.connected = true;
    info.useSctp = true;
    info.ip = targetIp;
    info.port = targetPort;
    peers_[targetGnbId] = info;
    endpointToNodeId_[endpointKey(targetIp, targetPort)] = targetGnbId;
    return true;
}

bool XnAPLink::bindTransportMulti(const std::vector<std::string>& localAddrs, uint16_t localPort)
{
    (void)rbs::net::SctpSocket::wsaInit();
    if (!sctp_) {
        sctp_ = std::make_unique<rbs::net::SctpSocket>("XNAP-" + std::to_string(localGnbId_));
    }
    if (localPort_ != 0 || sctp_->isOpen()) {
        return true;
    }
    if (!sctp_->bindMulti(localAddrs, localPort)) {
        RBS_LOG_ERROR("XnAP", "bindTransportMulti failed: localGnbId=", localGnbId_);
        return false;
    }
    localPort_ = sctp_->localPort();
    if (!rxStarted_) {
        rxStarted_ = sctp_->startReceive([this](const rbs::net::SctpPacket& pkt) {
            handleSctpRx(pkt);
        });
        if (!rxStarted_) {
            RBS_LOG_ERROR("XnAP", "startReceive (multi) failed: localGnbId=", localGnbId_);
            return false;
        }
    }
    return true;
}

bool XnAPLink::connectSctpPeerMulti(uint64_t targetGnbId, 
                                     const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs,
                                     int primaryIdx)
{
    if (!bindTransport(0)) {
        return false;
    }
    if (!sctp_->connectMulti(remoteAddrs, primaryIdx)) {
        RBS_LOG_ERROR("XnAP", "connectSctpPeerMulti failed: localGnbId=", localGnbId_,
                      " targetGnbId=", targetGnbId, " addresses=", remoteAddrs.size());
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
    peers_[targetGnbId] = info;
    
    // Register all endpoints to this target
    for (size_t i = 0; i < remoteAddrs.size(); ++i) {
        endpointToNodeId_[endpointKey(remoteAddrs[i].first, remoteAddrs[i].second)] = targetGnbId;
    }
    
    RBS_LOG_INFO("XnAP", "connectSctpPeerMulti: localGnbId=", localGnbId_, " targetGnbId=", targetGnbId,
                 " addresses=", remoteAddrs.size(), " primary=", primaryIdx);
    return true;
}

bool XnAPLink::switchToPath(uint64_t targetGnbId, int pathIdx)
{
    const auto it = peers_.find(targetGnbId);
    if (it == peers_.end()) {
        RBS_LOG_ERROR("XnAP", "switchToPath failed: peer not found, localGnbId=", localGnbId_, 
                      " targetGnbId=", targetGnbId);
        return false;
    }

    auto& info = it->second;
    if (info.sctpAddrs.empty()) {
        RBS_LOG_ERROR("XnAP", "switchToPath failed: not multi-homing, localGnbId=", localGnbId_, 
                      " targetGnbId=", targetGnbId);
        return false;
    }

    if (pathIdx < 0 || pathIdx >= static_cast<int>(info.sctpAddrs.size())) {
        RBS_LOG_ERROR("XnAP", "switchToPath failed: invalid pathIdx, localGnbId=", localGnbId_,
                      " targetGnbId=", targetGnbId, " pathIdx=", pathIdx, " count=", info.sctpAddrs.size());
        return false;
    }

    // Update peer info
    info.primaryAddrIdx = pathIdx;
    info.ip = info.sctpAddrs[pathIdx].first;
    info.port = info.sctpAddrs[pathIdx].second;

    // Notify SCTP layer
    if (!sctp_->setPrimaryPath(pathIdx)) {
        RBS_LOG_WARNING("XnAP", "SCTP setPrimaryPath failed: localGnbId=", localGnbId_,
                        " targetGnbId=", targetGnbId, " pathIdx=", pathIdx);
        return false;
    }

    RBS_LOG_INFO("XnAP", "switchToPath: localGnbId=", localGnbId_, " targetGnbId=", targetGnbId,
                 " switched to path ", pathIdx, " (", info.ip, ":", info.port, ")");
    return true;
}


bool XnAPLink::xnSetup(uint64_t targetGnbId, const XnSetupRequest& req) {
    return sendMessage(targetGnbId, XnAPProcedure::XN_SETUP_REQUEST, encodeXnSetupRequest(req));
}

bool XnAPLink::xnSetupResponse(uint64_t targetGnbId, const XnSetupResponse& rsp) {
    return sendMessage(targetGnbId, XnAPProcedure::XN_SETUP_RESPONSE, encodeXnSetupResponse(rsp));
}

bool XnAPLink::handoverRequest(const XnHandoverRequest& req) {
    return sendMessage(req.targetGnbId, XnAPProcedure::HANDOVER_REQUEST, encodeXnHandoverRequest(req));
}

bool XnAPLink::handoverNotify(const XnHandoverNotify& notify) {
    return sendMessage(notify.sourceGnbId, XnAPProcedure::HANDOVER_NOTIFY, encodeXnHandoverNotify(notify));
}

bool XnAPLink::recvXnApMessage(XnAPMessage& msg) {
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

bool XnAPLink::sendMessage(uint64_t targetGnbId, XnAPProcedure procedure, const ByteBuffer& payload) {
    if (!isConnected(targetGnbId)) {
        RBS_LOG_ERROR("XnAP", "send failed: peer not connected localGnbId=", localGnbId_, " targetGnbId=", targetGnbId);
        return false;
    }

    const auto peerIt = peers_.find(targetGnbId);
    if (peerIt != peers_.end() && peerIt->second.useSctp) {
        if (!sctp_) {
            return false;
        }
        const std::string traceId = rbs::Logger::instance().traceId();
        const ByteBuffer framed = makeXnapTransportFrame(procedure, payload, traceId);
        const bool ok = sctp_->send(framed);
        if (!ok) {
            RBS_LOG_WARNING("XnAP", "SCTP send failed localGnbId=", localGnbId_, " targetGnbId=", targetGnbId);
        }
        return ok;
    }

    XnAPLink* peer = nullptr;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        const auto it = registry_.find(targetGnbId);
        if (it == registry_.end()) {
            RBS_LOG_ERROR("XnAP", "send failed: peer missing localGnbId=", localGnbId_, " targetGnbId=", targetGnbId);
            return false;
        }
        peer = it->second;
    }

    peer->enqueue(XnAPMessage{procedure, localGnbId_, targetGnbId, payload, rbs::Logger::instance().traceId()});
    return true;
}

void XnAPLink::enqueue(XnAPMessage&& msg) {
    std::lock_guard<std::mutex> lock(rxMutex_);
    rxQueue_.push(std::move(msg));
}

void XnAPLink::handleSctpRx(const rbs::net::SctpPacket& pkt) {
    if (pkt.data.size() < 3) {
        return;
    }
    if (pkt.data[0] != 0x38 || pkt.data[1] != 0x42) {
        return;
    }

    const std::string key = endpointKey(pkt.srcIp, pkt.srcPort);
    uint64_t sourceGnbId = 0;
    const auto it = endpointToNodeId_.find(key);
    if (it != endpointToNodeId_.end()) {
        sourceGnbId = it->second;
    }

    XnAPMessage msg{};
    msg.procedure = static_cast<XnAPProcedure>(pkt.data[2]);
    msg.sourceGnbId = sourceGnbId;
    msg.targetGnbId = localGnbId_;

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
        ? rbs::Logger::makeTraceId("xnap-rx", sourceGnbId)
        : msg.traceId;
    RBS_TRACE_SCOPE(rxTrace);
    enqueue(std::move(msg));
}

std::string XnAPLink::endpointKey(const std::string& ip, uint16_t port) {
    return ip + ":" + std::to_string(port);
}

}  // namespace rbs::nr
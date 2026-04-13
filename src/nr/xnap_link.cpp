#include "xnap_link.h"

#include "../common/logger.h"

namespace rbs::nr {

namespace {
ByteBuffer makeXnapTransportFrame(XnAPProcedure procedure, const ByteBuffer& payload) {
    ByteBuffer framed;
    framed.reserve(payload.size() + 3);
    framed.push_back(0x38);
    framed.push_back(0x42);
    framed.push_back(static_cast<uint8_t>(procedure));
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
        const ByteBuffer framed = makeXnapTransportFrame(procedure, payload);
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

    peer->enqueue(XnAPMessage{procedure, localGnbId_, targetGnbId, payload});
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
    msg.payload = ByteBuffer(pkt.data.begin() + 3, pkt.data.end());
    enqueue(std::move(msg));
}

std::string XnAPLink::endpointKey(const std::string& ip, uint16_t port) {
    return ip + ":" + std::to_string(port);
}

}  // namespace rbs::nr
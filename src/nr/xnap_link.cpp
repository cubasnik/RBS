#include "xnap_link.h"

#include "../common/logger.h"

namespace rbs::nr {

std::mutex XnAPLink::registryMutex_;
std::unordered_map<uint64_t, XnAPLink*> XnAPLink::registry_;

XnAPLink::XnAPLink(uint64_t localGnbId, std::string gnbName)
    : localGnbId_(localGnbId)
    , gnbName_(std::move(gnbName))
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
    peers_[targetGnbId] = true;
    return true;
}

bool XnAPLink::isConnected(uint64_t targetGnbId) const {
    const auto it = peers_.find(targetGnbId);
    return it != peers_.end() && it->second;
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

}  // namespace rbs::nr
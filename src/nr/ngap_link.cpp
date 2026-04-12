#include "ngap_link.h"

#include "../common/logger.h"

namespace rbs::nr {

std::mutex NgapLink::registryMutex_;
std::unordered_map<uint64_t, NgapLink*> NgapLink::registry_;

NgapLink::NgapLink(uint64_t localNodeId)
    : localNodeId_(localNodeId)
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
    peers_[targetNodeId] = true;
    return true;
}

bool NgapLink::isConnected(uint64_t targetNodeId) const {
    const auto it = peers_.find(targetNodeId);
    return it != peers_.end() && it->second;
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
    return true;
}

bool NgapLink::sendMessage(uint64_t targetNodeId, NgapProcedure procedure, const ByteBuffer& payload) {
    if (!isConnected(targetNodeId)) {
        RBS_LOG_ERROR("NGAP", "send failed: peer not connected localNodeId=", localNodeId_, " targetNodeId=", targetNodeId);
        return false;
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

    peer->enqueue(NgapMessage{procedure, localNodeId_, targetNodeId, payload});
    return true;
}

void NgapLink::enqueue(NgapMessage&& msg) {
    std::lock_guard<std::mutex> lock(rxMutex_);
    rxQueue_.push(std::move(msg));
}

}  // namespace rbs::nr
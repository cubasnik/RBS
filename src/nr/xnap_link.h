#pragma once

#include "xnap_codec.h"
#include "../common/sctp_socket.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace rbs::nr {

struct XnAPMessage {
    XnAPProcedure procedure = XnAPProcedure::XN_SETUP_REQUEST;
    uint64_t sourceGnbId = 0;
    uint64_t targetGnbId = 0;
    ByteBuffer payload;
};

class XnAPLink {
public:
    explicit XnAPLink(uint64_t localGnbId, std::string gnbName = {});
    ~XnAPLink();

    // Transport management for real Xn interface over SCTP/IP.
    bool bindTransport(uint16_t localPort = 0);
    bool connectSctpPeer(uint64_t targetGnbId, const std::string& targetIp, uint16_t targetPort);

    bool connect(uint64_t targetGnbId);
    bool isConnected(uint64_t targetGnbId) const;

    bool xnSetup(uint64_t targetGnbId, const XnSetupRequest& req);
    bool xnSetupResponse(uint64_t targetGnbId, const XnSetupResponse& rsp);
    bool handoverRequest(const XnHandoverRequest& req);
    bool handoverNotify(const XnHandoverNotify& notify);

    bool recvXnApMessage(XnAPMessage& msg);

    uint64_t localGnbId() const { return localGnbId_; }
    const std::string& gnbName() const { return gnbName_; }

private:
    struct PeerInfo {
        bool connected = false;
        bool useSctp = false;
        std::string ip;
        uint16_t port = 0;
    };

    bool sendMessage(uint64_t targetGnbId, XnAPProcedure procedure, const ByteBuffer& payload);
    void enqueue(XnAPMessage&& msg);
    void handleSctpRx(const rbs::net::SctpPacket& pkt);
    static std::string endpointKey(const std::string& ip, uint16_t port);

    uint64_t localGnbId_;
    std::string gnbName_;
    std::unordered_map<uint64_t, PeerInfo> peers_;
    std::unordered_map<std::string, uint64_t> endpointToNodeId_;
    std::unique_ptr<rbs::net::SctpSocket> sctp_;
    uint16_t localPort_ = 0;
    bool rxStarted_ = false;
    mutable std::mutex rxMutex_;
    std::queue<XnAPMessage> rxQueue_;

    static std::mutex registryMutex_;
    static std::unordered_map<uint64_t, XnAPLink*> registry_;
};

}  // namespace rbs::nr
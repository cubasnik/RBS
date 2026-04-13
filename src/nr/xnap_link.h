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
    std::string traceId;
};

class XnAPLink {
public:
    explicit XnAPLink(uint64_t localGnbId, std::string gnbName = {});
    ~XnAPLink();

    // Transport management for real Xn interface over SCTP/IP.
    bool bindTransport(uint16_t localPort = 0);
    
    // Multi-homing: bind to multiple local addresses for load distribution.
    // Returns true if at least one address bound successfully.
    bool bindTransportMulti(const std::vector<std::string>& localAddrs, uint16_t localPort = 0);
    
    bool connectSctpPeer(uint64_t targetGnbId, const std::string& targetIp, uint16_t targetPort);
    
    // Multi-homing: connect to multiple remote addresses with primary path selection.
    // remoteAddrs: list of {IP, port} pairs; primaryIdx: which one is primary (default 0).
    // Returns true if connection initiated successfully.
    bool connectSctpPeerMulti(uint64_t targetGnbId, 
                             const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs, 
                             int primaryIdx = 0);
    
    // Failover: switch to a different remote address (multi-homing only).
    // Requires prior connectSctpPeerMulti() call for the target.
    bool switchToPath(uint64_t targetGnbId, int pathIdx);

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
        std::string ip;                                              // Primary IP (single-address mode)
        uint16_t port = 0;                                           // Primary port
        std::vector<std::pair<std::string, uint16_t>> sctpAddrs;     // All remote addresses (multi-homing)
        int primaryAddrIdx = 0;                                       // Index of primary address in sctpAddrs
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
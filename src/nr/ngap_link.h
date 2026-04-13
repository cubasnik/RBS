#pragma once

#include "ngap_codec.h"
#include "../common/sctp_socket.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace rbs::nr {

struct NgapMessage {
    NgapProcedure procedure = NgapProcedure::NG_SETUP_REQUEST;
    uint64_t sourceNodeId = 0;
    uint64_t targetNodeId = 0;
    ByteBuffer payload;
    std::string traceId;
};

class NgapLink {
public:
    explicit NgapLink(uint64_t localNodeId);
    ~NgapLink();

    // Transport management for real NG interface over SCTP/IP.
    bool bindTransport(uint16_t localPort = 0);
    
    // Multi-homing: bind to multiple local addresses for load distribution.
    // Returns true if at least one address bound successfully.
    bool bindTransportMulti(const std::vector<std::string>& localAddrs, uint16_t localPort = 0);
    
    bool connectSctpPeer(uint64_t targetNodeId, const std::string& targetIp, uint16_t targetPort);
    
    // Multi-homing: connect to multiple remote addresses with primary path selection.
    // remoteAddrs: list of {IP, port} pairs; primaryIdx: which one is primary (default 0).
    // Returns true if connection initiated successfully.
    bool connectSctpPeerMulti(uint64_t targetNodeId, 
                             const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs, 
                             int primaryIdx = 0);
    
    // Failover: switch to a different remote address (multi-homing only).
    // Requires prior connectSctpPeerMulti() call for the target.
    bool switchToPath(uint64_t targetNodeId, int pathIdx);

    bool connect(uint64_t targetNodeId);
    bool isConnected(uint64_t targetNodeId) const;

    bool ngSetup(uint64_t targetNodeId, const NgSetupRequest& req);
    bool ngSetupResponse(uint64_t targetNodeId, const NgSetupResponse& rsp);
    bool pduSessionSetupRequest(uint64_t targetNodeId, const PduSessionSetupRequest& req);
    bool pduSessionSetupResponse(uint64_t targetNodeId, const PduSessionSetupResponse& rsp);
    bool paging(uint64_t targetNodeId, const PagingMessage& paging);
    bool ueContextReleaseCommand(uint64_t targetNodeId, const UeContextReleaseCommand& cmd);
    bool ueContextReleaseComplete(uint64_t targetNodeId, const UeContextReleaseComplete& complete);

    bool recvNgapMessage(NgapMessage& msg);

private:
    struct PeerInfo {
        bool connected = false;
        bool useSctp = false;
        std::string ip;                                              // Primary IP (single-address mode)
        uint16_t port = 0;                                           // Primary port
        std::vector<std::pair<std::string, uint16_t>> sctpAddrs;     // All remote addresses (multi-homing)
        int primaryAddrIdx = 0;                                       // Index of primary address in sctpAddrs
    };

    bool sendMessage(uint64_t targetNodeId, NgapProcedure procedure, const ByteBuffer& payload);
    void enqueue(NgapMessage&& msg);
    void handleSctpRx(const rbs::net::SctpPacket& pkt);
    static std::string endpointKey(const std::string& ip, uint16_t port);

    uint64_t localNodeId_;
    std::unordered_map<uint64_t, PeerInfo> peers_;
    std::unordered_map<std::string, uint64_t> endpointToNodeId_;
    std::unique_ptr<rbs::net::SctpSocket> sctp_;
    uint16_t localPort_ = 0;
    bool rxStarted_ = false;
    mutable std::mutex rxMutex_;
    std::queue<NgapMessage> rxQueue_;

    static std::mutex registryMutex_;
    static std::unordered_map<uint64_t, NgapLink*> registry_;
};

}  // namespace rbs::nr
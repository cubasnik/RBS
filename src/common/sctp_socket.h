#pragma once

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using sctp_sock_t = SOCKET;
#  define SCTP_SOCK_INVALID INVALID_SOCKET
#  define SCTP_SOCK_ERR     SOCKET_ERROR
#else
#  include <sys/socket.h>
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <unistd.h>
   using sctp_sock_t = int;
#  define SCTP_SOCK_INVALID (-1)
#  define SCTP_SOCK_ERR     (-1)
#  define closesocket       close
#endif

#include "../common/types.h"
#include "../common/logger.h"
#include "udp_socket.h"

#include <string>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <optional>

namespace rbs::net {

struct SctpPacket {
    std::string srcIp;
    uint16_t    srcPort;
    ByteBuffer  data;
};

// ─────────────────────────────────────────────────────────────────────────
// Performance tuning parameters for SCTP socket
// ─────────────────────────────────────────────────────────────────────────

struct SctpTuning {
    // Heartbeat: keep-alive interval in milliseconds (0 = disabled)
    std::optional<uint32_t> heartbeatInterval;
    
    // Retransmit timeout (RTO) bounds: initial/min/max in milliseconds
    // RFC 4960 default: initial=3000ms, min=1000ms, max=60000ms
    std::optional<uint32_t> rtoInitial;
    std::optional<uint32_t> rtoMin;
    std::optional<uint32_t> rtoMax;
    
    // Init retransmit parameters
    // numAttempts: how many INIT attempts before abort (default 8)
    // maxTimeout: max backoff time in milliseconds (default 60000ms)
    std::optional<uint16_t> initNumAttempts;
    std::optional<uint32_t> initMaxTimeout;
    
    // Socket buffer sizes
    std::optional<uint32_t> rcvBufSize;
    std::optional<uint32_t> sndBufSize;
};

// SCTP notification event type
enum class SctpNotificationType {
    ASSOC_CHANGE,      // Association state changed
    PEER_ADDR_CHANGE,  // Peer address state changed
    SEND_FAILED,       // Send failed
    SHUTDOWN_EVENT,    // Shutdown event
    UNKNOWN,
};

struct SctpNotification {
    SctpNotificationType type = SctpNotificationType::UNKNOWN;
    uint64_t associationId = 0;
    std::string description;
    ByteBuffer rawData;  // Raw notification data for deep inspection
};

using NotificationCallback = std::function<void(const SctpNotification&)>;

class SctpSocket {
public:
    using RxCallback = std::function<void(const SctpPacket&)>;

    explicit SctpSocket(const std::string& name);
    ~SctpSocket();

    static bool wsaInit();
    static void wsaCleanup();

    static bool nativeSupported();

    // Single-address binding (backward compatible)
    bool bind(uint16_t localPort);
    
    // Multi-homing: bind to multiple local addresses.
    // On Linux + native SCTP, uses sctp_bindx with SCTP_BINDX_ADD_ADDR.
    // On Windows usrsctp or UDP fallback, limits to first address (multi-homing unsupported).
    // Returns true if at least one address bound successfully.
    bool bindMulti(const std::vector<std::string>& localAddrs, uint16_t localPort = 0);

    // Single-address connection (backward compatible)
    bool connect(const std::string& remoteIp, uint16_t remotePort);
    
    // Multi-homing: connect to multiple remote addresses with failover support.
    // On Linux + native SCTP, uses sctp_connectx.
    // primaryIdx: index of primary address (default 0, must be < remoteAddrs.size()).
    // Returns true if connection initiated successfully to primary address.
    bool connectMulti(const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs, int primaryIdx = 0);

    // Set primary path for failover (Linux + native SCTP only).
    // primaryIdx: new primary address index.
    // Returns true if successfully set.
    bool setPrimaryPath(int primaryIdx);

    // Performance tuning: apply SCTP socket options.
    // Called after bind() but before connect for optimal effect.
    // Returns true if all parameters successfully applied.
    bool applyTuning(const SctpTuning& tuning);

    // Notification callback: called for SCTP events (ASSOC_CHANGE, SEND_FAILED, etc.)
    // Set before startReceive() to capture events.
    void setNotificationCallback(NotificationCallback cb);

    uint16_t localPort() const { return localPort_; }
    int primaryRemoteIdx() const { return primaryRemoteIdx_; }
    size_t remoteAddrsCount() const { return remoteAddrs_.size(); }

    bool send(const uint8_t* data, size_t len);
    bool send(const ByteBuffer& buf) { return send(buf.data(), buf.size()); }

    bool startReceive(RxCallback cb);
    void close();

    bool isOpen() const;

private:
    std::string name_;
    std::string remoteIp_;                                      // Primary remote IP (for single-addr mode)
    uint16_t remotePort_ = 0;                                   // Primary remote port
    std::vector<std::pair<std::string, uint16_t>> remoteAddrs_; // All remote addresses (multi-homing)
    int primaryRemoteIdx_ = 0;                                  // Index of primary remote address
    uint16_t localPort_ = 0;

    bool useUdpFallback_ = true;
    UdpSocket udpFallback_;

    sctp_sock_t sock_ = SCTP_SOCK_INVALID;  // native SCTP fd (Linux)
#ifdef _WIN32
    void* usrSock_ = nullptr;               // struct socket* (usrsctp, Windows)
#endif
    std::atomic<bool> running_{false};
    std::thread rxThread_;
    RxCallback rxCb_;
    NotificationCallback notificationCb_;

    bool bindNative(uint16_t localPort);
    bool bindNativeMulti(const std::vector<std::string>& localAddrs, uint16_t localPort);
    bool connectNative(const std::string& remoteIp, uint16_t remotePort);
    bool connectNativeMulti(const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs, int primaryIdx);
    bool setPrimaryPathNative(int primaryIdx);
    bool applyTuningNative(const SctpTuning& tuning);
    bool sendNative(const uint8_t* data, size_t len);
    bool startReceiveNative(RxCallback cb);
    void closeNative();
    void rxLoopNative();
    void processNotification(const ByteBuffer& notifData);
};

} // namespace rbs::net

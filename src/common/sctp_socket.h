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

namespace rbs::net {

struct SctpPacket {
    std::string srcIp;
    uint16_t    srcPort;
    ByteBuffer  data;
};

class SctpSocket {
public:
    using RxCallback = std::function<void(const SctpPacket&)>;

    explicit SctpSocket(const std::string& name);
    ~SctpSocket();

    static bool wsaInit();
    static void wsaCleanup();

    static bool nativeSupported();

    bool bind(uint16_t localPort);
    bool connect(const std::string& remoteIp, uint16_t remotePort);

    uint16_t localPort() const { return localPort_; }

    bool send(const uint8_t* data, size_t len);
    bool send(const ByteBuffer& buf) { return send(buf.data(), buf.size()); }

    bool startReceive(RxCallback cb);
    void close();

    bool isOpen() const;

private:
    std::string name_;
    std::string remoteIp_;
    uint16_t remotePort_ = 0;
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

    bool bindNative(uint16_t localPort);
    bool connectNative(const std::string& remoteIp, uint16_t remotePort);
    bool sendNative(const uint8_t* data, size_t len);
    bool startReceiveNative(RxCallback cb);
    void closeNative();
    void rxLoopNative();
};

} // namespace rbs::net

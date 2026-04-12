#pragma once

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using tcp_sock_t = SOCKET;
#  define TCP_SOCK_INVALID INVALID_SOCKET
#  define TCP_SOCK_ERR     SOCKET_ERROR
#else
#  include <sys/socket.h>
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <unistd.h>
   using tcp_sock_t = int;
#  define TCP_SOCK_INVALID (-1)
#  define TCP_SOCK_ERR     (-1)
#  define closesocket       close
#endif

#include "../common/types.h"
#include "../common/logger.h"
#include <string>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include <vector>

namespace rbs::net {

struct TcpPacket {
    std::string srcIp;
    uint16_t    srcPort;
    ByteBuffer  data;
};

/// TCP Socket with async receive callback.
/// Used for Abis-over-IP (IPA protocol) and other point-to-point connections.
class TcpSocket {
public:
    using RxCallback = std::function<void(const TcpPacket&)>;

    explicit TcpSocket(const std::string& name);
    ~TcpSocket();

    static bool wsaInit();
    static void wsaCleanup();

    bool bind(uint16_t localPort);
    bool connect(const std::string& remoteIp, uint16_t remotePort);
    bool listen(uint16_t localPort, int backlog = 5);
    bool accept(TcpSocket& clientSocket);

    uint16_t localPort() const { return localPort_; }
    std::string remoteIp() const { return remoteIp_; }
    uint16_t remotePort() const { return remotePort_; }

    bool send(const uint8_t* data, size_t len);
    bool send(const ByteBuffer& buf) { return send(buf.data(), buf.size()); }

    void startReceive(RxCallback cb);
    void stopReceive();

    bool isConnected() const { return connected_; }
    void close();

private:
    std::string  name_;
    tcp_sock_t   socket_ = TCP_SOCK_INVALID;
    bool         connected_ = false;
    uint16_t     localPort_ = 0;
    std::string  remoteIp_;
    uint16_t     remotePort_ = 0;

    std::atomic<bool> rxRunning_{false};
    std::thread rxThread_;

    void rxThreadFunc(RxCallback cb);
};

} // namespace rbs::net

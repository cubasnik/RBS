#include "tcp_socket.h"
#include <cstring>
#include <chrono>

namespace rbs::net {

static bool wsaInitialized = false;

bool TcpSocket::wsaInit() {
#ifdef _WIN32
    if (wsaInitialized) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        RBS_LOG_ERROR("TcpSocket", "WSAStartup failed: {}", WSAGetLastError());
        return false;
    }
    wsaInitialized = true;
    RBS_LOG_DEBUG("TcpSocket", "WSA initialized");
    return true;
#else
    return true;  // Linux doesn't need WSA init
#endif
}

void TcpSocket::wsaCleanup() {
#ifdef _WIN32
    if (wsaInitialized) {
        WSACleanup();
        wsaInitialized = false;
    }
#endif
}

TcpSocket::TcpSocket(const std::string& name)
    : name_(name) {
    wsaInit();
}

TcpSocket::~TcpSocket() {
    stopReceive();
    close();
}

bool TcpSocket::bind(uint16_t localPort) {
    if (socket_ != TCP_SOCK_INVALID) {
        RBS_LOG_WARNING("TcpSocket", "[{}] bind: socket already bound", name_);
        return false;
    }

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == TCP_SOCK_INVALID) {
        RBS_LOG_ERROR("TcpSocket", "[{}] bind: socket() failed", name_);
        return false;
    }

    // Set SO_REUSEADDR to allow quick rebind after close
    int reuseaddr = 1;
    if (::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                     (const char*)&reuseaddr, sizeof(reuseaddr)) < 0) {
        RBS_LOG_WARNING("TcpSocket", "[{}] bind: setsockopt SO_REUSEADDR failed", name_);
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(localPort);

    if (::bind(socket_, (struct sockaddr*)&addr, sizeof(addr)) == TCP_SOCK_ERR) {
        RBS_LOG_ERROR("TcpSocket", "[{}] bind: bind() failed for port {}", name_, localPort);
        closesocket(socket_);
        socket_ = TCP_SOCK_INVALID;
        return false;
    }

    localPort_ = localPort;
    RBS_LOG_DEBUG("TcpSocket", "[{}] bind: port {}", name_, localPort);
    return true;
}

bool TcpSocket::listen(uint16_t localPort, int backlog) {
    if (!bind(localPort)) {
        return false;
    }
    
    if (::listen(socket_, backlog) == TCP_SOCK_ERR) {
        RBS_LOG_ERROR("TcpSocket", "[{}] listen: listen() failed", name_);
        closesocket(socket_);
        socket_ = TCP_SOCK_INVALID;
        return false;
    }
    
    RBS_LOG_DEBUG("TcpSocket", "[{}] listen: port {}", name_, localPort);
    return true;
}

bool TcpSocket::accept(TcpSocket& clientSocket) {
    if (socket_ == TCP_SOCK_INVALID) {
        RBS_LOG_ERROR("TcpSocket", "[{}] accept: not listening", name_);
        return false;
    }
    
    struct sockaddr_in clientAddr;
#ifdef _WIN32
    int clientAddrLen = sizeof(clientAddr);
#else
    socklen_t clientAddrLen = sizeof(clientAddr);
#endif
    std::memset(&clientAddr, 0, sizeof(clientAddr));
    
    tcp_sock_t clientSock = ::accept(socket_, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSock == TCP_SOCK_INVALID) {
        RBS_LOG_ERROR("TcpSocket", "[{}] accept: accept() failed", name_);
        return false;
    }
    
    clientSocket.socket_ = clientSock;
    clientSocket.connected_ = true;
    clientSocket.remoteIp_ = inet_ntoa(clientAddr.sin_addr);
    clientSocket.remotePort_ = ntohs(clientAddr.sin_port);
    clientSocket.localPort_ = localPort_;
    
    RBS_LOG_DEBUG("TcpSocket", "[{}] accept: client {}:{}", name_, 
                  clientSocket.remoteIp_, clientSocket.remotePort_);
    return true;
}

bool TcpSocket::connect(const std::string& remoteIp, uint16_t remotePort) {
    if (socket_ != TCP_SOCK_INVALID) {
        RBS_LOG_WARNING("TcpSocket", "[{}] connect: socket already connected", name_);
        return false;
    }

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == TCP_SOCK_INVALID) {
        RBS_LOG_ERROR("TcpSocket", "[{}] connect: socket() failed", name_);
        return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remotePort);

#ifdef _WIN32
    // Windows: use InetPton
    if (InetPton(AF_INET, remoteIp.c_str(), &addr.sin_addr) <= 0) {
        RBS_LOG_ERROR("TcpSocket", "[{}] connect: invalid IP {}", name_, remoteIp);
        closesocket(socket_);
        socket_ = TCP_SOCK_INVALID;
        return false;
    }
#else
    // Linux: use inet_pton
    if (inet_pton(AF_INET, remoteIp.c_str(), &addr.sin_addr) <= 0) {
        RBS_LOG_ERROR("TcpSocket", "[{}] connect: invalid IP {}", name_, remoteIp);
        closesocket(socket_);
        socket_ = TCP_SOCK_INVALID;
        return false;
    }
#endif

    if (::connect(socket_, (struct sockaddr*)&addr, sizeof(addr)) == TCP_SOCK_ERR) {
        RBS_LOG_ERROR("TcpSocket", "[{}] connect: connect() failed {}:{}", 
                      name_, remoteIp, remotePort);
        closesocket(socket_);
        socket_ = TCP_SOCK_INVALID;
        return false;
    }

    connected_ = true;
    remoteIp_ = remoteIp;
    remotePort_ = remotePort;
    RBS_LOG_INFO("TcpSocket", "[{}] connect: {}:{}", name_, remoteIp, remotePort);
    return true;
}

bool TcpSocket::send(const uint8_t* data, size_t len) {
    if (!connected_ || socket_ == TCP_SOCK_INVALID) {
        RBS_LOG_WARNING("TcpSocket", "[{}] send: not connected", name_);
        return false;
    }

    if (::send(socket_, (const char*)data, (int)len, 0) == TCP_SOCK_ERR) {
        RBS_LOG_ERROR("TcpSocket", "[{}] send: send() failed", name_);
        connected_ = false;
        return false;
    }

    return true;
}

void TcpSocket::startReceive(RxCallback cb) {
    if (rxRunning_) return;
    if (!connected_ && socket_ == TCP_SOCK_INVALID) {
        RBS_LOG_WARNING("TcpSocket", "[{}] startReceive: not connected", name_);
        return;
    }

    rxRunning_ = true;
    rxThread_ = std::thread([this, cb]() { rxThreadFunc(cb); });
}

void TcpSocket::stopReceive() {
    if (!rxRunning_) return;
    rxRunning_ = false;
    if (rxThread_.joinable()) {
        rxThread_.join();
    }
}

void TcpSocket::rxThreadFunc(RxCallback cb) {
    const size_t BUF_SIZE = 65536;
    std::vector<uint8_t> buf(BUF_SIZE);

    RBS_LOG_DEBUG("TcpSocket", "[{}] RX thread started", name_);

    while (rxRunning_ && (connected_ || socket_ != TCP_SOCK_INVALID)) {
        if (socket_ == TCP_SOCK_INVALID) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int n = ::recv(socket_, (char*)buf.data(), (int)BUF_SIZE, 0);
        if (n <= 0) {
            RBS_LOG_DEBUG("TcpSocket", "[{}] RX: connection closed (recv={})", name_, n);
            connected_ = false;
            break;
        }

        TcpPacket pkt;
        pkt.srcIp = remoteIp_;
        pkt.srcPort = remotePort_;
        pkt.data.assign(buf.begin(), buf.begin() + n);

        if (cb) {
            cb(pkt);
        }
    }

    RBS_LOG_DEBUG("TcpSocket", "[{}] RX thread stopped", name_);
}

void TcpSocket::close() {
    stopReceive();
    if (socket_ != TCP_SOCK_INVALID) {
        closesocket(socket_);
        socket_ = TCP_SOCK_INVALID;
    }
    connected_ = false;
    RBS_LOG_DEBUG("TcpSocket", "[{}] closed", name_);
}

} // namespace rbs::net

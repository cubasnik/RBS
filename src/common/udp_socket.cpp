#include "udp_socket.h"
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#  pragma comment(lib, "Ws2_32.lib")
#  include <mstcpip.h>
#  ifndef SIO_UDP_CONNRESET
#    define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#  endif
#endif

namespace rbs::net {

// ─────────────────────────────────────────────────────────────────────────────
// WSA lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool UdpSocket::wsaInit()
{
#ifdef _WIN32
    WSADATA wd{};
    int rc = WSAStartup(MAKEWORD(2, 2), &wd);
    if (rc != 0) {
        RBS_LOG_ERROR("UdpSocket", "WSAStartup ошибка: {}", rc);
        return false;
    }
    RBS_LOG_DEBUG("UdpSocket", "WSAStartup OK (v{}.{})",
                  LOBYTE(wd.wVersion), HIBYTE(wd.wVersion));
#endif
    return true;
}

void UdpSocket::wsaCleanup()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// UdpSocket
// ─────────────────────────────────────────────────────────────────────────────

UdpSocket::UdpSocket(const std::string& name)
    : name_(name)
{}

UdpSocket::~UdpSocket()
{
    close();
}

bool UdpSocket::bind(uint16_t localPort)
{
    if (sock_ != SOCK_INVALID) {
        RBS_LOG_WARNING("UdpSocket", "[{}] сокет уже открыт", name_);
        return true;
    }

    sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == SOCK_INVALID) {
        RBS_LOG_ERROR("UdpSocket", "[{}] socket() failed", name_);
        return false;
    }

    // SO_REUSEADDR — разрешить повторное открытие порта после перезапуска
    int opt = 1;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(localPort);

    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR) {
        RBS_LOG_ERROR("UdpSocket", "[{}] bind(:{}) failed", name_, localPort);
        ::closesocket(sock_);
        sock_ = SOCK_INVALID;
        return false;
    }

#ifdef _WIN32
    // On Windows, disable UDP connection reset notifications (WSAECONNRESET on recvfrom).
    DWORD bytesReturned = 0;
    BOOL newBehavior = FALSE;
    ::WSAIoctl(sock_, SIO_UDP_CONNRESET,
               &newBehavior, sizeof(newBehavior),
               nullptr, 0, &bytesReturned,
               nullptr, nullptr);
#endif

    // Считать реальный порт (актуально при localPort==0)
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(sock_, reinterpret_cast<sockaddr*>(&bound), &len);
    localPort_ = ntohs(bound.sin_port);

    RBS_LOG_INFO("UdpSocket", "[{}] UDP сокет привязан к порту {}", name_, localPort_);
    return true;
}

bool UdpSocket::send(const std::string& remoteIp, uint16_t remotePort,
                     const uint8_t* data, size_t len)
{
    if (sock_ == SOCK_INVALID) {
        RBS_LOG_ERROR("UdpSocket", "[{}] send: сокет не открыт", name_);
        return false;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(remotePort);
    if (::inet_pton(AF_INET, remoteIp.c_str(), &dest.sin_addr) != 1) {
        RBS_LOG_ERROR("UdpSocket", "[{}] send: некорректный IP {}", name_, remoteIp);
        return false;
    }

    int sent = ::sendto(sock_,
                        reinterpret_cast<const char*>(data),
                        static_cast<int>(len), 0,
                        reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    if (sent == SOCK_ERR) {
        RBS_LOG_ERROR("UdpSocket", "[{}] sendto() failed → {}:{}", name_, remoteIp, remotePort);
        return false;
    }
    RBS_LOG_DEBUG("UdpSocket", "[{}] UDP {} байт → {}:{}", name_, sent, remoteIp, remotePort);
    return true;
}

bool UdpSocket::startReceive(RxCallback cb)
{
    if (!isOpen()) {
        RBS_LOG_ERROR("UdpSocket", "[{}] startReceive: сокет не привязан", name_);
        return false;
    }
    rxCb_    = std::move(cb);
    running_ = true;
    rxThread_ = std::thread(&UdpSocket::rxLoop, this);
    RBS_LOG_INFO("UdpSocket", "[{}] поток приёма запущен (порт {})", name_, localPort_);
    return true;
}

void UdpSocket::close()
{
    running_ = false;
    if (sock_ != SOCK_INVALID) {
        ::closesocket(sock_);
        sock_ = SOCK_INVALID;
    }
    if (rxThread_.joinable()) rxThread_.join();
}

void UdpSocket::rxLoop()
{
    constexpr int BUF_SZ = 65536;
    std::vector<uint8_t> buf(BUF_SZ);

    while (running_) {
        sockaddr_in src{};
        socklen_t   srcLen = sizeof(src);

        int n = ::recvfrom(sock_,
                           reinterpret_cast<char*>(buf.data()),
                           BUF_SZ, 0,
                           reinterpret_cast<sockaddr*>(&src), &srcLen);

        if (n == SOCK_ERR) {
            if (running_) {
                RBS_LOG_WARNING("UdpSocket", "[{}] recvfrom ошибка, завершаем поток", name_);
            }
            break;
        }

        if (n == 0 || !rxCb_) continue;

        char ipStr[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &src.sin_addr, ipStr, sizeof(ipStr));

        UdpPacket pkt;
        pkt.srcIp   = ipStr;
        pkt.srcPort = ntohs(src.sin_port);
        pkt.data    = ByteBuffer(buf.data(), buf.data() + n);

        rxCb_(pkt);
    }
    RBS_LOG_DEBUG("UdpSocket", "[{}] поток приёма остановлен", name_);
}

} // namespace rbs::net

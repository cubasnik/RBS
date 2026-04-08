#pragma once

// ── Windows Winsock2 ──────────────────────────────────────────────────────────
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using sock_t = SOCKET;
#  define SOCK_INVALID  INVALID_SOCKET
#  define SOCK_ERR      SOCKET_ERROR
#else
#  include <sys/socket.h>
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <fcntl.h>
   using sock_t = int;
#  define SOCK_INVALID  (-1)
#  define SOCK_ERR      (-1)
#  define closesocket   close
#endif

#include "../common/types.h"
#include "../common/logger.h"
#include <string>
#include <cstdint>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <functional>

namespace rbs::net {

// ─────────────────────────────────────────────────────────────────────────────
// UdpSocket — тонкая кросс-платформенная обёртка над SOCK_DGRAM.
//
// Использование:
//   UdpSocket s("ENB-S1U");
//   s.bind(2152);                       // открыть локальный порт
//   s.send(remoteIp, 2152, buf, len);   // послать пакет
//   s.startReceive(cb);                 // запустить фоновый поток чтения
//   s.close();
// ─────────────────────────────────────────────────────────────────────────────

struct UdpPacket {
    std::string  srcIp;
    uint16_t     srcPort;
    ByteBuffer   data;
};

class UdpSocket {
public:
    using RxCallback = std::function<void(const UdpPacket&)>;

    explicit UdpSocket(const std::string& name);
    ~UdpSocket();

    /// Инициализировать Winsock (вызывается один раз в процессе).
    static bool wsaInit();
    static void wsaCleanup();

    /// Создать UDP сокет и привязать к порту (0 = случайный порт).
    bool bind(uint16_t localPort);

    /// Возвращает назначенный локальный порт (актуально при port=0).
    uint16_t localPort() const { return localPort_; }

    /// Послать данные на remoteIp:remotePort.
    bool send(const std::string& remoteIp, uint16_t remotePort,
              const uint8_t* data, size_t len);
    bool send(const std::string& remoteIp, uint16_t remotePort,
              const ByteBuffer& buf) {
        return send(remoteIp, remotePort, buf.data(), buf.size());
    }

    /// Запустить фоновый поток приёма; cb вызывается из потока приёма.
    bool startReceive(RxCallback cb);

    /// Остановить фоновый поток и закрыть сокет.
    void close();

    bool isOpen() const { return sock_ != SOCK_INVALID; }

private:
    std::string      name_;
    sock_t           sock_      = SOCK_INVALID;
    uint16_t         localPort_ = 0;
    std::atomic<bool> running_{false};
    std::thread      rxThread_;
    RxCallback       rxCb_;

    void rxLoop();
};

} // namespace rbs::net

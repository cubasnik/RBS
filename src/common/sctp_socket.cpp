#include "sctp_socket.h"

#include <chrono>
#include <cstring>

// ── usrsctp (Windows userspace SCTP) ───────────────────────────────────────
#if defined(RBS_USE_USRSCTP)
#  include <usrsctp.h>
#  define RBS_HAS_USRSCTP 1
#else
#  define RBS_HAS_USRSCTP 0
#endif

// ── native kernel SCTP (Linux) ───────────────────────────────────────
#if !defined(_WIN32) && __has_include(<netinet/sctp.h>)
#  include <netinet/sctp.h>
#  define RBS_HAS_NATIVE_SCTP 1
#else
#  define RBS_HAS_NATIVE_SCTP 0
#endif

namespace rbs::net {

bool SctpSocket::wsaInit()
{
    bool ok = UdpSocket::wsaInit();
#if RBS_HAS_USRSCTP
    static bool usrsctpInited = false;
    if (!usrsctpInited) {
        usrsctp_init(0, nullptr, nullptr);
        usrsctpInited = true;
    }
#endif
    return ok;
}

void SctpSocket::wsaCleanup()
{
#if RBS_HAS_USRSCTP
    usrsctp_finish();
#endif
    UdpSocket::wsaCleanup();
}

bool SctpSocket::nativeSupported()
{
#if RBS_HAS_USRSCTP || RBS_HAS_NATIVE_SCTP
    return true;
#else
    return false;
#endif
}

SctpSocket::SctpSocket(const std::string& name)
    : name_(name)
    , udpFallback_(name + "-udp-fallback")
{
#if defined(_WIN32)
    // Windows usrsctp backend is not yet wired to a userland I/O path
    // (register_address/conninput), so keep UDP fallback enabled there until
    // a full backend is implemented. Linux native SCTP still uses the real
    // transport path.
    useUdpFallback_ = true;
#elif RBS_HAS_USRSCTP || RBS_HAS_NATIVE_SCTP
    useUdpFallback_ = false;
#else
    useUdpFallback_ = true;
#endif
}

SctpSocket::~SctpSocket()
{
    close();
}

bool SctpSocket::bind(uint16_t localPort)
{
    if (useUdpFallback_) {
        if (!udpFallback_.bind(localPort)) return false;
        localPort_ = udpFallback_.localPort();
        RBS_LOG_WARNING("SctpSocket", "[{}] native SCTP unavailable, using UDP fallback on {}",
                        name_, localPort_);
        return true;
    }
    return bindNative(localPort);
}

bool SctpSocket::bindMulti(const std::vector<std::string>& localAddrs, uint16_t localPort)
{
    if (localAddrs.empty()) {
        RBS_LOG_ERROR("SctpSocket", "[{}] bindMulti: empty address list", name_);
        return false;
    }
    if (useUdpFallback_) {
        // UDP fallback: use only the first address
        RBS_LOG_WARNING("SctpSocket", 
                        "[{}] bindMulti: multi-homing not supported on fallback transport, "
                        "using first address ({})", name_, localAddrs[0]);
        return bind(localPort);
    }
    return bindNativeMulti(localAddrs, localPort);
}

bool SctpSocket::connect(const std::string& remoteIp, uint16_t remotePort)
{
    remoteIp_ = remoteIp;
    remotePort_ = remotePort;
    remoteAddrs_.clear();
    remoteAddrs_.push_back({remoteIp, remotePort});
    primaryRemoteIdx_ = 0;

    if (useUdpFallback_) {
        // UDP fallback is connectionless; treat configured peer as connected.
        return true;
    }
    return connectNative(remoteIp, remotePort);
}

bool SctpSocket::connectMulti(const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs, int primaryIdx)
{
    if (remoteAddrs.empty()) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectMulti: empty address list", name_);
        return false;
    }
    if (primaryIdx < 0 || primaryIdx >= static_cast<int>(remoteAddrs.size())) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectMulti: invalid primaryIdx {}", name_, primaryIdx);
        return false;
    }
    remoteAddrs_ = remoteAddrs;
    primaryRemoteIdx_ = primaryIdx;
    remoteIp_ = remoteAddrs[primaryIdx].first;
    remotePort_ = remoteAddrs[primaryIdx].second;

    if (useUdpFallback_) {
        // UDP fallback: treat primary address as connected
        RBS_LOG_WARNING("SctpSocket", 
                        "[{}] connectMulti: multi-homing not supported on fallback transport, "
                        "using primary address ({}:{})", name_, remoteIp_, remotePort_);
        return true;
    }
    return connectNativeMulti(remoteAddrs, primaryIdx);
}

void SctpSocket::setNotificationCallback(NotificationCallback cb)
{
    notificationCb_ = std::move(cb);
}

bool SctpSocket::applyTuning(const SctpTuning& tuning)
{
    if (useUdpFallback_) {
        RBS_LOG_WARNING("SctpSocket", "[{}] applyTuning: not supported on UDP fallback", name_);
        return true;  // No-op but return success
    }
    return applyTuningNative(tuning);
}

bool SctpSocket::setPrimaryPath(int primaryIdx)
{
    if (primaryIdx < 0 || primaryIdx >= static_cast<int>(remoteAddrs_.size())) {
        RBS_LOG_ERROR("SctpSocket", "[{}] setPrimaryPath: invalid index {}", name_, primaryIdx);
        return false;
    }
    primaryRemoteIdx_ = primaryIdx;
    remoteIp_ = remoteAddrs_[primaryIdx].first;
    remotePort_ = remoteAddrs_[primaryIdx].second;

    if (useUdpFallback_) {
        return true;  // No-op for fallback; just update cached primary
    }
    return setPrimaryPathNative(primaryIdx);
}


bool SctpSocket::send(const uint8_t* data, size_t len)
{
    if (useUdpFallback_) {
        return udpFallback_.send(remoteIp_, remotePort_, data, len);
    }
    return sendNative(data, len);
}

bool SctpSocket::startReceive(RxCallback cb)
{
    if (useUdpFallback_) {
        rxCb_ = std::move(cb);
        return udpFallback_.startReceive([this](const UdpPacket& pkt) {
            if (!rxCb_) return;
            SctpPacket out{pkt.srcIp, pkt.srcPort, pkt.data};
            rxCb_(out);
        });
    }
    return startReceiveNative(std::move(cb));
}

void SctpSocket::close()
{
    if (useUdpFallback_) {
        udpFallback_.close();
        return;
    }
    closeNative();
}

bool SctpSocket::isOpen() const
{
    if (useUdpFallback_) return udpFallback_.isOpen();
#if RBS_HAS_USRSCTP
    return usrSock_ != nullptr;
#else
    return sock_ != SCTP_SOCK_INVALID;
#endif
}

bool SctpSocket::bindNative(uint16_t localPort)
{
#if RBS_HAS_USRSCTP
    auto* s = static_cast<struct socket*>(
        usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, nullptr, nullptr, 0, nullptr));
    if (!s) {
        RBS_LOG_ERROR("SctpSocket", "[{}] usrsctp_socket() failed", name_);
        return false;
    }
    // Limit INIT retransmit bursts: 5 attempts, 5 s max backoff (TS 36.412)
    struct sctp_initmsg initm{};
    initm.sinit_num_ostreams   = 1;
    initm.sinit_max_instreams  = 1;
    initm.sinit_max_attempts   = 5;
    initm.sinit_max_init_timeo = 5000;  // ms
    usrsctp_setsockopt(s, IPPROTO_SCTP, SCTP_INITMSG, &initm, sizeof(initm));
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(localPort);
    if (usrsctp_bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        RBS_LOG_ERROR("SctpSocket", "[{}] usrsctp_bind(:{}) failed", name_, localPort);
        usrsctp_close(s);
        return false;
    }
    // usrsctp has no getsockname; use getladdrs to resolve the assigned port
    struct sockaddr* laddrs = nullptr;
    int naddrv = usrsctp_getladdrs(s, 0, &laddrs);
    if (naddrv > 0 && laddrs) {
        localPort_ = ntohs(
            reinterpret_cast<const sockaddr_in*>(laddrs)->sin_port);
        usrsctp_freeladdrs(laddrs);
    } else {
        localPort_ = localPort; // ephemeral — resolved after connect
    }
    usrSock_ = static_cast<void*>(s);
    return true;
#elif RBS_HAS_NATIVE_SCTP
    if (sock_ != SCTP_SOCK_INVALID) return true;

    sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (sock_ == SCTP_SOCK_INVALID) {
        RBS_LOG_ERROR("SctpSocket", "[{}] socket(AF_INET,SOCK_STREAM,IPPROTO_SCTP) failed", name_);
        return false;
    }

    int opt = 1;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(localPort);

    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SCTP_SOCK_ERR) {
        RBS_LOG_ERROR("SctpSocket", "[{}] bind(:{}) failed", name_, localPort);
        ::closesocket(sock_);
        sock_ = SCTP_SOCK_INVALID;
        return false;
    }

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(sock_, reinterpret_cast<sockaddr*>(&bound), &len);
    localPort_ = ntohs(bound.sin_port);
    return true;
#else
    (void)localPort;
    return false;
#endif
}

bool SctpSocket::connectNative(const std::string& remoteIp, uint16_t remotePort)
{
#if RBS_HAS_USRSCTP
    auto* s = static_cast<struct socket*>(usrSock_);
    if (!s) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectNative: socket not bound", name_);
        return false;
    }
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(remotePort);
    if (::inet_pton(AF_INET, remoteIp.c_str(), &dest.sin_addr) != 1) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectNative: invalid IP {}", name_, remoteIp);
        return false;
    }
    // Launch the SCTP 3-way handshake asynchronously so connect() returns
    // immediately.  sendNative() uses fire-and-forget semantics while the
    // association is being established (mirrors prior UDP fallback behaviour).
    std::string capturedName = name_;
    std::thread([s, dest, capturedName]() mutable {
        if (usrsctp_connect(s, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == 0) {
            RBS_LOG_INFO("SctpSocket", "[{}] usrsctp SCTP association established",
                         capturedName);
        } else {
            RBS_LOG_WARNING("SctpSocket",
                            "[{}] usrsctp_connect: association failed (errno={})",
                            capturedName, errno);
        }
    }).detach();
    return true;
#elif RBS_HAS_NATIVE_SCTP
    if (sock_ == SCTP_SOCK_INVALID) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectNative: socket not bound", name_);
        return false;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(remotePort);
    if (::inet_pton(AF_INET, remoteIp.c_str(), &dest.sin_addr) != 1) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectNative: invalid IP {}", name_, remoteIp);
        return false;
    }

    if (::connect(sock_, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == SCTP_SOCK_ERR) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectNative failed {}:{}", name_, remoteIp, remotePort);
        return false;
    }

    RBS_LOG_INFO("SctpSocket", "[{}] native SCTP connected to {}:{}", name_, remoteIp, remotePort);
    return true;
#else
    (void)remoteIp;
    (void)remotePort;
    return false;
#endif
}

bool SctpSocket::sendNative(const uint8_t* data, size_t len)
{
#if RBS_HAS_USRSCTP
    auto* s = static_cast<struct socket*>(usrSock_);
    if (!s) return false;
    ssize_t sent = usrsctp_sendv(s, data, len, nullptr, 0, nullptr, 0,
                                 SCTP_SENDV_NOINFO, 0);
    if (sent < 0) {
        RBS_LOG_WARNING("SctpSocket", "[{}] usrsctp_sendv failed (errno={})",
                        name_, errno);
    }
    return sent >= 0;
#elif RBS_HAS_NATIVE_SCTP
    if (sock_ == SCTP_SOCK_INVALID) return false;
    int sent = ::send(sock_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
    return sent != SCTP_SOCK_ERR;
#else
    (void)data;
    (void)len;
    return false;
#endif
}

bool SctpSocket::startReceiveNative(RxCallback cb)
{
#if RBS_HAS_USRSCTP
    auto* s = static_cast<struct socket*>(usrSock_);
    if (!s) return false;
    rxCb_    = std::move(cb);
    running_ = true;
    rxThread_ = std::thread(&SctpSocket::rxLoopNative, this);
    return true;
#elif RBS_HAS_NATIVE_SCTP
    if (sock_ == SCTP_SOCK_INVALID) return false;
    rxCb_ = std::move(cb);
    running_ = true;
    rxThread_ = std::thread(&SctpSocket::rxLoopNative, this);
    return true;
#else
    (void)cb;
    return false;
#endif
}

void SctpSocket::closeNative()
{
#if RBS_HAS_USRSCTP
    running_ = false;
    if (usrSock_) {
        usrsctp_close(static_cast<struct socket*>(usrSock_));
        usrSock_ = nullptr;
    }
    if (rxThread_.joinable()) rxThread_.join();
#elif RBS_HAS_NATIVE_SCTP
    running_ = false;
    if (sock_ != SCTP_SOCK_INVALID) {
        ::closesocket(sock_);
        sock_ = SCTP_SOCK_INVALID;
    }
    if (rxThread_.joinable()) rxThread_.join();
#endif
}

void SctpSocket::rxLoopNative()
{
#if RBS_HAS_USRSCTP
    auto* s = static_cast<struct socket*>(usrSock_);
    constexpr size_t BUF_SZ = 65536;
    std::vector<uint8_t> buf(BUF_SZ);

    while (running_) {
        unsigned int infotype = 0;
        int          flags    = 0;
        ssize_t n = usrsctp_recvv(s, buf.data(), BUF_SZ,
                                   nullptr, nullptr,
                                   nullptr, nullptr,
                                   &infotype, &flags);
        if (n < 0) {
            // ENOTCONN / EAGAIN: association is still being established —
            // wait briefly and retry (mirrors non-blocking connect semantics).
            if (errno == ENOTCONN || errno == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (running_) {
                RBS_LOG_WARNING("SctpSocket", "[{}] usrsctp_recvv failed (errno={})",
                                name_, errno);
            }
            break;
        }
        if (n == 0) break;  // EOF / association closed
        
        // Check if this is a notification or data
        if (flags & MSG_NOTIFICATION) {
            // Process SCTP notification
            ByteBuffer notifData(buf.data(), buf.data() + static_cast<size_t>(n));
            processNotification(notifData);
        } else if (!rxCb_) {
            continue;
        } else {
            // Regular data packet
            SctpPacket pkt{remoteIp_, remotePort_,
                           ByteBuffer(buf.data(), buf.data() + static_cast<size_t>(n))};
            rxCb_(pkt);
        }
    }
#elif RBS_HAS_NATIVE_SCTP
    constexpr int BUF_SZ = 65536;
    std::vector<uint8_t> buf(BUF_SZ);
    struct msghdr msg{};
    struct iovec iov;
    struct sockaddr_in peer{};
    
    while (running_) {
        iov.iov_base = buf.data();
        iov.iov_len = BUF_SZ;
        msg.msg_name = &peer;
        msg.msg_namelen = sizeof(peer);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        
        // Use recvmsg to capture MSG_NOTIFICATION flag
        int n = ::recvmsg(sock_, &msg, 0);
        if (n == SCTP_SOCK_ERR) {
            if (running_) {
                RBS_LOG_WARNING("SctpSocket", "[{}] recvmsg failed", name_);
            }
            break;
        }
        if (n <= 0) continue;

        // Check if this is a notification
        if (msg.msg_flags & MSG_NOTIFICATION) {
            // Process SCTP notification
            ByteBuffer notifData(buf.data(), buf.data() + static_cast<size_t>(n));
            processNotification(notifData);
        } else if (rxCb_) {
            // Regular data packet
            char ipStr[INET_ADDRSTRLEN] = {};
            ::inet_ntop(AF_INET, &peer.sin_addr, ipStr, sizeof(ipStr));
            
            SctpPacket pkt{ipStr, ntohs(peer.sin_port),
                          ByteBuffer(buf.data(), buf.data() + n)};
            rxCb_(pkt);
        }
    }
#endif
}

// ────────────────────────────────────────────────────────────────────────────
// Multi-homing support: bind to multiple local addresses
// ────────────────────────────────────────────────────────────────────────────

bool SctpSocket::bindNativeMulti(const std::vector<std::string>& localAddrs, uint16_t localPort)
{
#if RBS_HAS_NATIVE_SCTP && !defined(_WIN32)
    // Linux native SCTP: use sctp_bindx to bind to multiple addresses
    if (sock_ != SCTP_SOCK_INVALID) return true;

    sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (sock_ == SCTP_SOCK_INVALID) {
        RBS_LOG_ERROR("SctpSocket", "[{}] socket(AF_INET,SOCK_STREAM,IPPROTO_SCTP) failed", name_);
        return false;
    }

    int opt = 1;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    // Collect all addresses as sockaddr_in array
    std::vector<sockaddr_in> addrs;
    for (const auto& addrStr : localAddrs) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(localPort);
        if (::inet_pton(AF_INET, addrStr.c_str(), &addr.sin_addr) != 1) {
            RBS_LOG_WARNING("SctpSocket", "[{}] bindNativeMulti: invalid IP '{}', skipping", 
                           name_, addrStr);
            continue;
        }
        addrs.push_back(addr);
    }

    if (addrs.empty()) {
        RBS_LOG_ERROR("SctpSocket", "[{}] bindNativeMulti: no valid addresses", name_);
        ::closesocket(sock_);
        sock_ = SCTP_SOCK_INVALID;
        return false;
    }

    // Bind to first address with explicit bind
    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addrs[0]), sizeof(addrs[0])) == SCTP_SOCK_ERR) {
        RBS_LOG_ERROR("SctpSocket", "[{}] bindNativeMulti: bind to {} failed", 
                     name_, localAddrs[0]);
        ::closesocket(sock_);
        sock_ = SCTP_SOCK_INVALID;
        return false;
    }

    // Add remaining addresses with sctp_bindx
    if (addrs.size() > 1) {
#if RBS_HAS_NATIVE_SCTP
        int ret = sctp_bindx(sock_, reinterpret_cast<sockaddr*>(addrs.data() + 1), 
                            addrs.size() - 1, SCTP_BINDX_ADD_ADDR);
        if (ret != 0) {
            RBS_LOG_WARNING("SctpSocket", "[{}] sctp_bindx: added {} addresses (first only ok)", 
                           name_, addrs.size() - 1);
            // Don't fail; at least primary is bound
        } else {
            RBS_LOG_INFO("SctpSocket", "[{}] sctp_bindx: bound {} addresses successfully", 
                        name_, addrs.size());
        }
#endif
    }

    // Get actual bound port
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(sock_, reinterpret_cast<sockaddr*>(&bound), &len);
    localPort_ = ntohs(bound.sin_port);

    RBS_LOG_INFO("SctpSocket", "[{}] bindNativeMulti: bound {} address(es) on port {}",
                 name_, addrs.size(), localPort_);
    return true;
#elif RBS_HAS_USRSCTP
    // Windows usrsctp: doesn't support multi-homing in this iteration
    RBS_LOG_WARNING("SctpSocket", "[{}] bindNativeMulti: multi-homing not supported on usrsctp, "
                   "using first address", name_);
    return bindNative(localPort);
#else
    (void)localAddrs;
    (void)localPort;
    return false;
#endif
}

// ────────────────────────────────────────────────────────────────────────────
// Multi-homing support: connect to multiple remote addresses with failover
// ────────────────────────────────────────────────────────────────────────────

bool SctpSocket::connectNativeMulti(const std::vector<std::pair<std::string, uint16_t>>& remoteAddrs, int primaryIdx)
{
#if RBS_HAS_NATIVE_SCTP && !defined(_WIN32)
    // Linux native SCTP: use sctp_connectx to connect with multi-homing
    if (sock_ == SCTP_SOCK_INVALID) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectNativeMulti: socket not bound", name_);
        return false;
    }

    std::vector<sockaddr_in> addrs;
    for (const auto& [ip, port] : remoteAddrs) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            RBS_LOG_WARNING("SctpSocket", "[{}] connectNativeMulti: invalid IP '{}', skipping", 
                           name_, ip);
            continue;
        }
        addrs.push_back(addr);
    }

    if (addrs.empty()) {
        RBS_LOG_ERROR("SctpSocket", "[{}] connectNativeMulti: no valid addresses", name_);
        return false;
    }

    // Use sctp_connectx to establish association with multiple addresses
    sctp_assoc_t assoc_id = SCTP_FUTURE_ASSOC;
#if RBS_HAS_NATIVE_SCTP
    int ret = sctp_connectx(sock_, reinterpret_cast<sockaddr*>(addrs.data()), addrs.size(), &assoc_id);
    if (ret != 0) {
        RBS_LOG_ERROR("SctpSocket", "[{}] sctp_connectx failed (ret={})", name_, ret);
        return false;
    }
#else
    return false;
#endif

    RBS_LOG_INFO("SctpSocket", "[{}] connectNativeMulti: connected with {} addresses, primary=[{}:{}]",
                 name_, addrs.size(), remoteAddrs[primaryIdx].first, remoteAddrs[primaryIdx].second);
    
    // Now set primary address to the one at primaryIdx
    return setPrimaryPathNative(primaryIdx);
#elif RBS_HAS_USRSCTP
    // Windows usrsctp: fall back to single-address connect
    if (remoteAddrs.empty()) return false;
    RBS_LOG_WARNING("SctpSocket", "[{}] connectNativeMulti: multi-homing not supported on usrsctp, "
                   "using primary address", name_);
    return connectNative(remoteAddrs[primaryIdx].first, remoteAddrs[primaryIdx].second);
#else
    (void)remoteAddrs;
    (void)primaryIdx;
    return false;
#endif
}

// ────────────────────────────────────────────────────────────────────────────
// Multi-homing support: set primary path
// ────────────────────────────────────────────────────────────────────────────

bool SctpSocket::setPrimaryPathNative(int primaryIdx)
{
#if RBS_HAS_NATIVE_SCTP && !defined(_WIN32)
    // Linux native SCTP: use SCTP_PRIMARY_ADDR sockopt to select primary
    if (sock_ == SCTP_SOCK_INVALID) {
        RBS_LOG_ERROR("SctpSocket", "[{}] setPrimaryPathNative: socket not open", name_);
        return false;
    }

    if (primaryIdx < 0 || primaryIdx >= static_cast<int>(remoteAddrs_.size())) {
        RBS_LOG_ERROR("SctpSocket", "[{}] setPrimaryPathNative: invalid index {}", name_, primaryIdx);
        return false;
    }

    const auto& [ip, port] = remoteAddrs_[primaryIdx];
    sockaddr_in primary{};
    primary.sin_family = AF_INET;
    primary.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &primary.sin_addr) != 1) {
        RBS_LOG_ERROR("SctpSocket", "[{}] setPrimaryPathNative: invalid IP '{}'", name_, ip);
        return false;
    }

    sctp_assoc_t assoc_id = SCTP_FUTURE_ASSOC;
    if (::setsockopt(sock_, IPPROTO_SCTP, SCTP_PRIMARY_ADDR, 
                     reinterpret_cast<const char*>(&primary), sizeof(primary)) != 0) {
        RBS_LOG_WARNING("SctpSocket", "[{}] SCTP_PRIMARY_ADDR failed (errno={})", name_, errno);
        return false;
    }

    RBS_LOG_INFO("SctpSocket", "[{}] setPrimaryPathNative: primary path set to {}:{}",
                 name_, ip, port);
    return true;
#else
    (void)primaryIdx;
    return false;
#endif
}

// ────────────────────────────────────────────────────────────────────────────
// Performance tuning support
// ────────────────────────────────────────────────────────────────────────────

bool SctpSocket::applyTuningNative(const SctpTuning& tuning)
{
#if RBS_HAS_NATIVE_SCTP
    if (sock_ == SCTP_SOCK_INVALID) {
        RBS_LOG_ERROR("SctpSocket", "[{}] applyTuningNative: socket not open", name_);
        return false;
    }

    // Apply heartbeat interval
    if (tuning.heartbeatInterval) {
        struct sctp_paddrparams params{};
        params.spp_hbinterval = tuning.heartbeatInterval.value();
        params.spp_flags = SPP_HB_ENABLE;
        if (::setsockopt(sock_, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS,
                        reinterpret_cast<const char*>(&params), sizeof(params)) != 0) {
            RBS_LOG_WARNING("SctpSocket", "[{}] SCTP_PEER_ADDR_PARAMS (heartbeat) failed", name_);
        }
    }

    // Apply RTO bounds
    if (tuning.rtoInitial || tuning.rtoMin || tuning.rtoMax) {
        struct sctp_rtoinfo rtoinfo{};
        if (tuning.rtoInitial) rtoinfo.srto_initial = tuning.rtoInitial.value();
        if (tuning.rtoMin) rtoinfo.srto_min = tuning.rtoMin.value();
        if (tuning.rtoMax) rtoinfo.srto_max = tuning.rtoMax.value();
        if (::setsockopt(sock_, IPPROTO_SCTP, SCTP_RTOINFO,
                        reinterpret_cast<const char*>(&rtoinfo), sizeof(rtoinfo)) != 0) {
            RBS_LOG_WARNING("SctpSocket", "[{}] SCTP_RTOINFO failed", name_);
        }
    }

    // Apply init retransmit params
    if (tuning.initNumAttempts || tuning.initMaxTimeout) {
        struct sctp_initmsg initm{};
        if (tuning.initNumAttempts) initm.sinit_max_attempts = tuning.initNumAttempts.value();
        if (tuning.initMaxTimeout) initm.sinit_max_init_timeo = tuning.initMaxTimeout.value();
        if (::setsockopt(sock_, IPPROTO_SCTP, SCTP_INITMSG,
                        reinterpret_cast<const char*>(&initm), sizeof(initm)) != 0) {
            RBS_LOG_WARNING("SctpSocket", "[{}] SCTP_INITMSG failed", name_);
        }
    }

    // Apply socket buffer sizes
    if (tuning.rcvBufSize) {
        if (::setsockopt(sock_, SOL_SOCKET, SO_RCVBUF,
                        reinterpret_cast<const char*>(&tuning.rcvBufSize.value()),
                        sizeof(uint32_t)) != 0) {
            RBS_LOG_WARNING("SctpSocket", "[{}] SO_RCVBUF failed", name_);
        }
    }
    if (tuning.sndBufSize) {
        if (::setsockopt(sock_, SOL_SOCKET, SO_SNDBUF,
                        reinterpret_cast<const char*>(&tuning.sndBufSize.value()),
                        sizeof(uint32_t)) != 0) {
            RBS_LOG_WARNING("SctpSocket", "[{}] SO_SNDBUF failed", name_);
        }
    }

    RBS_LOG_INFO("SctpSocket", "[{}] applyTuningNative: tuning applied successfully", name_);
    return true;
#elif RBS_HAS_USRSCTP
    // usrsctp: apply basic tuning
    auto* s = static_cast<struct socket*>(usrSock_);
    if (!s) return false;

    if (tuning.heartbeatInterval) {
        struct sctp_paddrparams params{};
        params.spp_hbinterval = tuning.heartbeatInterval.value();
        params.spp_flags = SPP_HB_ENABLE;
        usrsctp_setsockopt(s, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &params, sizeof(params));
    }

    RBS_LOG_INFO("SctpSocket", "[{}] applyTuningNative (usrsctp): basic tuning applied", name_);
    return true;
#else
    (void)tuning;
    return false;
#endif
}

// ────────────────────────────────────────────────────────────────────────────
// SCTP Notification Handling
// ────────────────────────────────────────────────────────────────────────────

void SctpSocket::processNotification(const ByteBuffer& notifData)
{
    if (notifData.empty() || notifData.size() < 4) {
        return;  // Too short to be valid notification
    }

#if RBS_HAS_NATIVE_SCTP || RBS_HAS_USRSCTP
    SctpNotification notif;
    notif.rawData = notifData;

    // Get notification type from first 2 bytes (sn_type, sn_flags)
    uint16_t notifType = *reinterpret_cast<const uint16_t*>(notifData.data());
    notifType &= 0xFF;  // Extract sn_type from low byte

    switch (notifType) {
        case SCTP_ASSOC_CHANGE: {
            notif.type = SctpNotificationType::ASSOC_CHANGE;
            if (notifData.size() >= sizeof(struct sctp_assoc_change)) {
                auto* ac = reinterpret_cast<const struct sctp_assoc_change*>(notifData.data());
                notif.associationId = ac->sac_assoc_id;
                const char* stateName = "UNKNOWN";
                switch (ac->sac_state) {
                    case SCTP_COMM_UP: stateName = "COMM_UP"; break;
                    case SCTP_COMM_LOST: stateName = "COMM_LOST"; break;
                    case SCTP_RESTART: stateName = "RESTART"; break;
                    case SCTP_SHUTDOWN_COMP: stateName = "SHUTDOWN_COMP"; break;
                    case SCTP_CANT_STR_ASSOC: stateName = "CANT_STR_ASSOC"; break;
                }
                notif.description = "ASSOC_CHANGE: " + std::string(stateName);
                RBS_LOG_INFO("SctpSocket", "[{}] SCTP_ASSOC_CHANGE: {}, assoc_id={}", 
                            name_, stateName, ac->sac_assoc_id);
            }
            break;
        }
        case SCTP_PEER_ADDR_CHANGE: {
            notif.type = SctpNotificationType::PEER_ADDR_CHANGE;
            if (notifData.size() >= sizeof(struct sctp_paddr_change)) {
                auto* pc = reinterpret_cast<const struct sctp_paddr_change*>(notifData.data());
                notif.associationId = pc->spc_assoc_id;
                const char* stateName = "UNKNOWN";
                switch (pc->spc_state) {
                    case SCTP_ADDR_AVAILABLE: stateName = "AVAILABLE"; break;
                    case SCTP_ADDR_UNREACHABLE: stateName = "UNREACHABLE"; break;
                    case SCTP_ADDR_REMOVED: stateName = "REMOVED"; break;
                    case SCTP_ADDR_ADDED: stateName = "ADDED"; break;
                    case SCTP_ADDR_MADE_PRIM: stateName = "MADE_PRIM"; break;
                    case SCTP_ADDR_CONFIRMED: stateName = "CONFIRMED"; break;
                }
                notif.description = "PEER_ADDR_CHANGE: " + std::string(stateName);
                RBS_LOG_INFO("SctpSocket", "[{}] SCTP_PEER_ADDR_CHANGE: {} (multi-homing event)",
                            name_, stateName);
            }
            break;
        }
        case SCTP_SEND_FAILED: {
            notif.type = SctpNotificationType::SEND_FAILED;
            notif.description = "SEND_FAILED: data could not be transmitted";
            RBS_LOG_WARNING("SctpSocket", "[{}] SCTP_SEND_FAILED: transmission error", name_);
            break;
        }
        case SCTP_SHUTDOWN_EVENT: {
            notif.type = SctpNotificationType::SHUTDOWN_EVENT;
            notif.description = "SHUTDOWN_EVENT: association was shut down";
            RBS_LOG_WARNING("SctpSocket", "[{}] SCTP_SHUTDOWN_EVENT", name_);
            break;
        }
        default:
            notif.type = SctpNotificationType::UNKNOWN;
            notif.description = "Unknown notification type";
            RBS_LOG_DEBUG("SctpSocket", "[{}] Unknown SCTP notification type {}", name_, notifType);
    }

    // Invoke application callback if registered
    if (notificationCb_) {
        notificationCb_(notif);
    }
#endif
}

} // namespace rbs::net

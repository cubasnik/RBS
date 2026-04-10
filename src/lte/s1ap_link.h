#pragma once
#include "s1ap_interface.h"
#include "gtp_u.h"
#include "../common/udp_socket.h"
#include "../common/logger.h"
#include <queue>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// S1APLink — симуляционная реализация S1AP (TS 36.413).
//
// Моделирует управляющий канал eNB ↔ MME:
//   • S1 Setup при старте
//   • NAS transport (Initial UE Message, DL/UL NAS)
//   • Initial Context Setup / UE Context Release
//   • Path Switch после X2-хэндовера
//
// Транспорт: SCTP over IP (TS 36.412).
// В симуляции — in-memory очередь; S1AP PDU не ASN.1-кодируются.
// ─────────────────────────────────────────────────────────────────────────────
class S1APLink : public IS1AP {
public:
    explicit S1APLink(const std::string& enbId);

    // ── Управление соединением ────────────────────────────────────────────────
    bool connect   (const std::string& mmeAddr,
                    uint16_t port = 36412)                      override;
    void disconnect()                                            override;
    bool isConnected() const                                     override { return connected_; }

    // ── S1 Setup ──────────────────────────────────────────────────────────────
    bool s1Setup(uint32_t enbId, const std::string& enbName,
                 uint32_t tac, uint32_t plmnId)                  override;

    // ── NAS transport ─────────────────────────────────────────────────────────
    bool initialUEMessage  (RNTI rnti, IMSI imsi,
                            const ByteBuffer& nasPdu)            override;
    bool downlinkNASTransport(uint32_t mmeUeS1apId, RNTI rnti,
                              const ByteBuffer& nasPdu)          override;
    bool uplinkNASTransport  (uint32_t mmeUeS1apId, RNTI rnti,
                              const ByteBuffer& nasPdu)          override;

    // ── Управление контекстом ─────────────────────────────────────────────────
    bool initialContextSetupResponse(uint32_t mmeUeS1apId, RNTI rnti,
                                     const std::vector<ERAB>& erabs) override;
    bool ueContextReleaseRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                 const std::string& cause)       override;
    bool ueContextReleaseComplete(uint32_t mmeUeS1apId,
                                  RNTI rnti)                     override;

    // ── E-RAB management ─────────────────────────────────────────────────────
    bool erabSetupResponse (uint32_t mmeUeS1apId, RNTI rnti,
                            const std::vector<ERAB>& erabs,
                            const std::vector<uint8_t>& failedErabIds) override;
    bool erabReleaseResponse(uint32_t mmeUeS1apId, RNTI rnti,
                             const std::vector<uint8_t>& releasedErabIds) override;

    // ── Хэндовер ──────────────────────────────────────────────────────────────
    bool pathSwitchRequest(uint32_t mmeUeS1apId, RNTI rnti,
                           uint32_t targetEnbId,
                           const std::vector<ERAB>& erabs)       override;
    bool handoverRequired  (uint32_t mmeUeS1apId, RNTI rnti,
                            uint32_t targetEnbId,
                            const ByteBuffer& rrcContainer)      override;
    bool handoverNotify    (uint32_t mmeUeS1apId, RNTI rnti)     override;

    // ── Сырой обмен ───────────────────────────────────────────────────────────
    bool sendS1APMsg(const S1APMessage& msg)                     override;
    bool recvS1APMsg(S1APMessage& msg)                           override;

private:
    std::string enbId_;
    std::string mmeAddr_;
    uint16_t    mmePort_   = 0;
    bool        connected_ = false;
    uint32_t    enbIdNum_  = 0;  ///< Numeric eNB ID (20-bit macro)
    uint32_t    plmnId_    = 0;  ///< Cached PLMN identity from s1Setup
    uint16_t    tac_       = 0;  ///< Tracking Area Code from s1Setup
    uint32_t    cellId_    = 1;  ///< E-UTRAN cell identity (default 1)

    // RNTI → mmeUeS1apId
    std::unordered_map<RNTI, uint32_t> ueS1apIds_;
    uint32_t nextMmeId_ = 0x1000;

    std::queue<S1APMessage> rxQueue_;
    mutable std::mutex      rxMtx_;

    net::UdpSocket socket_;
    bool           socketReady_ = false;

    uint32_t allocateMmeId(RNTI rnti);
    void     onRxPacket(const net::UdpPacket& pkt);
};

// ─────────────────────────────────────────────────────────────────────────────
// S1ULink — реальная GTP-U реализация поверх UDP/Winsock2 (TS 29.060 / 29.274).
//
// eNB слушает UDP-порт GTPU_PORT (2152) для входящих DL-пакетов от SGW.
// UL-пакеты отправляются на sgwEndpoint.remoteIp:GTPU_PORT.
//
// Поток приёма: фоновый (rxThread_ в UdpSocket).
// DL-пакеты декодируются по TEID и кладутся в dlQueues_[key].
// ─────────────────────────────────────────────────────────────────────────────
class S1ULink : public IS1U {
public:
    explicit S1ULink(const std::string& enbId, uint16_t localPort = GTPU_PORT);

    bool createTunnel(RNTI rnti, uint8_t erabId,
                      const GTPUTunnel& sgwEndpoint)             override;
    bool deleteTunnel(RNTI rnti, uint8_t erabId)                 override;
    bool sendGtpuPdu (RNTI rnti, uint8_t erabId,
                      const ByteBuffer& ipPacket)                override;
    bool recvGtpuPdu (RNTI rnti, uint8_t erabId,
                      ByteBuffer& ipPacket)                      override;

private:
    std::string    enbId_;
    uint16_t       localPort_;
    net::UdpSocket socket_;
    bool           socketReady_ = false;

    // Ключ туннеля: (rnti << 8) | erabId
    static uint32_t tunnelKey(RNTI rnti, uint8_t erabId) {
        return (static_cast<uint32_t>(rnti) << 8) | erabId;
    }

    std::unordered_map<uint32_t, GTPUTunnel>              tunnels_;
    // teid → tunnelKey (для маршрутизации входящих DL-пакетов)
    std::unordered_map<uint32_t, uint32_t>                teidToKey_;
    std::unordered_map<uint32_t, std::queue<ByteBuffer>>  dlQueues_;
    mutable std::mutex                                    mtx_;

    void onRxPacket(const net::UdpPacket& pkt);
};

} // namespace rbs::lte

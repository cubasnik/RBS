#pragma once
#include "s1ap_interface.h"
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

    // ── Хэндовер ──────────────────────────────────────────────────────────────
    bool pathSwitchRequest(uint32_t mmeUeS1apId, RNTI rnti,
                           uint32_t targetEnbId,
                           const std::vector<ERAB>& erabs)       override;

    // ── Сырой обмен ───────────────────────────────────────────────────────────
    bool sendS1APMsg(const S1APMessage& msg)                     override;
    bool recvS1APMsg(S1APMessage& msg)                           override;

private:
    std::string enbId_;
    std::string mmeAddr_;
    uint16_t    mmePort_   = 0;
    bool        connected_ = false;

    // RNTI → mmeUeS1apId
    std::unordered_map<RNTI, uint32_t> ueS1apIds_;
    uint32_t nextMmeId_ = 0x1000;

    std::queue<S1APMessage> rxQueue_;
    mutable std::mutex      rxMtx_;

    uint32_t allocateMmeId(RNTI rnti);
};

// ─────────────────────────────────────────────────────────────────────────────
// S1ULink — симуляционная реализация S1-U GTP-U туннелей (TS 29.274).
//
// В реальной системе: UDP/IP дейтаграммы на порт 2152.
// В симуляции: per-(RNTI,erabId) очереди IP-пакетов.
// ─────────────────────────────────────────────────────────────────────────────
class S1ULink : public IS1U {
public:
    explicit S1ULink(const std::string& enbId);

    bool createTunnel(RNTI rnti, uint8_t erabId,
                      const GTPUTunnel& sgwEndpoint)             override;
    bool deleteTunnel(RNTI rnti, uint8_t erabId)                 override;
    bool sendGtpuPdu (RNTI rnti, uint8_t erabId,
                      const ByteBuffer& ipPacket)                override;
    bool recvGtpuPdu (RNTI rnti, uint8_t erabId,
                      ByteBuffer& ipPacket)                      override;

private:
    std::string enbId_;

    // Ключ туннеля: (rnti << 8) | erabId
    static uint32_t tunnelKey(RNTI rnti, uint8_t erabId) {
        return (static_cast<uint32_t>(rnti) << 8) | erabId;
    }

    std::unordered_map<uint32_t, GTPUTunnel>              tunnels_;
    std::unordered_map<uint32_t, std::queue<ByteBuffer>>  dlQueues_;
    mutable std::mutex                                    mtx_;
};

} // namespace rbs::lte

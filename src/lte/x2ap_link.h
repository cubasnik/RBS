#pragma once
#include "x2ap_interface.h"
#include "../common/logger.h"
#include <queue>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// X2APLink — симуляционная реализация X2AP (TS 36.423).
//
// Управляет X2-хэндовером между eNB:
//   • X2 Setup при установке соседства
//   • Handover Request / Ack / Failure / Cancel
//   • SN Status Transfer (PDCP SN для безпотерьной доставки)
//   • UE Context Release после хэндовера
//   • Load Indication для ICIC
//
// Транспорт: SCTP over IP (TS 36.422).  В симуляции — in-memory очереди.
// ─────────────────────────────────────────────────────────────────────────────
class X2APLink : public IX2AP {
public:
    explicit X2APLink(const std::string& enbId);

    // ── Управление соединением ────────────────────────────────────────────────
    bool connect   (uint32_t targetEnbId, const std::string& addr,
                    uint16_t port = 36422)                       override;
    void disconnect(uint32_t targetEnbId)                        override;
    bool isConnected(uint32_t targetEnbId) const                 override;

    // ── X2 Setup ──────────────────────────────────────────────────────────────
    bool x2Setup(uint32_t localEnbId, uint32_t targetEnbId)      override;

    // ── Подготовка хэндовера ──────────────────────────────────────────────────
    bool handoverRequest          (const X2HORequest& req)       override;
    bool handoverRequestAck       (const X2HORequestAck& ack)    override;
    bool handoverPreparationFailure(RNTI rnti,
                                    const std::string& cause)    override;
    bool handoverCancel           (RNTI rnti,
                                   const std::string& cause)     override;

    // ── После хэндовера ───────────────────────────────────────────────────────
    bool snStatusTransfer(RNTI rnti,
                          const std::vector<SNStatusItem>& items) override;
    bool ueContextRelease(RNTI rnti)                              override;

    // ── ICIC ──────────────────────────────────────────────────────────────────
    bool loadIndication(uint32_t targetEnbId,
                        uint8_t dlPrbOccupancy,
                        uint8_t ulPrbOccupancy)                  override;

    // ── Сырой обмен ───────────────────────────────────────────────────────────
    bool sendX2APMsg(const X2APMessage& msg)                     override;
    bool recvX2APMsg(X2APMessage& msg)                           override;

private:
    std::string enbId_;

    // enb → соединение: addr, port, connected
    struct Peer { std::string addr; uint16_t port; bool connected; };
    std::unordered_map<uint32_t, Peer> peers_;

    // Счётчики X2AP ID
    uint32_t nextSrcX2Id_ = 0x100;

    // RNTI → sourceEnbUeX2apId
    std::unordered_map<RNTI, uint32_t> hoIds_;

    std::queue<X2APMessage> rxQueue_;
    mutable std::mutex      rxMtx_;
};

// ─────────────────────────────────────────────────────────────────────────────
// X2ULink — симуляционная реализация X2-U GTP-U (TS 36.425).
//
// Пересылка DL PDCP PDU от source eNB к target eNB во время хэндовера.
// В симуляции — in-memory очереди пакетов.
// ─────────────────────────────────────────────────────────────────────────────
class X2ULink : public IX2U {
public:
    explicit X2ULink(const std::string& enbId);

    bool openForwardingTunnel(RNTI rnti, const std::string& targetAddr,
                              uint32_t teid)                     override;
    bool forwardPacket       (RNTI rnti,
                              const ByteBuffer& pdcpPdu)         override;
    void closeForwardingTunnel(RNTI rnti)                        override;

private:
    std::string enbId_;

    struct ForwardTunnel { std::string targetAddr; uint32_t teid; };
    std::unordered_map<RNTI, ForwardTunnel> tunnels_;

    // Буфер перенаправленных пакетов (читается target eNB)
    std::unordered_map<RNTI, std::queue<ByteBuffer>> fwdBuffers_;
    mutable std::mutex                               mtx_;
};

} // namespace rbs::lte

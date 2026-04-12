#pragma once
#include "abis_interface.h"
#include "ipa.h"
#include "../common/link_controller.h"
#include "../common/tcp_socket.h"
#include "../common/logger.h"
#include <queue>
#include <mutex>
#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <thread>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// AbisOml — Абис-поверх-IP реализация OML (TS 12.21).
//
// ТЕКУЩИЙ РЕЖИМ: In-memory симуляция (готовность для TCP/IPA).
// Поддерживает оба режима (структурно готово):
//   1. Реальный транспорт: TCP + IPA (IP Protocol for Abis, TS 12.21 §4.2) — через конфиг
//   2. Симуляция: in-memory очередь сообщений — текущее поведение
//
// Для включения TCP/IPA: установить [gsm].abis_transport=ipa_tcp в rbs.conf.
// ─────────────────────────────────────────────────────────────────────────────
class AbisOml : public IAbisOml, public rbs::LinkController {
public:
    explicit AbisOml(const std::string& btsId);
    ~AbisOml();

    // Transport mode control: false=simulation, true=TCP/IPA.
    void setUseRealTransport(bool enable) { useRealTransport_ = enable; }
    bool useRealTransport() const { return useRealTransport_; }
    void setHealthTiming(uint32_t heartbeatIntervalMs, uint32_t staleRxMs);
    void setKeepaliveConfig(bool enabled, uint32_t idleMs);
    void setInteropProfile(const std::string& profileName);
    std::string healthJson() const;

    // ── Управление соединением ────────────────────────────────────────────────
    bool connect   (const std::string& bscAddr, uint16_t port) override;
    void disconnect()                                           override;
    bool isConnected() const                                    override { return connected_; }

    // ── Обмен OML-сообщениями ─────────────────────────────────────────────────
    bool sendOmlMsg(OMLMsgType type, const AbisMessage& msg) override;
    bool recvOmlMsg(OMLMsgType& type, AbisMessage& msg)      override;

    // ── Управление BTS ────────────────────────────────────────────────────────
    bool configureTRX(uint8_t trxId, uint16_t arfcn,
                      int8_t txPower_dBm)                    override;
    void reportHwFailure(uint8_t objectClass,
                         const std::string& cause)           override;

    // ── LinkController hooks ──────────────────────────────────────────────────
    /// Повторно подключиться к BSC (если bscAddr известен).
    void reconnect();
    std::vector<std::string> injectableProcs() const;
    bool injectProcedure(const std::string& proc);

private:
    std::string btsId_;
    std::string bscAddr_;
    uint16_t    bscPort_   = 0;
    bool        connected_ = false;
    
    // Transport modes
    bool useRealTransport_ = false;  // true: TCP/IPA, false: in-memory simulation
    std::unique_ptr<rbs::net::TcpSocket> tcpSocket_;
    ipa::FrameParser ipaParser_;

    // Входящая очередь — для симуляции и буферизации
    struct RxItem { OMLMsgType type; AbisMessage msg; };
    std::queue<RxItem> rxQueue_;
    mutable std::mutex rxMtx_;

    struct RslRxItem { RSLMsgType type; AbisMessage msg; };
    std::queue<RslRxItem> rslRxQueue_;
    mutable std::mutex rslRxMtx_;

    // Конфигурация TRX
    struct TRXConfig { uint16_t arfcn; int8_t txPower_dBm; };
    std::unordered_map<uint8_t, TRXConfig> trxMap_;
    
    // TCP receive callback
    void onTcpRxPacket(const rbs::net::TcpPacket& pkt);
    void healthMonitorLoop();
    void startHealthMonitor();
    void stopHealthMonitor();
    bool sendKeepaliveProbe();
    bool sendRslMsgInternal(RSLMsgType type, const AbisMessage& msg);

    // Option B: reconnect/backoff state
    std::atomic<uint32_t> reconnectAttempts_{0};
    std::atomic<long long> lastRxEpochMs_{0};
    std::atomic<long long> lastConnectEpochMs_{0};
    std::atomic<long long> lastConnectAttemptEpochMs_{0};
    std::atomic<long long> nextReconnectEpochMs_{0};

    // Option B+: active heartbeat/health monitor
    std::atomic<uint32_t> heartbeatIntervalMs_{1000};
    std::atomic<uint32_t> staleRxMs_{10000};
    std::atomic<uint8_t> healthStatus_{0}; // 0=DOWN, 1=DEGRADED, 2=UP
    std::atomic<bool> keepaliveEnabled_{true};
    std::atomic<uint32_t> keepaliveIdleMs_{3000};
    std::atomic<uint32_t> keepaliveTxCount_{0};
    std::atomic<uint32_t> keepaliveFailCount_{0};
    std::atomic<long long> lastKeepaliveTxEpochMs_{0};
    std::string interopProfile_{"default"};
    std::atomic<uint64_t> omlTxFrames_{0};
    std::atomic<uint64_t> omlRxFrames_{0};
    std::atomic<uint64_t> rslTxFrames_{0};
    std::atomic<uint64_t> rslRxFrames_{0};
    std::atomic<bool> monitorRunning_{false};
    std::thread monitorThread_;
};

// ─────────────────────────────────────────────────────────────────────────────
// AbisRsl — симуляционная реализация RSL (TS 08.58).
// ─────────────────────────────────────────────────────────────────────────────
class AbisRsl : public IAbisRsl {
public:
    explicit AbisRsl(const std::string& btsId);

    // ── Обмен RSL-сообщениями ─────────────────────────────────────────────────
    bool sendRslMsg(RSLMsgType type, const AbisMessage& msg) override;
    bool recvRslMsg(RSLMsgType& type, AbisMessage& msg)      override;

    // ── Управление каналами ───────────────────────────────────────────────────
    bool activateChannel(uint8_t chanNr, GSMChannelType type,
                         RNTI rnti, uint8_t timingAdvance)   override;
    bool releaseChannel(uint8_t chanNr)                      override;

    // ── Шифрование ────────────────────────────────────────────────────────────
    bool sendCipherModeCommand(uint8_t chanNr, uint8_t algorithm,
                               const ByteBuffer& key)        override;

    // ── Измерения ─────────────────────────────────────────────────────────────
    void sendMeasurementResult(RNTI rnti,
                               int8_t rxlev, uint8_t rxqual) override;

    // ── Хэндовер ─────────────────────────────────────────────────────────────
    bool forwardHandoverCommand(RNTI rnti,
                                const ByteBuffer& hoCmd)     override;

private:
    std::string btsId_;

    // Активные каналы: chanNr → RNTI
    std::unordered_map<uint8_t, RNTI> activeChannels_;

    // Входящая очередь RSL-сообщений «от BSC»
    struct RxItem { RSLMsgType type; AbisMessage msg; };
    std::queue<RxItem> rxQueue_;
    mutable std::mutex rxMtx_;
};

} // namespace rbs::gsm

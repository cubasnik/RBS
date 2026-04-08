#pragma once
#include "abis_interface.h"
#include "../common/logger.h"
#include <queue>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rbs::gsm {

// ─────────────────────────────────────────────────────────────────────────────
// AbisOml — симуляционная реализация OML (TS 12.21).
//
// В реальной системе использовался бы IPA или TDM-транспорт.
// Здесь сообщения помещаются во внутреннюю очередь и
// логируются — достаточно для симуляторного BTS.
// ─────────────────────────────────────────────────────────────────────────────
class AbisOml : public IAbisOml {
public:
    explicit AbisOml(const std::string& btsId);

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

private:
    std::string btsId_;
    std::string bscAddr_;
    uint16_t    bscPort_   = 0;
    bool        connected_ = false;

    // Входящая очередь — сообщения «от BSC» (в симуляции вставляются тестами)
    struct RxItem { OMLMsgType type; AbisMessage msg; };
    std::queue<RxItem> rxQueue_;
    mutable std::mutex rxMtx_;

    // Конфигурация TRX
    struct TRXConfig { uint16_t arfcn; int8_t txPower_dBm; };
    std::unordered_map<uint8_t, TRXConfig> trxMap_;
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

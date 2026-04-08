#pragma once
#include "iub_interface.h"
#include "../common/logger.h"
#include <queue>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rbs::umts {

// ─────────────────────────────────────────────────────────────────────────────
// IubNbap — симуляционная реализация NBAP (TS 25.433).
//
// Управляет радиолинками UE: setup, addition, deletion;
// инициирует общие и выделенные измерения.
// Транспорт: SCTP over IP (в симуляции — in-memory очередь).
// ─────────────────────────────────────────────────────────────────────────────
class IubNbap : public IIubNbap {
public:
    explicit IubNbap(const std::string& nodeBId);

    // ── Управление соединением ────────────────────────────────────────────────
    bool connect   (const std::string& rncAddr, uint16_t port) override;
    void disconnect()                                           override;
    bool isConnected() const                                    override { return connected_; }

    // ── Common NBAP ───────────────────────────────────────────────────────────
    bool sendCellSetup(uint16_t cellId,
                       uint16_t primaryScrCode,
                       uint16_t uarfcnDl,
                       uint16_t uarfcnUl)                      override;

    bool commonMeasurementInitiation(uint16_t measId,
                                     const std::string& measObject) override;
    bool commonMeasurementTermination(uint16_t measId)          override;

    // ── Dedicated NBAP ────────────────────────────────────────────────────────
    bool radioLinkSetup  (RNTI rnti, uint16_t scrCode, SF sf)  override;
    bool radioLinkDeletion(RNTI rnti)                          override;
    bool dedicatedMeasurementInitiation(RNTI rnti,
                                        uint16_t measId)       override;

    // ── Сырой обмен сообщениями ───────────────────────────────────────────────
    bool sendNbapMsg(const NBAPMessage& msg)                    override;
    bool recvNbapMsg(NBAPMessage& msg)                          override;

private:
    std::string nodeBId_;
    std::string rncAddr_;
    uint16_t    rncPort_   = 0;
    bool        connected_ = false;
    uint16_t    nextTxId_  = 1;

    // Описание радиолинка
    struct RadioLink { uint16_t scrCode; SF sf; };
    std::unordered_map<RNTI, RadioLink> links_;

    // Активные измерения
    std::unordered_map<uint16_t /*measId*/, std::string> commonMeas_;

    // Входящая очередь «от RNC»
    std::queue<NBAPMessage> rxQueue_;
    mutable std::mutex      rxMtx_;

    uint16_t nextTxId() { return nextTxId_++; }
};

// ─────────────────────────────────────────────────────────────────────────────
// IubFp — симуляционная реализация Frame Protocol (TS 25.435).
//
// DCH пользовательский трафик между NodeB и RNC.
// В симуляции данные хранятся в per-RNTI очередях.
// ─────────────────────────────────────────────────────────────────────────────
class IubFp : public IIubFp {
public:
    explicit IubFp(const std::string& nodeBId);

    bool sendDchData(RNTI rnti, uint16_t scrCode,
                     const ByteBuffer& tbs)               override;
    bool receiveDchData(RNTI rnti, uint16_t scrCode,
                        ByteBuffer& tbs)                  override;
    void reportSyncStatus(RNTI rnti, bool inSync)         override;

private:
    std::string nodeBId_;

    // DL-очередь «от RNC к NodeB» (индексируется RNTI)
    std::unordered_map<RNTI, std::queue<ByteBuffer>> dlQueues_;
    mutable std::mutex dlMtx_;
};

} // namespace rbs::umts

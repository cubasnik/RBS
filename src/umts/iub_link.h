#pragma once
#include "iub_interface.h"
#include "../common/link_controller.h"
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
class IubNbap : public IIubNbap, public rbs::LinkController {
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
    bool radioLinkAddition(RNTI rnti, uint16_t scrCode, SF sf)  override;
    bool radioBearerSetup(RNTI rnti,
                          uint8_t rbId,
                          uint8_t rlcMode,
                          uint16_t maxBitrateKbps,
                          bool uplinkEnabled,
                          bool downlinkEnabled)                 override;
    bool radioBearerRelease(RNTI rnti, uint8_t rbId)            override;
    bool radioLinkDeletion(RNTI rnti)                          override;
    bool radioLinkDeletionSHO(RNTI rnti, uint16_t scrCode)     override;
    bool dedicatedMeasurementInitiation(RNTI rnti,
                                        uint16_t measId)       override;

    // ── DCH / HSDPA extensions ────────────────────────────────────────────────
    bool commonTransportChannelSetup(uint16_t cellId,
                                      NBAPCommonChannel channelType) override;
    bool radioLinkReconfigurePrepare(RNTI rnti, SF newSf)            override;
    bool radioLinkReconfigureCommit (RNTI rnti)                      override;
    bool radioLinkSetupHSDPA        (RNTI rnti, uint16_t scrCode,
                                      uint8_t hsDschCodes = 5)       override;
    bool radioLinkSetupEDCH         (RNTI rnti, uint16_t scrCode,
                                      EDCHTTI tti = EDCHTTI::TTI_10MS) override;

    // ── Сырой обмен сообщениями ───────────────────────────────────────────────
    bool sendNbapMsg(const NBAPMessage& msg)                    override;
    bool recvNbapMsg(NBAPMessage& msg)                          override;

    // ── LinkController hooks ──────────────────────────────────────────────────
    void reconnect();
    std::vector<std::string> injectableProcs() const;
    bool injectProcedure(const std::string& proc);

private:
    std::string nodeBId_;
    std::string rncAddr_;
    uint16_t    rncPort_   = 0;
    bool        connected_ = false;
    uint16_t    nextTxId_  = 1;

    // Описание радиолинка
    // Описание радиолинка
    struct RadioLink {
        uint16_t scrCode;
        SF       sf;
        bool     softHoLeg = false;  ///< true = secondary active-set leg
    };
    struct RadioBearerCfg {
        uint8_t  rbId;
        uint8_t  rlcMode;
        uint16_t maxBitrateKbps;
        bool     uplinkEnabled;
        bool     downlinkEnabled;
        SF       targetSf;
    };
    std::unordered_map<RNTI, RadioLink> links_;
    std::unordered_map<RNTI, std::unordered_map<uint8_t, RadioBearerCfg>> bearers_;
    /// Per-UE map of secondary legs keyed by scrambling code
    std::unordered_map<RNTI, std::unordered_map<uint16_t, RadioLink>> shoLegs_;

    // Активные измерения
    std::unordered_map<uint16_t /*measId*/, std::string> commonMeas_;

    // Входящая очередь «от RNC»
    std::queue<NBAPMessage> rxQueue_;
    mutable std::mutex      rxMtx_;
    std::unordered_map<uint16_t, std::string> pendingTraceSummary_;

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

#pragma once
#include "../common/types.h"
#include "nr_phy.h"
#include "nr_mac.h"
#include "nr_sdap.h"
#include "nr_pdcp.h"
#include "f1ap_codec.h"
#include "ngap_link.h"
#include "xnap_link.h"
#include "../hal/rf_interface.h"
#include <memory>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <optional>
#include <mutex>

namespace rbs::nr {

// ────────────────────────────────────────────────────────────────
// NRStack — 5G NR gNB-DU cell controller (stub)
//
// Drives the NR subframe clock (1 ms), manages UE contexts,
// and handles F1AP signalling towards the gNB-CU.
//
// References: TS 38.300, TS 38.401, TS 38.473
// ────────────────────────────────────────────────────────────────
class NRStack {
public:
    explicit NRStack(std::shared_ptr<hal::IRFHardware> rf,
                     const NRCellConfig& cfg);
    ~NRStack();

    bool   start();
    void   stop();
    bool   isRunning() const { return running_.load(); }

    /// Admit a UE on this NR cell.  Returns a 16-bit C-RNTI.
    uint16_t admitUE(uint64_t imsi, uint8_t defaultCQI = 9);

    /// Release a UE context.
    void     releaseUE(uint16_t crnti);

    size_t   connectedUECount() const;
    void     printStats() const;

    const NRCellConfig& config() const { return cfg_; }

    // ── F1AP interface ───────────────────────────────────────────
    /// Build and return the F1 Setup Request PDU for this cell.
    ByteBuffer buildF1SetupRequest() const;

    /// Process an incoming F1 Setup Response / Failure PDU.
    bool handleF1SetupResponse(const ByteBuffer& pdu);

    // ── NGAP interface ──────────────────────────────────────────
    void attachNgapLink(std::shared_ptr<NgapLink> ngap);
    bool connectNgPeer(uint64_t amfId);
    bool ngSetup(uint64_t amfId);
    bool ngSetupComplete(uint64_t amfId) const;
    size_t processNgMessages();
    bool hasActivePduSession(uint16_t crnti, uint8_t pduSessionId) const;

    // ── XnAP interface ──────────────────────────────────────────
    void attachXnLink(std::shared_ptr<XnAPLink> xnap);
    bool connectXnPeer(uint64_t targetGnbId);
    bool xnSetup(uint64_t targetGnbId);
    bool xnSetupComplete(uint64_t peerGnbId) const;
    bool handoverRequired(uint16_t crnti, uint64_t targetGnbId,
                          uint64_t targetCellId, uint8_t causeType = 0);
    size_t processXnMessages();
    bool hasReceivedXnHandover(uint16_t sourceCrnti) const;
    bool hasCompletedXnHandover(uint16_t sourceCrnti) const;
    uint16_t handoverTargetCrnti(uint16_t sourceCrnti) const;

    uint32_t currentSFN() const { return phy_ ? phy_->currentSFN() : 0; }

    // ── NR MAC/SDAP helpers for NSA data-path simulation ─────────────────────
    bool configureQoSFlow(uint16_t crnti, uint8_t qfi, uint8_t drbId);
    uint8_t resolveDrbForQfi(uint16_t crnti, uint8_t qfi) const;
    bool updateUeCqi(uint16_t crnti, uint8_t cqi);
    bool reportHarqFeedback(uint16_t crnti, uint8_t harqId, bool ack);
    std::vector<NRScheduleGrant> scheduleDl(uint16_t totalPrbs);
    uint32_t pendingDlBytes(uint16_t crnti) const;
    void setAutoDlScheduling(bool enabled, uint16_t prbsPerTick = 20);
    bool autoDlSchedulingEnabled() const { return autoDlEnabled_.load(); }
    bool submitDlSdapData(uint16_t crnti, uint8_t qfi,
                          const ByteBuffer& payload,
                          ByteBuffer& outPdcpPdu);
    bool setUeSlice(uint16_t crnti, NRSlice slice);
    std::vector<NRSliceMetrics> currentSliceMetrics() const;

    // ── EN-DC Secondary Cell Group (SCG) bearer support — TS 37.340 ──────────

    /// Accept a DC bearer from the MN (called after SgNB Addition Request).
    /// Returns the NR C-RNTI assigned for this UE on the NR cell, or 0 on error.
    uint16_t acceptSCGBearer(RNTI lteCrnti, const rbs::DCBearerConfig& cfg);

    /// Release all SCG bearers for a UE (called after SgNB Release).
    void releaseSCGBearer(RNTI lteCrnti);

    /// Returns the NR C-RNTI paired with \p lteCrnti, or 0 if not found.
    uint16_t scgCrnti(RNTI lteCrnti) const;

    /// Returns the active EN-DC option for a UE, or nullopt if not in EN-DC.
    std::optional<rbs::ENDCOption> endcOption(RNTI lteCrnti) const;

    /// Active EN-DC UE count.
    size_t endcUECount() const { return scgMap_.size(); }

private:
    NRCellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<NRPhy> phy_;
    std::shared_ptr<NRMac> mac_;
    std::shared_ptr<NRSDAP> sdap_;
    std::shared_ptr<NRPDCP> pdcp_;
    std::shared_ptr<NgapLink> ngap_;
    std::shared_ptr<XnAPLink> xnap_;
    mutable std::mutex stateMutex_;

    std::atomic<bool> running_{false};
    std::atomic<bool> autoDlEnabled_{false};
    std::atomic<uint16_t> autoDlPrbs_{20};
    std::thread       subframeThread_;

    uint16_t nextCrnti_ = 1;
    std::unordered_map<uint16_t, uint64_t> ueMap_;  // crnti → imsi

    bool f1SetupOk_ = false;
    uint64_t preferredAmfId_ = 0;
    uint16_t nextNgTransactionId_ = 1;
    uint16_t nextXnTransactionId_ = 1;

    struct NgPduSessionState {
        uint64_t amfUeNgapId = 0;
        uint8_t pduSessionId = 0;
        uint8_t sst = 1;
        uint32_t sd = 0;
        uint32_t gtpTeid = 0;
        bool active = false;
    };

    std::unordered_map<uint64_t, bool> ngPeers_;
    std::unordered_map<uint16_t, std::unordered_map<uint8_t, NgPduSessionState>> pduSessions_;

    struct XnHandoverState {
        uint64_t sourceGnbId = 0;
        uint64_t targetGnbId = 0;
        uint64_t sourceCellId = 0;
        uint64_t targetCellId = 0;
        uint16_t sourceCrnti = 0;
        uint16_t targetCrnti = 0;
        bool requestSent = false;
        bool requestReceived = false;
        bool notifyReceived = false;
    };

    std::unordered_map<uint64_t, bool> xnPeers_;
    std::unordered_map<uint16_t, XnHandoverState> xnHandovers_;

    // EN-DC SCG bearer table:  LTE-RNTI → {NR-CRNTI, option, bearer configs}
    struct SCGEntry {
        uint16_t                      nrCrnti;
        rbs::ENDCOption               option;
        std::vector<rbs::DCBearerConfig> bearers;
    };
    std::unordered_map<uint16_t, SCGEntry> scgMap_;  // lteCrnti → SCGEntry

    void subframeLoop();
};

}  // namespace rbs::nr

#pragma once
#include "../common/types.h"
#include "nr_phy.h"
#include "f1ap_codec.h"
#include "../hal/rf_interface.h"
#include <memory>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <optional>

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

    size_t   connectedUECount() const { return ueMap_.size(); }
    void     printStats() const;

    const NRCellConfig& config() const { return cfg_; }

    // ── F1AP interface ───────────────────────────────────────────
    /// Build and return the F1 Setup Request PDU for this cell.
    ByteBuffer buildF1SetupRequest() const;

    /// Process an incoming F1 Setup Response / Failure PDU.
    bool handleF1SetupResponse(const ByteBuffer& pdu);

    uint32_t currentSFN() const { return phy_ ? phy_->currentSFN() : 0; }

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

    std::atomic<bool> running_{false};
    std::thread       subframeThread_;

    uint16_t nextCrnti_ = 1;
    std::unordered_map<uint16_t, uint64_t> ueMap_;  // crnti → imsi

    bool f1SetupOk_ = false;

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

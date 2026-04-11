#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Inter-RAT Mobility Manager
//
// Tracks UE locations across GSM, UMTS, and LTE cells and coordinates
// inter-RAT handover triggers.  The manager is intentionally stack-agnostic:
// callers register a UE when it is admitted and provide a handover callback
// that performs the actual stack-level admit/release.
//
// Typical flow:
//   1. LTE stack admits UE → caller calls registerUE(imsi, RAT::LTE, rnti)
//   2. Signal degrades → caller calls triggerHandover(imsi, RAT::GSM, cb)
//   3. Callback admits UE in GSM stack, gets new RNTI, calls cb with it
//   4. Manager updates record; OMS counter "mobility.interRatHO" incremented
// ─────────────────────────────────────────────────────────────────────────────
#include "types.h"
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace rbs {

// ── Public data types ─────────────────────────────────────────────────────────

struct UELocation {
    RAT    rat;
    RNTI   rnti;
    CellId cellId;   // serving cell
};

// Handover completion callback — called by the user's admit logic.
// Must return the RNTI assigned in the target RAT (0 on failure).
using HandoverCallback = std::function<RNTI(IMSI, RAT targetRat)>;

// CSFB completion callback.  Called with (imsi, lteRnti, gsmArfcn).
// Must: call lteStack.triggerCSFB(lteRnti, gsmArfcn) and gsmStack.admitUE(imsi).
// Returns the new GSM RNTI (0 on failure).
using CSFBCallback = std::function<RNTI(IMSI, RNTI lteRnti, uint16_t gsmArfcn)>;

// ── MobilityManager ──────────────────────────────────────────────────────────

class MobilityManager {
public:
    // ---------------------------------------------------------------------------
    // Register a UE that has just been admitted to a RAT cell.
    // Overwrites any previous registration for the same IMSI.
    // ---------------------------------------------------------------------------
    void registerUE(IMSI imsi, RAT rat, RNTI rnti, CellId cellId = 0);

    // ---------------------------------------------------------------------------
    // Remove a UE record (called when the UE detaches or is released).
    // Silent no-op if the IMSI is not registered.
    // ---------------------------------------------------------------------------
    void unregisterUE(IMSI imsi);

    // ---------------------------------------------------------------------------
    // Return current location of a UE, or std::nullopt if unknown.
    // ---------------------------------------------------------------------------
    std::optional<UELocation> getUELocation(IMSI imsi) const;

    // ---------------------------------------------------------------------------
    // Return number of currently registered UEs.
    // ---------------------------------------------------------------------------
    size_t registeredUECount() const;

    // ---------------------------------------------------------------------------
    // Trigger an inter-RAT handover.
    //
    // The caller provides a HandoverCallback that is responsible for:
    //   - Admitting the UE in targetRat and returning the new RNTI, OR
    //   - Returning 0 to indicate failure (record is left unchanged).
    //
    // On success the UE record is updated to the target RAT and the internal
    // handover counter is incremented.
    //
    // Returns true if the handover was executed successfully.
    // ---------------------------------------------------------------------------
    bool triggerHandover(IMSI imsi, RAT targetRat, CellId targetCellId,
                         const HandoverCallback& cb);

    // ---------------------------------------------------------------------------
    // Trigger Circuit Switched Fallback (LTE → GSM).
    //
    // Validates that the UE is currently on LTE.  The callback must:
    //   1. Call lteStack.triggerCSFB(lteRnti, gsmArfcn)  — releases LTE + redirects
    //   2. Call gsmStack.admitUE(imsi)                   — admits UE in GSM
    //   3. Return the new GSM RNTI (0 on failure)
    //
    // On success the UE record is updated to RAT::GSM and csfbCount is incremented.
    // TS 36.300 §22.3.2
    // ---------------------------------------------------------------------------
    bool triggerCSFB(IMSI imsi, uint16_t gsmArfcn, CellId gsmCellId,
                     const CSFBCallback& cb);

    // Return total number of successful CSFB procedures since creation.
    uint64_t csfbCount() const;

    // ---------------------------------------------------------------------------
    // Return the total number of successful inter-RAT handovers since creation.
    // ---------------------------------------------------------------------------
    uint64_t handoverCount() const;

    // ---------------------------------------------------------------------------
    // Reset all state (for testing).
    // ---------------------------------------------------------------------------
    void reset();

private:
    mutable std::mutex                     mutex_;
    std::unordered_map<IMSI, UELocation>   ues_;
    uint64_t                               hoCount_{0};
    uint64_t                               csfbCount_{0};
};

} // namespace rbs

#pragma once
#include "../common/types.h"
#include "lte_phy.h"
#include "lte_mac.h"
#include "lte_pdcp.h"
#include "lte_rrc.h"
#include "lte_rlc.h"
#include "s1ap_link.h"
#include "x2ap_link.h"
#include "volte_stub.h"
#include "../hal/rf_interface.h"
#include "ilte_stack.h"
#include <memory>
#include <unordered_map>
#include <set>
#include <thread>
#include <atomic>

namespace rbs::lte {

// ────────────────────────────────────────────────────────────────
// LTE Stack – top-level eNodeB cell controller
// Wires PDCP → (RLC stub) → MAC → PHY and drives the 1 ms
// subframe clock.
// ────────────────────────────────────────────────────────────────
class LTEStack : public ILTEStack {
public:
    explicit LTEStack(std::shared_ptr<hal::IRFHardware> rf,
                      const LTECellConfig& cfg);
    ~LTEStack();

    bool  start()                                                              override;
    void  stop()                                                               override;
    bool  isRunning() const                                                    override { return running_.load(); }

    bool  sendIPPacket  (RNTI rnti, uint16_t bearerId, ByteBuffer ipPacket)    override;
    bool  receiveIPPacket(RNTI rnti, uint16_t bearerId, ByteBuffer& ipPacket)  override;

    RNTI  admitUE(IMSI imsi, uint8_t defaultCQI = 9)                           override;
    RNTI  admitUECA(IMSI imsi, uint8_t ccCount, uint8_t defaultCQI = 9)        override;
    void  releaseUE(RNTI rnti)                                                 override;
    void  triggerCSFB(RNTI rnti, uint16_t gsmArfcn)                           override;
    void  updateCQI(RNTI rnti, uint8_t cqi)                                   override;

    bool  setupERAB  (RNTI rnti, uint8_t erabId, const GTPUTunnel& sgw)        override;
    bool  teardownERAB(RNTI rnti, uint8_t erabId)                              override;
    bool  setupVoLTEBearer(RNTI rnti)                                           override;
    bool  handleSipMessage(RNTI rnti, const std::string& sipMessage)            override;
    size_t sendVoLteRtpBurst(RNTI rnti, size_t packetCount,
                             size_t payloadBytes = 160)                          override;
    bool  requestHandover(RNTI rnti, uint16_t targetPci, EARFCN targetEarfcn);

    size_t connectedUECount() const                                            override;
    void   printStats() const                                                  override;

    const LTECellConfig& config() const                                        override { return cfg_; }

private:
    LTECellConfig cfg_;
    std::shared_ptr<hal::IRFHardware> rf_;
    std::shared_ptr<LTEPhy>  phy_;
    std::shared_ptr<LTEMAC>  mac_;
    std::shared_ptr<PDCP>    pdcp_;
    std::shared_ptr<LTERrc>  rrc_;
    std::shared_ptr<LTERlc>  rlc_;
    std::unique_ptr<S1APLink> s1ap_;
    std::unique_ptr<S1ULink>  s1u_;   // S1-U GTP-U forwarding (TS 29.060)
    std::unique_ptr<X2APLink> x2ap_;  // X2AP inter-eNB handover (TS 36.423)

    std::atomic<bool>  running_{false};
    std::thread        subframeThread_;
    RNTI nextRnti_ = 1;
    std::unordered_map<RNTI, IMSI> ueMap_;
    std::set<std::pair<RNTI, uint8_t>> activeERABs_;  ///< (rnti, erabId) tuples with live GTP-U tunnels
    struct VoLTEState {
        uint16_t rtpSeq = 1;
        uint32_t rtpTs = 0;
        uint32_t ssrc = 0;
    };
    std::unordered_map<RNTI, VoLTEState> volteState_;
    std::unordered_map<RNTI, uint64_t> lastHoEpochMs_;
    std::unordered_map<RNTI, uint16_t> lastHoTargetPci_;
    uint32_t hoMinIntervalMs_ = 1000;

    static uint32_t packPlmnHex(uint16_t mcc, uint16_t mnc);
    void subframeLoop();
    void forwardDlPackets();    ///< Poll S1-U DL GTP-U and inject into air interface
};

}  // namespace rbs::lte

#pragma once
#include "../common/types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// S1 interface — LTE eNodeB ↔ EPC (MME + SGW)
//
// References:
//   TS 36.410 — S1 general aspects
//   TS 36.412 — S1 transport mapping (SCTP/IP)
//   TS 36.413 — S1AP (Application Protocol, eNB–MME)
//   TS 29.274 — GTP-U tunnelling for S1-U (eNB–SGW)
// ─────────────────────────────────────────────────────────────────────────────

// ── S1AP Procedure Codes (TS 36.413 §9.1, representative subset) ─────────────
enum class S1APProcedure : uint8_t {
    S1_SETUP                     = 0x11,
    RESET                        = 0x0E,
    PAGING                       = 0x0A,  // ProcedureCode_id_Paging = 10
    INITIAL_UE_MESSAGE           = 0x0C,
    DOWNLINK_NAS_TRANSPORT       = 0x0B,
    UPLINK_NAS_TRANSPORT         = 0x0D,
    INITIAL_CONTEXT_SETUP        = 0x09,
    UE_CONTEXT_RELEASE_REQUEST   = 0x1A,
    UE_CONTEXT_RELEASE_COMMAND   = 0x17,
    UE_CONTEXT_RELEASE_COMPLETE  = 0x19,
    E_RAB_SETUP                  = 0x05,
    E_RAB_MODIFY                 = 0x06,
    E_RAB_RELEASE                = 0x07,
    HANDOVER_REQUIRED            = 0x00,
    HANDOVER_COMMAND             = 0x01,  ///< HandoverResourceAllocation proc (target side) or HO accepted
    HANDOVER_NOTIFY              = 0x02,
    PATH_SWITCH_REQUEST          = 0x03,
    ERROR_INDICATION             = 0x0F,
    // ── Extended S1 Handover procedures ─────────────────────────────────────
    HANDOVER_REQUEST             = 0x1B,  ///< HandoverResourceAllocation initiating (MME→target eNB)
    HANDOVER_REQUEST_ACKNOWLEDGE = 0x1C,  ///< HandoverResourceAllocation successful (target eNB→MME)
    HANDOVER_PREPARATION_FAILURE = 0x1D,  ///< HandoverPreparation unsuccessful (MME→source eNB)
    HANDOVER_FAILURE             = 0x1E,  ///< HandoverResourceAllocation unsuccessful (target eNB→MME)
    ENB_STATUS_TRANSFER          = 0x18,  ///< eNBStatusTransfer proc=24 (source eNB→MME)
    MME_STATUS_TRANSFER          = 0x1F,  ///< MMEStatusTransfer proc=25 (MME→target eNB)
};

// ── GTP-U tunnel endpoint ─────────────────────────────────────────────────────
struct GTPUTunnel {
    uint32_t teid;        ///< Tunnel Endpoint Identifier
    uint32_t remoteIPv4;  ///< SGW/target IP address (network byte order)
    uint16_t udpPort;     ///< GTP-U port (default 2152)
};

// ── E-RAB (E-UTRAN Radio Access Bearer) ──────────────────────────────────────
struct ERAB {
    uint8_t    erabId;
    uint8_t    qci;                ///< QoS Class Identifier
    uint8_t    arpPriorityLevel;   ///< Allocation/Retention Priority
    GTPUTunnel sgwTunnel;          ///< SGW-side GTP-U endpoint
};

struct S1APMessage {
    S1APProcedure procedure;
    uint32_t      mmeUeS1apId;
    uint32_t      enbUeS1apId;
    ByteBuffer    payload;
    bool          isSuccessfulOutcome = false;
    bool          isUnsuccessfulOutcome = false;
};

// ── IS1AP — S1 Application Protocol (eNB ↔ MME) ─────────────────────────────
class IS1AP {
public:
    virtual ~IS1AP() = default;

    // ── Link management ───────────────────────────────────────────────────────
    virtual bool connect   (const std::string& mmeAddr,
                            uint16_t port = 36412) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // ── S1 Setup (TS 36.413 §8.7.3) ──────────────────────────────────────────
    /// Exchange eNB capabilities and supported PLMNs with the MME.
    virtual bool s1Setup(uint32_t enbId, const std::string& enbName,
                         uint32_t tac, uint32_t plmnId) = 0;

    // ── NAS transport ─────────────────────────────────────────────────────────
    /// Initial UE Message — UE-triggered, carries first NAS PDU (TS 36.413 §8.6.2.1).
    virtual bool initialUEMessage  (RNTI rnti, IMSI imsi,
                                    const ByteBuffer& nasPdu) = 0;

    /// DL NAS Transport — MME → eNB → UE.
    virtual bool downlinkNASTransport(uint32_t mmeUeS1apId, RNTI rnti,
                                      const ByteBuffer& nasPdu) = 0;

    /// UL NAS Transport — UE → eNB → MME.
    virtual bool uplinkNASTransport  (uint32_t mmeUeS1apId, RNTI rnti,
                                      const ByteBuffer& nasPdu) = 0;

    // ── Context / bearer management ───────────────────────────────────────────
    /// Reply to Initial Context Setup Request from MME.
    virtual bool initialContextSetupResponse(uint32_t mmeUeS1apId,
                                             RNTI rnti,
                                             const std::vector<ERAB>& erabs) = 0;

    /// Request that the MME release the UE context (radio/transport failure).
    virtual bool ueContextReleaseRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                         const std::string& cause) = 0;

    /// Confirm UE context fully released at eNB side.
    virtual bool ueContextReleaseComplete(uint32_t mmeUeS1apId,
                                          RNTI rnti) = 0;

    // ── E-RAB bearer management ───────────────────────────────────────────────
    /// Confirm E-RABs set up (TS 36.413 §8.4.1).
    virtual bool erabSetupResponse(uint32_t mmeUeS1apId, RNTI rnti,
                                   const std::vector<ERAB>& erabs,
                                   const std::vector<uint8_t>& failedErabIds) = 0;

    /// Confirm E-RABs released (TS 36.413 §8.4.3).
    virtual bool erabReleaseResponse(uint32_t mmeUeS1apId, RNTI rnti,
                                     const std::vector<uint8_t>& releasedErabIds) = 0;

    // ── Handover ──────────────────────────────────────────────────────────────
    /// Path Switch Request — after successful X2 handover, re-anchor S1 path.
    virtual bool pathSwitchRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                   uint32_t targetEnbId,
                                   const std::vector<ERAB>& erabs) = 0;

    /// Handover Required — notify MME that UE needs to move to target eNB (TS 36.413 §8.5.1).
    virtual bool handoverRequired(uint32_t mmeUeS1apId, RNTI rnti,
                                  uint32_t targetEnbId,
                                  const ByteBuffer& rrcContainer) = 0;

    /// Handover Notify — confirm UE arrived at target cell (TS 36.413 §8.5.2).
    virtual bool handoverNotify(uint32_t mmeUeS1apId, RNTI rnti) = 0;

    /// Handover Request Acknowledge (TS 36.413 §8.5.2 — target eNB→MME).
    /// Sent after successfully allocating resources for the handed-over UE.
    /// targetToSrcContainer: Target-to-Source transparent container (RRC reconf).
    virtual bool handoverRequestAcknowledge(uint32_t mmeUeS1apId, RNTI rnti,
                                            const ByteBuffer& targetToSrcContainer) = 0;

    /// eNB Status Transfer (TS 36.413 §8.5.2 — source eNB→MME).
    /// Forwards PDCP SN status (one dummy bearer at eRAB-ID=5 for simulation).
    virtual bool enbStatusTransfer(uint32_t mmeUeS1apId, RNTI rnti) = 0;

    /// Handover Failure (TS 36.413 §8.5.2 — target eNB→MME).
    /// Sent when the target eNB cannot allocate resources for the UE.
    virtual bool handoverFailure(uint32_t mmeUeS1apId,
                                 uint8_t causeGroup, uint8_t causeValue) = 0;

    // ── Paging (MME→eNB, TS 36.413 §8.7.1) ──────────────────────────────────
    /// Encode and send a Paging message (initiatingMessage direction).
    /// ueIdxVal: 10-bit UE identity index (IMSI mod 1024).
    /// imsi: raw IMSI bytes used as UEPagingID iMSI.
    /// cnDomain: 0=PS, 1=CS.
    virtual bool paging(uint16_t ueIdxVal, const ByteBuffer& imsi,
                        uint32_t plmnId, uint16_t tac,
                        uint8_t cnDomain) = 0;

    /// Send Reset (TS 36.413 §8.7.2).  causeGroup: 0=radioNetwork 1=transport
    ///   2=nas 3=protocol 4+=misc.  resetAll=true → whole S1-interface reset.
    virtual bool reset(uint8_t causeGroup, uint8_t causeValue,
                       bool resetAll = true) = 0;

    /// Send Error Indication (TS 36.413 §8.7.4).
    /// Pass mmeUeS1apId/enbUeS1apId = 0 to omit those IEs.
    /// Pass causeGroup = 0xFF to omit the Cause IE.
    virtual bool errorIndication(uint32_t mmeUeS1apId, uint32_t enbUeS1apId,
                                 uint8_t causeGroup, uint8_t causeValue) = 0;

    // ── Raw message pump ──────────────────────────────────────────────────────
    virtual bool sendS1APMsg(const S1APMessage& msg) = 0;
    virtual bool recvS1APMsg(S1APMessage& msg) = 0;
};

// ── IS1U — S1-U user-plane GTP tunnelling (eNB ↔ SGW) ───────────────────────
class IS1U {
public:
    virtual ~IS1U() = default;

    /// Create a GTP-U bearer tunnel (called after Initial Context Setup).
    virtual bool createTunnel(RNTI rnti, uint8_t erabId,
                              const GTPUTunnel& sgwEndpoint) = 0;

    /// Delete a GTP-U bearer tunnel (called on E-RAB release).
    virtual bool deleteTunnel(RNTI rnti, uint8_t erabId) = 0;

    /// Deliver a UL IP packet to the SGW via GTP-U.
    virtual bool sendGtpuPdu(RNTI rnti, uint8_t erabId,
                             const ByteBuffer& ipPacket) = 0;

    /// Receive a DL IP packet from the SGW via GTP-U.
    virtual bool recvGtpuPdu(RNTI rnti, uint8_t erabId,
                             ByteBuffer& ipPacket) = 0;
};

}  // namespace rbs::lte

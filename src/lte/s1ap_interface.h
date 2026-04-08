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
    PAGING                       = 0x18,
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
    HANDOVER_COMMAND             = 0x01,
    HANDOVER_NOTIFY              = 0x02,
    PATH_SWITCH_REQUEST          = 0x03,
    ERROR_INDICATION             = 0x0F,
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

    // ── Handover ──────────────────────────────────────────────────────────────
    /// Path Switch Request — after successful X2 handover, re-anchor S1 path.
    virtual bool pathSwitchRequest(uint32_t mmeUeS1apId, RNTI rnti,
                                   uint32_t targetEnbId,
                                   const std::vector<ERAB>& erabs) = 0;

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

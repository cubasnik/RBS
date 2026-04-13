#pragma once
#include "../common/types.h"
#include "s1ap_interface.h"   // for ERAB, GTPUTunnel
#include <string>
#include <vector>
#include <cstdint>

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// X2 interface — inter-eNodeB direct interface
//
// References:
//   TS 36.420 — X2 general aspects
//   TS 36.422 — X2 transport mapping (SCTP/IP)
//   TS 36.423 — X2AP (X2 Application Protocol)
//   TS 36.425 — X2-U user-plane (GTP-U data forwarding during HO)
//
// Use cases:
//   • X2-based handover (TS 36.300 §10.1.2)
//   • Inter-cell interference coordination (ICIC) — load indication
//   • Radio Link Failure indication
// ─────────────────────────────────────────────────────────────────────────────

// ── X2AP Procedure Codes (TS 36.423 §9.2, representative subset) ─────────────
enum class X2APProcedure : uint8_t {
    HANDOVER_REQUEST              = 0x00,
    HANDOVER_REQUEST_ACK          = 0x01,
    HANDOVER_PREPARATION_FAILURE  = 0x02,
    HANDOVER_CANCEL               = 0x03,
    SN_STATUS_TRANSFER            = 0x04,
    UE_CONTEXT_RELEASE            = 0x05,
    X2_SETUP                      = 0x06,
    X2_SETUP_RESPONSE             = 0x07,
    X2_SETUP_FAILURE              = 0x08,
    RESET                         = 0x09,
    LOAD_INDICATION               = 0x0A,
    ERROR_INDICATION              = 0x0B,
    RLF_INDICATION                = 0x0C,
    HANDOVER_REPORT               = 0x0D,
    RESOURCE_STATUS_REQUEST       = 0x0F,
    RESOURCE_STATUS_RESPONSE      = 0x10,
    RESOURCE_STATUS_REPORT        = 0x11,
    // EN-DC procedures (TS 36.423 §8.x, as extended by TS 37.340)
    SGNB_ADDITION_REQUEST         = 0x20,
    SGNB_ADDITION_REQUEST_ACK     = 0x21,
    SGNB_ADDITION_REQUEST_REJECT  = 0x22,
    SGNB_MODIFICATION_REQUEST     = 0x23,
    SGNB_MODIFICATION_REQUEST_ACK = 0x24,
    SGNB_RELEASE_REQUEST          = 0x25,
    SGNB_RELEASE_REQUEST_ACK      = 0x26,
    SGNB_COUNT_REPORT             = 0x27,
};

// ── PDCP SN status per DRB (for lossless in-order delivery at target) ────────
struct SNStatusItem {
    uint8_t  drbId;
    uint16_t ulPdcpSN;   ///< next expected UL PDCP SN at target
    uint32_t ulHfn;      ///< HFN count for UL
    uint16_t dlPdcpSN;   ///< last used DL PDCP SN at source
    uint32_t dlHfn;      ///< HFN count for DL
};

// ── Handover Request / Acknowledge ───────────────────────────────────────────
struct X2HORequest {
    RNTI               rnti;
    uint32_t           sourceEnbId;
    uint32_t           targetCellId;
    uint8_t            causeType;         ///< 0=radio, 1=transport, 2=protocol
    std::vector<ERAB>  erabs;             ///< E-RABs to handover
    ByteBuffer         rrcContainer;      ///< RRC HandoverPreparation message
};

struct X2HORequestAck {
    RNTI                    rnti;
    std::vector<ERAB>       admittedErabs;
    std::vector<uint8_t>    notAdmittedErabIds;
    ByteBuffer              rrcContainer; ///< RRC HandoverCommand embedded
};

struct X2APMessage {
    X2APProcedure procedure;
    uint32_t      sourceEnbUeX2apId;
    uint32_t      targetEnbUeX2apId;
    ByteBuffer    payload;
    std::string   traceId;
};

// ── IX2AP — X2 Application Protocol ─────────────────────────────────────────
class IX2AP {
public:
    virtual ~IX2AP() = default;

    // ── Link management ───────────────────────────────────────────────────────
    virtual bool connect   (uint32_t targetEnbId, const std::string& addr,
                            uint16_t port = 36422,
                            uint16_t localPort = 0) = 0;
    virtual void disconnect(uint32_t targetEnbId) = 0;
    virtual bool isConnected(uint32_t targetEnbId) const = 0;

    // ── X2 Setup (TS 36.423 §8.3.4) ──────────────────────────────────────────
    /// Establish X2 link with a neighbour eNB.
    virtual bool x2Setup(uint32_t localEnbId, uint32_t targetEnbId) = 0;

    // ── Handover preparation ──────────────────────────────────────────────────
    /// Source eNB: send Handover Request to target eNB.
    virtual bool handoverRequest(const X2HORequest& req) = 0;

    /// Target eNB: acknowledge Handover Request (resources admitted).
    virtual bool handoverRequestAck(const X2HORequestAck& ack) = 0;

    /// Target eNB: reject Handover Request.
    virtual bool handoverPreparationFailure(RNTI rnti,
                                            const std::string& cause) = 0;

    /// Source eNB: cancel an ongoing handover preparation.
    virtual bool handoverCancel(RNTI rnti, const std::string& cause) = 0;

    // ── Post-handover ─────────────────────────────────────────────────────────
    /// Transfer PDCP SN status to ensure lossless delivery at target.
    virtual bool snStatusTransfer(RNTI rnti,
                                  const std::vector<SNStatusItem>& items) = 0;

    /// Release X2 resources after handover completion.
    virtual bool ueContextRelease(RNTI rnti) = 0;

    // ── Interference coordination (ICIC) ──────────────────────────────────────
    /// Send load indication (DL/UL PRB utilisation) to neighbour eNBs.
    virtual bool loadIndication(uint32_t targetEnbId,
                                uint8_t dlPrbOccupancy,
                                uint8_t ulPrbOccupancy) = 0;

    // ── Raw message pump ──────────────────────────────────────────────────────
    virtual bool sendX2APMsg(const X2APMessage& msg) = 0;
    virtual bool recvX2APMsg(X2APMessage& msg) = 0;

    // ── EN-DC Secondary gNB (SgNB) management — TS 37.340 / TS 36.423 §8.x ──

    /// MN→SN: request SN to add UE as secondary node (Options 3, 3a, 3x).
    /// \param rnti      UE RNTI at MN (eNB)
    /// \param option    EN-DC deployment option
    /// \param bearers   Bearer split configuration proposed by MN
    virtual bool sgNBAdditionRequest(RNTI rnti, rbs::ENDCOption option,
                                     const std::vector<rbs::DCBearerConfig>& bearers) = 0;

    /// SN→MN: positive acknowledgement; snAssignedCrnti filled for SCG-leg.
    virtual bool sgNBAdditionRequestAck(RNTI rnti,
                                        std::vector<rbs::DCBearerConfig>& bearers) = 0;

    /// SN→MN: SN rejects the addition (resource shortage, etc.)
    virtual bool sgNBAdditionRequestReject(RNTI rnti,
                                           const std::string& cause) = 0;

    /// MN→SN: request bearer reconfiguration (e.g. change split threshold).
    virtual bool sgNBModificationRequest(RNTI rnti,
                                         const rbs::DCBearerConfig& bearer) = 0;

    /// SN→MN: confirm modification.
    virtual bool sgNBModificationRequestAck(RNTI rnti,
                                            const rbs::DCBearerConfig& bearer) = 0;

    /// MN→SN: release UE from the secondary node.
    virtual bool sgNBReleaseRequest(RNTI rnti) = 0;

    /// SN→MN: confirm release.
    virtual bool sgNBReleaseRequestAck(RNTI rnti) = 0;
};

// ── IX2U — X2-U GTP-U data forwarding (during handover execution) ────────────
class IX2U {
public:
    virtual ~IX2U() = default;

    /// Open a DL forwarding tunnel to the target eNB (indirect forwarding).
    virtual bool openForwardingTunnel(RNTI rnti, const std::string& targetAddr,
                                      uint32_t teid) = 0;

    /// Forward a queued DL PDCP PDU to the target eNB.
    virtual bool forwardPacket(RNTI rnti, const ByteBuffer& pdcpPdu) = 0;

    /// Close forwarding tunnel after successful path switch.
    virtual void closeForwardingTunnel(RNTI rnti) = 0;
};

}  // namespace rbs::lte

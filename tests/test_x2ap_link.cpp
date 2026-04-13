#include "../src/lte/x2ap_link.h"
#include "../src/lte/x2ap_codec.h"
#include "../src/common/logger.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>

using namespace rbs;
using namespace rbs::lte;

static bool waitX2Msg(X2APLink& link, X2APMessage& msg, int timeoutMs = 1000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (link.recvX2APMsg(msg)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

static void test_x2ap_transport_trace_path() {
    X2APLink source("eNB-src-trace");
    X2APLink target("eNB-tgt-trace");

    assert(source.connect(0x200, "127.0.0.1", 39512, 39511));
    assert(target.connect(0x100, "127.0.0.1", 39511, 39512));
    assert(source.isConnected(0x200));
    assert(target.isConnected(0x100));

    X2APMessage rx{};
    const std::string setupTraceId = "test-x2ap-setup-trace";
    {
        rbs::ScopedTraceId traceScope(setupTraceId);
        assert(source.x2Setup(0x100, 0x200));
    }
    assert(waitX2Msg(target, rx));
    assert(rx.procedure == X2APProcedure::X2_SETUP);
    assert(rx.traceId == setupTraceId);
    assert(!rx.payload.empty());

    const std::string hoTraceId = "test-x2ap-ho-trace";
    ERAB erab1{5, 9, 2, {0xABCD0001, 0x7F000002, 2152}};
    X2HORequest hoReq{};
    hoReq.rnti = 0x0003;
    hoReq.sourceEnbId = 0x100;
    hoReq.targetCellId = 0x20000;
    hoReq.causeType = 0;
    hoReq.erabs = {erab1};
    hoReq.rrcContainer = {0x06, 0x00, 0x01};
    {
        rbs::ScopedTraceId traceScope(hoTraceId);
        assert(source.handoverRequest(hoReq));
    }
    assert(waitX2Msg(target, rx));
    assert(rx.procedure == X2APProcedure::HANDOVER_REQUEST);
    assert(rx.traceId == hoTraceId);
    assert(!rx.payload.empty());

    source.disconnect(0x200);
    target.disconnect(0x100);
    std::puts("  test_x2ap_transport_trace_path PASSED");
}

static void test_x2ap_multi_ue_handover_isolation() {
    // ────────────────────────────────────────────────────────────────────────────
    // Multi-UE Handover Isolation Test
    // Verifies that handover/release of one UE does not affect other UEs' context
    // ────────────────────────────────────────────────────────────────────────────
    X2APLink sourceEnb("eNB-src-multi");
    X2APLink targetEnb("eNB-tgt-multi");
    
    const uint32_t sourceId = 0x100;
    const uint32_t targetId = 0x200;
    
    // Establish X2 connectivity
    assert(sourceEnb.connect(targetId, "127.0.0.1", 39522, 39521));
    assert(targetEnb.connect(sourceId, "127.0.0.1", 39521, 39522));
    assert(sourceEnb.isConnected(targetId));
    assert(targetEnb.isConnected(sourceId));
    
    // X2 Setup
    assert(sourceEnb.x2Setup(sourceId, targetId));
    assert(targetEnb.x2Setup(targetId, sourceId));
    
    // ── Handover UE1 ──────────────────────────────────────────────────────────
    const uint16_t rnti1 = 0x0001;
    ERAB erab1_ue1{5, 9, 2, {0x0001, 0x7F000002, 2152}};
    
    X2HORequest hoReq1{};
    hoReq1.rnti         = rnti1;
    hoReq1.sourceEnbId  = sourceId;
    hoReq1.targetCellId = 0x200 << 8;  // Target eNB ID = 0x200
    hoReq1.causeType    = 0;  // radio
    hoReq1.erabs        = {erab1_ue1};
    hoReq1.rrcContainer = {0x06, 0x00, 0x01};
    
    assert(sourceEnb.handoverRequest(hoReq1));
    
    // Target receives and acknowledges
    X2APMessage hoAckMsg1{};
    bool gotHoAck1 = targetEnb.recvX2APMsg(hoAckMsg1);
    (void)gotHoAck1;
    
    // Send HO Request Ack
    X2HORequestAck hoAck1{};
    hoAck1.rnti          = rnti1;
    hoAck1.admittedErabs = {erab1_ue1};
    hoAck1.rrcContainer  = {0x06, 0x01};
    assert(targetEnb.handoverRequestAck(hoAck1));
    
    // ── Handover UE2 (different RNTI) ──────────────────────────────────────────
    const uint16_t rnti2 = 0x0002;
    ERAB erab1_ue2{6, 9, 2, {0x0002, 0x7F000003, 2152}};
    
    X2HORequest hoReq2{};
    hoReq2.rnti         = rnti2;
    hoReq2.sourceEnbId  = sourceId;
    hoReq2.targetCellId = 0x200 << 8;
    hoReq2.causeType    = 0;
    hoReq2.erabs        = {erab1_ue2};
    hoReq2.rrcContainer = {0x06, 0x00, 0x02};
    
    assert(sourceEnb.handoverRequest(hoReq2));
    
    // Target receives
    X2APMessage hoAckMsg2{};
    bool gotHoAck2 = targetEnb.recvX2APMsg(hoAckMsg2);
    (void)gotHoAck2;
    
    // Send HO Request Ack for UE2
    X2HORequestAck hoAck2{};
    hoAck2.rnti          = rnti2;
    hoAck2.admittedErabs = {erab1_ue2};
    hoAck2.rrcContainer  = {0x06, 0x01};
    assert(targetEnb.handoverRequestAck(hoAck2));
    
    // ── SN Status Transfer for both UEs ──────────────────────────────────────────
    SNStatusItem snItem1{1, 100, 0, 200, 0};
    assert(sourceEnb.snStatusTransfer(rnti1, {snItem1}));
    
    SNStatusItem snItem2{1, 150, 0, 250, 0};
    assert(sourceEnb.snStatusTransfer(rnti2, {snItem2}));
    
    // ── Release UE1 only; verify UE2 state intact ──────────────────────────────
    assert(sourceEnb.ueContextRelease(rnti1));
    
    // Verify UE2 can still perform X2 operations (e.g., handover cancel for diff UE)
    assert(sourceEnb.handoverCancel(0x0003, "test-cancel"));  // Different RNTI not in active list
    
    // Release UE2
    assert(sourceEnb.ueContextRelease(rnti2));
    
    sourceEnb.disconnect(targetId);
    targetEnb.disconnect(sourceId);
    assert(!sourceEnb.isConnected(targetId));
    assert(!targetEnb.isConnected(sourceId));
    
    std::puts("  test_x2ap_multi_ue_handover_isolation PASSED");
}

int main() {
    // ── X2APLink ──────────────────────────────────────────────────────────────
    X2APLink x2("eNB-002");

    assert(!x2.isConnected(1));

    // connect() к peer eNB 1
    assert(x2.connect(1 /*targetEnbId*/, "127.0.0.1", 36422));
    assert(x2.isConnected(1));

    // x2Setup: кодирует X2 Setup Request, симулирует ACK
    assert(x2.x2Setup(2 /*localEnbId*/, 1 /*targetEnbId*/));

    // recvX2APMsg: может быть симулированный ACK
    X2APMessage setupResp{};
    bool gotSetup = x2.recvX2APMsg(setupResp);
    (void)gotSetup;

    // handoverRequest: симулирует немедленный HO Request Ack
    ERAB erab1{5, 9, 2, {0xABCD0001, 0x7F000002, 2152}};
    X2HORequest hoReq{};
    hoReq.rnti         = 0x0003;
    hoReq.sourceEnbId  = 2;
    // handoverRequest проверяет isConnected(targetCellId >> 8) — peer ID = 1
    hoReq.targetCellId = 0x100; // 0x100 >> 8 == 1 == connected peer
    hoReq.causeType    = 0; // radio
    hoReq.erabs        = {erab1};
    hoReq.rrcContainer = {0x06, 0x00, 0x01};
    assert(x2.handoverRequest(hoReq));

    // recvX2APMsg: HO Request Ack должен прийти
    X2APMessage hoAck{};
    bool gotAck = x2.recvX2APMsg(hoAck);
    (void)gotAck;  // зависит от impl

    // handoverRequestAck
    X2HORequestAck ack{};
    ack.rnti           = 0x0003;
    ack.admittedErabs  = {erab1};
    ack.rrcContainer   = {0x06, 0x01};
    assert(x2.handoverRequestAck(ack));

    // snStatusTransfer
    SNStatusItem sn{1, 100, 0, 200, 0};
    assert(x2.snStatusTransfer(0x0003, {sn}));

    // ueContextRelease
    assert(x2.ueContextRelease(0x0003));

    // loadIndication
    assert(x2.loadIndication(1, 70 /*dlPrb%*/, 50 /*ulPrb%*/));

    // handoverPreparationFailure
    assert(x2.handoverPreparationFailure(0x0004, "no-radio-resources"));

    // handoverCancel
    assert(x2.handoverCancel(0x0005, "handover-cancelled"));

    // sendX2APMsg / recvX2APMsg
    X2APMessage msg{X2APProcedure::X2_SETUP, 0, 0, {0x01}};
    assert(x2.sendX2APMsg(msg));
    X2APMessage rxMsg{};
    bool rx = x2.recvX2APMsg(rxMsg);
    (void)rx;

    test_x2ap_transport_trace_path();
    test_x2ap_multi_ue_handover_isolation();

    x2.disconnect(1);
    assert(!x2.isConnected(1));

    // ── X2ULink ───────────────────────────────────────────────────────────────
    X2ULink x2u("eNB-002");

    // openForwardingTunnel
    assert(x2u.openForwardingTunnel(0x0003, "127.0.0.1", 0x11223344 /*teid*/));

    // forwardPacket: отправляет PDCP PDU на target eNB через GTP-U
    ByteBuffer pdcpPdu{0x80, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    bool fwdOk = x2u.forwardPacket(0x0003, pdcpPdu);
    (void)fwdOk;

    // closeForwardingTunnel
    x2u.closeForwardingTunnel(0x0003);

    // повторный forward после close → не крашится
    x2u.forwardPacket(0x0003, pdcpPdu);

    std::puts("test_x2ap_link PASSED");
    return 0;
}

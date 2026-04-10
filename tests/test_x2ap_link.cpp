#include "../src/lte/x2ap_link.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::lte;

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

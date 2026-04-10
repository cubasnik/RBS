#include "../src/lte/s1ap_link.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>

using namespace rbs;
using namespace rbs::lte;

int main() {
    // ── S1APLink ──────────────────────────────────────────────────────────────
    S1APLink s1("eNB-001");

    assert(!s1.isConnected());

    // connect() — bind UDP socket к случайному порту, connect к "MME"
    // В симуляторе просто устанавливается connected_=true
    assert(s1.connect("127.0.0.1", 36412));
    assert(s1.isConnected());

    // s1Setup: кодирует S1 Setup Request, симулирует S1_OK ответ
    assert(s1.s1Setup(0x0001F101, "TesteNB", 1, 0xF20001));

    // recvS1APMsg: должен быть симулированный ответ от MME
    S1APMessage setupResp{};
    bool gotSetup = s1.recvS1APMsg(setupResp);
    (void)gotSetup; // зависит от impl, но не должно крашиться

    // initialUEMessage: первое NAS PDU от UE
    ByteBuffer nasPdu1{0x07, 0x41, 0x01, 0x02, 0x03};
    assert(s1.initialUEMessage(0x0001 /*rnti*/, 111222333444555ULL, nasPdu1));

    // uplinkNASTransport: UL NAS
    ByteBuffer nasPdu2{0x07, 0x5C, 0x04, 0x05};
    assert(s1.uplinkNASTransport(0x1000 /*mmeUeS1apId*/, 0x0001, nasPdu2));

    // downlinkNASTransport: DL NAS
    ByteBuffer nasPdu3{0x27, 0x01, 0x02, 0x03, 0x04};
    assert(s1.downlinkNASTransport(0x1000, 0x0001, nasPdu3));

    // initialContextSetupResponse
    ERAB erab1{5, 9, 2, {0x0001, 0x7F000001, 2152}};
    assert(s1.initialContextSetupResponse(0x1000, 0x0001, {erab1}));

    // erabSetupResponse
    assert(s1.erabSetupResponse(0x1000, 0x0001, {erab1}, {}));

    // erabReleaseResponse
    assert(s1.erabReleaseResponse(0x1000, 0x0001, {5}));

    // ueContextReleaseRequest
    assert(s1.ueContextReleaseRequest(0x1000, 0x0001, "normal-release"));

    // ueContextReleaseComplete
    assert(s1.ueContextReleaseComplete(0x1000, 0x0001));

    // handoverRequired
    ByteBuffer rrcCont{0x06, 0x00, 0x01};
    assert(s1.handoverRequired(0x1000, 0x0001, 2 /*targetEnbId*/, rrcCont));

    // handoverNotify
    assert(s1.handoverNotify(0x1000, 0x0001));

    // pathSwitchRequest
    assert(s1.pathSwitchRequest(0x1000, 0x0001, 2, {erab1}));

    // sendS1APMsg / recvS1APMsg
    S1APMessage msg{S1APProcedure::S1_SETUP, 0, 0, {0x01, 0x02}};
    assert(s1.sendS1APMsg(msg));
    // recvS1APMsg: может быть сообщение или нет
    S1APMessage rxMsg{};
    bool rxOk = s1.recvS1APMsg(rxMsg);
    (void)rxOk;

    s1.disconnect();
    assert(!s1.isConnected());

    // ── S1ULink ───────────────────────────────────────────────────────────────
    S1ULink s1u("eNB-001");

    GTPUTunnel sgwTunnel{0x12345678, 0x7F000001, 2152};
    assert(s1u.createTunnel(0x0002 /*rnti*/, 5 /*erabId*/, sgwTunnel));

    // sendGtpuPdu: инкапсулирует IP пакет в GTP-U, отправляет на SGW
    ByteBuffer ipPkt(60, 0x45);
    bool txOk = s1u.sendGtpuPdu(0x0002, 5, ipPkt);
    (void)txOk;  // зависит от наличия сети

    // recvGtpuPdu: DL пакеты ещё не пришли
    ByteBuffer rxPkt;
    assert(!s1u.recvGtpuPdu(0x0002, 5, rxPkt));

    // deleteTunnel
    assert(s1u.deleteTunnel(0x0002, 5));

    // deleteTunnel несуществующего → false
    assert(!s1u.deleteTunnel(0x0002, 5));

    std::puts("test_s1ap_link PASSED");
    return 0;
}

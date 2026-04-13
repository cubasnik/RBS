#include "../src/lte/s1ap_link.h"
#include "../src/lte/s1ap_codec.h"
#include "../src/common/logger.h"
#include "../src/common/sctp_socket.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>
#include <mutex>
#include <optional>

using namespace rbs;
using namespace rbs::lte;

static bool waitS1Msg(S1APLink& link, S1APMessage& msg, int timeoutMs = 1000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (link.recvS1APMsg(msg)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

static void test_s1ap_transport_trace_path() {
    S1APLink enb("eNB-trace");
    net::SctpSocket mmeSocket("S1AP-MME-trace");
    std::mutex rxMutex;
    std::optional<ByteBuffer> lastRx;

    assert(mmeSocket.bind(39412));
    assert(mmeSocket.connect("127.0.0.1", 39411));
    assert(mmeSocket.startReceive([&](const net::SctpPacket& pkt) {
        std::lock_guard<std::mutex> lock(rxMutex);
        lastRx = pkt.data;
    }));

    assert(enb.connect("127.0.0.1", 39412, 39411));
    assert(enb.isConnected());

    const std::string uplinkTraceId = "test-s1ap-ul-trace";
    ByteBuffer initialNasPdu{0x07, 0x41, 0x01, 0x02, 0x03};
    {
        rbs::ScopedTraceId traceScope(uplinkTraceId);
        assert(enb.initialUEMessage(0x0001, 111222333444555ULL, initialNasPdu));
    }

    const auto uplinkDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    bool gotUplink = false;
    while (std::chrono::steady_clock::now() < uplinkDeadline && !gotUplink) {
        {
            std::lock_guard<std::mutex> lock(rxMutex);
            gotUplink = lastRx.has_value();
        }
        if (!gotUplink) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    ByteBuffer uplinkPayload;
    {
        std::lock_guard<std::mutex> lock(rxMutex);
        assert(lastRx.has_value());
        uplinkPayload = *lastRx;
        lastRx.reset();
    }

    S1APMessage decodedUl{};
    assert(s1ap_decode_message(uplinkPayload, decodedUl));
    assert(decodedUl.procedure == S1APProcedure::INITIAL_UE_MESSAGE);

    ByteBuffer ulNasPdu{0x07, 0x5C, 0x04, 0x05};
    {
        rbs::ScopedTraceId traceScope(uplinkTraceId);
        assert(enb.uplinkNASTransport(0x1000, 0x0001, ulNasPdu));
    }

    const auto ulNasDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    bool gotUlNas = false;
    while (std::chrono::steady_clock::now() < ulNasDeadline && !gotUlNas) {
        {
            std::lock_guard<std::mutex> lock(rxMutex);
            gotUlNas = lastRx.has_value();
        }
        if (!gotUlNas) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    {
        std::lock_guard<std::mutex> lock(rxMutex);
        assert(lastRx.has_value());
        uplinkPayload = *lastRx;
        lastRx.reset();
    }

    assert(s1ap_decode_message(uplinkPayload, decodedUl));
    assert(decodedUl.procedure == S1APProcedure::UPLINK_NAS_TRANSPORT);
    auto uplinkPdu = s1ap_decode(uplinkPayload.data(), uplinkPayload.size());
    assert(uplinkPdu != nullptr);
    assert(s1ap_extract_nas_pdu(uplinkPdu) == ulNasPdu);
    s1ap_pdu_free(uplinkPdu);

    ByteBuffer imsi{0x02, 0x50, 0x01, 0x00, 0x00, 0x00, 0x00, 0x55};
    ByteBuffer pagingPdu = s1ap_encode_Paging(0x0155, imsi, 0x250001, 1, 0);
    assert(!pagingPdu.empty());
    assert(mmeSocket.send(pagingPdu.data(), pagingPdu.size()));

    S1APMessage rx{};
    assert(waitS1Msg(enb, rx));
    assert(rx.procedure == S1APProcedure::PAGING);
    assert(!rx.traceId.empty());
    assert(rx.traceId != uplinkTraceId);
    S1APMessage decodedPaging{};
    assert(s1ap_decode_message(rx.payload, decodedPaging));
    assert(decodedPaging.procedure == S1APProcedure::PAGING);

    enb.disconnect();
    mmeSocket.close();
    std::puts("  test_s1ap_transport_trace_path PASSED");
}

static void test_s1ap_multi_ue_isolation() {
    // ────────────────────────────────────────────────────────────────────────────
    // Multi-UE Release Isolation Test
    // Verifies that releasing one UE does not affect other UEs' context state
    // ────────────────────────────────────────────────────────────────────────────
    S1APLink enb("eNB-multi-ue");
    
    assert(enb.connect("127.0.0.1", 36412));
    assert(enb.isConnected());
    
    // S1 Setup
    assert(enb.s1Setup(0x0001F101, "TesteNB-MultiUE", 1, 0xF20001));
    
    // Admit two UEs with different RNTIs
    const uint16_t rnti1 = 0x0001;
    const uint16_t rnti2 = 0x0002;
    const uint64_t imsi1 = 111222333444555ULL;
    const uint64_t imsi2 = 111222333444666ULL;
    
    ByteBuffer nasPdu1{0x07, 0x41, 0x01, 0x02, 0x03};
    assert(enb.initialUEMessage(rnti1, imsi1, nasPdu1));
    
    ByteBuffer nasPdu2{0x07, 0x41, 0x02, 0x03, 0x04};
    assert(enb.initialUEMessage(rnti2, imsi2, nasPdu2));
    
    // Setup context for both UEs
    uint32_t mmeUeS1apId1 = 0x1000;
    uint32_t mmeUeS1apId2 = 0x1001;
    
    ERAB erab1{5, 9, 2, {0x0001, 0x7F000001, 2152}};
    assert(enb.initialContextSetupResponse(mmeUeS1apId1, rnti1, {erab1}));
    
    ERAB erab2{6, 9, 2, {0x0002, 0x7F000002, 2152}};
    assert(enb.initialContextSetupResponse(mmeUeS1apId2, rnti2, {erab2}));
    
    // Verify both have active context by sending UL NAS
    ByteBuffer ulNas1{0x07, 0x5C, 0x04, 0x05};
    assert(enb.uplinkNASTransport(mmeUeS1apId1, rnti1, ulNas1));
    
    ByteBuffer ulNas2{0x07, 0x5C, 0x06, 0x07};
    assert(enb.uplinkNASTransport(mmeUeS1apId2, rnti2, ulNas2));
    
    // Release only UE1 context
    assert(enb.ueContextReleaseRequest(mmeUeS1apId1, rnti1, "normal-release"));
    assert(enb.ueContextReleaseComplete(mmeUeS1apId1, rnti1));
    
    // Verify UE2 can still send UL NAS (context intact)
    ByteBuffer ulNas2Again{0x07, 0x5C, 0x08, 0x09};
    assert(enb.uplinkNASTransport(mmeUeS1apId2, rnti2, ulNas2Again));
    
    // Verify UE2 context is still operational for E-RAB operations
    std::vector<uint8_t> releasedErabs{5};  // Release ERAB 5 (belongs to UE1)
    assert(enb.erabReleaseResponse(mmeUeS1apId2, rnti2, releasedErabs));
    
    // Setup new E-RAB for UE2
    ERAB erab3{7, 9, 2, {0x0003, 0x7F000003, 2152}};
    assert(enb.erabSetupResponse(mmeUeS1apId2, rnti2, {erab3}, {}));
    
    // Release UE2
    assert(enb.ueContextReleaseRequest(mmeUeS1apId2, rnti2, "normal-release"));
    assert(enb.ueContextReleaseComplete(mmeUeS1apId2, rnti2));
    
    enb.disconnect();
    assert(!enb.isConnected());
    
    std::puts("  test_s1ap_multi_ue_isolation PASSED");
}

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

    test_s1ap_transport_trace_path();
    test_s1ap_multi_ue_isolation();

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

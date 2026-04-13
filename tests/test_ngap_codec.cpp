#include "../src/nr/ngap_codec.h"
#include "../src/nr/ngap_link.h"
#include "../src/nr/nr_stack.h"
#include "../src/hal/rf_hardware.h"

#include <cassert>
#include <cstdio>
#include <memory>

using namespace rbs;
using namespace rbs::nr;
using namespace rbs::hal;

static NRCellConfig makeCfg() {
    NRCellConfig cfg{};
    cfg.cellId = 903;
    cfg.nrArfcn = 632000;
    cfg.scs = NRScs::SCS30;
    cfg.band = 78;
    cfg.gnbDuId = 0x3001;
    cfg.gnbCuId = 0x4002;
    cfg.nrCellIdentity = 0xABCDE1;
    cfg.nrPci = 303;
    cfg.ssbPeriodMs = 20;
    cfg.tac = 7;
    cfg.mcc = 250;
    cfg.mnc = 1;
    cfg.cuAddr = "127.0.0.1";
    cfg.cuPort = 38472;
    cfg.numTxRx = 2;
    return cfg;
}

static void test_ng_setup_roundtrip() {
    NgSetupRequest req{};
    req.transactionId = 1;
    req.ranNodeId = 0x3001;
    req.gnbName = "RBS-gNB";
    req.tac = 7;
    req.mcc = 250;
    req.mnc = 1;

    const ByteBuffer pdu = encodeNgSetupRequest(req);
    NgSetupRequest decoded{};
    assert(decodeNgSetupRequest(pdu, decoded));
    assert(decoded.ranNodeId == req.ranNodeId);
    assert(decoded.gnbName == req.gnbName);
    assert(decoded.tac == req.tac);

    NgSetupResponse rsp{};
    rsp.transactionId = 1;
    rsp.amfId = 0xA001;
    rsp.amfName = "AMF-Core";
    rsp.relativeCapacity = 200;
    const ByteBuffer rspPdu = encodeNgSetupResponse(rsp);
    NgSetupResponse decodedRsp{};
    assert(decodeNgSetupResponse(rspPdu, decodedRsp));
    assert(decodedRsp.amfId == rsp.amfId);
    assert(decodedRsp.amfName == rsp.amfName);
    std::puts("  test_ng_setup_roundtrip PASSED");
}

static void test_pdu_session_roundtrip() {
    PduSessionSetupRequest req{};
    req.transactionId = 9;
    req.amfUeNgapId = 0x1100;
    req.ranUeNgapId = 0x22;
    req.pduSessionId = 5;
    req.sst = 1;
    req.sd = 0x010203;
    req.nasPdu = {0x01, 0x02, 0x03};

    const ByteBuffer pdu = encodePduSessionSetupRequest(req);
    PduSessionSetupRequest decoded{};
    assert(decodePduSessionSetupRequest(pdu, decoded));
    assert(decoded.amfUeNgapId == req.amfUeNgapId);
    assert(decoded.pduSessionId == req.pduSessionId);
    assert(decoded.nasPdu == req.nasPdu);

    PduSessionSetupResponse rsp{};
    rsp.transactionId = 9;
    rsp.amfUeNgapId = 0x1100;
    rsp.ranUeNgapId = 0x22;
    rsp.pduSessionId = 5;
    rsp.gtpTeid = 0xABCD1234;
    rsp.transfer = {0x11, 0x22};
    const ByteBuffer rspPdu = encodePduSessionSetupResponse(rsp);
    PduSessionSetupResponse decodedRsp{};
    assert(decodePduSessionSetupResponse(rspPdu, decodedRsp));
    assert(decodedRsp.gtpTeid == rsp.gtpTeid);
    assert(decodedRsp.transfer == rsp.transfer);
    std::puts("  test_pdu_session_roundtrip PASSED");
}

static void test_ue_context_release_roundtrip() {
    UeContextReleaseCommand cmd{};
    cmd.transactionId = 10;
    cmd.amfUeNgapId = 0x9988;
    cmd.ranUeNgapId = 0x77;
    cmd.causeType = 2;
    cmd.causeValue = 3;
    cmd.releaseAction = 1;
    cmd.contextInfo = {0x10, 0x20};
    const ByteBuffer pdu = encodeUeContextReleaseCommand(cmd);
    UeContextReleaseCommand decoded{};
    assert(decodeUeContextReleaseCommand(pdu, decoded));
    assert(decoded.amfUeNgapId == cmd.amfUeNgapId);
    assert(decoded.ranUeNgapId == cmd.ranUeNgapId);
    assert(decoded.releaseAction == cmd.releaseAction);
    assert(decoded.contextInfo == cmd.contextInfo);

    UeContextReleaseComplete complete{};
    complete.transactionId = 10;
    complete.amfUeNgapId = 0x9988;
    complete.ranUeNgapId = 0x77;
    complete.releaseReport = {0x01, 0x02, 0x03};
    const ByteBuffer completePdu = encodeUeContextReleaseComplete(complete);
    UeContextReleaseComplete decodedComplete{};
    assert(decodeUeContextReleaseComplete(completePdu, decodedComplete));
    assert(decodedComplete.ranUeNgapId == complete.ranUeNgapId);
    assert(decodedComplete.releaseReport == complete.releaseReport);
    std::puts("  test_ue_context_release_roundtrip PASSED");
}

static void test_paging_roundtrip() {
    PagingMessage paging{};
    paging.transactionId = 44;
    paging.uePagingIdentity = 250010000000444ULL;
    paging.fivegTmsi = 0xABCDEF11;
    paging.tac = 7;
    paging.mcc = 250;
    paging.mnc = 1;
    paging.pagingPriority = 5;
    paging.drxCycle = 64;

    const ByteBuffer pdu = encodePagingMessage(paging);
    PagingMessage decoded{};
    assert(decodePagingMessage(pdu, decoded));
    assert(decoded.uePagingIdentity == paging.uePagingIdentity);
    assert(decoded.fivegTmsi == paging.fivegTmsi);
    assert(decoded.pagingPriority == paging.pagingPriority);
    assert(decoded.drxCycle == paging.drxCycle);
    std::puts("  test_paging_roundtrip PASSED");
}

static void test_nr_stack_ng_setup_pdu_session_and_release() {
    auto rf = std::make_shared<RFHardware>();
    NRStack stack(rf, makeCfg());

    auto ngapGnb = std::make_shared<NgapLink>(makeCfg().gnbDuId);
    auto ngapAmf = std::make_shared<NgapLink>(0xA001);
    stack.attachNgapLink(ngapGnb);
    assert(stack.connectNgPeer(0xA001));
    assert(ngapAmf->connect(makeCfg().gnbDuId));

    assert(stack.start());

    NgapMessage msg{};
    assert(ngapAmf->recvNgapMessage(msg));
    assert(msg.procedure == NgapProcedure::NG_SETUP_REQUEST);
    NgSetupRequest setupReq{};
    assert(decodeNgSetupRequest(msg.payload, setupReq));
    assert(setupReq.ranNodeId == makeCfg().gnbDuId);

    NgSetupResponse setupRsp{};
    setupRsp.transactionId = setupReq.transactionId;
    setupRsp.amfId = 0xA001;
    setupRsp.amfName = "AMF-Test";
    setupRsp.relativeCapacity = 180;
    assert(ngapAmf->ngSetupResponse(makeCfg().gnbDuId, setupRsp));
    assert(stack.processNgMessages() == 1);
    assert(stack.ngSetupComplete(0xA001));

    const uint16_t crnti = stack.admitUE(250010000000333ULL, 10);
    assert(crnti != 0);

    PduSessionSetupRequest pduReq{};
    pduReq.transactionId = 2;
    pduReq.amfUeNgapId = 0x5001;
    pduReq.ranUeNgapId = crnti;
    pduReq.pduSessionId = 10;
    pduReq.sst = 1;
    pduReq.sd = 0x010203;
    pduReq.nasPdu = {0x7E, 0x01};
    assert(ngapAmf->pduSessionSetupRequest(makeCfg().gnbDuId, pduReq));
    assert(stack.processNgMessages() == 1);
    assert(stack.hasActivePduSession(crnti, 10));

    assert(ngapAmf->recvNgapMessage(msg));
    assert(msg.procedure == NgapProcedure::PDU_SESSION_SETUP_RESPONSE);
    PduSessionSetupResponse pduRsp{};
    assert(decodePduSessionSetupResponse(msg.payload, pduRsp));
    assert(pduRsp.ranUeNgapId == crnti);
    assert(pduRsp.pduSessionId == 10);
    assert(pduRsp.gtpTeid != 0);

    UeContextReleaseCommand releaseCmd{};
    releaseCmd.transactionId = 3;
    releaseCmd.amfUeNgapId = 0x5001;
    releaseCmd.ranUeNgapId = crnti;
    releaseCmd.causeType = 1;
    releaseCmd.causeValue = 1;
    assert(ngapAmf->ueContextReleaseCommand(makeCfg().gnbDuId, releaseCmd));
    assert(stack.processNgMessages() == 1);
    assert(!stack.hasActivePduSession(crnti, 10));
    assert(stack.connectedUECount() == 0);

    assert(ngapAmf->recvNgapMessage(msg));
    assert(msg.procedure == NgapProcedure::UE_CONTEXT_RELEASE_COMPLETE);
    UeContextReleaseComplete complete{};
    assert(decodeUeContextReleaseComplete(msg.payload, complete));
    assert(complete.ranUeNgapId == crnti);

    stack.stop();
    std::puts("  test_nr_stack_ng_setup_pdu_session_and_release PASSED");
}

int main() {
    std::puts("=== test_ngap_codec ===");
    test_ng_setup_roundtrip();
    test_pdu_session_roundtrip();
    test_ue_context_release_roundtrip();
    test_paging_roundtrip();
    test_nr_stack_ng_setup_pdu_session_and_release();
    std::puts("test_ngap_codec PASSED");
    return 0;
}
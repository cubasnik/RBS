#include "../src/nr/xnap_codec.h"
#include "../src/nr/xnap_link.h"
#include "../src/nr/nr_stack.h"
#include "../src/hal/rf_hardware.h"

#include <cassert>
#include <cstdio>
#include <memory>

using namespace rbs;
using namespace rbs::nr;
using namespace rbs::hal;

static NRCellConfig makeCfg(uint32_t cellId, uint64_t gnbDuId, uint64_t nrCellIdentity, uint16_t pci) {
    NRCellConfig cfg{};
    cfg.cellId = cellId;
    cfg.nrArfcn = 620000 + static_cast<uint32_t>(cellId);
    cfg.scs = NRScs::SCS30;
    cfg.band = 78;
    cfg.gnbDuId = gnbDuId;
    cfg.gnbCuId = 0x9000 + gnbDuId;
    cfg.nrCellIdentity = nrCellIdentity;
    cfg.nrPci = pci;
    cfg.ssbPeriodMs = 20;
    cfg.tac = 1;
    cfg.mcc = 250;
    cfg.mnc = 1;
    cfg.cuAddr = "127.0.0.1";
    cfg.cuPort = 38472;
    cfg.numTxRx = 2;
    return cfg;
}

static void test_xn_setup_roundtrip() {
    XnSetupRequest req{};
    req.transactionId = 11;
    req.localGnbId = 0x101;
    req.gnbName = "gNB-A";
    req.servedCells.push_back(XnServedCell{0xABC001, 620000, 101, 1});

    const ByteBuffer pdu = encodeXnSetupRequest(req);
    assert(!pdu.empty());

    XnSetupRequest decoded{};
    assert(decodeXnSetupRequest(pdu, decoded));
    assert(decoded.transactionId == req.transactionId);
    assert(decoded.localGnbId == req.localGnbId);
    assert(decoded.gnbName == req.gnbName);
    assert(decoded.servedCells.size() == 1);
    assert(decoded.servedCells[0].nrCellIdentity == req.servedCells[0].nrCellIdentity);

    XnSetupResponse rsp{};
    rsp.transactionId = req.transactionId;
    rsp.respondingGnbId = 0x202;
    rsp.activatedCells.push_back(0xABC002);
    const ByteBuffer rspPdu = encodeXnSetupResponse(rsp);
    XnSetupResponse decodedRsp{};
    assert(decodeXnSetupResponse(rspPdu, decodedRsp));
    assert(decodedRsp.respondingGnbId == rsp.respondingGnbId);
    assert(decodedRsp.activatedCells.size() == 1);
    std::puts("  test_xn_setup_roundtrip PASSED");
}

static void test_xn_handover_request_notify_roundtrip() {
    XnHandoverRequest req{};
    req.transactionId = 7;
    req.sourceGnbId = 0x101;
    req.targetGnbId = 0x202;
    req.sourceCellId = 0xABC001;
    req.targetCellId = 0xABC002;
    req.sourceCrnti = 0x44;
    req.ueImsi = 250010000000777ULL;
    req.causeType = 1;
    req.sourceUeAmbr = 2048;
    req.pduSessionIds = {5, 9};
    req.securityContext = {0x5A, 0x01, 0x00};
    req.rrcContainer = {0xAA, 0xBB};

    const ByteBuffer pdu = encodeXnHandoverRequest(req);
    XnHandoverRequest decoded{};
    assert(decodeXnHandoverRequest(pdu, decoded));
    assert(decoded.targetGnbId == req.targetGnbId);
    assert(decoded.ueImsi == req.ueImsi);
    assert(decoded.sourceUeAmbr == req.sourceUeAmbr);
    assert(decoded.pduSessionIds == req.pduSessionIds);
    assert(decoded.securityContext == req.securityContext);
    assert(decoded.rrcContainer == req.rrcContainer);

    XnHandoverNotify notify{};
    notify.transactionId = req.transactionId;
    notify.sourceGnbId = req.sourceGnbId;
    notify.targetGnbId = req.targetGnbId;
    notify.sourceCellId = req.sourceCellId;
    notify.targetCellId = req.targetCellId;
    notify.sourceCrnti = req.sourceCrnti;
    notify.targetCrnti = 0x88;
    notify.rrcContainer = {0xCC, 0xDD};

    const ByteBuffer notifyPdu = encodeXnHandoverNotify(notify);
    XnHandoverNotify decodedNotify{};
    assert(decodeXnHandoverNotify(notifyPdu, decodedNotify));
    assert(decodedNotify.targetCrnti == notify.targetCrnti);
    assert(decodedNotify.rrcContainer == notify.rrcContainer);
    std::puts("  test_xn_handover_request_notify_roundtrip PASSED");
}

static void test_xnap_stack_end_to_end() {
    auto rfA = std::make_shared<RFHardware>();
    auto rfB = std::make_shared<RFHardware>();

    NRStack source(rfA, makeCfg(901, 0x101, 0xABC001, 101));
    NRStack target(rfB, makeCfg(902, 0x202, 0xABC002, 202));

    auto linkA = std::make_shared<XnAPLink>(0x101, "gNB-A");
    auto linkB = std::make_shared<XnAPLink>(0x202, "gNB-B");
    source.attachXnLink(linkA);
    target.attachXnLink(linkB);

    assert(source.start());
    assert(target.start());

    assert(source.connectXnPeer(0x202));
    assert(target.connectXnPeer(0x101));

    assert(source.xnSetup(0x202));
    assert(target.processXnMessages() == 1);
    assert(source.processXnMessages() == 1);
    assert(source.xnSetupComplete(0x202));
    assert(target.xnSetupComplete(0x101));

    const uint16_t sourceCrnti = source.admitUE(250010000000901ULL, 11);
    assert(sourceCrnti != 0);

    assert(source.handoverRequired(sourceCrnti, 0x202, 0xABC002, 0));
    assert(target.processXnMessages() == 1);
    assert(target.hasReceivedXnHandover(sourceCrnti));

    assert(source.processXnMessages() == 1);
    assert(source.hasCompletedXnHandover(sourceCrnti));
    assert(source.handoverTargetCrnti(sourceCrnti) != 0);
    assert(target.handoverTargetCrnti(sourceCrnti) != 0);
    assert(target.connectedUECount() == 1);

    source.stop();
    target.stop();
    std::puts("  test_xnap_stack_end_to_end PASSED");
}

int main() {
    std::puts("=== test_xnap ===");
    test_xn_setup_roundtrip();
    test_xn_handover_request_notify_roundtrip();
    test_xnap_stack_end_to_end();
    std::puts("test_xnap PASSED");
    return 0;
}
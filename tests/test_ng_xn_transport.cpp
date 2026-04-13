#include "../src/nr/ngap_link.h"
#include "../src/nr/xnap_link.h"
#include "../src/nr/ngap_codec.h"
#include "../src/nr/xnap_codec.h"
#include "../src/oms/oms.h"
#include "../src/common/logger.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace rbs::nr;

static bool waitNgMsg(NgapLink& link, NgapMessage& msg, int timeoutMs = 1000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (link.recvNgapMessage(msg)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

static bool waitXnMsg(XnAPLink& link, XnAPMessage& msg, int timeoutMs = 1000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (link.recvXnApMessage(msg)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

static bool containsPromMetricWithTrace(const std::string& rendered,
                                        const std::string& metricName,
                                        const std::string& traceId) {
    const std::string needle = metricName + "{trace_id=\"" + traceId + "\"}";
    return rendered.find(needle) != std::string::npos;
}

static void test_ngap_transport_path() {
    NgapLink gnb(0x3001);
    NgapLink amf(0xA001);

    assert(gnb.bindTransport(39011));
    assert(amf.bindTransport(39012));

    assert(gnb.connectSctpPeer(0xA001, "127.0.0.1", 39012));
    assert(amf.connectSctpPeer(0x3001, "127.0.0.1", 39011));
    assert(gnb.isConnected(0xA001));
    assert(amf.isConnected(0x3001));

    NgSetupRequest req{};
    req.transactionId = 1;
    req.ranNodeId = 0x3001;
    req.gnbName = "RBS-gNB-transport";
    req.tac = 7;
    req.mcc = 250;
    req.mnc = 1;
    const std::string ngSetupTraceId = "test-ng-setup-trace";

    NgapMessage rx{};
    bool delivered = false;
    for (int attempt = 0; attempt < 50 && !delivered; ++attempt) {
        {
            rbs::ScopedTraceId traceScope(ngSetupTraceId);
            if (gnb.ngSetup(0xA001, req)) {
                delivered = waitNgMsg(amf, rx, 100);
            }
        }
        if (!delivered) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    assert(delivered);
    assert(rx.procedure == NgapProcedure::NG_SETUP_REQUEST);
    assert(rx.targetNodeId == 0xA001);
    assert(!rx.payload.empty());
    assert(rx.traceId == ngSetupTraceId);

    NgSetupRequest decodedSetup{};
    assert(decodeNgSetupRequest(rx.payload, decodedSetup));
    assert(decodedSetup.ranNodeId == req.ranNodeId);
    assert(decodedSetup.gnbName == req.gnbName);

    // Transport receive path must propagate traceId into OMS/Prometheus labels.
    auto& oms = rbs::oms::OMS::instance();
    {
        rbs::ScopedTraceId traceScope(rx.traceId);
        oms.updateCounter("test.ngap.transport.trace.counter", 1.0);
    }
    const std::string ngProm = oms.renderPrometheus();
    assert(containsPromMetricWithTrace(ngProm,
        "rbs_test_ngap_transport_trace_counter", ngSetupTraceId));

    PagingMessage paging{};
    paging.transactionId = 3;
    paging.uePagingIdentity = 250010000000901ULL;
    paging.fivegTmsi = 0xABCDEF22;
    paging.tac = 7;
    paging.mcc = 250;
    paging.mnc = 1;
    paging.pagingPriority = 6;
    paging.drxCycle = 64;
    const std::string pagingTraceId = "test-ng-paging-trace";

    delivered = false;
    for (int attempt = 0; attempt < 50 && !delivered; ++attempt) {
        {
            rbs::ScopedTraceId traceScope(pagingTraceId);
            if (amf.paging(0x3001, paging)) {
                delivered = waitNgMsg(gnb, rx, 100);
            }
        }
        if (!delivered) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    assert(delivered);
    assert(rx.procedure == NgapProcedure::PAGING);
    assert(rx.traceId == pagingTraceId);
    PagingMessage decodedPaging{};
    assert(decodePagingMessage(rx.payload, decodedPaging));
    assert(decodedPaging.uePagingIdentity == paging.uePagingIdentity);
    assert(decodedPaging.fivegTmsi == paging.fivegTmsi);
    assert(decodedPaging.pagingPriority == paging.pagingPriority);
    assert(decodedPaging.drxCycle == paging.drxCycle);

    UeContextReleaseCommand rel{};
    rel.transactionId = 4;
    rel.amfUeNgapId = 0x5001;
    rel.ranUeNgapId = 0x44;
    rel.causeType = 2;
    rel.causeValue = 9;
    rel.releaseAction = 1;
    rel.contextInfo = {0x10, 0x20, 0x30};
    const std::string releaseTraceId = "test-ng-release-trace";

    delivered = false;
    for (int attempt = 0; attempt < 50 && !delivered; ++attempt) {
        {
            rbs::ScopedTraceId traceScope(releaseTraceId);
            if (amf.ueContextReleaseCommand(0x3001, rel)) {
                delivered = waitNgMsg(gnb, rx, 100);
            }
        }
        if (!delivered) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    assert(delivered);
    assert(rx.procedure == NgapProcedure::UE_CONTEXT_RELEASE_COMMAND);
    assert(rx.traceId == releaseTraceId);
    UeContextReleaseCommand decodedRelease{};
    assert(decodeUeContextReleaseCommand(rx.payload, decodedRelease));
    assert(decodedRelease.amfUeNgapId == rel.amfUeNgapId);
    assert(decodedRelease.ranUeNgapId == rel.ranUeNgapId);
    assert(decodedRelease.releaseAction == rel.releaseAction);
    assert(decodedRelease.contextInfo == rel.contextInfo);

    std::puts("  test_ngap_transport_path PASSED");
}

static void test_xnap_transport_path() {
    XnAPLink gnbA(0x1101, "gNB-A");
    XnAPLink gnbB(0x2202, "gNB-B");

    assert(gnbA.bindTransport(39111));
    assert(gnbB.bindTransport(39112));

    assert(gnbA.connectSctpPeer(0x2202, "127.0.0.1", 39112));
    assert(gnbB.connectSctpPeer(0x1101, "127.0.0.1", 39111));
    assert(gnbA.isConnected(0x2202));
    assert(gnbB.isConnected(0x1101));

    XnSetupRequest req{};
    req.transactionId = 2;
    req.localGnbId = 0x1101;
    req.gnbName = "gNB-A";
    req.servedCells.push_back(XnServedCell{0xABC001, 620100, 111, 1});
    const std::string xnSetupTraceId = "test-xn-setup-trace";

    XnAPMessage rx{};
    bool delivered = false;
    for (int attempt = 0; attempt < 50 && !delivered; ++attempt) {
        {
            rbs::ScopedTraceId traceScope(xnSetupTraceId);
            if (gnbA.xnSetup(0x2202, req)) {
                delivered = waitXnMsg(gnbB, rx, 100);
            }
        }
        if (!delivered) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    assert(delivered);
    assert(rx.procedure == XnAPProcedure::XN_SETUP_REQUEST);
    assert(rx.targetGnbId == 0x2202);
    assert(!rx.payload.empty());
    assert(rx.traceId == xnSetupTraceId);

    XnSetupRequest decodedSetup{};
    assert(decodeXnSetupRequest(rx.payload, decodedSetup));
    assert(decodedSetup.localGnbId == req.localGnbId);
    assert(decodedSetup.gnbName == req.gnbName);

    XnHandoverRequest hoReq{};
    hoReq.transactionId = 9;
    hoReq.sourceGnbId = 0x1101;
    hoReq.targetGnbId = 0x2202;
    hoReq.sourceCellId = 0xABC001;
    hoReq.targetCellId = 0xABC002;
    hoReq.sourceCrnti = 0x77;
    hoReq.ueImsi = 250010000000777ULL;
    hoReq.causeType = 1;
    hoReq.sourceUeAmbr = 2048;
    hoReq.pduSessionIds = {5, 10};
    hoReq.securityContext = {0x5A, 0x01, 0x00};
    hoReq.rrcContainer = {0xAA, 0xBB, 0xCC};
    const std::string hoTraceId = "test-xn-ho-trace";

    delivered = false;
    for (int attempt = 0; attempt < 50 && !delivered; ++attempt) {
        {
            rbs::ScopedTraceId traceScope(hoTraceId);
            if (gnbA.handoverRequest(hoReq)) {
                delivered = waitXnMsg(gnbB, rx, 100);
            }
        }
        if (!delivered) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    assert(delivered);
    assert(rx.procedure == XnAPProcedure::HANDOVER_REQUEST);
    assert(rx.traceId == hoTraceId);
    XnHandoverRequest decodedHo{};
    assert(decodeXnHandoverRequest(rx.payload, decodedHo));
    assert(decodedHo.sourceGnbId == hoReq.sourceGnbId);
    assert(decodedHo.targetGnbId == hoReq.targetGnbId);
    assert(decodedHo.sourceCrnti == hoReq.sourceCrnti);
    assert(decodedHo.ueImsi == hoReq.ueImsi);
    assert(decodedHo.sourceUeAmbr == hoReq.sourceUeAmbr);
    assert(decodedHo.pduSessionIds == hoReq.pduSessionIds);
    assert(decodedHo.securityContext == hoReq.securityContext);
    assert(decodedHo.rrcContainer == hoReq.rrcContainer);

    // Same check for XnAP transport: traceId from wire must be visible in Prometheus label.
    auto& oms = rbs::oms::OMS::instance();
    {
        rbs::ScopedTraceId traceScope(rx.traceId);
        oms.updateCounter("test.xnap.transport.trace.counter", 2.0);
    }
    const std::string xnProm = oms.renderPrometheus();
    assert(containsPromMetricWithTrace(xnProm,
        "rbs_test_xnap_transport_trace_counter", hoTraceId));

    std::puts("  test_xnap_transport_path PASSED");
}

int main() {
    std::puts("=== test_ng_xn_transport ===");
    test_ngap_transport_path();
    test_xnap_transport_path();
    std::puts("test_ng_xn_transport PASSED");
    return 0;
}

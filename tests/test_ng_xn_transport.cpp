#include "../src/nr/ngap_link.h"
#include "../src/nr/xnap_link.h"

#include <cassert>
#include <cstdio>

using namespace rbs::nr;

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
    assert(gnb.ngSetup(0xA001, req));

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
    assert(gnbA.xnSetup(0x2202, req));

    std::puts("  test_xnap_transport_path PASSED");
}

int main() {
    std::puts("=== test_ng_xn_transport ===");
    test_ngap_transport_path();
    test_xnap_transport_path();
    std::puts("test_ng_xn_transport PASSED");
    return 0;
}

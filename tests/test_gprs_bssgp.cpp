#include "../src/gsm/gprs_ns.h"
#include "../src/gsm/gprs_bssgp.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::gsm;

// ─────────────────────────────────────────────────────────────────────────────
// NS layer tests
// ─────────────────────────────────────────────────────────────────────────────

// Full NS reset → unblock handshake ending in both sides ALIVE.
static void test_ns_reset_handshake() {
    GprsNs nsA(1001, 1);
    GprsNs nsB(1001, 1);

    // A sends NS-RESET.
    ByteBuffer resetPdu = nsA.encodeReset();
    assert(!resetPdu.empty());
    assert(resetPdu[0] == static_cast<uint8_t>(NsPduType::NS_RESET));

    // B handles NS-RESET → BLOCKED, reply = NS-RESET-ACK.
    ByteBuffer r1;
    nsB.handlePdu(resetPdu, r1);
    assert(nsB.state() == NsVcState::BLOCKED);
    assert(!r1.empty());
    assert(r1[0] == static_cast<uint8_t>(NsPduType::NS_RESET_ACK));

    // A handles NS-RESET-ACK → BLOCKED, reply = NS-UNBLOCK.
    ByteBuffer r2;
    nsA.handlePdu(r1, r2);
    assert(nsA.state() == NsVcState::BLOCKED);
    assert(!r2.empty());
    assert(r2[0] == static_cast<uint8_t>(NsPduType::NS_UNBLOCK));

    // B handles NS-UNBLOCK → ALIVE, reply = NS-UNBLOCK-ACK.
    ByteBuffer r3;
    nsB.handlePdu(r2, r3);
    assert(nsB.state() == NsVcState::ALIVE);
    assert(!r3.empty());
    assert(r3[0] == static_cast<uint8_t>(NsPduType::NS_UNBLOCK_ACK));

    // A handles NS-UNBLOCK-ACK → ALIVE.
    ByteBuffer r4;
    nsA.handlePdu(r3, r4);
    assert(nsA.state() == NsVcState::ALIVE);

    std::puts("  test_ns_reset_handshake PASSED");
}

// NS-ALIVE / NS-ALIVE-ACK roundtrip; stats counters incremented.
static void test_ns_alive_handshake() {
    GprsNs nsA(2001, 2);
    GprsNs nsB(2001, 2);
    nsA.forceAlive();
    nsB.forceAlive();

    ByteBuffer alive = nsA.encodeAlive();
    assert(alive.size() == 1);
    assert(alive[0] == static_cast<uint8_t>(NsPduType::NS_ALIVE));

    // B responds with NS-ALIVE-ACK.
    ByteBuffer r1;
    nsB.handlePdu(alive, r1);
    assert(!r1.empty());
    assert(r1[0] == static_cast<uint8_t>(NsPduType::NS_ALIVE_ACK));
    assert(nsB.stats().rxAlive == 1);

    // A consumes the ACK; rxAlive incremented on A.
    ByteBuffer r2;
    nsA.handlePdu(r1, r2);
    assert(nsA.stats().rxAlive == 1);
    assert(r2.empty());  // no further reply expected

    std::puts("  test_ns_alive_handshake PASSED");
}

// NS-UNITDATA wrapping roundtrip: BSSGP SDU is transparently carried.
static void test_ns_unitdata_wrapping() {
    GprsNs nsA(3001, 3);
    GprsNs nsB(3001, 3);
    nsA.forceAlive();
    nsB.forceAlive();

    const ByteBuffer payload = {0xDE, 0xAD, 0xBE, 0xEF};
    ByteBuffer nsPdu = nsA.encodeUnitdata(1234, payload);
    assert(!nsPdu.empty());
    assert(nsPdu[0] == static_cast<uint8_t>(NsPduType::NS_UNITDATA));

    ByteBuffer r;
    ByteBuffer extracted = nsB.handlePdu(nsPdu, r);
    assert(extracted == payload);
    assert(r.empty());  // no response for UNITDATA

    std::puts("  test_ns_unitdata_wrapping PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// BSSGP layer tests
// ─────────────────────────────────────────────────────────────────────────────

// BVC-RESET triggers automatic BVC-RESET-ACK response.
static void test_bssgp_bvc_reset() {
    GprsBssgp bssgp(100);

    ByteBuffer reset = bssgp.encodeBvcReset(BssgpCause::OM_INTERVENTION);
    assert(!reset.empty());
    assert(reset[0] == static_cast<uint8_t>(BssgpPduType::BVC_RESET));

    GprsBssgpTrace trace{};
    ByteBuffer response;
    bssgp.handlePdu(reset, trace, response);

    assert(!response.empty());
    assert(response[0] == static_cast<uint8_t>(BssgpPduType::BVC_RESET_ACK));

    std::puts("  test_bssgp_bvc_reset PASSED");
}

// UL-UNITDATA encode/decode roundtrip with Cell ID and trace logging.
static void test_bssgp_ul_unitdata_roundtrip() {
    GprsBssgp bssgp(200);

    const BssgpCellId cell{262, 1, 0x0A2B, 3, 0x0100};
    const BssgpQoS    qos{2, 1, 2, 4};
    const ByteBuffer  llcPdu = {0x43, 0x00, 0x01, 0x02, 0x03};

    ByteBuffer ul = bssgp.encodeUlUnitdata(0xDEADBEEFu, qos, cell, llcPdu);
    assert(!ul.empty());
    assert(ul[0] == static_cast<uint8_t>(BssgpPduType::UL_UNITDATA));

    GprsBssgpTrace trace{};
    ByteBuffer response;
    ByteBuffer decoded = bssgp.handlePdu(ul, trace, response);

    assert(decoded == llcPdu);
    assert(trace.dir      == GprsBssgpTrace::Dir::UL);
    assert(trace.tlli     == 0xDEADBEEFu);
    assert(trace.bvci     == 200);
    assert(trace.llcBytes == llcPdu.size());
    assert(trace.cellId.lac == cell.lac);
    assert(trace.cellId.mcc == cell.mcc);
    assert(bssgp.traceLog().size() == 1);

    std::puts("  test_bssgp_ul_unitdata_roundtrip PASSED");
}

// DL-UNITDATA encode/decode roundtrip with PDU lifetime.
static void test_bssgp_dl_unitdata_roundtrip() {
    GprsBssgp bssgp(300);

    const BssgpQoS   qos{1, 0, 1, 5};
    const ByteBuffer llcPdu = {0x41, 0xFF, 0x00, 0x10};

    ByteBuffer dl = bssgp.encodeDlUnitdata(0x12345678u, qos, 150, llcPdu);
    assert(!dl.empty());
    assert(dl[0] == static_cast<uint8_t>(BssgpPduType::DL_UNITDATA));

    GprsBssgpTrace trace{};
    ByteBuffer response;
    ByteBuffer decoded = bssgp.handlePdu(dl, trace, response);

    assert(decoded == llcPdu);
    assert(trace.dir      == GprsBssgpTrace::Dir::DL);
    assert(trace.tlli     == 0x12345678u);
    assert(trace.bvci     == 300);
    assert(trace.llcBytes == llcPdu.size());

    std::puts("  test_bssgp_dl_unitdata_roundtrip PASSED");
}

// RADIO-STATUS encodes without error and is parsed without crash.
static void test_bssgp_radio_status() {
    GprsBssgp bssgp(400);

    ByteBuffer rs = bssgp.encodeRadioStatus(0xAABBCCDDu,
                                            BssgpCause::TRANSIT_NETWORK_FAIL);
    assert(!rs.empty());
    assert(rs[0] == static_cast<uint8_t>(BssgpPduType::RADIO_STATUS));

    GprsBssgpTrace trace{};
    ByteBuffer response;
    ByteBuffer result = bssgp.handlePdu(rs, trace, response);
    assert(result.empty());    // RADIO-STATUS carries no LLC payload
    assert(response.empty());  // no automatic reply

    std::puts("  test_bssgp_radio_status PASSED");
}

// Cell ID BCD roundtrip: encode then decode yields identical struct.
static void test_bssgp_cell_id_bcd_roundtrip() {
    GprsBssgp bssgp(500);

    const BssgpCellId cellIn{302, 720, 0xABCD, 7, 0x1234};
    const BssgpQoS    qos{};
    const ByteBuffer  llcPdu = {0x01, 0x02};

    ByteBuffer ul = bssgp.encodeUlUnitdata(0x11223344u, qos, cellIn, llcPdu);
    GprsBssgpTrace trace{};
    ByteBuffer response;
    bssgp.handlePdu(ul, trace, response);

    assert(trace.cellId.mcc == cellIn.mcc);
    assert(trace.cellId.mnc == cellIn.mnc);
    assert(trace.cellId.lac == cellIn.lac);
    assert(trace.cellId.rac == cellIn.rac);
    assert(trace.cellId.ci  == cellIn.ci);

    std::puts("  test_bssgp_cell_id_bcd_roundtrip PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end: NS layer carrying a BSSGP UL-UNITDATA PDU
// ─────────────────────────────────────────────────────────────────────────────
static void test_ns_bssgp_end_to_end() {
    // Tx side
    GprsNs    nsA(4001, 4);
    GprsBssgp bssgpA(600);
    nsA.forceAlive();

    const BssgpCellId cell{234, 30, 0x1234, 0, 0x0010};
    const BssgpQoS    qos{0, 0, 0, 6};
    const ByteBuffer  llcPdu = {0xC3, 0x01, 0x02, 0x03, 0x04, 0x05};

    ByteBuffer bssgpPdu = bssgpA.encodeUlUnitdata(0xAABBCCDDu, qos, cell, llcPdu);
    ByteBuffer nsPdu    = nsA.encodeUnitdata(600, bssgpPdu);

    // Rx side
    GprsNs    nsB(4001, 4);
    GprsBssgp bssgpB(600);
    nsB.forceAlive();

    ByteBuffer r1;
    ByteBuffer extractedBssgp = nsB.handlePdu(nsPdu, r1);
    assert(!extractedBssgp.empty());

    GprsBssgpTrace trace{};
    ByteBuffer r2;
    ByteBuffer extractedLlc = bssgpB.handlePdu(extractedBssgp, trace, r2);

    assert(extractedLlc == llcPdu);
    assert(trace.tlli == 0xAABBCCDDu);
    assert(trace.dir  == GprsBssgpTrace::Dir::UL);

    std::puts("  test_ns_bssgp_end_to_end PASSED");
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::puts("=== test_gprs_bssgp ===");
    test_ns_reset_handshake();
    test_ns_alive_handshake();
    test_ns_unitdata_wrapping();
    test_bssgp_bvc_reset();
    test_bssgp_ul_unitdata_roundtrip();
    test_bssgp_dl_unitdata_roundtrip();
    test_bssgp_radio_status();
    test_bssgp_cell_id_bcd_roundtrip();
    test_ns_bssgp_end_to_end();
    std::puts("test_gprs_bssgp PASSED");
    return 0;
}

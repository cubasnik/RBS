#include "../src/lte/lte_pdcp.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    rbs::lte::PDCP pdcp;

    rbs::lte::PDCPConfig cfg{};
    cfg.bearerId           = 1;
    cfg.cipherAlg          = rbs::lte::PDCPCipherAlg::NULL_ALG;
    cfg.headerCompression  = false;

    const rbs::RNTI rnti = 42;
    assert(pdcp.addBearer(rnti, cfg));

    // DL: IP packet → PDCP PDU
    rbs::ByteBuffer ipPkt(64, 0xAB);
    rbs::ByteBuffer pdu = pdcp.processDlPacket(rnti, 1, ipPkt);

    // PDU must be larger by 2 (PDCP header)
    assert(pdu.size() == ipPkt.size() + 2);
    // D/C bit must be set in first byte
    assert((pdu[0] & 0x80) == 0x80);

    // UL: PDCP PDU → IP packet (loopback via a new bearer)
    rbs::lte::PDCPConfig cfg2 = cfg;
    cfg2.bearerId = 2;
    const rbs::RNTI rnti2 = 43;
    assert(pdcp.addBearer(rnti2, cfg2));

    rbs::ByteBuffer ulPdu = pdcp.processDlPacket(rnti2, 2, ipPkt);
    rbs::ByteBuffer recovered = pdcp.processUlPDU(rnti2, 2, ulPdu);
    assert(recovered.size() == ipPkt.size());
    assert(recovered == ipPkt);

    // Remove bearer
    assert(pdcp.removeBearer(rnti, 1));
    // Second removal must fail
    assert(!pdcp.removeBearer(rnti, 1));

    std::puts("test_pdcp PASSED");
    return 0;
}

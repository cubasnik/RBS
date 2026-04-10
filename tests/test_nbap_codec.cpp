#include "../src/umts/nbap_codec.h"
#include <cassert>
#include <cstdio>

// NBAP is a hand-rolled APER encoder (no asn1c for NBAP — spec is 1MB+).
// Tests verify: output is non-empty, minimum byte length is sensible,
// and the first byte has the initiatingMessage CHOICE bit (bit 7 = 0).

static void check_initiating(const rbs::ByteBuffer& pdu, const char* name) {
    assert(!pdu.empty());
    // NBAP-PDU CHOICE: bit 0 of first byte = 0 → initiatingMessage
    // (APER CHOICE with ext marker: first emitted bit is ext=0, then index bits)
    // So bit 7 of byte 0 must be 0.
    assert((pdu[0] & 0x80) == 0);
    (void)name;
}

int main() {
    using namespace rbs::umts;

    // ── Cell Setup Request FDD  (TS 25.433 §8.3.6.1) ─────────────────────────
    rbs::ByteBuffer cellSetup = nbap_encode_CellSetupRequestFDD(
        1,      // localCellId
        100,    // cId
        1,      // cfgGenId
        9612,   // uarfcnUl (UARFCN)
        10562,  // uarfcnDl
        400,    // maxTxPower (0.1 dBm units)
        0       // primaryScrCode
    );
    assert(!cellSetup.empty());
    assert(cellSetup.size() >= 5);   // at least header + mandatory IEs
    check_initiating(cellSetup, "CellSetupRequestFDD");

    // ── Radio Link Setup Request FDD  (TS 25.433 §8.1.1) ─────────────────────
    rbs::ByteBuffer rlSetup = nbap_encode_RadioLinkSetupRequestFDD(
        0x00001   // crncCtxId
    );
    assert(!rlSetup.empty());
    assert(rlSetup.size() >= 4);
    check_initiating(rlSetup, "RadioLinkSetupRequestFDD");

    // ── Radio Link Addition Request FDD  (TS 25.433 §8.1.4) ─────────────────
    rbs::ByteBuffer rlAdd = nbap_encode_RadioLinkAdditionRequestFDD(
        0x00002   // nodeBCtxId
    );
    assert(!rlAdd.empty());
    assert(rlAdd.size() >= 4);
    check_initiating(rlAdd, "RadioLinkAdditionRequestFDD");

    // ── Radio Link Deletion Request  (TS 25.433 §8.1.6) ─────────────────────
    rbs::ByteBuffer rlDel = nbap_encode_RadioLinkDeletionRequest(
        0x00002,  // nodeBCtxId
        0x00001   // crncCtxId
    );
    assert(!rlDel.empty());
    assert(rlDel.size() >= 4);
    check_initiating(rlDel, "RadioLinkDeletionRequest");

    // ── Reset Request  (TS 25.433 §8.7.1) ────────────────────────────────────
    rbs::ByteBuffer reset = nbap_encode_ResetRequest();
    assert(!reset.empty());
    assert(reset.size() >= 4);
    check_initiating(reset, "ResetRequest");

    // ── Audit Request  (TS 25.433 §8.6) ──────────────────────────────────────
    rbs::ByteBuffer audit = nbap_encode_AuditRequest(true);
    assert(!audit.empty());
    assert(audit.size() >= 4);
    check_initiating(audit, "AuditRequest (start-of-seq)");

    rbs::ByteBuffer auditNS = nbap_encode_AuditRequest(false);
    assert(!auditNS.empty());
    check_initiating(auditNS, "AuditRequest (not-start)");

    // ── Transaction IDs are independent ──────────────────────────────────────
    rbs::ByteBuffer r0 = nbap_encode_ResetRequest(0);
    rbs::ByteBuffer r1 = nbap_encode_ResetRequest(1);
    // Different txId → at least one byte differs (txId field)
    assert(r0 != r1);

    std::puts("test_nbap_codec PASSED");
    return 0;
}

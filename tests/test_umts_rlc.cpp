#include "../src/umts/umts_rlc.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::umts;

int main() {
    UMTSRlc rlc;

    const RNTI r1 = 10;
    const RNTI r2 = 20;

    // ── addRB ─────────────────────────────────────────────────────────────────
    assert(rlc.addRB(r1, 1, RLCMode::AM));
    assert(rlc.addRB(r1, 2, RLCMode::UM));
    assert(rlc.addRB(r1, 3, RLCMode::TM));
    assert(rlc.addRB(r2, 1, RLCMode::AM));

    // ── txSN / rxSN начальное значение — 0 ───────────────────────────────────
    assert(rlc.txSN(r1, 1) == 0);
    assert(rlc.rxSN(r1, 1) == 0);

    // ── AM mode: sendSdu → pollPdu выдаёт PDU с заголовком ───────────────────
    ByteBuffer sdu1(20, 0xAB);
    assert(rlc.sendSdu(r1, 1, sdu1));  // в очередь AM txQueue

    ByteBuffer pdu1;
    assert(rlc.pollPdu(r1, 1, pdu1)); // должен достать AM PDU
    assert(!pdu1.empty());
    // SN должен вырасти после выдачи PDU
    assert(rlc.txSN(r1, 1) == 1);

    // Ещё один SDU и PDU
    assert(rlc.sendSdu(r1, 1, ByteBuffer(10, 0xCD)));
    ByteBuffer pdu2;
    assert(rlc.pollPdu(r1, 1, pdu2));
    assert(!pdu2.empty());
    assert(rlc.txSN(r1, 1) == 2);

    // Когда txQueue пуст → pollPdu возвращает false
    ByteBuffer emptyPdu;
    assert(!rlc.pollPdu(r1, 1, emptyPdu));

    // ── UM mode: sendSdu + pollPdu ────────────────────────────────────────────
    assert(rlc.sendSdu(r1, 2, ByteBuffer(8, 0x77)));
    ByteBuffer umPdu;
    assert(rlc.pollPdu(r1, 2, umPdu));
    assert(!umPdu.empty());
    assert(rlc.txSN(r1, 2) == 1);

    // ── TM mode: sendSdu + pollPdu (нет заголовка) ───────────────────────────
    ByteBuffer tmData{0x01, 0x02, 0x03};
    assert(rlc.sendSdu(r1, 3, tmData));
    ByteBuffer tmPdu;
    assert(rlc.pollPdu(r1, 3, tmPdu));
    assert(!tmPdu.empty());

    // ── deliverPdu → receiveSdu ──────────────────────────────────────────────
    // Строим минимальный AM DATA PDU c SN=0, D/C=1, P=0, RF=0, FI=0
    // Байт 0: 1_0_0_0_0000 (D/C=1, RF=0, P=0, FI=0, SN hi 4 bits = 0)
    // Байт 1: SN lo 8 bits = 0
    // Байты 2+: данные
    ByteBuffer inboundAm;
    inboundAm.push_back(0x80); // D/C=1, остальное=0
    inboundAm.push_back(0x00); // SN=0
    ByteBuffer payload{0xDE, 0xAD, 0xBE, 0xEF};
    inboundAm.insert(inboundAm.end(), payload.begin(), payload.end());
    rlc.deliverPdu(r2, 1, inboundAm);

    ByteBuffer rxSdu;
    bool gotSdu = rlc.receiveSdu(r2, 1, rxSdu);
    assert(gotSdu);
    assert(!rxSdu.empty());
    assert(rlc.rxSN(r2, 1) == 1);

    // После первого receiveSdu очередь пуста
    ByteBuffer rx2;
    assert(!rlc.receiveSdu(r2, 1, rx2));

    // ── removeRB ─────────────────────────────────────────────────────────────
    assert(rlc.removeRB(r1, 1));
    assert(rlc.removeRB(r1, 2));
    assert(rlc.removeRB(r1, 3));
    assert(rlc.removeRB(r2, 1));

    // removeRB для несуществующего → false
    assert(!rlc.removeRB(r1, 99));

    std::puts("test_umts_rlc PASSED");
    return 0;
}

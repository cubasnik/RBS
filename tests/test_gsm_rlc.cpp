#include "../src/gsm/gsm_rlc.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::gsm;

int main() {
    GSMRlc rlc;

    const RNTI r1 = 1;
    const RNTI r2 = 2;

    // ── requestLink ───────────────────────────────────────────────────────────
    // Before any link: state должен быть IDLE
    assert(rlc.linkState(r1, SAPI::RR_MM_CC) == LAPDmState::IDLE);

    assert(rlc.requestLink(r1, SAPI::RR_MM_CC));
    assert(rlc.linkState(r1, SAPI::RR_MM_CC) == LAPDmState::AWAITING_EST);

    assert(rlc.requestLink(r1, SAPI::SMS));
    assert(rlc.linkState(r1, SAPI::SMS) == LAPDmState::AWAITING_EST);

    // Второй UE независимо
    assert(rlc.requestLink(r2, SAPI::RR_MM_CC));
    assert(rlc.linkState(r2, SAPI::RR_MM_CC) == LAPDmState::AWAITING_EST);

    // ── tick с SABM-фреймом от UE → переход в MULTIPLE_FRAME_EST ──────────────
    // Диспетчер tick(): U-frame если (ctl & 0x05) != 0x01, т.е. bit2=1.
    // SABM = CTL_SABM_BASE = 0x2F (бит 2 = 1) → processUnnumb.
    // processUnnumb: SABM из AWAITING_EST → MULTIPLE_FRAME_EST.
    LAPDmFrame sabm{};
    sabm.address = 0x01; // SAPI=0 (RR_MM_CC), EA=1
    sabm.control = 0x2F; // SABM
    sabm.length  = 0;
    rlc.tick(sabm, r1);
    // После получения SABM → MULTIPLE_FRAME_EST
    assert(rlc.linkState(r1, SAPI::RR_MM_CC) == LAPDmState::MULTIPLE_FRAME_EST);

    // ── sendSdu в MULTIPLE_FRAME_EST → I-фрейм (ack-mode) ───────────────────
    ByteBuffer sdu1{0x01, 0x02, 0x03, 0x04};
    assert(rlc.sendSdu(r1, SAPI::RR_MM_CC, sdu1));

    ByteBuffer sdu2{0x05, 0x06};
    assert(rlc.sendSdu(r1, SAPI::RR_MM_CC, sdu2));

    // sendSdu перед установкой связи (не MULTIPLE_FRAME_EST) → UI
    ByteBuffer sdu3{0xAA};
    assert(rlc.sendSdu(r2, SAPI::RR_MM_CC, sdu3));  // AWAITING_EST → UI

    // ── receiveSdu (rxSduQueue пуст, должен вернуть false) ───────────────────
    ByteBuffer rx;
    assert(!rlc.receiveSdu(r1, SAPI::RR_MM_CC, rx));

    // ── tick с I-фреймом → данные попадают в rxSduQueue ──────────────────────
    // I-фрейм: address=0x01 (SAPI 0), control[0]=0x00 (N(S)=0, P=0), N(R)=0
    // info field содержит payload
    LAPDmFrame ifrm{};
    ifrm.address = 0x01;
    ifrm.control = 0x00; // I-frame: LSB=0
    ifrm.length  = 4;
    ifrm.info    = {0x11, 0x22, 0x33, 0x44};
    rlc.tick(ifrm, r1);

    // Теперь receiveSdu должен вернуть данные
    ByteBuffer rx2;
    bool got = rlc.receiveSdu(r1, SAPI::RR_MM_CC, rx2);
    assert(got);
    assert(rx2 == ifrm.info);

    // Вторая попытка — очередь пуста
    ByteBuffer rx3;
    assert(!rlc.receiveSdu(r1, SAPI::RR_MM_CC, rx3));

    // ── releaseLink ───────────────────────────────────────────────────────────
    assert(rlc.releaseLink(r1, SAPI::RR_MM_CC));
    assert(rlc.releaseLink(r1, SAPI::SMS));
    assert(rlc.releaseLink(r2, SAPI::RR_MM_CC));

    // После releaseLink — AWAITING_REL (DISC отправлен, ожидаем UA от UE)
    assert(rlc.linkState(r1, SAPI::RR_MM_CC) == LAPDmState::AWAITING_REL);

    // ── tick с неизвестным/не-очевидным полем управления ─────────────────────
    LAPDmFrame unknown{};
    unknown.address = 0x01;
    unknown.control = 0xFF; // произвольный
    unknown.length  = 0;
    rlc.tick(unknown, r1);  // должно не крашиться

    std::puts("test_gsm_rlc PASSED");
    return 0;
}

#include "../src/umts/umts_rrc.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rbs;
using namespace rbs::umts;

int main() {
    UMTSRrc rrc;

    const RNTI r1 = 100;
    [[maybe_unused]] const RNTI r2 = 101;

    // ── handleConnectionRequest ───────────────────────────────────────────────
    assert(rrc.rrcState(r1) == UMTSRrcState::IDLE);

    assert(rrc.handleConnectionRequest(r1, 123456789012345ULL));
    assert(rrc.rrcState(r1) == UMTSRrcState::CELL_DCH);

    assert(rrc.handleConnectionRequest(r2, 999000000000001ULL));
    assert(rrc.rrcState(r2) == UMTSRrcState::CELL_DCH);

    // ── setupRadioBearer ─────────────────────────────────────────────────────
    RadioBearer rb1{1, 2 /*AM*/, 384, true, true};  // 384 kbps
    assert(rrc.setupRadioBearer(r1, rb1));
    assert(!rrc.bearers(r1).empty());
    assert(rrc.bearers(r1)[0].rbId == 1);

    RadioBearer rb2{2, 2 /*AM*/, 64000, true, false};
    assert(rrc.setupRadioBearer(r1, rb2));
    assert(rrc.bearers(r1).size() == 2);

    // ── releaseRadioBearer ────────────────────────────────────────────────────
    assert(rrc.releaseRadioBearer(r1, 1));
    assert(rrc.bearers(r1).size() == 1);
    assert(rrc.bearers(r1)[0].rbId == 2);

    // releaseRadioBearer несуществующего → false
    assert(!rrc.releaseRadioBearer(r1, 99));

    // ── addToActiveSet ───────────────────────────────────────────────────────
    ActiveSetEntry e1{0,   10562, -10, true };  // primary
    ActiveSetEntry e2{16,  10562, -14, false};
    ActiveSetEntry e3{32,  10562, -16, false};

    assert(rrc.addToActiveSet(r1, e1));
    assert(rrc.addToActiveSet(r1, e2));
    assert(rrc.addToActiveSet(r1, e3));

    // Четвёртый не добавляется (max 3)
    ActiveSetEntry e4{48, 10562, -18, false};
    assert(!rrc.addToActiveSet(r1, e4));

    // ── removeFromActiveSet ──────────────────────────────────────────────────
    assert(rrc.removeFromActiveSet(r1, 16));
    // Удаление несуществующего → false
    assert(!rrc.removeFromActiveSet(r1, 99));

    // ── processMeasurementReport ─────────────────────────────────────────────
    MeasurementReport mr{};
    mr.rnti             = r1;
    mr.event            = RrcMeasEvent::EVENT_1A;
    mr.triggeringScrCode= 48;
    mr.cpichEcNo_dB     = -8;
    mr.cpichRscp_dBm    = -75;
    rrc.processMeasurementReport(mr); // не крашится

    MeasurementReport mr2{};
    mr2.rnti             = r1;
    mr2.event            = RrcMeasEvent::EVENT_1B;
    mr2.triggeringScrCode= 32; // убрать из active set
    mr2.cpichEcNo_dB     = -20;
    mr2.cpichRscp_dBm    = -90;
    rrc.processMeasurementReport(mr2); // не крашится

    // ── activateSecurity ─────────────────────────────────────────────────────
    uint8_t ck[16] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                      0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    uint8_t ik[16] = {0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
                      0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10};
    assert(rrc.activateSecurity(r1, ck, ik));

    // ── scheduleSIB не крашится ───────────────────────────────────────────────
    rrc.scheduleSIB(1);   // SIB1
    rrc.scheduleSIB(3);   // SIB3
    rrc.scheduleSIB(5);   // SIB5

    // ── releaseConnection ────────────────────────────────────────────────────
    assert(rrc.releaseConnection(r1));
    assert(rrc.rrcState(r1) == UMTSRrcState::IDLE);

    assert(rrc.releaseConnection(r2));
    assert(rrc.rrcState(r2) == UMTSRrcState::IDLE);

    // Повторный release → false
    assert(!rrc.releaseConnection(r1));

    std::puts("test_umts_rrc PASSED");
    return 0;
}

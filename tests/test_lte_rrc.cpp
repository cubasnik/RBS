#include "../src/lte/lte_rrc.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rbs;
using namespace rbs::lte;

int main() {
    LTERrc rrc;

    const RNTI r1 = 200;
    const RNTI r2 = 201;

    // ── начальное состояние ───────────────────────────────────────────────────
    assert(rrc.rrcState(r1) == LTERrcState::RRC_IDLE);
    assert(rrc.drbs(r1).empty());

    // ── handleConnectionRequest ───────────────────────────────────────────────
    assert(rrc.handleConnectionRequest(r1, 111222333444555ULL));
    assert(rrc.rrcState(r1) == LTERrcState::RRC_CONNECTED);

    assert(rrc.handleConnectionRequest(r2, 555444333222111ULL));
    assert(rrc.rrcState(r2) == LTERrcState::RRC_CONNECTED);

    // ── setupDRB ─────────────────────────────────────────────────────────────
    LTEDataBearer drb1{1, 9, 50000000, 10000000, 0, 0};  // DRB1, QCI=9
    assert(rrc.setupDRB(r1, drb1));
    assert(rrc.drbs(r1).size() == 1);
    assert(rrc.drbs(r1)[0].drbId == 1);
    assert(rrc.drbs(r1)[0].qci   == 9);

    LTEDataBearer drb2{2, 5, 100000000, 20000000, 500000, 100000};  // DRB2, QCI=5
    assert(rrc.setupDRB(r1, drb2));
    assert(rrc.drbs(r1).size() == 2);

    // setupDRB для второго UE
    LTEDataBearer drb2b{1, 1, 128000000, 32000000, 1000000, 256000};
    assert(rrc.setupDRB(r2, drb2b));
    assert(rrc.drbs(r2).size() == 1);

    // ── releaseDRB ────────────────────────────────────────────────────────────
    assert(rrc.releaseDRB(r1, 1));
    assert(rrc.drbs(r1).size() == 1);
    assert(rrc.drbs(r1)[0].drbId == 2);

    // releaseDRB несуществующего → false
    assert(!rrc.releaseDRB(r1, 99));

    // ── activateSecurity ─────────────────────────────────────────────────────
    uint8_t kRrcEnc[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                            0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00};
    uint8_t kRrcInt[16] = {0x00,0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,
                            0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11};
    // cipherAlg=1 (EEA1/SNOW), integAlg=2 (EIA2/AES)
    assert(rrc.activateSecurity(r1, 1, 2, kRrcEnc, kRrcInt));

    // ── sendMeasurementConfig не крашится ─────────────────────────────────────
    MeasObject mo{1, 3000, LTEBand::B3};
    ReportConfig rc{};
    rc.reportConfigId = 1;
    rc.triggerQty     = RrcTriggerQty::RSRP;
    rc.a3Offset_dB    = 3;
    rc.timeToTrigger_ms = 160;
    rrc.sendMeasurementConfig(r1, mo, rc);
    rrc.sendMeasurementConfig(r2, mo, rc);

    // ── processMeasurementReport не крашится ─────────────────────────────────
    LTERrcMeasResult mr{};
    mr.rnti       = r1;
    mr.measId     = 1;
    mr.rsrp_q     = 60;  // RSRP -80 dBm in Q step
    mr.rsrq_q     = 20;
    mr.servCellId = 1;
    mr.neighbours.push_back({200, 3050, 55, 18});
    mr.neighbours.push_back({201, 3050, 50, 15});
    rrc.processMeasurementReport(mr);  // не крашится

    // ── prepareHandover ───────────────────────────────────────────────────────
    bool hoOk = rrc.prepareHandover(r1, 200 /*targetPci*/, 3050 /*targetEarfcn*/);
    (void)hoOk;  // может быть true/false в зависимости от impl

    // ── scheduleSIB не крашится ───────────────────────────────────────────────
    rrc.scheduleSIB(1);
    rrc.scheduleSIB(2);
    rrc.scheduleSIB(3);
    rrc.scheduleSIB(4);

    // ── releaseConnection ────────────────────────────────────────────────────
    assert(rrc.releaseConnection(r1));
    assert(rrc.rrcState(r1) == LTERrcState::RRC_IDLE);

    assert(rrc.releaseConnection(r2));
    assert(rrc.rrcState(r2) == LTERrcState::RRC_IDLE);

    // Повторный release → false
    assert(!rrc.releaseConnection(r1));

    // drbs пусты после release
    assert(rrc.drbs(r1).empty());

    std::puts("test_lte_rrc PASSED");
    return 0;
}

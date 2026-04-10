#include "../src/umts/iub_link.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::umts;

int main() {
    // ── IubNbap: control plane ────────────────────────────────────────────────
    IubNbap nbap("NodeB-01");

    assert(!nbap.isConnected());

    // connect() — симулятор, устанавливает connected_=true
    assert(nbap.connect("127.0.0.1", 25099));
    assert(nbap.isConnected());

    // sendCellSetup: кодирует Cell Setup Request FDD, не крашится
    assert(nbap.sendCellSetup(1 /*cellId*/, 0 /*primaryScrCode*/,
                               10562 /*uarfcnDl*/, 9612 /*uarfcnUl*/));

    // sendNbapMsg / recvNbapMsg
    NBAPMessage req{NBAPProcedure::RESET, 1, {0x01}};
    assert(nbap.sendNbapMsg(req));
    // Нет авто-ответа для RESET → recvNbapMsg возвращает false
    NBAPMessage resp{};
    // может быть false если нет авто-ответа
    bool got = nbap.recvNbapMsg(resp);
    (void)got;

    // commonMeasurementInitiation → авто-ответ COMMON_MEASUREMENT_REPORT
    assert(nbap.commonMeasurementInitiation(1, "RSCP"));
    NBAPMessage measResp{};
    assert(nbap.recvNbapMsg(measResp));
    assert(measResp.procedure == NBAPProcedure::COMMON_MEASUREMENT_REPORT);

    // commonMeasurementTermination
    assert(nbap.commonMeasurementTermination(1));

    // radioLinkSetup для UE
    assert(nbap.radioLinkSetup(0x0A /*rnti*/, 0 /*scrCode*/, SF::SF16));
    assert(nbap.radioLinkSetup(0x0B /*rnti*/, 16 /*scrCode*/, SF::SF8));

    // dedicatedMeasurementInitiation
    assert(nbap.dedicatedMeasurementInitiation(0x0A, 10 /*measId*/));

    // radioLinkDeletion
    assert(nbap.radioLinkDeletion(0x0A));
    assert(nbap.radioLinkDeletion(0x0B));

    // Повторное удаление → false
    assert(!nbap.radioLinkDeletion(0x0A));

    // disconnect
    nbap.disconnect();
    assert(!nbap.isConnected());

    // ── IubFp: user-plane DCH transport ──────────────────────────────────────
    IubFp fp("NodeB-01");

    // sendDchData — UL (к RNC), только логируется
    fp.sendDchData(0x0A, 0 /*scrCode*/, ByteBuffer(40, 0xBB));

    // receiveDchData — DL очередь пуста → false
    ByteBuffer tbs;
    assert(!fp.receiveDchData(0x0A, 0, tbs));

    // reportSyncStatus не крашится
    fp.reportSyncStatus(0x0A, true);
    fp.reportSyncStatus(0x0A, false);

    std::puts("test_iub_link PASSED");
    return 0;
}

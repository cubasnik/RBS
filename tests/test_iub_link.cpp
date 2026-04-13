#include "../src/umts/iub_link.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::umts;

static void test_bearer_setup_rollback_negative_cases() {
    IubNbap nbap("NodeB-rollback");
    assert(nbap.connect("127.0.0.1", 25099));
    assert(nbap.radioLinkSetup(0x2A, 7, SF::SF16));

    // Baseline bearer must exist before negative reconfigure cases.
    assert(nbap.radioBearerSetup(0x2A, 3, 2, 384, true, true));

    // Case 1: Prepare failure must rollback (existing bearer preserved).
    nbap.blockMsg("NBAP:82"); // RADIO_LINK_RECONFIGURE_PREP
    assert(!nbap.radioBearerSetup(0x2A, 3, 2, 2048, true, true));
    nbap.unblockMsg("NBAP:82");

    // Existing bearer context should remain valid after failed update.
    assert(nbap.radioBearerRelease(0x2A, 3));

    // Recreate baseline bearer for commit-failure case.
    assert(nbap.radioBearerSetup(0x2A, 3, 2, 384, true, true));

    // Case 2: Commit failure must rollback and keep previously configured bearer.
    nbap.blockMsg("NBAP:83"); // RADIO_LINK_RECONFIGURE_COMMIT
    assert(!nbap.radioBearerSetup(0x2A, 4, 2, 1024, true, true));
    nbap.unblockMsg("NBAP:83");

    // Failed new bearer must not appear, baseline bearer must still be present.
    assert(!nbap.radioBearerRelease(0x2A, 4));
    assert(nbap.radioBearerRelease(0x2A, 3));

    // UE radio link must remain intact after failed bearer attempts.
    assert(nbap.radioLinkDeletion(0x2A));
    nbap.disconnect();
    std::puts("  test_bearer_setup_rollback_negative_cases PASSED");
}

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

    // radioBearerSetup: полноценный NBAP workflow (prepare/commit)
    assert(nbap.radioBearerSetup(0x0A, 3 /*rbId*/, 2 /*AM*/, 384, true, true));
    assert(nbap.radioBearerSetup(0x0B, 4 /*rbId*/, 2 /*AM*/, 1024, true, true));

    // Повторная настройка bearer допускается (reconfigure existing bearer)
    assert(nbap.radioBearerSetup(0x0A, 3 /*rbId*/, 2 /*AM*/, 2048, true, true));

    // dedicatedMeasurementInitiation
    assert(nbap.dedicatedMeasurementInitiation(0x0A, 10 /*measId*/));

    // radioLinkDeletion
    assert(nbap.radioBearerRelease(0x0A, 3));
    assert(nbap.radioBearerRelease(0x0B, 4));
    assert(nbap.radioLinkDeletion(0x0A));
    assert(nbap.radioLinkDeletion(0x0B));

    // Повторное удаление → false
    assert(!nbap.radioLinkDeletion(0x0A));
    assert(!nbap.radioBearerRelease(0x0A, 3));

    test_bearer_setup_rollback_negative_cases();

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

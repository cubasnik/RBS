#include "../src/gsm/abis_link.h"
#include <cassert>
#include <cstdio>

using namespace rbs;
using namespace rbs::gsm;

int main() {
    // ── AbisOml: OML link ─────────────────────────────────────────────────────
    AbisOml oml("BTS-01");

    assert(!oml.isConnected());

    // connect() — симулятор, реальной сети нет
    assert(oml.connect("127.0.0.1", 3002));
    assert(oml.isConnected());

    // connect() внутри вызывает sendOmlMsg(OPSTART) → авто-ACK уже в rxQueue
    // Вытащим его, чтобы не мешал дальнейшим тестам.
    { OMLMsgType t{}; AbisMessage m{}; oml.recvOmlMsg(t, m); }

    // sendOmlMsg: OPSTART → должен авто-ответить OPSTART_ACK в rxQueue
    AbisMessage omsg{0x01, {0x00}};
    assert(oml.sendOmlMsg(OMLMsgType::OPSTART, omsg));

    // recvOmlMsg: должны получить ACK
    [[maybe_unused]] OMLMsgType rxType{};
    AbisMessage rxMsg{};
    assert(oml.recvOmlMsg(rxType, rxMsg));
    assert(rxType == OMLMsgType::OPSTART_ACK);

    // Когда очередь пуста → false
    assert(!oml.recvOmlMsg(rxType, rxMsg));

    // sendOmlMsg: SET_BTS_ATTR (нет авто-ответа)
    assert(oml.sendOmlMsg(OMLMsgType::SET_BTS_ATTR, AbisMessage{0x01, {0x42}}));
    assert(!oml.recvOmlMsg(rxType, rxMsg));  // нет ответа

    // configureTRX: stores config, sends SET_RADIO_CARRIER_ATTR
    oml.configureTRX(0, 60,  43);   // TRX 0: ARFCN=60, 43 dBm
    oml.configureTRX(1, 65,  40);   // TRX 1: ARFCN=65, 40 dBm

    // reportHwFailure: не крашится
    oml.reportHwFailure(0x01, "PA overtemperature");

    // disconnect
    oml.disconnect();
    assert(!oml.isConnected());

    // ── AbisRsl: RSL link ─────────────────────────────────────────────────────
    AbisRsl rsl("BTS-01");

    // sendRslMsg: DATA_REQUEST
    AbisMessage rmsg{0x01, {0x01, 0x02, 0x03}};
    assert(rsl.sendRslMsg(RSLMsgType::DATA_REQUEST, rmsg));

    // recvRslMsg: очередь должна быть пуста (DATA_REQUEST без авто-ответа)
    RSLMsgType rslType{};
    AbisMessage rslMsg{};
    assert(!rsl.recvRslMsg(rslType, rslMsg));

    // activateChannel → авто-ответ CHANNEL_ACTIVATION_ACK
    assert(rsl.activateChannel(0 /*chanNr*/, GSMChannelType::SDCCH, 10 /*rnti*/, 5 /*TA*/));
    assert(rsl.recvRslMsg(rslType, rslMsg));
    assert(rslType == RSLMsgType::CHANNEL_ACTIVATION_ACK);

    // activateChannel на другом channel number
    assert(rsl.activateChannel(1, GSMChannelType::TCH_F, 11, 3));
    rsl.recvRslMsg(rslType, rslMsg);  // выгрести ACK

    // releaseChannel
    assert(rsl.releaseChannel(0));
    // releaseChannel для незарегистрированного канала → false
    assert(!rsl.releaseChannel(99));

    // sendCipherModeCommand → авто-ответ CIPHER_MODE_COMPLETE
    ByteBuffer key(8, 0xA5);
    assert(rsl.sendCipherModeCommand(1, 1 /*A5/1*/, key));
    assert(rsl.recvRslMsg(rslType, rslMsg));
    assert(rslType == RSLMsgType::CIPHER_MODE_COMPLETE);

    // sendMeasurementResult: не крашится
    rsl.sendMeasurementResult(11, -70, 3);

    // forwardHandoverCommand: не крашится
    rsl.forwardHandoverCommand(11, ByteBuffer{0x06, 0x2E, 0x01});

    std::puts("test_abis_link PASSED");
    return 0;
}

#include "../src/lte/lte_stack.h"
#include "../src/lte/volte_stub.h"
#include "../src/hal/rf_hardware.h"
#include "../src/oms/oms.h"

#include <cassert>
#include <cstdio>

int main() {
    rbs::LTECellConfig cfg{};
    cfg.cellId = 31;
    cfg.earfcn = 1900;
    cfg.band = rbs::LTEBand::B3;
    cfg.bandwidth = rbs::LTEBandwidth::BW10;
    cfg.duplexMode = rbs::LTEDuplexMode::FDD;
    cfg.txPower = {43.0};
    cfg.pci = 210;
    cfg.tac = 1;
    cfg.mcc = 250;
    cfg.mnc = 1;
    cfg.numAntennas = 2;
    cfg.mmeAddr = "";
    cfg.s1uLocalPort = 3252;

    auto rf = std::make_shared<rbs::hal::RFHardware>(2, 4);
    assert(rf->initialise());

    rbs::lte::LTEStack stack(rf, cfg);
    assert(stack.start());

    const rbs::RNTI rnti = stack.admitUE(799900000000001ULL, 12);
    assert(rnti != 0);

    const double inviteBefore = rbs::oms::OMS::instance().getCounter("volte.sip.invite");
    const double rtpBefore = rbs::oms::OMS::instance().getCounter("volte.rtp.tx.packets");

    const std::string invite = rbs::lte::volte::buildInvite(
        "sip:ue1@ims.local", "sip:ue2@ims.local", "call-42",
        "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=RBS\r\n");

    const auto parsed = rbs::lte::volte::parseMessage(invite);
    assert(parsed.method == rbs::lte::volte::SipMethod::INVITE);
    assert(parsed.callId == "call-42");

    assert(stack.setupVoLTEBearer(rnti));
    assert(stack.handleSipMessage(rnti, invite));
    assert(stack.sendVoLteRtpBurst(rnti, 2, 120) >= 1);

    const std::string bye = rbs::lte::volte::buildBye(
        "sip:ue1@ims.local", "sip:ue2@ims.local", "call-42");
    assert(stack.handleSipMessage(rnti, bye));

    const double inviteAfter = rbs::oms::OMS::instance().getCounter("volte.sip.invite");
    const double rtpAfter = rbs::oms::OMS::instance().getCounter("volte.rtp.tx.packets");
    assert(inviteAfter >= inviteBefore + 1.0);
    assert(rtpAfter >= rtpBefore + 3.0);

    stack.releaseUE(rnti);
    stack.stop();
    rf->shutdown();

    std::puts("test_volte PASSED");
    return 0;
}

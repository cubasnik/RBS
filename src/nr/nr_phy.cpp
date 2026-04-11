// NRPhy — 5G NR gNB-DU Physical Layer
// TS 38.211 §4 (frame structure), §7.4.2 (PSS/SSS), §7.4.3 (SSB)
// TS 38.213 §4.1 (SSB timing)
#include "nr_phy.h"
#include "../common/logger.h"
#include <cstring>
#include <cstdio>

namespace rbs::nr {

// ── Constants ────────────────────────────────────────────────────
// PSS/SSS sequence length (TS 38.211 §7.4.2)
static constexpr uint8_t NR_PSS_LEN = 127;
static constexpr uint8_t NR_SSS_LEN = 127;
// PBCH payload: 32 bits MIB + 24 bits CRC (TS 38.212 §7.1.5)
static constexpr uint8_t NR_PBCH_BYTES = 7;  // ceil(56/8)

// ── PSS: m-sequence based on PCI (TS 38.211 §7.4.2.2.1) ─────────
// PSS sequence d_{PSS}(n) = 1 - 2*x(m_PSS), where m_PSS = (n + 43*N_ID_2) mod 127
// N_ID_2 = PCI mod 3 (physical-layer identity within group)
ByteBuffer NRPhy::buildPSS(uint16_t pci) const {
    const uint8_t N_ID_2 = static_cast<uint8_t>(pci % 3);
    // MLS generator: x(i+7) = (x(i+4)+x(i)) mod 2; initial state = 1110100b
    uint8_t reg = 0x74;  // 0b1110100
    uint8_t seq[NR_PSS_LEN];
    for (int i = 0; i < NR_PSS_LEN; ++i) {
        seq[i] = (reg >> 6) & 1;  // x(i)
        uint8_t fb = ((reg >> 3) ^ reg) & 1;
        reg = static_cast<uint8_t>((reg << 1) | fb) & 0x7F;
    }
    ByteBuffer pss(NR_PSS_LEN);
    for (int n = 0; n < NR_PSS_LEN; ++n) {
        const int m = (n + 43 * N_ID_2) % NR_PSS_LEN;
        pss[static_cast<size_t>(n)] = static_cast<uint8_t>(1 - 2 * seq[m]);
    }
    return pss;
}

// ── SSS: Gold-sequence based on PCI (TS 38.211 §7.4.2.3.1) ─────
// d_{SSS}(n) = [1-2x0(m0+n mod 127)][1-2x1(m1+n mod 127)]
// N_ID_1 = PCI / 3   (group identity, 0-335)
// m0 = 15*(N_ID_1/112) + 5*N_ID_2
// m1 = N_ID_1 mod 112
ByteBuffer NRPhy::buildSSS(uint16_t pci) const {
    const uint8_t  N_ID_2 = static_cast<uint8_t>(pci % 3);
    const uint16_t N_ID_1 = static_cast<uint16_t>(pci / 3);
    const int m0 = 15 * (N_ID_1 / 112) + 5 * N_ID_2;
    const int m1 = N_ID_1 % 112;

    // MLS x0: initial state x(0..6) = 0000001
    uint8_t x0_reg = 0x01;
    uint8_t x0[NR_SSS_LEN];
    for (int i = 0; i < NR_SSS_LEN; ++i) {
        x0[i] = x0_reg & 1;
        uint8_t fb = ((x0_reg >> 3) ^ (x0_reg)) & 1;
        x0_reg = static_cast<uint8_t>((x0_reg >> 1) | (fb << 6));
    }
    // MLS x1: initial state x(0..6) = 0000001
    uint8_t x1_reg = 0x01;
    uint8_t x1[NR_SSS_LEN];
    for (int i = 0; i < NR_SSS_LEN; ++i) {
        x1[i] = x1_reg & 1;
        uint8_t fb = ((x1_reg >> 3) ^ (x1_reg)) & 1;
        x1_reg = static_cast<uint8_t>((x1_reg >> 1) | (fb << 6));
    }

    ByteBuffer sss(NR_SSS_LEN);
    for (int n = 0; n < NR_SSS_LEN; ++n) {
        const uint8_t v0 = x0[(m0 + n) % NR_SSS_LEN];
        const uint8_t v1 = x1[(m1 + n) % NR_SSS_LEN];
        sss[static_cast<size_t>(n)] = static_cast<uint8_t>((1 - 2*v0) * (1 - 2*v1));
    }
    return sss;
}

// ── PBCH: MIB packed + 24-bit CRC (TS 38.331 §6.2.2) ───────────
// MIB fields (simplified, 6 bits from SFN + misc):
//   systemFrameNumber [0..3] (4 MSBs, remaining 6 are in PBCH DMRS)
//   subCarrierSpacingCommon (scs)
//   dmrs-TypeA-Position = pos2
//   controlResourceSetZero = 0
//   searchSpaceZero = 0
//   cellBarred = false
//   intraFreqReselection = allowed
//   spare = 0
ByteBuffer NRPhy::buildPBCH(uint32_t sfn, uint8_t halfFrame) const {
    ByteBuffer pbch(NR_PBCH_BYTES, 0);
    // Pack SFN[9:6] into bits [5:2] of byte 0
    pbch[0] = static_cast<uint8_t>((sfn >> 6) & 0x0F) << 2;
    // Pack SCS into bits [1:0] of byte 0
    pbch[0] |= static_cast<uint8_t>(cfg_.scs) & 0x03;
    // Byte 1: halfFrame (bit 7), SFN[5:0] (bits 6:1)
    pbch[1] = static_cast<uint8_t>((halfFrame & 1) << 7)
            | static_cast<uint8_t>((sfn & 0x3F) << 1);
    // Bytes 2-3: PCI (2 bytes, big-endian)
    pbch[2] = static_cast<uint8_t>(cfg_.nrPci >> 8);
    pbch[3] = static_cast<uint8_t>(cfg_.nrPci & 0xFF);
    // Bytes 4-5: nrArfcn lower 16 bits
    pbch[4] = static_cast<uint8_t>(cfg_.nrArfcn >> 8);
    pbch[5] = static_cast<uint8_t>(cfg_.nrArfcn & 0xFF);
    // Byte 6: SCS | cell barred=0 | spare
    pbch[6] = static_cast<uint8_t>(cfg_.scs);
    return pbch;
}

// ── SSB subframe detection ───────────────────────────────────────
// Default SSB period = 20 ms = every 20 subframes (sfIdx_==0 of frame% period)
// TS 38.213 §4.1: SSB burst set transmitted in first half-frame of each period.
bool NRPhy::isSSBSubframe() const {
    const uint8_t period = cfg_.ssbPeriodMs;  // 5/10/20/40/80/160 ms
    // One subframe = 1 ms. SSB is at sfIdx==0 of the first subframe in the period.
    const uint32_t absMs = sfn_ * 10u + sfIdx_;
    return (sfIdx_ == 0) && ((absMs % period) == 0);
}

// ────────────────────────────────────────────────────────────────
NRPhy::NRPhy(std::shared_ptr<hal::IRFHardware> rf, const NRCellConfig& cfg)
    : rf_(std::move(rf)), cfg_(cfg) {}

bool NRPhy::start() {
    if (running_) return true;
    running_ = true;
    RBS_LOG_INFO("NRPhy", "NR PHY started: ARFCN=", cfg_.nrArfcn,
             " SCS=", nrScsKhz(cfg_.scs), "kHz",
             " PCI=", cfg_.nrPci,
             " SSBperiod=", static_cast<int>(cfg_.ssbPeriodMs), "ms");
    return true;
}

void NRPhy::stop() {
    if (!running_) return;
    running_ = false;
    RBS_LOG_INFO("NRPhy", "NR PHY stopped after ", ssbCount_, " SSBs");
}

void NRPhy::tick() {
    if (!running_) return;

    // Simulate received SS-RSRP from RF (TS 38.215 §5.1.1)
    if (rf_) {
        const auto status = rf_->getStatus();
        ssRsrp_dBm_ = status.rxNoiseFigure_dB > 0
                    ? -80.0 - status.rxNoiseFigure_dB * 0.5
                    : -80.0;
    }

    // Transmit SSB burst if due
    if (isSSBSubframe()) {
        const uint8_t hf = static_cast<uint8_t>((sfn_ % 2 == 0) ? 0 : 1);
        // FR1: 4 candidate SSBs per half-frame (TS 38.213 §4.1 Table 4.1-2)
        const uint8_t numSSBs = 4;
        for (uint8_t idx = 0; idx < numSSBs; ++idx) {
            NRSSBlock ssb;
            ssb.ssbIdx    = idx;
            ssb.sfn       = sfn_;
            ssb.halfFrame = hf;
            ssb.pss  = buildPSS(cfg_.nrPci);
            ssb.sss  = buildSSS(cfg_.nrPci);
            ssb.pbch = buildPBCH(sfn_, hf);

            if (rf_) rf_->transmit(ssb.pss);  // simplified: send PSS to RF HAL
            ++ssbCount_;
            if (ssbCb_) ssbCb_(ssb);
        }
        RBS_LOG_DEBUG("NRPhy", "SSB burst SFN=", sfn_,
                " hf=", static_cast<int>(hf),
                " total=", ssbCount_);
    }

    // Advance subframe / SFN counter (TS 38.211 §4.3.2)
    if (++sfIdx_ >= 10) {
        sfIdx_ = 0;
        sfn_ = (sfn_ + 1) & 0x3FF;  // SFN wraps at 1024
    }
}

}  // namespace rbs::nr

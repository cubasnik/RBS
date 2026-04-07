#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "lte_phy.h"
#include "../common/logger.h"
#include <cstring>
#include <random>
#include <cmath>

namespace rbs::lte {

LTEPhy::LTEPhy(std::shared_ptr<hal::IRFHardware> rf, const LTECellConfig& cfg)
    : rf_(std::move(rf)), cfg_(cfg) {}

// ────────────────────────────────────────────────────────────────
bool LTEPhy::start() {
    if (running_) return true;
    // Convert EARFCN to frequency (E-UTRA Band 3 formula, 3GPP TS 36.101 §5.7.3)
    // FDL = FDL_low + 0.1*(NDL − NDL_offset)
    // Band 3: FDL_low=1805.0, NDL_offset=1200
    double dlFreq = 1805.0 + 0.1 * (static_cast<double>(cfg_.earfcn) - 1200.0);
    double ulFreq = dlFreq - 190.0;   // Band 3 duplex gap
    if (!rf_->setDlFrequency(dlFreq) || !rf_->setUlFrequency(ulFreq)) {
        RBS_LOG_ERROR("LTEPhy", "Frequency config failed for EARFCN=", cfg_.earfcn);
        return false;
    }
    rf_->setTxPower(cfg_.txPower.dBm);
    rf_->setActiveTxAntennas((1u << cfg_.numAntennas) - 1u);
    sfn_  = 0;
    sfIdx_ = 0;
    running_ = true;
    RBS_LOG_INFO("LTEPhy", "Started – EARFCN=", cfg_.earfcn,
                 " PCI=", cfg_.pci,
                 " BW=", static_cast<int>(numResourceBlocks()), " RBs",
                 " DL=", dlFreq, " MHz",
                 " Antennas=", static_cast<int>(cfg_.numAntennas));
    return true;
}

void LTEPhy::stop() {
    running_ = false;
    RBS_LOG_INFO("LTEPhy", "Stopped");
}

// ────────────────────────────────────────────────────────────────
void LTEPhy::tick() {
    if (!running_) return;

    LTESubframe sf;
    sf.sfn            = sfn_;
    sf.subframeIndex  = sfIdx_;

    // Build and transmit downlink
    transmitSubframe(sf);

    // Receive uplink
    LTESubframe ulSF;
    if (receiveSubframe(ulSF) && rxCb_) {
        rxCb_(ulSF);
    }

    // Advance subframe/SFN
    ++sfIdx_;
    if (sfIdx_ >= LTE_SUBFRAMES_PER_FRAME) {
        sfIdx_ = 0;
        ++sfn_;
        if (sfn_ >= 1024) sfn_ = 0;
    }
}

// ────────────────────────────────────────────────────────────────
bool LTEPhy::transmitSubframe(const LTESubframe& sf) {
    ByteBuffer txBuf;
    auto append = [&](const ByteBuffer& b) {
        txBuf.insert(txBuf.end(), b.begin(), b.end());
    };

    if (isSyncSubframe()) {
        append(buildPSS());
        append(buildSSS());
        append(buildPBCH());
    }
    if (!sf.dlGrants.empty()) {
        append(buildPDCCH(sf.dlGrants));
        append(buildPDSCH(sf.dlGrants));
    }
    return rf_->transmit(ofdmModulate(txBuf));
}

bool LTEPhy::receiveSubframe(LTESubframe& sf) {
    ByteBuffer raw;
    uint32_t samplesPerSF = 30720u;  // 30.72 MHz sampling rate * 1 ms
    if (!rf_->receive(raw, samplesPerSF)) return false;
    sf.sfn           = sfn_;
    sf.subframeIndex = sfIdx_;
    // In a real system: FFT, channel estimation, PUSCH decoding, HARQ combining
    return true;
}

// ────────────────────────────────────────────────────────────────
bool LTEPhy::isSyncSubframe() const {
    return sfIdx_ == 0;   // subframe 0 of every radio frame
}

// ────────────────────────────────────────────────────────────────
// Physical channel builders (simplified bit patterns)
// ────────────────────────────────────────────────────────────────
ByteBuffer LTEPhy::buildPSS() const {
    // PSS: Zadoff-Chu sequence root depends on PCI mod 3
    // Here simplified to 62 non-zero subcarrier symbols (3GPP TS 36.211 §6.11.1)
    uint8_t root = cfg_.pci % 3;  // roots: 25, 29, 34
    ByteBuffer pss(62, 0);
    for (int k = 0; k < 62; ++k)
        pss[k] = static_cast<uint8_t>(
            static_cast<int>(255 * std::cos(M_PI * root * k * (k + 1) / 63.0)) & 0xFF);
    return pss;
}

ByteBuffer LTEPhy::buildSSS() const {
    // SSS encodes physical layer cell identity group (PCI / 3)
    uint16_t grp = cfg_.pci / 3;
    ByteBuffer sss(62, 0);
    for (int k = 0; k < 62; ++k)
        sss[k] = static_cast<uint8_t>((grp ^ (k * 0x5A + sfn_)) & 0xFF);
    return sss;
}

ByteBuffer LTEPhy::buildPBCH() const {
    // MIB: bandwidth, PHICH config, SFN (3GPP TS 36.331 §6.2.2)
    ByteBuffer mib(3, 0);
    mib[0] = static_cast<uint8_t>(cfg_.bandwidth) << 5;     // dl-Bandwidth (3 bits)
    mib[0] |= 0;                                              // phich-Config  (3 bits)
    mib[1] = static_cast<uint8_t>((sfn_ >> 2) & 0xFF);       // systemFrameNumber (8 MSBs of 10-bit SFN)
    mib[2] = static_cast<uint8_t>((sfn_ & 0x3) << 6);        // 2 LSBs
    return mib;
}

ByteBuffer LTEPhy::buildPDCCH(const std::vector<ResourceBlock>& grants) const {
    // DCI format 1A per grant (simplified)
    ByteBuffer pdcch;
    for (const auto& rb : grants) {
        pdcch.push_back(rb.rnti >> 8);
        pdcch.push_back(rb.rnti & 0xFF);
        pdcch.push_back(rb.rbIndex);
        pdcch.push_back(rb.mcs);
    }
    return pdcch;
}

ByteBuffer LTEPhy::buildPDSCH(const std::vector<ResourceBlock>& grants) const {
    // Size: each RB carries 12 subcarriers * 7 symbols * bits-per-symbol
    // For MCS 9 (QPSK): ~150 bytes per RB
    ByteBuffer pdsch;
    for (const auto& rb : grants) {
        uint32_t rbBytes = 150u * (1u + rb.mcs / 10u);
        ByteBuffer payload(rbBytes, static_cast<uint8_t>(rb.rnti & 0xFF));
        pdsch.insert(pdsch.end(), payload.begin(), payload.end());
    }
    return pdsch;
}

ByteBuffer LTEPhy::ofdmModulate(const ByteBuffer& freqDomain) const {
    // Simplified: pack data as IQ pairs (each byte → I=byte, Q=0)
    ByteBuffer iq;
    iq.reserve(freqDomain.size() * 2);
    for (uint8_t b : freqDomain) {
        iq.push_back(b);    // I
        iq.push_back(0);    // Q (real-valued symbol in simplified model)
    }
    return iq;
}

}  // namespace rbs::lte

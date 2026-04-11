#include "rf_hardware.h"
#include "../common/logger.h"
#include <cstring>
#include <cmath>
#include <random>

namespace rbs::hal {

RFHardware::RFHardware(int txAntennas, int rxAntennas)
    : txAntennas_(static_cast<uint8_t>(txAntennas))
    , rxAntennas_(static_cast<uint8_t>(rxAntennas))
    , activeTxMask_((1u << txAntennas) - 1u)
{}

// ────────────────────────────────────────────────────────────────
bool RFHardware::initialise() {
    if (initialised_) return true;
    RBS_LOG_INFO("RFHardware", "Initialising RF frontend (Tx=",
                 static_cast<int>(txAntennas_), " Rx=", static_cast<int>(rxAntennas_), ")");
    // In a real system: probe PCIe device, load FPGA bitstream,
    // calibrate DAC/ADC offsets, run PLL lock sequence.
    txPower_dBm_ = 0.0;
    dlFreq_MHz_  = 1800.0;
    ulFreq_MHz_  = 1710.0;
    hwStatus_.store(HardwareStatus::OK);
    initialised_ = true;
    RBS_LOG_INFO("RFHardware", "RF frontend initialised successfully");
    return true;
}

// ────────────────────────────────────────────────────────────────
bool RFHardware::selfTest() {
    if (!initialised_) return false;
    RBS_LOG_INFO("RFHardware", "Running RF self-test...");
    // Simulate loopback test
    const bool loopbackOk = true;               // replace with actual loopback
    const bool pllLocked  = true;               // replace with PLL status read
    const bool paOk       = txPower_dBm_ <= 46.0;
    if (!loopbackOk || !pllLocked || !paOk) {
        raiseAlarm(HardwareStatus::FAULT, "Self-test failed");
        return false;
    }
    RBS_LOG_INFO("RFHardware", "Self-test PASSED");
    return true;
}

// ────────────────────────────────────────────────────────────────
void RFHardware::shutdown() {
    if (!initialised_) return;
    RBS_LOG_INFO("RFHardware", "Shutting down RF frontend");
    // Ramp down PA, put PLL into sleep, safe state
    txPower_dBm_ = 0.0;
    hwStatus_.store(HardwareStatus::OFFLINE);
    initialised_ = false;
}

// ────────────────────────────────────────────────────────────────
bool RFHardware::setDlFrequency(double freq_MHz) {
    if (!checkFrequencyRange(freq_MHz)) {
        RBS_LOG_ERROR("RFHardware", "DL frequency out of range: ", freq_MHz, " MHz");
        return false;
    }
    dlFreq_MHz_ = freq_MHz;
    RBS_LOG_DEBUG("RFHardware", "DL frequency set to ", freq_MHz, " MHz");
    return true;
}

bool RFHardware::setUlFrequency(double freq_MHz) {
    if (!checkFrequencyRange(freq_MHz)) {
        RBS_LOG_ERROR("RFHardware", "UL frequency out of range: ", freq_MHz, " MHz");
        return false;
    }
    ulFreq_MHz_ = freq_MHz;
    RBS_LOG_DEBUG("RFHardware", "UL frequency set to ", freq_MHz, " MHz");
    return true;
}

// ────────────────────────────────────────────────────────────────
bool RFHardware::setTxPower(double dBm) {
    if (dBm < -30.0 || dBm > 46.0) {
        RBS_LOG_WARNING("RFHardware", "TX power ", dBm, " dBm out of safe range");
        return false;
    }
    txPower_dBm_ = dBm;
    RBS_LOG_DEBUG("RFHardware", "TX power set to ", dBm, " dBm");
    return true;
}

double RFHardware::getTxPower() const { return txPower_dBm_; }

// ────────────────────────────────────────────────────────────────
bool RFHardware::transmit(const ByteBuffer& iqSamples) {
    if (!initialised_) return false;
    // In a real system: write IQ samples to DMA buffer → DAC → PA → antenna
    RBS_LOG_DEBUG("RFHardware", "Transmitting ", iqSamples.size(), " IQ bytes");
    return true;
}

bool RFHardware::receive(ByteBuffer& iqSamples, uint32_t numSamples) {
    if (!initialised_) return false;
    // Simulate: fill with gaussian noise (placeholder for actual ADC read)
    iqSamples.resize(numSamples * 4); // 2× int16 per complex sample
    static std::mt19937 rng{42};
    static std::uniform_int_distribution<unsigned int> dist(0, 255);
    for (auto& b : iqSamples) b = static_cast<uint8_t>(dist(rng));
    return true;
}

// ────────────────────────────────────────────────────────────────
RFStatus RFHardware::getStatus() const {
    RFStatus st{};
    st.status           = hwStatus_.load();
    st.vswr             = 1.2;           // ideal antenna match
    st.temperature_C    = 45.0;          // typical PA temperature
    st.txPower_dBm      = txPower_dBm_;
    st.rxNoiseFigure_dB = 3.5;
    return st;
}

// ────────────────────────────────────────────────────────────────
bool RFHardware::setActiveTxAntennas(uint8_t mask) {
    uint8_t fullMask = (1u << txAntennas_) - 1u;
    if ((mask & ~fullMask) != 0) {
        RBS_LOG_ERROR("RFHardware", "Invalid TX antenna mask: 0x",
                      std::hex, static_cast<int>(mask));
        return false;
    }
    activeTxMask_ = mask;
    return true;
}

// ────────────────────────────────────────────────────────────────
bool RFHardware::checkFrequencyRange(double freq_MHz) const {
    return freq_MHz >= 700.0 && freq_MHz <= 2700.0;
}

void RFHardware::raiseAlarm(HardwareStatus s, const std::string& msg) {
    hwStatus_.store(s);
    RBS_LOG_ERROR("RFHardware", "ALARM: ", msg);
    if (alarmCb_) alarmCb_(s, msg);
}

}  // namespace rbs::hal

#pragma once
#include "rf_interface.h"
#include <atomic>

namespace rbs::hal {

// ────────────────────────────────────────────────────────────────
// Simulated RF hardware implementation.
// In a real deployment this would interface with the actual
// FPGA / baseband chip via PCIe / SPI / JESD204.
// ────────────────────────────────────────────────────────────────
class RFHardware final : public IRFHardware {
public:
    explicit RFHardware(int txAntennas = 2, int rxAntennas = 2);

    bool  initialise()  override;
    bool  selfTest()    override;
    void  shutdown()    override;

    bool  setDlFrequency(double freq_MHz) override;
    bool  setUlFrequency(double freq_MHz) override;

    bool  setTxPower(double dBm)   override;
    double getTxPower()     const  override;

    bool  transmit(const ByteBuffer& iqSamples) override;
    bool  receive (ByteBuffer& iqSamples, uint32_t numSamples) override;

    RFStatus getStatus() const override;

    uint8_t numTxAntennas() const override { return txAntennas_; }
    uint8_t numRxAntennas() const override { return rxAntennas_; }
    bool    setActiveTxAntennas(uint8_t mask) override;

    void setAlarmCallback(AlarmCb cb) override { alarmCb_ = std::move(cb); }

private:
    uint8_t  txAntennas_;
    uint8_t  rxAntennas_;
    uint8_t  activeTxMask_;
    double   dlFreq_MHz_  = 0.0;
    double   ulFreq_MHz_  = 0.0;
    double   txPower_dBm_ = 0.0;
    bool     initialised_ = false;
    AlarmCb  alarmCb_;

    std::atomic<HardwareStatus> hwStatus_{HardwareStatus::OFFLINE};

    bool checkFrequencyRange(double freq_MHz) const;
    void raiseAlarm(HardwareStatus s, const std::string& msg);
};

}  // namespace rbs::hal

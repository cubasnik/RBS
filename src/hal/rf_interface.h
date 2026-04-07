#pragma once
#include "../common/types.h"
#include <functional>

namespace rbs::hal {

// ────────────────────────────────────────────────────────────────
// Pure-virtual RF hardware interface
// All concrete radio front-ends must implement this contract.
// ────────────────────────────────────────────────────────────────
class IRFHardware {
public:
    virtual ~IRFHardware() = default;

    // Lifecycle
    virtual bool  initialise()  = 0;
    virtual bool  selfTest()    = 0;
    virtual void  shutdown()    = 0;

    // Frequency control
    virtual bool  setDlFrequency(double freq_MHz) = 0;
    virtual bool  setUlFrequency(double freq_MHz) = 0;

    // Power control
    virtual bool  setTxPower(double dBm) = 0;
    virtual double getTxPower() const    = 0;

    // Transmit / Receive (synchronous, for simulation)
    virtual bool  transmit(const ByteBuffer& iqSamples) = 0;
    virtual bool  receive (ByteBuffer& iqSamples, uint32_t numSamples) = 0;

    // Status & monitoring
    virtual RFStatus getStatus() const = 0;

    // Antenna control (for MIMO / diversity)
    virtual uint8_t numTxAntennas() const = 0;
    virtual uint8_t numRxAntennas() const = 0;
    virtual bool    setActiveTxAntennas(uint8_t mask) = 0;

    // Callback: called when hardware raises an alarm
    using AlarmCb = std::function<void(HardwareStatus, const std::string&)>;
    virtual void setAlarmCallback(AlarmCb cb) = 0;
};

}  // namespace rbs::hal

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <chrono>

namespace rbs {

// ────────────────────────────────────────────────────────────────
// Radio Access Technology
// ────────────────────────────────────────────────────────────────
enum class RAT : uint8_t {
    GSM  = 0,   // 2G – TDMA/FDMA
    UMTS = 1,   // 3G – WCDMA
    LTE  = 2,   // 4G – OFDMA/SC-FDMA
};

inline const char* ratToString(RAT r) {
    switch (r) {
        case RAT::GSM:  return "GSM";
        case RAT::UMTS: return "UMTS";
        case RAT::LTE:  return "LTE";
    }
    return "UNKNOWN";
}

// ────────────────────────────────────────────────────────────────
// Identifiers
// ────────────────────────────────────────────────────────────────
using CellId   = uint32_t;
using RNTI     = uint16_t;  // LTE Radio Network Temporary Identifier
using IMSI     = uint64_t;  // International Mobile Subscriber Identity
using ARFCN    = uint16_t;  // GSM Absolute Radio Frequency Channel Number
using UARFCN   = uint32_t;  // UMTS Absolute Radio Frequency Channel Number
using EARFCN   = uint32_t;  // LTE Absolute Radio Frequency Channel Number
using ScrCode  = uint16_t;  // UMTS scrambling code

// ────────────────────────────────────────────────────────────────
// Frequency & Power
// ────────────────────────────────────────────────────────────────
struct Frequency {
    double dl_MHz;   // Downlink centre frequency [MHz]
    double ul_MHz;   // Uplink centre frequency [MHz]
};

struct TxPower {
    double dBm;      // Transmit power [dBm]
};

// ────────────────────────────────────────────────────────────────
// GSM specific
// ────────────────────────────────────────────────────────────────
constexpr uint8_t  GSM_SLOTS_PER_FRAME   = 8;
constexpr uint8_t  GSM_FRAMES_PER_MULTIFRAME = 26;
constexpr double   GSM_SLOT_DURATION_US  = 576.9;  // ~576.9 µs
constexpr double   GSM_FRAME_DURATION_MS = 4.615;  // ~4.615 ms

enum class GSMBand : uint8_t { P900, E900, DCS1800, PCS1900 };
enum class GSMChannelType : uint8_t { BCCH, CCCH, SDCCH, TCH_F, TCH_H, SACCH, FACCH };

struct GSMBurst {
    uint8_t  timeSlot;
    uint8_t  frameNumber;
    std::array<uint8_t, 148> bits;  // Normal burst: 148 bits
};

// ────────────────────────────────────────────────────────────────
// UMTS specific
// ────────────────────────────────────────────────────────────────
constexpr uint32_t UMTS_CHIP_RATE        = 3'840'000;  // 3.84 Mcps
constexpr uint32_t UMTS_SLOTS_PER_FRAME  = 15;
constexpr double   UMTS_SLOT_DURATION_US = 666.7;
constexpr double   UMTS_FRAME_DURATION_MS= 10.0;

enum class UMTSBand : uint8_t { B1=1, B2, B4, B5, B8 };
enum class UMTSChannelType : uint8_t { BCH, FACH, RACH, PCH, CPICH, SCH, DCH };
enum class SF : uint16_t { SF4=4, SF8=8, SF16=16, SF32=32, SF64=64, SF128=128, SF256=256 };

struct UMTSFrame {
    uint32_t frameNumber;
    uint16_t scramblingCode;
    std::vector<uint8_t> data;
};

// ────────────────────────────────────────────────────────────────
// LTE specific
// ────────────────────────────────────────────────────────────────
constexpr uint32_t LTE_SUBFRAME_DURATION_US = 1'000;   // 1 ms
constexpr uint32_t LTE_SLOT_DURATION_US     =   500;   // 0.5 ms
constexpr uint8_t  LTE_SUBFRAMES_PER_FRAME  =    10;
constexpr uint8_t  LTE_RB_SIZE_SUBCARRIERS  =    12;   // 12 subcarriers per RB
constexpr double   LTE_SUBCARRIER_SPACING_KHZ = 15.0;  // 15 kHz

enum class LTEBand : uint8_t { B1=1, B3=3, B7=7, B8=8, B20=20, B38=38 };
enum class LTEBandwidth : uint8_t { BW1_4=0, BW3, BW5, BW10, BW15, BW20 };

inline uint8_t lteBandwidthToRB(LTEBandwidth bw) {
    static const uint8_t table[] = {6, 15, 25, 50, 75, 100};
    return table[static_cast<uint8_t>(bw)];
}

enum class LTEDuplexMode : uint8_t { FDD, TDD };
enum class LTEChannelType : uint8_t { PBCH, PDCCH, PDSCH, PUCCH, PUSCH, PRACH, PSS, SSS };

struct ResourceBlock {
    uint8_t  rbIndex;           // Physical RB index (0…99)
    uint8_t  mcs;               // Modulation & Coding Scheme (0…28)
    RNTI     rnti;
};

struct LTESubframe {
    uint32_t sfn;               // System Frame Number (0…1023)
    uint8_t  subframeIndex;     // 0…9
    std::vector<ResourceBlock> dlGrants;
    std::vector<ResourceBlock> ulGrants;
};

// ────────────────────────────────────────────────────────────────
// UE Context (common for all RATs)
// ────────────────────────────────────────────────────────────────
enum class UEState : uint8_t { IDLE, CONNECTING, CONNECTED, RELEASING };

struct UEContext {
    RNTI      rnti;
    IMSI      imsi;
    RAT       rat;
    UEState   state;
    double    rsrp_dBm;    // Reference Signal Received Power
    double    sinr_dB;     // Signal-to-Interference+Noise Ratio
    uint32_t  dlThroughput_bps;
    uint32_t  ulThroughput_bps;
    std::chrono::steady_clock::time_point connectedAt;
};

// ────────────────────────────────────────────────────────────────
// Cell Configuration
// ────────────────────────────────────────────────────────────────
struct GSMCellConfig {
    CellId    cellId;
    ARFCN     arfcn;
    GSMBand   band;
    TxPower   txPower;
    uint8_t   bsic;         // Base Station Identity Code (0-63)
    uint16_t  lac;          // Location Area Code
    uint16_t  mcc;          // Mobile Country Code
    uint16_t  mnc;          // Mobile Network Code
};

struct UMTSCellConfig {
    CellId    cellId;
    UARFCN    uarfcn;
    UMTSBand  band;
    TxPower   txPower;
    ScrCode   primaryScrCode;
    uint16_t  lac;
    uint16_t  rac;          // Routing Area Code
    uint16_t  mcc;
    uint16_t  mnc;
};

struct LTECellConfig {
    CellId       cellId;
    EARFCN       earfcn;
    LTEBand      band;
    LTEBandwidth bandwidth;
    LTEDuplexMode duplexMode;
    TxPower      txPower;
    uint16_t     pci;       // Physical Cell Identity (0-503)
    uint8_t      tac;       // Tracking Area Code
    uint16_t     mcc;
    uint16_t     mnc;
    uint8_t      numAntennas;
};

// ────────────────────────────────────────────────────────────────
// RF Status
// ────────────────────────────────────────────────────────────────
enum class HardwareStatus : uint8_t { OK, WARNING, FAULT, OFFLINE };

struct RFStatus {
    HardwareStatus status;
    double  vswr;           // Voltage Standing Wave Ratio
    double  temperature_C;
    double  txPower_dBm;
    double  rxNoiseFigure_dB;
};

// ────────────────────────────────────────────────────────────────
// Utility: raw byte buffer
// ────────────────────────────────────────────────────────────────
using ByteBuffer = std::vector<uint8_t>;

}  // namespace rbs

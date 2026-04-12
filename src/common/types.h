#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <chrono>

namespace rbs {

// ────────────────────────────────────────────────────────────────
// Utility: raw byte buffer (defined early — used by LTE signal structs)
// ────────────────────────────────────────────────────────────────
using ByteBuffer = std::vector<uint8_t>;

// ────────────────────────────────────────────────────────────────
// Radio Access Technology
// ────────────────────────────────────────────────────────────────
enum class RAT : uint8_t {
    GSM  = 0,   // 2G – TDMA/FDMA
    UMTS = 1,   // 3G – WCDMA
    LTE  = 2,   // 4G – OFDMA/SC-FDMA
    NR   = 3,   // 5G – NR (TS 38.211)
};

inline const char* ratToString(RAT r) {
    switch (r) {
        case RAT::GSM:  return "GSM";
        case RAT::UMTS: return "UMTS";
        case RAT::LTE:  return "LTE";
        case RAT::NR:   return "NR";
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
enum class UMTSChannelType : uint8_t { BCH, FACH, RACH, PCH, CPICH, SCH, DCH, HS_DSCH, E_DCH };
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

// TS 36.321 §5.14 / TS 36.300 §10.1: Carrier Aggregation (LTE-A Rel-10)
// Up to 5 component carriers, total max 100 MHz aggregate bandwidth.
constexpr uint8_t CA_MAX_CC = 5;  ///< Rel-10: max 5 DL component carriers

struct ComponentCarrier {
    uint8_t      ccIdx;           ///< 0 = PCC, 1-4 = SCC
    EARFCN       earfcn;
    LTEBandwidth bandwidth;
    uint16_t     pci;             ///< Physical Cell Identity
    bool         active = true;
};

inline uint8_t lteBandwidthToRB(LTEBandwidth bw) {
    static const uint8_t table[] = {6, 15, 25, 50, 75, 100};
    return table[static_cast<uint8_t>(bw)];
}

enum class LTEDuplexMode : uint8_t { FDD, TDD };
enum class LTEChannelType : uint8_t { PBCH, PDCCH, PDSCH, PUCCH, PUSCH, PRACH, PSS, SSS };

// TS 36.211 §5.4: PUCCH format selection
enum class PUCCHFormat : uint8_t {
    FORMAT_1  = 1,  ///< Scheduling Request (1-bit SR presence)
    FORMAT_1A = 2,  ///< 1-bit HARQ ACK/NACK
    FORMAT_2  = 3,  ///< 20-bit CQI/PMI report
};

/// PUCCH feedback from one UE in one UL subframe (decoded by eNB PHY)
struct PUCCHReport {
    RNTI        rnti;
    PUCCHFormat format;
    uint8_t     ackNack;    ///< 0=ACK, 1=NACK  (Format 1a)
    uint8_t     cqiValue;   ///< CQI 1–15       (Format 2)
    bool        srPresent;  ///< true = SR bit   (Format 1)
};

/// SRS (Sounding Reference Signal) from one UE, TS 36.211 §5.5.3
struct SRSReport {
    RNTI       rnti;
    uint8_t    bwConfig;    ///< SRS BW config 0–3 → 8/16/32/64 RBs
    ByteBuffer sequence;    ///< CAZAC/ZC I-samples (comb-2 simplified)
};

// PUCCH/SRS timing constants
constexpr uint32_t LTE_PUCCH_CQI_PERIOD_SF = 40;  ///< CQI report period, ms (TS 36.213 §7.2.2)
constexpr uint32_t LTE_SRS_PERIOD_SF        = 80;  ///< SRS tx period, ms  (TS 36.211 §5.5.3.3)
constexpr uint8_t  LTE_SRS_SUBFRAME_IDX     =  1;  ///< SRS subframe index for FDD
constexpr uint8_t  LTE_PUCCH_EDGE_RB        =  2;  ///< PUCCH uses 2 edge RBs each side

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
    std::vector<PUCCHReport>   pucchReports;  ///< UL control feedback (HARQ/CQI/SR)
    std::vector<SRSReport>     srsReports;    ///< Sounding ref signals (sfIdx==1)
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
    CellId      cellId;
    ARFCN       arfcn;
    GSMBand     band;
    TxPower     txPower;
    uint8_t     bsic;           // Base Station Identity Code (0-63)
    uint16_t    lac;            // Location Area Code
    uint16_t    mcc;            // Mobile Country Code
    uint16_t    mnc;            // Mobile Network Code
    std::string bscAddr;        // BSC address for Abis/OML (empty = simulation only)
    uint16_t    bscPort = 3002; // Abis OML TCP port (IPA default)
};

struct UMTSCellConfig {
    CellId      cellId;
    UARFCN      uarfcn;
    UMTSBand    band;
    TxPower     txPower;
    ScrCode     primaryScrCode;
    uint16_t    lac;
    uint16_t    rac;            // Routing Area Code
    uint16_t    mcc;
    uint16_t    mnc;
    std::string rncAddr;        // RNC address for Iub/NBAP (empty = simulation only)
    uint16_t    rncPort = 25412; // Iub NBAP SCTP port (TS 25.430)
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
    std::string  mmeAddr;   // MME address for S1AP
    uint16_t     mmePort;   // MME SCTP port (default 36412)
    uint16_t     s1uLocalPort = 2152;  // Local UDP port for S1-U GTP-U (TS 29.060 §4)
    // Carrier Aggregation: optional secondary component carriers (TS 36.300 §10.1)
    std::vector<ComponentCarrier> secondaryCCs;  ///< empty = no CA
};

// ────────────────────────────────────────────────────────────────
// 5G NR specific (TS 38.211, TS 38.101, TS 38.473)
// ────────────────────────────────────────────────────────────────

/// NR subcarrier spacing (µ parameter, TS 38.211 §4.2 Table 4.2-1)
enum class NRScs : uint8_t {
    SCS15  = 0,  ///< 15 kHz,  FR1, µ=0 — 1 ms/slot
    SCS30  = 1,  ///< 30 kHz,  FR1, µ=1 — 0.5 ms/slot
    SCS60  = 2,  ///< 60 kHz,  FR1/FR2, µ=2 — 0.25 ms/slot
    SCS120 = 3,  ///< 120 kHz, FR2 (mmWave), µ=3 — 0.125 ms/slot
};

/// Returns the subcarrier spacing in kHz
inline uint16_t nrScsKhz(NRScs s) {
    static const uint16_t table[] = {15, 30, 60, 120};
    return table[static_cast<uint8_t>(s)];
}

/// Slots per 10 ms radio frame (TS 38.211 Table 4.3.2-1)
inline uint8_t nrSlotsPerFrame(NRScs s) {
    static const uint8_t table[] = {10, 20, 40, 80};
    return table[static_cast<uint8_t>(s)];
}

/// NR Synchronization Signal Block (SS/PBCH Block, TS 38.211 §7.4.3)
struct NRSSBlock {
    uint8_t    ssbIdx;        ///< Beam index: 0-7 (FR1) or 0-63 (FR2)
    uint32_t   sfn;           ///< System Frame Number 0-1023
    uint8_t    halfFrame;     ///< 0 = first half-frame, 1 = second
    ByteBuffer pss;           ///< PSS: 127-sample Gold/m-sequence (BPSK)
    ByteBuffer sss;           ///< SSS: 127-sample Gold/m-sequence (BPSK)
    ByteBuffer pbch;          ///< PBCH payload (MIB: 32 bits + 24-bit CRC)
};

/// NR subframe: 1 ms boundary, contains 2^µ slots
struct NRSubframe {
    uint32_t  sfn;            ///< System Frame Number 0-1023
    uint8_t   subframeIdx;    ///< 0-9 within frame
    NRScs     scs;
    std::vector<NRSSBlock> ssBlocks;  ///< SSBs transmitted this subframe (if any)
};

/// NR Cell configuration (gNB-DU side, TS 38.473 §9.3.1.9)
struct NRCellConfig {
    CellId   cellId;
    uint32_t nrArfcn;         ///< NR-ARFCN (TS 38.101-1 §5.4.2)
    NRScs    scs;
    uint8_t  band;            ///< NR operating band (e.g. 1 for n1 FDD, 78 for n78 TDD)
    uint64_t gnbDuId;         ///< gNB-DU ID: 36-bit (TS 38.473 §9.3.1.9)
    uint64_t gnbCuId;         ///< gNB-CU ID: 36-bit
    uint64_t nrCellIdentity;  ///< NCI: 36-bit = gNB-ID || Cell-ID
    uint16_t nrPci;           ///< Physical Cell Identity 0-1007
    uint8_t  ssbPeriodMs = 20;///< SSB periodicity ms: 5/10/20/40/80/160
    uint16_t tac;
    uint16_t mcc;
    uint16_t mnc;
    std::string cuAddr;       ///< gNB-CU F1AP address
    uint16_t    cuPort = 38472; ///< F1AP SCTP port (TS 38.473)
    uint8_t  numTxRx = 2;     ///< Antenna ports
};

// ────────────────────────────────────────────────────────────────
// EN-DC (E-UTRAN NR Dual Connectivity) types — TS 37.340
// ────────────────────────────────────────────────────────────────

/// EN-DC deployment option (TS 37.340 §4)
enum class ENDCOption : uint8_t {
    OPTION_3  = 3,   ///< Split bearer: MN-PDCP, both LTE+NR legs
    OPTION_3A = 30,  ///< SCG bearer:   no PDCP split, NR leg only
    OPTION_3X = 31,  ///< Split bearer: SN-PDCP, both NR+LTE legs
};

/// Bearer type in a dual-connectivity scenario
enum class DCBearerType : uint8_t {
    MCG      = 0,  ///< Master Cell Group bearer (LTE-only leg)
    SCG      = 1,  ///< Secondary Cell Group bearer (NR-only leg, Option 3a)
    SPLIT_MN = 2,  ///< Split bearer with PDCP at MN/eNB (Option 3)
    SPLIT_SN = 3,  ///< Split bearer with PDCP at SN/en-gNB (Option 3x)
};

/// DC bearer configuration exchanged in X2 SgNB Addition
struct DCBearerConfig {
    uint8_t       enbBearerId = 0;  ///< E-RAB/DRB identifier at MN
    DCBearerType  type        = DCBearerType::SCG;
    uint8_t       mcgLegDrbId = 0;  ///< DRB ID for LTE leg (SPLIT_MN / SPLIT_SN)
    uint8_t       scgLegDrbId = 0;  ///< DRB ID for NR leg (SCG / SPLIT_*)
    uint64_t      nrCellId    = 0;  ///< Target NR cell identity (NCI)
    uint16_t      snCrnti     = 0;  ///< C-RNTI assigned by SN (filled in Ack)
};

/// EN-DC run-time configuration (read from [endc] section in rbs.conf)
struct ENDCConfig {
    bool        enabled      = false;          ///< Activate EN-DC when LTE+NR both running
    ENDCOption  option       = ENDCOption::OPTION_3A; ///< Deployment option
    std::string x2Addr       = "127.0.0.1";   ///< X2 address of Secondary Node
    uint16_t    x2Port       = 36422;          ///< X2AP port
    uint8_t     enbBearerId  = 5;              ///< E-RAB ID on Master Node (LTE)
    uint8_t     scgDrbId     = 1;              ///< DRB ID on Secondary Node (NR)
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

}  // namespace rbs
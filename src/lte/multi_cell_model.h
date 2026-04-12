#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rbs::lte {

inline double dbmToMw(double dbm) {
    return std::pow(10.0, dbm / 10.0);
}

inline double mwToDbm(double mw) {
    return 10.0 * std::log10((std::max)(mw, 1e-12));
}

// Simplified path-loss + co-channel interference model for dense macro layout.
inline double estimateSinrDb(double distanceMeters, uint32_t interfererCount,
                             double txPowerDbm = 43.0, double noiseFloorDbm = -101.0) {
    const double d = (std::max)(distanceMeters, 1.0);
    const double pathLossDb = 32.4 + 35.0 * std::log10(d / 1000.0);
    const double signalDbm = txPowerDbm - pathLossDb;

    double interfMw = 0.0;
    for (uint32_t i = 0; i < interfererCount; ++i) {
        const double scale = 1.0 + 0.35 * static_cast<double>(i + 1);
        const double interfDbm = txPowerDbm - (32.4 + 35.0 * std::log10((d * scale) / 1000.0));
        interfMw += dbmToMw(interfDbm);
    }

    const double noiseMw = dbmToMw(noiseFloorDbm);
    const double sinrMw = dbmToMw(signalDbm) / (std::max)(interfMw + noiseMw, 1e-12);
    return 10.0 * std::log10(sinrMw);
}

inline uint8_t sinrToCqi(double sinrDb) {
    const int raw = static_cast<int>(std::lround((sinrDb + 6.0) / 1.8));
    return static_cast<uint8_t>((std::clamp)(raw, 1, 15));
}

}  // namespace rbs::lte

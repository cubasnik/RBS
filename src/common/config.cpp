#include "config.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace rbs {

// ────────────────────────────────────────────────────────────────
// File loader (minimal INI parser)
// ────────────────────────────────────────────────────────────────
bool Config::loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        RBS_LOG_ERROR("Config", "Cannot open config file: ", path);
        return false;
    }

    std::string currentSection = "global";
    std::string line;
    while (std::getline(f, line)) {
        // Strip inline comments and whitespace
        auto commentPos = line.find('#');
        if (commentPos != std::string::npos) line.erase(commentPos);
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        auto last = line.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) line.erase(last + 1);

        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            // Normalise to lower-case
            std::transform(currentSection.begin(), currentSection.end(),
                           currentSection.begin(), ::tolower);
        } else {
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            std::string key = line.substr(0, eqPos);
            std::string val = line.substr(eqPos + 1);
            // trim key / val
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t"));
                auto e = s.find_last_not_of(" \t");
                if (e != std::string::npos) s.erase(e + 1);
            };
            trim(key); trim(val);
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            data_[currentSection][key] = val;
        }
    }
    RBS_LOG_INFO("Config", "Loaded configuration from ", path);
    return true;
}

// ────────────────────────────────────────────────────────────────
// Internal lookup
// ────────────────────────────────────────────────────────────────
std::optional<std::string> Config::find(const std::string& section,
                                        const std::string& key) const {
    auto sit = data_.find(section);
    if (sit == data_.end()) return std::nullopt;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return std::nullopt;
    return kit->second;
}

// ────────────────────────────────────────────────────────────────
// Typed getters
// ────────────────────────────────────────────────────────────────
std::string Config::getString(const std::string& s, const std::string& k,
                               const std::string& def) const {
    auto v = find(s, k);
    return v ? *v : def;
}

int Config::getInt(const std::string& s, const std::string& k, int def) const {
    auto v = find(s, k);
    if (!v) return def;
    try { return std::stoi(*v); } catch (...) { return def; }
}

double Config::getDouble(const std::string& s, const std::string& k, double def) const {
    auto v = find(s, k);
    if (!v) return def;
    try { return std::stod(*v); } catch (...) { return def; }
}

bool Config::getBool(const std::string& s, const std::string& k, bool def) const {
    auto v = find(s, k);
    if (!v) return def;
    std::string lc = *v;
    std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
    return (lc == "true" || lc == "1" || lc == "yes" || lc == "on");
}

// ────────────────────────────────────────────────────────────────
// Cell config builders
// ────────────────────────────────────────────────────────────────
GSMCellConfig Config::buildGSMConfig() const {
    GSMCellConfig c{};
    c.cellId   = static_cast<CellId>(getInt   ("gsm", "cell_id",  1));
    c.arfcn    = static_cast<ARFCN> (getInt   ("gsm", "arfcn",   60));
    c.band     = GSMBand::DCS1800;
    c.txPower  = {getDouble("gsm", "tx_power_dbm", 43.0)};
    c.bsic     = static_cast<uint8_t>(getInt  ("gsm", "bsic",    10));
    c.lac      = static_cast<uint16_t>(getInt ("gsm", "lac",   1000));
    c.mcc      = static_cast<uint16_t>(getInt ("gsm", "mcc",    250));
    c.mnc      = static_cast<uint16_t>(getInt ("gsm", "mnc",      1));
    return c;
}

UMTSCellConfig Config::buildUMTSConfig() const {
    UMTSCellConfig c{};
    c.cellId         = static_cast<CellId>  (getInt   ("umts", "cell_id",       2));
    c.uarfcn         = static_cast<UARFCN>  (getInt   ("umts", "uarfcn",    10700));
    c.band           = UMTSBand::B1;
    c.txPower        = {getDouble("umts", "tx_power_dbm", 43.0)};
    c.primaryScrCode = static_cast<ScrCode> (getInt   ("umts", "psc",          64));
    c.lac            = static_cast<uint16_t>(getInt   ("umts", "lac",        1000));
    c.rac            = static_cast<uint16_t>(getInt   ("umts", "rac",           1));
    c.mcc            = static_cast<uint16_t>(getInt   ("umts", "mcc",         250));
    c.mnc            = static_cast<uint16_t>(getInt   ("umts", "mnc",           1));
    return c;
}

LTECellConfig Config::buildLTEConfig() const {
    LTECellConfig c{};
    c.cellId      = static_cast<CellId>  (getInt   ("lte", "cell_id",     3));
    c.earfcn      = static_cast<EARFCN>  (getInt   ("lte", "earfcn",   1800));
    c.band        = LTEBand::B3;
    c.bandwidth   = LTEBandwidth::BW20;
    c.duplexMode  = LTEDuplexMode::FDD;
    c.txPower     = {getDouble("lte", "tx_power_dbm", 43.0)};
    c.pci         = static_cast<uint16_t>(getInt   ("lte", "pci",       300));
    c.tac         = static_cast<uint8_t> (getInt   ("lte", "tac",         1));
    c.mcc         = static_cast<uint16_t>(getInt   ("lte", "mcc",       250));
    c.mnc         = static_cast<uint16_t>(getInt   ("lte", "mnc",         1));
    c.numAntennas = static_cast<uint8_t> (getInt   ("lte", "num_antennas", 2));
    return c;
}

}  // namespace rbs

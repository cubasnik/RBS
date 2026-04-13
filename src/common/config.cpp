#include "config.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace rbs {

std::string Config::normalize(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return s;
}

// ────────────────────────────────────────────────────────────────
// File loader (minimal INI parser)
// ────────────────────────────────────────────────────────────────
bool Config::loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        RBS_LOG_ERROR("Config", "Cannot open config file: ", path);
        return false;
    }

    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> parsed;

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
            currentSection = normalize(line.substr(1, line.size() - 2));
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
            key = normalize(key);
            parsed[currentSection][key] = val;
        }
    }
    {
        std::unique_lock<std::shared_mutex> lk(mtx_);
        data_.swap(parsed);
    }
    RBS_LOG_INFO("Config", "Loaded configuration from ", path);
    return true;
}

void Config::setString(const std::string& section,
                       const std::string& key,
                       const std::string& value) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    data_[normalize(section)][normalize(key)] = value;
}

// ────────────────────────────────────────────────────────────────
// Internal lookup
// ────────────────────────────────────────────────────────────────
std::optional<std::string> Config::find(const std::string& section,
                                        const std::string& key) const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    const std::string sectionNorm = normalize(section);
    const std::string keyNorm = normalize(key);
    auto sit = data_.find(sectionNorm);
    if (sit == data_.end()) return std::nullopt;
    auto kit = sit->second.find(keyNorm);
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
    std::transform(lc.begin(), lc.end(), lc.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
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
    c.bscAddr  = getString("gsm", "bsc_addr", "");
    c.bscPort  = static_cast<uint16_t>(getInt ("gsm", "bsc_port", 3002));
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
    c.rncAddr        = getString("umts", "rnc_addr", "");
    c.rncPort        = static_cast<uint16_t>(getInt   ("umts", "rnc_port",  25412));
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
    c.mmeAddr     = getString("lte", "mme_addr", "127.0.0.1");
    c.mmePort     = static_cast<uint16_t>(getInt("lte", "mme_port", 36412));
    return c;
}

NRCellConfig Config::buildNRConfig() const {
    NRCellConfig c{};
    c.cellId          = static_cast<CellId>  (getInt   ("nr", "cell_id",       4));
    c.nrArfcn         = static_cast<uint32_t>(getInt   ("nr", "nr_arfcn",  428000)); // n1 FDD DL 2140 MHz
    c.scs             = NRScs::SCS15;
    c.band            = static_cast<uint8_t> (getInt   ("nr", "band",            1));
    c.gnbDuId         = static_cast<uint64_t>(getInt   ("nr", "gnb_du_id",       1));
    c.gnbCuId         = static_cast<uint64_t>(getInt   ("nr", "gnb_cu_id",       1));
    c.nrCellIdentity  = static_cast<uint64_t>(getInt   ("nr", "nr_cell_id",      1));
    c.nrPci           = static_cast<uint16_t>(getInt   ("nr", "pci",           400));
    c.ssbPeriodMs     = static_cast<uint8_t> (getInt   ("nr", "ssb_period_ms",  20));
    c.tac             = static_cast<uint16_t>(getInt   ("nr", "tac",             1));
    c.mcc             = static_cast<uint16_t>(getInt   ("nr", "mcc",           250));
    c.mnc             = static_cast<uint16_t>(getInt   ("nr", "mnc",             1));
    c.cuAddr          = getString("nr", "cu_addr", "127.0.0.1");
    c.cuPort          = static_cast<uint16_t>(getInt   ("nr", "cu_port",      38472));
    c.numTxRx         = static_cast<uint8_t> (getInt   ("nr", "num_antennas",    4));
    return c;
}

ENDCConfig Config::buildENDCConfig() const {
    ENDCConfig c{};
    c.enabled     = getBool  ("endc", "enabled",       false);
    c.x2Addr      = getString("endc", "x2_addr",       "127.0.0.1");
    c.x2Port      = static_cast<uint16_t>(getInt("endc", "x2_port",    36422));
    c.enbBearerId = static_cast<uint8_t> (getInt("endc", "enb_bearer_id", 5));
    c.scgDrbId    = static_cast<uint8_t> (getInt("endc", "scg_drb_id",    1));

    const std::string opt = getString("endc", "option", "3a");
    if      (opt == "3")  c.option = ENDCOption::OPTION_3;
    else if (opt == "3x") c.option = ENDCOption::OPTION_3X;
    else                  c.option = ENDCOption::OPTION_3A;  // default

    return c;
}

}  // namespace rbs

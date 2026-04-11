#pragma once
#include "types.h"
#include <string>
#include <unordered_map>
#include <optional>

namespace rbs {

// Centralised run-time configuration store.
// Values are loaded from an INI-style file (key=value, sections [section]).
class Config {
public:
    static Config& instance() {
        static Config inst;
        return inst;
    }

    bool loadFile(const std::string& path);

    // ── typed getters (return default if key missing) ──────────────
    std::string getString(const std::string& section,
                          const std::string& key,
                          const std::string& def = "") const;

    int         getInt   (const std::string& section,
                          const std::string& key,
                          int                def = 0) const;

    double      getDouble(const std::string& section,
                          const std::string& key,
                          double             def = 0.0) const;

    bool        getBool  (const std::string& section,
                          const std::string& key,
                          bool               def = false) const;

    // ── convenience: build cell configs from loaded values ─────────
    GSMCellConfig  buildGSMConfig()  const;
    UMTSCellConfig buildUMTSConfig() const;
    LTECellConfig  buildLTEConfig()  const;
    NRCellConfig   buildNRConfig()   const;
    ENDCConfig     buildENDCConfig() const;

private:
    Config() = default;
    // storage:  section → key → value
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> data_;

    std::optional<std::string> find(const std::string& section,
                                    const std::string& key) const;
};

}  // namespace rbs

#pragma once
#include "link_controller.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace rbs {

// ─────────────────────────────────────────────────────────────────────────────
// LinkEntry — все данные об одном интерфейсе (Abis/Iub/S1).
// ─────────────────────────────────────────────────────────────────────────────
struct LinkEntry {
    std::string     name;       ///< "abis" | "iub" | "s1"
    std::string     rat;        ///< "GSM" | "UMTS" | "LTE"
    std::string     peerAddr;   ///< BSC/RNC/MME адрес из конфига
    uint16_t        peerPort = 0;
    LinkController* ctrl     = nullptr;  ///< trace + block (owned by the link object)

    // ── Lifecycle callbacks ───────────────────────────────────────────────────
    std::function<bool()>                     isConnected;
    std::function<void()>                     reconnect;
    std::function<void()>                     disconnect;

    // ── Inject callbacks ──────────────────────────────────────────────────────
    /// Returns list of injectable procedure names for this link.
    std::function<std::vector<std::string>()> injectableProcs;
    /// Triggers a pre-canned procedure by name; returns false if unknown.
    std::function<bool(const std::string&)>   injectProcedure;
};

// ─────────────────────────────────────────────────────────────────────────────
// LinkRegistry — singleton реестр всех сетевых интерфейсов БС.
//
// Стеки (GSMStack, UMTSStack, LTEStack) регистрируют свои линки в конструкторе.
// REST API читает реестр для отображения статуса и управления.
// ─────────────────────────────────────────────────────────────────────────────
class LinkRegistry {
public:
    static LinkRegistry& instance() {
        static LinkRegistry inst;
        return inst;
    }

    void registerLink(LinkEntry entry);
    void unregisterLink(const std::string& name);

    /// nullptr if not found.
    const LinkEntry* getLink(const std::string& name) const;
    LinkEntry*       getLink(const std::string& name);

    /// All registered links (order not guaranteed).
    std::vector<const LinkEntry*> allLinks() const;

private:
    LinkRegistry() = default;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, LinkEntry> links_;
};

} // namespace rbs

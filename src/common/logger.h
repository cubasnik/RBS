#pragma once
#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <cctype>
#include <filesystem>
#include <atomic>
#include <thread>

// ── Windows headers (must come before enum definitions) ──────────────────────
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
// NOGDI prevents wingdi.h which defines ERROR=0, conflicting with our enum.
#  ifndef NOGDI
#    define NOGDI
#  endif
#  include <windows.h>
#endif

namespace rbs {

// Make sure Windows SDK macros don't poison our enum names.
#ifdef ERROR
#  undef ERROR
#endif
#ifdef DEBUG
#  undef DEBUG
#endif

enum class LogLevel : uint8_t { DBG = 0, INFO, WARNING, ERR, CRITICAL };

// ── ANSI colour helpers ───────────────────────────────────────────────────────

// One-time setup: enable ANSI Virtual Terminal Processing on Windows.
inline void enableConsoleColours() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        SetConsoleOutputCP(CP_UTF8);  // render UTF-8 chars (–, →, …) correctly
        SetConsoleCP(CP_UTF8);
        done = true;
    }
#endif
}

// ANSI escape codes as inline functions (avoids constexpr linkage issues).
inline const char* ansiReset()     { return "\033[0m";  }
inline const char* ansiWhite()     { return "\033[97m"; }
inline const char* ansiYellow()    { return "\033[93m"; }
inline const char* ansiGreen()     { return "\033[92m"; }
inline const char* ansiCyan()      { return "\033[96m"; }
inline const char* ansiRed()       { return "\033[91m"; }
inline const char* ansiMagenta()   { return "\033[95m"; }
inline const char* ansiLightBlue() { return "\033[38;5;153m"; }
inline const char* ansiDarkMaroon(){ return "\033[38;5;88m"; }
inline const char* ansiBgYellow()  { return "\033[43m";  }
inline const char* ansiBgRed()     { return "\033[41m";  }

// Wrap tokens in green:
//  - version strings:  v1.0.0
//  - RAT generation:   2G / 3G / 4G
//  - freq with unit:   947 MHz / 902 MHz
//  - bare numbers:     60, 300, 1, 0, 43.0
// All other text uses baseColour.
inline std::string colouriseNumbers(const std::string& text, const char* baseColour) {
    std::string result;
    result += baseColour;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        // Version string: v<digits>[.<digits>]*  e.g. v1.0.0
        if (text[i] == 'v' && i + 1 < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
            result += ansiGreen();
            result += text[i++]; // 'v'
            while (i < text.size()) {
                bool dot = (text[i] == '.' && i + 1 < text.size() &&
                            std::isdigit(static_cast<unsigned char>(text[i + 1])));
                if (std::isdigit(static_cast<unsigned char>(text[i])) || dot)
                    result += text[i++];
                else
                    break;
            }
            result += baseColour;
        }
        // Digit run  →  check suffix: G (2G/3G/4G)  or  " MHz"
        else if (std::isdigit(c)) {
            result += ansiGreen();
            while (i < text.size()) {
                bool dot = (text[i] == '.' && i + 1 < text.size() &&
                            std::isdigit(static_cast<unsigned char>(text[i + 1])));
                if (std::isdigit(static_cast<unsigned char>(text[i])) || dot)
                    result += text[i++];
                else
                    break;
            }
            // "G" suffix  (2G, 3G, 4G)
            if (i < text.size() && text[i] == 'G') {
                result += text[i++];
            }
            // " MHz" suffix
            else if (i + 4 <= text.size() && text.substr(i, 4) == " MHz") {
                result += " MHz";
                i += 4;
            }
            result += baseColour;
        }
        else {
            result += text[i++];
        }
    }
    return result;
}

inline const char* logLevelColour(LogLevel l) {
    switch (l) {
        case LogLevel::DBG:      return "\033[2;37m";  // dim gray
        case LogLevel::INFO:     return "\033[92m";    // bright green
        case LogLevel::WARNING:  return "\033[1;93m";  // bold yellow
        case LogLevel::ERR:      return "\033[1;91m";  // bold red
        case LogLevel::CRITICAL: return "\033[1;97m";  // bold white
    }
    return ansiWhite();
}

// Background colour for WARNING/ERR/CRITICAL lines; empty string for others.
inline const char* logLevelBg(LogLevel l) {
    switch (l) {
        case LogLevel::WARNING:  return ansiBgYellow();
        case LogLevel::ERR:      return ansiBgRed();
        case LogLevel::CRITICAL: return ansiBgRed();
        default:                 return "";
    }
}

// Per-RAT / per-subsystem foreground colour for the [component] token.
inline const char* componentColour(std::string_view comp) {
    if (comp == "GSM")                                       return "\033[32m";  // green
    if (comp == "UMTS" || comp == "NBAP")                   return "\033[36m";  // cyan
    if (comp == "LTE"  || comp == "S1AP" || comp == "X2AP") return "\033[94m";  // bright blue
    if (comp == "NR"   || comp == "F1AP" || comp == "NGAP") return "\033[95m";  // magenta
    if (comp == "RBS")                                       return "\033[97m";  // white
    if (comp == "OMS")                                       return "\033[33m";  // orange
    if (comp == "HAL")                                       return "\033[90m";  // dark gray
    return ansiLightBlue();
}

// ── fmt-style {} placeholder substitution (C++17, no external deps) ─────────
namespace detail {

// Stream val to oss applying optional format spec (e.g. "X", "02X", "x").
template<typename T>
void applySpec(std::ostringstream& oss, std::string_view spec, T&& val) {
    // Promote char-sized integer types so they print as numbers, not characters.
    using U = std::decay_t<T>;
    if constexpr (std::is_same_v<U, char> ||
                  std::is_same_v<U, signed char> ||
                  std::is_same_v<U, unsigned char>) {
        applySpec(oss, spec, static_cast<int>(val));
        return;
    }
    if (spec.empty()) { oss << val; return; }
    // Parse: optional '0' fill, optional width digits, then type char.
    char fill  = ' ';
    int  width = 0;
    size_t i   = 0;
    if (spec.size() > 1 && spec[0] == '0') { fill = '0'; i = 1; }
    while (i + 1 < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i])))
        width = width * 10 + (spec[i++] - '0');
    char type = (i < spec.size()) ? spec[i] : '\0';
    if (type == 'X' || type == 'x') {
        if (width > 0) oss << std::setw(width) << std::setfill(fill);
        oss << std::hex;
        if (type == 'X') oss << std::uppercase;
        oss << val;
        oss << std::dec << std::nouppercase << std::setw(0) << std::setfill(' ');
    } else {
        oss << val;
    }
}

// Forward declaration of recursive overload.
template<typename T, typename... Rest>
void buildFmt(std::ostringstream& oss, std::string_view fmt, T&& val, Rest&&... rest);

// Base case: no more args — just append what remains of the format string.
inline void buildFmt(std::ostringstream& oss, std::string_view fmt) { oss << fmt; }

// Recursive case: find next {…} placeholder, substitute val, recurse.
template<typename T, typename... Rest>
void buildFmt(std::ostringstream& oss, std::string_view fmt, T&& val, Rest&&... rest) {
    auto pos = fmt.find('{');
    if (pos == std::string_view::npos) { oss << fmt; return; }
    oss << fmt.substr(0, pos);
    auto end = fmt.find('}', pos + 1);
    if (end == std::string_view::npos) { oss << fmt.substr(pos); return; }
    std::string_view spec = fmt.substr(pos + 1, end - pos - 1);
    if (!spec.empty() && spec[0] == ':') spec = spec.substr(1);
    applySpec(oss, spec, std::forward<T>(val));
    buildFmt(oss, fmt.substr(end + 1), std::forward<Rest>(rest)...);
}

} // namespace detail

// ── Thread-safe singleton logger ─────────────────────────────────────────────
class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void setLevel(LogLevel level) { minLevel_ = level; }

    void setJsonOutput(bool enabled) { jsonOutput_.store(enabled); }
    bool jsonOutputEnabled() const { return jsonOutput_.load(); }

    void setTraceId(const std::string& traceId) { traceIdTls_ = traceId; }
    void clearTraceId() { traceIdTls_.clear(); }
    std::string traceId() const { return traceIdTls_; }

    static std::string makeTraceId(const char* scope, uint64_t key = 0) {
        using namespace std::chrono;
        static std::atomic<uint64_t> seq{0};
        const uint64_t tick = static_cast<uint64_t>(duration_cast<microseconds>(
            system_clock::now().time_since_epoch()).count());
        std::ostringstream oss;
        oss << scope << "-" << std::hex << key << "-" << tick << "-" << seq.fetch_add(1);
        return oss.str();
    }

    void enableFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fileStream_.is_open()) fileStream_.close();
        if (path.empty()) { logPath_.clear(); logBytes_ = 0; return; }
        logPath_  = path;
        logBytes_ = 0;
        if (std::filesystem::exists(path))
            logBytes_ = static_cast<size_t>(std::filesystem::file_size(path));
        fileStream_.open(path, std::ios::app);
    }

    template<typename... Args>
    void log(LogLevel level, const char* component, Args&&... args) {
        if (level < minLevel_) return;

        // Build message body.  If the first argument is a const char* that
        // contains '{', treat it as a {}-placeholder format string and
        // substitute values in order.  Otherwise stream-concatenate all args
        // (legacy comma-separated style).
        std::ostringstream body;
        buildBody_(body, std::forward<Args>(args)...);
        std::string bodyText = body.str();

        std::string ts  = timestamp();
        std::string lv  = levelStr(level);
        std::string tid = traceIdTls_;

        std::string msg;
        if (jsonOutput_.load()) {
            msg = buildJsonLine_(ts, lv, component, bodyText, tid);
        } else {
            msg = ts + " [" + lv + "] [" + component + "] ";
            if (!tid.empty()) {
                msg += "[trace=" + tid + "] ";
            }
            msg += bodyText;
        }

        std::string consoleLine;
        if (jsonOutput_.load()) {
            consoleLine = msg;
        } else {
            enableConsoleColours();

            // Coloured output for the console:
            //   full line bg  → yellow (WARNING) / red (ERR / CRITICAL)
            //   timestamp     → white
            //   [LEVEL]       → dim-gray / bright-green / bold-yellow / bold-red / bold-white
            //   [component]   → per-RAT: GSM=green UMTS=cyan LTE=blue NR=magenta OMS=orange HAL=gray
            //   message body  → light blue + green numbers; plain white on alert bg
            const char* levelBg    = logLevelBg(level);
            const char* compColour = componentColour(component);
            const bool  hasBg      = (levelBg[0] != '\0');

            std::string coloured;
            if (hasBg) coloured += levelBg;       // full-line background

            // Timestamp: white
            coloured += ansiWhite();
            coloured += ts;
            coloured += ansiReset();
            if (hasBg) coloured += levelBg;
            coloured += " ";

            // [LEVEL]: level foreground colour
            coloured += logLevelColour(level);
            coloured += "[";
            coloured += lv;
            coloured += "]";
            coloured += ansiReset();
            if (hasBg) coloured += levelBg;
            coloured += " ";

            // [component]: per-RAT colour
            coloured += compColour;
            coloured += "[";
            coloured += component;
            coloured += "]";
            coloured += ansiReset();
            if (hasBg) coloured += levelBg;
            coloured += " ";

            // Message body
            std::string bodyPart;
            if (!tid.empty()) bodyPart += "[trace=" + tid + "] ";
            bodyPart += bodyText;

            bool specialShutdownLine =
                std::string_view(component) == "RBS" &&
                (bodyText == "Signal 2 received \xe2\x80\x93 initiating shutdown" ||
                 bodyText == "Radio Base Station OFFLINE");

            if (specialShutdownLine) {
                coloured += colouriseNumbers(bodyPart, ansiDarkMaroon());
            } else if (hasBg) {
                // On coloured backgrounds: plain white — keeps text readable
                coloured += ansiWhite();
                coloured += bodyPart;
            } else {
                coloured += colouriseNumbers(bodyPart, ansiLightBlue());
            }
            coloured += ansiReset();
            consoleLine = std::move(coloured);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << consoleLine << "\n";
        if (fileStream_.is_open()) {
            fileStream_ << msg << "\n";
            fileStream_.flush();
            logBytes_ += msg.size() + 1;
            if (logBytes_ >= kMaxLogBytes_) rotateLog_();
        }
    }

private:
    // ── buildBody_ overloads ───────────────────────────────────────────────
    // fmt-style: first arg is const char* that contains '{' → use buildFmt
    template<typename... Rest>
    static void buildBody_(std::ostringstream& oss,
                           const char* fmt, Rest&&... rest) {
        if (std::string_view(fmt).find('{') != std::string_view::npos)
            detail::buildFmt(oss, std::string_view(fmt), std::forward<Rest>(rest)...);
        else {
            oss << fmt;
            int dummy[] = { 0, (oss << rest, 0)... };
            (void)dummy;
        }
    }
    // stream-style: first arg is not const char* → concatenate everything
    template<typename T, typename... Rest>
    static void buildBody_(std::ostringstream& oss, T&& val, Rest&&... rest) {
        oss << val;
        int dummy[] = { 0, (oss << rest, 0)... };
        (void)dummy;
    }
    // No-arg fallback (single message string without extras already handled above)
    static void buildBody_(std::ostringstream&) {}

    Logger() = default;
    LogLevel      minLevel_  = LogLevel::INFO;
    std::mutex    mutex_;
    std::ofstream fileStream_;
    std::string   logPath_;
    size_t        logBytes_  = 0;
    std::atomic<bool> jsonOutput_{false};
    static constexpr size_t kMaxLogBytes_ = 100ULL * 1024 * 1024; // 100 MiB
    inline static thread_local std::string traceIdTls_;

    // Called under mutex_ when file exceeds kMaxLogBytes_.
    void rotateLog_() {
        fileStream_.close();
        std::error_code ec;
        std::filesystem::rename(logPath_, logPath_ + ".1", ec);
        fileStream_.open(logPath_, std::ios::trunc);
        logBytes_ = 0;
    }

    static const char* levelStr(LogLevel l) {
        switch (l) {
            case LogLevel::DBG:      return "DEBUG";
            case LogLevel::INFO:     return "INFO ";
            case LogLevel::WARNING:  return "WARN ";
            case LogLevel::ERR:      return "ERROR";
            case LogLevel::CRITICAL: return "CRIT ";
        }
        return "?????";
    }

    static std::string jsonEscape_(std::string_view src) {
        std::string out;
        out.reserve(src.size() + 8);
        for (char c : src) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:   out += c; break;
            }
        }
        return out;
    }

    static std::string buildJsonLine_(const std::string& ts,
                                      const std::string& level,
                                      const char* component,
                                      const std::string& message,
                                      const std::string& traceId) {
        const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        std::ostringstream oss;
        oss << "{"
            << "\"ts\":\"" << jsonEscape_(ts) << "\","
            << "\"level\":\"" << jsonEscape_(level) << "\","
            << "\"component\":\"" << jsonEscape_(component) << "\","
            << "\"thread\":\"" << std::hex << tid << "\",";
        if (!traceId.empty()) {
            oss << "\"trace_id\":\"" << jsonEscape_(traceId) << "\",";
        }
        oss << "\"message\":\"" << jsonEscape_(message) << "\"}";
        return oss.str();
    }

    static std::string timestamp() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto t   = system_clock::to_time_t(now);
        auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        std::ostringstream oss;
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
};

class ScopedTraceId {
public:
    explicit ScopedTraceId(std::string traceId)
        : hadPrev_(!Logger::instance().traceId().empty())
        , previous_(Logger::instance().traceId()) {
        Logger::instance().setTraceId(traceId);
    }

    ~ScopedTraceId() {
        if (hadPrev_) {
            Logger::instance().setTraceId(previous_);
        } else {
            Logger::instance().clearTraceId();
        }
    }

private:
    bool hadPrev_;
    std::string previous_;
};

#define RBS_TRACE_CONCAT_INNER_(a, b) a##b
#define RBS_TRACE_CONCAT_(a, b) RBS_TRACE_CONCAT_INNER_(a, b)
#define RBS_TRACE_SCOPE(id) rbs::ScopedTraceId RBS_TRACE_CONCAT_(_rbsTraceScope_, __LINE__)(id)
#define RBS_TRACE_SCOPE_AUTO(scope, key) \
    rbs::ScopedTraceId RBS_TRACE_CONCAT_(_rbsTraceScope_, __LINE__)(rbs::Logger::makeTraceId(scope, static_cast<uint64_t>(key)))

// Convenience macros
#define RBS_LOG_DEBUG(comp, ...)    rbs::Logger::instance().log(rbs::LogLevel::DBG,      comp, __VA_ARGS__)
#define RBS_LOG_INFO(comp, ...)     rbs::Logger::instance().log(rbs::LogLevel::INFO,     comp, __VA_ARGS__)
#define RBS_LOG_WARNING(comp, ...)  rbs::Logger::instance().log(rbs::LogLevel::WARNING,  comp, __VA_ARGS__)
#define RBS_LOG_ERROR(comp, ...)    rbs::Logger::instance().log(rbs::LogLevel::ERR,      comp, __VA_ARGS__)
#define RBS_LOG_CRITICAL(comp, ...) rbs::Logger::instance().log(rbs::LogLevel::CRITICAL, comp, __VA_ARGS__)

}  // namespace rbs

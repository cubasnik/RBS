#pragma once
#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <cctype>

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
inline const char* ansiLightBlue() { return "\033[94m"; }

// Wrap every run of digits (incl. decimals like 43.0) in green,
// returning to baseColour for the surrounding text.
inline std::string colouriseNumbers(const std::string& text, const char* baseColour) {
    std::string result;
    result += baseColour;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isdigit(c)) {
            result += ansiGreen();
            while (i < text.size()) {
                unsigned char d = static_cast<unsigned char>(text[i]);
                bool isDecimalDot = (text[i] == '.' &&
                                     i + 1 < text.size() &&
                                     std::isdigit(static_cast<unsigned char>(text[i+1])));
                if (std::isdigit(d) || isDecimalDot)
                    result += text[i++];
                else
                    break;
            }
            result += baseColour;
        } else {
            result += text[i++];
        }
    }
    return result;
}

inline const char* logLevelColour(LogLevel l) {
    switch (l) {
        case LogLevel::DBG:      return ansiCyan();
        case LogLevel::INFO:     return ansiYellow();
        case LogLevel::WARNING:  return ansiMagenta();
        case LogLevel::ERR:      return ansiRed();
        case LogLevel::CRITICAL: return ansiRed();
    }
    return ansiWhite();
}

// ── Thread-safe singleton logger ─────────────────────────────────────────────
class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void setLevel(LogLevel level) { minLevel_ = level; }

    void enableFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        fileStream_.open(path, std::ios::app);
    }

    template<typename... Args>
    void log(LogLevel level, const char* component, Args&&... args) {
        if (level < minLevel_) return;
        enableConsoleColours();

        // Build message body — expand pack into ostringstream one arg at a time.
        std::ostringstream body;
        int dummy[] = { 0, (body << args, 0)... };
        (void)dummy;
        std::string bodyText = body.str();

        // Plain text for the log file (no ANSI escape codes).
        std::string ts  = timestamp();
        std::string lv  = levelStr(level);
        std::string msg = ts + " [" + lv + "] [" + component + "] " + bodyText;

        // Coloured output for the console:
        //   timestamp       → white
        //   [LEVEL]         → level colour (yellow for INFO, etc.)
        //   [component] msg → light blue; numeric values inside → green
        std::string coloured;
        // Timestamp: white
        coloured  = ansiWhite();
        coloured += ts;
        coloured += ansiReset();
        coloured += " ";
        // [LEVEL]: level colour
        coloured += logLevelColour(level);
        coloured += "[";
        coloured += lv;
        coloured += "]";
        coloured += ansiReset();
        coloured += " ";
        // [component] message: light blue with green numbers
        std::string bodyPart = std::string("[") + component + "] " + bodyText;
        coloured += colouriseNumbers(bodyPart, ansiLightBlue());
        coloured += ansiReset();

        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << coloured << "\n";
        if (fileStream_.is_open()) {
            fileStream_ << msg << "\n";
            fileStream_.flush();
        }
    }

private:
    Logger() = default;
    LogLevel      minLevel_ = LogLevel::INFO;
    std::mutex    mutex_;
    std::ofstream fileStream_;

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

// Convenience macros
#define RBS_LOG_DEBUG(comp, ...)    rbs::Logger::instance().log(rbs::LogLevel::DBG,      comp, __VA_ARGS__)
#define RBS_LOG_INFO(comp, ...)     rbs::Logger::instance().log(rbs::LogLevel::INFO,     comp, __VA_ARGS__)
#define RBS_LOG_WARNING(comp, ...)  rbs::Logger::instance().log(rbs::LogLevel::WARNING,  comp, __VA_ARGS__)
#define RBS_LOG_ERROR(comp, ...)    rbs::Logger::instance().log(rbs::LogLevel::ERR,      comp, __VA_ARGS__)
#define RBS_LOG_CRITICAL(comp, ...) rbs::Logger::instance().log(rbs::LogLevel::CRITICAL, comp, __VA_ARGS__)

}  // namespace rbs

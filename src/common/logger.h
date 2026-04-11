#pragma once
#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <cctype>
#include <filesystem>

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
            logBytes_ += msg.size() + 1;
            if (logBytes_ >= kMaxLogBytes_) rotateLog_();
        }
    }

private:
    Logger() = default;
    LogLevel      minLevel_  = LogLevel::INFO;
    std::mutex    mutex_;
    std::ofstream fileStream_;
    std::string   logPath_;
    size_t        logBytes_  = 0;
    static constexpr size_t kMaxLogBytes_ = 100ULL * 1024 * 1024; // 100 MiB

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

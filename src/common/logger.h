#pragma once
#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <iostream>

namespace rbs {

enum class LogLevel : uint8_t { DEBUG = 0, INFO, WARNING, ERROR, CRITICAL };

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
        std::ostringstream oss;
        oss << timestamp() << " [" << levelStr(level) << "] [" << component << "] ";
        (oss << ... << std::forward<Args>(args));
        std::string msg = oss.str();
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << msg << "\n";
        if (fileStream_.is_open()) { fileStream_ << msg << "\n"; fileStream_.flush(); }
    }

private:
    Logger() = default;
    LogLevel    minLevel_  = LogLevel::INFO;
    std::mutex  mutex_;
    std::ofstream fileStream_;

    static const char* levelStr(LogLevel l) {
        switch (l) {
            case LogLevel::DEBUG:    return "DEBUG";
            case LogLevel::INFO:     return "INFO ";
            case LogLevel::WARNING:  return "WARN ";
            case LogLevel::ERROR:    return "ERROR";
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
#define RBS_LOG_DEBUG(comp, ...)    rbs::Logger::instance().log(rbs::LogLevel::DEBUG,    comp, __VA_ARGS__)
#define RBS_LOG_INFO(comp, ...)     rbs::Logger::instance().log(rbs::LogLevel::INFO,     comp, __VA_ARGS__)
#define RBS_LOG_WARNING(comp, ...)  rbs::Logger::instance().log(rbs::LogLevel::WARNING,  comp, __VA_ARGS__)
#define RBS_LOG_ERROR(comp, ...)    rbs::Logger::instance().log(rbs::LogLevel::ERROR,    comp, __VA_ARGS__)
#define RBS_LOG_CRITICAL(comp, ...) rbs::Logger::instance().log(rbs::LogLevel::CRITICAL, comp, __VA_ARGS__)

}  // namespace rbs

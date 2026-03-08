#include "eternal/utils/Logger.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>

namespace eternal {

// ---------------------------------------------------------------------------
// Logger singleton
// ---------------------------------------------------------------------------

Logger& Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::Logger() : output_(&std::cerr) {}

Logger::~Logger() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

void Logger::setLevel(LogLevel level) noexcept {
    min_level_ = level;
}

void Logger::setOutput(const std::string& file_path) {
    std::lock_guard lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
    if (file_path.empty()) {
        output_ = &std::cerr;
        return;
    }
    file_stream_.open(file_path, std::ios::app);
    if (file_stream_.is_open()) {
        output_ = &file_stream_;
    } else {
        output_ = &std::cerr;
    }
}

void Logger::enableColors(bool enable) noexcept {
    colors_ = enable;
}

void Logger::log(LogLevel level, std::string_view message) {
    if (level < min_level_) return;

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&time_t));

    std::lock_guard lock(mutex_);

    if (colors_ && output_ == &std::cerr) {
        *output_ << levelToColor(level)
                 << "[" << time_buf << "." << ([&]{ char buf[8]; std::snprintf(buf, sizeof(buf), "%03d", static_cast<int>(ms.count()) % 1000); return std::string(buf); }()) << "] "
                 << "[" << levelToString(level) << "] "
                 << message
                 << "\033[0m\n";
    } else {
        *output_ << "[" << time_buf << "." << ([&]{ char buf[8]; std::snprintf(buf, sizeof(buf), "%03d", static_cast<int>(ms.count()) % 1000); return std::string(buf); }()) << "] "
                 << "[" << levelToString(level) << "] "
                 << message << "\n";
    }
    output_->flush();
}

std::string_view Logger::levelToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO ";
        case LogLevel::Warn:     return "WARN ";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRIT ";
    }
    return "?????";
}

std::string_view Logger::levelToColor(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:    return "\033[90m";         // dark grey
        case LogLevel::Debug:    return "\033[36m";         // cyan
        case LogLevel::Info:     return "\033[32m";         // green
        case LogLevel::Warn:     return "\033[33m";         // yellow
        case LogLevel::Error:    return "\033[31m";         // red
        case LogLevel::Critical: return "\033[1;31m";       // bold red
    }
    return "\033[0m";
}

} // namespace eternal

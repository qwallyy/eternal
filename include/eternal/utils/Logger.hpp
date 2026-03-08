#pragma once

#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace eternal {

// ---------------------------------------------------------------------------
// Log levels
// ---------------------------------------------------------------------------

#ifndef ETERNAL_LOG_LEVEL_DEFINED
#define ETERNAL_LOG_LEVEL_DEFINED
enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};
#endif

// ---------------------------------------------------------------------------
// Logger singleton
// ---------------------------------------------------------------------------

class Logger {
public:
    static Logger& instance();

    // Non-copyable, non-movable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Set the minimum log level that will be emitted.
    void setLevel(LogLevel level) noexcept;

    /// Current minimum log level.
    [[nodiscard]] LogLevel level() const noexcept { return min_level_; }

    /// Redirect log output to a file. Pass empty string to revert to stderr.
    void setOutput(const std::string& file_path);

    /// Enable or disable ANSI colour codes in output.
    void enableColors(bool enable) noexcept;

    /// Core logging function (pre-formatted message).
    void log(LogLevel level, std::string_view message);

    /// Variadic fmt-style logging.
    template <typename... Args>
    void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        if (level < min_level_) return;
        log(level, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    Logger();
    ~Logger();

    static std::string_view levelToString(LogLevel level) noexcept;
    static std::string_view levelToColor(LogLevel level) noexcept;

    LogLevel      min_level_ = LogLevel::Info;
    bool          colors_    = true;
    std::mutex    mutex_;
    std::ofstream file_stream_;
    std::ostream* output_ = nullptr;   // points to stderr or file_stream_
};

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------

#define LOG_TRACE(...) ::eternal::Logger::instance().log(::eternal::LogLevel::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) ::eternal::Logger::instance().log(::eternal::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::eternal::Logger::instance().log(::eternal::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::eternal::Logger::instance().log(::eternal::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::eternal::Logger::instance().log(::eternal::LogLevel::Error, __VA_ARGS__)
#define LOG_CRIT(...)  ::eternal::Logger::instance().log(::eternal::LogLevel::Critical, __VA_ARGS__)

} // namespace eternal

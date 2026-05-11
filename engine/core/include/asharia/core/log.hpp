#pragma once

#include <cstdint>
#include <source_location>
#include <string_view>

namespace asharia {

enum class LogLevel : std::uint8_t {
    Trace,
    Info,
    Warning,
    Error,
};

void log(
    LogLevel level,
    std::string_view message,
    const std::source_location& location = std::source_location::current());

inline void logTrace(
    std::string_view message,
    const std::source_location& location = std::source_location::current()) {
    log(LogLevel::Trace, message, location);
}

inline void logInfo(
    std::string_view message,
    const std::source_location& location = std::source_location::current()) {
    log(LogLevel::Info, message, location);
}

inline void logWarning(
    std::string_view message,
    const std::source_location& location = std::source_location::current()) {
    log(LogLevel::Warning, message, location);
}

inline void logError(
    std::string_view message,
    const std::source_location& location = std::source_location::current()) {
    log(LogLevel::Error, message, location);
}

} // namespace asharia

#include "vke/core/log.hpp"

#include <iostream>
#include <mutex>

namespace vke {
    namespace {

        std::mutex& logMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::string_view levelName(LogLevel level) {
            switch (level) {
            case LogLevel::Trace:
                return "trace";
            case LogLevel::Info:
                return "info";
            case LogLevel::Warning:
                return "warning";
            case LogLevel::Error:
                return "error";
            }

            return "unknown";
        }

    } // namespace

    void log(LogLevel level, std::string_view message, const std::source_location& location) {
        std::scoped_lock lock{logMutex()};

        auto& stream = level == LogLevel::Error ? std::cerr : std::cout;
        stream << '[' << levelName(level) << "] " << message << " (" << location.file_name() << ':'
               << location.line() << ")\n";
    }

} // namespace vke

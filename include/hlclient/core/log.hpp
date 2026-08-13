#pragma once

#include <string_view>

namespace hlclient::core {

enum class LogLevel {
    trace,
    debug,
    info,
    warning,
    error,
    fatal,
};

void initialize_logging(LogLevel minimum_level = LogLevel::info) noexcept;
void log(LogLevel level, std::string_view message) noexcept;

} // namespace hlclient::core

#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <cstddef>

namespace hlclient::core {

enum class LogLevel {
    trace,
    debug,
    info,
    warning,
    error,
    fatal,
};

// Delivery purpose is independent of severity. Ordinary technical diagnostics
// stay in the existing report/terminal sink; only explicit useful notices and
// inert server text enter scrollback. Warnings/errors remain visible regardless
// of channel, so routing never hides actionable failures.
enum class LogChannel {
    diagnostic,
    user_notice,
    server_text,
    game_event,
};

void initialize_logging(LogLevel minimum_level = LogLevel::info) noexcept;
void log(LogLevel level, std::string_view message,
         LogChannel channel = LogChannel::diagnostic) noexcept;
// Inert untrusted display text: bounded UTF-8 repair, control filtering,
// per-line prefixing and sensitive-token redaction before either sink.
// Never accepts commands or emits an unprefixed protocol/status marker.
void log_server_text(std::string_view message,LogLevel level=LogLevel::info) noexcept;
void log_game_event(std::string_view message) noexcept;

struct ConsoleLogRecord { LogLevel level; std::string message; LogChannel channel{LogChannel::diagnostic}; };
struct ConsoleLogBatch {
    std::vector<ConsoleLogRecord> records;
    std::size_t dropped_lines{};
};
// A bounded producer queue, drained by the application update thread. It never
// invokes GUI callbacks or renderer work while holding the logging mutex.
[[nodiscard]] ConsoleLogBatch drain_console_log(std::size_t maximum_records = 128) noexcept;
void reset_console_log() noexcept;

} // namespace hlclient::core

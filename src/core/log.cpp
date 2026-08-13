#include <hlclient/core/log.hpp>

#include <array>
#include <atomic>
#include <iostream>
#include <mutex>

namespace hlclient::core {
namespace {

std::atomic<LogLevel> minimum_level{LogLevel::info};
std::mutex output_mutex;

[[nodiscard]] constexpr std::string_view level_name(const LogLevel level) noexcept
{
    constexpr std::array names{
        std::string_view{"trace"},
        std::string_view{"debug"},
        std::string_view{"info"},
        std::string_view{"warning"},
        std::string_view{"error"},
        std::string_view{"fatal"},
    };
    return names[static_cast<std::size_t>(level)];
}

} // namespace

void initialize_logging(const LogLevel requested_minimum_level) noexcept
{
    minimum_level.store(requested_minimum_level, std::memory_order_relaxed);
}

void log(const LogLevel level, const std::string_view message) noexcept
{
    if (level < minimum_level.load(std::memory_order_relaxed)) {
        return;
    }

    try {
        const std::scoped_lock lock{output_mutex};
        std::clog << '[' << level_name(level) << "] " << message << '\n';
    } catch (...) {
        // Logging must never make shutdown or error handling fail.
    }
}

} // namespace hlclient::core

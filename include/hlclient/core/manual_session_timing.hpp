#pragma once

#include <cstdint>
#include <optional>

namespace hlclient::core {
inline constexpr std::uint32_t kMaximumManualSessionSeconds = 86'400U;

// Pure host timing policy. nullopt means user-ended gameplay, never an
// unbounded startup. No clocks, process handles or game rules are owned here.
[[nodiscard]] constexpr bool manual_client_wait_expired(
    const std::uint64_t elapsed_ms, const bool runtime_observed,
    const std::optional<std::uint32_t> duration_seconds) noexcept
{
    if (!runtime_observed) return elapsed_ms >= 60'000U;
    return duration_seconds &&
        elapsed_ms >= (static_cast<std::uint64_t>(*duration_seconds) + 60U) * 1'000U;
}
} // namespace hlclient::core

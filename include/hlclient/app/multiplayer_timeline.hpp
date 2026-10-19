#pragma once

#include <hlclient/core/multiplayer_diagnostics.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace hlclient::app {
struct MultiplayerTimelineStatistics final {
    std::uint64_t observed{}, accepted{}, invalid{}, duplicates{}, stale_generation{},
        sampled_out{}, recent_evictions{}, exact_evictions{}, manual_omissions{},
        manual_marks{}, manual_marks_dropped{}, allocation_failures{}, coverage_omissions{};
    std::array<std::uint64_t,10> payload_observations{};
    std::array<std::uint64_t,static_cast<std::size_t>(core::MultiplayerDiagnosticProducer::count)> producer_observations{};
};
// A component of MovementIncidentRecorder, never an independent logger. All
// buffers are allocated once at opt-in; collection does not serialize or read
// clocks. Encoding is terminal/offline work bounded by bytes and a deadline.
class MultiplayerTimeline final {
public:
    static constexpr std::size_t maximum_recent_events=1280, maximum_exact_events=512;
    static constexpr std::size_t maximum_action_exact_events=384, maximum_chain_exact_events=128;
    static constexpr std::size_t maximum_manual_windows=8, maximum_manual_events=64;
    static constexpr std::size_t maximum_capacity_bytes=2U*1024U*1024U;
    static constexpr std::size_t maximum_encoded_bytes=2U*1024U*1024U;
    static constexpr std::uint64_t pre_window_ns=4'000'000'000ULL, post_window_ns=1'000'000'000ULL;
    static constexpr std::uint64_t background_interval_ns=50'000'000ULL, manual_interval_ns=125'000'000ULL;
    static constexpr std::uint64_t encoding_deadline_ms=100;
    explicit MultiplayerTimeline(bool enabled=false) noexcept;
    ~MultiplayerTimeline();
    MultiplayerTimeline(const MultiplayerTimeline&)=delete;
    MultiplayerTimeline& operator=(const MultiplayerTimeline&)=delete;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool observe(const core::MultiplayerDiagnosticEvent&) noexcept;
    void set_clock_origin_ns(std::uint64_t) noexcept;
    [[nodiscard]] bool mark_manual_relative_us(std::uint64_t) noexcept;
    [[nodiscard]] MultiplayerTimelineStatistics statistics() const noexcept;
    [[nodiscard]] std::size_t capacity_bytes() const noexcept;
    [[nodiscard]] std::optional<std::string> encode(std::size_t maximum_bytes=maximum_encoded_bytes) const;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
    bool requested_{};
    std::uint64_t allocation_failures_{};
};
} // namespace hlclient::app

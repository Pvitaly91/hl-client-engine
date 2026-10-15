#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace hlclient::app {
struct MovementFrameRecord;

// Exact application observations, separate from the bounded detailed record.
// Unknown is not treated as focused, alive, or eligible for local control.
struct MovementFrameContext final {
    std::optional<bool> focused,captured,alive,control_active;
    std::optional<std::uint64_t> life_epoch;
};
struct MovementSessionStatistics final {
    std::uint64_t accepted_frames{},generation_boundaries{},life_boundaries{};
    std::uint64_t correction_samples{},correction_ordinal_advances_unmeasured{},fallback_entries{};
    std::uint64_t gpu_observations{},gpu_duplicates{},gpu_unmatched{},gpu_invalid{};
    std::uint64_t timeline_bucket_width_us{},timeline_compactions{};
    std::size_t timeline_bins{},allocated_capacity_bytes{};
};

// Whole-session aggregates are independent of the short incident windows.
// One bounded allocation at startup; observe never allocates or performs I/O.
// Call observe only after the detailed recorder accepts and sanitizes a frame.
class MovementSessionRecorder final {
public:
    static constexpr std::size_t maximum_capacity_bytes=512U*1024U;
    static constexpr std::size_t maximum_timeline_bins=512,maximum_gpu_identities=1024;
    static constexpr std::size_t maximum_frame_pacing_episodes=64;
    static constexpr double frame_pacing_threshold_ms=20.0;
    static constexpr std::size_t maximum_encoded_bytes=2U*1024U*1024U;
    explicit MovementSessionRecorder(bool enabled=false) noexcept;
    ~MovementSessionRecorder();
    MovementSessionRecorder(MovementSessionRecorder&&) noexcept;
    MovementSessionRecorder& operator=(MovementSessionRecorder&&) noexcept;
    MovementSessionRecorder(const MovementSessionRecorder&)=delete;
    MovementSessionRecorder& operator=(const MovementSessionRecorder&)=delete;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::size_t capacity_bytes() const noexcept;
    void observe(const MovementFrameRecord&,const MovementFrameContext&) noexcept;
    [[nodiscard]] bool mark_manual(std::uint64_t monotonic_us) noexcept;
    [[nodiscard]] bool observe_gpu(std::uint64_t frame_number,double gpu_ms) noexcept;
    [[nodiscard]] MovementSessionStatistics statistics() const noexcept;
    // Exit/test work only. Returned JSON is one object suitable for embedding.
    [[nodiscard]] std::optional<std::string> encode() const;
private:
    struct Impl;
    bool requested_{};
    std::unique_ptr<Impl> impl_;
};
} // namespace hlclient::app

#pragma once

#include <hlclient/entity_visual/player_presentation_diagnostics.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace hlclient::app {
struct MovementFrameRecord;
// Owned numeric observations only. Missing values never mean zero or invisible.
// This seam observes presentation; it does not predict remote players or alter
// their state, clocks, interpolation, collision, animation or rendering.
struct RemoteMovementObservation final {
    std::uint32_t entity_index{},model_index{};
    std::uint64_t lifetime{};
    std::optional<std::array<double,3>> previous_raw_origin,current_raw_origin,latest_raw_origin,sampled_origin,rendered_origin;
    std::optional<std::uint64_t> previous_source_ordinal,current_source_ordinal,latest_source_ordinal;
    std::optional<double> previous_server_s,current_server_s,latest_server_s,sample_server_s;
    std::optional<std::uint32_t> selected_main,selected_gait,selected_weapon,raw_main,raw_gait,raw_weapon,raw_effects;
    std::optional<std::uint32_t> raw_render_mode,raw_render_amount,raw_render_effect;
    std::optional<double> rendered_opacity;
    std::optional<std::uint32_t> support_entity,support_model;
    std::optional<double> support_offset_z;
    std::optional<bool> pose_submitted,visible;
    entity_visual::PlayerPresentationDiagnostics presentation;
};
struct RemoteMovementStatistics final {
    std::uint64_t accepted_frames{},duplicate_frames{},rejected_frames{},invalid_observations{},invalid_values{},duplicate_actors{};
    std::uint64_t absences{},reentries{},generation_boundaries{},lifetime_boundaries{};
    std::uint64_t event_capacity_drops{},recent_capacity_drops{},retention_evictions{},event_cooldown_suppressions{},manual_capture_drops{};
    std::size_t retained_samples{},retained_events{},manual_captures{},allocated_capacity_bytes{};
};
// All actor observations are checked every presented frame. A separate 5 Hz
// recent ring covers four seconds at 32 actors. Automatic exact-frame events
// and two independent one-second pre-marker captures survive ring eviction.
// Each manual capture also retains the latest exact actor samples and up to
// 64 exact automatic events from its pre-window, labelled with observed times.
// One opt-in heap allocation; observe/mark never allocate or perform file I/O.
class RemoteMovementRecorder final {
public:
    static constexpr std::size_t maximum_actors=32,maximum_recent_samples=640,maximum_event_records=128;
    static constexpr std::size_t maximum_manual_captures=2,maximum_manual_samples=160,maximum_manual_events=64;
    static constexpr std::uint64_t retention_us=4'000'000,sample_interval_us=200'000,event_cooldown_us=250'000,manual_pre_us=1'000'000;
    static constexpr double displacement_trigger_units=8.0;
    static constexpr std::size_t maximum_capacity_bytes=1024U*1024U,maximum_encoded_bytes=2U*1024U*1024U;
    explicit RemoteMovementRecorder(bool enabled=false) noexcept;
    ~RemoteMovementRecorder();
    RemoteMovementRecorder(RemoteMovementRecorder&&) noexcept;
    RemoteMovementRecorder& operator=(RemoteMovementRecorder&&) noexcept;
    RemoteMovementRecorder(const RemoteMovementRecorder&)=delete;
    RemoteMovementRecorder& operator=(const RemoteMovementRecorder&)=delete;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::size_t capacity_bytes() const noexcept;
    [[nodiscard]] bool observe(const MovementFrameRecord&,std::span<const RemoteMovementObservation>) noexcept;
    [[nodiscard]] bool mark_manual(std::uint64_t monotonic_us) noexcept;
    [[nodiscard]] RemoteMovementStatistics statistics() const noexcept;
    // Exit/test work only. A small explicit unavailable object is returned when
    // the caller's remaining overall report budget cannot hold this section.
    [[nodiscard]] std::string encode(std::size_t budget=maximum_encoded_bytes) const;
private:
    struct Impl;
    bool requested_{};
    std::unique_ptr<Impl> impl_;
};
} // namespace hlclient::app

#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace hlclient::app {

// Numeric, owning projection of one selected slide's first nonzero -> zero
// operation. It is not a query journal or a reconstructed simulation checkpoint.
struct MovementStopPlane final {
    std::array<float,3> normal{};
    double distance{};
    std::optional<std::uint32_t> source_index;
};
struct MovementVelocityStopRecord final {
    std::uint64_t capture_ordinal{}, frame_number{};
    std::optional<std::uint64_t> observed_us;
    std::uint64_t network_generation{}, map_generation{}, prediction_epoch{};
    std::uint64_t life_epoch{}, life_deaths{}, query_world_revision{}, query_scene_signature{};
    std::uint64_t source_record_identity{}, scene_record_identity{}, scene_revision{};
    std::uint64_t simulation_start_ns{}, simulation_end_ns{};
    std::optional<std::uint64_t> command_sample_us;
    std::optional<double> source_server_time_s, scene_server_time_s;
    std::optional<std::uint32_t> source_sequence;
    std::uint32_t command_sequence{}, history_anchor_command{}, history_latest_command{};
    std::array<std::uint16_t,3> command_angle_turns{};
    std::array<std::int16_t,3> command_movement{};
    std::uint16_t command_buttons{};
    std::uint8_t command_msec{}, phase{}, reason{}, candidate{}, hull{}, source_freshness{};
    // 0:not a rebase, 1:hard snap, 2:no active support, 3:small retained,
    // 4:verified support retained, 5:incompatible/missing predecessor.
    std::uint8_t predecessor_decision{};
    bool replay{}, source_matches_scene{}, command_known{};
    std::array<float,3> before_origin{}, after_origin{}, before_velocity{}, after_velocity{};
    std::array<float,3> query_start{}, query_end{}, trace_end{};
    double trace_fraction{1.};
    bool startsolid{}, allsolid{}, in_open{}, in_liquid{};
    std::uint8_t collision_profile{};
    std::int32_t start_contents{}, end_contents{};
    std::optional<std::int32_t> blocking_contents;
    std::optional<std::uint8_t> hit_kind;
    std::optional<std::uint32_t> hit_entity, hit_model;
    std::optional<std::uint64_t> hit_instance;
    std::optional<MovementStopPlane> hit_plane;
    std::array<MovementStopPlane,5> clip_planes{};
    std::uint8_t clip_plane_count{};
};

} // namespace hlclient::app

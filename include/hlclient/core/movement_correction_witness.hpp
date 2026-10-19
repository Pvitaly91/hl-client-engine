#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace hlclient::core {

enum class MovementCorrectionClassification : std::uint8_t {
    rejected, initial_seed, ordinary_rebase, verified_velocity_visual, hard_unclassified_snap
};
enum class MovementCorrectionCameraStatus : std::uint8_t {
    not_measured, same_time_samples, different_time_samples, incompatible_context
};
enum class MovementWitnessFreshness : std::uint8_t {
    unavailable, observed_in_record, retained_exact_history, derived_current_operation,
    same_time_presentation, retained_context
};

// Fixed, owning diagnostic data only: no movement state, borrowed string,
// callback, scene, packet bytes or replay checkpoint. Producer ns stamps use
// steady_clock's epoch; only the application fills receipt-relative observed_us.
// owner_time_ns is the caller's logical host argument, not an actual clock read.
// Optional fields are genuinely unavailable, never invented zero observations.
struct MovementCorrectionWitness final {
    std::uint64_t event_ordinal{},accepted_ordinal{};
    std::uint64_t network_generation{},map_generation{},life_epoch{},life_deaths{};
    std::uint64_t source_record_identity{},source_record_ordinal{};
    std::uint64_t scene_record_identity{},scene_revision{};
    std::uint64_t before_prediction_epoch{},after_prediction_epoch{},carrier_history_revision{};
    std::uint64_t collision_world_revision{},collision_scene_signature{};
    std::optional<std::uint64_t> observed_ns,source_commit_ns,observed_us;
    std::int64_t owner_time_ns{};
    std::optional<std::int64_t> command_sample_ns,presentation_requested_ns;
    std::optional<std::int64_t> presentation_before_ns,presentation_after_ns;
    std::uint32_t source_sequence{},boundary_command{};
    std::optional<std::uint32_t> carrier_acknowledgement,carrier_outgoing_sequence;
    std::uint32_t history_anchor_command{},history_latest_command{},history_size{};
    std::uint32_t suffix_first_command{},suffix_last_command{},suffix_commands{},suffix_milliseconds{},replayed_commands{};
    // Exact reconstructed wire observations, separate from the collision-derived
    // prediction seed (which may use an already established boundary recovery).
    std::array<double,3> authoritative_origin{},authoritative_velocity{};
    std::optional<std::array<float,3>> matched_origin,matched_velocity,rebase_origin,rebase_velocity;
    std::optional<std::array<float,3>> position_delta,velocity_delta,rebase_position_delta;
    std::optional<std::array<float,3>> before_eye,after_eye,query_start,query_end,plane_normal;
    // raw_position_error uses the production comparison against rebase_origin
    // when available (raw_error_basis=2), otherwise exact authority (basis=1).
    // Camera delta belongs only to this event when camera_status=1.
    std::optional<double> raw_position_error,raw_velocity_error,same_time_camera_delta;
    std::optional<double> source_server_time_s,scene_server_time_s,support_velocity_z,trace_fraction,residual_units;
    std::optional<std::uint32_t> support_entity,support_model,blocking_entity,blocking_model,collision_plane_index;
    std::optional<std::uint64_t> blocking_instance;
    std::array<std::uint16_t,3> command_angle_turns{};
    std::array<std::int16_t,3> command_movement{};
    std::uint16_t command_buttons{};
    std::uint8_t command_msec{};
    // Owner ordinals:0 off/unknown,1 waiting,2 active,3 suspended,4 failed.
    std::uint8_t prediction_owner_state{},classification{},camera_status{},predecessor_decision{};
    std::uint8_t source_freshness{},authoritative_origin_freshness{},authoritative_velocity_freshness{};
    std::uint8_t matched_freshness{},scene_freshness{},support_freshness{},collision_freshness{},presentation_freshness{};
    std::uint8_t raw_error_basis{},rebase_status{255U},anchor_status{},collision_stage{},hull{},mode{},blocking_kind{};
    // 0 unavailable,1 exact requested time,2 selected endpoint at another time.
    std::uint8_t presentation_before_kind{},presentation_after_kind{};
    bool accepted{},control_active{},source_matches_scene{},source_reliable{},reassembled{},contemporaneous_fragment_suffix{};
    bool startsolid{},allsolid{},grounded{};
    std::optional<bool> alive;
    std::array<char,96> reason{};
    std::array<char,40> presentation_reason{};
};

template<std::size_t N>
inline void set_movement_witness_token(std::array<char,N>& output,const std::string_view input) noexcept {
    output.fill('\0');
    const auto count=std::min(input.size(),N-1U);
    for(std::size_t i=0;i<count;++i) {
        const char c=input[i];
        if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-'))return;
    }
    std::copy_n(input.begin(),count,output.begin());
}

static_assert(std::is_trivially_copyable_v<MovementCorrectionWitness>);
static_assert(sizeof(MovementCorrectionWitness)<=1024U);

} // namespace hlclient::core

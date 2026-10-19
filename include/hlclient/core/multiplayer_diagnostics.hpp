#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>

namespace hlclient::core {

// Owning, bounded observations only. No gameplay authority, packet bytes,
// chat text, asset references, callbacks or speculative server outcomes.
enum class MultiplayerDiagnosticProducer : std::uint8_t {
    application, input, command_scheduler, transport, runtime_commit,
    game_host, prediction, presentation, renderer, command_transmission, remote_presentation, ordinary_studio, count
};
enum class MultiplayerProvenance : std::uint8_t {
    unknown, runtime_observed, derived_estimate, source_model, synthetic, user_report
};
enum class MultiplayerKnowledge : std::uint8_t {
    unknown, input_intent, local_action_accepted, command_encoded, command_sent,
    transport_acknowledged, server_event_observed, server_damage_observed,
    server_miss_observed, local_proxy, cosmetic_feedback
};
enum class MultiplayerEdgeConfidence : std::uint8_t { unknown, exact, candidate, ambiguous };
enum class MultiplayerTimeDomain : std::uint8_t { unknown, server_simulation_seconds, command_seconds, presentation_seconds };
enum class MultiplayerCommandPhase : std::uint8_t { sampled, scheduler_due, built, history_inserted, planned, encoded, sent, cancelled };
enum class MultiplayerNetworkPhase : std::uint8_t { packet_planned, packet_encoded, packet_queued, packet_sent, socket_read, decode_begin, decode_end, committed, rollback, transport_ack, duplicate, reordered, rejected };
enum class MultiplayerActionKind : std::uint8_t { unknown, fire, melee, projectile, pellets, charge, beam, throw_object, place, detonate, reload, deploy, holster, zoom, use, select_weapon, recharge, guidance };
enum class MultiplayerActionPhase : std::uint8_t { intent, accepted, begin, update, end, cancel, rejected, server_observed, feedback };
enum class MultiplayerLifecycleKind : std::uint8_t { unknown, connect, disconnect, map_change, spawn, death, respawn, visibility_enter, visibility_leave, object_spawn, object_remove, pickup, ammo, health, suit, inventory, focus, console, damage };
enum class MultiplayerPresentationPhase : std::uint8_t { prediction, correction, remote_sample, pose, scene, submit, swap_begin, swap_end, gpu_result, feedback };
enum class MultiplayerGeometryKind : std::uint8_t { unknown, last_presented_view, command_query, source_model_query, visible_proxy, world_blocker };

struct MultiplayerDiagnosticIdentity final {
    MultiplayerDiagnosticProducer producer{MultiplayerDiagnosticProducer::application};
    // Unique in this producer and network/map generation. Never a packet ID.
    std::uint64_t producer_event_id{}, network_generation{}, map_generation{};
    std::optional<std::uint64_t> local_life, actor_entity, actor_occupant,
        actor_visual_lifetime, actor_model_revision, input_sequence, command_id,
        action_id, carrier_packet, source_sequence, source_record, source_event,
        simulation_iteration, render_frame, scene_revision, pose_revision,
        related_action_id, related_actor_entity, source_query_id,
        source_record_ordinal, source_message_bit_offset, source_entry_ordinal;
};
struct MultiplayerCommandPayload final {
    MultiplayerCommandPhase phase{MultiplayerCommandPhase::sampled};
    std::array<std::uint16_t,3> angle_turns{};
    std::array<std::int16_t,3> movement{};
    std::uint16_t buttons{}, lerp_msec{};
    std::uint8_t msec{}, impulse{};
    std::optional<std::uint8_t> weapon_select;
    // deadline_ns is the logical scheduler owner's domain (same as
    // owner_time_ns), not independently measured physical input time.
    std::optional<std::uint64_t> input_sample_ns, deadline_ns, build_ns, history_insert_ns;
    // Logical owner time is NOT an actual local steady-clock observation.
    std::optional<std::int64_t> owner_time_ns;
    std::optional<double> configured_interpolation_seconds, effective_interpolation_seconds;
};
struct MultiplayerNetworkPayload final {
    MultiplayerNetworkPhase phase{MultiplayerNetworkPhase::packet_planned};
    std::optional<std::uint64_t> begin_ns, end_ns, commit_ns, acknowledgement,
        reliable_generation, first_command_id, last_command_id;
    std::uint32_t bytes{}, command_count{}, backup_count{}, record_count{};
    std::array<std::uint32_t,64> command_ids{};
    std::uint8_t inclusion_count{};
    std::optional<bool> reliable, atomic_commit;
    // This is deliberately transport-only; no execution/hit fields exist here.
};
struct MultiplayerActionPayload final {
    MultiplayerActionKind kind{MultiplayerActionKind::unknown};
    MultiplayerActionPhase phase{MultiplayerActionPhase::intent};
    std::uint32_t weapon_id{}, mode{}, emitter_count{};
    std::optional<std::int32_t> clip_before, clip_after, reserve_before, reserve_after;
    std::optional<bool> eligible, reloading;
    std::optional<double> next_attack_seconds;
    std::optional<std::array<float,3>> origin, direction, spread;
    std::optional<std::array<float,3>> command_angles_degrees, server_punch_degrees,
        player_origin, player_velocity;
    std::optional<bool> player_ducked;
    std::optional<double> command_sample_seconds, action_started_seconds;
    // Actual receive steady-clock seconds, never server/action simulation time.
    std::optional<double> source_received_steady_seconds;
    std::optional<std::uint32_t> source_kind, source_resource_slot, source_flags, source_channel;
    std::optional<std::uint32_t> suppressed_repetitions;
    std::optional<std::uint32_t> random_seed;
    // Project-owned reason token, never arbitrary server/chat text.
    std::array<char,48> reason{};
};
struct MultiplayerLifecyclePayload final {
    MultiplayerLifecycleKind kind{MultiplayerLifecycleKind::unknown};
    std::optional<std::int32_t> before, after;
    std::optional<std::uint64_t> object_entity, object_lifetime;
    std::optional<bool> alive, visible, input_owned;
    std::optional<std::uint32_t> model_index, object_profile;
    std::optional<std::array<float,3>> origin;
    std::optional<std::uint64_t> owner_entity;
    std::optional<std::uint32_t> omitted_objects;
};
struct MultiplayerPresentationPayload final {
    MultiplayerPresentationPhase phase{MultiplayerPresentationPhase::remote_sample};
    std::optional<double> previous_source_seconds, current_source_seconds,
        sample_seconds, pose_seconds, alpha, configured_delay_seconds,
        effective_delay_seconds, frame_ms, cpu_ms, present_ms, gpu_ms;
    std::optional<std::array<float,3>> origin, angles, velocity;
    std::optional<std::uint32_t> sequence;
    std::optional<float> animation_frame;
    // Owning network animation anchors, separate from evaluated sequence/frame.
    std::optional<std::uint32_t> raw_animation_sequence;
    std::optional<double> raw_animation_frame, raw_animtime_seconds, raw_framerate;
    std::optional<std::array<std::uint8_t,4>> raw_controllers;
    std::optional<std::array<std::uint8_t,2>> raw_blending;
    std::optional<std::uint32_t> suppressed_repetitions, diagnostic_cache_evictions;
    std::optional<bool> held, clamped, visible, cache_hit;
    std::optional<std::uint32_t> viewport_width, viewport_height, framebuffer_width, framebuffer_height;
};
struct MultiplayerClockMappingPayload final {
    std::uint64_t mapping_revision{}, local_anchor_ns{};
    double server_anchor_seconds{}, uncertainty_seconds{};
    // A mapping is an estimate unless its producer explicitly establishes more.
};
struct MultiplayerGeometryPayload final {
    MultiplayerGeometryKind kind{MultiplayerGeometryKind::unknown};
    std::optional<std::array<float,3>> origin, direction, endpoint;
    std::optional<std::array<float,3>> camera_up;
    std::optional<float> vertical_fov_radians, near_plane, far_plane;
    std::optional<double> fraction;
    std::optional<std::uint64_t> proxy_actor, blocker_entity;
    std::optional<std::uint64_t> proxy_model_revision, world_query_steady_ns;
    std::optional<bool> intersects, startsolid, allsolid;
    std::optional<std::uint32_t> viewport_width, viewport_height, framebuffer_width, framebuffer_height;
    std::optional<double> proxy_age_ms;
    std::optional<std::uint32_t> hitbox_group;
    std::optional<bool> exact_model_pose, world_blocked, query_budget_exhausted, query_incomplete;
};
struct MultiplayerInputPayload final {
    std::uint32_t event_type{}, key{}, mouse_button{};
    float delta_x{}, delta_y{}, wheel{};
    bool repeat{};
    // SDL timestamp is since SDL initialization, not the local steady epoch.
    std::optional<std::uint64_t> sdl_timestamp_ns, dequeue_steady_ns;
    std::optional<bool> focused, captured, console_owned, hud_consumed, gameplay_routed;
};
struct MultiplayerMovementPayload final {
    MultiplayerPresentationPhase phase{MultiplayerPresentationPhase::prediction};
    std::optional<std::array<float,3>> raw_server_origin, predicted_origin, presented_eye,
        velocity, basevelocity, camera_base_angles, server_punch_angles, local_punch_angles, camera_final_angles;
    std::optional<double> raw_correction_units, camera_jump_units, replay_duration_ms, source_seconds, render_sample_seconds;
    std::optional<std::uint32_t> support_entity, support_model, hull;
    std::optional<bool> prediction_active, grounded, startsolid, allsolid;
};
struct MultiplayerRemotePayload final {
    std::optional<std::array<float,3>> previous_raw_origin, current_raw_origin, latest_raw_origin, sampled_origin, rendered_origin;
    std::optional<double> previous_source_seconds, current_source_seconds, latest_source_seconds, sample_seconds, alpha, support_offset_z;
    std::optional<std::uint64_t> previous_source_ordinal, current_source_ordinal, latest_source_ordinal;
    std::optional<std::uint32_t> model_index, selected_main, selected_gait, selected_weapon, support_entity, support_model;
    std::optional<bool> pose_submitted, visible;
    std::uint32_t decision{}, floor_reasons{}, new_boundary_reasons{}, hold_reasons{}, clock_reasons{};
};
using MultiplayerDiagnosticPayload = std::variant<MultiplayerCommandPayload,
    MultiplayerNetworkPayload, MultiplayerActionPayload, MultiplayerLifecyclePayload,
    MultiplayerPresentationPayload, MultiplayerClockMappingPayload, MultiplayerGeometryPayload,
    MultiplayerInputPayload, MultiplayerMovementPayload, MultiplayerRemotePayload>;
struct MultiplayerDiagnosticEvent final {
    MultiplayerDiagnosticIdentity identity;
    // Actual steady_clock::now().time_since_epoch() in nanoseconds. Not frame
    // delta, server time or the logical simulation owner's time argument.
    std::uint64_t local_steady_ns{};
    std::optional<double> server_seconds;
    MultiplayerTimeDomain server_time_domain{MultiplayerTimeDomain::unknown};
    MultiplayerProvenance provenance{MultiplayerProvenance::unknown};
    MultiplayerKnowledge knowledge{MultiplayerKnowledge::unknown};
    MultiplayerEdgeConfidence edge{MultiplayerEdgeConfidence::unknown};
    MultiplayerDiagnosticPayload payload;
};
class MultiplayerDiagnosticSink {
public:
    virtual ~MultiplayerDiagnosticSink()=default;
    [[nodiscard]] virtual bool enabled() const noexcept=0;
    [[nodiscard]] virtual bool observe(const MultiplayerDiagnosticEvent&) noexcept=0;
};
[[nodiscard]] inline std::uint64_t multiplayer_diagnostic_now_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
static_assert(std::is_trivially_copyable_v<MultiplayerDiagnosticEvent>);
static_assert(sizeof(MultiplayerDiagnosticEvent)<=864);
} // namespace hlclient::core

#include <hlclient/prediction/local_prediction.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <type_traits>

namespace hlclient::prediction {
namespace {

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= 1'099'511'628'211ULL;
}

template<class Value>
void hash_value(std::uint64_t& hash, const Value value) noexcept
{
    static_assert(std::is_trivially_copyable_v<Value>);
    const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
    for (const auto byte : bytes) {
        hash_byte(hash, std::to_integer<std::uint8_t>(byte));
    }
}

void hash_vector(
    std::uint64_t& hash,
    const assets::AssetVector3& value) noexcept
{
    hash_value(hash, value.x);
    hash_value(hash, value.y);
    hash_value(hash, value.z);
}

} // namespace

std::uint64_t prediction_movement_environment_signature(
    const goldsrc::movement::GoldSrcMovementEnvironment& environment) noexcept
{
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    const auto& values = environment.values();
    hash_value(hash, values.gravity);
    hash_value(hash, values.stop_speed);
    hash_value(hash, values.maximum_speed);
    hash_value(hash, values.acceleration);
    hash_value(hash, values.air_acceleration);
    hash_value(hash, values.friction);
    hash_value(hash, values.step_size);
    hash_value(hash, values.maximum_velocity);
    hash_value(hash, values.entity_gravity);
    hash_value(hash, values.deferred.water_acceleration);
    hash_value(hash, values.deferred.water_friction);
    hash_value(hash, values.deferred.edge_friction);
    hash_value(hash, values.deferred.bounce);
    hash_value(hash, values.deferred.z_maximum);
    hash_value(hash, values.deferred.wave_height);
    hash_value(hash, environment.profile());
    hash_value(hash, environment.evidence_profile());
    hash_value(hash, environment.source_profile());
    return hash == 0U ? 1U : hash;
}

std::uint64_t prediction_movement_config_signature(
    const goldsrc::movement::GoldSrcLocalMovementConfig& config) noexcept
{
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    hash_value(hash, config.maximum_command_duration_seconds);
    hash_value(hash, config.maximum_substep_duration_seconds);
    hash_value(hash, config.maximum_substeps_per_command);
    hash_value(hash, config.ground_probe_distance);
    hash_value(hash, config.minimum_walkable_normal_z);
    hash_value(hash, config.maximum_ground_snap_upward_velocity);
    hash_value(hash, config.maximum_slide_bumps);
    hash_value(hash, config.maximum_clip_planes);
    hash_value(hash, config.maximum_touches_per_command);
    hash_value(hash, config.stop_epsilon);
    hash_value(hash, config.air_wish_speed_cap);
    hash_value(hash, config.jump_impulse);
    hash_vector(hash, config.standing_view_offset);
    hash_vector(hash, config.duck_view_offset);
    hash_value(hash, config.gravity_profile);
    hash_value(hash, config.jump_profile);
    hash_value(hash, config.duck_profile);
    hash_value(hash, config.air_profile);
    hash_value(hash, config.airborne_duck_policy);
    hash_value(hash, config.collision_query.query_limits.maximum_traversal_steps);
    hash_value(hash, config.collision_query.query_limits.maximum_stack_entries);
    hash_value(hash, config.collision_query.query_limits.maximum_fraction_splits);
    hash_value(
        hash, config.collision_query.query_limits.maximum_query_scratch_bytes);
    hash_value(
        hash, config.collision_query.trace_tolerance.plane_distance_epsilon);
    hash_value(hash, config.collision_query.trace_tolerance.fraction_epsilon);
    hash_value(
        hash, config.collision_query.trace_tolerance.minimum_progress_fraction);
    hash_value(
        hash, config.collision_query.scene_limits.maximum_brush_candidates);
    hash_value(hash, config.collision_query.scene_limits.maximum_model_traces);
    hash_value(hash, config.state_limits.maximum_coordinate_magnitude);
    hash_value(hash, config.state_limits.maximum_velocity_magnitude);
    hash_value(hash, config.state_limits.maximum_angle_magnitude);
    hash_value(hash, config.state_limits.maximum_state_revision);
    return hash == 0U ? 1U : hash;
}

PredictionSessionCreateResult create_prediction_session_identity(
    const std::uint64_t session_generation,
    const std::uint64_t prediction_generation,
    const goldsrc::movement::ILocalMovementCollision& collision,
    const goldsrc::movement::GoldSrcMovementEnvironment& environment,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config,
    const movement::LocalPlayerMovementState& initial_state,
    const PredictionCompatibilityProfile profile,
    const PredictionAcknowledgementProfile acknowledgement_profile) noexcept
{
    if (profile == PredictionCompatibilityProfile::
            stock_protocol_48_authoritative_reconciliation_evidence_pending ||
        acknowledgement_profile == PredictionAcknowledgementProfile::
            stock_usercmd_acknowledgement_evidence_pending) {
        return {std::nullopt,
            PredictionError{PredictionErrorCode::stock_evidence_pending,
                std::nullopt,
                "stock prediction acknowledgement and player-state evidence is pending"}};
    }
    const auto collision_identity = collision.session_identity();
    if (session_generation == 0U || prediction_generation == 0U ||
        !collision.valid() || !collision_identity ||
        !collision_identity->valid() ||
        collision.profile() != collision_identity->profile ||
        !goldsrc::movement::valid_goldsrc_local_movement_config(
            movement_config) ||
        initial_state.source_command_sequence() != 0U ||
        initial_state.command_profile() !=
            movement::GoldSrcMovementCommandProfile::
                synthetic_usercmd_semantics_v1) {
        return {std::nullopt,
            PredictionError{PredictionErrorCode::invalid_session_identity,
                std::nullopt,
                "prediction session components are invalid or identity-less"}};
    }
    PredictionSessionIdentity identity;
    identity.session_generation = session_generation;
    identity.prediction_generation = prediction_generation;
    identity.collision_world_primary =
        collision_identity->collision_world_primary;
    identity.collision_world_secondary =
        collision_identity->collision_world_secondary;
    identity.collision_world_revision =
        collision_identity->collision_world_revision;
    identity.collision_scene_signature = collision_identity->scene_signature;
    identity.collision_profile = collision_identity->profile;
    identity.movement_environment_signature =
        prediction_movement_environment_signature(environment);
    identity.movement_config_signature =
        prediction_movement_config_signature(movement_config);
    identity.spawn_initial_state_signature =
        movement::local_player_movement_state_signature(initial_state);
    identity.command_profile = initial_state.command_profile();
    identity.prediction_profile = profile;
    identity.acknowledgement_profile = acknowledgement_profile;
    if (!identity.valid()) {
        return {std::nullopt,
            PredictionError{PredictionErrorCode::invalid_session_identity,
                std::nullopt,
                "constructed prediction session identity is invalid"}};
    }
    return {identity, std::nullopt};
}

} // namespace hlclient::prediction

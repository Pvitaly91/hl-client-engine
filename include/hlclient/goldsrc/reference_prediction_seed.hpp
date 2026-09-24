#pragma once

#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/goldsrc/reference_prediction_anchor.hpp>
#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc {

enum class ReferencePredictionFieldOrigin : std::uint8_t {
    clientdata_observed,
    matched_player_entity_observed,
    reference_anchored_neutral_command,
    exact_retained_prediction_slot,
    reference_vertical_only_view_offset_policy,
};

struct ReferenceRetainedPredictionButtons final {
    GoldSrcUserCmdSequence command_identity;
    std::uint16_t old_buttons{0U};
};

// A semantic correction candidate, not a complete PM_Move or authoritative
// state. Ground, contents and collision identity must be derived separately.
struct ReferencePredictionSeed final {
    std::uint64_t generation{0U};
    std::uint64_t record_identity{0U};
    client::RuntimeObservationSource clientdata_source;
    client::RuntimeObservationSource player_entity_source;
    std::uint32_t receiving_entity_number{0U};
    GoldSrcUserCmdSequence command_boundary;
    client::RuntimeVector3Observation origin;
    client::RuntimeVector3Observation velocity;
    client::RuntimeVector3Observation view_offset;
    ReferencePredictionFieldOrigin view_offset_origin{
        ReferencePredictionFieldOrigin::clientdata_observed};
    client::RuntimeVector3Observation base_velocity;
    std::uint32_t flags{0U};
    std::uint32_t move_type{0U};
    std::uint32_t use_hull{0U};
    std::uint32_t water_level{0U};
    bool in_duck{false};
    std::uint16_t old_buttons{0U};
    std::optional<double> maximum_speed;
    std::optional<double> gravity_multiplier;
    std::optional<double> friction_multiplier;
    ReferencePredictionFieldOrigin old_buttons_origin{
        ReferencePredictionFieldOrigin::reference_anchored_neutral_command};
};

enum class ReferencePredictionSeedStatus : std::uint8_t {
    ready,
    invalid_anchor,
    clientdata_unavailable,
    player_entity_unavailable,
    incoherent_records,
    missing_semantic_field,
    unsupported_context,
    matching_prediction_slot_required,
    retained_slot_mismatch,
};

enum class ReferencePredictionSeedField : std::uint8_t {
    origin, velocity, view_offset, flags, water_level, dead_flag,
    in_duck, move_type, use_hull, gravity_multiplier,
    friction_multiplier, base_velocity, spectator, maximum_speed,
};

[[nodiscard]] constexpr std::string_view to_string(
    const ReferencePredictionSeedStatus status) noexcept {
    switch (status) {
    case ReferencePredictionSeedStatus::ready: return "ready";
    case ReferencePredictionSeedStatus::invalid_anchor: return "invalid_anchor";
    case ReferencePredictionSeedStatus::clientdata_unavailable: return "clientdata_unavailable";
    case ReferencePredictionSeedStatus::player_entity_unavailable: return "player_entity_unavailable";
    case ReferencePredictionSeedStatus::incoherent_records: return "incoherent_records";
    case ReferencePredictionSeedStatus::missing_semantic_field: return "missing_semantic_field";
    case ReferencePredictionSeedStatus::unsupported_context: return "unsupported_context";
    case ReferencePredictionSeedStatus::matching_prediction_slot_required:
        return "matching_prediction_slot_required";
    case ReferencePredictionSeedStatus::retained_slot_mismatch:
        return "retained_slot_mismatch";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ReferencePredictionSeedField field) noexcept {
    switch (field) {
    case ReferencePredictionSeedField::origin: return "origin";
    case ReferencePredictionSeedField::velocity: return "velocity";
    case ReferencePredictionSeedField::view_offset: return "view_offset";
    case ReferencePredictionSeedField::flags: return "flags";
    case ReferencePredictionSeedField::water_level: return "water_level";
    case ReferencePredictionSeedField::dead_flag: return "dead_flag";
    case ReferencePredictionSeedField::in_duck: return "in_duck";
    case ReferencePredictionSeedField::move_type: return "move_type";
    case ReferencePredictionSeedField::use_hull: return "use_hull";
    case ReferencePredictionSeedField::gravity_multiplier: return "gravity_multiplier";
    case ReferencePredictionSeedField::friction_multiplier: return "friction_multiplier";
    case ReferencePredictionSeedField::base_velocity: return "base_velocity";
    case ReferencePredictionSeedField::spectator: return "spectator";
    case ReferencePredictionSeedField::maximum_speed: return "maximum_speed";
    }
    return "unknown";
}

struct ReferencePredictionSeedResult final {
    ReferencePredictionSeedStatus status{
        ReferencePredictionSeedStatus::invalid_anchor};
    std::optional<ReferencePredictionSeed> seed;
    std::optional<ReferencePredictionSeedField> field;
};

enum class ReferencePredictionGroundStatus : std::uint8_t {
    ready,
    invalid_collision_identity,
    unsupported_hull,
    collision_query_failed,
    solid_start,
    unsupported_contents,
    invalid_ground_trace,
    ground_flag_disagreement,
    invalid_movement_state,
};

[[nodiscard]] constexpr std::string_view to_string(
    const ReferencePredictionGroundStatus status) noexcept {
    switch (status) {
    case ReferencePredictionGroundStatus::ready: return "ready";
    case ReferencePredictionGroundStatus::invalid_collision_identity:
        return "invalid_collision_identity";
    case ReferencePredictionGroundStatus::unsupported_hull: return "unsupported_hull";
    case ReferencePredictionGroundStatus::collision_query_failed:
        return "collision_query_failed";
    case ReferencePredictionGroundStatus::solid_start: return "solid_start";
    case ReferencePredictionGroundStatus::unsupported_contents:
        return "unsupported_contents";
    case ReferencePredictionGroundStatus::invalid_ground_trace:
        return "invalid_ground_trace";
    case ReferencePredictionGroundStatus::ground_flag_disagreement:
        return "ground_flag_disagreement";
    case ReferencePredictionGroundStatus::invalid_movement_state:
        return "invalid_movement_state";
    }
    return "unknown";
}

struct ReferencePredictionGroundResult final {
    ReferencePredictionGroundStatus status{
        ReferencePredictionGroundStatus::invalid_collision_identity};
    std::optional<hlclient::movement::LocalPlayerMovementState> state;
    std::optional<movement::LocalMovementCollisionSessionIdentity>
        collision_identity;
    std::optional<hlclient::movement::PlayerGroundStateCreateInfo> ground_evidence;
};

// Read-only collision categorization of a coherent server seed. The observed
// origin and velocity are never snapped to the probe endpoint or rewritten.
[[nodiscard]] ReferencePredictionGroundResult derive_reference_prediction_ground(
    const ReferencePredictionSeed& seed,
    const GoldSrcWireUserCmd& boundary_command,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config = {});

// ServerInfo's client slot is zero based; ReHLDS serializes its player entity
// as slot + 1. Both substates must be freshly observed in the same record.
[[nodiscard]] ReferencePredictionSeedResult inspect_reference_prediction_seed(
    const client::RuntimeClientObservationState& observation,
    std::uint8_t receiving_client_slot,
    const ReferencePredictionAnchorResult& anchor,
    std::optional<ReferenceRetainedPredictionButtons> retained = {}) noexcept;

} // namespace hlclient::goldsrc

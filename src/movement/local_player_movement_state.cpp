#include <hlclient/movement/local_player_movement_state.hpp>

#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace hlclient::movement {
namespace {

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool within(
    const assets::AssetVector3& value,
    const float maximum) noexcept
{
    return std::abs(value.x) <= maximum && std::abs(value.y) <= maximum &&
        std::abs(value.z) <= maximum;
}

[[nodiscard]] bool valid_hull(const PlayerMovementHull hull) noexcept
{
    return hull == PlayerMovementHull::standing ||
        hull == PlayerMovementHull::ducked;
}

[[nodiscard]] bool valid_mode(const PlayerMovementMode mode) noexcept
{
    switch (mode) {
    case PlayerMovementMode::walking:
    case PlayerMovementMode::airborne:
    case PlayerMovementMode::unsupported_liquid:
    case PlayerMovementMode::unsupported_ladder:
    case PlayerMovementMode::invalid_or_stuck: return true;
    }
    return false;
}

[[nodiscard]] bool valid_contents(const PlayerMovementContents contents) noexcept
{
    switch (contents) {
    case PlayerMovementContents::empty:
    case PlayerMovementContents::solid:
    case PlayerMovementContents::water:
    case PlayerMovementContents::slime:
    case PlayerMovementContents::lava:
    case PlayerMovementContents::current:
    case PlayerMovementContents::sky:
    case PlayerMovementContents::special: return true;
    }
    return false;
}

[[nodiscard]] bool valid_hit_identity(
    const PlayerMovementHitIdentity& hit) noexcept
{
    switch (hit.kind) {
    case PlayerMovementHitKind::world:
        return hit.source_model_index == 0U &&
            !hit.stable_instance_ordinal.has_value() &&
            !hit.source_entity_index.has_value();
    case PlayerMovementHitKind::explicit_synthetic_brush:
        return hit.source_model_index != 0U &&
            hit.stable_instance_ordinal.has_value();
    }
    return false;
}

[[nodiscard]] bool valid_ground(
    const PlayerGroundStateCreateInfo& ground,
    const PlayerMovementMode mode,
    const float coordinate_limit) noexcept
{
    if (!std::isfinite(ground.probe_fraction) ||
        ground.probe_fraction < 0.0 || ground.probe_fraction > 1.0 ||
        !finite(ground.plane.normal) || !std::isfinite(ground.plane.distance) ||
        !finite(ground.contact_position) ||
        !within(ground.contact_position, coordinate_limit) ||
        ground.evidence_profile !=
            PlayerGroundEvidenceProfile::deterministic_collision_trace_v1) {
        return false;
    }
    if (!ground.grounded) {
        return !ground.walkable && !ground.hit.has_value() &&
            mode != PlayerMovementMode::walking;
    }
    if (!ground.walkable || !ground.hit ||
        !valid_hit_identity(*ground.hit) ||
        mode != PlayerMovementMode::walking) {
        return false;
    }
    const auto normal_length = std::sqrt(
        static_cast<double>(ground.plane.normal.x) * ground.plane.normal.x +
        static_cast<double>(ground.plane.normal.y) * ground.plane.normal.y +
        static_cast<double>(ground.plane.normal.z) * ground.plane.normal.z);
    return std::isfinite(normal_length) &&
        std::abs(normal_length - 1.0) <= 1.0e-4 &&
        ground.plane.normal.z >= 0.7F;
}

[[nodiscard]] LocalPlayerMovementState::CreationResult failure(
    const LocalPlayerMovementStateErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, LocalPlayerMovementStateError{code, context}};
}

template<class Value, bool IsEnum = std::is_enum_v<Value>>
struct IntegralRaw {
    using type = Value;
};

template<class Value>
struct IntegralRaw<Value, true> {
    using type = std::underlying_type_t<Value>;
};

template<class Value>
void hash_integral(std::uint64_t& hash, const Value value) noexcept
{
    using Raw = typename IntegralRaw<Value>::type;
    using Unsigned = std::make_unsigned_t<Raw>;
    const auto raw = static_cast<Unsigned>(static_cast<Raw>(value));
    for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
        hash ^= static_cast<std::uint8_t>(raw >> (index * 8U));
        hash *= 1'099'511'628'211ULL;
    }
}

template<>
void hash_integral<bool>(std::uint64_t& hash, const bool value) noexcept
{
    hash_integral(hash, static_cast<std::uint8_t>(value ? 1U : 0U));
}

template<>
void hash_integral<float>(std::uint64_t& hash, const float value) noexcept
{
    hash_integral(hash, std::bit_cast<std::uint32_t>(value));
}

template<>
void hash_integral<double>(std::uint64_t& hash, const double value) noexcept
{
    hash_integral(hash, std::bit_cast<std::uint64_t>(value));
}

void hash_vector(
    std::uint64_t& hash,
    const assets::AssetVector3& value) noexcept
{
    hash_integral(hash, value.x);
    hash_integral(hash, value.y);
    hash_integral(hash, value.z);
}

} // namespace

PlayerGroundState::PlayerGroundState(
    const PlayerGroundStateCreateInfo& create_info) noexcept
    : grounded_{create_info.grounded},
      walkable_{create_info.walkable},
      hit_{create_info.hit},
      plane_{create_info.plane},
      contact_position_{create_info.contact_position},
      probe_fraction_{create_info.probe_fraction},
      evidence_profile_{create_info.evidence_profile}
{
}

bool PlayerGroundState::grounded() const noexcept { return grounded_; }
bool PlayerGroundState::walkable() const noexcept { return walkable_; }
const std::optional<PlayerMovementHitIdentity>& PlayerGroundState::hit()
    const noexcept { return hit_; }
const PlayerMovementPlane& PlayerGroundState::plane() const noexcept
{
    return plane_;
}
const assets::AssetVector3& PlayerGroundState::contact_position() const noexcept
{
    return contact_position_;
}
double PlayerGroundState::probe_fraction() const noexcept
{
    return probe_fraction_;
}
PlayerGroundEvidenceProfile PlayerGroundState::evidence_profile() const noexcept
{
    return evidence_profile_;
}

bool valid_local_player_movement_state_limits(
    const LocalPlayerMovementStateLimits& limits) noexcept
{
    return std::isfinite(limits.maximum_coordinate_magnitude) &&
        limits.maximum_coordinate_magnitude > 0.0F &&
        std::isfinite(limits.maximum_velocity_magnitude) &&
        limits.maximum_velocity_magnitude > 0.0F &&
        std::isfinite(limits.maximum_angle_magnitude) &&
        limits.maximum_angle_magnitude > 0.0F &&
        limits.maximum_state_revision > 0U;
}

LocalPlayerMovementState::LocalPlayerMovementState(
    const LocalPlayerMovementStateCreateInfo& create_info) noexcept
    : origin_{create_info.origin},
      velocity_{create_info.velocity},
      view_angles_{create_info.view_angles},
      hull_{create_info.hull},
      mode_{create_info.mode},
      ground_state_{create_info.ground},
      view_offset_{create_info.view_offset},
      old_buttons_{create_info.old_buttons},
      source_command_sequence_{create_info.source_command_sequence},
      simulation_time_nanoseconds_{create_info.simulation_time_nanoseconds},
      last_valid_contents_{create_info.last_valid_contents},
      gravity_multiplier_{create_info.gravity_multiplier},
      friction_multiplier_{create_info.friction_multiplier},
      state_revision_{create_info.state_revision},
      compatibility_profile_{create_info.compatibility_profile},
      evidence_profile_{create_info.evidence_profile},
      command_profile_{create_info.command_profile}
{
}

LocalPlayerMovementState::CreationResult LocalPlayerMovementState::create(
    const LocalPlayerMovementStateCreateInfo& create_info,
    const LocalPlayerMovementStateLimits& limits) noexcept
{
    if (!valid_local_player_movement_state_limits(limits)) {
        return failure(LocalPlayerMovementStateErrorCode::invalid_limits,
            "movement-state limits are invalid");
    }
    if (!finite(create_info.origin)) {
        return failure(LocalPlayerMovementStateErrorCode::non_finite_origin,
            "movement origin is non-finite");
    }
    if (!finite(create_info.velocity)) {
        return failure(LocalPlayerMovementStateErrorCode::non_finite_velocity,
            "movement velocity is non-finite");
    }
    if (!finite(create_info.view_angles)) {
        return failure(LocalPlayerMovementStateErrorCode::non_finite_angles,
            "movement view angles are non-finite");
    }
    if (!finite(create_info.view_offset)) {
        return failure(LocalPlayerMovementStateErrorCode::non_finite_view_offset,
            "movement view offset is non-finite");
    }
    if (!within(create_info.origin, limits.maximum_coordinate_magnitude) ||
        !within(create_info.view_offset, limits.maximum_coordinate_magnitude)) {
        return failure(LocalPlayerMovementStateErrorCode::coordinate_limit_exceeded,
            "movement coordinate exceeds its safety bound");
    }
    if (!within(create_info.velocity, limits.maximum_velocity_magnitude)) {
        return failure(LocalPlayerMovementStateErrorCode::velocity_limit_exceeded,
            "movement velocity exceeds its safety bound");
    }
    if (!within(create_info.view_angles, limits.maximum_angle_magnitude)) {
        return failure(LocalPlayerMovementStateErrorCode::angle_limit_exceeded,
            "movement view angle exceeds its safety bound");
    }
    if (!valid_hull(create_info.hull)) {
        return failure(LocalPlayerMovementStateErrorCode::invalid_hull,
            "movement hull is invalid");
    }
    if (!valid_mode(create_info.mode)) {
        return failure(LocalPlayerMovementStateErrorCode::invalid_mode,
            "movement mode is invalid");
    }
    if (!valid_ground(
            create_info.ground, create_info.mode,
            limits.maximum_coordinate_magnitude)) {
        return failure(LocalPlayerMovementStateErrorCode::invalid_ground_state,
            "movement ground metadata is inconsistent");
    }
    if (!valid_contents(create_info.last_valid_contents)) {
        return failure(LocalPlayerMovementStateErrorCode::invalid_mode,
            "movement contents category is invalid");
    }
    if (!std::isfinite(create_info.gravity_multiplier) ||
        create_info.gravity_multiplier <= 0.0F ||
        !std::isfinite(create_info.friction_multiplier) ||
        create_info.friction_multiplier < 0.0F) {
        return failure(LocalPlayerMovementStateErrorCode::invalid_multiplier,
            "movement multiplier is invalid");
    }
    if (create_info.state_revision == 0U ||
        create_info.state_revision > limits.maximum_state_revision) {
        return failure(LocalPlayerMovementStateErrorCode::invalid_revision,
            "movement state revision is invalid");
    }
    if (create_info.compatibility_profile ==
            GoldSrcMovementCompatibilityProfile::
                stock_pm_move_full_compatibility_evidence_pending ||
        create_info.evidence_profile ==
            GoldSrcMovementEvidenceProfile::
                stock_pm_move_full_compatibility_evidence_pending ||
        create_info.command_profile ==
            GoldSrcMovementCommandProfile::
                stock_usercmd_semantics_evidence_pending) {
        return failure(LocalPlayerMovementStateErrorCode::stock_evidence_pending,
            "stock movement or usercmd semantics remain evidence-pending");
    }
    if (create_info.compatibility_profile !=
            GoldSrcMovementCompatibilityProfile::
                public_valve_pm_shared_dry_walk_subset_v1 ||
        create_info.evidence_profile !=
            GoldSrcMovementEvidenceProfile::
                public_valve_pm_shared_and_independent_fixtures ||
        create_info.command_profile !=
            GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1) {
        return failure(LocalPlayerMovementStateErrorCode::unsupported_profile,
            "movement state profile is unsupported");
    }
    return {std::optional<LocalPlayerMovementState>{
                LocalPlayerMovementState{create_info}},
        std::nullopt};
}

const assets::AssetVector3& LocalPlayerMovementState::origin() const noexcept
{ return origin_; }
const assets::AssetVector3& LocalPlayerMovementState::velocity() const noexcept
{ return velocity_; }
const assets::AssetVector3& LocalPlayerMovementState::view_angles() const noexcept
{ return view_angles_; }
PlayerMovementHull LocalPlayerMovementState::hull() const noexcept
{ return hull_; }
PlayerMovementMode LocalPlayerMovementState::mode() const noexcept
{ return mode_; }
const PlayerGroundState& LocalPlayerMovementState::ground_state() const noexcept
{ return ground_state_; }
const assets::AssetVector3& LocalPlayerMovementState::view_offset() const noexcept
{ return view_offset_; }
std::uint16_t LocalPlayerMovementState::old_buttons() const noexcept
{ return old_buttons_; }
std::uint32_t LocalPlayerMovementState::source_command_sequence() const noexcept
{ return source_command_sequence_; }
std::uint64_t LocalPlayerMovementState::simulation_time_nanoseconds() const noexcept
{ return simulation_time_nanoseconds_; }
PlayerMovementContents LocalPlayerMovementState::last_valid_contents() const noexcept
{ return last_valid_contents_; }
float LocalPlayerMovementState::gravity_multiplier() const noexcept
{ return gravity_multiplier_; }
float LocalPlayerMovementState::friction_multiplier() const noexcept
{ return friction_multiplier_; }
std::uint64_t LocalPlayerMovementState::state_revision() const noexcept
{ return state_revision_; }
GoldSrcMovementCompatibilityProfile
LocalPlayerMovementState::compatibility_profile() const noexcept
{ return compatibility_profile_; }
GoldSrcMovementEvidenceProfile LocalPlayerMovementState::evidence_profile()
    const noexcept { return evidence_profile_; }
GoldSrcMovementCommandProfile LocalPlayerMovementState::command_profile()
    const noexcept { return command_profile_; }

std::uint64_t local_player_movement_state_signature(
    const LocalPlayerMovementState& state) noexcept
{
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    hash_vector(hash, state.origin());
    hash_vector(hash, state.velocity());
    hash_vector(hash, state.view_angles());
    hash_integral(hash, state.hull());
    hash_integral(hash, state.mode());
    hash_integral(hash, state.ground_state().grounded());
    hash_integral(hash, state.ground_state().walkable());
    hash_vector(hash, state.ground_state().plane().normal);
    hash_integral(hash, state.ground_state().plane().distance);
    hash_integral(
        hash, state.ground_state().plane().source_plane_index.has_value());
    if (state.ground_state().plane().source_plane_index) {
        hash_integral(
            hash, *state.ground_state().plane().source_plane_index);
    }
    hash_vector(hash, state.ground_state().contact_position());
    hash_integral(hash, state.ground_state().probe_fraction());
    hash_integral(hash, state.ground_state().evidence_profile());
    hash_vector(hash, state.view_offset());
    hash_integral(hash, state.old_buttons());
    hash_integral(hash, state.source_command_sequence());
    hash_integral(hash, state.simulation_time_nanoseconds());
    hash_integral(hash, state.last_valid_contents());
    hash_integral(hash, state.gravity_multiplier());
    hash_integral(hash, state.friction_multiplier());
    hash_integral(hash, state.state_revision());
    hash_integral(hash, state.compatibility_profile());
    hash_integral(hash, state.evidence_profile());
    hash_integral(hash, state.command_profile());
    if (state.ground_state().hit()) {
        hash_integral(hash, true);
        hash_integral(hash, state.ground_state().hit()->kind);
        hash_integral(hash, state.ground_state().hit()->source_model_index);
        hash_integral(hash,
            state.ground_state().hit()->stable_instance_ordinal.has_value());
        if (state.ground_state().hit()->stable_instance_ordinal) {
            hash_integral(hash,
                *state.ground_state().hit()->stable_instance_ordinal);
        }
        hash_integral(hash,
            state.ground_state().hit()->source_entity_index.has_value());
        if (state.ground_state().hit()->source_entity_index) {
            hash_integral(hash,
                *state.ground_state().hit()->source_entity_index);
        }
    } else {
        hash_integral(hash, false);
    }
    return hash;
}

LocalPlayerMovementStateCreateInfo local_player_movement_state_create_info(
    const LocalPlayerMovementState& state) noexcept
{
    LocalPlayerMovementStateCreateInfo result;
    result.origin = state.origin();
    result.velocity = state.velocity();
    result.view_angles = state.view_angles();
    result.hull = state.hull();
    result.mode = state.mode();
    result.ground = {
        state.ground_state().grounded(),
        state.ground_state().walkable(),
        state.ground_state().hit(),
        state.ground_state().plane(),
        state.ground_state().contact_position(),
        state.ground_state().probe_fraction(),
        state.ground_state().evidence_profile(),
    };
    result.view_offset = state.view_offset();
    result.old_buttons = state.old_buttons();
    result.source_command_sequence = state.source_command_sequence();
    result.simulation_time_nanoseconds = state.simulation_time_nanoseconds();
    result.last_valid_contents = state.last_valid_contents();
    result.gravity_multiplier = state.gravity_multiplier();
    result.friction_multiplier = state.friction_multiplier();
    result.state_revision = state.state_revision();
    result.compatibility_profile = state.compatibility_profile();
    result.evidence_profile = state.evidence_profile();
    result.command_profile = state.command_profile();
    return result;
}

std::string_view to_string(
    const LocalPlayerMovementStateErrorCode code) noexcept
{
    switch (code) {
    case LocalPlayerMovementStateErrorCode::invalid_limits:
        return "invalid_limits";
    case LocalPlayerMovementStateErrorCode::non_finite_origin:
        return "non_finite_origin";
    case LocalPlayerMovementStateErrorCode::non_finite_velocity:
        return "non_finite_velocity";
    case LocalPlayerMovementStateErrorCode::non_finite_angles:
        return "non_finite_angles";
    case LocalPlayerMovementStateErrorCode::non_finite_view_offset:
        return "non_finite_view_offset";
    case LocalPlayerMovementStateErrorCode::coordinate_limit_exceeded:
        return "coordinate_limit_exceeded";
    case LocalPlayerMovementStateErrorCode::velocity_limit_exceeded:
        return "velocity_limit_exceeded";
    case LocalPlayerMovementStateErrorCode::angle_limit_exceeded:
        return "angle_limit_exceeded";
    case LocalPlayerMovementStateErrorCode::invalid_hull:
        return "invalid_hull";
    case LocalPlayerMovementStateErrorCode::invalid_mode:
        return "invalid_mode";
    case LocalPlayerMovementStateErrorCode::invalid_ground_state:
        return "invalid_ground_state";
    case LocalPlayerMovementStateErrorCode::invalid_multiplier:
        return "invalid_multiplier";
    case LocalPlayerMovementStateErrorCode::invalid_revision:
        return "invalid_revision";
    case LocalPlayerMovementStateErrorCode::unsupported_profile:
        return "unsupported_profile";
    case LocalPlayerMovementStateErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(
    const GoldSrcMovementCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcMovementCompatibilityProfile::
            public_valve_pm_shared_dry_walk_subset_v1:
        return "public_valve_pm_shared_dry_walk_subset_v1";
    case GoldSrcMovementCompatibilityProfile::
            stock_pm_move_full_compatibility_evidence_pending:
        return "stock_pm_move_full_compatibility_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(const GoldSrcMovementEvidenceProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcMovementEvidenceProfile::
            public_valve_pm_shared_and_independent_fixtures:
        return "public_valve_pm_shared_and_independent_fixtures";
    case GoldSrcMovementEvidenceProfile::
            stock_pm_move_full_compatibility_evidence_pending:
        return "stock_pm_move_full_compatibility_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(const GoldSrcMovementCommandProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1:
        return "synthetic_usercmd_semantics_v1";
    case GoldSrcMovementCommandProfile::stock_usercmd_semantics_evidence_pending:
        return "stock_usercmd_semantics_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(const PlayerMovementHull hull) noexcept
{
    switch (hull) {
    case PlayerMovementHull::standing: return "standing";
    case PlayerMovementHull::ducked: return "ducked";
    }
    return "unknown";
}

std::string_view to_string(const PlayerMovementMode mode) noexcept
{
    switch (mode) {
    case PlayerMovementMode::walking: return "walking";
    case PlayerMovementMode::airborne: return "airborne";
    case PlayerMovementMode::unsupported_liquid: return "unsupported_liquid";
    case PlayerMovementMode::unsupported_ladder: return "unsupported_ladder";
    case PlayerMovementMode::invalid_or_stuck: return "invalid_or_stuck";
    }
    return "unknown";
}

std::string_view to_string(const PlayerMovementContents contents) noexcept
{
    switch (contents) {
    case PlayerMovementContents::empty: return "empty";
    case PlayerMovementContents::solid: return "solid";
    case PlayerMovementContents::water: return "water";
    case PlayerMovementContents::slime: return "slime";
    case PlayerMovementContents::lava: return "lava";
    case PlayerMovementContents::current: return "current";
    case PlayerMovementContents::sky: return "sky";
    case PlayerMovementContents::special: return "special";
    }
    return "unknown";
}

} // namespace hlclient::movement

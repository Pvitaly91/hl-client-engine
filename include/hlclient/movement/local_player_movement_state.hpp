#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::movement {

enum class GoldSrcMovementCompatibilityProfile : std::uint8_t {
    public_valve_pm_shared_dry_walk_subset_v1,
    stock_pm_move_full_compatibility_evidence_pending,
};

enum class GoldSrcMovementEvidenceProfile : std::uint8_t {
    public_valve_pm_shared_and_independent_fixtures,
    stock_pm_move_full_compatibility_evidence_pending,
};

enum class GoldSrcMovementCommandProfile : std::uint8_t {
    synthetic_usercmd_semantics_v1,
    stock_usercmd_semantics_evidence_pending,
};

enum class PlayerMovementHull : std::uint8_t {
    standing,
    ducked,
};

enum class PlayerMovementMode : std::uint8_t {
    walking,
    airborne,
    unsupported_liquid,
    unsupported_ladder,
    invalid_or_stuck,
};

enum class PlayerMovementContents : std::uint8_t {
    empty,
    solid,
    water,
    slime,
    lava,
    current,
    sky,
    special,
};

enum class PlayerMovementHitKind : std::uint8_t {
    world,
    explicit_synthetic_brush,
};

enum class PlayerGroundEvidenceProfile : std::uint8_t {
    deterministic_collision_trace_v1,
};

enum class PlayerMovementPhase : std::uint8_t {
    ground_probe,
    direct_slide,
    step_up,
    step_horizontal,
    step_down,
    airborne_slide,
    duck_transition,
    stand_transition,
};

struct PlayerMovementHitIdentity {
    PlayerMovementHitKind kind{PlayerMovementHitKind::world};
    std::uint32_t source_model_index{0U};
    std::optional<std::uint64_t> stable_instance_ordinal;
    std::optional<std::uint32_t> source_entity_index;

    [[nodiscard]] friend bool operator==(
        const PlayerMovementHitIdentity&,
        const PlayerMovementHitIdentity&) = default;
};

struct PlayerMovementPlane {
    assets::AssetVector3 normal{};
    double distance{0.0};
    std::optional<std::uint32_t> source_plane_index;

    [[nodiscard]] friend bool operator==(
        const PlayerMovementPlane& left,
        const PlayerMovementPlane& right) noexcept
    {
        return left.normal.x == right.normal.x &&
            left.normal.y == right.normal.y &&
            left.normal.z == right.normal.z &&
            left.distance == right.distance &&
            left.source_plane_index == right.source_plane_index;
    }
};

struct PlayerGroundStateCreateInfo {
    bool grounded{false};
    bool walkable{false};
    std::optional<PlayerMovementHitIdentity> hit;
    PlayerMovementPlane plane{};
    assets::AssetVector3 contact_position{};
    double probe_fraction{1.0};
    PlayerGroundEvidenceProfile evidence_profile{
        PlayerGroundEvidenceProfile::deterministic_collision_trace_v1};
};

class PlayerGroundState final {
public:
    PlayerGroundState(const PlayerGroundState&) = default;
    PlayerGroundState(PlayerGroundState&&) noexcept = default;
    PlayerGroundState& operator=(const PlayerGroundState&) = delete;
    PlayerGroundState& operator=(PlayerGroundState&&) = delete;
    ~PlayerGroundState() = default;

    [[nodiscard]] bool grounded() const noexcept;
    [[nodiscard]] bool walkable() const noexcept;
    [[nodiscard]] const std::optional<PlayerMovementHitIdentity>& hit()
        const noexcept;
    [[nodiscard]] const PlayerMovementPlane& plane() const noexcept;
    [[nodiscard]] const assets::AssetVector3& contact_position() const noexcept;
    [[nodiscard]] double probe_fraction() const noexcept;
    [[nodiscard]] PlayerGroundEvidenceProfile evidence_profile() const noexcept;

private:
    friend class LocalPlayerMovementState;

    explicit PlayerGroundState(
        const PlayerGroundStateCreateInfo& create_info) noexcept;

    bool grounded_{false};
    bool walkable_{false};
    std::optional<PlayerMovementHitIdentity> hit_;
    PlayerMovementPlane plane_{};
    assets::AssetVector3 contact_position_{};
    double probe_fraction_{1.0};
    PlayerGroundEvidenceProfile evidence_profile_{
        PlayerGroundEvidenceProfile::deterministic_collision_trace_v1};
};

struct LocalPlayerMovementStateLimits {
    float maximum_coordinate_magnitude{1'000'000.0F};
    float maximum_velocity_magnitude{1'000'000.0F};
    float maximum_angle_magnitude{360'000.0F};
    std::uint64_t maximum_state_revision{UINT64_MAX};
};

[[nodiscard]] bool valid_local_player_movement_state_limits(
    const LocalPlayerMovementStateLimits& limits) noexcept;

struct LocalPlayerMovementStateCreateInfo {
    assets::AssetVector3 origin{};
    assets::AssetVector3 velocity{};
    assets::AssetVector3 view_angles{};
    PlayerMovementHull hull{PlayerMovementHull::standing};
    PlayerMovementMode mode{PlayerMovementMode::airborne};
    PlayerGroundStateCreateInfo ground{};
    assets::AssetVector3 view_offset{0.0F, 0.0F, 28.0F};
    std::uint16_t old_buttons{0U};
    std::uint32_t source_command_sequence{0U};
    std::uint64_t simulation_time_nanoseconds{0U};
    PlayerMovementContents last_valid_contents{PlayerMovementContents::empty};
    float gravity_multiplier{1.0F};
    float friction_multiplier{1.0F};
    std::uint64_t state_revision{1U};
    GoldSrcMovementCompatibilityProfile compatibility_profile{
        GoldSrcMovementCompatibilityProfile::
            public_valve_pm_shared_dry_walk_subset_v1};
    GoldSrcMovementEvidenceProfile evidence_profile{
        GoldSrcMovementEvidenceProfile::
            public_valve_pm_shared_and_independent_fixtures};
    GoldSrcMovementCommandProfile command_profile{
        GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1};
};

enum class LocalPlayerMovementStateErrorCode : std::uint8_t {
    invalid_limits,
    non_finite_origin,
    non_finite_velocity,
    non_finite_angles,
    non_finite_view_offset,
    coordinate_limit_exceeded,
    velocity_limit_exceeded,
    angle_limit_exceeded,
    invalid_hull,
    invalid_mode,
    invalid_ground_state,
    invalid_multiplier,
    invalid_revision,
    unsupported_profile,
    stock_evidence_pending,
};

[[nodiscard]] std::string_view to_string(
    LocalPlayerMovementStateErrorCode code) noexcept;

struct LocalPlayerMovementStateError {
    LocalPlayerMovementStateErrorCode code{
        LocalPlayerMovementStateErrorCode::invalid_limits};
    std::string_view context;
};

class LocalPlayerMovementState final {
public:
    struct CreationResult;

    LocalPlayerMovementState(const LocalPlayerMovementState&) = default;
    LocalPlayerMovementState(LocalPlayerMovementState&&) noexcept = default;
    LocalPlayerMovementState& operator=(const LocalPlayerMovementState&) = delete;
    LocalPlayerMovementState& operator=(LocalPlayerMovementState&&) = delete;
    ~LocalPlayerMovementState() = default;

    [[nodiscard]] static CreationResult create(
        const LocalPlayerMovementStateCreateInfo& create_info,
        const LocalPlayerMovementStateLimits& limits = {}) noexcept;

    [[nodiscard]] const assets::AssetVector3& origin() const noexcept;
    [[nodiscard]] const assets::AssetVector3& velocity() const noexcept;
    [[nodiscard]] const assets::AssetVector3& view_angles() const noexcept;
    [[nodiscard]] PlayerMovementHull hull() const noexcept;
    [[nodiscard]] PlayerMovementMode mode() const noexcept;
    [[nodiscard]] const PlayerGroundState& ground_state() const noexcept;
    [[nodiscard]] const assets::AssetVector3& view_offset() const noexcept;
    [[nodiscard]] std::uint16_t old_buttons() const noexcept;
    [[nodiscard]] std::uint32_t source_command_sequence() const noexcept;
    [[nodiscard]] std::uint64_t simulation_time_nanoseconds() const noexcept;
    [[nodiscard]] PlayerMovementContents last_valid_contents() const noexcept;
    [[nodiscard]] float gravity_multiplier() const noexcept;
    [[nodiscard]] float friction_multiplier() const noexcept;
    [[nodiscard]] std::uint64_t state_revision() const noexcept;
    [[nodiscard]] GoldSrcMovementCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] GoldSrcMovementEvidenceProfile evidence_profile() const noexcept;
    [[nodiscard]] GoldSrcMovementCommandProfile command_profile() const noexcept;

private:
    explicit LocalPlayerMovementState(
        const LocalPlayerMovementStateCreateInfo& create_info) noexcept;

    assets::AssetVector3 origin_{};
    assets::AssetVector3 velocity_{};
    assets::AssetVector3 view_angles_{};
    PlayerMovementHull hull_{PlayerMovementHull::standing};
    PlayerMovementMode mode_{PlayerMovementMode::airborne};
    PlayerGroundState ground_state_;
    assets::AssetVector3 view_offset_{};
    std::uint16_t old_buttons_{0U};
    std::uint32_t source_command_sequence_{0U};
    std::uint64_t simulation_time_nanoseconds_{0U};
    PlayerMovementContents last_valid_contents_{PlayerMovementContents::empty};
    float gravity_multiplier_{1.0F};
    float friction_multiplier_{1.0F};
    std::uint64_t state_revision_{1U};
    GoldSrcMovementCompatibilityProfile compatibility_profile_{
        GoldSrcMovementCompatibilityProfile::
            public_valve_pm_shared_dry_walk_subset_v1};
    GoldSrcMovementEvidenceProfile evidence_profile_{
        GoldSrcMovementEvidenceProfile::
            public_valve_pm_shared_and_independent_fixtures};
    GoldSrcMovementCommandProfile command_profile_{
        GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1};
};

struct LocalPlayerMovementState::CreationResult {
    std::optional<LocalPlayerMovementState> state;
    std::optional<LocalPlayerMovementStateError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

struct PlayerMovementTouch {
    PlayerMovementHitIdentity hit{};
    PlayerMovementPlane plane{};
    double fraction{1.0};
    PlayerMovementPhase phase{PlayerMovementPhase::direct_slide};
    std::uint32_t source_command_sequence{0U};
};

struct PlayerMovementStatistics {
    std::uint64_t command_count{0U};
    std::uint64_t substep_count{0U};
    std::uint64_t grounded_command_count{0U};
    std::uint64_t airborne_command_count{0U};
    std::uint64_t ground_probe_count{0U};
    std::uint64_t trace_count{0U};
    std::uint64_t collision_hit_count{0U};
    std::uint64_t slide_bump_count{0U};
    std::uint64_t clip_plane_count{0U};
    std::uint64_t step_attempt_count{0U};
    std::uint64_t step_success_count{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t duck_enter_count{0U};
    std::uint64_t duck_exit_count{0U};
    std::uint64_t stand_blocked_count{0U};
    std::uint64_t start_solid_count{0U};
    std::uint64_t all_solid_count{0U};
    double total_horizontal_distance{0.0};
    double total_vertical_distance{0.0};
};

[[nodiscard]] std::uint64_t local_player_movement_state_signature(
    const LocalPlayerMovementState& state) noexcept;

[[nodiscard]] std::string_view to_string(
    GoldSrcMovementCompatibilityProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    GoldSrcMovementEvidenceProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    GoldSrcMovementCommandProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(PlayerMovementHull hull) noexcept;
[[nodiscard]] std::string_view to_string(PlayerMovementMode mode) noexcept;
[[nodiscard]] std::string_view to_string(PlayerMovementContents contents) noexcept;

} // namespace hlclient::movement

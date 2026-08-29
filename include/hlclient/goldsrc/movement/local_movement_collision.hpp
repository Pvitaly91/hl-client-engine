#pragma once

#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::movement {

enum class LocalMovementCollisionProfile : std::uint8_t {
    world_only_v1,
    explicit_synthetic_static_brush_v1,
};

[[nodiscard]] std::string_view to_string(
    LocalMovementCollisionProfile profile) noexcept;

struct LocalMovementCollisionContents {
    hlclient::movement::PlayerMovementContents category{
        hlclient::movement::PlayerMovementContents::empty};
    std::int32_t source_goldsrc_code{-1};

    [[nodiscard]] friend bool operator==(
        const LocalMovementCollisionContents&,
        const LocalMovementCollisionContents&) = default;
};

[[nodiscard]] std::optional<LocalMovementCollisionContents>
normalize_local_movement_contents(
    const hlclient::collision::CollisionContents& contents) noexcept;

[[nodiscard]] std::optional<hlclient::collision::CollisionHullOrdinal>
local_movement_collision_hull(
    hlclient::movement::PlayerMovementHull hull) noexcept;

struct LocalMovementCollisionTraversalStatistics {
    std::size_t point_contents_steps{0U};
    std::size_t traversal_steps{0U};
    std::size_t maximum_stack_entries{0U};
    std::size_t fraction_split_count{0U};
    std::size_t terminal_interval_count{0U};

    [[nodiscard]] friend bool operator==(
        const LocalMovementCollisionTraversalStatistics&,
        const LocalMovementCollisionTraversalStatistics&) = default;
};

struct LocalMovementPointContents {
    LocalMovementCollisionContents contents{};
    std::size_t traversal_depth{0U};
};

enum class LocalMovementPositionStatus : std::uint8_t {
    free,
    blocking,
};

struct LocalMovementPositionTest {
    LocalMovementPositionStatus status{LocalMovementPositionStatus::free};
    LocalMovementCollisionContents contents{};
    std::optional<hlclient::movement::PlayerMovementHitIdentity> hit;
    std::size_t traversal_depth{0U};
};

struct LocalMovementTrace {
    bool all_solid{false};
    bool start_solid{false};
    bool in_open{false};
    bool in_liquid{false};
    double fraction{1.0};
    assets::AssetVector3 end_position{};
    std::optional<hlclient::movement::PlayerMovementPlane> collision_plane;
    std::optional<hlclient::movement::PlayerMovementHitIdentity> hit;
    LocalMovementCollisionContents start_contents{};
    LocalMovementCollisionContents end_contents{};
    std::optional<LocalMovementCollisionContents> blocking_contents;
    LocalMovementCollisionTraversalStatistics traversal_statistics{};
    LocalMovementCollisionProfile collision_profile{
        LocalMovementCollisionProfile::world_only_v1};
};

struct LocalMovementCollisionSessionIdentity {
    LocalMovementCollisionProfile profile{
        LocalMovementCollisionProfile::world_only_v1};
    std::uint64_t collision_world_primary{0U};
    std::uint64_t collision_world_secondary{0U};
    std::uint64_t collision_world_revision{0U};
    std::uint64_t scene_signature{0U};

    [[nodiscard]] bool valid() const noexcept
    {
        const bool profile_valid =
            profile == LocalMovementCollisionProfile::world_only_v1 ||
            profile == LocalMovementCollisionProfile::
                explicit_synthetic_static_brush_v1;
        return profile_valid &&
            (collision_world_primary != 0U ||
                   collision_world_secondary != 0U) &&
            collision_world_revision != 0U && scene_signature != 0U;
    }

    [[nodiscard]] friend bool operator==(
        const LocalMovementCollisionSessionIdentity&,
        const LocalMovementCollisionSessionIdentity&) = default;
};

struct LocalMovementCollisionQueryConfig {
    hlclient::collision::CollisionQueryLimits query_limits{};
    hlclient::collision::CollisionTraceToleranceProfile trace_tolerance{};
    hlclient::goldsrc::collision::BrushCollisionSceneQueryLimits scene_limits{};
};

[[nodiscard]] bool valid_local_movement_collision_query_config(
    const LocalMovementCollisionQueryConfig& config) noexcept;

enum class LocalMovementCollisionErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_point,
    invalid_segment,
    invalid_hull,
    invalid_collision_source,
    unsupported_collision_profile,
    unresolved_synthetic_brush_role,
    world_query_failed,
    brush_scene_query_failed,
    non_finite_result,
};

[[nodiscard]] std::string_view to_string(
    LocalMovementCollisionErrorCode code) noexcept;

struct LocalMovementCollisionError {
    LocalMovementCollisionErrorCode code{
        LocalMovementCollisionErrorCode::invalid_configuration};
    std::optional<hlclient::collision::CollisionQueryError> world_query_error;
    std::optional<hlclient::goldsrc::collision::BrushCollisionSceneQueryError>
        brush_scene_error;
};

template<class Result>
struct LocalMovementCollisionQueryResult {
    std::optional<Result> result;
    std::optional<LocalMovementCollisionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return result.has_value() && !error.has_value();
    }
};

using LocalMovementPointContentsQueryResult =
    LocalMovementCollisionQueryResult<LocalMovementPointContents>;
using LocalMovementPositionQueryResult =
    LocalMovementCollisionQueryResult<LocalMovementPositionTest>;
using LocalMovementTraceQueryResult =
    LocalMovementCollisionQueryResult<LocalMovementTrace>;

class ILocalMovementCollision {
public:
    virtual ~ILocalMovementCollision() = default;

    [[nodiscard]] virtual LocalMovementCollisionProfile profile()
        const noexcept = 0;
    [[nodiscard]] virtual bool valid() const noexcept = 0;
    // Prediction must fail closed when a collision provider cannot identify
    // the immutable world/session it queries. Kept non-pure so legacy test
    // doubles remain source-compatible and explicitly identity-less.
    [[nodiscard]] virtual std::optional<LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept;

    // Point contents intentionally uses world model zero and point hull zero.
    // Explicit synthetic brushes are solid trace participants, not liquid
    // contents providers.
    [[nodiscard]] virtual LocalMovementPointContentsQueryResult point_contents(
        const assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const = 0;

    [[nodiscard]] virtual LocalMovementPositionQueryResult test_position(
        const assets::AssetVector3& origin,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const = 0;

    [[nodiscard]] virtual LocalMovementTraceQueryResult trace_hull(
        const assets::AssetVector3& start,
        const assets::AssetVector3& end,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const = 0;
};

class WorldOnlyMovementCollision final : public ILocalMovementCollision {
public:
    explicit WorldOnlyMovementCollision(
        std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
            package) noexcept;

    [[nodiscard]] LocalMovementCollisionProfile profile()
        const noexcept override;
    [[nodiscard]] bool valid() const noexcept override;
    [[nodiscard]] std::optional<LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept override;
    [[nodiscard]] const std::shared_ptr<
        const hlclient::collision::CollisionWorldPackage>&
    package() const noexcept;

    [[nodiscard]] LocalMovementPointContentsQueryResult point_contents(
        const assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override;
    [[nodiscard]] LocalMovementPositionQueryResult test_position(
        const assets::AssetVector3& origin,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override;
    [[nodiscard]] LocalMovementTraceQueryResult trace_hull(
        const assets::AssetVector3& start,
        const assets::AssetVector3& end,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override;

private:
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage> package_;
};

class SyntheticBrushMovementCollision final : public ILocalMovementCollision {
public:
    explicit SyntheticBrushMovementCollision(
        std::shared_ptr<
            const hlclient::goldsrc::collision::BrushCollisionScene>
            scene) noexcept;

    [[nodiscard]] LocalMovementCollisionProfile profile()
        const noexcept override;
    [[nodiscard]] bool valid() const noexcept override;
    [[nodiscard]] std::optional<LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept override;
    [[nodiscard]] const std::shared_ptr<
        const hlclient::goldsrc::collision::BrushCollisionScene>&
    scene() const noexcept;

    [[nodiscard]] LocalMovementPointContentsQueryResult point_contents(
        const assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override;
    [[nodiscard]] LocalMovementPositionQueryResult test_position(
        const assets::AssetVector3& origin,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override;
    [[nodiscard]] LocalMovementTraceQueryResult trace_hull(
        const assets::AssetVector3& start,
        const assets::AssetVector3& end,
        hlclient::movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalMovementCollisionQueryConfig& config = {}) const override;

private:
    [[nodiscard]] std::shared_ptr<
        const hlclient::collision::CollisionWorldPackage>
    world_package() const noexcept;

    std::shared_ptr<const hlclient::goldsrc::collision::BrushCollisionScene>
        scene_;
};

} // namespace hlclient::goldsrc::movement

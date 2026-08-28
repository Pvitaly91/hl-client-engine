#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include <cmath>
#include <utility>

namespace hlclient::goldsrc::movement {
namespace {

namespace core_collision = hlclient::collision;
namespace brush_collision = hlclient::goldsrc::collision;
namespace player_movement = hlclient::movement;

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_world_package(
    const std::shared_ptr<const core_collision::CollisionWorldPackage>&
        package) noexcept
{
    if (package == nullptr ||
        package->compatibility_profile() !=
            core_collision::CollisionWorldCompatibilityProfile::
                valve_bsp_v30_clip_hulls_v1 ||
        package->evidence_profile() !=
            core_collision::CollisionWorldEvidenceProfile::
                public_valve_bsp_compiler_and_original_map_validation) {
        return false;
    }
    const auto* world = package->model(0U);
    if (world == nullptr) {
        return false;
    }
    for (const auto ordinal : {
             core_collision::CollisionHullOrdinal::point,
             core_collision::CollisionHullOrdinal::standing_32x32x72,
             core_collision::CollisionHullOrdinal::duck_32x32x36}) {
        const auto* hull = world->hull(ordinal);
        const auto expected =
            core_collision::standard_collision_hull_profile(ordinal);
        if (hull == nullptr || !expected || hull->profile != *expected) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<LocalMovementCollisionErrorCode>
synthetic_scene_error(
    const std::shared_ptr<const brush_collision::BrushCollisionScene>& scene)
    noexcept
{
    if (scene == nullptr || scene->model_library() == nullptr ||
        !valid_world_package(scene->model_library()->collision_world())) {
        return LocalMovementCollisionErrorCode::invalid_collision_source;
    }
    if (scene->role_provider_profile() !=
        brush_collision::BrushCollisionRoleProviderProfile::
            explicit_synthetic_brush_solidity_v1) {
        return LocalMovementCollisionErrorCode::
            unsupported_collision_profile;
    }
    for (const auto& instance : scene->instances()) {
        if (instance.role_provider_profile !=
            brush_collision::BrushCollisionRoleProviderProfile::
                explicit_synthetic_brush_solidity_v1) {
            return LocalMovementCollisionErrorCode::
                unsupported_collision_profile;
        }
        if (instance.role == brush_collision::BrushCollisionRole::unsupported ||
            instance.role ==
                brush_collision::BrushCollisionRole::evidence_pending) {
            return LocalMovementCollisionErrorCode::
                unresolved_synthetic_brush_role;
        }
    }
    return std::nullopt;
}

template<class Result>
[[nodiscard]] LocalMovementCollisionQueryResult<Result> failure(
    const LocalMovementCollisionErrorCode code,
    std::optional<core_collision::CollisionQueryError> world_error =
        std::nullopt,
    std::optional<brush_collision::BrushCollisionSceneQueryError>
        scene_error = std::nullopt) noexcept
{
    return {
        std::nullopt,
        LocalMovementCollisionError{
            code, std::move(world_error), std::move(scene_error)},
    };
}

[[nodiscard]] std::optional<player_movement::PlayerMovementHitIdentity>
normalize_world_hit(const core_collision::CollisionTraceResult& trace) noexcept
{
    if (!trace.hit) {
        return std::nullopt;
    }
    if (trace.hit->kind != core_collision::CollisionTraceHitKind::world ||
        trace.hit->source_model_index != 0U) {
        return std::nullopt;
    }
    return player_movement::PlayerMovementHitIdentity{
        player_movement::PlayerMovementHitKind::world,
        0U,
        std::nullopt,
        std::nullopt,
    };
}

struct NormalizedSceneHit {
    bool valid{true};
    std::optional<player_movement::PlayerMovementHitIdentity> hit;
};

[[nodiscard]] NormalizedSceneHit normalize_scene_hit(
    const core_collision::CollisionTraceResult& trace,
    const std::optional<brush_collision::BrushCollisionSceneHit>& scene_hit)
    noexcept
{
    if (!scene_hit) {
        if (!trace.hit) {
            return {};
        }
        if (trace.hit->kind == core_collision::CollisionTraceHitKind::world &&
            trace.hit->source_model_index == 0U) {
            return {true,
                player_movement::PlayerMovementHitIdentity{
                    player_movement::PlayerMovementHitKind::world,
                    0U,
                    std::nullopt,
                    std::nullopt}};
        }
        // A brush hit without its stable scene identity is not publishable.
        return {false, std::nullopt};
    }
    if (scene_hit->kind == core_collision::CollisionTraceHitKind::world) {
        if (scene_hit->brush_instance ||
            (trace.hit &&
                (trace.hit->kind !=
                        core_collision::CollisionTraceHitKind::world ||
                    trace.hit->source_model_index != 0U))) {
            return {false, std::nullopt};
        }
        return {true,
            player_movement::PlayerMovementHitIdentity{
                player_movement::PlayerMovementHitKind::world,
                0U,
                std::nullopt,
                std::nullopt}};
    }
    if (scene_hit->kind !=
            core_collision::CollisionTraceHitKind::collision_model ||
        !scene_hit->brush_instance) {
        return {false, std::nullopt};
    }
    const auto& identity = *scene_hit->brush_instance;
    if (identity.source_model_index == 0U ||
        (trace.hit &&
            (trace.hit->kind !=
                    core_collision::CollisionTraceHitKind::collision_model ||
                trace.hit->source_model_index !=
                    identity.source_model_index))) {
        return {false, std::nullopt};
    }
    return {true,
        player_movement::PlayerMovementHitIdentity{
            player_movement::PlayerMovementHitKind::
                explicit_synthetic_brush,
            identity.source_model_index,
            identity.stable_instance_ordinal,
            identity.source_entity_index}};
}

[[nodiscard]] std::optional<LocalMovementTrace> normalize_trace(
    const core_collision::CollisionTraceResult& trace,
    const LocalMovementCollisionProfile profile,
    const std::optional<brush_collision::BrushCollisionSceneHit>& scene_hit =
        std::nullopt) noexcept
{
    if (!std::isfinite(trace.fraction) || trace.fraction < 0.0 ||
        trace.fraction > 1.0 || !finite_vector(trace.end_position)) {
        return std::nullopt;
    }
    const auto start_contents =
        normalize_local_movement_contents(trace.start_contents);
    const auto end_contents =
        normalize_local_movement_contents(trace.end_contents);
    if (!start_contents || !end_contents) {
        return std::nullopt;
    }
    std::optional<LocalMovementCollisionContents> blocking_contents;
    if (trace.blocking_contents) {
        blocking_contents =
            normalize_local_movement_contents(*trace.blocking_contents);
        if (!blocking_contents) {
            return std::nullopt;
        }
    }

    std::optional<player_movement::PlayerMovementPlane> plane;
    if (trace.collision_plane) {
        if (!finite_vector(trace.collision_plane->normal) ||
            !std::isfinite(trace.collision_plane->distance)) {
            return std::nullopt;
        }
        plane = player_movement::PlayerMovementPlane{
            trace.collision_plane->normal,
            trace.collision_plane->distance,
            trace.collision_plane->source_plane_index,
        };
    }

    std::optional<player_movement::PlayerMovementHitIdentity> hit;
    if (profile == LocalMovementCollisionProfile::world_only_v1) {
        hit = normalize_world_hit(trace);
        if (trace.hit && !hit) {
            return std::nullopt;
        }
    } else {
        const auto normalized = normalize_scene_hit(trace, scene_hit);
        if (!normalized.valid) {
            return std::nullopt;
        }
        hit = normalized.hit;
    }

    return LocalMovementTrace{
        trace.all_solid,
        trace.start_solid,
        trace.in_open,
        trace.in_liquid,
        trace.fraction,
        trace.end_position,
        std::move(plane),
        std::move(hit),
        *start_contents,
        *end_contents,
        std::move(blocking_contents),
        LocalMovementCollisionTraversalStatistics{
            trace.traversal_statistics.point_contents_steps,
            trace.traversal_statistics.traversal_steps,
            trace.traversal_statistics.maximum_stack_entries,
            trace.traversal_statistics.fraction_split_count,
            trace.traversal_statistics.terminal_interval_count,
        },
        profile,
    };
}

[[nodiscard]] core_collision::CollisionPointContentsRequest point_request(
    const assets::AssetVector3& point,
    const LocalMovementCollisionQueryConfig& config) noexcept
{
    core_collision::CollisionPointContentsRequest request;
    request.point = point;
    request.source_model_index = 0U;
    request.hull = core_collision::CollisionHullOrdinal::point;
    request.contents_policy =
        core_collision::CollisionContentsPolicy::project_solid_only_v1;
    request.limits = config.query_limits;
    return request;
}

[[nodiscard]] core_collision::CollisionPointContentsRequest position_request(
    const assets::AssetVector3& origin,
    const core_collision::CollisionHullOrdinal hull,
    const LocalMovementCollisionQueryConfig& config) noexcept
{
    auto request = point_request(origin, config);
    request.hull = hull;
    return request;
}

[[nodiscard]] core_collision::CollisionTraceRequest world_trace_request(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const core_collision::CollisionHullOrdinal hull,
    const LocalMovementCollisionQueryConfig& config) noexcept
{
    core_collision::CollisionTraceRequest request;
    request.start = start;
    request.end = end;
    request.source_model_index = 0U;
    request.hull = hull;
    request.contents_policy =
        core_collision::CollisionContentsPolicy::project_solid_only_v1;
    request.trace_profile =
        core_collision::CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1;
    request.tolerance = config.trace_tolerance;
    request.limits = config.query_limits;
    return request;
}

[[nodiscard]] brush_collision::BrushCollisionSceneTraceRequest
scene_trace_request(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const core_collision::CollisionHullOrdinal hull,
    const LocalMovementCollisionQueryConfig& config) noexcept
{
    brush_collision::BrushCollisionSceneTraceRequest request;
    request.start = start;
    request.end = end;
    request.include_world = true;
    request.hull = hull;
    request.contents_policy =
        core_collision::CollisionContentsPolicy::project_solid_only_v1;
    request.trace_profile =
        core_collision::CollisionTraceCompatibilityProfile::
            project_deterministic_bsp_hull_trace_v1;
    request.tolerance = config.trace_tolerance;
    request.query_limits = config.query_limits;
    request.scene_limits = config.scene_limits;
    return request;
}

} // namespace

std::string_view to_string(const LocalMovementCollisionProfile profile) noexcept
{
    switch (profile) {
    case LocalMovementCollisionProfile::world_only_v1:
        return "world_only_v1";
    case LocalMovementCollisionProfile::explicit_synthetic_static_brush_v1:
        return "explicit_synthetic_static_brush_v1";
    }
    return "unknown";
}

std::optional<LocalMovementCollisionContents> normalize_local_movement_contents(
    const core_collision::CollisionContents& contents) noexcept
{
    using Category = core_collision::CollisionContentsCategory;
    using Output = player_movement::PlayerMovementContents;
    switch (contents.category) {
    case Category::empty:
        return LocalMovementCollisionContents{Output::empty,
            contents.source.raw};
    case Category::solid:
        return LocalMovementCollisionContents{Output::solid,
            contents.source.raw};
    case Category::water:
        return LocalMovementCollisionContents{Output::water,
            contents.source.raw};
    case Category::slime:
        return LocalMovementCollisionContents{Output::slime,
            contents.source.raw};
    case Category::lava:
        return LocalMovementCollisionContents{Output::lava,
            contents.source.raw};
    case Category::current_0:
    case Category::current_90:
    case Category::current_180:
    case Category::current_270:
    case Category::current_up:
    case Category::current_down:
        return LocalMovementCollisionContents{Output::current,
            contents.source.raw};
    case Category::sky:
        return LocalMovementCollisionContents{Output::sky,
            contents.source.raw};
    case Category::origin:
    case Category::clip:
    case Category::translucent:
        return LocalMovementCollisionContents{Output::special,
            contents.source.raw};
    }
    return std::nullopt;
}

std::optional<core_collision::CollisionHullOrdinal>
local_movement_collision_hull(
    const player_movement::PlayerMovementHull hull) noexcept
{
    switch (hull) {
    case player_movement::PlayerMovementHull::standing:
        return core_collision::CollisionHullOrdinal::standing_32x32x72;
    case player_movement::PlayerMovementHull::ducked:
        return core_collision::CollisionHullOrdinal::duck_32x32x36;
    }
    return std::nullopt;
}

bool valid_local_movement_collision_query_config(
    const LocalMovementCollisionQueryConfig& config) noexcept
{
    return core_collision::valid_collision_query_limits(config.query_limits) &&
        core_collision::valid_collision_trace_tolerance_profile(
            config.trace_tolerance) &&
        brush_collision::valid_brush_collision_scene_query_limits(
            config.scene_limits);
}

std::string_view to_string(const LocalMovementCollisionErrorCode code) noexcept
{
    switch (code) {
    case LocalMovementCollisionErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalMovementCollisionErrorCode::invalid_point:
        return "invalid_point";
    case LocalMovementCollisionErrorCode::invalid_segment:
        return "invalid_segment";
    case LocalMovementCollisionErrorCode::invalid_hull:
        return "invalid_hull";
    case LocalMovementCollisionErrorCode::invalid_collision_source:
        return "invalid_collision_source";
    case LocalMovementCollisionErrorCode::unsupported_collision_profile:
        return "unsupported_collision_profile";
    case LocalMovementCollisionErrorCode::unresolved_synthetic_brush_role:
        return "unresolved_synthetic_brush_role";
    case LocalMovementCollisionErrorCode::world_query_failed:
        return "world_query_failed";
    case LocalMovementCollisionErrorCode::brush_scene_query_failed:
        return "brush_scene_query_failed";
    case LocalMovementCollisionErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

WorldOnlyMovementCollision::WorldOnlyMovementCollision(
    std::shared_ptr<const core_collision::CollisionWorldPackage> package)
    noexcept
    : package_{std::move(package)}
{
}

LocalMovementCollisionProfile WorldOnlyMovementCollision::profile()
    const noexcept
{
    return LocalMovementCollisionProfile::world_only_v1;
}

bool WorldOnlyMovementCollision::valid() const noexcept
{
    return valid_world_package(package_);
}

const std::shared_ptr<const core_collision::CollisionWorldPackage>&
WorldOnlyMovementCollision::package() const noexcept
{
    return package_;
}

LocalMovementPointContentsQueryResult
WorldOnlyMovementCollision::point_contents(
    const assets::AssetVector3& point,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    if (!valid_local_movement_collision_query_config(config)) {
        return failure<LocalMovementPointContents>(
            LocalMovementCollisionErrorCode::invalid_configuration);
    }
    if (!finite_vector(point)) {
        return failure<LocalMovementPointContents>(
            LocalMovementCollisionErrorCode::invalid_point);
    }
    if (!valid()) {
        return failure<LocalMovementPointContents>(
            LocalMovementCollisionErrorCode::invalid_collision_source);
    }
    core_collision::CollisionWorldQuery query{package_};
    auto queried = query.point_contents(point_request(point, config), scratch);
    if (!queried || !queried.result) {
        return failure<LocalMovementPointContents>(
            LocalMovementCollisionErrorCode::world_query_failed,
            std::move(queried.error));
    }
    const auto contents =
        normalize_local_movement_contents(queried.result->contents);
    if (!contents) {
        return failure<LocalMovementPointContents>(
            LocalMovementCollisionErrorCode::invalid_collision_source);
    }
    return {
        LocalMovementPointContents{
            *contents, queried.result->traversal_depth},
        std::nullopt,
    };
}

LocalMovementPositionQueryResult WorldOnlyMovementCollision::test_position(
    const assets::AssetVector3& origin,
    const player_movement::PlayerMovementHull hull,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    if (!valid_local_movement_collision_query_config(config)) {
        return failure<LocalMovementPositionTest>(
            LocalMovementCollisionErrorCode::invalid_configuration);
    }
    if (!finite_vector(origin)) {
        return failure<LocalMovementPositionTest>(
            LocalMovementCollisionErrorCode::invalid_point);
    }
    const auto ordinal = local_movement_collision_hull(hull);
    if (!ordinal) {
        return failure<LocalMovementPositionTest>(
            LocalMovementCollisionErrorCode::invalid_hull);
    }
    if (!valid()) {
        return failure<LocalMovementPositionTest>(
            LocalMovementCollisionErrorCode::invalid_collision_source);
    }
    core_collision::CollisionWorldQuery query{package_};
    auto queried = query.test_position(
        position_request(origin, *ordinal, config), scratch);
    if (!queried || !queried.result) {
        return failure<LocalMovementPositionTest>(
            LocalMovementCollisionErrorCode::world_query_failed,
            std::move(queried.error));
    }
    const auto contents =
        normalize_local_movement_contents(queried.result->contents);
    if (!contents) {
        return failure<LocalMovementPositionTest>(
            LocalMovementCollisionErrorCode::invalid_collision_source);
    }
    return {
        LocalMovementPositionTest{
            queried.result->status ==
                    core_collision::CollisionPositionStatus::blocking
                ? LocalMovementPositionStatus::blocking
                : LocalMovementPositionStatus::free,
            *contents,
            std::nullopt,
            queried.result->traversal_depth,
        },
        std::nullopt,
    };
}

LocalMovementTraceQueryResult WorldOnlyMovementCollision::trace_hull(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const player_movement::PlayerMovementHull hull,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    if (!valid_local_movement_collision_query_config(config)) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_configuration);
    }
    if (!finite_vector(start) || !finite_vector(end)) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_segment);
    }
    const auto ordinal = local_movement_collision_hull(hull);
    if (!ordinal) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_hull);
    }
    if (!valid()) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_collision_source);
    }
    core_collision::CollisionWorldQuery query{package_};
    auto queried = query.trace_hull(
        world_trace_request(start, end, *ordinal, config), scratch);
    if (!queried || !queried.result) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::world_query_failed,
            std::move(queried.error));
    }
    auto normalized = normalize_trace(
        *queried.result, LocalMovementCollisionProfile::world_only_v1);
    if (!normalized) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::non_finite_result);
    }
    return {std::move(*normalized), std::nullopt};
}

SyntheticBrushMovementCollision::SyntheticBrushMovementCollision(
    std::shared_ptr<const brush_collision::BrushCollisionScene> scene) noexcept
    : scene_{std::move(scene)}
{
}

LocalMovementCollisionProfile SyntheticBrushMovementCollision::profile()
    const noexcept
{
    return LocalMovementCollisionProfile::
        explicit_synthetic_static_brush_v1;
}

bool SyntheticBrushMovementCollision::valid() const noexcept
{
    return !synthetic_scene_error(scene_).has_value();
}

const std::shared_ptr<const brush_collision::BrushCollisionScene>&
SyntheticBrushMovementCollision::scene() const noexcept
{
    return scene_;
}

std::shared_ptr<const core_collision::CollisionWorldPackage>
SyntheticBrushMovementCollision::world_package() const noexcept
{
    return scene_ != nullptr && scene_->model_library() != nullptr
        ? scene_->model_library()->collision_world()
        : nullptr;
}

LocalMovementPointContentsQueryResult
SyntheticBrushMovementCollision::point_contents(
    const assets::AssetVector3& point,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    if (const auto error = synthetic_scene_error(scene_)) {
        return failure<LocalMovementPointContents>(*error);
    }
    // Synthetic static-solid brushes are deliberately not contents providers.
    WorldOnlyMovementCollision world{world_package()};
    return world.point_contents(point, scratch, config);
}

LocalMovementPositionQueryResult
SyntheticBrushMovementCollision::test_position(
    const assets::AssetVector3& origin,
    const player_movement::PlayerMovementHull hull,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    auto traced = trace_hull(origin, origin, hull, scratch, config);
    if (!traced || !traced.result) {
        return {std::nullopt, std::move(traced.error)};
    }
    auto& trace = *traced.result;
    const bool blocking = trace.start_solid || trace.all_solid ||
        trace.fraction < 1.0 || trace.hit.has_value() ||
        trace.blocking_contents.has_value();
    return {
        LocalMovementPositionTest{
            blocking ? LocalMovementPositionStatus::blocking
                     : LocalMovementPositionStatus::free,
            trace.blocking_contents.value_or(trace.start_contents),
            std::move(trace.hit),
            trace.traversal_statistics.traversal_steps,
        },
        std::nullopt,
    };
}

LocalMovementTraceQueryResult SyntheticBrushMovementCollision::trace_hull(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const player_movement::PlayerMovementHull hull,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    if (!valid_local_movement_collision_query_config(config)) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_configuration);
    }
    if (!finite_vector(start) || !finite_vector(end)) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_segment);
    }
    const auto ordinal = local_movement_collision_hull(hull);
    if (!ordinal) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_hull);
    }
    if (const auto error = synthetic_scene_error(scene_)) {
        return failure<LocalMovementTrace>(*error);
    }

    brush_collision::BrushCollisionSceneQuery query{scene_};
    auto queried = query.trace_hull(
        scene_trace_request(start, end, *ordinal, config), scratch);
    if (!queried || !queried.result) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::brush_scene_query_failed,
            std::nullopt,
            std::move(queried.error));
    }
    auto normalized = normalize_trace(
        queried.result->trace,
        LocalMovementCollisionProfile::explicit_synthetic_static_brush_v1,
        queried.result->scene_hit);
    if (!normalized) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::non_finite_result);
    }
    return {std::move(*normalized), std::nullopt};
}

} // namespace hlclient::goldsrc::movement

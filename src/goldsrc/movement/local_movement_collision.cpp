#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace hlclient::goldsrc::movement {
bool valid_server_point_box_exclusion_rules(
    const std::span<const ServerPointBoxExclusionRule> rules) noexcept
{
    if (rules.size()>kMaximumServerPointBoxExclusionRules) return false;
    for (std::size_t i=0; i<rules.size(); ++i) {
        const auto& rule=rules[i];
        // Opaque virtual tokens, not native path capabilities. No file is
        // opened here; only an exact current-session resource name may match.
        if (rule.model_name.empty() || rule.model_name.size()>128U ||
            rule.model_name.find('\0')!=std::string::npos || rule.move_type>255U)
            return false;
        for (std::size_t j=0; j<i; ++j)
            if (rules[j].model_name==rule.model_name && rules[j].move_type==rule.move_type)
                return false;
    }
    return true;
}
namespace {

namespace core_collision = hlclient::collision;
namespace brush_collision = hlclient::goldsrc::collision;
namespace player_movement = hlclient::movement;

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

class StableIdentityHash final {
public:
    template<class Value>
        requires(std::is_integral_v<Value> || std::is_enum_v<Value>)
    void mix(const Value value) noexcept
    {
        mix_unsigned(static_cast<std::uint64_t>(value));
    }

    void mix(const float value) noexcept
    {
        mix_unsigned(std::bit_cast<std::uint32_t>(value));
    }

    void mix(const double value) noexcept
    {
        mix_unsigned(std::bit_cast<std::uint64_t>(value));
    }

    void mix(const assets::AssetVector3& value) noexcept
    {
        mix(value.x);
        mix(value.y);
        mix(value.z);
    }

    void mix(const assets::WorldBounds& value) noexcept
    {
        mix(value.minimum);
        mix(value.maximum);
    }

    [[nodiscard]] assets::AssetSourceFingerprint finish() const noexcept
    {
        return {
            primary_ == 0U ? kPrimaryFallback : primary_,
            secondary_ == 0U ? kSecondaryFallback : secondary_,
        };
    }

private:
    void mix_unsigned(const std::uint64_t value) noexcept
    {
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            const auto shift = static_cast<unsigned int>(index * 8U);
            const auto byte = static_cast<std::uint8_t>(value >> shift);
            primary_ ^= byte;
            primary_ *= kFnvPrime;
            secondary_ ^= byte;
            secondary_ *= kFnvPrime;
        }
    }
    static constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
    static constexpr std::uint64_t kPrimaryFallback =
        14'695'981'039'346'656'037ULL;
    static constexpr std::uint64_t kSecondaryFallback =
        10'965'116'821'109'875'189ULL;

    std::uint64_t primary_{14'695'981'039'346'656'037ULL};
    std::uint64_t secondary_{
        14'695'981'039'346'656'037ULL ^ 0x9E37'79B9'7F4A'7C15ULL};
};

void hash_contents(
    StableIdentityHash& hash,
    const core_collision::CollisionContents& contents) noexcept
{
    hash.mix(static_cast<std::uint64_t>(
        std::bit_cast<std::uint32_t>(contents.source.raw)));
    hash.mix(static_cast<std::uint64_t>(contents.category));
}

[[nodiscard]] assets::AssetSourceFingerprint derived_package_fingerprint(
    const core_collision::CollisionWorldPackage& package) noexcept
{
    StableIdentityHash hash;
    hash.mix(0x484C'434F'4C4C'5631ULL); // HLCOLLV1
    hash.mix(static_cast<std::uint64_t>(package.compatibility_profile()));
    hash.mix(static_cast<std::uint64_t>(package.evidence_profile()));

    const auto& identity = package.identity();
    hash.mix(identity.source_fingerprint.has_value() ? 1U : 0U);
    if (identity.source_fingerprint) {
        hash.mix(identity.source_fingerprint->primary);
        hash.mix(identity.source_fingerprint->secondary);
    }
    hash.mix(identity.source_revision);

    hash.mix(static_cast<std::uint64_t>(package.planes().size()));
    for (const auto& plane : package.planes()) {
        hash.mix(plane.normal);
        hash.mix(plane.distance);
        hash.mix(plane.source_plane_index);
        hash.mix(static_cast<std::uint64_t>(
            std::bit_cast<std::uint32_t>(plane.source_type)));
    }

    hash.mix(static_cast<std::uint64_t>(package.nodes().size()));
    for (const auto& node : package.nodes()) {
        hash.mix(node.plane_index);
        for (const auto& child : node.children) {
            hash.mix(static_cast<std::uint64_t>(child.kind));
            hash.mix(child.index);
        }
    }

    hash.mix(static_cast<std::uint64_t>(package.leaves().size()));
    for (const auto& leaf : package.leaves()) {
        hash.mix(leaf.source_leaf_index);
        hash_contents(hash, leaf.contents);
    }

    hash.mix(static_cast<std::uint64_t>(package.clipnodes().size()));
    for (const auto& clipnode : package.clipnodes()) {
        hash.mix(clipnode.plane_index);
        for (const auto& child : clipnode.children) {
            hash.mix(static_cast<std::uint64_t>(child.kind));
            hash.mix(child.index);
            hash_contents(hash, child.terminal);
        }
    }

    hash.mix(static_cast<std::uint64_t>(package.models().size()));
    for (const auto& model : package.models()) {
        hash.mix(model.source_model_index);
        hash.mix(model.source_origin);
        hash.mix(model.source_bounds);
        hash.mix(model.first_source_face);
        hash.mix(model.source_face_count);
        for (const auto& hull : model.hulls) {
            hash.mix(static_cast<std::uint64_t>(hull.ordinal));
            hash.mix(static_cast<std::uint64_t>(hull.domain));
            hash.mix(static_cast<std::uint64_t>(hull.root.kind));
            hash.mix(hull.root.index);
            hash_contents(hash, hull.root.terminal);
            hash.mix(static_cast<std::uint64_t>(hull.profile.ordinal));
            hash.mix(hull.profile.clip_mins);
            hash.mix(hull.profile.clip_maxs);
        }
    }

    const auto& statistics = package.statistics();
    hash.mix(statistics.plane_count);
    hash.mix(statistics.node_count);
    hash.mix(statistics.leaf_count);
    hash.mix(statistics.clipnode_count);
    hash.mix(statistics.model_count);
    hash.mix(statistics.reachable_hull0_nodes);
    hash.mix(statistics.reachable_clipnodes);
    hash.mix(statistics.unreachable_clipnodes);
    hash.mix(statistics.model_hull_root_count);
    hash.mix(statistics.direct_terminal_root_count);
    hash.mix(statistics.maximum_tree_depth);
    return hash.finish();
}

[[nodiscard]] assets::AssetSourceFingerprint collision_world_fingerprint(
    const core_collision::CollisionWorldPackage& package) noexcept
{
    const auto& source = package.identity().source_fingerprint;
    if (source && (source->primary != 0U || source->secondary != 0U)) {
        return *source;
    }
    return derived_package_fingerprint(package);
}

[[nodiscard]] std::uint64_t scene_identity_signature(
    const brush_collision::BrushCollisionScene& scene) noexcept
{
    StableIdentityHash hash;
    hash.mix(0x484C'5343'454E'4531ULL); // HLSCENE1
    hash.mix(static_cast<std::uint64_t>(scene.role_provider_profile()));
    hash.mix(static_cast<std::uint64_t>(scene.instances().size()));
    for (const auto& instance : scene.instances()) {
        hash.mix(instance.identity.stable_instance_ordinal);
        hash.mix(instance.identity.source_model_index);
        hash.mix(instance.identity.source_entity_index.has_value() ? 1U : 0U);
        if (instance.identity.source_entity_index) {
            hash.mix(*instance.identity.source_entity_index);
        }
        hash.mix(instance.transform.translation);
        hash.mix(instance.transform.rotation_basis.local_x_in_world);
        hash.mix(instance.transform.rotation_basis.local_y_in_world);
        hash.mix(instance.transform.rotation_basis.local_z_in_world);
        hash.mix(instance.transform.rotation_degrees);
        hash.mix(instance.transform.source_model_origin);
        hash.mix(static_cast<std::uint64_t>(
            instance.transform.coordinate_profile));
        hash.mix(static_cast<std::uint64_t>(
            instance.transform.transform_profile));
        hash.mix(instance.transformed_bounds);
        hash.mix(static_cast<std::uint64_t>(instance.role));
        hash.mix(static_cast<std::uint64_t>(
            instance.role_provider_profile));
    }
    return hash.finish().primary;
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
            explicit_synthetic_brush_solidity_v1 &&
        scene->role_provider_profile() != brush_collision::BrushCollisionRoleProviderProfile::reference_entity_solid_bsp_v1) {
        return LocalMovementCollisionErrorCode::
            unsupported_collision_profile;
    }
    for (const auto& instance : scene->instances()) {
        if (instance.role_provider_profile != scene->role_provider_profile()) {
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
        if (hit && hit->kind == player_movement::PlayerMovementHitKind::explicit_synthetic_brush &&
            profile == LocalMovementCollisionProfile::reference_brush_scene_v1)
            hit->kind = player_movement::PlayerMovementHitKind::brush_entity;
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

std::optional<LocalMovementCollisionSessionIdentity>
ILocalMovementCollision::session_identity() const noexcept
{
    return std::nullopt;
}

std::string_view to_string(const LocalMovementCollisionProfile profile) noexcept
{
    switch (profile) {
    case LocalMovementCollisionProfile::reference_brush_scene_v1:
        return "reference_brush_scene_v1";
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

std::optional<LocalMovementCollisionSessionIdentity>
WorldOnlyMovementCollision::session_identity() const noexcept
{
    if (!valid()) {
        return std::nullopt;
    }
    const auto& source = package_->identity();
    const auto fingerprint = collision_world_fingerprint(*package_);
    return LocalMovementCollisionSessionIdentity{
        profile(),
        fingerprint.primary,
        fingerprint.secondary,
        source.source_revision,
        1U,
    };
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

BrushSceneMovementCollision::BrushSceneMovementCollision(
    std::shared_ptr<const brush_collision::BrushCollisionScene> scene,
    const std::span<const ServerEntityCollisionBox> boxes)
    : scene_{std::move(scene)}
{
    boxes_valid_=boxes.size()<=kMaximumServerEntityCollisionBoxes;
    if (!boxes_valid_) return;
    for (const auto& box:boxes) {
        const auto valid_axis=[](float low,float high) {
            return std::isfinite(low) && std::isfinite(high) && low<=high &&
                std::abs(low)<=1'000'000.F && std::abs(high)<=1'000'000.F && high-low<=8192.F;
        };
        if (!box.entity || !valid_axis(box.bounds.minimum.x,box.bounds.maximum.x) ||
            !valid_axis(box.bounds.minimum.y,box.bounds.maximum.y) ||
            !valid_axis(box.bounds.minimum.z,box.bounds.maximum.z)) boxes_valid_=false;
    }
    if (boxes_valid_) boxes_.assign(boxes.begin(),boxes.end());
}

LocalMovementCollisionProfile BrushSceneMovementCollision::profile()
    const noexcept
{
    if (scene_ && scene_->role_provider_profile() ==
        brush_collision::BrushCollisionRoleProviderProfile::reference_entity_solid_bsp_v1)
        return LocalMovementCollisionProfile::reference_brush_scene_v1;
    return LocalMovementCollisionProfile::
        explicit_synthetic_static_brush_v1;
}

bool BrushSceneMovementCollision::valid() const noexcept
{
    return boxes_valid_ && !synthetic_scene_error(scene_).has_value();
}

std::optional<LocalMovementCollisionSessionIdentity>
BrushSceneMovementCollision::session_identity() const noexcept
{
    const auto package = world_package();
    if (!valid() || !package || !scene_) {
        return std::nullopt;
    }
    const auto& source = package->identity();
    const auto fingerprint = collision_world_fingerprint(*package);
    auto signature=scene_identity_signature(*scene_);
    if (!boxes_.empty()) {
        StableIdentityHash box_hash; box_hash.mix(signature);
        for(const auto& box:boxes_) { box_hash.mix(box.entity); box_hash.mix(box.bounds); }
        signature=box_hash.finish().primary;
    }
    return LocalMovementCollisionSessionIdentity{
        profile(),
        fingerprint.primary,
        fingerprint.secondary,
        source.source_revision,
        signature,
    };
}

const std::shared_ptr<const brush_collision::BrushCollisionScene>&
BrushSceneMovementCollision::scene() const noexcept
{
    return scene_;
}

std::shared_ptr<const core_collision::CollisionWorldPackage>
BrushSceneMovementCollision::world_package() const noexcept
{
    return scene_ != nullptr && scene_->model_library() != nullptr
        ? scene_->model_library()->collision_world()
        : nullptr;
}

LocalMovementPointContentsQueryResult
BrushSceneMovementCollision::point_contents(
    const assets::AssetVector3& point,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    if (const auto error = synthetic_scene_error(scene_)) {
        return failure<LocalMovementPointContents>(*error);
    }
    // Explicit solid brushes (synthetic or committed reference) are trace
    // participants, not inferred liquid contents providers.
    WorldOnlyMovementCollision world{world_package()};
    return world.point_contents(point, scratch, config);
}

LocalMovementPositionQueryResult
BrushSceneMovementCollision::test_position(
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

LocalMovementTraceQueryResult BrushSceneMovementCollision::trace_hull(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const player_movement::PlayerMovementHull hull,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    return trace_shape(start,end,local_movement_collision_hull(hull),scratch,config);
}
LocalMovementTraceQueryResult BrushSceneMovementCollision::trace_point(
    const assets::AssetVector3& start, const assets::AssetVector3& end,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    return trace_shape(start,end,core_collision::CollisionHullOrdinal::point,scratch,config);
}
LocalMovementTraceQueryResult BrushSceneMovementCollision::trace_shape(
    const assets::AssetVector3& start, const assets::AssetVector3& end,
    const std::optional<core_collision::CollisionHullOrdinal> ordinal,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config) const
{
    if (!boxes_valid_) return failure<LocalMovementTrace>(LocalMovementCollisionErrorCode::invalid_configuration);
    if (!valid_local_movement_collision_query_config(config)) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_configuration);
    }
    if (!finite_vector(start) || !finite_vector(end)) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::invalid_segment);
    }
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
        profile(),
        queried.result->scene_hit);
    if (!normalized) {
        return failure<LocalMovementTrace>(
            LocalMovementCollisionErrorCode::non_finite_result);
    }
    // Axis-aligned server box Minkowski-expanded by the existing player hull.
    // This is a query against one immutable committed frame, not object physics.
    const auto hull_profile=core_collision::standard_collision_hull_profile(*ordinal);
    if (!hull_profile) return failure<LocalMovementTrace>(LocalMovementCollisionErrorCode::invalid_hull);
    for (const auto& box:boxes_) {
        const std::array<double,3> low{box.bounds.minimum.x-hull_profile->clip_maxs.x,
            box.bounds.minimum.y-hull_profile->clip_maxs.y,box.bounds.minimum.z-hull_profile->clip_maxs.z};
        const std::array<double,3> high{box.bounds.maximum.x-hull_profile->clip_mins.x,
            box.bounds.maximum.y-hull_profile->clip_mins.y,box.bounds.maximum.z-hull_profile->clip_mins.z};
        const std::array<double,3> from{start.x,start.y,start.z}, to{end.x,end.y,end.z};
        bool inside_from=true,inside_to=true,miss=false;
        double enter=0.0,leave=1.0; int enter_axis=-1; double normal_sign=0.0;
        for (std::size_t axis=0;axis<3;++axis) {
            inside_from=inside_from && from[axis]>low[axis] && from[axis]<high[axis];
            inside_to=inside_to && to[axis]>low[axis] && to[axis]<high[axis];
            const auto delta=to[axis]-from[axis];
            if (delta==0.0) { if (from[axis]<=low[axis] || from[axis]>=high[axis]) miss=true; continue; }
            const auto near=(delta>0.0 ? low[axis] : high[axis]);
            const auto far=(delta>0.0 ? high[axis] : low[axis]);
            const auto first=(near-from[axis])/delta,last=(far-from[axis])/delta;
            if (first>=enter) { enter=first;enter_axis=static_cast<int>(axis);normal_sign=delta>0.0 ? -1.0 : 1.0; }
            leave=std::min(leave,last);
        }
        normalized->traversal_statistics.traversal_steps+=6U;
        if (inside_from) {
            normalized->start_solid=true;
            normalized->start_contents={player_movement::PlayerMovementContents::solid,-2};
            if (inside_to) {
                normalized->all_solid=true;normalized->fraction=0;normalized->end_position=start;
                normalized->end_contents={player_movement::PlayerMovementContents::solid,-2};
                normalized->blocking_contents=normalized->start_contents;
                normalized->hit=player_movement::PlayerMovementHitIdentity{
                    player_movement::PlayerMovementHitKind::server_entity_box,0,box.entity,box.entity};
                normalized->collision_plane.reset();
            }
            continue;
        }
        // Occupancy above is strictly interior. An entry and exit at the
        // same instant only touch a free corner/edge; clipping that tangent
        // invents a wall or floor during a jump. Real penetration/support
        // retains a positive interval, including boundary-to-interior t=0.
        if (miss || enter_axis<0 || enter>=leave || enter<0.0 || enter>=1.0 || leave<0.0) continue;
        const auto axis=static_cast<std::size_t>(enter_axis);
        // Match this query's declared plane tolerance; no new gameplay epsilon.
        const auto fraction=std::max(0.0,enter-config.trace_tolerance.plane_distance_epsilon/
            std::abs(to[axis]-from[axis]));
        if (fraction>=normalized->fraction) continue;
        assets::AssetVector3 normal{};
        if (axis==0) normal.x=static_cast<float>(normal_sign);
        else if(axis==1) normal.y=static_cast<float>(normal_sign); else normal.z=static_cast<float>(normal_sign);
        normalized->fraction=fraction;
        normalized->end_position={static_cast<float>(start.x+(end.x-start.x)*fraction),
            static_cast<float>(start.y+(end.y-start.y)*fraction),static_cast<float>(start.z+(end.z-start.z)*fraction)};
        normalized->collision_plane=player_movement::PlayerMovementPlane{
            normal,normal_sign*(normal_sign<0.0 ? low[axis] : high[axis]),{}};
        normalized->hit=player_movement::PlayerMovementHitIdentity{
            player_movement::PlayerMovementHitKind::server_entity_box,0,box.entity,box.entity};
        normalized->blocking_contents=LocalMovementCollisionContents{player_movement::PlayerMovementContents::solid,-2};
    }
    return {std::move(*normalized), std::nullopt};
}

std::optional<assets::AssetVector3> resolve_movement_boundary_position(
    const assets::AssetVector3& origin, const player_movement::PlayerMovementHull hull,
    const ILocalMovementCollision& collision,
    core_collision::CollisionQueryScratch& scratch,
    const LocalMovementCollisionQueryConfig& config)
{
    if (!finite_vector(origin) || !collision.valid()) return std::nullopt;
    const auto original = collision.test_position(origin, hull, scratch, config);
    if (!original || !original.result) return std::nullopt;
    if (original.result->status == LocalMovementPositionStatus::free)
        return original.result->contents.category == player_movement::PlayerMovementContents::empty && !original.result->hit
            ? std::optional{origin} : std::nullopt;
    if (original.result->status != LocalMovementPositionStatus::blocking ||
        original.result->contents.category != player_movement::PlayerMovementContents::solid) return std::nullopt;
    // The world-only adapter has no per-object hit metadata: its validated
    // profile already proves that this query contains only BSP model zero.
    if (collision.profile() != LocalMovementCollisionProfile::world_only_v1 &&
        (!original.result->hit ||
        (original.result->hit->kind != player_movement::PlayerMovementHitKind::world &&
         original.result->hit->kind != player_movement::PlayerMovementHitKind::brush_entity &&
         original.result->hit->kind != player_movement::PlayerMovementHitKind::explicit_synthetic_brush)))
        return std::nullopt;
    std::optional<assets::AssetVector3> best;
    double best_distance = std::numeric_limits<double>::infinity();
    const auto step = [](const float value, const int direction) {
        return direction == 0 ? value : std::nextafter(value,
            direction < 0 ? -std::numeric_limits<float>::infinity()
                          : std::numeric_limits<float>::infinity());
    };
    for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y)
        for (int z = -1; z <= 1; ++z) {
            if (x == 0 && y == 0 && z == 0) continue;
            const assets::AssetVector3 candidate{step(origin.x, x), step(origin.y, y), step(origin.z, z)};
            if (!finite_vector(candidate)) continue;
            const double dx = static_cast<double>(candidate.x) - origin.x;
            const double dy = static_cast<double>(candidate.y) - origin.y;
            const double dz = static_cast<double>(candidate.z) - origin.z;
            const double distance = dx * dx + dy * dy + dz * dz;
            if (distance >= best_distance || distance > (1.0 / 64.0) * (1.0 / 64.0)) continue;
            const auto clear = collision.test_position(candidate, hull, scratch, config);
            if (!clear || !clear.result) return std::nullopt;
            if (clear.result->status != LocalMovementPositionStatus::free ||
                clear.result->hit ||
                clear.result->contents.category != player_movement::PlayerMovementContents::empty) continue;
            best = candidate;
            best_distance = distance;
        }
    return best;
}

} // namespace hlclient::goldsrc::movement

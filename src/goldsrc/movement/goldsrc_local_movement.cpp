#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>

#include <hlclient/goldsrc/movement/goldsrc_movement_math.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::movement {
namespace {

using hlclient::movement::LocalPlayerMovementState;
using hlclient::movement::LocalPlayerMovementStateCreateInfo;
using hlclient::movement::PlayerGroundStateCreateInfo;
using hlclient::movement::PlayerMovementContents;
using hlclient::movement::PlayerMovementHull;
using hlclient::movement::PlayerMovementMode;
using hlclient::movement::PlayerMovementPhase;
using hlclient::movement::PlayerMovementStatistics;
using hlclient::movement::PlayerMovementTouch;

struct WorkingState {
    assets::AssetVector3 origin{};
    assets::AssetVector3 velocity{};
    assets::AssetVector3 view_angles{};
    PlayerMovementHull hull{PlayerMovementHull::standing};
    PlayerMovementMode mode{PlayerMovementMode::airborne};
    PlayerGroundStateCreateInfo ground{};
    assets::AssetVector3 view_offset{};
    PlayerMovementContents last_contents{PlayerMovementContents::empty};
    float friction_multiplier{1.0F};
};

struct OperationError {
    LocalMovementSimulationErrorCode code{
        LocalMovementSimulationErrorCode::movement_trace_failed};
    std::optional<LocalMovementCollisionError> collision_error;
    std::string_view context;
};

struct SlideResult {
    assets::AssetVector3 origin{};
    assets::AssetVector3 velocity{};
    GoldSrcClipBlockedAxis blocked_axes{GoldSrcClipBlockedAxis::none};
    std::optional<OperationError> error;
};

struct StepResult {
    bool available{false};
    assets::AssetVector3 origin{};
    assets::AssetVector3 velocity{};
    std::optional<OperationError> error;
};

enum class PlaneRelation : std::uint8_t {
    distinct,
    same_facing,
    opposing,
};

[[nodiscard]] bool trace_has_contact(
    const LocalMovementTrace& trace) noexcept
{
    return trace.collision_plane.has_value() && trace.hit.has_value() &&
        trace.blocking_contents.has_value();
}

[[nodiscard]] PlayerMovementDiagnosticHitKind diagnostic_hit_kind(
    const std::optional<hlclient::movement::PlayerMovementHitIdentity>& hit)
    noexcept
{
    if (!hit) {
        return PlayerMovementDiagnosticHitKind::none;
    }
    switch (hit->kind) {
    case hlclient::movement::PlayerMovementHitKind::world:
        return PlayerMovementDiagnosticHitKind::world;
    case hlclient::movement::PlayerMovementHitKind::explicit_synthetic_brush:
        return PlayerMovementDiagnosticHitKind::explicit_synthetic_brush;
    }
    return PlayerMovementDiagnosticHitKind::none;
}

[[nodiscard]] PlaneRelation plane_relation(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    constexpr float tolerance = 1.0e-4F;
    const auto dot = movement_dot(left, right);
    if (!std::isfinite(dot)) {
        return PlaneRelation::distinct;
    }
    if (dot >= 1.0F - tolerance) {
        return PlaneRelation::same_facing;
    }
    if (dot <= -1.0F + tolerance) {
        return PlaneRelation::opposing;
    }
    return PlaneRelation::distinct;
}

[[nodiscard]] bool equivalent_touch_plane(
    const hlclient::movement::PlayerMovementPlane& left,
    const hlclient::movement::PlayerMovementPlane& right) noexcept
{
    constexpr double distance_tolerance = 1.0e-4;
    return plane_relation(left.normal, right.normal) ==
            PlaneRelation::same_facing &&
        std::abs(left.distance - right.distance) <= distance_tolerance;
}

[[nodiscard]] bool opposing_coplanar_planes(
    const hlclient::movement::PlayerMovementPlane& left,
    const hlclient::movement::PlayerMovementPlane& right) noexcept
{
    constexpr double distance_tolerance = 1.0e-4;
    return plane_relation(left.normal, right.normal) ==
            PlaneRelation::opposing &&
        std::abs(left.distance + right.distance) <= distance_tolerance;
}

[[nodiscard]] std::optional<OperationError> append_touch(
    std::vector<PlayerMovementTouch>& touches,
    const PlayerMovementTouch& touch,
    const std::size_t maximum_touches)
{
    for (auto& retained : touches) {
        if (retained.hit == touch.hit && retained.phase == touch.phase &&
            equivalent_touch_plane(retained.plane, touch.plane)) {
            retained.fraction = std::min(retained.fraction, touch.fraction);
            return std::nullopt;
        }
    }
    if (touches.size() >= maximum_touches) {
        return OperationError{
            LocalMovementSimulationErrorCode::touch_limit_exceeded,
            std::nullopt,
            "movement touch limit exceeded"};
    }
    try {
        touches.push_back(touch);
    } catch (const std::bad_alloc&) {
        return OperationError{
            LocalMovementSimulationErrorCode::allocation_failed,
            std::nullopt,
            "movement touch allocation failed"};
    } catch (const std::length_error&) {
        return OperationError{
            LocalMovementSimulationErrorCode::allocation_failed,
            std::nullopt,
            "movement touch storage length failed"};
    }
    return std::nullopt;
}

[[nodiscard]] bool merge_statistics(
    PlayerMovementStatistics& target,
    const PlayerMovementStatistics& delta) noexcept
{
    auto merged = target;
    constexpr std::array counters{
        &PlayerMovementStatistics::command_count,
        &PlayerMovementStatistics::substep_count,
        &PlayerMovementStatistics::grounded_command_count,
        &PlayerMovementStatistics::airborne_command_count,
        &PlayerMovementStatistics::ground_probe_count,
        &PlayerMovementStatistics::trace_count,
        &PlayerMovementStatistics::collision_hit_count,
        &PlayerMovementStatistics::slide_bump_count,
        &PlayerMovementStatistics::clip_plane_count,
        &PlayerMovementStatistics::step_attempt_count,
        &PlayerMovementStatistics::step_success_count,
        &PlayerMovementStatistics::jump_count,
        &PlayerMovementStatistics::duck_enter_count,
        &PlayerMovementStatistics::duck_exit_count,
        &PlayerMovementStatistics::stand_blocked_count,
        &PlayerMovementStatistics::start_solid_count,
        &PlayerMovementStatistics::all_solid_count,
    };
    for (const auto counter : counters) {
        if (delta.*counter >
            std::numeric_limits<std::uint64_t>::max() - merged.*counter) {
            return false;
        }
        merged.*counter += delta.*counter;
    }
    const auto horizontal = merged.total_horizontal_distance +
        delta.total_horizontal_distance;
    const auto vertical = merged.total_vertical_distance +
        delta.total_vertical_distance;
    if (!std::isfinite(horizontal) || !std::isfinite(vertical)) {
        return false;
    }
    merged.total_horizontal_distance = horizontal;
    merged.total_vertical_distance = vertical;
    target = merged;
    return true;
}

void record_diagnostic(
    GoldSrcLocalMovementScratch& scratch,
    PlayerWallContactDiagnosticFrame frame) noexcept
{
    scratch.diagnostics.push(frame);
    if (const auto* latest = scratch.diagnostics.latest()) {
        scratch.last_diagnostic = *latest;
    }
}

[[nodiscard]] bool finite(const double value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return finite_movement_vector(value);
}

[[nodiscard]] bool valid_collision_profile(
    const LocalMovementCollisionProfile profile) noexcept
{
    switch (profile) {
    case LocalMovementCollisionProfile::world_only_v1:
    case LocalMovementCollisionProfile::explicit_synthetic_static_brush_v1:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_collision_contents(
    const LocalMovementCollisionContents& contents) noexcept
{
    using Contents = PlayerMovementContents;
    switch (contents.category) {
    case Contents::empty:
        return contents.source_goldsrc_code == -1;
    case Contents::solid:
        return contents.source_goldsrc_code == -2;
    case Contents::water:
        return contents.source_goldsrc_code == -3;
    case Contents::slime:
        return contents.source_goldsrc_code == -4;
    case Contents::lava:
        return contents.source_goldsrc_code == -5;
    case Contents::current:
        return contents.source_goldsrc_code >= -14 &&
            contents.source_goldsrc_code <= -9;
    case Contents::sky:
        return contents.source_goldsrc_code == -6;
    case Contents::special:
        return contents.source_goldsrc_code == -7 ||
            contents.source_goldsrc_code == -8 ||
            contents.source_goldsrc_code == -15;
    }
    return false;
}

[[nodiscard]] bool blocking_collision_contents(
    const LocalMovementCollisionContents& contents) noexcept
{
    return contents.category == PlayerMovementContents::solid;
}

[[nodiscard]] bool valid_collision_hit(
    const hlclient::movement::PlayerMovementHitIdentity& hit,
    const LocalMovementCollisionProfile profile) noexcept
{
    switch (hit.kind) {
    case hlclient::movement::PlayerMovementHitKind::world:
        return hit.source_model_index == 0U &&
            !hit.stable_instance_ordinal && !hit.source_entity_index;
    case hlclient::movement::PlayerMovementHitKind::
            explicit_synthetic_brush:
        return profile == LocalMovementCollisionProfile::
                explicit_synthetic_static_brush_v1 &&
            hit.source_model_index != 0U &&
            hit.stable_instance_ordinal.has_value();
    }
    return false;
}

[[nodiscard]] bool valid_collision_plane(
    const hlclient::movement::PlayerMovementPlane& plane) noexcept
{
    if (!finite(plane.normal) || !finite(plane.distance)) {
        return false;
    }
    const auto length_squared =
        static_cast<double>(plane.normal.x) * plane.normal.x +
        static_cast<double>(plane.normal.y) * plane.normal.y +
        static_cast<double>(plane.normal.z) * plane.normal.z;
    return finite(length_squared) &&
        std::abs(length_squared - 1.0) <= 1.0e-4;
}

[[nodiscard]] bool consistent_trace_endpoint(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const double fraction,
    const assets::AssetVector3& actual) noexcept
{
    const std::array expected{
        static_cast<double>(start.x) +
            (static_cast<double>(end.x) - start.x) * fraction,
        static_cast<double>(start.y) +
            (static_cast<double>(end.y) - start.y) * fraction,
        static_cast<double>(start.z) +
            (static_cast<double>(end.z) - start.z) * fraction,
    };
    const std::array observed{
        static_cast<double>(actual.x),
        static_cast<double>(actual.y),
        static_cast<double>(actual.z),
    };
    for (std::size_t axis = 0U; axis < expected.size(); ++axis) {
        const auto tolerance = std::max(
            1.0e-4, std::abs(expected[axis]) * 1.0e-6);
        if (!finite(expected[axis]) || !finite(observed[axis]) ||
            std::abs(expected[axis] - observed[axis]) > tolerance) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] assets::AssetVector3 add(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] assets::AssetVector3 subtract(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] assets::AssetVector3 scale(
    const assets::AssetVector3& value,
    const float factor) noexcept
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] double horizontal_progress_squared(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end) noexcept
{
    const auto x = static_cast<double>(end.x) - start.x;
    const auto y = static_cast<double>(end.y) - start.y;
    return x * x + y * y;
}

[[nodiscard]] double horizontal_distance(
    const assets::AssetVector3& start,
    const assets::AssetVector3& end) noexcept
{
    return std::sqrt(horizontal_progress_squared(start, end));
}

[[nodiscard]] double vector_length_squared(
    const assets::AssetVector3& value) noexcept
{
    return static_cast<double>(value.x) * value.x +
        static_cast<double>(value.y) * value.y +
        static_cast<double>(value.z) * value.z;
}

[[nodiscard]] std::optional<assets::AssetVector3> normalized(
    const assets::AssetVector3& value) noexcept
{
    const auto length_squared = vector_length_squared(value);
    if (!finite(length_squared) || length_squared <= 0.0) {
        return std::nullopt;
    }
    const auto inverse = 1.0 / std::sqrt(length_squared);
    const assets::AssetVector3 output{
        static_cast<float>(value.x * inverse),
        static_cast<float>(value.y * inverse),
        static_cast<float>(value.z * inverse),
    };
    return finite(output) ? std::optional{output} : std::nullopt;
}

[[nodiscard]] assets::AssetVector3 cross(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] PlayerGroundStateCreateInfo airborne_ground() noexcept
{
    return {};
}

[[nodiscard]] WorkingState working_state(
    const LocalPlayerMovementState& state) noexcept
{
    WorkingState output;
    output.origin = state.origin();
    output.velocity = state.velocity();
    output.view_angles = state.view_angles();
    output.hull = state.hull();
    output.mode = state.mode();
    output.ground.grounded = state.ground_state().grounded();
    output.ground.walkable = state.ground_state().walkable();
    output.ground.hit = state.ground_state().hit();
    output.ground.plane = state.ground_state().plane();
    output.ground.contact_position = state.ground_state().contact_position();
    output.ground.probe_fraction = state.ground_state().probe_fraction();
    output.ground.evidence_profile = state.ground_state().evidence_profile();
    output.view_offset = state.view_offset();
    output.last_contents = state.last_valid_contents();
    output.friction_multiplier = state.friction_multiplier();
    return output;
}

[[nodiscard]] LocalMovementSimulationResult failure(
    const LocalMovementSimulationErrorCode code,
    const std::uint32_t command_sequence,
    const PlayerMovementStatistics& statistics,
    const std::string_view context,
    std::optional<LocalMovementCollisionError> collision_error = std::nullopt)
{
    LocalMovementSimulationResult output;
    output.statistics = statistics;
    output.command_sequence = command_sequence;
    output.error = LocalMovementSimulationError{
        code, std::move(collision_error), context};
    return output;
}

[[nodiscard]] OperationError collision_failure(
    const LocalMovementCollisionError& error,
    const std::string_view context) noexcept
{
    return {
        LocalMovementSimulationErrorCode::collision_query_failed,
        error,
        context,
    };
}

[[nodiscard]] OperationError collision_failure(
    const std::optional<LocalMovementCollisionError>& error,
    const std::string_view context) noexcept
{
    if (error) {
        return collision_failure(*error, context);
    }
    LocalMovementCollisionError missing_error;
    missing_error.code =
        LocalMovementCollisionErrorCode::invalid_collision_source;
    return collision_failure(missing_error, context);
}

[[nodiscard]] OperationError invalid_collision_result(
    const std::string_view context,
    const LocalMovementCollisionErrorCode code =
        LocalMovementCollisionErrorCode::non_finite_result) noexcept
{
    LocalMovementCollisionError error;
    error.code = code;
    return collision_failure(error, context);
}

[[nodiscard]] std::optional<OperationError> validate_point_contents_result(
    const LocalMovementPointContents& result,
    const GoldSrcLocalMovementConfig& config) noexcept
{
    if (!valid_collision_contents(result.contents) ||
        result.traversal_depth >
            config.collision_query.query_limits.maximum_traversal_steps) {
        return invalid_collision_result(
            "point-contents provider returned malformed data");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<OperationError> validate_position_result(
    const LocalMovementPositionTest& result,
    const LocalMovementCollisionProfile profile,
    const GoldSrcLocalMovementConfig& config) noexcept
{
    switch (result.status) {
    case LocalMovementPositionStatus::free:
    case LocalMovementPositionStatus::blocking:
        break;
    default:
        return invalid_collision_result(
            "position provider returned an invalid status");
    }
    if (!valid_collision_contents(result.contents) ||
        (result.hit && !valid_collision_hit(*result.hit, profile)) ||
        (result.status == LocalMovementPositionStatus::free && result.hit) ||
        result.traversal_depth >
            config.collision_query.query_limits.maximum_traversal_steps) {
        return invalid_collision_result(
            "position provider returned malformed data");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<OperationError> validate_trace_result(
    const LocalMovementTrace& trace,
    const assets::AssetVector3& start,
    const assets::AssetVector3& end,
    const LocalMovementCollisionProfile profile,
    const GoldSrcLocalMovementConfig& config) noexcept
{
    const bool contact = trace_has_contact(trace);
    const bool partial_contact_metadata =
        trace.collision_plane.has_value() || trace.hit.has_value() ||
        trace.blocking_contents.has_value();
    if (!trace.start_solid && trace.fraction == 1.0 && !contact &&
        blocking_collision_contents(trace.end_contents)) {
        return invalid_collision_result(
            "no-contact trace reports blocking endpoint contents",
            LocalMovementCollisionErrorCode::invalid_collision_source);
    }
    if (trace.blocking_contents &&
        valid_collision_contents(*trace.blocking_contents) &&
        !blocking_collision_contents(*trace.blocking_contents)) {
        return invalid_collision_result(
            "trace contact reports nonblocking blocking contents",
            LocalMovementCollisionErrorCode::invalid_collision_source);
    }
    if (!finite(trace.fraction) || trace.fraction < 0.0 ||
        trace.fraction > 1.0 || !finite(trace.end_position) ||
        trace.collision_profile != profile ||
        !valid_collision_profile(trace.collision_profile) ||
        !valid_collision_contents(trace.start_contents) ||
        !valid_collision_contents(trace.end_contents) ||
        (trace.blocking_contents &&
            !valid_collision_contents(*trace.blocking_contents)) ||
        (trace.collision_plane &&
            !valid_collision_plane(*trace.collision_plane)) ||
        (trace.hit && !valid_collision_hit(*trace.hit, profile)) ||
        !consistent_trace_endpoint(
            start, end, trace.fraction, trace.end_position) ||
        (trace.all_solid && !trace.start_solid) ||
        (trace.all_solid && trace.fraction != 0.0) ||
        (!trace.start_solid && partial_contact_metadata && !contact) ||
        (trace.fraction < 1.0 && !trace.start_solid && !contact) ||
        trace.traversal_statistics.maximum_stack_entries >
            config.collision_query.query_limits.maximum_stack_entries) {
        return invalid_collision_result(
            "trace provider returned malformed data");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<OperationError> validate_start_position(
    const WorkingState& state,
    const ILocalMovementCollision& collision,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config,
    PlayerMovementStatistics& statistics)
{
    const auto tested = collision.test_position(
        state.origin, state.hull, scratch.collision, config.collision_query);
    if (!tested || !tested.result) {
        return collision_failure(
            tested.error, "initial player position query failed");
    }
    if (auto error = validate_position_result(
            *tested.result, collision.profile(), config)) {
        return error;
    }
    if (tested.result->status == LocalMovementPositionStatus::free) {
        return std::nullopt;
    }
    const auto traced = collision.trace_hull(
        state.origin, state.origin, state.hull, scratch.collision,
        config.collision_query);
    ++statistics.trace_count;
    if (!traced || !traced.result) {
        return collision_failure(
            traced.error, "initial zero-length player trace failed");
    }
    if (auto error = validate_trace_result(
            *traced.result, state.origin, state.origin,
            collision.profile(), config)) {
        return error;
    }
    if (traced.result->all_solid) {
        ++statistics.all_solid_count;
        return OperationError{
            LocalMovementSimulationErrorCode::player_allsolid,
            std::nullopt,
            "player hull begins allsolid"};
    }
    ++statistics.start_solid_count;
    return OperationError{
        LocalMovementSimulationErrorCode::player_startsolid,
        std::nullopt,
        "player hull begins startsolid"};
}

[[nodiscard]] bool liquid(const PlayerMovementContents contents) noexcept
{
    return contents == PlayerMovementContents::water ||
        contents == PlayerMovementContents::slime ||
        contents == PlayerMovementContents::lava ||
        contents == PlayerMovementContents::current;
}

[[nodiscard]] std::optional<OperationError> update_contents(
    WorkingState& state,
    const ILocalMovementCollision& collision,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config)
{
    const auto queried = collision.point_contents(
        state.origin, scratch.collision, config.collision_query);
    if (!queried || !queried.result) {
        return collision_failure(
            queried.error, "player contents query failed");
    }
    if (auto error = validate_point_contents_result(*queried.result, config)) {
        return error;
    }
    state.last_contents = queried.result->contents.category;
    if (liquid(state.last_contents)) {
        state.mode = PlayerMovementMode::unsupported_liquid;
        return OperationError{
            LocalMovementSimulationErrorCode::liquid_movement_unsupported,
            std::nullopt,
            "dry-walk movement cannot continue through liquid contents"};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<OperationError> categorize_ground(
    WorkingState& state,
    const ILocalMovementCollision& collision,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config,
    PlayerMovementStatistics& statistics)
{
    ++statistics.ground_probe_count;
    if (state.velocity.z >
        static_cast<float>(config.maximum_ground_snap_upward_velocity)) {
        state.ground = airborne_ground();
        state.mode = PlayerMovementMode::airborne;
        return std::nullopt;
    }
    const assets::AssetVector3 end{
        state.origin.x,
        state.origin.y,
        static_cast<float>(
            static_cast<double>(state.origin.z) - config.ground_probe_distance),
    };
    if (!finite(end)) {
        return OperationError{
            LocalMovementSimulationErrorCode::non_finite_result,
            std::nullopt,
            "ground probe endpoint is non-finite"};
    }
    const auto traced = collision.trace_hull(
        state.origin, end, state.hull, scratch.collision,
        config.collision_query);
    ++statistics.trace_count;
    if (!traced || !traced.result) {
        return collision_failure(traced.error, "ground probe failed");
    }
    if (auto error = validate_trace_result(
            *traced.result, state.origin, end, collision.profile(), config)) {
        return error;
    }
    const auto& trace = *traced.result;
    if (trace.all_solid) {
        ++statistics.all_solid_count;
        return OperationError{
            LocalMovementSimulationErrorCode::player_allsolid,
            std::nullopt,
            "ground probe returned allsolid"};
    }
    if (trace.start_solid) {
        ++statistics.start_solid_count;
        return OperationError{
            LocalMovementSimulationErrorCode::player_startsolid,
            std::nullopt,
            "ground probe returned startsolid"};
    }
    if (!trace_has_contact(trace) ||
        trace.collision_plane->normal.z <
            static_cast<float>(config.minimum_walkable_normal_z)) {
        state.ground = airborne_ground();
        state.mode = PlayerMovementMode::airborne;
        return std::nullopt;
    }
    auto contact_position = state.origin;
    if (trace.fraction > 0.0) {
        const auto clearance = collision.test_position(
            trace.end_position, state.hull, scratch.collision,
            config.collision_query);
        if (!clearance || !clearance.result) {
            return collision_failure(
                clearance.error, "ground contact clearance query failed");
        }
        if (auto error = validate_position_result(
                *clearance.result, collision.profile(), config)) {
            return error;
        }
        if (clearance.result->status == LocalMovementPositionStatus::free) {
            contact_position = trace.end_position;
            state.origin = trace.end_position;
        }
    }
    const auto into_ground = movement_dot(
        state.velocity, trace.collision_plane->normal);
    if (into_ground < 0.0F) {
        const auto clipped = clip_velocity(
            state.velocity, trace.collision_plane->normal, 1.0F,
            static_cast<float>(config.stop_epsilon));
        if (!clipped || !clipped.value) {
            return OperationError{
                LocalMovementSimulationErrorCode::non_finite_result,
                std::nullopt,
                "ground velocity clipping failed"};
        }
        state.velocity = *clipped.value;
    }
    state.ground.grounded = true;
    state.ground.walkable = true;
    state.ground.hit = trace.hit;
    state.ground.plane = *trace.collision_plane;
    state.ground.contact_position = contact_position;
    state.ground.probe_fraction = trace.fraction;
    state.mode = PlayerMovementMode::walking;
    return std::nullopt;
}

[[nodiscard]] std::optional<OperationError> apply_duck_transition(
    WorkingState& state,
    const bool duck_requested,
    const ILocalMovementCollision& collision,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config,
    PlayerMovementStatistics& statistics)
{
    const auto requested_hull = duck_requested
        ? PlayerMovementHull::ducked
        : PlayerMovementHull::standing;
    if (requested_hull == state.hull) {
        return std::nullopt;
    }
    auto candidate = state.origin;
    if (state.ground.grounded) {
        const auto current_minimum = state.hull == PlayerMovementHull::standing
            ? kValveStandingHullMinimumZ
            : kValveDuckHullMinimumZ;
        const auto requested_minimum =
            requested_hull == PlayerMovementHull::standing
            ? kValveStandingHullMinimumZ
            : kValveDuckHullMinimumZ;
        candidate.z = state.origin.z + current_minimum - requested_minimum;
    }
    if (!finite(candidate)) {
        return OperationError{
            LocalMovementSimulationErrorCode::non_finite_result,
            std::nullopt,
            "duck transition origin is non-finite"};
    }
    const auto tested = collision.test_position(
        candidate, requested_hull, scratch.collision, config.collision_query);
    if (!tested || !tested.result) {
        return collision_failure(tested.error, "duck clearance query failed");
    }
    if (auto error = validate_position_result(
            *tested.result, collision.profile(), config)) {
        return error;
    }
    if (tested.result->status == LocalMovementPositionStatus::blocking) {
        if (requested_hull == PlayerMovementHull::standing) {
            ++statistics.stand_blocked_count;
            return std::nullopt;
        }
        return OperationError{
            LocalMovementSimulationErrorCode::duck_transition_failed,
            std::nullopt,
            "duck destination hull is blocked"};
    }
    state.origin = candidate;
    state.hull = requested_hull;
    if (requested_hull == PlayerMovementHull::ducked) {
        state.view_offset = config.duck_view_offset;
        ++statistics.duck_enter_count;
    } else {
        state.view_offset = config.standing_view_offset;
        ++statistics.duck_exit_count;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<assets::AssetVector3> solve_slide_velocity(
    const assets::AssetVector3& velocity,
    const std::array<hlclient::movement::PlayerMovementPlane,
        kValveMaximumClipPlanes>& planes,
    const std::size_t plane_count,
    const float stop_epsilon) noexcept
{
    for (std::size_t candidate_plane = 0U;
         candidate_plane < plane_count;
         ++candidate_plane) {
        const auto clipped = clip_velocity(
            velocity, planes[candidate_plane].normal, 1.0F, stop_epsilon);
        if (!clipped || !clipped.value) {
            return std::nullopt;
        }
        bool valid = true;
        for (std::size_t other = 0U; other < plane_count; ++other) {
            if (other != candidate_plane &&
                movement_dot(*clipped.value, planes[other].normal) <
                    -stop_epsilon) {
                valid = false;
                break;
            }
        }
        if (valid) {
            return clipped.value;
        }
    }
    if (plane_count == 2U) {
        const auto crease = normalized(
            cross(planes[0U].normal, planes[1U].normal));
        if (!crease) {
            return assets::AssetVector3{};
        }
        const auto speed = movement_dot(velocity, *crease);
        auto candidate = scale(*crease, speed);
        for (std::size_t index = 0U; index < plane_count; ++index) {
            if (movement_dot(candidate, planes[index].normal) <
                -stop_epsilon) {
                return assets::AssetVector3{};
            }
        }
        const auto stopped = apply_component_stop_epsilon(candidate, stop_epsilon);
        return stopped && stopped.value ? stopped.value
                                        : std::optional<assets::AssetVector3>{};
    }
    return assets::AssetVector3{};
}

[[nodiscard]] SlideResult slide_move(
    const assets::AssetVector3& start_origin,
    const assets::AssetVector3& start_velocity,
    const float duration,
    const PlayerMovementHull hull,
    const PlayerMovementPhase phase,
    const std::uint32_t command_sequence,
    const ILocalMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& query_scratch,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config,
    const std::size_t maximum_touches,
    PlayerMovementStatistics& statistics,
    std::vector<PlayerMovementTouch>& touches)
{
    SlideResult output;
    output.origin = start_origin;
    output.velocity = start_velocity;
    double remaining = duration;
    std::array<hlclient::movement::PlayerMovementPlane,
        kValveMaximumClipPlanes> planes{};
    std::size_t plane_count = 0U;
    for (std::size_t bump = 0U;
         bump < config.maximum_slide_bumps && remaining > 0.0F;
         ++bump) {
        ++statistics.slide_bump_count;
        const auto remaining_float = static_cast<float>(remaining);
        const auto end = add(
            output.origin, scale(output.velocity, remaining_float));
        if (!finite(end)) {
            output.error = OperationError{
                LocalMovementSimulationErrorCode::non_finite_result,
                std::nullopt,
                "slide endpoint is non-finite"};
            return output;
        }
        const auto bump_origin = output.origin;
        const auto bump_velocity = output.velocity;
        const auto old_remaining = remaining;
        const auto traced = collision.trace_hull(
            output.origin, end, hull, query_scratch,
            config.collision_query);
        ++statistics.trace_count;
        if (!traced || !traced.result) {
            output.error = collision_failure(traced.error, "slide trace failed");
            return output;
        }
        if (auto error = validate_trace_result(
                *traced.result, output.origin, end,
                collision.profile(), config)) {
            output.error = error;
            return output;
        }
        const auto& trace = *traced.result;
        PlayerWallContactDiagnosticFrame diagnostic;
        diagnostic.command_sequence = command_sequence;
        diagnostic.phase = phase;
        diagnostic.substep_ordinal = scratch.diagnostic_substep_ordinal;
        diagnostic.generated_command_count = 1U;
        diagnostic.trace_count = static_cast<std::uint32_t>(std::min(
            statistics.trace_count,
            static_cast<std::uint64_t>(UINT32_MAX)));
        diagnostic.slide_bump_ordinal = static_cast<std::uint16_t>(bump);
        diagnostic.start_solid = trace.start_solid;
        diagnostic.all_solid = trace.all_solid;
        diagnostic.hit_present = trace.hit.has_value();
        diagnostic.hit_kind = diagnostic_hit_kind(trace.hit);
        diagnostic.collision_result = trace.all_solid
            ? PlayerMovementCollisionResultClass::all_solid
            : trace.start_solid
                ? PlayerMovementCollisionResultClass::start_solid
                : trace_has_contact(trace)
                    ? PlayerMovementCollisionResultClass::contact
                    : PlayerMovementCollisionResultClass::no_hit;
        diagnostic.fraction_class = trace.fraction == 0.0
            ? PlayerMovementTraceFractionClass::zero
            : trace.fraction <=
                    config.collision_query.trace_tolerance.
                        minimum_progress_fraction
                ? PlayerMovementTraceFractionClass::near_zero
                : trace.fraction < 1.0
                    ? PlayerMovementTraceFractionClass::partial
                    : PlayerMovementTraceFractionClass::complete;
        if (trace.all_solid) {
            ++statistics.all_solid_count;
            output.error = OperationError{
                LocalMovementSimulationErrorCode::player_allsolid,
                std::nullopt,
                "slide trace returned allsolid"};
            diagnostic.result =
                PlayerMovementDiagnosticResult::movement_failure;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        if (trace.start_solid) {
            ++statistics.start_solid_count;
            output.error = OperationError{
                LocalMovementSimulationErrorCode::player_startsolid,
                std::nullopt,
                "slide trace returned startsolid"};
            diagnostic.result =
                PlayerMovementDiagnosticResult::movement_failure;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        if (trace.in_liquid) {
            output.error = OperationError{
                LocalMovementSimulationErrorCode::liquid_movement_unsupported,
                std::nullopt,
                "slide crossed unsupported liquid contents"};
            diagnostic.result =
                PlayerMovementDiagnosticResult::movement_failure;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        if (!trace_has_contact(trace)) {
            const auto clearance = collision.test_position(
                trace.end_position, hull, query_scratch,
                config.collision_query);
            if (!clearance || !clearance.result) {
                output.error = collision_failure(
                    clearance.error,
                    "no-contact slide endpoint clearance query failed");
                diagnostic.result =
                    PlayerMovementDiagnosticResult::collision_failure;
                record_diagnostic(scratch, diagnostic);
                return output;
            }
            if (auto error = validate_position_result(
                    *clearance.result, collision.profile(), config)) {
                output.error = error;
                diagnostic.result =
                    PlayerMovementDiagnosticResult::collision_failure;
                record_diagnostic(scratch, diagnostic);
                return output;
            }
            if (clearance.result->status !=
                LocalMovementPositionStatus::free) {
                output.error = invalid_collision_result(
                    "no-contact slide endpoint is blocking",
                    LocalMovementCollisionErrorCode::
                        invalid_collision_source);
                diagnostic.result =
                    PlayerMovementDiagnosticResult::collision_failure;
                record_diagnostic(scratch, diagnostic);
                return output;
            }
            output.origin = trace.end_position;
            diagnostic.result = PlayerMovementDiagnosticResult::success;
            diagnostic.remaining_time_present = false;
            diagnostic.remaining_time_class =
                PlayerMovementRemainingTimeClass::exhausted;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        ++statistics.collision_hit_count;
        double committed_fraction = 0.0;
        if (trace.fraction > 0.0) {
            const auto clearance = collision.test_position(
                trace.end_position, hull, query_scratch,
                config.collision_query);
            if (!clearance || !clearance.result) {
                output.error = collision_failure(
                    clearance.error, "slide contact clearance query failed");
                diagnostic.result =
                    PlayerMovementDiagnosticResult::collision_failure;
                record_diagnostic(scratch, diagnostic);
                return output;
            }
            if (auto error = validate_position_result(
                    *clearance.result, collision.profile(), config)) {
                output.error = error;
                diagnostic.result =
                    PlayerMovementDiagnosticResult::collision_failure;
                record_diagnostic(scratch, diagnostic);
                return output;
            }
            if (clearance.result->status ==
                LocalMovementPositionStatus::free) {
                output.origin = trace.end_position;
                committed_fraction = trace.fraction;
            }
        }
        if (auto error = append_touch(touches, PlayerMovementTouch{
            *trace.hit,
            *trace.collision_plane,
            trace.fraction,
            phase,
            command_sequence,
        }, maximum_touches)) {
            output.error = error;
            diagnostic.result =
                PlayerMovementDiagnosticResult::movement_failure;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        const auto new_remaining = remaining * (1.0 - committed_fraction);
        if (!std::isfinite(new_remaining) || new_remaining < 0.0 ||
            new_remaining > old_remaining) {
            output.error = OperationError{
                LocalMovementSimulationErrorCode::movement_stalled,
                std::nullopt,
                "slide remaining time did not decrease monotonically"};
            diagnostic.result =
                PlayerMovementDiagnosticResult::movement_failure;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        remaining = new_remaining;

        std::optional<std::size_t> duplicate_index;
        bool opposing = false;
        for (std::size_t index = 0U; index < plane_count; ++index) {
            if (!duplicate_index && equivalent_touch_plane(
                    planes[index], *trace.collision_plane)) {
                duplicate_index = index;
            }
            opposing = opposing || opposing_coplanar_planes(
                planes[index], *trace.collision_plane);
        }
        if (opposing) {
            output.velocity = {};
            diagnostic.clip_plane_count =
                static_cast<std::uint16_t>(plane_count);
            diagnostic.distinct_plane_count = diagnostic.clip_plane_count;
            diagnostic.result = PlayerMovementDiagnosticResult::stable_stop;
            diagnostic.remaining_time_present = remaining > 0.0F;
            diagnostic.remaining_time_class = remaining > 0.0F
                ? PlayerMovementRemainingTimeClass::retained
                : PlayerMovementRemainingTimeClass::exhausted;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        if (!duplicate_index) {
            if (plane_count >= config.maximum_clip_planes ||
                plane_count >= planes.size()) {
                output.error = OperationError{
                    LocalMovementSimulationErrorCode::
                        clip_plane_limit_exceeded,
                    std::nullopt,
                    "slide distinct clip-plane limit exceeded"};
                diagnostic.result =
                    PlayerMovementDiagnosticResult::movement_failure;
                record_diagnostic(scratch, diagnostic);
                return output;
            }
            planes[plane_count++] = *trace.collision_plane;
            ++statistics.clip_plane_count;
        } else {
            // A noisy repeat of the same surface does not consume bounded
            // plane capacity, but the current collision evidence is the one
            // that must constrain the velocity published by this bump.
            planes[*duplicate_index] = *trace.collision_plane;
        }
        diagnostic.clip_plane_count =
            static_cast<std::uint16_t>(plane_count);
        diagnostic.distinct_plane_count = diagnostic.clip_plane_count;
        diagnostic.plane_class = PlayerMovementPlaneClass::finite_unit;
        const auto classified = clip_velocity_against_plane(
            output.velocity, trace.collision_plane->normal, 1.0F,
            static_cast<float>(config.stop_epsilon));
        if (!classified || !classified.value) {
            output.error = OperationError{
                LocalMovementSimulationErrorCode::non_finite_result,
                std::nullopt,
                "slide blocked-axis classification failed"};
            return output;
        }
        output.blocked_axes = static_cast<GoldSrcClipBlockedAxis>(
            static_cast<std::uint8_t>(output.blocked_axes) |
            static_cast<std::uint8_t>(classified.value->blocked_axes));
        const auto solved = solve_slide_velocity(
            output.velocity, planes, plane_count,
            static_cast<float>(config.stop_epsilon));
        if (!solved || !finite(*solved)) {
            output.error = OperationError{
                LocalMovementSimulationErrorCode::non_finite_result,
                std::nullopt,
                "multi-plane slide velocity is invalid"};
            return output;
        }
        output.velocity = *solved;
        auto into_plane = movement_dot(
            output.velocity, trace.collision_plane->normal);
        if (into_plane < 0.0F) {
            const auto current_plane_clipped = clip_velocity_against_plane(
                output.velocity, trace.collision_plane->normal, 1.0F,
                static_cast<float>(config.stop_epsilon));
            if (!current_plane_clipped ||
                !current_plane_clipped.value ||
                !finite(current_plane_clipped.value->velocity)) {
                output.error = OperationError{
                    LocalMovementSimulationErrorCode::non_finite_result,
                    std::nullopt,
                    "current duplicate plane velocity clip is invalid"};
                return output;
            }
            output.velocity = current_plane_clipped.value->velocity;
            into_plane = movement_dot(
                output.velocity, trace.collision_plane->normal);
            if (into_plane < 0.0F) {
                // Binary32 projection may leave a negative residual. A
                // bounded stop is preferable to publishing velocity back
                // into collision on the next command.
                output.velocity = {};
                into_plane = 0.0F;
            }
        }
        diagnostic.velocity_class = vector_length_squared(output.velocity) == 0.0
            ? PlayerMovementVelocityClass::finite_zero
            : into_plane < -static_cast<float>(config.stop_epsilon)
                ? PlayerMovementVelocityClass::finite_into_plane
                : PlayerMovementVelocityClass::finite_not_into_plane;
        if (vector_length_squared(output.velocity) == 0.0) {
            diagnostic.result = PlayerMovementDiagnosticResult::stable_stop;
            diagnostic.remaining_time_present = remaining > 0.0F;
            diagnostic.remaining_time_class = remaining > 0.0F
                ? PlayerMovementRemainingTimeClass::retained
                : PlayerMovementRemainingTimeClass::exhausted;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        const bool origin_progress =
            horizontal_progress_squared(bump_origin, output.origin) > 0.0 ||
            output.origin.z != bump_origin.z;
        const bool velocity_progress =
            vector_length_squared(subtract(output.velocity, bump_velocity)) >
            0.0;
        if (!origin_progress && !velocity_progress) {
            diagnostic.result = PlayerMovementDiagnosticResult::stable_stop;
            diagnostic.remaining_time_present = remaining > 0.0F;
            diagnostic.remaining_time_class = remaining > 0.0F
                ? PlayerMovementRemainingTimeClass::retained
                : PlayerMovementRemainingTimeClass::exhausted;
            record_diagnostic(scratch, diagnostic);
            return output;
        }
        diagnostic.result = PlayerMovementDiagnosticResult::progressing;
        diagnostic.remaining_time_present = remaining > 0.0F;
        diagnostic.remaining_time_class = remaining > 0.0F
            ? PlayerMovementRemainingTimeClass::retained
            : PlayerMovementRemainingTimeClass::exhausted;
        record_diagnostic(scratch, diagnostic);
    }
    // Bump exhaustion is a successful bounded stop. Retain any already-safe
    // tangential velocity for the next command instead of erasing it.
    return output;
}

[[nodiscard]] StepResult step_candidate(
    const assets::AssetVector3& start_origin,
    const assets::AssetVector3& start_velocity,
    const float duration,
    const PlayerMovementHull hull,
    const std::uint32_t command_sequence,
    const float step_size,
    const ILocalMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& query_scratch,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config,
    PlayerMovementStatistics& statistics,
    std::vector<PlayerMovementTouch>& touches)
{
    StepResult output;
    const assets::AssetVector3 upward{
        start_origin.x, start_origin.y, start_origin.z + step_size};
    const auto up_trace = collision.trace_hull(
        start_origin, upward, hull, query_scratch, config.collision_query);
    ++statistics.trace_count;
    if (!up_trace || !up_trace.result) {
        output.error = collision_failure(up_trace.error, "step-up trace failed");
        return output;
    }
    if (auto error = validate_trace_result(
            *up_trace.result, start_origin, upward,
            collision.profile(), config)) {
        output.error = error;
        return output;
    }
    if (up_trace.result->in_liquid) {
        output.error = OperationError{
            LocalMovementSimulationErrorCode::liquid_movement_unsupported,
            std::nullopt,
            "step-up trace crossed unsupported liquid contents"};
        return output;
    }
    if (up_trace.result->start_solid || up_trace.result->all_solid ||
        trace_has_contact(*up_trace.result)) {
        return output;
    }
    auto horizontal = slide_move(
        up_trace.result->end_position, start_velocity, duration, hull,
        PlayerMovementPhase::step_horizontal, command_sequence, collision,
        query_scratch, scratch, config, config.maximum_slide_bumps + 1U,
        statistics, touches);
    if (horizontal.error) {
        output.error = horizontal.error;
        return output;
    }
    const assets::AssetVector3 downward{
        horizontal.origin.x,
        horizontal.origin.y,
        static_cast<float>(
            horizontal.origin.z - step_size -
            static_cast<float>(config.ground_probe_distance)),
    };
    const auto down_trace = collision.trace_hull(
        horizontal.origin, downward, hull, query_scratch,
        config.collision_query);
    ++statistics.trace_count;
    if (!down_trace || !down_trace.result) {
        output.error = collision_failure(
            down_trace.error, "step-down trace failed");
        return output;
    }
    if (auto error = validate_trace_result(
            *down_trace.result, horizontal.origin, downward,
            collision.profile(), config)) {
        output.error = error;
        return output;
    }
    if (down_trace.result->in_liquid) {
        output.error = OperationError{
            LocalMovementSimulationErrorCode::liquid_movement_unsupported,
            std::nullopt,
            "step-down trace crossed unsupported liquid contents"};
        return output;
    }
    const auto& landing = *down_trace.result;
    if (landing.start_solid || landing.all_solid ||
        !trace_has_contact(landing) ||
        landing.collision_plane->normal.z <
            static_cast<float>(config.minimum_walkable_normal_z)) {
        return output;
    }
    const auto clearance = collision.test_position(
        landing.end_position, hull, query_scratch, config.collision_query);
    if (!clearance || !clearance.result) {
        output.error = collision_failure(
            clearance.error, "step landing clearance query failed");
        return output;
    }
    if (auto error = validate_position_result(
            *clearance.result, collision.profile(), config)) {
        output.error = error;
        return output;
    }
    if (clearance.result->status != LocalMovementPositionStatus::free) {
        return output;
    }
    if (auto error = append_touch(touches, PlayerMovementTouch{
        *landing.hit,
        *landing.collision_plane,
        landing.fraction,
        PlayerMovementPhase::step_down,
        command_sequence,
    }, config.maximum_slide_bumps + 1U)) {
        output.error = error;
        return output;
    }
    output.available = true;
    output.origin = landing.end_position;
    output.velocity = horizontal.velocity;
    return output;
}

[[nodiscard]] std::optional<OperationError> clamp_velocity(
    WorkingState& state,
    const GoldSrcMovementEnvironment& environment);

[[nodiscard]] std::optional<OperationError> apply_walk_move(
    WorkingState& state,
    const GoldSrcUserCmdState& command,
    const float duration,
    const GoldSrcMovementEnvironment& environment,
    const ILocalMovementCollision& collision,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config,
    PlayerMovementStatistics& statistics,
    std::vector<PlayerMovementTouch>& touches)
{
    auto friction = apply_horizontal_ground_friction(
        state.velocity, environment.stop_speed(), environment.friction(),
        state.friction_multiplier, duration);
    if (!friction || !friction.value) {
        return OperationError{
            LocalMovementSimulationErrorCode::non_finite_result,
            std::nullopt,
            "ground friction failed"};
    }
    state.velocity = *friction.value;
    const auto wish = yaw_only_wish_direction(
        command.view_angles()[1U], command.forward_move(), command.side_move(),
        environment.maximum_speed());
    if (!wish || !wish.wish) {
        return OperationError{
            LocalMovementSimulationErrorCode::non_finite_result,
            std::nullopt,
            "ground wish direction failed"};
    }
    auto wish_direction = wish.wish->direction;
    if (state.ground.grounded) {
        const auto projection = movement_dot(
            wish_direction, state.ground.plane.normal);
        const auto projected = subtract(
            wish_direction, scale(state.ground.plane.normal, projection));
        if (wish.wish->speed > 0.0F) {
            const auto normalized_projection = normalized(projected);
            if (!normalized_projection) {
                return OperationError{
                    LocalMovementSimulationErrorCode::non_finite_result,
                    std::nullopt,
                    "ground-plane wish projection failed"};
            }
            wish_direction = *normalized_projection;
        }
    }
    const auto accelerated = accelerate_ground(
        state.velocity, wish_direction, wish.wish->speed,
        environment.acceleration(), state.friction_multiplier, duration);
    if (!accelerated || !accelerated.value) {
        return OperationError{
            LocalMovementSimulationErrorCode::non_finite_result,
            std::nullopt,
            "ground acceleration failed"};
    }
    auto ground_velocity = accelerated.value;
    if (state.ground.grounded) {
        const auto projected_velocity = clip_velocity(
            *accelerated.value, state.ground.plane.normal, 1.0F,
            static_cast<float>(config.stop_epsilon));
        if (!projected_velocity || !projected_velocity.value) {
            return OperationError{
                LocalMovementSimulationErrorCode::non_finite_result,
                std::nullopt,
                "ground-plane velocity projection failed"};
        }
        ground_velocity = projected_velocity.value;
    }
    const auto start = state.origin;
    state.velocity = *ground_velocity;
    if (auto error = clamp_velocity(state, environment)) {
        return error;
    }
    const auto velocity = state.velocity;
    std::vector<PlayerMovementTouch> direct_touches;
    std::vector<PlayerMovementTouch> step_touches;
    try {
        // Candidate-local storage is bounded by what each route can produce,
        // not by the selected command-output limit. Applying the command limit
        // before selection would let a discarded step landing invalidate a
        // valid direct route.
        direct_touches.reserve(config.maximum_slide_bumps);
        step_touches.reserve(config.maximum_slide_bumps + 1U);
    } catch (const std::bad_alloc&) {
        return OperationError{
            LocalMovementSimulationErrorCode::allocation_failed,
            std::nullopt,
            "movement candidate touch allocation failed"};
    } catch (const std::length_error&) {
        return OperationError{
            LocalMovementSimulationErrorCode::allocation_failed,
            std::nullopt,
            "movement candidate touch storage length failed"};
    }
    PlayerMovementStatistics direct_statistics;
    PlayerMovementStatistics step_statistics;
    auto direct = slide_move(
        start, velocity, duration, state.hull,
        PlayerMovementPhase::direct_slide, command.command_sequence().value(),
        collision, scratch.direct_candidate_collision, scratch, config,
        config.maximum_slide_bumps, direct_statistics, direct_touches);
    if (direct.error) {
        return direct.error;
    }
    const auto direct_diagnostic = scratch.last_diagnostic;
    ++statistics.step_attempt_count;
    auto step = step_candidate(
        start, velocity, duration, state.hull,
        command.command_sequence().value(), environment.step_size(), collision,
        scratch.step_candidate_collision, scratch, config, step_statistics,
        step_touches);
    if (step.error) {
        return step.error;
    }
    const auto direct_progress = horizontal_progress_squared(start, direct.origin);
    const auto step_progress = step.available
        ? horizontal_progress_squared(start, step.origin)
        : -1.0;
    const bool select_step =
        step.available && step_progress > direct_progress;
    const auto selected_diagnostic =
        select_step ? scratch.last_diagnostic : direct_diagnostic;
    const auto& selected_touches = select_step ? step_touches : direct_touches;
    for (const auto& touch : selected_touches) {
        if (auto error = append_touch(
                touches, touch, config.maximum_touches_per_command)) {
            return error;
        }
    }
    auto selected_statistics =
        select_step ? step_statistics : direct_statistics;
    if (select_step) {
        ++selected_statistics.step_success_count;
    }
    if (!merge_statistics(statistics, selected_statistics)) {
        return OperationError{
            LocalMovementSimulationErrorCode::statistics_overflow,
            std::nullopt,
            "movement candidate statistics overflowed"};
    }
    state.origin = select_step ? step.origin : direct.origin;
    state.velocity = select_step ? step.velocity : direct.velocity;
    // Step probing is speculative. Recommit the chosen route's terminal
    // diagnostic so a rejected step cannot label a successful direct result.
    if (selected_diagnostic) {
        record_diagnostic(scratch, *selected_diagnostic);
    }
    return categorize_ground(
        state, collision, scratch, config, statistics);
}

[[nodiscard]] std::optional<OperationError> clamp_velocity(
    WorkingState& state,
    const GoldSrcMovementEnvironment& environment)
{
    const auto clamped = clamp_velocity_per_axis(
        state.velocity, environment.maximum_velocity());
    if (!clamped || !clamped.value) {
        return OperationError{
            LocalMovementSimulationErrorCode::velocity_limit_exceeded,
            std::nullopt,
            "velocity validation or component clamp failed"};
    }
    state.velocity = *clamped.value;
    return std::nullopt;
}

[[nodiscard]] std::optional<OperationError> apply_air_move(
    WorkingState& state,
    const GoldSrcUserCmdState& command,
    const float duration,
    const float effective_gravity,
    const GoldSrcMovementEnvironment& environment,
    const ILocalMovementCollision& collision,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config,
    PlayerMovementStatistics& statistics,
    std::vector<PlayerMovementTouch>& touches)
{
    state.velocity.z -= effective_gravity * duration * 0.5F;
    if (auto error = clamp_velocity(state, environment)) {
        return error;
    }
    const auto wish = yaw_only_wish_direction(
        command.view_angles()[1U], command.forward_move(), command.side_move(),
        environment.maximum_speed());
    if (!wish || !wish.wish) {
        return OperationError{
            LocalMovementSimulationErrorCode::non_finite_result,
            std::nullopt,
            "air wish direction failed"};
    }
    const auto accelerated = accelerate_air(
        state.velocity, wish.wish->direction, wish.wish->speed,
        environment.air_acceleration(), state.friction_multiplier, duration);
    if (!accelerated || !accelerated.value) {
        return OperationError{
            LocalMovementSimulationErrorCode::non_finite_result,
            std::nullopt,
            "air acceleration failed"};
    }
    auto moved = slide_move(
        state.origin, *accelerated.value, duration, state.hull,
        PlayerMovementPhase::airborne_slide,
        command.command_sequence().value(), collision, scratch.collision,
        scratch, config, config.maximum_touches_per_command, statistics,
        touches);
    if (moved.error) {
        return moved.error;
    }
    state.origin = moved.origin;
    state.velocity = moved.velocity;
    state.velocity.z -= effective_gravity * duration * 0.5F;
    if (auto error = clamp_velocity(state, environment)) {
        return error;
    }
    return categorize_ground(
        state, collision, scratch, config, statistics);
}

[[nodiscard]] LocalPlayerMovementStateCreateInfo final_state_info(
    const LocalPlayerMovementState& previous,
    const WorkingState& state,
    const GoldSrcUserCmdState& command,
    const std::uint64_t simulation_time,
    const std::uint64_t revision) noexcept
{
    LocalPlayerMovementStateCreateInfo info;
    info.origin = state.origin;
    info.velocity = state.velocity;
    info.view_angles = state.view_angles;
    info.hull = state.hull;
    info.mode = state.mode;
    info.ground = state.ground;
    info.view_offset = state.view_offset;
    info.old_buttons = command.buttons();
    info.source_command_sequence = command.command_sequence().value();
    info.simulation_time_nanoseconds = simulation_time;
    info.last_valid_contents = state.last_contents;
    info.gravity_multiplier = previous.gravity_multiplier();
    info.friction_multiplier = previous.friction_multiplier();
    info.state_revision = revision;
    info.compatibility_profile = previous.compatibility_profile();
    info.evidence_profile = previous.evidence_profile();
    info.command_profile = previous.command_profile();
    return info;
}

} // namespace

bool valid_goldsrc_local_movement_config(
    const GoldSrcLocalMovementConfig& config) noexcept
{
    return finite(config.maximum_command_duration_seconds) &&
        config.maximum_command_duration_seconds > 0.0 &&
        config.maximum_command_duration_seconds <= 1.0 &&
        finite(config.maximum_substep_duration_seconds) &&
        config.maximum_substep_duration_seconds > 0.0 &&
        config.maximum_substep_duration_seconds <=
            config.maximum_command_duration_seconds &&
        config.maximum_substeps_per_command > 0U &&
        config.maximum_substeps_per_command <= 256U &&
        finite(config.ground_probe_distance) &&
        config.ground_probe_distance > 0.0 &&
        config.ground_probe_distance <= 64.0 &&
        finite(config.minimum_walkable_normal_z) &&
        config.minimum_walkable_normal_z ==
            kGoldSrcMovementMinimumWalkableNormalZ &&
        finite(config.maximum_ground_snap_upward_velocity) &&
        config.maximum_ground_snap_upward_velocity >= 0.0 &&
        config.maximum_slide_bumps > 0U &&
        config.maximum_slide_bumps <= kValveMaximumSlideBumps &&
        config.maximum_clip_planes > 0U &&
        config.maximum_clip_planes <= kValveMaximumClipPlanes &&
        config.maximum_touches_per_command > 0U &&
        config.maximum_touches_per_command <=
            kGoldSrcMovementHardMaximumTouchesPerCommand &&
        finite(config.stop_epsilon) && config.stop_epsilon > 0.0 &&
        config.stop_epsilon <= 1.0 &&
        finite(config.air_wish_speed_cap) &&
        config.air_wish_speed_cap == kValveAirWishSpeedCap &&
        finite(config.jump_impulse) &&
        std::abs(config.jump_impulse - kValveJumpImpulse) <= 1.0e-5 &&
        finite(config.standing_view_offset) &&
        finite(config.duck_view_offset) &&
        valid_local_movement_collision_query_config(config.collision_query) &&
        hlclient::movement::valid_local_player_movement_state_limits(
            config.state_limits) &&
        config.gravity_profile ==
            GoldSrcMovementGravityProfile::public_valve_split_half_step_v1 &&
        config.jump_profile ==
            GoldSrcMovementJumpProfile::public_valve_reference_800x45_v1 &&
        config.duck_profile ==
            GoldSrcMovementDuckProfile::project_immediate_bounded_v1 &&
        config.air_profile == GoldSrcMovementAirProfile::
            public_valve_wish_cap_30_uncapped_accel_v1 &&
        config.airborne_duck_policy ==
            GoldSrcMovementAirborneDuckPolicy::preserve_hull_center_v1;
}

std::string_view to_string(const LocalMovementSimulationErrorCode code) noexcept
{
    switch (code) {
    case LocalMovementSimulationErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalMovementSimulationErrorCode::invalid_state: return "invalid_state";
    case LocalMovementSimulationErrorCode::invalid_environment:
        return "invalid_environment";
    case LocalMovementSimulationErrorCode::unsupported_command_profile:
        return "unsupported_command_profile";
    case LocalMovementSimulationErrorCode::stock_semantics_pending:
        return "stock_semantics_pending";
    case LocalMovementSimulationErrorCode::invalid_command_sequence:
        return "invalid_command_sequence";
    case LocalMovementSimulationErrorCode::invalid_command_duration:
        return "invalid_command_duration";
    case LocalMovementSimulationErrorCode::collision_query_failed:
        return "collision_query_failed";
    case LocalMovementSimulationErrorCode::player_startsolid:
        return "player_startsolid";
    case LocalMovementSimulationErrorCode::player_allsolid:
        return "player_allsolid";
    case LocalMovementSimulationErrorCode::movement_trace_failed:
        return "movement_trace_failed";
    case LocalMovementSimulationErrorCode::velocity_limit_exceeded:
        return "velocity_limit_exceeded";
    case LocalMovementSimulationErrorCode::substep_limit_exceeded:
        return "substep_limit_exceeded";
    case LocalMovementSimulationErrorCode::clip_plane_limit_exceeded:
        return "clip_plane_limit_exceeded";
    case LocalMovementSimulationErrorCode::touch_limit_exceeded:
        return "touch_limit_exceeded";
    case LocalMovementSimulationErrorCode::allocation_failed:
        return "allocation_failed";
    case LocalMovementSimulationErrorCode::statistics_overflow:
        return "statistics_overflow";
    case LocalMovementSimulationErrorCode::movement_stalled:
        return "movement_stalled";
    case LocalMovementSimulationErrorCode::liquid_movement_unsupported:
        return "liquid_movement_unsupported";
    case LocalMovementSimulationErrorCode::ladder_movement_unsupported:
        return "ladder_movement_unsupported";
    case LocalMovementSimulationErrorCode::duck_transition_failed:
        return "duck_transition_failed";
    case LocalMovementSimulationErrorCode::stand_clearance_blocked:
        return "stand_clearance_blocked";
    case LocalMovementSimulationErrorCode::state_revision_exhausted:
        return "state_revision_exhausted";
    case LocalMovementSimulationErrorCode::simulation_time_overflow:
        return "simulation_time_overflow";
    case LocalMovementSimulationErrorCode::non_finite_result:
        return "non_finite_result";
    }
    return "unknown";
}

LocalMovementSimulationResult GoldSrcLocalMovementKernel::simulate(
    const LocalPlayerMovementState& previous_state,
    const GoldSrcUserCmdState& command,
    const GoldSrcMovementEnvironment& environment,
    const ILocalMovementCollision& collision,
    GoldSrcLocalMovementScratch& scratch,
    const GoldSrcLocalMovementConfig& config)
{
    PlayerMovementStatistics statistics;
    const auto sequence = command.command_sequence().value();
    const auto before_signature =
        hlclient::movement::local_player_movement_state_signature(
            previous_state);
    scratch.last_diagnostic.reset();
    scratch.diagnostic_substep_ordinal = 0U;
    const auto fail = [&scratch, sequence, before_signature](
                          const LocalMovementSimulationErrorCode code,
                          const PlayerMovementStatistics& failure_statistics,
                          const std::string_view context,
                          std::optional<LocalMovementCollisionError>
                              collision_error = std::nullopt) {
        auto diagnostic = scratch.last_diagnostic.value_or(
            PlayerWallContactDiagnosticFrame{});
        diagnostic.command_sequence = sequence;
        diagnostic.state_signature_before = before_signature;
        // Simulation failures are transactional: no successor state is
        // published, so the retained state remains the caller's input state.
        diagnostic.state_signature_after = before_signature;
        diagnostic.substep_ordinal = scratch.diagnostic_substep_ordinal;
        diagnostic.generated_command_count = 1U;
        diagnostic.trace_count = static_cast<std::uint32_t>(std::min(
            failure_statistics.trace_count,
            static_cast<std::uint64_t>(UINT32_MAX)));
        diagnostic.result = collision_error
            ? PlayerMovementDiagnosticResult::collision_failure
            : PlayerMovementDiagnosticResult::movement_failure;
        diagnostic.collision_result = collision_error
            ? PlayerMovementCollisionResultClass::typed_failure
            : PlayerMovementCollisionResultClass::none;
        record_diagnostic(scratch, diagnostic);
        return failure(code, sequence, failure_statistics, context,
            std::move(collision_error));
    };
    if (!valid_goldsrc_local_movement_config(config) ||
        !valid_collision_profile(collision.profile()) ||
        !collision.valid()) {
        return fail(
            LocalMovementSimulationErrorCode::invalid_configuration,
            statistics, "movement configuration is invalid");
    }
    if (previous_state.compatibility_profile() != hlclient::movement::
            GoldSrcMovementCompatibilityProfile::
                public_valve_pm_shared_dry_walk_subset_v1 ||
        previous_state.command_profile() != hlclient::movement::
            GoldSrcMovementCommandProfile::synthetic_usercmd_semantics_v1 ||
        previous_state.mode() == PlayerMovementMode::invalid_or_stuck) {
        return fail(LocalMovementSimulationErrorCode::invalid_state,
            statistics, "previous movement state is not executable");
    }
    if (previous_state.mode() == PlayerMovementMode::unsupported_liquid) {
        return fail(
            LocalMovementSimulationErrorCode::liquid_movement_unsupported,
            statistics, "previous movement state is in liquid mode");
    }
    if (previous_state.mode() == PlayerMovementMode::unsupported_ladder) {
        return fail(
            LocalMovementSimulationErrorCode::ladder_movement_unsupported,
            statistics, "ladder movement remains unsupported");
    }
    if (environment.profile() !=
        GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1) {
        return fail(LocalMovementSimulationErrorCode::invalid_environment,
            statistics, "movement environment is not executable");
    }
    if (command.compatibility_profile() !=
        GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1) {
        return fail(
            command.compatibility_profile() ==
                    GoldSrcUserCmdCompatibilityProfile::
                        stock_protocol_48_evidence_pending ||
                    command.compatibility_profile() ==
                        GoldSrcUserCmdCompatibilityProfile::
                            stock_protocol_48_build_10210
                ? LocalMovementSimulationErrorCode::stock_semantics_pending
                : LocalMovementSimulationErrorCode::unsupported_command_profile,
            statistics,
            "only synthetic usercmd semantics are executable locally");
    }
    if (previous_state.source_command_sequence() == UINT32_MAX ||
        sequence != previous_state.source_command_sequence() + 1U) {
        return fail(
            LocalMovementSimulationErrorCode::invalid_command_sequence,
            statistics,
            "movement commands must be contiguous and strictly ordered");
    }
    const auto duration = static_cast<double>(command.msec()) * 0.001;
    if (!finite(duration) || duration <= 0.0 ||
        duration > config.maximum_command_duration_seconds) {
        return fail(
            LocalMovementSimulationErrorCode::invalid_command_duration,
            statistics,
            "usercmd msec is outside the movement duration profile");
    }
    const auto substep_count_wide = static_cast<std::size_t>(
        std::ceil(duration / config.maximum_substep_duration_seconds));
    if (substep_count_wide == 0U ||
        substep_count_wide > config.maximum_substeps_per_command) {
        return fail(
            LocalMovementSimulationErrorCode::substep_limit_exceeded,
            statistics,
            "movement command exceeds the bounded substep count");
    }
    if (previous_state.state_revision() == UINT64_MAX ||
        previous_state.state_revision() >=
            config.state_limits.maximum_state_revision) {
        return fail(
            LocalMovementSimulationErrorCode::state_revision_exhausted,
            statistics, "movement state revision is exhausted");
    }
    const auto command_nanoseconds =
        static_cast<std::uint64_t>(command.msec()) * 1'000'000ULL;
    if (previous_state.simulation_time_nanoseconds() >
        UINT64_MAX - command_nanoseconds) {
        return fail(
            LocalMovementSimulationErrorCode::simulation_time_overflow,
            statistics, "movement simulation time overflowed");
    }

    WorkingState state = working_state(previous_state);
    state.view_angles = {
        command.view_angles()[0U], command.view_angles()[1U],
        command.view_angles()[2U]};
    if (!finite(state.view_angles)) {
        return fail(LocalMovementSimulationErrorCode::non_finite_result,
            statistics, "command view angles are non-finite");
    }
    if (auto error = validate_start_position(
            state, collision, scratch, config, statistics)) {
        return fail(error->code, statistics, error->context,
            std::move(error->collision_error));
    }
    if (auto error = update_contents(state, collision, scratch, config)) {
        return fail(error->code, statistics, error->context,
            std::move(error->collision_error));
    }

    std::vector<PlayerMovementTouch> touches;
    try {
        touches.reserve(config.maximum_touches_per_command);
    } catch (const std::bad_alloc&) {
        return fail(LocalMovementSimulationErrorCode::allocation_failed,
            statistics, "movement touch allocation failed");
    } catch (const std::length_error&) {
        return fail(LocalMovementSimulationErrorCode::allocation_failed,
            statistics, "movement touch storage length failed");
    }
    const auto effective_gravity_double =
        static_cast<double>(environment.gravity()) *
        previous_state.gravity_multiplier() * environment.entity_gravity();
    if (!finite(effective_gravity_double) || effective_gravity_double <= 0.0 ||
        effective_gravity_double > environment.maximum_velocity() * 1'000.0) {
        return fail(LocalMovementSimulationErrorCode::invalid_environment,
            statistics, "effective gravity is invalid");
    }
    const auto effective_gravity = static_cast<float>(effective_gravity_double);
    const bool jump_edge =
        (command.buttons() & kSyntheticGoldSrcButtonJump) != 0U &&
        (previous_state.old_buttons() & kSyntheticGoldSrcButtonJump) == 0U;
    const bool duck_requested =
        (command.buttons() & kSyntheticGoldSrcButtonDuck) != 0U;
    const auto equal_substep = duration /
        static_cast<double>(substep_count_wide);
    double consumed_duration = 0.0;
    for (std::size_t substep = 0U; substep < substep_count_wide; ++substep) {
        scratch.diagnostic_substep_ordinal =
            static_cast<std::uint16_t>(substep);
        ++statistics.substep_count;
        const auto substep_duration = substep + 1U == substep_count_wide
            ? duration - consumed_duration
            : equal_substep;
        consumed_duration += substep_duration;
        const auto dt = static_cast<float>(substep_duration);
        if (!std::isfinite(dt) || dt <= 0.0F) {
            return fail(
                LocalMovementSimulationErrorCode::invalid_command_duration,
                statistics,
                "movement substep duration is non-finite or empty");
        }
        if (auto error = categorize_ground(
                state, collision, scratch, config, statistics)) {
            return fail(error->code, statistics, error->context,
                std::move(error->collision_error));
        }
        if (substep == 0U) {
            if (auto error = apply_duck_transition(
                    state, duck_requested, collision, scratch, config,
                    statistics)) {
                return fail(error->code, statistics,
                    error->context, std::move(error->collision_error));
            }
        }
        bool jumped = false;
        if (substep == 0U && jump_edge && state.ground.grounded) {
            state.ground = airborne_ground();
            state.mode = PlayerMovementMode::airborne;
            state.velocity.z = static_cast<float>(config.jump_impulse);
            ++statistics.jump_count;
            jumped = true;
        }
        std::optional<OperationError> error;
        if (state.ground.grounded && !jumped) {
            ++statistics.grounded_command_count;
            error = apply_walk_move(
                state, command, dt, environment, collision, scratch, config,
                statistics, touches);
        } else {
            ++statistics.airborne_command_count;
            error = apply_air_move(
                state, command, dt, effective_gravity, environment, collision,
                scratch, config, statistics, touches);
        }
        if (error) {
            return fail(error->code, statistics, error->context,
                std::move(error->collision_error));
        }
        if (auto clamp_error = clamp_velocity(state, environment)) {
            return fail(clamp_error->code, statistics,
                clamp_error->context,
                std::move(clamp_error->collision_error));
        }
        if (auto contents_error = update_contents(
                state, collision, scratch, config)) {
            return fail(contents_error->code, statistics,
                contents_error->context,
                std::move(contents_error->collision_error));
        }
    }
    ++statistics.command_count;
    statistics.total_horizontal_distance =
        horizontal_distance(previous_state.origin(), state.origin);
    statistics.total_vertical_distance =
        std::abs(static_cast<double>(state.origin.z) -
            previous_state.origin().z);
    const auto simulation_time =
        previous_state.simulation_time_nanoseconds() + command_nanoseconds;
    const auto revision = previous_state.state_revision() + 1U;
    const auto created = LocalPlayerMovementState::create(
        final_state_info(
            previous_state, state, command, simulation_time, revision),
        config.state_limits);
    if (!created || !created.state) {
        return fail(LocalMovementSimulationErrorCode::non_finite_result,
            statistics, "final movement state validation failed");
    }
    LocalMovementSimulationResult result;
    result.deterministic_state_signature =
        hlclient::movement::local_player_movement_state_signature(
            *created.state);
    result.state.emplace(std::move(*created.state));
    result.touches = std::move(touches);
    result.statistics = statistics;
    result.command_sequence = sequence;
    auto final_diagnostic = scratch.last_diagnostic.value_or(
        PlayerWallContactDiagnosticFrame{});
    final_diagnostic.command_sequence = sequence;
    final_diagnostic.state_signature_before =
        hlclient::movement::local_player_movement_state_signature(
            previous_state);
    final_diagnostic.state_signature_after =
        result.deterministic_state_signature;
    final_diagnostic.substep_ordinal = scratch.diagnostic_substep_ordinal;
    final_diagnostic.result = PlayerMovementDiagnosticResult::success;
    record_diagnostic(scratch, final_diagnostic);
    return result;
}

} // namespace hlclient::goldsrc::movement

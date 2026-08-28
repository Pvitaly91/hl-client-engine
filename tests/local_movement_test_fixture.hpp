#pragma once

#include <hlclient/goldsrc/movement/goldsrc_local_movement.hpp>
#include <hlclient/goldsrc/movement/goldsrc_movement_math.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace hlclient::tests::local_movement {

namespace goldsrc = hlclient::goldsrc;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

inline constexpr float kFixtureExtent = 100'000.0F;

struct FixtureBox {
    assets::AssetVector3 minimum{};
    assets::AssetVector3 maximum{};
    std::uint32_t model_index{0U};
};

struct FixtureLiquid {
    assets::AssetVector3 minimum{};
    assets::AssetVector3 maximum{};
    player::PlayerMovementContents contents{
        player::PlayerMovementContents::water};
};

class DeterministicLocalMovementCollision final
    : public movement::ILocalMovementCollision {
public:
    explicit DeterministicLocalMovementCollision(const bool with_floor = true)
    {
        if (with_floor) {
            add_box(
                {-kFixtureExtent, -kFixtureExtent, -kFixtureExtent},
                {kFixtureExtent, kFixtureExtent, 0.0F});
        }
    }

    void add_box(
        const assets::AssetVector3& minimum,
        const assets::AssetVector3& maximum)
    {
        solids_.push_back(FixtureBox{
            minimum, maximum,
            static_cast<std::uint32_t>(solids_.size() + 1U)});
    }

    void add_positive_x_wall(const float surface_x)
    {
        add_box(
            {surface_x, -kFixtureExtent, -kFixtureExtent},
            {kFixtureExtent, kFixtureExtent, kFixtureExtent});
    }

    void add_positive_y_wall(const float surface_y)
    {
        add_box(
            {-kFixtureExtent, surface_y, -kFixtureExtent},
            {kFixtureExtent, kFixtureExtent, kFixtureExtent});
    }

    void add_ceiling(const float surface_z)
    {
        add_box(
            {-kFixtureExtent, -kFixtureExtent, surface_z},
            {kFixtureExtent, kFixtureExtent, kFixtureExtent});
    }

    void add_step(
        const float minimum_x,
        const float maximum_x,
        const float minimum_y,
        const float maximum_y,
        const float height)
    {
        add_box(
            {minimum_x, minimum_y, 0.0F},
            {maximum_x, maximum_y, height});
    }

    void add_liquid(
        const assets::AssetVector3& minimum,
        const assets::AssetVector3& maximum,
        const player::PlayerMovementContents contents =
            player::PlayerMovementContents::water)
    {
        liquids_.push_back({minimum, maximum, contents});
    }

    void set_valid(const bool value) noexcept { valid_ = value; }
    void fail_point_contents(const bool value = true) noexcept
    {
        fail_point_contents_ = value;
    }
    void fail_position_tests(const bool value = true) noexcept
    {
        fail_position_tests_ = value;
    }
    void fail_traces(const bool value = true) noexcept
    {
        fail_traces_ = value;
    }
    void zero_length_blocking_is_all_solid(const bool value) noexcept
    {
        zero_length_blocking_is_all_solid_ = value;
    }

    [[nodiscard]] movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return movement::LocalMovementCollisionProfile::
            explicit_synthetic_static_brush_v1;
    }

    [[nodiscard]] bool valid() const noexcept override { return valid_; }

    [[nodiscard]] movement::LocalMovementPointContentsQueryResult
    point_contents(
        const assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch&,
        const movement::LocalMovementCollisionQueryConfig&) const override
    {
        if (fail_point_contents_) {
            return point_failure();
        }
        movement::LocalMovementPointContents result;
        result.contents = contents_at(point);
        result.traversal_depth = 1U;
        return {result, std::nullopt};
    }

    [[nodiscard]] movement::LocalMovementPositionQueryResult test_position(
        const assets::AssetVector3& origin,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch&,
        const movement::LocalMovementCollisionQueryConfig&) const override
    {
        if (fail_position_tests_) {
            return position_failure();
        }
        const auto hull_bounds = bounds_for(hull);
        if (!hull_bounds) {
            return position_failure(
                movement::LocalMovementCollisionErrorCode::invalid_hull);
        }
        movement::LocalMovementPositionTest result;
        result.contents = contents_at(origin);
        result.traversal_depth = 1U;
        for (const auto& solid : solids_) {
            const auto expanded = expand(solid, *hull_bounds);
            if (strictly_inside(origin, expanded.minimum, expanded.maximum)) {
                result.status = movement::LocalMovementPositionStatus::blocking;
                result.contents = solid_contents();
                result.hit = hit_for(solid);
                break;
            }
        }
        return {result, std::nullopt};
    }

    [[nodiscard]] movement::LocalMovementTraceQueryResult trace_hull(
        const assets::AssetVector3& start,
        const assets::AssetVector3& end,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch&,
        const movement::LocalMovementCollisionQueryConfig&) const override
    {
        if (fail_traces_) {
            return trace_failure();
        }
        const auto hull_bounds = bounds_for(hull);
        if (!hull_bounds) {
            return trace_failure(
                movement::LocalMovementCollisionErrorCode::invalid_hull);
        }

        movement::LocalMovementTrace result;
        result.fraction = 1.0;
        result.end_position = end;
        result.start_contents = contents_at(start);
        result.end_contents = contents_at(end);
        result.in_open = result.start_contents.category ==
                player::PlayerMovementContents::empty ||
            result.end_contents.category ==
                player::PlayerMovementContents::empty;
        result.in_liquid = liquid(result.start_contents.category) ||
            liquid(result.end_contents.category);
        result.collision_profile = profile();
        result.traversal_statistics.traversal_steps = solids_.size();
        result.traversal_statistics.maximum_stack_entries =
            solids_.empty() ? 0U : 1U;

        const auto delta = subtract(end, start);
        std::optional<SweepHit> best;
        for (const auto& solid : solids_) {
            const auto expanded = expand(solid, *hull_bounds);
            if (strictly_inside(start, expanded.minimum, expanded.maximum)) {
                result.start_solid = true;
                const bool zero_length = delta.x == 0.0F &&
                    delta.y == 0.0F && delta.z == 0.0F;
                result.all_solid = strictly_inside(
                        end, expanded.minimum, expanded.maximum) &&
                    (!zero_length || zero_length_blocking_is_all_solid_);
                result.fraction = 0.0;
                result.end_position = start;
                result.hit = hit_for(solid);
                result.blocking_contents = solid_contents();
                return {result, std::nullopt};
            }
            const auto swept = sweep_point(start, delta, expanded);
            if (swept && (!best || swept->fraction < best->fraction)) {
                best = *swept;
                best->box = &solid;
            }
        }

        if (best && best->box) {
            result.fraction = best->fraction;
            result.end_position = add(
                start, scale(delta, static_cast<float>(best->fraction)));
            result.collision_plane = player::PlayerMovementPlane{
                best->normal, 0.0, std::nullopt};
            result.hit = hit_for(*best->box);
            result.blocking_contents = solid_contents();
            result.end_contents = contents_at(result.end_position);
            result.traversal_statistics.fraction_split_count = 1U;
            result.traversal_statistics.terminal_interval_count = 1U;
        }
        return {result, std::nullopt};
    }

private:
    struct HullBounds {
        assets::AssetVector3 minimum{};
        assets::AssetVector3 maximum{};
    };

    struct ExpandedBox {
        assets::AssetVector3 minimum{};
        assets::AssetVector3 maximum{};
    };

    struct SweepHit {
        double fraction{1.0};
        assets::AssetVector3 normal{};
        const FixtureBox* box{nullptr};
    };

    [[nodiscard]] static std::optional<HullBounds> bounds_for(
        const player::PlayerMovementHull hull) noexcept
    {
        switch (hull) {
        case player::PlayerMovementHull::standing:
            return HullBounds{
                {-16.0F, -16.0F, movement::kValveStandingHullMinimumZ},
                {16.0F, 16.0F, movement::kValveStandingHullMaximumZ}};
        case player::PlayerMovementHull::ducked:
            return HullBounds{
                {-16.0F, -16.0F, movement::kValveDuckHullMinimumZ},
                {16.0F, 16.0F, movement::kValveDuckHullMaximumZ}};
        }
        return std::nullopt;
    }

    [[nodiscard]] static ExpandedBox expand(
        const FixtureBox& box,
        const HullBounds& hull) noexcept
    {
        return {
            {
                box.minimum.x - hull.maximum.x,
                box.minimum.y - hull.maximum.y,
                box.minimum.z - hull.maximum.z,
            },
            {
                box.maximum.x - hull.minimum.x,
                box.maximum.y - hull.minimum.y,
                box.maximum.z - hull.minimum.z,
            },
        };
    }

    [[nodiscard]] static bool strictly_inside(
        const assets::AssetVector3& point,
        const assets::AssetVector3& minimum,
        const assets::AssetVector3& maximum) noexcept
    {
        return point.x > minimum.x && point.x < maximum.x &&
            point.y > minimum.y && point.y < maximum.y &&
            point.z > minimum.z && point.z < maximum.z;
    }

    [[nodiscard]] static bool point_inside_closed(
        const assets::AssetVector3& point,
        const assets::AssetVector3& minimum,
        const assets::AssetVector3& maximum) noexcept
    {
        return point.x >= minimum.x && point.x <= maximum.x &&
            point.y >= minimum.y && point.y <= maximum.y &&
            point.z >= minimum.z && point.z <= maximum.z;
    }

    [[nodiscard]] static std::optional<SweepHit> sweep_point(
        const assets::AssetVector3& start,
        const assets::AssetVector3& delta,
        const ExpandedBox& box) noexcept
    {
        constexpr double epsilon = 1.0e-7;
        double entry = -std::numeric_limits<double>::infinity();
        double exit = std::numeric_limits<double>::infinity();
        assets::AssetVector3 entry_normal{};

        const std::array starts{start.x, start.y, start.z};
        const std::array deltas{delta.x, delta.y, delta.z};
        const std::array minima{box.minimum.x, box.minimum.y, box.minimum.z};
        const std::array maxima{box.maximum.x, box.maximum.y, box.maximum.z};
        for (std::size_t axis = 0U; axis < starts.size(); ++axis) {
            const auto start_axis = static_cast<double>(starts[axis]);
            const auto delta_axis = static_cast<double>(deltas[axis]);
            const auto minimum = static_cast<double>(minima[axis]);
            const auto maximum = static_cast<double>(maxima[axis]);
            if (std::abs(delta_axis) <= epsilon) {
                // Merely touching a face is a free position and may move
                // tangentially along that face.
                if (start_axis <= minimum || start_axis >= maximum) {
                    return std::nullopt;
                }
                continue;
            }

            auto near_time = (minimum - start_axis) / delta_axis;
            auto far_time = (maximum - start_axis) / delta_axis;
            assets::AssetVector3 near_normal{};
            if (axis == 0U) {
                near_normal.x = delta_axis > 0.0 ? -1.0F : 1.0F;
            } else if (axis == 1U) {
                near_normal.y = delta_axis > 0.0 ? -1.0F : 1.0F;
            } else {
                near_normal.z = delta_axis > 0.0 ? -1.0F : 1.0F;
            }
            if (near_time > far_time) {
                std::swap(near_time, far_time);
            }
            if (near_time > entry) {
                entry = near_time;
                entry_normal = near_normal;
            }
            exit = std::min(exit, far_time);
            if (entry > exit) {
                return std::nullopt;
            }
        }

        // A boundary point moving away has a negative entry time. A boundary
        // point moving inward has entry == 0 and is a real blocking hit.
        if (entry < -epsilon || entry > 1.0 || exit <= epsilon) {
            return std::nullopt;
        }
        return SweepHit{std::clamp(entry, 0.0, 1.0), entry_normal, nullptr};
    }

    [[nodiscard]] movement::LocalMovementCollisionContents contents_at(
        const assets::AssetVector3& point) const noexcept
    {
        for (const auto& liquid_box : liquids_) {
            if (point_inside_closed(
                    point, liquid_box.minimum, liquid_box.maximum)) {
                return {liquid_box.contents, liquid_source_code(
                    liquid_box.contents)};
            }
        }
        for (const auto& solid : solids_) {
            if (strictly_inside(point, solid.minimum, solid.maximum)) {
                return solid_contents();
            }
        }
        return {player::PlayerMovementContents::empty, -1};
    }

    [[nodiscard]] static std::int32_t liquid_source_code(
        const player::PlayerMovementContents contents) noexcept
    {
        switch (contents) {
        case player::PlayerMovementContents::water: return -3;
        case player::PlayerMovementContents::slime: return -4;
        case player::PlayerMovementContents::lava: return -5;
        case player::PlayerMovementContents::current: return -9;
        default: return -1;
        }
    }

    [[nodiscard]] static bool liquid(
        const player::PlayerMovementContents contents) noexcept
    {
        return contents == player::PlayerMovementContents::water ||
            contents == player::PlayerMovementContents::slime ||
            contents == player::PlayerMovementContents::lava ||
            contents == player::PlayerMovementContents::current;
    }

    [[nodiscard]] static movement::LocalMovementCollisionContents
    solid_contents() noexcept
    {
        return {player::PlayerMovementContents::solid, -2};
    }

    [[nodiscard]] static player::PlayerMovementHitIdentity hit_for(
        const FixtureBox& box) noexcept
    {
        player::PlayerMovementHitIdentity hit;
        hit.kind = player::PlayerMovementHitKind::explicit_synthetic_brush;
        hit.source_model_index = box.model_index;
        hit.stable_instance_ordinal = box.model_index;
        return hit;
    }

    [[nodiscard]] static assets::AssetVector3 subtract(
        const assets::AssetVector3& left,
        const assets::AssetVector3& right) noexcept
    {
        return {left.x - right.x, left.y - right.y, left.z - right.z};
    }

    [[nodiscard]] static assets::AssetVector3 add(
        const assets::AssetVector3& left,
        const assets::AssetVector3& right) noexcept
    {
        return {left.x + right.x, left.y + right.y, left.z + right.z};
    }

    [[nodiscard]] static assets::AssetVector3 scale(
        const assets::AssetVector3& value,
        const float factor) noexcept
    {
        return {value.x * factor, value.y * factor, value.z * factor};
    }

    [[nodiscard]] static movement::LocalMovementCollisionError error(
        const movement::LocalMovementCollisionErrorCode code =
            movement::LocalMovementCollisionErrorCode::world_query_failed)
        noexcept
    {
        movement::LocalMovementCollisionError value;
        value.code = code;
        return value;
    }

    [[nodiscard]] static movement::LocalMovementPointContentsQueryResult
    point_failure()
    {
        return {std::nullopt, error()};
    }

    [[nodiscard]] static movement::LocalMovementPositionQueryResult
    position_failure(
        const movement::LocalMovementCollisionErrorCode code =
            movement::LocalMovementCollisionErrorCode::world_query_failed)
    {
        return {std::nullopt, error(code)};
    }

    [[nodiscard]] static movement::LocalMovementTraceQueryResult trace_failure(
        const movement::LocalMovementCollisionErrorCode code =
            movement::LocalMovementCollisionErrorCode::world_query_failed)
    {
        return {std::nullopt, error(code)};
    }

    std::vector<FixtureBox> solids_;
    std::vector<FixtureLiquid> liquids_;
    bool valid_{true};
    bool fail_point_contents_{false};
    bool fail_position_tests_{false};
    bool fail_traces_{false};
    bool zero_length_blocking_is_all_solid_{true};
};

[[nodiscard]] inline player::LocalPlayerMovementState make_state(
    const assets::AssetVector3& origin = {0.0F, 0.0F, 36.0F},
    const assets::AssetVector3& velocity = {},
    const player::PlayerMovementMode mode = player::PlayerMovementMode::walking,
    const player::PlayerMovementHull hull = player::PlayerMovementHull::standing,
    const std::uint32_t source_sequence = 0U,
    const std::uint16_t old_buttons = 0U,
    const float gravity_multiplier = 1.0F,
    const float friction_multiplier = 1.0F,
    const std::uint64_t simulation_time_nanoseconds = 0U,
    const std::uint64_t state_revision = 1U)
{
    player::LocalPlayerMovementStateCreateInfo info;
    info.origin = origin;
    info.velocity = velocity;
    info.hull = hull;
    info.mode = mode;
    info.view_offset = hull == player::PlayerMovementHull::standing
        ? assets::AssetVector3{0.0F, 0.0F, movement::kValveStandingViewOffsetZ}
        : assets::AssetVector3{0.0F, 0.0F, movement::kValveDuckViewOffsetZ};
    if (mode == player::PlayerMovementMode::walking) {
        info.ground.grounded = true;
        info.ground.walkable = true;
        info.ground.hit = player::PlayerMovementHitIdentity{
            player::PlayerMovementHitKind::explicit_synthetic_brush,
            1U, 1U, std::nullopt};
        info.ground.plane.normal = {0.0F, 0.0F, 1.0F};
        info.ground.contact_position = origin;
        info.ground.probe_fraction = 0.0;
    }
    info.old_buttons = old_buttons;
    info.source_command_sequence = source_sequence;
    info.simulation_time_nanoseconds = simulation_time_nanoseconds;
    info.gravity_multiplier = gravity_multiplier;
    info.friction_multiplier = friction_multiplier;
    info.state_revision = state_revision;
    const auto created = player::LocalPlayerMovementState::create(info);
    if (!created.state) {
        std::terminate();
    }
    return *created.state;
}

[[nodiscard]] inline goldsrc::GoldSrcUserCmdState make_command(
    const std::uint32_t sequence,
    const std::uint8_t msec = 10U,
    const float forward_move = 0.0F,
    const float side_move = 0.0F,
    const std::uint16_t buttons = 0U,
    const float yaw_degrees = 0.0F)
{
    const auto sequence_value = goldsrc::GoldSrcUserCmdSequence::create(sequence);
    if (!sequence_value) {
        std::terminate();
    }
    auto info = goldsrc::goldsrc_usercmd_default_create_info(*sequence_value);
    info.msec = msec;
    info.sample_duration_nanoseconds =
        static_cast<std::uint64_t>(msec) * 1'000'000ULL;
    info.view_angles[1U] = yaw_degrees;
    info.forward_move = forward_move;
    info.side_move = side_move;
    info.buttons = buttons;
    const auto created = goldsrc::GoldSrcUserCmdState::create(info);
    if (!created.state) {
        std::terminate();
    }
    return *created.state;
}

[[nodiscard]] inline movement::GoldSrcMovementEnvironment make_environment()
{
    auto built = movement::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    if (!built.environment) {
        std::terminate();
    }
    return *built.environment;
}

[[nodiscard]] inline movement::LocalMovementSimulationResult simulate(
    const player::LocalPlayerMovementState& state,
    const goldsrc::GoldSrcUserCmdState& command,
    const movement::ILocalMovementCollision& collision,
    const movement::GoldSrcLocalMovementConfig& config = {})
{
    auto environment = make_environment();
    movement::GoldSrcLocalMovementScratch scratch;
    return movement::GoldSrcLocalMovementKernel::simulate(
        state, command, environment, collision, scratch, config);
}

} // namespace hlclient::tests::local_movement

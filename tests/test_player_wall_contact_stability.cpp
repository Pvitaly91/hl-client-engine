#include "literal_movement_bsp_fixture.hpp"
#include "local_movement_test_fixture.hpp"

#include <hlclient/collision/collision_contents.hpp>
#include <hlclient/collision/collision_world_package.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace {

namespace fixture = hlclient::tests::local_movement;
namespace literal = hlclient::tests::literal_movement_bsp;
namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::goldsrc::collision;
namespace core_collision = hlclient::collision;
namespace goldsrc = hlclient::goldsrc;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;

[[nodiscard]] std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
wall_package()
{
    const auto parsed = bsp::GoldSrcBspParser::parse(literal::make_bsp_v30());
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    const auto built = collision::GoldSrcCollisionWorldBuilder::build(
        parsed.document->collision_source);
    REQUIRE(built);
    REQUIRE(built.package);
    return built.package;
}

[[nodiscard]] core_collision::CollisionContents collision_contents(
    const std::int32_t raw)
{
    const auto decoded = core_collision::decode_goldsrc_contents({raw});
    REQUIRE(decoded);
    return *decoded;
}

[[nodiscard]] std::array<core_collision::CollisionHull,
    core_collision::kCollisionHullCount> oblique_hulls()
{
    std::array<core_collision::CollisionHull,
        core_collision::kCollisionHullCount> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto ordinal = core_collision::collision_hull_ordinal(index);
        const auto profile = ordinal
            ? core_collision::standard_collision_hull_profile(*ordinal)
            : std::nullopt;
        REQUIRE(ordinal);
        REQUIRE(profile);
        result[index] = core_collision::CollisionHull{
            *ordinal,
            index == 0U
                ? core_collision::CollisionHullTreeDomain::node_leaf
                : core_collision::CollisionHullTreeDomain::clipnode,
            core_collision::CollisionHullRoot{
                index == 0U
                    ? core_collision::CollisionHullRootKind::node
                    : core_collision::CollisionHullRootKind::clipnode,
                0U,
                collision_contents(-1)},
            *profile};
    }
    return result;
}

[[nodiscard]] std::shared_ptr<const core_collision::CollisionWorldPackage>
oblique_wall_package()
{
    constexpr hlclient::assets::AssetVector3 normal{
        0.70710677F, 0.70710677F, 0.0F};
    const core_collision::CollisionModel model{
        0U,
        {},
        hlclient::assets::WorldBounds{
            {-128.0F, -128.0F, -128.0F},
            {128.0F, 128.0F, 128.0F}},
        0U,
        1U,
        oblique_hulls()};
    return std::make_shared<const core_collision::CollisionWorldPackage>(
        std::vector<core_collision::CollisionPlane>{
            {normal, 1.0, 77U, 3}},
        std::vector<core_collision::CollisionNode>{
            {0U,
                {core_collision::CollisionNodeChild{
                     core_collision::CollisionNodeChildKind::leaf, 1U},
                    core_collision::CollisionNodeChild{
                        core_collision::CollisionNodeChildKind::leaf,
                        0U}}}},
        std::vector<core_collision::CollisionLeaf>{
            {0U, collision_contents(-2)},
            {1U, collision_contents(-1)}},
        std::vector<core_collision::CollisionClipnode>{
            {0U,
                {core_collision::CollisionClipnodeChild{
                     core_collision::CollisionClipnodeChildKind::terminal,
                     0U,
                     collision_contents(-1)},
                    core_collision::CollisionClipnodeChild{
                        core_collision::CollisionClipnodeChildKind::terminal,
                        0U,
                        collision_contents(-2)}}}},
        std::vector<core_collision::CollisionModel>{model});
}

class ScriptedContactCollision final
    : public movement::ILocalMovementCollision {
public:
    ScriptedContactCollision(
        const double fraction,
        const std::array<hlclient::assets::AssetVector3, 4U>& planes,
        const std::size_t plane_count,
        const std::optional<std::size_t> fail_on_horizontal_trace =
            std::nullopt) noexcept
        : fraction_{fraction},
          planes_{planes},
          plane_count_{plane_count},
          fail_on_horizontal_trace_{fail_on_horizontal_trace}
    {
        if (plane_count_ == 0U || plane_count_ > planes_.size()) {
            plane_count_ = 1U;
        }
    }

    [[nodiscard]] movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return base_.profile();
    }

    [[nodiscard]] bool valid() const noexcept override { return true; }

    [[nodiscard]] movement::LocalMovementPointContentsQueryResult
    point_contents(
        const hlclient::assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return base_.point_contents(point, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementPositionQueryResult test_position(
        const hlclient::assets::AssetVector3& origin,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return base_.test_position(origin, hull, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementTraceQueryResult trace_hull(
        const hlclient::assets::AssetVector3& start,
        const hlclient::assets::AssetVector3& end,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        if (start.x == end.x && start.y == end.y) {
            return base_.trace_hull(start, end, hull, scratch, config);
        }
        if (fail_on_horizontal_trace_ &&
            trace_count_ == *fail_on_horizontal_trace_) {
            movement::LocalMovementCollisionError error;
            error.code = movement::LocalMovementCollisionErrorCode::
                world_query_failed;
            return {std::nullopt, error};
        }
        const auto plane_index = std::min(trace_count_, plane_count_ - 1U);
        ++trace_count_;
        movement::LocalMovementTrace trace;
        trace.fraction = fraction_;
        trace.end_position = {
            static_cast<float>(start.x + (end.x - start.x) * fraction_),
            static_cast<float>(start.y + (end.y - start.y) * fraction_),
            static_cast<float>(start.z + (end.z - start.z) * fraction_),
        };
        trace.collision_plane = player::PlayerMovementPlane{
            planes_[plane_index], 0.0,
            static_cast<std::uint32_t>(plane_index)};
        trace.hit = player::PlayerMovementHitIdentity{
            player::PlayerMovementHitKind::explicit_synthetic_brush,
            1U,
            1U,
            std::nullopt};
        trace.start_contents = {player::PlayerMovementContents::empty, -1};
        trace.end_contents = {player::PlayerMovementContents::solid, -2};
        trace.blocking_contents =
            movement::LocalMovementCollisionContents{
                player::PlayerMovementContents::solid, -2};
        trace.collision_profile = profile();
        trace.traversal_statistics.traversal_steps = 1U;
        return {trace, std::nullopt};
    }

private:
    fixture::DeterministicLocalMovementCollision base_{false};
    double fraction_{0.0};
    std::array<hlclient::assets::AssetVector3, 4U> planes_{};
    std::size_t plane_count_{1U};
    std::optional<std::size_t> fail_on_horizontal_trace_;
    mutable std::size_t trace_count_{0U};
};

class LegacyNoContactEndpointCollision final
    : public movement::ILocalMovementCollision {
public:
    explicit LegacyNoContactEndpointCollision(
        const bool advertise_blocking_end_contents)
        : advertise_blocking_end_contents_{
              advertise_blocking_end_contents}
    {
        base_.add_positive_x_wall(20.0F);
    }

    [[nodiscard]] movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return base_.profile();
    }

    [[nodiscard]] bool valid() const noexcept override
    {
        return base_.valid();
    }

    [[nodiscard]] movement::LocalMovementPointContentsQueryResult
    point_contents(
        const hlclient::assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return base_.point_contents(point, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementPositionQueryResult test_position(
        const hlclient::assets::AssetVector3& origin,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return base_.test_position(origin, hull, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementTraceQueryResult trace_hull(
        const hlclient::assets::AssetVector3& start,
        const hlclient::assets::AssetVector3& end,
        const player::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        auto traced = base_.trace_hull(start, end, hull, scratch, config);
        if (!traced || !traced.result ||
            !traced.result->collision_plane ||
            (start.x == end.x && start.y == end.y && start.z == end.z)) {
            return traced;
        }

        // Reproduce the pre-contact-metadata contract: a sweep that ends in
        // solid is incorrectly returned as a complete no-hit trace.
        auto& trace = *traced.result;
        trace.fraction = 1.0;
        trace.end_position = end;
        trace.collision_plane.reset();
        trace.hit.reset();
        trace.blocking_contents.reset();
        trace.end_contents = advertise_blocking_end_contents_
            ? movement::LocalMovementCollisionContents{
                  player::PlayerMovementContents::solid, -2}
            : movement::LocalMovementCollisionContents{
                  player::PlayerMovementContents::empty, -1};
        return traced;
    }

private:
    fixture::DeterministicLocalMovementCollision base_{false};
    bool advertise_blocking_end_contents_{false};
};

struct ProductionCampaignSummary {
    std::uint64_t signature{0U};
    std::uint64_t selected_wall_contacts{0U};
    std::uint64_t contacts_before_release{0U};
    std::uint64_t contacts_during_release{0U};
    std::uint64_t contacts_after_release{0U};
    std::uint64_t collision_count{0U};
    std::uint64_t jump_count{0U};
    std::uint64_t duck_enter_count{0U};
    std::uint64_t duck_exit_count{0U};
    std::uint64_t start_solid_count{0U};
    std::uint64_t all_solid_count{0U};
    float final_x{0.0F};
    float final_y{0.0F};
    float final_z{0.0F};
    float minimum_z{0.0F};
    float maximum_z{0.0F};
    bool all_positions_free{true};
    bool saw_ducked{false};
    bool saw_standing_after_duck{false};
    player::PlayerMovementHull final_hull{
        player::PlayerMovementHull::standing};

    [[nodiscard]] friend bool operator==(
        const ProductionCampaignSummary&,
        const ProductionCampaignSummary&) = default;
};

TEST_CASE("Legacy complete no-contact traces cannot publish a blocking endpoint",
    "[goldsrc][movement][wall-contact][provider-contract][transactional]")
{
    const auto initial = fixture::make_state(
        {0.0F, 0.0F, 100.0F}, {1'000.0F, 0.0F, 0.0F},
        player::PlayerMovementMode::airborne);

    for (const bool advertises_blocking_contents : {false, true}) {
        INFO(advertises_blocking_contents);
        LegacyNoContactEndpointCollision collision_source{
            advertises_blocking_contents};
        const auto result = fixture::simulate(
            initial, fixture::make_command(1U), collision_source);

        REQUIRE_FALSE(result);
        CHECK_FALSE(result.state);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            movement::LocalMovementSimulationErrorCode::
                collision_query_failed);
        REQUIRE(result.error->collision_error);
        CHECK(result.error->collision_error->code ==
            movement::LocalMovementCollisionErrorCode::
                invalid_collision_source);
    }
}

TEST_CASE("Rounded production oblique contacts retain the prior free origin",
    "[goldsrc][movement][wall-contact][oblique][production-query]")
{
    movement::WorldOnlyMovementCollision world{oblique_wall_package()};
    movement::GoldSrcLocalMovementScratch scratch;
    movement::GoldSrcLocalMovementConfig config;
    config.maximum_slide_bumps = 1U;
    const hlclient::assets::AssetVector3 start{2.0F, 0.0F, 100.0F};
    const hlclient::assets::AssetVector3 penetrating_end{
        0.0F, 0.0F, 100.0F};

    const auto traced = world.trace_hull(
        start, penetrating_end, player::PlayerMovementHull::standing,
        scratch.collision, config.collision_query);
    REQUIRE(traced);
    REQUIRE(traced.result);
    REQUIRE(traced.result->collision_plane);
    CHECK(traced.result->fraction == Catch::Approx(0.29289320671183516));
    const auto rounded_endpoint = world.test_position(
        traced.result->end_position, player::PlayerMovementHull::standing,
        scratch.collision, config.collision_query);
    REQUIRE(rounded_endpoint);
    REQUIRE(rounded_endpoint.result);
    REQUIRE(rounded_endpoint.result->status ==
        movement::LocalMovementPositionStatus::blocking);

    auto environment = fixture::make_environment();
    std::optional<player::LocalPlayerMovementState> state;
    state.emplace(fixture::make_state(
        start, {-200.0F, 0.0F, 0.0F},
        player::PlayerMovementMode::airborne));
    std::uint64_t contacts = 0U;
    for (std::uint32_t sequence = 1U; sequence <= 128U; ++sequence) {
        const auto prior_origin = state->origin();
        auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
            *state,
            fixture::make_command(sequence, 10U, 320.0F, 0.0F, 0U,
                180.0F),
            environment, world, scratch, config);
        INFO(sequence);
        REQUIRE(simulated);
        REQUIRE(simulated.state);
        contacts += simulated.statistics.collision_hit_count;
        const auto published = world.test_position(
            simulated.state->origin(), simulated.state->hull(),
            scratch.collision, config.collision_query);
        REQUIRE(published);
        REQUIRE(published.result);
        CHECK(published.result->status ==
            movement::LocalMovementPositionStatus::free);
        if (simulated.statistics.collision_hit_count > 0U) {
            CHECK(simulated.state->origin().x == prior_origin.x);
            CHECK(simulated.state->origin().y == prior_origin.y);
        }
        state.emplace(std::move(*simulated.state));
    }
    CHECK(contacts > 0U);
}

TEST_CASE("Production BSP wall contact stays finite and nonpenetrating for 10000 commands",
    "[goldsrc][movement][wall-contact][stress][literal-bsp]")
{
    movement::WorldOnlyMovementCollision world{wall_package()};
    auto environment = fixture::make_environment();
    movement::GoldSrcLocalMovementScratch scratch;
    movement::GoldSrcLocalMovementConfig config;
    std::optional<player::LocalPlayerMovementState> state;
    state.emplace(fixture::make_state(
        {15.0F, 40.0F, 36.0F}, {240.0F, 0.0F, 0.0F}));
    std::uint64_t contacts = 0U;

    for (std::uint32_t sequence = 1U; sequence <= 10'000U; ++sequence) {
        auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
            *state,
            fixture::make_command(sequence, 10U, 240.0F),
            environment,
            world,
            scratch,
            config);
        INFO(sequence);
        REQUIRE(simulated);
        REQUIRE(simulated.state);
        contacts += simulated.statistics.collision_hit_count;
        CHECK(simulated.touches.size() <= config.maximum_touches_per_command);
        CHECK(std::isfinite(simulated.state->origin().x));
        CHECK(std::isfinite(simulated.state->origin().y));
        CHECK(std::isfinite(simulated.state->origin().z));
        const auto position = world.test_position(
            simulated.state->origin(), simulated.state->hull(),
            scratch.collision, config.collision_query);
        REQUIRE(position);
        REQUIRE(position.result);
        CHECK(position.result->status ==
            movement::LocalMovementPositionStatus::free);
        state.emplace(std::move(*simulated.state));
    }
    CHECK(contacts > 0U);
    CHECK(state->origin().x <= 16.0F);
    REQUIRE(scratch.last_diagnostic);
    CHECK(scratch.diagnostics.capacity() ==
        movement::kPlayerMovementDiagnosticCapacity);
}

TEST_CASE("Production wall campaigns cover release glancing parallel jump and duck",
    "[goldsrc][movement][wall-contact][campaigns][literal-bsp]")
{
    movement::WorldOnlyMovementCollision world{wall_package()};
    auto environment = fixture::make_environment();
    movement::GoldSrcLocalMovementConfig config;

    const auto run_campaign = [&](const float forward,
                                  const float side,
                                  const bool release_cycle,
                                  const bool jump,
                                  const bool duck_cycle,
                                  const float initial_speed) {
        movement::GoldSrcLocalMovementScratch scratch;
        std::optional<player::LocalPlayerMovementState> state;
        state.emplace(fixture::make_state(
            {15.0F, 40.0F, 36.0F}, {initial_speed, 0.0F, 0.0F}));
        ProductionCampaignSummary summary;
        summary.minimum_z = state->origin().z;
        summary.maximum_z = state->origin().z;
        for (std::uint32_t sequence = 1U; sequence <= 512U; ++sequence) {
            const bool released =
                release_cycle && sequence >= 192U && sequence < 256U;
            std::uint16_t buttons = 0U;
            if (jump && sequence == 1U) {
                buttons |= goldsrc::kSyntheticGoldSrcButtonJump;
            }
            if (duck_cycle &&
                ((sequence >= 1U && sequence <= 96U) ||
                    (sequence >= 193U && sequence <= 256U))) {
                buttons |= goldsrc::kSyntheticGoldSrcButtonDuck;
            }
            auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
                *state,
                fixture::make_command(sequence,
                    10U,
                    released ? 0.0F : forward,
                    released ? 0.0F : side,
                    buttons),
                environment,
                world,
                scratch,
                config);
            INFO(sequence);
            REQUIRE(simulated);
            REQUIRE(simulated.state);
            summary.collision_count +=
                simulated.statistics.collision_hit_count;
            summary.jump_count += simulated.statistics.jump_count;
            summary.duck_enter_count +=
                simulated.statistics.duck_enter_count;
            summary.duck_exit_count +=
                simulated.statistics.duck_exit_count;
            summary.start_solid_count +=
                simulated.statistics.start_solid_count;
            summary.all_solid_count +=
                simulated.statistics.all_solid_count;
            for (const auto& touch : simulated.touches) {
                const bool selected_wall =
                    touch.hit.kind == player::PlayerMovementHitKind::world &&
                    touch.hit.source_model_index == 0U &&
                    touch.plane.normal.x < -0.999F &&
                    std::abs(touch.plane.normal.y) < 1.0e-6F &&
                    std::abs(touch.plane.normal.z) < 1.0e-6F &&
                    std::abs(touch.plane.distance + 16.0) < 1.0e-4;
                if (!selected_wall) {
                    continue;
                }
                ++summary.selected_wall_contacts;
                if (sequence < 192U) {
                    ++summary.contacts_before_release;
                } else if (sequence < 256U) {
                    ++summary.contacts_during_release;
                } else {
                    ++summary.contacts_after_release;
                }
            }
            const auto position = world.test_position(
                simulated.state->origin(), simulated.state->hull(),
                scratch.collision, config.collision_query);
            REQUIRE(position);
            REQUIRE(position.result);
            summary.all_positions_free = summary.all_positions_free &&
                position.result->status ==
                    movement::LocalMovementPositionStatus::free;
            summary.minimum_z = std::min(
                summary.minimum_z, simulated.state->origin().z);
            summary.maximum_z = std::max(
                summary.maximum_z, simulated.state->origin().z);
            if (simulated.state->hull() ==
                player::PlayerMovementHull::ducked) {
                summary.saw_ducked = true;
            } else if (summary.saw_ducked) {
                summary.saw_standing_after_duck = true;
            }
            state.emplace(std::move(*simulated.state));
        }
        summary.signature =
            player::local_player_movement_state_signature(*state);
        summary.final_x = state->origin().x;
        summary.final_y = state->origin().y;
        summary.final_z = state->origin().z;
        summary.final_hull = state->hull();
        return summary;
    };

    SECTION("release and recontact")
    {
        const auto first =
            run_campaign(240.0F, 0.0F, true, false, false, 240.0F);
        const auto second =
            run_campaign(240.0F, 0.0F, true, false, false, 240.0F);
        CHECK(first == second);
        CHECK(first.signature != 0U);
        CHECK(first.contacts_before_release > 0U);
        CHECK(first.contacts_during_release == 0U);
        CHECK(first.contacts_after_release > 0U);
        CHECK(first.all_positions_free);
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
    }
    SECTION("glancing W plus small strafe")
    {
        const auto first =
            run_campaign(240.0F, 40.0F, false, false, false, 240.0F);
        const auto second =
            run_campaign(240.0F, 40.0F, false, false, false, 240.0F);
        CHECK(first == second);
        CHECK(first.selected_wall_contacts > 0U);
        CHECK(std::abs(first.final_y - 40.0F) > 10.0F);
        CHECK(first.all_positions_free);
    }
    SECTION("parallel movement")
    {
        const auto first =
            run_campaign(0.0F, 240.0F, false, false, false, 0.0F);
        const auto second =
            run_campaign(0.0F, 240.0F, false, false, false, 0.0F);
        CHECK(first == second);
        CHECK(std::abs(first.final_y - 40.0F) > 100.0F);
        CHECK(first.all_positions_free);
    }
    SECTION("W plus D")
    {
        const auto first =
            run_campaign(240.0F, 240.0F, false, false, false, 240.0F);
        const auto second =
            run_campaign(240.0F, 240.0F, false, false, false, 240.0F);
        CHECK(first == second);
        CHECK(first.selected_wall_contacts > 0U);
        CHECK(std::abs(first.final_y - 40.0F) > 100.0F);
        CHECK(first.all_positions_free);
    }
    SECTION("jump into wall")
    {
        const auto first =
            run_campaign(240.0F, 0.0F, false, true, false, 240.0F);
        const auto second =
            run_campaign(240.0F, 0.0F, false, true, false, 240.0F);
        CHECK(first == second);
        CHECK(first.selected_wall_contacts > 0U);
        CHECK(first.jump_count == 1U);
        CHECK(first.maximum_z > 36.0F);
        CHECK(first.final_z == Catch::Approx(36.0F));
        CHECK(first.all_positions_free);
    }
    SECTION("duck and stand near wall")
    {
        const auto first =
            run_campaign(240.0F, 0.0F, false, false, true, 240.0F);
        const auto second =
            run_campaign(240.0F, 0.0F, false, false, true, 240.0F);
        CHECK(first == second);
        CHECK(first.selected_wall_contacts > 0U);
        CHECK(first.duck_enter_count == 2U);
        CHECK(first.duck_exit_count == 2U);
        CHECK(first.saw_ducked);
        CHECK(first.saw_standing_after_duck);
        CHECK(first.final_hull == player::PlayerMovementHull::standing);
        CHECK(first.all_positions_free);
    }
    SECTION("high-speed contact")
    {
        const auto first =
            run_campaign(400.0F, 0.0F, false, false, false, 1'500.0F);
        const auto second =
            run_campaign(400.0F, 0.0F, false, false, false, 1'500.0F);
        CHECK(first == second);
        CHECK(first.selected_wall_contacts > 0U);
        CHECK(first.collision_count > 0U);
        CHECK(first.final_x <= 16.0F);
        CHECK(first.all_positions_free);
        CHECK(first.start_solid_count == 0U);
        CHECK(first.all_solid_count == 0U);
    }
}

TEST_CASE("Zero and near-zero repeated wall contacts stop or slide successfully",
    "[goldsrc][movement][wall-contact][zero-progress][dedup]")
{
    const std::array planes{
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
    };
    for (const double fraction : {0.0, 1.0e-13}) {
        INFO(fraction);
        ScriptedContactCollision collision_source{fraction, planes, 4U};
        const auto result = fixture::simulate(
            fixture::make_state(
                {0.0F, 0.0F, 100.0F}, {100.0F, 50.0F, 0.0F},
                player::PlayerMovementMode::airborne),
            fixture::make_command(1U), collision_source);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(result.statistics.clip_plane_count == 1U);
        CHECK(result.touches.size() == 1U);
        CHECK(std::abs(result.state->origin().x) <= 1.0e-6F);
        CHECK(result.state->velocity().x == 0.0F);
        CHECK(result.state->velocity().y > 0.0F);
    }
}

TEST_CASE("Opposing planes form a bounded successful trap",
    "[goldsrc][movement][wall-contact][opposing][trap]")
{
    const std::array planes{
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{1.0F, 0.0F, 0.0F},
    };
    ScriptedContactCollision collision_source{0.0, planes, 4U};
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 50.0F, 0.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision_source);
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.clip_plane_count == 1U);
    CHECK(result.state->velocity().x == 0.0F);
    CHECK(result.state->velocity().y == 0.0F);
}

TEST_CASE("Near-identical planes do not consume distinct-plane capacity",
    "[goldsrc][movement][wall-contact][dedup][near]")
{
    const std::array planes{
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, -9.0e-5F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, -9.0e-5F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, -9.0e-5F, 0.0F},
    };
    ScriptedContactCollision collision_source{0.0, planes, 4U};
    movement::GoldSrcLocalMovementConfig config;
    config.maximum_clip_planes = 1U;
    config.maximum_slide_bumps = 2U;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 1'500.0F, 0.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision_source, config);
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.statistics.clip_plane_count == 1U);
    CHECK(result.touches.size() == 1U);
    CHECK(result.state->velocity().y > 1'000.0F);
    CHECK(movement::movement_dot(
        result.state->velocity(), planes[1U]) >= 0.0F);
}

TEST_CASE("A genuinely distinct plane still enforces the configured limit",
    "[goldsrc][movement][wall-contact][dedup][distinct-limit]")
{
    const std::array planes{
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{0.0F, -1.0F, 0.0F},
        hlclient::assets::AssetVector3{0.0F, 0.0F, -1.0F},
        hlclient::assets::AssetVector3{0.0F, 0.0F, -1.0F},
    };
    ScriptedContactCollision collision_source{0.0, planes, 4U};
    movement::GoldSrcLocalMovementConfig config;
    config.maximum_clip_planes = 1U;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 50.0F, 50.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision_source, config);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == movement::LocalMovementSimulationErrorCode::
        clip_plane_limit_exceeded);
}

TEST_CASE("A discarded grounded step candidate cannot spend the direct touch budget",
    "[goldsrc][movement][wall-contact][step][transactional][touch-limit]")
{
    fixture::DeterministicLocalMovementCollision collision_source;
    collision_source.add_positive_x_wall(20.0F);
    movement::GoldSrcLocalMovementConfig config;
    config.maximum_touches_per_command = 1U;

    const auto previous = fixture::make_state(
        {0.0F, 0.0F, 36.0F}, {1'000.0F, 0.0F, 0.0F});
    auto environment = fixture::make_environment();
    movement::GoldSrcLocalMovementScratch scratch;
    const auto result = movement::GoldSrcLocalMovementKernel::simulate(
        previous, fixture::make_command(1U), environment, collision_source,
        scratch, config);

    REQUIRE(result);
    REQUIRE(result.state);
    REQUIRE(result.touches.size() == 1U);
    CHECK(result.touches.front().phase ==
        player::PlayerMovementPhase::direct_slide);
    CHECK(result.statistics.step_attempt_count == 1U);
    CHECK(result.statistics.step_success_count == 0U);
    CHECK(result.state->origin().x == Catch::Approx(4.0F).margin(1.0e-4F));
    REQUIRE(scratch.last_diagnostic);
    CHECK(scratch.last_diagnostic->phase ==
        player::PlayerMovementPhase::direct_slide);
    CHECK(scratch.last_diagnostic->result ==
        movement::PlayerMovementDiagnosticResult::success);
    CHECK(scratch.last_diagnostic->state_signature_before ==
        player::local_player_movement_state_signature(previous));
    CHECK(scratch.last_diagnostic->state_signature_after ==
        result.deterministic_state_signature);
}

TEST_CASE("Touch limits return a typed transactional failure",
    "[goldsrc][movement][wall-contact][touch-limit]")
{
    const std::array planes{
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{0.0F, -1.0F, 0.0F},
        hlclient::assets::AssetVector3{0.0F, 0.0F, -1.0F},
        hlclient::assets::AssetVector3{0.0F, 0.0F, -1.0F},
    };
    ScriptedContactCollision collision_source{0.0, planes, 4U};
    movement::GoldSrcLocalMovementConfig config;
    config.maximum_touches_per_command = 1U;
    const auto result = fixture::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 50.0F, 50.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U), collision_source, config);
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        movement::LocalMovementSimulationErrorCode::touch_limit_exceeded);
}

TEST_CASE("Typed trace failures retain the last bounded contact evidence",
    "[goldsrc][movement][wall-contact][diagnostics][typed-failure]")
{
    const std::array planes{
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
    };
    ScriptedContactCollision collision_source{0.0, planes, 4U, 1U};
    const auto previous = fixture::make_state(
        {0.0F, 0.0F, 100.0F}, {100.0F, 50.0F, 0.0F},
        player::PlayerMovementMode::airborne);
    auto environment = fixture::make_environment();
    movement::GoldSrcLocalMovementScratch scratch;

    const auto result = movement::GoldSrcLocalMovementKernel::simulate(
        previous, fixture::make_command(1U), environment, collision_source,
        scratch);

    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        movement::LocalMovementSimulationErrorCode::collision_query_failed);
    REQUIRE(scratch.last_diagnostic);
    CHECK(scratch.last_diagnostic->phase ==
        player::PlayerMovementPhase::airborne_slide);
    CHECK(scratch.last_diagnostic->fraction_class ==
        movement::PlayerMovementTraceFractionClass::zero);
    CHECK(scratch.last_diagnostic->clip_plane_count == 1U);
    CHECK(scratch.last_diagnostic->distinct_plane_count == 1U);
    CHECK(scratch.last_diagnostic->hit_kind ==
        movement::PlayerMovementDiagnosticHitKind::
            explicit_synthetic_brush);
    CHECK(scratch.last_diagnostic->result ==
        movement::PlayerMovementDiagnosticResult::collision_failure);
    CHECK(scratch.last_diagnostic->collision_result ==
        movement::PlayerMovementCollisionResultClass::typed_failure);
    const auto before_signature =
        player::local_player_movement_state_signature(previous);
    CHECK(scratch.last_diagnostic->state_signature_before == before_signature);
    CHECK(scratch.last_diagnostic->state_signature_after == before_signature);
}

TEST_CASE("Movement diagnostics retain hit kind and caller runtime ordinals",
    "[goldsrc][movement][wall-contact][diagnostics][bounded]")
{
    movement::PlayerMovementDiagnosticRing ring;
    movement::PlayerWallContactDiagnosticFrame frame;
    frame.frame_ordinal = 41U;
    frame.hit_present = true;
    frame.hit_kind = movement::PlayerMovementDiagnosticHitKind::world;
    ring.push(frame);

    REQUIRE(ring.latest());
    CHECK(ring.latest()->diagnostic_ordinal == 0U);
    CHECK(ring.latest()->frame_ordinal == 41U);
    CHECK(ring.latest()->hit_kind ==
        movement::PlayerMovementDiagnosticHitKind::world);

    auto enriched = *ring.latest();
    movement::apply_player_movement_diagnostic_runtime_context(
        enriched,
        movement::PlayerMovementDiagnosticRuntimeContext{
            99U,
            static_cast<std::size_t>(UINT16_MAX) + 1U,
            17U,
            23U});
    CHECK(enriched.diagnostic_ordinal == 0U);
    CHECK(enriched.frame_ordinal == 99U);
    CHECK(enriched.generated_command_count == UINT16_MAX);
    CHECK(enriched.camera_revision == 17U);
    CHECK(enriched.visibility_revision == 23U);

    const std::array planes{
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
        hlclient::assets::AssetVector3{-1.0F, 0.0F, 0.0F},
    };
    ScriptedContactCollision collision_source{0.0, planes, 4U};
    movement::GoldSrcLocalMovementScratch scratch;
    auto environment = fixture::make_environment();
    const auto simulated = movement::GoldSrcLocalMovementKernel::simulate(
        fixture::make_state(
            {0.0F, 0.0F, 100.0F}, {100.0F, 50.0F, 0.0F},
            player::PlayerMovementMode::airborne),
        fixture::make_command(1U),
        environment,
        collision_source,
        scratch);
    REQUIRE(simulated);
    REQUIRE(scratch.last_diagnostic);
    CHECK(scratch.last_diagnostic->hit_present);
    CHECK(scratch.last_diagnostic->hit_kind == movement::
        PlayerMovementDiagnosticHitKind::explicit_synthetic_brush);
}

} // namespace

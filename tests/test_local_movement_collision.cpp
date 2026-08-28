#include <hlclient/goldsrc/movement/local_movement_collision.hpp>

#include "collision_brush_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush_models = hlclient::goldsrc::brush_models;
namespace collision = hlclient::collision;
namespace fixture = hlclient::tests::collision_brush_fixture;
namespace movement = hlclient::movement;
namespace movement_collision = hlclient::goldsrc::movement;
namespace scene_collision = hlclient::goldsrc::collision;

[[nodiscard]] brush_models::BrushRigidTransform rigid(
    const assets::AssetVector3 origin)
{
    const auto built = brush_models::make_brush_rigid_transform(origin, {});
    REQUIRE(built);
    REQUIRE(built.transform);
    return *built.transform;
}

[[nodiscard]] std::shared_ptr<const scene_collision::BrushCollisionScene>
single_solid_brush_scene()
{
    auto package = fixture::package(false);
    const auto library =
        scene_collision::build_brush_collision_model_library(package);
    REQUIRE(library);
    REQUIRE(library.library);

    const scene_collision::BrushCollisionInstanceIdentity identity{
        7U, 1U, 42U};
    const std::array definitions{
        scene_collision::BrushCollisionInstanceDefinition{
            identity, rigid({10.0F, 0.0F, 0.0F})},
    };
    const scene_collision::ExplicitSyntheticBrushCollisionRoleProvider provider{
        std::vector<scene_collision::SyntheticBrushCollisionRoleBinding>{
            {identity, scene_collision::BrushCollisionRole::solid},
        }};
    const auto built = scene_collision::build_brush_collision_scene(
        library.library, definitions, provider);
    REQUIRE(built);
    REQUIRE(built.scene);
    return built.scene;
}

[[nodiscard]] movement_collision::LocalMovementCollisionQueryConfig
invalid_query_config()
{
    movement_collision::LocalMovementCollisionQueryConfig config;
    config.query_limits.maximum_traversal_steps = 0U;
    return config;
}

[[nodiscard]] collision::CollisionContents decoded(const std::int32_t raw)
{
    const auto value = collision::decode_goldsrc_contents({raw});
    REQUIRE(value);
    return *value;
}

} // namespace

TEST_CASE("Local movement contents normalization is exact and exhaustive",
    "[goldsrc][movement][collision][contents]")
{
    struct Case {
        std::int32_t raw;
        movement::PlayerMovementContents expected;
    };
    constexpr std::array cases{
        Case{-1, movement::PlayerMovementContents::empty},
        Case{-2, movement::PlayerMovementContents::solid},
        Case{-3, movement::PlayerMovementContents::water},
        Case{-4, movement::PlayerMovementContents::slime},
        Case{-5, movement::PlayerMovementContents::lava},
        Case{-6, movement::PlayerMovementContents::sky},
        Case{-7, movement::PlayerMovementContents::special},
        Case{-8, movement::PlayerMovementContents::special},
        Case{-9, movement::PlayerMovementContents::current},
        Case{-10, movement::PlayerMovementContents::current},
        Case{-11, movement::PlayerMovementContents::current},
        Case{-12, movement::PlayerMovementContents::current},
        Case{-13, movement::PlayerMovementContents::current},
        Case{-14, movement::PlayerMovementContents::current},
        Case{-15, movement::PlayerMovementContents::special},
    };

    for (const auto& test : cases) {
        INFO("GoldSrc contents=" << test.raw);
        const auto normalized =
            movement_collision::normalize_local_movement_contents(
                decoded(test.raw));
        REQUIRE(normalized);
        CHECK(normalized->category == test.expected);
        CHECK(normalized->source_goldsrc_code == test.raw);
    }

    const collision::CollisionContents invalid{
        {-999}, static_cast<collision::CollisionContentsCategory>(255U)};
    CHECK_FALSE(
        movement_collision::normalize_local_movement_contents(invalid));
}

TEST_CASE("Local movement hull mapping uses exact compiler hull ordinals",
    "[goldsrc][movement][collision][hull]")
{
    CHECK(movement_collision::local_movement_collision_hull(
              movement::PlayerMovementHull::standing) ==
        collision::CollisionHullOrdinal::standing_32x32x72);
    CHECK(movement_collision::local_movement_collision_hull(
              movement::PlayerMovementHull::ducked) ==
        collision::CollisionHullOrdinal::duck_32x32x36);
    CHECK_FALSE(movement_collision::local_movement_collision_hull(
        static_cast<movement::PlayerMovementHull>(255U)));
}

TEST_CASE("World-only movement collision preserves world query evidence",
    "[goldsrc][movement][collision][world]")
{
    const auto package = fixture::package(true, 0.0);
    const movement_collision::WorldOnlyMovementCollision adapter{package};
    collision::CollisionQueryScratch scratch;

    REQUIRE(adapter.valid());
    CHECK(adapter.package() == package);
    CHECK(adapter.profile() ==
        movement_collision::LocalMovementCollisionProfile::world_only_v1);

    const auto open = adapter.point_contents({1.0F, 0.0F, 0.0F}, scratch);
    REQUIRE(open);
    REQUIRE(open.result);
    CHECK_FALSE(open.error);
    CHECK(open.result->contents.category ==
        movement::PlayerMovementContents::empty);
    CHECK(open.result->contents.source_goldsrc_code == -1);
    CHECK(open.result->traversal_depth == 1U);

    const auto solid = adapter.point_contents({-1.0F, 0.0F, 0.0F}, scratch);
    REQUIRE(solid);
    CHECK(solid.result->contents.category ==
        movement::PlayerMovementContents::solid);
    CHECK(solid.result->contents.source_goldsrc_code == -2);

    const auto standing_free = adapter.test_position(
        {1.0F, 0.0F, 0.0F}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE(standing_free);
    CHECK(standing_free.result->status ==
        movement_collision::LocalMovementPositionStatus::free);
    CHECK_FALSE(standing_free.result->hit);

    const auto ducked_blocked = adapter.test_position(
        {-1.0F, 0.0F, 0.0F}, movement::PlayerMovementHull::ducked, scratch);
    REQUIRE(ducked_blocked);
    CHECK(ducked_blocked.result->status ==
        movement_collision::LocalMovementPositionStatus::blocking);
    CHECK(ducked_blocked.result->contents.category ==
        movement::PlayerMovementContents::solid);

    const auto traced = adapter.trace_hull(
        {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F},
        movement::PlayerMovementHull::standing, scratch);
    REQUIRE(traced);
    REQUIRE(traced.result);
    CHECK(traced.result->collision_profile ==
        movement_collision::LocalMovementCollisionProfile::world_only_v1);
    CHECK_FALSE(traced.result->all_solid);
    CHECK_FALSE(traced.result->start_solid);
    CHECK(traced.result->in_open);
    CHECK_FALSE(traced.result->in_liquid);
    CHECK(traced.result->fraction == Catch::Approx(0.5));
    CHECK(traced.result->end_position.x == Catch::Approx(0.0F));
    REQUIRE(traced.result->collision_plane);
    CHECK(traced.result->collision_plane->normal.x == 1.0F);
    CHECK(traced.result->collision_plane->distance == 0.0);
    REQUIRE(traced.result->collision_plane->source_plane_index);
    CHECK(*traced.result->collision_plane->source_plane_index == 10U);
    REQUIRE(traced.result->hit);
    CHECK(traced.result->hit->kind ==
        movement::PlayerMovementHitKind::world);
    CHECK(traced.result->hit->source_model_index == 0U);
    CHECK_FALSE(traced.result->hit->stable_instance_ordinal);
    CHECK_FALSE(traced.result->hit->source_entity_index);
    CHECK(traced.result->start_contents.category ==
        movement::PlayerMovementContents::empty);
    CHECK(traced.result->end_contents.category ==
        movement::PlayerMovementContents::solid);
    REQUIRE(traced.result->blocking_contents);
    CHECK(traced.result->blocking_contents->category ==
        movement::PlayerMovementContents::solid);
    CHECK(traced.result->traversal_statistics.traversal_steps > 0U);
    CHECK(traced.result->traversal_statistics.terminal_interval_count > 0U);
}

TEST_CASE("World-only movement collision rejects malformed requests before query",
    "[goldsrc][movement][collision][world][security]")
{
    const movement_collision::WorldOnlyMovementCollision null_adapter{nullptr};
    collision::CollisionQueryScratch scratch;
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    CHECK_FALSE(null_adapter.valid());

    auto result = null_adapter.point_contents({}, scratch);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);
    CHECK_FALSE(result.result);

    result = null_adapter.point_contents(
        {}, scratch, invalid_query_config());
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_configuration);

    result = null_adapter.point_contents({nan, 0.0F, 0.0F}, scratch);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_point);

    const auto invalid_position = null_adapter.test_position(
        {}, static_cast<movement::PlayerMovementHull>(255U), scratch);
    REQUIRE_FALSE(invalid_position);
    REQUIRE(invalid_position.error);
    CHECK(invalid_position.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_hull);

    const auto invalid_segment = null_adapter.trace_hull(
        {nan, 0.0F, 0.0F}, {}, movement::PlayerMovementHull::standing,
        scratch);
    REQUIRE_FALSE(invalid_segment);
    REQUIRE(invalid_segment.error);
    CHECK(invalid_segment.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_segment);

    const auto invalid_hull = null_adapter.trace_hull(
        {}, {}, static_cast<movement::PlayerMovementHull>(255U), scratch);
    REQUIRE_FALSE(invalid_hull);
    REQUIRE(invalid_hull.error);
    CHECK(invalid_hull.error->code ==
        movement_collision::LocalMovementCollisionErrorCode::invalid_hull);

    const auto empty_package =
        std::make_shared<const collision::CollisionWorldPackage>();
    const movement_collision::WorldOnlyMovementCollision malformed{
        empty_package};
    CHECK_FALSE(malformed.valid());
    const auto rejected = malformed.trace_hull(
        {}, {}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);
}

TEST_CASE("Synthetic movement collision publishes stable brush identity",
    "[goldsrc][movement][collision][synthetic]")
{
    const auto scene = single_solid_brush_scene();
    const movement_collision::SyntheticBrushMovementCollision adapter{scene};
    collision::CollisionQueryScratch scratch;

    REQUIRE(adapter.valid());
    CHECK(adapter.scene() == scene);
    CHECK(adapter.profile() == movement_collision::
        LocalMovementCollisionProfile::explicit_synthetic_static_brush_v1);

    const auto traced = adapter.trace_hull(
        {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F},
        movement::PlayerMovementHull::ducked, scratch);
    REQUIRE(traced);
    REQUIRE(traced.result);
    CHECK(traced.result->collision_profile == movement_collision::
        LocalMovementCollisionProfile::explicit_synthetic_static_brush_v1);
    CHECK(traced.result->fraction == Catch::Approx(0.25));
    REQUIRE(traced.result->hit);
    CHECK(traced.result->hit->kind ==
        movement::PlayerMovementHitKind::explicit_synthetic_brush);
    CHECK(traced.result->hit->source_model_index == 1U);
    REQUIRE(traced.result->hit->stable_instance_ordinal);
    CHECK(*traced.result->hit->stable_instance_ordinal == 7U);
    REQUIRE(traced.result->hit->source_entity_index);
    CHECK(*traced.result->hit->source_entity_index == 42U);

    // Synthetic brushes participate in traces, but never masquerade as world
    // point-contents providers.
    const auto contents =
        adapter.point_contents({9.0F, 0.0F, 0.0F}, scratch);
    REQUIRE(contents);
    CHECK(contents.result->contents.category ==
        movement::PlayerMovementContents::empty);
}

TEST_CASE("Synthetic movement collision fails closed without explicit roles",
    "[goldsrc][movement][collision][synthetic][security]")
{
    collision::CollisionQueryScratch scratch;
    const movement_collision::SyntheticBrushMovementCollision null_adapter{
        nullptr};
    CHECK_FALSE(null_adapter.valid());

    const auto point = null_adapter.point_contents({}, scratch);
    REQUIRE_FALSE(point);
    REQUIRE(point.error);
    CHECK(point.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);

    const auto traced = null_adapter.trace_hull(
        {}, {}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE_FALSE(traced);
    REQUIRE(traced.error);
    CHECK(traced.error->code == movement_collision::
        LocalMovementCollisionErrorCode::invalid_collision_source);

    const scene_collision::StockBrushCollisionRoleProvider stock_provider;
    const auto library = scene_collision::build_brush_collision_model_library(
        fixture::package(false));
    REQUIRE(library);
    const std::array definitions{
        scene_collision::BrushCollisionInstanceDefinition{
            {1U, 1U, std::nullopt}, rigid({10.0F, 0.0F, 0.0F})},
    };
    const auto stock_scene = scene_collision::build_brush_collision_scene(
        library.library, definitions, stock_provider);
    REQUIRE(stock_scene);
    const movement_collision::SyntheticBrushMovementCollision unsupported{
        stock_scene.scene};
    CHECK_FALSE(unsupported.valid());
    const auto rejected = unsupported.trace_hull(
        {}, {}, movement::PlayerMovementHull::standing, scratch);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == movement_collision::
        LocalMovementCollisionErrorCode::unsupported_collision_profile);
}

TEST_CASE("Local movement collision configuration and labels are stable",
    "[goldsrc][movement][collision][profile]")
{
    CHECK(movement_collision::valid_local_movement_collision_query_config({}));
    CHECK_FALSE(movement_collision::valid_local_movement_collision_query_config(
        invalid_query_config()));
    CHECK(movement_collision::to_string(
              movement_collision::LocalMovementCollisionProfile::world_only_v1) ==
        "world_only_v1");
    CHECK(movement_collision::to_string(
              movement_collision::LocalMovementCollisionProfile::
                  explicit_synthetic_static_brush_v1) ==
        "explicit_synthetic_static_brush_v1");
    CHECK(movement_collision::to_string(
              movement_collision::LocalMovementCollisionErrorCode::
                  invalid_collision_source) ==
        "invalid_collision_source");
    CHECK(movement_collision::to_string(
              static_cast<movement_collision::LocalMovementCollisionProfile>(
                  255U)) ==
        "unknown");
}

#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>

#include "collision_brush_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <limits>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;
namespace collision = hlclient::collision;
namespace fixture = hlclient::tests::collision_brush_fixture;
namespace scene = hlclient::goldsrc::collision;

constexpr float kMargin = 1.0e-5F;

void check_vector(
    const assets::AssetVector3& actual,
    const assets::AssetVector3& expected)
{
    CHECK(actual.x == Catch::Approx(expected.x).margin(kMargin));
    CHECK(actual.y == Catch::Approx(expected.y).margin(kMargin));
    CHECK(actual.z == Catch::Approx(expected.z).margin(kMargin));
}

[[nodiscard]] std::shared_ptr<const scene::BrushCollisionModelLibrary>
library()
{
    const auto built = scene::build_brush_collision_model_library(
        fixture::package(false));
    REQUIRE(built);
    return built.library;
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
rounding_edge_package()
{
    const auto quantum =
        static_cast<double>((std::numeric_limits<float>::denorm_min)());
    std::vector<collision::CollisionNode> nodes;
    std::vector<collision::CollisionClipnode> clipnodes;
    nodes.reserve(7U);
    clipnodes.reserve(7U);
    nodes.push_back(collision::CollisionNode{
        0U,
        {collision::CollisionNodeChild{
             collision::CollisionNodeChildKind::leaf, 1U},
            collision::CollisionNodeChild{
                collision::CollisionNodeChildKind::leaf, 1U}},
    });
    clipnodes.push_back(collision::CollisionClipnode{
        0U,
        {collision::CollisionClipnodeChild{
             collision::CollisionClipnodeChildKind::terminal,
             0U,
             fixture::contents(-1)},
            collision::CollisionClipnodeChild{
                collision::CollisionClipnodeChildKind::terminal,
                0U,
                fixture::contents(-1)}},
    });

    // Retain 0 <= x,y < denorm_min and -1 <= z < 1. The proof publisher
    // widens the two tiny axes to [-denorm_min, 2 * denorm_min].
    constexpr std::array upper_plane{
        false, true, false, true, true, false};
    for (std::uint32_t index = 1U; index <= upper_plane.size(); ++index) {
        const auto upper = upper_plane[index - 1U];
        const auto final_plane = index == upper_plane.size();
        const collision::CollisionNodeChild empty_leaf{
            collision::CollisionNodeChildKind::leaf, 1U};
        const collision::CollisionNodeChild next_or_solid{
            final_plane ? collision::CollisionNodeChildKind::leaf
                        : collision::CollisionNodeChildKind::node,
            final_plane ? 0U : index + 1U,
        };
        nodes.push_back(collision::CollisionNode{
            index,
            upper ? std::array{empty_leaf, next_or_solid}
                  : std::array{next_or_solid, empty_leaf},
        });

        const collision::CollisionClipnodeChild empty_terminal{
            collision::CollisionClipnodeChildKind::terminal,
            0U,
            fixture::contents(-1),
        };
        const collision::CollisionClipnodeChild next_or_solid_terminal{
            final_plane
                ? collision::CollisionClipnodeChildKind::terminal
                : collision::CollisionClipnodeChildKind::clipnode,
            final_plane ? 0U : index + 1U,
            final_plane ? fixture::contents(-2) : fixture::contents(-1),
        };
        clipnodes.push_back(collision::CollisionClipnode{
            index,
            upper ? std::array{empty_terminal, next_or_solid_terminal}
                  : std::array{next_or_solid_terminal, empty_terminal},
        });
    }

    return std::make_shared<const collision::CollisionWorldPackage>(
        std::vector<collision::CollisionPlane>{
            {{1.0F, 0.0F, 0.0F}, 0.0, 10U, 0},
            {{1.0F, 0.0F, 0.0F}, 0.0, 20U, 0},
            {{1.0F, 0.0F, 0.0F}, quantum, 21U, 0},
            {{0.0F, 1.0F, 0.0F}, 0.0, 22U, 1},
            {{0.0F, 1.0F, 0.0F}, quantum, 23U, 1},
            {{0.0F, 0.0F, 1.0F}, 1.0, 24U, 2},
            {{0.0F, 0.0F, 1.0F}, -1.0, 25U, 2},
        },
        std::move(nodes),
        std::vector<collision::CollisionLeaf>{
            {0U, fixture::contents(-2)},
            {1U, fixture::contents(-1)},
        },
        std::move(clipnodes),
        std::vector<collision::CollisionModel>{
            fixture::model(0U, 0U, 0U),
            fixture::model(1U, 1U, 1U),
        },
        collision::CollisionWorldIdentity{
            assets::AssetSourceFingerprint{0xABCDU, 0x9876U}, 30U});
}

[[nodiscard]] scene::ExplicitBrushCollisionTraceQueryResult trace(
    const scene::BrushCollisionModel& model,
    const brush::BrushRigidTransform& transform,
    const assets::AssetVector3 start,
    const assets::AssetVector3 end,
    const collision::CollisionHullOrdinal hull =
        collision::CollisionHullOrdinal::point)
{
    collision::CollisionQueryScratch scratch;
    return scene::trace_explicit_brush_model(
        model,
        transform,
        scene::ExplicitBrushCollisionTraceRequest{start, end, hull},
        scratch);
}

[[nodiscard]] scene::ExplicitBrushCollisionTraceQueryResult trace_request(
    const scene::BrushCollisionModel& model,
    const brush::BrushRigidTransform& transform,
    const scene::ExplicitBrushCollisionTraceRequest& request)
{
    collision::CollisionQueryScratch scratch;
    return scene::trace_explicit_brush_model(
        model, transform, request, scratch);
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
package_with_direct_solid_brush_hull()
{
    const auto source = fixture::package(false);
    std::vector<collision::CollisionModel> models{
        source->models().begin(), source->models().end()};
    REQUIRE(models.size() > 1U);
    models[1U].hulls[1U].root = collision::CollisionHullRoot{
        collision::CollisionHullRootKind::terminal,
        0U,
        fixture::contents(-2),
    };
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::vector<collision::CollisionPlane>{
            source->planes().begin(), source->planes().end()},
        std::vector<collision::CollisionNode>{
            source->nodes().begin(), source->nodes().end()},
        std::vector<collision::CollisionLeaf>{
            source->leaves().begin(), source->leaves().end()},
        std::vector<collision::CollisionClipnode>{
            source->clipnodes().begin(), source->clipnodes().end()},
        std::move(models),
        source->identity(),
        source->statistics(),
        source->compatibility_profile(),
        source->evidence_profile());
}

TEST_CASE("Brush collision model library retains models one through N",
    "[collision][brush-model][library]")
{
    const auto models = library();
    REQUIRE(models->models().size() == 2U);
    CHECK(models->model(0U) == nullptr);
    REQUIRE(models->model(1U) != nullptr);
    REQUIRE(models->model(2U) != nullptr);
    CHECK(models->model(3U) == nullptr);
    CHECK(models->model(1U)->hull_roots.size() ==
        collision::kCollisionHullCount);
    CHECK(models->model(1U)->collision_identity.source_revision == 30U);
    CHECK(models->model(1U)->collision_world == models->collision_world());
}

TEST_CASE("Explicit brush trace preserves identity translation and fraction",
    "[collision][brush-model][trace][translation]")
{
    const auto models = library();
    const auto* model = models->model(1U);
    REQUIRE(model != nullptr);

    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);
    const auto local = trace(
        *model, *identity.transform, {2.0F, 0.0F, 0.0F}, {-2.0F, 0.0F, 0.0F});
    REQUIRE(local);
    CHECK_FALSE(local.result->broad_phase_rejected);
    CHECK(local.result->trace.fraction == Catch::Approx(0.5));
    check_vector(local.result->trace.end_position, {0.0F, 0.0F, 0.0F});
    REQUIRE(local.result->trace.hit);
    CHECK(local.result->trace.hit->kind ==
        collision::CollisionTraceHitKind::collision_model);
    CHECK(local.result->trace.hit->source_model_index == 1U);

    const auto translated = brush::make_brush_rigid_transform(
        {10.0F, -3.0F, 4.0F}, {});
    REQUIRE(translated);
    const auto moved = trace(*model,
        *translated.transform,
        {12.0F, -3.0F, 4.0F},
        {8.0F, -3.0F, 4.0F});
    REQUIRE(moved);
    CHECK(moved.result->trace.fraction == Catch::Approx(0.5));
    check_vector(moved.result->trace.end_position, {10.0F, -3.0F, 4.0F});
    REQUIRE(moved.result->trace.collision_plane);
    check_vector(moved.result->trace.collision_plane->normal,
        {1.0F, 0.0F, 0.0F});
    CHECK(moved.result->trace.collision_plane->distance ==
        Catch::Approx(10.0));
}

TEST_CASE("Rotated explicit brush trace transforms plane into world space",
    "[collision][brush-model][trace][rotation][plane]")
{
    const auto models = library();
    const auto* model = models->model(1U);
    REQUIRE(model != nullptr);
    const auto rotated = brush::make_brush_rigid_transform(
        {10.0F, 20.0F, 3.0F}, {0.0F, 90.0F, 0.0F});
    REQUIRE(rotated);

    const auto result = trace(*model,
        *rotated.transform,
        {10.0F, 22.0F, 3.0F},
        {10.0F, 18.0F, 3.0F});
    REQUIRE(result);
    CHECK(result.result->trace.fraction == Catch::Approx(0.5));
    check_vector(result.result->trace.end_position, {10.0F, 20.0F, 3.0F});
    REQUIRE(result.result->trace.collision_plane);
    check_vector(result.result->trace.collision_plane->normal,
        {0.0F, 1.0F, 0.0F});
    CHECK(result.result->trace.collision_plane->distance ==
        Catch::Approx(20.0).margin(1.0e-5));
    CHECK(result.result->trace.collision_plane->source_plane_index == 20U);
}

TEST_CASE("Reverse-direction explicit brush trace inverts its source plane",
    "[collision][brush-model][trace][reverse][plane]")
{
    const auto built = scene::build_brush_collision_model_library(
        fixture::package(false, 0.0, 2U, true));
    REQUIRE(built);
    const auto* model = built.library->model(1U);
    REQUIRE(model != nullptr);
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);

    const auto reversed = trace(*model,
        *identity.transform,
        {-2.0F, 0.0F, 0.0F},
        {2.0F, 0.0F, 0.0F});
    REQUIRE(reversed);
    CHECK_FALSE(reversed.result->trace.start_solid);
    CHECK_FALSE(reversed.result->trace.all_solid);
    CHECK(reversed.result->trace.fraction == Catch::Approx(0.5));
    check_vector(reversed.result->trace.end_position, {0.0F, 0.0F, 0.0F});
    REQUIRE(reversed.result->trace.hit);
    CHECK(reversed.result->trace.hit->kind ==
        collision::CollisionTraceHitKind::collision_model);
    CHECK(reversed.result->trace.hit->source_model_index == 1U);
    REQUIRE(reversed.result->trace.collision_plane);
    check_vector(reversed.result->trace.collision_plane->normal,
        {-1.0F, 0.0F, 0.0F});
    CHECK(reversed.result->trace.collision_plane->distance ==
        Catch::Approx(0.0));
    CHECK(reversed.result->trace.collision_plane->source_plane_index == 20U);
    CHECK(reversed.result->trace.collision_plane->orientation ==
        collision::CollisionPlaneOrientation::inverted_source);
}

TEST_CASE("Tree-proven broad phase rejects misses and retains hull contacts",
    "[collision][brush-model][trace][broad-phase][hull]")
{
    const auto built = scene::build_brush_collision_model_library(
        fixture::bounded_brush_package());
    REQUIRE(built);
    const auto* model = built.library->model(1U);
    REQUIRE(model != nullptr);
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);

    const auto miss = trace(
        *model, *identity.transform, {2.0F, 100.0F, 0.0F},
        {-2.0F, 100.0F, 0.0F});
    REQUIRE(miss);
    CHECK(miss.result->broad_phase_rejected);
    CHECK(miss.result->trace.fraction == 1.0);
    CHECK_FALSE(miss.result->trace.hit);
    CHECK(miss.result->trace.in_open);
    CHECK(miss.result->trace.start_contents.category ==
        collision::CollisionContentsCategory::empty);
    CHECK(miss.result->trace.end_contents.category ==
        collision::CollisionContentsCategory::empty);
    CHECK(miss.result->expanded_world_bounds.minimum.x < -1.0F);
    CHECK(miss.result->expanded_world_bounds.maximum.x > 1.0F);
    CHECK(miss.result->expanded_world_bounds.minimum.y < -1.0F);
    CHECK(miss.result->expanded_world_bounds.maximum.y > 1.0F);

    const auto standing = trace(*model,
        *identity.transform,
        {2.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F},
        collision::CollisionHullOrdinal::standing_32x32x72);
    REQUIRE(standing);
    CHECK_FALSE(standing.result->broad_phase_rejected);
    CHECK(standing.result->trace.fraction == Catch::Approx(0.5));
    REQUIRE(standing.result->trace.hit);

    const auto rotated = brush::make_brush_rigid_transform(
        {10.0F, 20.0F, 0.0F}, {0.0F, 90.0F, 0.0F});
    REQUIRE(rotated);
    const auto rotated_contact = trace(*model,
        *rotated.transform,
        {10.0F, 22.0F, 0.0F},
        {10.0F, 20.0F, 0.0F});
    REQUIRE(rotated_contact);
    CHECK_FALSE(rotated_contact.result->broad_phase_rejected);
    CHECK(rotated_contact.result->trace.fraction == Catch::Approx(0.5));
    REQUIRE(rotated_contact.result->trace.hit);
    CHECK(rotated_contact.result->expanded_world_bounds.minimum.x < 9.0F);
    CHECK(rotated_contact.result->expanded_world_bounds.maximum.x > 11.0F);
    CHECK(rotated_contact.result->expanded_world_bounds.minimum.y < 19.0F);
    CHECK(rotated_contact.result->expanded_world_bounds.maximum.y > 21.0F);

    const auto rotated_miss = trace(*model,
        *rotated.transform,
        {100.0F, 22.0F, 0.0F},
        {100.0F, 20.0F, 0.0F});
    REQUIRE(rotated_miss);
    CHECK(rotated_miss.result->broad_phase_rejected);
}

TEST_CASE("Unproven source bounds never reject an indexed hull hit",
    "[collision][brush-model][trace][broad-phase][false-negative]")
{
    // The synthetic indexed hull is an unbounded halfspace while its dmodel
    // bounds are only [-1, 1]. A source-bounds broad phase would incorrectly
    // reject this real hit at y=100.
    const auto models = library();
    const auto* model = models->model(1U);
    REQUIRE(model != nullptr);
    CHECK(model->conservative_blocking_bounds(
              collision::CollisionHullOrdinal::point) == nullptr);
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);

    const auto result = trace(*model,
        *identity.transform,
        {2.0F, 100.0F, 0.0F},
        {-2.0F, 100.0F, 0.0F});
    REQUIRE(result);
    CHECK_FALSE(result.result->broad_phase_rejected);
    CHECK(result.result->trace.fraction == Catch::Approx(0.5));
    REQUIRE(result.result->trace.hit);
    CHECK(result.result->trace.hit->source_model_index == 1U);
}

TEST_CASE("Brush broad phase outward-rounds slab fraction arithmetic",
    "[collision][brush-model][trace][broad-phase][rounding]")
{
    const auto built = scene::build_brush_collision_model_library(
        rounding_edge_package());
    REQUIRE(built);
    const auto* model = built.library->model(1U);
    REQUIRE(model != nullptr);
    const auto* bounds = model->conservative_blocking_bounds(
        collision::CollisionHullOrdinal::point);
    REQUIRE(bounds != nullptr);
    const auto quantum = (std::numeric_limits<float>::denorm_min)();
    CHECK(bounds->minimum.x == -quantum);
    CHECK(bounds->maximum.x == 2.0F * quantum);
    CHECK(bounds->minimum.y == -quantum);
    CHECK(bounds->maximum.y == 2.0F * quantum);

    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);
    // These binary32 pairs have the same exact zero-crossing fraction: both
    // x magnitudes are the same exact binary-rational multiple of the
    // corresponding y magnitudes. Naive binary64 slab divisions round the x
    // entry above the y exit and incorrectly reject the segment.
    const assets::AssetVector3 start{
        -std::bit_cast<float>(0x0FD55555U),
        std::bit_cast<float>(0x0F800000U),
        0.0F,
    };
    const assets::AssetVector3 end{
        std::bit_cast<float>(0x00D55555U),
        -std::bit_cast<float>(0x00800000U),
        0.0F,
    };
    const auto result = trace(*model, *identity.transform, start, end);
    REQUIRE(result);
    CHECK_FALSE(result.result->broad_phase_rejected);
}

TEST_CASE("Explicit brush trace validates configuration before spatial miss",
    "[collision][brush-model][trace][broad-phase][configuration]")
{
    // Contract validation must not depend on the broad phase selecting the
    // model.
    const auto models = library();
    const auto* model = models->model(1U);
    REQUIRE(model != nullptr);
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);

    scene::ExplicitBrushCollisionTraceRequest request{
        {2.0F, 100.0F, 0.0F},
        {-2.0F, 100.0F, 0.0F},
    };
    collision::CollisionQueryErrorCode expected_query_error{};
    SECTION("invalid query limits") {
        request.query_limits.maximum_traversal_steps = 0U;
        expected_query_error =
            collision::CollisionQueryErrorCode::invalid_configuration;
    }
    SECTION("invalid tolerance") {
        request.tolerance.fraction_epsilon = 0.0;
        expected_query_error =
            collision::CollisionQueryErrorCode::invalid_configuration;
    }
    SECTION("unsupported contents policy") {
        request.contents_policy =
            collision::CollisionContentsPolicy::
                stock_player_trace_contents_policy_pending;
        expected_query_error =
            collision::CollisionQueryErrorCode::unsupported_contents_policy;
    }
    SECTION("unsupported trace profile") {
        request.trace_profile =
            collision::CollisionTraceCompatibilityProfile::
                stock_engine_trace_behavior_evidence_pending;
        expected_query_error =
            collision::CollisionQueryErrorCode::unsupported_trace_profile;
    }

    const auto result = trace_request(*model, *identity.transform, request);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        scene::ExplicitBrushCollisionTraceErrorCode::invalid_configuration);
    REQUIRE(result.error->query_error);
    CHECK(result.error->query_error->code == expected_query_error);
}

TEST_CASE("Direct terminal brush hulls cannot be broad-phase rejected",
    "[collision][brush-model][trace][broad-phase][terminal]")
{
    const auto built = scene::build_brush_collision_model_library(
        package_with_direct_solid_brush_hull());
    REQUIRE(built);
    const auto* model = built.library->model(1U);
    REQUIRE(model != nullptr);
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);

    const auto result = trace(*model,
        *identity.transform,
        {2.0F, 100.0F, 0.0F},
        {-2.0F, 100.0F, 0.0F},
        collision::CollisionHullOrdinal::standing_32x32x72);
    REQUIRE(result);
    CHECK_FALSE(result.result->broad_phase_rejected);
    CHECK(result.result->trace.start_solid);
    CHECK(result.result->trace.all_solid);
    CHECK(result.result->trace.fraction == 0.0);
    REQUIRE(result.result->trace.hit);
    CHECK(result.result->trace.hit->source_model_index == 1U);
}

TEST_CASE("Explicit brush trace rejects incoherent copied model bounds",
    "[collision][brush-model][trace][coherence]")
{
    const auto models = library();
    const auto* retained = models->model(1U);
    REQUIRE(retained != nullptr);
    auto incoherent = *retained;
    incoherent.local_bounds = {
        {-0.25F, -0.25F, -0.25F},
        {0.25F, 0.25F, 0.25F},
    };
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);

    const auto result = trace(incoherent,
        *identity.transform,
        {2.0F, 100.0F, 0.0F},
        {-2.0F, 100.0F, 0.0F});
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        scene::ExplicitBrushCollisionTraceErrorCode::invalid_model);
    REQUIRE(result.error->query_error);
    CHECK(result.error->query_error->code ==
        collision::CollisionQueryErrorCode::invalid_model);
}

TEST_CASE("Explicit brush trace rejects nonfinite and non-rigid transforms",
    "[collision][brush-model][trace][safety]")
{
    const auto models = library();
    const auto* model = models->model(1U);
    REQUIRE(model != nullptr);
    const auto identity = brush::make_brush_rigid_transform({}, {});
    REQUIRE(identity);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto invalid_segment = trace(
        *model, *identity.transform, {nan, 0.0F, 0.0F}, {});
    REQUIRE_FALSE(invalid_segment);
    REQUIRE(invalid_segment.error);
    CHECK(invalid_segment.error->code ==
        scene::ExplicitBrushCollisionTraceErrorCode::invalid_segment);

    auto scaled = *identity.transform;
    scaled.rotation_basis.local_x_in_world.x = 2.0F;
    const auto invalid_transform = trace(
        *model, scaled, {2.0F, 0.0F, 0.0F}, {-2.0F, 0.0F, 0.0F});
    REQUIRE_FALSE(invalid_transform);
    REQUIRE(invalid_transform.error);
    CHECK(invalid_transform.error->code ==
        scene::ExplicitBrushCollisionTraceErrorCode::invalid_transform);
}

} // namespace

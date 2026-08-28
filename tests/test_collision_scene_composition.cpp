#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>

#include "collision_brush_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string_view>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;
namespace collision = hlclient::collision;
namespace fixture = hlclient::tests::collision_brush_fixture;
namespace scene = hlclient::goldsrc::collision;

struct NamedBrushEntity {
    std::string_view classname;
    scene::BrushCollisionInstanceDefinition collision;
};

[[nodiscard]] brush::BrushRigidTransform rigid(
    const assets::AssetVector3 origin,
    const assets::AssetVector3 angles = {})
{
    const auto built = brush::make_brush_rigid_transform(origin, angles);
    REQUIRE(built);
    return *built.transform;
}

[[nodiscard]] scene::BrushCollisionInstanceIdentity identity(
    const std::uint64_t ordinal,
    const std::uint32_t model,
    const std::optional<std::uint32_t> entity = std::nullopt)
{
    return {ordinal, model, entity};
}

[[nodiscard]] std::shared_ptr<const scene::BrushCollisionModelLibrary>
library(
    std::shared_ptr<const collision::CollisionWorldPackage> package)
{
    const auto built = scene::build_brush_collision_model_library(
        std::move(package));
    REQUIRE(built);
    return built.library;
}

[[nodiscard]] std::shared_ptr<const scene::BrushCollisionScene>
synthetic_scene(
    std::shared_ptr<const collision::CollisionWorldPackage> package,
    const std::vector<scene::BrushCollisionInstanceDefinition>& definitions,
    std::vector<scene::SyntheticBrushCollisionRoleBinding> bindings)
{
    const scene::ExplicitSyntheticBrushCollisionRoleProvider provider{
        std::move(bindings)};
    const auto built = scene::build_brush_collision_scene(
        library(std::move(package)), definitions, provider);
    REQUIRE(built);
    return built.scene;
}

[[nodiscard]] scene::BrushCollisionSceneTraceQueryResult trace(
    std::shared_ptr<const scene::BrushCollisionScene> collision_scene,
    scene::BrushCollisionSceneTraceRequest request = {
        {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F}})
{
    scene::BrushCollisionSceneQuery query{std::move(collision_scene)};
    collision::CollisionQueryScratch scratch;
    return query.trace_hull(request, scratch);
}

TEST_CASE("Stock brush role provider is evidence-pending and never auto-solid",
    "[collision][scene][role][stock][evidence]")
{
    const auto instance_identity = identity(7U, 1U, 42U);
    const std::vector definitions{
        scene::BrushCollisionInstanceDefinition{
            instance_identity, rigid({10.0F, 0.0F, 0.0F})},
    };
    const scene::StockBrushCollisionRoleProvider provider;
    CHECK(provider.profile() == scene::BrushCollisionRoleProviderProfile::
        stock_brush_solidity_evidence_pending);
    CHECK(provider.role_for(instance_identity) ==
        scene::BrushCollisionRole::evidence_pending);

    const auto built = scene::build_brush_collision_scene(
        library(fixture::package(false)), definitions, provider);
    REQUIRE(built);
    REQUIRE(built.scene->instances().size() == 1U);
    CHECK(built.scene->instances()[0U].role ==
        scene::BrushCollisionRole::evidence_pending);

    const auto result = trace(built.scene);
    REQUIRE(result);
    CHECK_FALSE(result.result->scene_hit);
    CHECK(result.result->trace.fraction == 1.0);
    CHECK(result.result->statistics.evidence_pending_instance_count == 1U);
    CHECK(result.result->statistics.brush_model_trace_count == 0U);
    CHECK(result.result->all_solid_classification ==
        scene::BrushCollisionSceneAllSolidClassification::
            exact_from_world_only);

    scene::BrushCollisionSceneTraceRequest brushes_only{
        {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F}};
    brushes_only.include_world = false;
    const auto filtered = trace(built.scene, brushes_only);
    REQUIRE(filtered);
    CHECK_FALSE(filtered.result->scene_hit);
    CHECK(filtered.result->statistics.evidence_pending_instance_count == 1U);
    CHECK(filtered.result->statistics.broad_phase_test_count == 0U);
    CHECK(filtered.result->all_solid_classification ==
        scene::BrushCollisionSceneAllSolidClassification::
            exact_without_model_trace);
}

TEST_CASE("Stock classnames never infer brush collision roles",
    "[collision][scene][role][stock][classname]")
{
    const std::array entities{
        NamedBrushEntity{"func_door",
            {identity(20U, 1U, 100U), rigid({10.0F, 0.0F, 0.0F})}},
        NamedBrushEntity{"func_wall",
            {identity(21U, 1U, 101U), rigid({8.0F, 0.0F, 0.0F})}},
        NamedBrushEntity{"func_water",
            {identity(22U, 1U, 102U), rigid({6.0F, 0.0F, 0.0F})}},
        NamedBrushEntity{"trigger_multiple",
            {identity(23U, 1U, 103U), rigid({4.0F, 0.0F, 0.0F})}},
    };
    const std::array expected_names{
        std::string_view{"func_door"},
        std::string_view{"func_wall"},
        std::string_view{"func_water"},
        std::string_view{"trigger_multiple"},
    };
    std::vector<scene::BrushCollisionInstanceDefinition> definitions;
    definitions.reserve(entities.size());
    const scene::StockBrushCollisionRoleProvider provider;
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        INFO("classname=" << entities[index].classname);
        CHECK(entities[index].classname == expected_names[index]);
        CHECK(provider.role_for(entities[index].collision.identity) ==
            scene::BrushCollisionRole::evidence_pending);
        definitions.push_back(entities[index].collision);
    }

    const auto built = scene::build_brush_collision_scene(
        library(fixture::package(false)), definitions, provider);
    REQUIRE(built);
    REQUIRE(built.scene->instances().size() == entities.size());
    for (const auto& instance : built.scene->instances()) {
        CHECK(instance.role == scene::BrushCollisionRole::evidence_pending);
        CHECK(instance.role_provider_profile ==
            scene::BrushCollisionRoleProviderProfile::
                stock_brush_solidity_evidence_pending);
    }

    const auto result = trace(built.scene);
    REQUIRE(result);
    CHECK_FALSE(result.result->scene_hit);
    CHECK(result.result->trace.fraction == 1.0);
    CHECK(result.result->statistics.evidence_pending_instance_count ==
        entities.size());
    CHECK(result.result->statistics.solid_instance_count == 0U);
    CHECK(result.result->statistics.brush_candidate_count == 0U);
    CHECK(result.result->statistics.brush_model_trace_count == 0U);
}

TEST_CASE("Scene selects the deterministic earliest world or brush hit",
    "[collision][scene][earliest][tie]")
{
    const auto brush_identity = identity(5U, 1U);
    const std::vector definitions{
        scene::BrushCollisionInstanceDefinition{
            brush_identity, rigid({10.0F, 0.0F, 0.0F})},
    };
    const std::vector bindings{
        scene::SyntheticBrushCollisionRoleBinding{
            brush_identity, scene::BrushCollisionRole::solid},
    };

    SECTION("brush before world")
    {
        const auto result = trace(synthetic_scene(
            fixture::package(true, 0.0), definitions, bindings));
        REQUIRE(result);
        REQUIRE(result.result->scene_hit);
        CHECK(result.result->scene_hit->kind ==
            collision::CollisionTraceHitKind::collision_model);
        REQUIRE(result.result->scene_hit->brush_instance);
        CHECK(*result.result->scene_hit->brush_instance == brush_identity);
        CHECK(result.result->trace.fraction == Catch::Approx(0.25));
        CHECK(result.result->all_solid_classification ==
            scene::BrushCollisionSceneAllSolidClassification::
                multi_object_interval_union_evidence_pending);
    }
    SECTION("world before brush")
    {
        const auto result = trace(synthetic_scene(
            fixture::package(true, 15.0), definitions, bindings));
        REQUIRE(result);
        REQUIRE(result.result->scene_hit);
        CHECK(result.result->scene_hit->kind ==
            collision::CollisionTraceHitKind::world);
        CHECK_FALSE(result.result->scene_hit->brush_instance);
        CHECK(result.result->trace.fraction == Catch::Approx(0.125));
    }
    SECTION("world wins an equal-fraction tie")
    {
        const auto result = trace(synthetic_scene(
            fixture::package(true, 10.0), definitions, bindings));
        REQUIRE(result);
        REQUIRE(result.result->scene_hit);
        CHECK(result.result->scene_hit->kind ==
            collision::CollisionTraceHitKind::world);
        CHECK(result.result->trace.fraction == Catch::Approx(0.25));
    }
}

TEST_CASE("Scene request filters world and exact brush identities",
    "[collision][scene][filter][ignore]")
{
    const auto earlier = identity(1U, 1U);
    const auto later = identity(2U, 2U);
    const std::vector definitions{
        scene::BrushCollisionInstanceDefinition{
            earlier, rigid({15.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            later, rigid({10.0F, 0.0F, 0.0F})},
    };
    const std::vector bindings{
        scene::SyntheticBrushCollisionRoleBinding{
            earlier, scene::BrushCollisionRole::solid},
        scene::SyntheticBrushCollisionRoleBinding{
            later, scene::BrushCollisionRole::solid},
    };
    const auto collision_scene = synthetic_scene(
        fixture::package(true, 17.0), definitions, bindings);

    SECTION("world can be excluded")
    {
        scene::BrushCollisionSceneTraceRequest request{
            {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F}};
        request.include_world = false;
        const auto result = trace(collision_scene, request);
        REQUIRE(result);
        REQUIRE(result.result->scene_hit);
        REQUIRE(result.result->scene_hit->brush_instance);
        CHECK(*result.result->scene_hit->brush_instance == earlier);
        CHECK(result.result->trace.fraction == Catch::Approx(0.125));
    }
    SECTION("explicit brush list is an exact allowlist")
    {
        const std::array selected{later};
        scene::BrushCollisionSceneTraceRequest request{
            {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F}};
        request.include_world = false;
        request.explicit_brush_instances = selected;
        const auto result = trace(collision_scene, request);
        REQUIRE(result);
        REQUIRE(result.result->scene_hit);
        REQUIRE(result.result->scene_hit->brush_instance);
        CHECK(*result.result->scene_hit->brush_instance == later);
        CHECK(result.result->trace.fraction == Catch::Approx(0.25));
        CHECK(result.result->statistics.brush_model_trace_count == 1U);
    }
    SECTION("ignored identity wins over normal scene selection")
    {
        scene::BrushCollisionSceneTraceRequest request{
            {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F}};
        request.include_world = false;
        request.ignored_instance = earlier;
        const auto result = trace(collision_scene, request);
        REQUIRE(result);
        REQUIRE(result.result->scene_hit);
        REQUIRE(result.result->scene_hit->brush_instance);
        CHECK(*result.result->scene_hit->brush_instance == later);
        CHECK(result.result->trace.fraction == Catch::Approx(0.25));
        CHECK(result.result->statistics.brush_model_trace_count == 1U);
    }
    SECTION("ignored identity wins over an explicit-list match")
    {
        const std::array selected{earlier};
        scene::BrushCollisionSceneTraceRequest request{
            {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F}};
        request.include_world = false;
        request.explicit_brush_instances = selected;
        request.ignored_instance = earlier;
        const auto result = trace(collision_scene, request);
        REQUIRE(result);
        CHECK_FALSE(result.result->scene_hit);
        CHECK(result.result->trace.fraction == 1.0);
        CHECK(result.result->trace.end_position.x == -20.0F);
        CHECK(result.result->trace.in_open);
        CHECK_FALSE(result.result->trace.all_solid);
        CHECK(result.result->statistics.brush_model_trace_count == 0U);
        CHECK(result.result->all_solid_classification ==
            scene::BrushCollisionSceneAllSolidClassification::
                exact_without_model_trace);
    }
    SECTION("broad-phase misses remain an exact no-model-trace result")
    {
        const auto bounded_scene = synthetic_scene(
            fixture::bounded_brush_package(), definitions, bindings);
        scene::BrushCollisionSceneTraceRequest request{
            {20.0F, 100.0F, 0.0F}, {-20.0F, 100.0F, 0.0F}};
        request.include_world = false;
        const auto result = trace(bounded_scene, request);
        REQUIRE(result);
        CHECK_FALSE(result.result->scene_hit);
        CHECK(result.result->trace.fraction == 1.0);
        CHECK(result.result->statistics.broad_phase_test_count == 2U);
        CHECK(result.result->statistics.broad_phase_rejection_count == 2U);
        CHECK(result.result->statistics.brush_model_trace_count == 0U);
        CHECK(result.result->all_solid_classification ==
            scene::BrushCollisionSceneAllSolidClassification::
                exact_without_model_trace);
    }
}

TEST_CASE("Brush tie uses stable ordinal then source model index",
    "[collision][scene][tie][identity]")
{
    const auto high_ordinal = identity(9U, 1U);
    const auto low_ordinal = identity(3U, 2U);
    const std::vector definitions{
        scene::BrushCollisionInstanceDefinition{
            high_ordinal, rigid({10.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            low_ordinal, rigid({10.0F, 0.0F, 0.0F})},
    };
    const std::vector bindings{
        scene::SyntheticBrushCollisionRoleBinding{
            high_ordinal, scene::BrushCollisionRole::solid},
        scene::SyntheticBrushCollisionRoleBinding{
            low_ordinal, scene::BrushCollisionRole::solid},
    };
    auto result = trace(synthetic_scene(
        fixture::package(false), definitions, bindings));
    REQUIRE(result);
    REQUIRE(result.result->scene_hit);
    REQUIRE(result.result->scene_hit->brush_instance);
    CHECK(*result.result->scene_hit->brush_instance == low_ordinal);

    const auto model_two = identity(4U, 2U);
    const auto model_one = identity(4U, 1U);
    const std::vector same_ordinal_definitions{
        scene::BrushCollisionInstanceDefinition{
            model_two, rigid({10.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            model_one, rigid({10.0F, 0.0F, 0.0F})},
    };
    const std::vector same_ordinal_bindings{
        scene::SyntheticBrushCollisionRoleBinding{
            model_two, scene::BrushCollisionRole::solid},
        scene::SyntheticBrushCollisionRoleBinding{
            model_one, scene::BrushCollisionRole::solid},
    };
    result = trace(synthetic_scene(fixture::package(false),
        same_ordinal_definitions,
        same_ordinal_bindings));
    REQUIRE(result);
    REQUIRE(result.result->scene_hit);
    REQUIRE(result.result->scene_hit->brush_instance);
    CHECK(*result.result->scene_hit->brush_instance == model_one);
}

TEST_CASE("Scene excludes every role except explicitly solid instances",
    "[collision][scene][role][filter]")
{
    const auto solid = identity(1U, 1U);
    const auto non_solid = identity(2U, 1U);
    const auto unsupported = identity(3U, 1U);
    const auto pending = identity(4U, 1U);
    const std::vector definitions{
        scene::BrushCollisionInstanceDefinition{
            non_solid, rigid({15.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            unsupported, rigid({14.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            pending, rigid({13.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            solid, rigid({10.0F, 0.0F, 0.0F})},
    };
    const std::vector bindings{
        scene::SyntheticBrushCollisionRoleBinding{
            solid, scene::BrushCollisionRole::solid},
        scene::SyntheticBrushCollisionRoleBinding{
            non_solid, scene::BrushCollisionRole::non_solid},
        scene::SyntheticBrushCollisionRoleBinding{
            unsupported, scene::BrushCollisionRole::unsupported},
        scene::SyntheticBrushCollisionRoleBinding{
            pending, scene::BrushCollisionRole::evidence_pending},
    };
    const auto result = trace(synthetic_scene(
        fixture::package(false), definitions, bindings));
    REQUIRE(result);
    REQUIRE(result.result->scene_hit);
    REQUIRE(result.result->scene_hit->brush_instance);
    CHECK(*result.result->scene_hit->brush_instance == solid);
    CHECK(result.result->statistics.solid_instance_count == 1U);
    CHECK(result.result->statistics.non_solid_instance_count == 1U);
    CHECK(result.result->statistics.unsupported_instance_count == 1U);
    CHECK(result.result->statistics.evidence_pending_instance_count == 1U);
    CHECK(result.result->statistics.brush_model_trace_count == 1U);
}

TEST_CASE("Scene broad phase and candidate limits are deterministic",
    "[collision][scene][broad-phase][limits]")
{
    const auto near = identity(1U, 1U);
    const auto far = identity(2U, 1U);
    const auto second_near = identity(3U, 2U);
    const std::vector definitions{
        scene::BrushCollisionInstanceDefinition{
            near, rigid({10.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            far, rigid({10.0F, 100.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            second_near, rigid({5.0F, 0.0F, 0.0F})},
    };
    const std::vector bindings{
        scene::SyntheticBrushCollisionRoleBinding{
            near, scene::BrushCollisionRole::solid},
        scene::SyntheticBrushCollisionRoleBinding{
            far, scene::BrushCollisionRole::solid},
        scene::SyntheticBrushCollisionRoleBinding{
            second_near, scene::BrushCollisionRole::solid},
    };
    const auto collision_scene = synthetic_scene(
        fixture::bounded_brush_package(), definitions, bindings);
    const auto result = trace(collision_scene);
    REQUIRE(result);
    CHECK(result.result->statistics.broad_phase_test_count == 3U);
    CHECK(result.result->statistics.broad_phase_rejection_count == 1U);
    CHECK(result.result->statistics.brush_candidate_count == 2U);
    CHECK(result.result->statistics.brush_model_trace_count == 2U);
    REQUIRE(result.result->scene_hit);
    REQUIRE(result.result->scene_hit->brush_instance);
    CHECK(*result.result->scene_hit->brush_instance == near);

    scene::BrushCollisionSceneTraceRequest limited{
        {20.0F, 0.0F, 0.0F}, {-20.0F, 0.0F, 0.0F}};
    limited.scene_limits.maximum_brush_candidates = 1U;
    const auto overflow = trace(collision_scene, limited);
    REQUIRE_FALSE(overflow);
    REQUIRE(overflow.error);
    CHECK(overflow.error->code ==
        scene::BrushCollisionSceneQueryErrorCode::candidate_limit_exceeded);
}

TEST_CASE("Multiple instances may share one collision model",
    "[collision][scene][instance-sharing]")
{
    const auto earlier = identity(10U, 1U);
    const auto later = identity(11U, 1U);
    const std::vector definitions{
        scene::BrushCollisionInstanceDefinition{
            later, rigid({5.0F, 0.0F, 0.0F})},
        scene::BrushCollisionInstanceDefinition{
            earlier, rigid({10.0F, 0.0F, 0.0F})},
    };
    const std::vector bindings{
        scene::SyntheticBrushCollisionRoleBinding{
            earlier, scene::BrushCollisionRole::solid},
        scene::SyntheticBrushCollisionRoleBinding{
            later, scene::BrushCollisionRole::solid},
    };
    const auto result = trace(synthetic_scene(
        fixture::package(false), definitions, bindings));
    REQUIRE(result);
    REQUIRE(result.result->scene_hit);
    REQUIRE(result.result->scene_hit->brush_instance);
    CHECK(*result.result->scene_hit->brush_instance == earlier);
    CHECK(result.result->trace.fraction == Catch::Approx(0.25));
}

} // namespace

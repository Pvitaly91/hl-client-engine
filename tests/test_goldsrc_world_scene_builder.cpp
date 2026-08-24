#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>

#include "world_render_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;
namespace bsp = hlclient::goldsrc::bsp;
namespace goldsrc_spatial = hlclient::goldsrc::spatial;
namespace scene = hlclient::world_scene_render;
namespace fixture = hlclient::tests::world_render_fixture;

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto character : text) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    return bytes;
}

[[nodiscard]] std::shared_ptr<
    const hlclient::world_render::WorldRenderPackage>
make_world_package()
{
    auto built = fixture::make_package();
    REQUIRE(built);
    return std::make_shared<
        const hlclient::world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] bsp::GoldSrcBspParsedDocument make_document(
    const std::string_view entities,
    const bool retain_brush_model = true,
    const assets::AssetVector3 source_model_origin = {})
{
    bsp::GoldSrcBspParsedDocument document;
    document.world_asset = fixture::make_world();
    const assets::WorldBounds spatial_bounds{
        {-128.0F, -128.0F, -128.0F},
        {128.0F, 128.0F, 128.0F},
    };
    document.spatial_source.planes = {
        goldsrc_spatial::GoldSrcSpatialSourcePlane{
            {1.0F, 0.0F, 0.0F}, 0.0, 0},
    };
    document.spatial_source.nodes = {
        goldsrc_spatial::GoldSrcSpatialSourceNode{
            0,
            {-2, -2},
            spatial_bounds,
            std::nullopt,
            std::nullopt,
        },
    };
    document.spatial_source.leaves = {
        goldsrc_spatial::GoldSrcSpatialSourceLeaf{
            -2, -1, spatial_bounds, 0U, 0U},
        goldsrc_spatial::GoldSrcSpatialSourceLeaf{
            -1, -1, spatial_bounds, 0U, 1U},
    };
    document.spatial_source.marksurface_face_ordinals = {0U};
    document.spatial_source.world_model = {
        spatial_bounds,
        0,
        1,
    };
    document.spatial_source.source_face_count = 1U;
    document.entity_lump_bytes = bytes_of(entities);

    const auto model_count = retain_brush_model ? 2U : 1U;
    document.lump_element_counts[static_cast<std::size_t>(
        bsp::GoldSrcBspLumpId::models)] = model_count;
    if (retain_brush_model) {
        auto geometry = fixture::make_world();
        const auto bounds = geometry.bounds;
        document.brush_submodels.push_back(
            bsp::GoldSrcBspBrushSubmodelAsset{
                1U,
                source_model_origin,
                bounds,
                0,
                std::move(geometry),
            });
    }
    return document;
}

[[nodiscard]] scene::BrushSubmodelRenderLibrary make_brush_library(
    std::shared_ptr<const hlclient::world_render::WorldRenderPackage>
        render_package)
{
    std::vector<scene::BrushSubmodelRenderModel> models;
    models.emplace_back(
        1U, fixture::make_world().bounds, std::vector<std::uint32_t>{0U});
    return {std::move(render_package), std::move(models)};
}

TEST_CASE("World scene brush-off mode owns only world and spatial packages",
    "[goldsrc-world-scene][off][ownership]")
{
    const auto world = make_world_package();
    auto library = make_brush_library(world);
    auto document = make_document(R"({"classname" "unterminated})");
    const auto source_entity_bytes = document.entity_lump_bytes;

    const auto built = brush::GoldSrcWorldSceneBuilder::build(
        document,
        world,
        std::move(library));
    REQUIRE(built);
    REQUIRE(built.scene_package);
    CHECK(built.scene_package->world_package() == world);
    CHECK(built.scene_package->spatial_package().nodes().size() == 1U);
    CHECK(built.scene_package->spatial_package().leaves().size() == 2U);
    CHECK(built.scene_package->brush_library().render_package() == nullptr);
    CHECK(built.scene_package->brush_library().models().empty());
    CHECK(built.scene_package->brush_instances().empty());
    CHECK(built.statistics.entity_document_parse_count == 0U);
    CHECK_FALSE(built.spawn_camera.has_value());
    CHECK(document.entity_lump_bytes == source_entity_bytes);
}

TEST_CASE("Static scene conversion retains every instance outcome and ownership",
    "[goldsrc-world-scene][static][conversion][ownership]")
{
    const auto world = make_world_package();
    const auto built = [&world] {
        auto document = make_document(
            R"({"classname" "worldspawn"})"
            R"({"classname" "func_door" "model" "*1" "origin" "32 0 0"})"
            R"({"classname" "func_wall" "model" "*1" "origin" "64 0 0" "rendermode" "5"})"
            R"({"classname" "func_button" "model" "*2"})");
        auto library = make_brush_library(world);
        const brush::GoldSrcWorldSceneBuildConfig config{
            brush::GoldSrcWorldSceneBrushMode::static_initial,
            false,
        };
        return brush::GoldSrcWorldSceneBuilder::build(
            document,
            world,
            std::move(library),
            config);
    }();

    REQUIRE(built);
    REQUIRE(built.scene_package);
    const auto& package = *built.scene_package;
    CHECK(package.world_package() == world);
    CHECK(package.brush_library().render_package() == world);
    REQUIRE(package.brush_library().models().size() == 1U);
    REQUIRE(package.brush_instances().size() == 3U);

    const auto& supported = package.brush_instances()[0U];
    CHECK(supported.source_instance_index == 0U);
    CHECK(supported.source_entity_ordinal == 1U);
    CHECK(supported.source_model_index == 1U);
    CHECK(supported.support_status ==
        scene::BrushSubmodelRenderSupportStatus::supported_static_opaque);
    CHECK(supported.model_transform.values[12U] == Catch::Approx(32.0F));
    CHECK(supported.transformed_bounds.minimum.x == Catch::Approx(32.0F));
    CHECK(supported.transformed_bounds.maximum.x == Catch::Approx(48.0F));
    CHECK(supported.touched_leaf_indices == std::vector<std::uint32_t>{1U});

    const auto& unsupported_render = package.brush_instances()[1U];
    CHECK(unsupported_render.source_instance_index == 1U);
    CHECK(unsupported_render.source_entity_ordinal == 2U);
    CHECK(unsupported_render.support_status ==
        scene::BrushSubmodelRenderSupportStatus::unsupported_rendermode);
    CHECK(unsupported_render.model_transform.values[12U] ==
        Catch::Approx(64.0F));
    CHECK(unsupported_render.transformed_bounds.minimum.x ==
        Catch::Approx(64.0F));
    CHECK(unsupported_render.touched_leaf_indices.empty());

    const auto& invalid_model = package.brush_instances()[2U];
    CHECK(invalid_model.source_instance_index == 2U);
    CHECK(invalid_model.source_entity_ordinal == 3U);
    CHECK_FALSE(invalid_model.source_model_index.has_value());
    CHECK(invalid_model.support_status ==
        scene::BrushSubmodelRenderSupportStatus::invalid_model_reference);
    CHECK(invalid_model.model_transform == hlclient::renderer::RenderMatrix4{});
    CHECK(invalid_model.transformed_bounds.minimum.x == 0.0F);
    CHECK(invalid_model.transformed_bounds.maximum.x == 0.0F);

    CHECK(package.statistics().brush_instance_count == 3U);
    CHECK(package.statistics().supported_brush_instance_count == 1U);
    CHECK(package.statistics().unsupported_brush_instance_count == 2U);
    CHECK(package.bounds().maximum.x == Catch::Approx(48.0F));
    CHECK(built.statistics.entity_document_parse_count == 1U);
    CHECK(built.statistics.parsed_entity_count == 4U);
    CHECK(built.statistics.brush_instance_count == 3U);
    CHECK(built.statistics.supported_brush_instance_count == 1U);
    CHECK(built.statistics.unsupported_brush_instance_count == 2U);
}

TEST_CASE("Every brush instance status has an exact neutral scene status",
    "[goldsrc-world-scene][status]")
{
    using Input = brush::BrushSubmodelInstanceStatus;
    using Output = scene::BrushSubmodelRenderSupportStatus;
    constexpr std::array mappings{
        std::pair{Input::supported_static_opaque,
            Output::supported_static_opaque},
        std::pair{Input::unsupported_transform, Output::unsupported_transform},
        std::pair{
            Input::unsupported_rendermode, Output::unsupported_rendermode},
        std::pair{
            Input::invalid_model_reference, Output::invalid_model_reference},
        std::pair{
            Input::missing_model_geometry, Output::missing_model_geometry},
        std::pair{
            Input::invalid_entity_metadata, Output::invalid_entity_metadata},
        std::pair{Input::outside_world_spatial_tree,
            Output::outside_world_spatial_tree},
        std::pair{Input::no_visible_leaf_membership,
            Output::no_visible_leaf_membership},
    };
    for (const auto& [input, expected] : mappings) {
        CHECK(brush::to_world_scene_render_support_status(input) == expected);
    }
}

TEST_CASE("Absent brush render assets retain typed missing-model instances",
    "[goldsrc-world-scene][static][missing-library]")
{
    const auto world = make_world_package();
    auto document = make_document(
        R"({"classname" "func_door" "model" "*1" "origin" "32 0 0"})");
    const brush::GoldSrcWorldSceneBuildConfig config{
        brush::GoldSrcWorldSceneBrushMode::static_initial,
        false,
    };
    const auto built = brush::GoldSrcWorldSceneBuilder::build(
        document,
        world,
        std::nullopt,
        config);
    REQUIRE(built);
    REQUIRE(built.scene_package);
    CHECK(built.scene_package->brush_library().models().empty());
    REQUIRE(built.scene_package->brush_instances().size() == 1U);
    const auto& instance = built.scene_package->brush_instances()[0U];
    CHECK(instance.support_status ==
        scene::BrushSubmodelRenderSupportStatus::missing_model_geometry);
    CHECK(instance.model_transform == hlclient::renderer::RenderMatrix4{});
    CHECK(instance.touched_leaf_indices.empty());
    CHECK(built.statistics.supported_brush_instance_count == 0U);
    CHECK(built.statistics.unsupported_brush_instance_count == 1U);
}

TEST_CASE("Spawn extraction shares the one inert entity-document parse",
    "[goldsrc-world-scene][spawn]")
{
    const auto world = make_world_package();
    SECTION("static brush and spawn camera")
    {
        auto document = make_document(
            R"({"classname" "func_door" "model" "*1" "origin" "32 0 0"})"
            R"({"classname" "info_player_deathmatch" "origin" "1 2 3"})"
            R"({"classname" "info_player_start" "origin" "4 5 6" "angle" "90"})");
        auto library = make_brush_library(world);
        const brush::GoldSrcWorldSceneBuildConfig config{
            brush::GoldSrcWorldSceneBrushMode::static_initial,
            true,
        };
        const auto built = brush::GoldSrcWorldSceneBuilder::build(
            document,
            world,
            std::move(library),
            config);
        REQUIRE(built);
        REQUIRE(built.spawn_camera);
        REQUIRE(*built.spawn_camera);
        REQUIRE(built.spawn_camera->descriptor);
        CHECK(built.spawn_camera->descriptor->source_class ==
            brush::GoldSrcSpawnCameraSourceClass::info_player_start);
        CHECK(built.spawn_camera->descriptor->source_entity_ordinal == 2U);
        CHECK(built.spawn_camera->descriptor->position.x == 4.0F);
        CHECK(built.spawn_camera->descriptor->forward.y ==
            Catch::Approx(1.0F));
        CHECK(built.statistics.entity_document_parse_count == 1U);
        CHECK(built.statistics.parsed_entity_count == 3U);
    }
    SECTION("spawn extraction remains independent of brush-off mode")
    {
        auto document = make_document(
            R"({"classname" "info_player_deathmatch" "origin" "7 8 9"})");
        const brush::GoldSrcWorldSceneBuildConfig config{
            brush::GoldSrcWorldSceneBrushMode::off,
            true,
        };
        const auto built = brush::GoldSrcWorldSceneBuilder::build(
            document,
            world,
            std::nullopt,
            config);
        REQUIRE(built);
        REQUIRE(built.spawn_camera);
        REQUIRE(built.spawn_camera->descriptor);
        CHECK(built.scene_package->brush_instances().empty());
        CHECK(built.spawn_camera->descriptor->source_class ==
            brush::GoldSrcSpawnCameraSourceClass::info_player_deathmatch);
        CHECK(built.statistics.entity_document_parse_count == 1U);
    }
}

TEST_CASE("Entity and scene failures publish no partial scene or spawn result",
    "[goldsrc-world-scene][transactional][error]")
{
    const auto world = make_world_package();
    auto document = make_document(R"({"classname" "unterminated})");
    const brush::GoldSrcWorldSceneBuildConfig config{
        brush::GoldSrcWorldSceneBrushMode::off,
        true,
    };
    const auto built = brush::GoldSrcWorldSceneBuilder::build(
        document,
        world,
        std::nullopt,
        config);
    REQUIRE_FALSE(built);
    CHECK_FALSE(built.scene_package.has_value());
    CHECK_FALSE(built.spawn_camera.has_value());
    REQUIRE(built.error);
    CHECK(built.error->code ==
        brush::GoldSrcWorldSceneBuildErrorCode::entity_document_parse_failed);
    REQUIRE(built.error->entity_error);
    CHECK(built.error->entity_error->code ==
        bsp::GoldSrcEntityDocumentErrorCode::unterminated_quote);
    CHECK(built.statistics.entity_document_parse_count == 1U);
}

TEST_CASE("Nested builder failures remain typed and transactionally empty",
    "[goldsrc-world-scene][limits][error]")
{
    const auto world = make_world_package();
    SECTION("spatial validation")
    {
        auto document = make_document(R"({"classname" "worldspawn"})");
        document.spatial_source.planes[0U].normal = {};
        const auto built = brush::GoldSrcWorldSceneBuilder::build(
            document, world);
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code == brush::GoldSrcWorldSceneBuildErrorCode::
            spatial_package_build_failed);
        REQUIRE(built.error->spatial_error);
        CHECK(built.error->spatial_error->code ==
            goldsrc_spatial::GoldSrcSpatialImportErrorCode::invalid_plane);
        CHECK_FALSE(built.scene_package.has_value());
    }
    SECTION("instance limit")
    {
        auto document = make_document(
            R"({"model" "*1" "origin" "32 0 0"})"
            R"({"model" "*1" "origin" "64 0 0"})");
        auto library = make_brush_library(world);
        const brush::GoldSrcWorldSceneBuildConfig config{
            brush::GoldSrcWorldSceneBrushMode::static_initial,
            false,
        };
        auto limits = brush::GoldSrcWorldSceneBuildLimits{};
        limits.instances.maximum_instances = 1U;
        const auto built = brush::GoldSrcWorldSceneBuilder::build(
            document,
            world,
            std::move(library),
            config,
            limits);
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code == brush::GoldSrcWorldSceneBuildErrorCode::
            brush_instance_set_build_failed);
        REQUIRE(built.error->instance_error);
        CHECK(built.error->instance_error->code ==
            brush::BrushSubmodelInstanceBuildErrorCode::
                instance_limit_exceeded);
        CHECK_FALSE(built.scene_package.has_value());
    }
    SECTION("scene limit")
    {
        auto document = make_document(
            R"({"model" "*1" "origin" "32 0 0"})"
            R"({"model" "*1" "origin" "64 0 0"})");
        auto library = make_brush_library(world);
        const brush::GoldSrcWorldSceneBuildConfig config{
            brush::GoldSrcWorldSceneBrushMode::static_initial,
            false,
        };
        auto limits = brush::GoldSrcWorldSceneBuildLimits{};
        limits.scene.maximum_brush_instances = 1U;
        const auto built = brush::GoldSrcWorldSceneBuilder::build(
            document,
            world,
            std::move(library),
            config,
            limits);
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code == brush::GoldSrcWorldSceneBuildErrorCode::
            scene_package_build_failed);
        REQUIRE(built.error->scene_error);
        CHECK(built.error->scene_error->code ==
            scene::WorldSceneRenderErrorCode::invalid_brush_instance);
        CHECK_FALSE(built.scene_package.has_value());
    }
}

TEST_CASE("World scene rejects cross-document world and brush packages",
    "[goldsrc-world-scene][provenance][transactional]")
{
    const auto world = make_world_package();
    SECTION("same-layout world package from another source")
    {
        auto document = make_document(R"({"classname" "worldspawn"})");
        document.world_asset.source_content_fingerprint =
            assets::AssetSourceFingerprint{0x1111U, 0x2222U};
        const auto built = brush::GoldSrcWorldSceneBuilder::build(
            document, world);
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code == brush::GoldSrcWorldSceneBuildErrorCode::
            world_package_document_mismatch);
        CHECK_FALSE(built.scene_package);
    }
    SECTION("brush geometry provenance differs from its document")
    {
        auto document = make_document(
            R"({"classname" "func_wall" "model" "*1"})");
        REQUIRE_FALSE(document.brush_submodels.empty());
        document.brush_submodels[0U].geometry.source_content_fingerprint =
            assets::AssetSourceFingerprint{0x3333U, 0x4444U};
        auto library = make_brush_library(world);
        const brush::GoldSrcWorldSceneBuildConfig config{
            brush::GoldSrcWorldSceneBrushMode::static_initial,
            false,
        };
        const auto built = brush::GoldSrcWorldSceneBuilder::build(
            document,
            world,
            std::move(library),
            config);
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code == brush::GoldSrcWorldSceneBuildErrorCode::
            brush_library_document_mismatch);
        CHECK_FALSE(built.scene_package);
    }
}

} // namespace

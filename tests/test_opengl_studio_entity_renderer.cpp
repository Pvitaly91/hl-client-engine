#include "entity_render/entity_opengl_test_support.hpp"
#include "world_render_test_fixture.hpp"

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::tests::entity_opengl_fixture;
namespace world_fixture = hlclient::tests::world_render_fixture;
namespace entity = hlclient::entity_render;
namespace renderer = hlclient::renderer;
namespace spatial = hlclient::world_spatial;
namespace visibility = hlclient::world_visibility;

[[nodiscard]] std::shared_ptr<const entity::EntityRenderFrame>
single_studio_frame(
    const entity::EntitySceneRenderPackage& package,
    const std::uint64_t revision,
    const hlclient::assets::AssetVector3 origin,
    const hlclient::assets::AssetVector3 rotation,
    const float scale,
    const hlclient::assets::WorldBounds bounds,
    const std::uint32_t skin_family_index = 0U,
    const visibility::WorldViewFrustum* view_frustum = nullptr,
    const spatial::WorldSpatialPackage* spatial_package = nullptr,
    const std::optional<std::uint32_t> camera_leaf_index = std::nullopt)
{
    REQUIRE(package.studio_assets().size() == 1U);
    entity::EntityRenderFrameBuildInput input;
    input.resource_id = 0xE510U;
    input.resource_revision = revision;
    input.interpolation = {
        0.5,
        0.0,
        1.0,
        0.5F,
        revision,
        revision + 1U,
        entity::EntityRenderInterpolationProfile::synthetic_seconds_v1,
    };
    input.studio_poses.push_back({
        package.studio_assets()[0U]->source_identity(),
        {fixture::pose_matrix()},
    });
    entity::StudioEntityRenderInstance instance;
    instance.entity_number = 1U;
    instance.studio_asset_index = 0U;
    instance.pose_index = 0U;
    instance.transform.origin = origin;
    instance.transform.rotation_degrees = rotation;
    instance.transform.uniform_scale = scale;
    instance.body_value = 0U;
    instance.skin_family_index = skin_family_index;
    instance.interpolated_bounds = bounds;
    input.studio_instances.push_back(instance);
    input.view_frustum = view_frustum;
    input.spatial_package = spatial_package;
    input.camera_leaf_index = camera_leaf_index;
    auto built = entity::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const entity::EntityRenderFrame>(
        std::move(*built.frame));
}

[[nodiscard]] spatial::WorldSpatialPackage visibility_spatial_package()
{
    const hlclient::assets::WorldBounds bounds{
        {-4.0F, -4.0F, -4.0F}, {4.0F, 4.0F, 4.0F}};
    spatial::WorldSpatialNode node;
    node.plane_index = 0U;
    node.children = {
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, 1U},
        spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::leaf, 2U},
    };
    node.bounds = bounds;

    spatial::WorldSpatialLeaf solid;
    solid.source_leaf_index = 0U;
    solid.bounds = bounds;
    solid.surface_membership.source_leaf_index = 0U;
    solid.solid_or_special = true;
    spatial::WorldSpatialLeaf visible;
    visible.source_leaf_index = 1U;
    visible.bounds = {{0.0F, -4.0F, -4.0F}, {4.0F, 4.0F, 4.0F}};
    visible.pvs_row_index = 0U;
    visible.pvs_bit_addressable = true;
    visible.surface_membership.source_leaf_index = 1U;
    spatial::WorldSpatialLeaf hidden;
    hidden.source_leaf_index = 2U;
    hidden.bounds = {{-4.0F, -4.0F, -4.0F}, {0.0F, 4.0F, 4.0F}};
    hidden.pvs_row_index = 1U;
    hidden.pvs_bit_addressable = true;
    hidden.surface_membership.source_leaf_index = 2U;

    return spatial::WorldSpatialPackage{
        {{{1.0F, 0.0F, 0.0F}, 0.0F, 0}},
        {node},
        {solid, visible, hidden},
        spatial::WorldPvsTable{
            1U,
            2U,
            {{std::byte{0x01U}}, {std::byte{0x02U}}},
            {std::nullopt, 0U, 1U},
            0U},
        spatial::WorldSpatialModelMetadata{0U, 2U, bounds},
        spatial::WorldSpatialStatistics{1U, 1U, 3U, 0U, 0U, 2U, 2U},
        spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

TEST_CASE("OpenGL Studio entities share uploads and update bounded poses",
    "[renderer][opengl][entity-render][studio][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    REQUIRE(world_result.package);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));

    auto first = fixture::studio_scene(1U, 0.0F, 0U);
    auto first_scene = fixture::render_scene(first);
    first_scene.static_world.emplace(hlclient::renderer::RenderStaticWorld{
        world,
        hlclient::renderer::RenderCullMode::none,
        hlclient::renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    context->renderer().render(first_scene, {96, 96});
    const auto first_pixels = fixture::framebuffer();
    const auto first_statistics = context->renderer().entity_statistics();
    CHECK(first_statistics.studio_asset_upload_count == 1U);
    CHECK(first_statistics.sprite_asset_upload_count == 0U);
    CHECK(first_statistics.entity_frame_revision == 1U);
    CHECK(first_statistics.studio_draw_count > 0U);
    CHECK(first_statistics.pose_ubo_update_count >= 2U);
    CHECK(first_statistics.visible_entity_count == 2U);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(fixture::has_non_clear_pixel(first_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));

    auto second = fixture::SceneAndFrame{
        first.package,
        fixture::make_frame(*first.package, 2U, 2U, 0U, 1.25F, 0U, 1U),
    };
    auto second_scene = fixture::render_scene(second);
    second_scene.static_world.emplace(hlclient::renderer::RenderStaticWorld{
        world,
        hlclient::renderer::RenderCullMode::none,
        hlclient::renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    context->renderer().render(second_scene, {96, 96});
    const auto second_pixels = fixture::framebuffer();
    const auto second_statistics = context->renderer().entity_statistics();
    CHECK(second_statistics.studio_asset_upload_count == 1U);
    CHECK(second_statistics.entity_frame_revision == 2U);
    CHECK(second_statistics.studio_draw_count >
        first_statistics.studio_draw_count);
    CHECK(second_statistics.pose_ubo_update_count >
        first_statistics.pose_ubo_update_count);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(second_pixels != first_pixels);
    CHECK(glGetError() == GL_NO_ERROR);

    context->renderer().render({}, {96, 96});
    CHECK_FALSE(context->renderer().entity_statistics().active_entity_resources);
    CHECK(context->renderer()
              .entity_statistics()
              .entity_resource_release_count == 1U);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK_FALSE(context->renderer().statistics().active_world_resources);
    context->release_renderer();
}

TEST_CASE("OpenGL entity scene revisions reuse retained assets and preserve world",
    "[renderer][opengl][entity-render][cache][incremental][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    REQUIRE(world_result.package);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));

    auto studio_only_assets =
        hlclient::tests::entity_render_fixture::render_assets(true, false);
    auto mixed_assets =
        hlclient::tests::entity_render_fixture::render_assets(true, true);
    REQUIRE(studio_only_assets.studio);
    REQUIRE(mixed_assets.studio);
    REQUIRE(studio_only_assets.studio->resource_id() ==
        mixed_assets.studio->resource_id());
    REQUIRE(studio_only_assets.studio->resource_revision() ==
        mixed_assets.studio->resource_revision());
    auto studio_package_result =
        hlclient::tests::entity_render_fixture::scene_package(
            std::move(studio_only_assets));
    REQUIRE(studio_package_result);
    auto studio_package =
        std::make_shared<const hlclient::entity_render::EntitySceneRenderPackage>(
            std::move(*studio_package_result.package));
    fixture::SceneAndFrame first{
        studio_package,
        fixture::make_frame(*studio_package, 1U, 1U, 0U),
    };
    auto first_scene = fixture::render_scene(first);
    first_scene.static_world.emplace(hlclient::renderer::RenderStaticWorld{
        world,
        hlclient::renderer::RenderCullMode::none,
        hlclient::renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    context->renderer().render(first_scene, {96, 96});
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count ==
        1U);
    CHECK(context->renderer().entity_statistics().sprite_asset_upload_count ==
        0U);
    CHECK(context->renderer().statistics().upload_count == 1U);

    auto mixed_package_result = hlclient::tests::entity_render_fixture::
        scene_package(std::move(mixed_assets));
    REQUIRE(mixed_package_result);
    auto mixed_package =
        std::make_shared<const hlclient::entity_render::EntitySceneRenderPackage>(
            std::move(*mixed_package_result.package));
    fixture::SceneAndFrame second{
        mixed_package,
        fixture::make_frame(*mixed_package, 2U, 1U, 1U),
    };
    auto second_scene = fixture::render_scene(second);
    second_scene.static_world = first_scene.static_world;
    context->renderer().render(second_scene, {96, 96});

    const auto statistics = context->renderer().entity_statistics();
    CHECK(statistics.studio_asset_upload_count == 1U);
    CHECK(statistics.sprite_asset_upload_count == 1U);
    CHECK(statistics.entity_frame_revision == 2U);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(glGetError() == GL_NO_ERROR);

    context->release_renderer();
}

TEST_CASE("OpenGL Studio selects a nonzero skin family without reupload",
    "[renderer][opengl][entity-render][studio][skin][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto skin_zero = fixture::studio_scene(1U, 0.0F, 0U, 0U, true);
    context->renderer().render(fixture::render_scene(skin_zero), {96, 96});
    const auto zero_pixels = fixture::framebuffer();
    const auto first_statistics = context->renderer().entity_statistics();
    CHECK(fixture::has_non_clear_pixel(zero_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));
    const auto skin_zero_material =
        skin_zero.package->studio_assets()[0U]->select_material(0U, 0U);
    const auto skin_one_material =
        skin_zero.package->studio_assets()[0U]->select_material(0U, 1U);
    REQUIRE(skin_zero_material);
    REQUIRE(skin_one_material);
    CHECK(*skin_zero_material.material_index !=
        *skin_one_material.material_index);

    fixture::SceneAndFrame skin_one{
        skin_zero.package,
        fixture::make_frame(
            *skin_zero.package, 2U, 2U, 0U, 0.0F, 0U, 0U, 1U),
    };
    context->renderer().render(fixture::render_scene(skin_one), {96, 96});
    const auto one_pixels = fixture::framebuffer();
    const auto second_statistics = context->renderer().entity_statistics();
    CHECK(fixture::has_non_clear_pixel(one_pixels,
        {std::byte{5U}, std::byte{8U}, std::byte{10U}, std::byte{255U}}));
    CHECK(one_pixels != zero_pixels);
    CHECK(first_statistics.studio_asset_upload_count == 1U);
    CHECK(second_statistics.studio_asset_upload_count == 1U);
    CHECK(second_statistics.entity_frame_revision == 2U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("OpenGL Studio depth testing preserves an occluding world surface",
    "[renderer][opengl][entity-render][studio][world-depth][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto world_result = world_fixture::make_package();
    REQUIRE(world_result);
    REQUIRE(world_result.package);
    auto world = std::make_shared<const hlclient::world_render::WorldRenderPackage>(
        std::move(*world_result.package));
    auto entities = fixture::studio_scene();
    entities.frame = single_studio_frame(*entities.package,
        1U,
        {8.0F, 12.0F, -3.0F},
        {90.0F, 0.0F, 0.0F},
        2.0F,
        {{7.5F, 11.9F, -3.5F}, {10.5F, 12.1F, -0.5F}});
    auto entity_scene = fixture::render_scene(entities);
    entity_scene.camera.position = {8.0F, -12.0F, 6.0F};
    entity_scene.camera.target = {8.5F, 12.0F, -2.0F};

    auto clear_scene = entity_scene;
    clear_scene.dynamic_entities.reset();
    context->renderer().render(clear_scene, {96, 96});
    const auto clear_pixels = fixture::framebuffer();
    context->renderer().render(entity_scene, {96, 96});
    const auto entity_pixels = fixture::framebuffer();
    CHECK(entity_pixels != clear_pixels);

    auto world_scene = entity_scene;
    world_scene.dynamic_entities.reset();
    world_scene.static_world.emplace(renderer::RenderStaticWorld{
        world,
        renderer::RenderCullMode::none,
        renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    auto combined_scene = world_scene;
    combined_scene.dynamic_entities = entity_scene.dynamic_entities;
    context->renderer().render(combined_scene, {96, 96});
    const auto combined_pixels = fixture::framebuffer();

    context->renderer().render(world_scene, {96, 96});
    const auto world_pixels = fixture::framebuffer();
    CHECK(world_pixels != clear_pixels);
    CHECK(combined_pixels == world_pixels);
    CHECK(context->renderer().statistics().upload_count == 1U);
    CHECK(context->renderer().entity_statistics().studio_asset_upload_count ==
        1U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("OpenGL Studio consumes CPU PVS and frustum culling",
    "[renderer][opengl][entity-render][studio][visibility][actual-context]")
{
    auto context = fixture::try_context();
    if (!context || !fixture::capable_context()) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    auto entities = fixture::studio_scene();
    auto spatial_package = visibility_spatial_package();
    entities.frame = single_studio_frame(*entities.package,
        1U,
        {},
        {90.0F, 0.0F, 0.0F},
        1.0F,
        {{-0.5F, -0.1F, -0.1F}, {-0.1F, 0.1F, 0.1F}},
        0U,
        nullptr,
        &spatial_package,
        1U);
    CHECK(entities.frame->statistics().culled_by_pvs_count == 1U);
    CHECK(entities.frame->statistics().visible_count == 0U);
    CHECK(entities.frame->draw_commands().empty());

    auto clear_scene = fixture::render_scene(entities);
    clear_scene.dynamic_entities.reset();
    context->renderer().render(clear_scene, {96, 96});
    const auto clear_pixels = fixture::framebuffer();
    context->renderer().render(fixture::render_scene(entities), {96, 96});
    CHECK(fixture::framebuffer() == clear_pixels);
    CHECK(context->renderer().entity_statistics().studio_draw_count == 0U);

    renderer::RenderMatrix4 identity;
    auto made_frustum = visibility::WorldViewFrustum::from_view_projection(
        identity);
    REQUIRE(made_frustum);
    entities.frame = single_studio_frame(*entities.package,
        2U,
        {},
        {90.0F, 0.0F, 0.0F},
        1.0F,
        {{2.0F, 2.0F, 2.0F}, {3.0F, 3.0F, 3.0F}},
        0U,
        &*made_frustum.frustum);
    CHECK(entities.frame->statistics().culled_by_frustum_count == 1U);
    CHECK(entities.frame->statistics().visible_count == 0U);
    CHECK(entities.frame->draw_commands().empty());
    context->renderer().render(fixture::render_scene(entities), {96, 96});
    CHECK(fixture::framebuffer() == clear_pixels);
    CHECK(context->renderer().entity_statistics().studio_draw_count == 0U);
    CHECK(context->renderer().entity_statistics().entity_frame_revision == 2U);
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

} // namespace

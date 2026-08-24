#include "world_render_test_fixture.hpp"

#include <hlclient/world_render/world_render_package_builder.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

namespace assets = hlclient::assets;
namespace fixture = hlclient::tests::world_render_fixture;
namespace world_render = hlclient::world_render;

template <typename Type>
concept HasRendererHandle = requires(Type value) {
    value.vao;
    value.opengl_texture;
};

[[nodiscard]] world_render::WorldRenderPackageBuildResult make_two_material_package()
{
    auto world = fixture::make_world();
    world.bounds.maximum.x = 48.0F;
    const auto first_vertex = static_cast<std::uint32_t>(world.vertices.size());
    world.vertices.push_back(
        {{32.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}});
    world.vertices.push_back(
        {{48.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {16.0F, 0.0F}});
    world.vertices.push_back(
        {{48.0F, 16.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {16.0F, 16.0F}});
    world.vertices.push_back(
        {{32.0F, 16.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 16.0F}});
    world.indices.insert(world.indices.end(),
        {first_vertex, first_vertex + 1U, first_vertex + 2U,
            first_vertex, first_vertex + 2U, first_vertex + 3U});
    auto second_material = world.materials.front();
    second_material.texture_name = "{MASKED";
    second_material.source_texture_index = 1U;
    world.materials.push_back(second_material);
    auto second_surface = world.surfaces.front();
    second_surface.first_index = 6U;
    second_surface.material_index = 1U;
    second_surface.first_vertex = first_vertex;
    second_surface.source_surface_ordinal = 1U;
    second_surface.bounds = {{32.0F, 0.0F, 0.0F}, {48.0F, 16.0F, 0.0F}};
    world.surfaces.push_back(second_surface);

    auto opaque = fixture::make_texture(false);
    auto masked = fixture::make_texture(true);
    masked.source_bsp_texture_index = 1U;
    assets::WorldMaterialTextureBinding opaque_binding;
    opaque_binding.material_index = 0U;
    opaque_binding.status = assets::WorldMaterialTextureBindingStatus::resolved_embedded;
    opaque_binding.texture_asset_index = 0U;
    opaque_binding.source_bsp_texture_index = 0U;
    auto masked_binding = opaque_binding;
    masked_binding.material_index = 1U;
    masked_binding.texture_asset_index = 1U;
    masked_binding.source_bsp_texture_index = 1U;
    auto textures = assets::WorldTextureSet::create(
        {std::move(opaque), std::move(masked)},
        {opaque_binding, masked_binding},
        {},
        2U);
    if (!textures) {
        throw std::runtime_error{"Unable to build two-material texture fixture"};
    }

    assets::WorldSurfaceLightmapBinding first_lightmap;
    first_lightmap.surface_index = 0U;
    first_lightmap.status = assets::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
    first_lightmap.sample_width = 2U;
    first_lightmap.sample_height = 2U;
    auto second_lightmap = first_lightmap;
    second_lightmap.surface_index = 1U;
    auto lightmaps = assets::WorldLightmapSet::create(
        {}, {first_lightmap, second_lightmap}, 2U);
    if (!lightmaps) {
        throw std::runtime_error{"Unable to build two-material lightmap fixture"};
    }
    world_render::WorldRenderPackageBuilder builder;
    return builder.build(
        assets::TexturedWorldAsset{std::move(world), std::move(*textures.texture_set)},
        std::move(*lightmaps.lightmap_set));
}

TEST_CASE("World render package retains exact neutral geometry and UV bindings",
          "[world-render][package]")
{
    STATIC_REQUIRE(std::is_standard_layout_v<world_render::WorldRenderVertex>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<world_render::WorldRenderVertex>);
    STATIC_REQUIRE_FALSE(HasRendererHandle<world_render::WorldRenderPackage>);

    auto built = fixture::make_package();
    REQUIRE(built);
    REQUIRE(built.package);
    const auto& package = *built.package;
    CHECK(package.vertices().size() == 4U);
    CHECK(package.indices().size() == 6U);
    CHECK(package.materials().size() == 1U);
    CHECK(package.draw_batches().size() == 1U);
    CHECK(package.textured_world().textures.binding_count() ==
        package.textured_world().world.materials.size());
    CHECK(package.lightmaps().binding_count() ==
        package.textured_world().world.surfaces.size());

    CHECK(package.vertices()[0].base_texture_coordinate.x == 0.0F);
    CHECK(package.vertices()[0].base_texture_coordinate.y == 0.0F);
    CHECK(package.vertices()[2].base_texture_coordinate.x == 1.0F);
    CHECK(package.vertices()[2].base_texture_coordinate.y == 1.0F);
    CHECK(package.vertices()[0].lightmap_atlas_coordinate.x ==
        Catch::Approx(0.375F));
    CHECK(package.vertices()[0].lightmap_atlas_coordinate.y ==
        Catch::Approx(0.375F));
    CHECK(package.vertices()[2].lightmap_atlas_coordinate.x ==
        Catch::Approx(0.625F));
    CHECK(package.vertices()[2].lightmap_atlas_coordinate.y ==
        Catch::Approx(0.625F));
    CHECK(package.bounds().minimum.x == 0.0F);
    CHECK(package.bounds().maximum.x == 16.0F);
    CHECK(package.statistics().triangle_count == 2U);
    CHECK(package.statistics().source_surface_count == 1U);
    CHECK(package.resource_id() != 0U);
    CHECK(package.resource_revision() != 0U);
}

TEST_CASE("World render package supports unlit, masked and multiple-page materials",
          "[world-render][package]")
{
    SECTION("unlit surface uses the explicit white-lightmap mode")
    {
        auto built = fixture::make_package(fixture::FixtureOptions{
            false, true, 0U, 0U});
        REQUIRE(built);
        REQUIRE(built.package);
        CHECK(built.package->materials()[0].lightmap_mode ==
            world_render::WorldRenderLightmapMode::unlit_white);
        CHECK_FALSE(built.package->materials()[0].lightmap_atlas_page_index);
        CHECK(built.package->vertices()[0].lightmap_atlas_coordinate.x == 0.5F);
        CHECK(built.package->statistics().unlit_batch_count == 1U);
    }

    SECTION("masked texture retains masked alpha mode")
    {
        auto built = fixture::make_package(fixture::FixtureOptions{
            true, false, 0U, 1U});
        REQUIRE(built);
        REQUIRE(built.package);
        CHECK(built.package->materials()[0].base_texture_alpha_mode ==
            assets::WorldTextureAlphaMode::masked_index_255);
        CHECK(built.package->draw_batches()[0].alpha_mode ==
            assets::WorldTextureAlphaMode::masked_index_255);
    }

    SECTION("a resolved surface retains its exact atlas page")
    {
        auto built = fixture::make_package(fixture::FixtureOptions{
            false, false, 1U, 2U});
        REQUIRE(built);
        REQUIRE(built.package);
        REQUIRE(built.package->materials()[0].lightmap_atlas_page_index);
        CHECK(*built.package->materials()[0].lightmap_atlas_page_index == 1U);
        CHECK(built.package->lightmaps().page_count() == 2U);
    }

    SECTION("opaque and masked surfaces form separate batches")
    {
        auto built = make_two_material_package();
        REQUIRE(built);
        REQUIRE(built.package);
        CHECK(built.package->draw_batches().size() == 2U);
        CHECK(built.package->statistics().opaque_batch_count == 1U);
        CHECK(built.package->statistics().masked_batch_count == 1U);
        CHECK(built.package->statistics().triangle_count == 4U);
        CHECK(built.package->draw_batches()[0].first_index == 0U);
        CHECK(built.package->draw_batches()[1].first_index == 6U);
    }
}

TEST_CASE("World render package construction is deterministic and owning",
          "[world-render][package]")
{
    auto first = fixture::make_package();
    auto second = fixture::make_package();
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.package);
    REQUIRE(second.package);
    CHECK(first.package->resource_identity() == second.package->resource_identity());
    CHECK(std::ranges::equal(first.package->indices(), second.package->indices()));

    const auto expected_revision = first.package->resource_revision();
    auto retained = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*first.package));
    first.package.reset();
    CHECK(retained->resource_revision() == expected_revision);
    CHECK(retained->textured_world().textures.textures()[0]
            .mip_levels[0]
            .rgba_pixels[0] == std::byte{0x40});
    CHECK(retained->lightmaps().pages()[0]
            .style_slot_images[0]
            .rgba_pixels[0] == std::byte{0x10});
}

TEST_CASE("World render package variants repeat deterministically twenty times",
          "[world-render][package][repeat]")
{
    std::uint64_t baseline_revision = 0U;
    std::uint64_t unlit_masked_revision = 0U;
    std::uint64_t multiple_atlas_revision = 0U;

    for (std::size_t run = 0U; run < 20U; ++run) {
        auto baseline = fixture::make_package();
        REQUIRE(baseline);
        REQUIRE(baseline.package);
        CHECK(baseline.package->statistics().triangle_count == 2U);
        CHECK(baseline.package->draw_batches().size() == 1U);

        auto unlit_masked = fixture::make_package(
            fixture::FixtureOptions{true, true, 0U, 0U});
        REQUIRE(unlit_masked);
        REQUIRE(unlit_masked.package);
        CHECK(unlit_masked.package->materials()[0].base_texture_alpha_mode ==
            assets::WorldTextureAlphaMode::masked_index_255);
        CHECK(unlit_masked.package->materials()[0].lightmap_mode ==
            world_render::WorldRenderLightmapMode::unlit_white);

        auto multiple_atlas = fixture::make_package(
            fixture::FixtureOptions{false, false, 1U, 2U});
        REQUIRE(multiple_atlas);
        REQUIRE(multiple_atlas.package);
        CHECK(multiple_atlas.package->lightmaps().page_count() == 2U);
        REQUIRE(multiple_atlas.package->materials()[0]
                    .lightmap_atlas_page_index);
        CHECK(*multiple_atlas.package->materials()[0]
                   .lightmap_atlas_page_index == 1U);

        if (run == 0U) {
            baseline_revision = baseline.package->resource_revision();
            unlit_masked_revision =
                unlit_masked.package->resource_revision();
            multiple_atlas_revision =
                multiple_atlas.package->resource_revision();
        } else {
            CHECK(baseline.package->resource_revision() == baseline_revision);
            CHECK(unlit_masked.package->resource_revision() ==
                unlit_masked_revision);
            CHECK(multiple_atlas.package->resource_revision() ==
                multiple_atlas_revision);
        }
    }
}

TEST_CASE("World render package rejects incomplete or malformed prerequisites transactionally",
          "[world-render][package]")
{
    world_render::WorldRenderPackageBuilder builder;

    SECTION("incomplete texture set")
    {
        assets::WorldMaterialTextureBinding missing;
        missing.material_index = 0U;
        missing.status =
            assets::WorldMaterialTextureBindingStatus::external_texture_not_found;
        auto textures = assets::WorldTextureSet::create(
            {}, {missing}, {}, 1U);
        REQUIRE(textures);
        auto result = builder.build(
            {fixture::make_world(), std::move(*textures.texture_set)},
            fixture::make_lightmap_set({}));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            world_render::WorldRenderPackageErrorCode::texture_set_incomplete);
        CHECK_FALSE(result.package);
    }

    SECTION("lightmap binding cardinality mismatch")
    {
        auto empty_lightmaps = assets::WorldLightmapSet::create({}, {}, 0U);
        REQUIRE(empty_lightmaps);
        auto result = builder.build(
            fixture::make_textured_world(),
            std::move(*empty_lightmaps.lightmap_set));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            world_render::WorldRenderPackageErrorCode::lightmap_binding_mismatch);
    }

    SECTION("unused world materials and their textures are rejected")
    {
        auto world = fixture::make_world();
        auto unused_material = world.materials[0U];
        unused_material.source_texture_index = 1U;
        world.materials.push_back(unused_material);
        world.statistics.material_count = 2U;

        auto first_texture = fixture::make_texture(false);
        auto second_texture = fixture::make_texture(false);
        second_texture.source_bsp_texture_index = 1U;
        assets::WorldMaterialTextureBinding first_binding;
        first_binding.material_index = 0U;
        first_binding.status =
            assets::WorldMaterialTextureBindingStatus::resolved_embedded;
        first_binding.texture_asset_index = 0U;
        first_binding.source_bsp_texture_index = 0U;
        auto second_binding = first_binding;
        second_binding.material_index = 1U;
        second_binding.texture_asset_index = 1U;
        second_binding.source_bsp_texture_index = 1U;
        auto textures = assets::WorldTextureSet::create(
            {std::move(first_texture), std::move(second_texture)},
            {first_binding, second_binding},
            {},
            2U);
        REQUIRE(textures);

        auto result = builder.build(
            {std::move(world), std::move(*textures.texture_set)},
            fixture::make_lightmap_set({}));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            world_render::WorldRenderPackageErrorCode::invalid_material_binding);
        CHECK(result.error->element_index == 1U);
        CHECK_FALSE(result.package);
    }

    SECTION("world material metadata matches its resolved texture exactly")
    {
        auto textured = fixture::make_textured_world();
        SECTION("source texture identity")
        {
            textured.world.materials[0U].source_texture_index = 1U;
        }
        SECTION("source dimensions")
        {
            textured.world.materials[0U].width = 8U;
        }
        SECTION("storage class")
        {
            textured.world.materials[0U].texture_storage =
                assets::WorldTextureStorage::external_reference;
        }
        SECTION("closed material profile")
        {
            textured.world.materials[0U].compatibility_profile =
                static_cast<assets::WorldMaterialCompatibilityProfile>(0x7fU);
        }
        auto result = builder.build(
            std::move(textured), fixture::make_lightmap_set({}));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            world_render::WorldRenderPackageErrorCode::invalid_material_binding);
        CHECK_FALSE(result.package);
    }

    SECTION("overlapping face-local vertex range")
    {
        auto textured = fixture::make_textured_world();
        textured.world.surfaces[0].vertex_count = 5U;
        auto result = builder.build(
            std::move(textured), fixture::make_lightmap_set({}));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            world_render::WorldRenderPackageErrorCode::invalid_surface_range);
    }

    SECTION("native source identity")
    {
        auto textured = fixture::make_textured_world();
        textured.world.identity.source_name = "C:\\games\\map.bsp";
        auto result = builder.build(
            std::move(textured), fixture::make_lightmap_set({}));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            world_render::WorldRenderPackageErrorCode::invalid_prerequisite);
    }
}

TEST_CASE("World render package enforces exact output and retained-byte limits",
          "[world-render][package]")
{
    SECTION("vertex limit and limit plus one")
    {
        world_render::WorldRenderPackageLimits exact;
        exact.maximum_vertices = 4U;
        auto accepted = fixture::make_package({}, exact);
        REQUIRE(accepted);

        auto over = exact;
        over.maximum_vertices = 3U;
        auto rejected = fixture::make_package({}, over);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            world_render::WorldRenderPackageErrorCode::output_limit_exceeded);
    }

    SECTION("base texture byte limit and limit plus one")
    {
        world_render::WorldRenderPackageLimits exact;
        exact.maximum_base_texture_bytes = 1'360U;
        REQUIRE(fixture::make_package({}, exact));
        exact.maximum_base_texture_bytes = 1'359U;
        auto rejected = fixture::make_package({}, exact);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            world_render::WorldRenderPackageErrorCode::output_limit_exceeded);
    }

    SECTION("lightmap byte limit and limit plus one")
    {
        world_render::WorldRenderPackageLimits exact;
        exact.maximum_lightmap_bytes = 256U;
        REQUIRE(fixture::make_package({}, exact));
        exact.maximum_lightmap_bytes = 255U;
        auto rejected = fixture::make_package({}, exact);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
            world_render::WorldRenderPackageErrorCode::output_limit_exceeded);
    }
}

} // namespace

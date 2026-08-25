#include "goldsrc_lightmap_test_fixture.hpp"

#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lightmaps = hlclient::goldsrc::lightmaps;
namespace fixture = hlclient::tests::lightmap_fixture;

namespace {

[[nodiscard]] std::array<std::uint8_t, 4U> atlas_pixel(
    const hlclient::assets::WorldLightmapImage& image,
    const std::uint32_t x,
    const std::uint32_t y)
{
    const auto offset =
        (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return {
        std::to_integer<std::uint8_t>(image.rgba_pixels[offset]),
        std::to_integer<std::uint8_t>(image.rgba_pixels[offset + 1U]),
        std::to_integer<std::uint8_t>(image.rgba_pixels[offset + 2U]),
        std::to_integer<std::uint8_t>(image.rgba_pixels[offset + 3U]),
    };
}

} // namespace

TEST_CASE("GoldSrc lightmap import retains exact RGB and source style slots",
    "[goldsrc-lightmap][import]")
{
    SECTION("one 2x2 style-zero lightmap preserves byte and channel order")
    {
        const auto lighting = fixture::sequential_rgb_samples(4U);
        const auto bsp = fixture::make_bsp_with_lighting(lighting);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(), bsp);
        INFO((imported.error ? imported.error->context : std::string{}));
        REQUIRE(imported);
        const auto& set = *imported.lightmap_set;
        REQUIRE(set.binding_count() == 1U);
        REQUIRE(set.page_count() == 1U);
        const auto& binding = set.bindings()[0U];
        CHECK(binding.status ==
            hlclient::assets::WorldSurfaceLightmapBindingStatus::resolved);
        CHECK(binding.sample_width == 2U);
        CHECK(binding.sample_height == 2U);
        CHECK(binding.source_styles.style_count == 1U);
        CHECK(binding.source_styles.style_ids[0U] == 0U);
        CHECK(binding.selected_static_source_style_slot == 0U);
        const auto& image = set.pages()[0U].style_slot_images[0U];
        CHECK(atlas_pixel(image,
                  binding.inner_rectangle.x,
                  binding.inner_rectangle.y) ==
            std::array<std::uint8_t, 4U>{1U, 2U, 3U, 255U});
        CHECK(atlas_pixel(image,
                  binding.inner_rectangle.x + 1U,
                  binding.inner_rectangle.y) ==
            std::array<std::uint8_t, 4U>{4U, 5U, 6U, 255U});
        CHECK(atlas_pixel(image,
                  binding.inner_rectangle.x,
                  binding.inner_rectangle.y + 1U) ==
            std::array<std::uint8_t, 4U>{7U, 8U, 9U, 255U});
        CHECK(atlas_pixel(image,
                  binding.inner_rectangle.x + 1U,
                  binding.inner_rectangle.y + 1U) ==
            std::array<std::uint8_t, 4U>{10U, 11U, 12U, 255U});
    }

    SECTION("two style slots remain separate and are never summed")
    {
        std::vector<std::byte> lighting(24U);
        for (std::size_t index = 0U; index < 12U; ++index) {
            lighting[index] = std::byte{10U};
            lighting[12U + index] = std::byte{200U};
        }
        fixture::SurfaceDescription description;
        description.styles = fixture::styles({0U, 21U});
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting));
        REQUIRE(imported);
        const auto& binding = imported.lightmap_set->bindings()[0U];
        CHECK(binding.source_styles.style_count == 2U);
        CHECK(binding.source_styles.style_ids[0U] == 0U);
        CHECK(binding.source_styles.style_ids[1U] == 21U);
        const auto x = binding.inner_rectangle.x;
        const auto y = binding.inner_rectangle.y;
        CHECK(atlas_pixel(
                  imported.lightmap_set->pages()[0U].style_slot_images[0U], x, y) ==
            std::array<std::uint8_t, 4U>{10U, 10U, 10U, 255U});
        CHECK(atlas_pixel(
                  imported.lightmap_set->pages()[0U].style_slot_images[1U], x, y) ==
            std::array<std::uint8_t, 4U>{200U, 200U, 200U, 255U});
    }

    SECTION("all four source slots and their exact style IDs are retained")
    {
        const auto lighting = fixture::sequential_rgb_samples(16U);
        fixture::SurfaceDescription description;
        description.styles = fixture::styles({0U, 3U, 17U, 42U});
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting));
        REQUIRE(imported);
        const auto& styles = imported.lightmap_set->bindings()[0U].source_styles;
        CHECK(styles.style_count == 4U);
        CHECK(styles.style_ids == description.styles);
        CHECK(imported.lightmap_set->statistics().retained_source_style_count == 4U);
        CHECK(imported.lightmap_set->statistics().total_source_sample_count == 16U);
    }

    SECTION("255 terminates the active list even when later bytes are non-255")
    {
        fixture::SurfaceDescription description;
        description.styles = {0U, 0xFFU, 17U, 42U};
        const auto lighting = fixture::sequential_rgb_samples(4U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting));
        REQUIRE(imported);
        const auto& styles = imported.lightmap_set->bindings()[0U].source_styles;
        CHECK(styles.style_count == 1U);
        CHECK(styles.style_ids == fixture::styles({0U}));
    }

    SECTION("unused atlas layers are black with opaque alpha")
    {
        const auto lighting = fixture::sequential_rgb_samples(4U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(),
            fixture::make_bsp_with_lighting(lighting));
        REQUIRE(imported);
        const auto& binding = imported.lightmap_set->bindings()[0U];
        for (std::size_t slot = 1U; slot < 4U; ++slot) {
            CHECK(atlas_pixel(
                      imported.lightmap_set->pages()[0U].style_slot_images[slot],
                      binding.inner_rectangle.x,
                      binding.inner_rectangle.y) ==
                std::array<std::uint8_t, 4U>{0U, 0U, 0U, 255U});
        }
    }
}

TEST_CASE("GoldSrc lightmap import handles unlit and malformed metadata explicitly",
    "[goldsrc-lightmap][import][validation]")
{
    SECTION("surface without offset and styles is a valid unlit binding")
    {
        fixture::SurfaceDescription description;
        description.lightmap_offset.reset();
        description.styles = fixture::styles({});
        const std::vector<std::byte> empty_lighting;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(empty_lighting));
        REQUIRE(imported);
        CHECK(imported.lightmap_set->page_count() == 0U);
        REQUIRE(imported.lightmap_set->binding_count() == 1U);
        const auto& binding = imported.lightmap_set->bindings()[0U];
        CHECK(binding.status == hlclient::assets::
                WorldSurfaceLightmapBindingStatus::unlit_no_lightmap);
        CHECK_FALSE(binding.atlas_page_index.has_value());
        CHECK(binding.sample_width == 2U);
        CHECK(binding.sample_height == 2U);
        CHECK(imported.lightmap_set->complete_for_world_surfaces());
    }

    SECTION("active style without offset is inconsistent metadata")
    {
        fixture::SurfaceDescription description;
        description.lightmap_offset.reset();
        const std::vector<std::byte> empty_lighting;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(empty_lighting));
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::invalid_lightmap_metadata);
        CHECK(imported.error->binding_status == hlclient::assets::
                WorldSurfaceLightmapBindingStatus::invalid_metadata);
    }

    SECTION("raw face offset below minus one is rejected before publication")
    {
        hlclient::tests::SyntheticBspBuilder builder;
        hlclient::tests::SyntheticBspFace face;
        face.light_offset = -2;
        builder.set_faces(std::span{&face, 1U});
        const auto parsed = hlclient::goldsrc::bsp::GoldSrcBspParser::parse(
            builder.build());
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error);
        CHECK(parsed.error->code ==
            hlclient::goldsrc::bsp::GoldSrcBspErrorCode::invalid_light_offset);
    }

    SECTION("offset without an active style is inconsistent metadata")
    {
        fixture::SurfaceDescription description;
        description.styles = fixture::styles({});
        const auto lighting = fixture::sequential_rgb_samples(4U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting));
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::invalid_lightmap_metadata);
    }

    SECTION("configured style-profile limit produces an explicit status")
    {
        fixture::SurfaceDescription description;
        description.styles = fixture::styles({0U, 21U});
        const auto lighting = fixture::sequential_rgb_samples(8U);
        auto limits = lightmaps::GoldSrcWorldLightmapImportLimits{};
        limits.maximum_style_count = 1U;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting),
            limits);
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->binding_status == hlclient::assets::
                WorldSurfaceLightmapBindingStatus::unsupported_style_profile);
    }

    SECTION("offset at exact lighting-lump end is rejected")
    {
        auto description = fixture::SurfaceDescription{};
        description.lightmap_offset = 12U;
        const auto lighting = fixture::sequential_rgb_samples(4U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting));
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::lightmap_range_out_of_bounds);
    }

    SECTION("a one-byte truncated RGB range is rejected transactionally")
    {
        auto lighting = fixture::sequential_rgb_samples(4U);
        lighting.pop_back();
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(),
            fixture::make_bsp_with_lighting(lighting));
        REQUIRE_FALSE(imported);
        CHECK_FALSE(imported.lightmap_set.has_value());
        REQUIRE(imported.error);
        CHECK(imported.error->binding_status == hlclient::assets::
                WorldSurfaceLightmapBindingStatus::range_out_of_bounds);
    }

    SECTION("aggregate source sample limit is exact")
    {
        const auto lighting = fixture::sequential_rgb_samples(4U);
        auto limits = lightmaps::GoldSrcWorldLightmapImportLimits{};
        limits.maximum_total_source_samples = 4U;
        CHECK(lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(), fixture::make_bsp_with_lighting(lighting), limits));
        limits.maximum_total_source_samples = 3U;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(), fixture::make_bsp_with_lighting(lighting), limits);
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::source_sample_limit_exceeded);
    }

    SECTION("surface count limit is exact")
    {
        std::array descriptions{
            fixture::SurfaceDescription{}, fixture::SurfaceDescription{}};
        descriptions[1U].lightmap_offset = 12U;
        const auto lighting = fixture::sequential_rgb_samples(8U);
        auto limits = lightmaps::GoldSrcWorldLightmapImportLimits{};
        limits.maximum_surface_count = 2U;
        CHECK(lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(descriptions),
            fixture::make_bsp_with_lighting(lighting),
            limits));
        limits.maximum_surface_count = 1U;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(descriptions),
            fixture::make_bsp_with_lighting(lighting),
            limits);
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::surface_limit_exceeded);
    }

    SECTION("atlas RGBA memory limit is exact and no partial set is published")
    {
        const auto lighting = fixture::sequential_rgb_samples(4U);
        auto limits = lightmaps::GoldSrcWorldLightmapImportLimits{};
        // Default placement is 1024 x 4 pixels x RGBA x four style images.
        limits.maximum_total_atlas_rgba_bytes = 65'536U;
        CHECK(lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(), fixture::make_bsp_with_lighting(lighting), limits));
        limits.maximum_total_atlas_rgba_bytes = 65'535U;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(), fixture::make_bsp_with_lighting(lighting), limits);
        REQUIRE_FALSE(imported);
        CHECK_FALSE(imported.lightmap_set.has_value());
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::atlas_memory_limit_exceeded);
    }
}

TEST_CASE("Stock-sized wide lightmap faces use a bounded compatibility limit",
    "[goldsrc-lightmap][import][stock-compatibility]")
{
    constexpr std::size_t stock_derived_sample_width = 177U;
    constexpr std::size_t stock_derived_sample_height = 204U;
    constexpr std::size_t stock_derived_sample_count =
        stock_derived_sample_width * stock_derived_sample_height;
    fixture::SurfaceDescription description;
    description.maximum_s =
        static_cast<float>((stock_derived_sample_width - 1U) * 16U);
    description.maximum_t =
        static_cast<float>((stock_derived_sample_height - 1U) * 16U);
    const auto world = fixture::make_world(description);
    const auto bsp = fixture::make_bsp_with_lighting(
        fixture::sequential_rgb_samples(stock_derived_sample_count));

    const auto imported =
        lightmaps::GoldSrcWorldLightmapImporter::import(world, bsp);
    INFO((imported.error ? imported.error->context : std::string{}));
    REQUIRE(imported);
    REQUIRE(imported.lightmap_set->binding_count() == 1U);
    const auto& binding = imported.lightmap_set->bindings()[0U];
    CHECK(binding.sample_width == stock_derived_sample_width);
    CHECK(binding.sample_height == stock_derived_sample_height);
    CHECK(imported.lightmap_set->statistics().total_source_sample_count ==
        stock_derived_sample_count);

    auto limits = lightmaps::GoldSrcWorldLightmapImportLimits{};
    limits.maximum_samples_per_surface = stock_derived_sample_count;
    REQUIRE(lightmaps::GoldSrcWorldLightmapImporter::import(
        world, bsp, limits));
    limits.maximum_samples_per_surface = stock_derived_sample_count - 1U;
    const auto rejected =
        lightmaps::GoldSrcWorldLightmapImporter::import(world, bsp, limits);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == lightmaps::
        GoldSrcWorldLightmapImportErrorCode::invalid_lightmap_extent);
    CHECK(rejected.error->extent_code ==
        lightmaps::GoldSrcLightmapExtentErrorCode::sample_limit_exceeded);
}

TEST_CASE("GoldSrc lightmap bytes are copied without gamma or exposure",
    "[goldsrc-lightmap][color]")
{
    std::vector<std::byte> lighting(12U);
    for (std::size_t index = 0U; index < lighting.size(); index += 3U) {
        lighting[index] = std::byte{17U};
        lighting[index + 1U] = std::byte{93U};
        lighting[index + 2U] = std::byte{241U};
    }
    const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
        fixture::make_world(), fixture::make_bsp_with_lighting(lighting));
    REQUIRE(imported);
    const auto& binding = imported.lightmap_set->bindings()[0U];
    CHECK(atlas_pixel(imported.lightmap_set->pages()[0U].style_slot_images[0U],
              binding.inner_rectangle.x,
              binding.inner_rectangle.y) ==
        std::array<std::uint8_t, 4U>{17U, 93U, 241U, 255U});
}

TEST_CASE("Parsed BSP face-local ranges feed the lightmap importer directly",
    "[goldsrc-lightmap][import][bsp-integration]")
{
    constexpr std::array vertices{
        hlclient::tests::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
        hlclient::tests::SyntheticBspVector3{16.0F, 0.0F, 0.0F},
        hlclient::tests::SyntheticBspVector3{16.0F, 16.0F, 0.0F},
        hlclient::tests::SyntheticBspVector3{0.0F, 16.0F, 0.0F},
    };
    hlclient::tests::SyntheticBspBuilder builder;
    builder.set_convex_polygon(vertices);
    hlclient::tests::SyntheticBspFace face;
    face.light_styles = fixture::styles({0U});
    face.light_offset = 0;
    builder.set_faces(std::span{&face, 1U});
    builder.lump(hlclient::tests::SyntheticBspLumpId::lighting) =
        fixture::sequential_rgb_samples(4U);
    const auto bsp = builder.build();
    const auto parsed =
        hlclient::goldsrc::bsp::GoldSrcBspParser::parse(bsp);
    REQUIRE(parsed);
    const auto& world = parsed.document->world_asset;
    REQUIRE(world.surfaces.size() == 1U);
    CHECK(world.surfaces[0U].first_vertex == 0U);
    CHECK(world.surfaces[0U].vertex_count == 4U);

    const auto imported =
        lightmaps::GoldSrcWorldLightmapImporter::import(world, bsp);
    INFO((imported.error ? imported.error->context : std::string{}));
    REQUIRE(imported);
    REQUIRE(imported.lightmap_set->binding_count() == 1U);
    CHECK(imported.lightmap_set->bindings()[0U].sample_width == 2U);
    CHECK(imported.lightmap_set->bindings()[0U].sample_height == 2U);
}

#include "goldsrc_lightmap_test_fixture.hpp"

#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lightmaps = hlclient::goldsrc::lightmaps;
namespace fixture = hlclient::tests::lightmap_fixture;
using Catch::Approx;

namespace {

[[nodiscard]] lightmaps::GoldSrcWorldLightmapImportLimits atlas_limits(
    const std::uint32_t width = 8U,
    const std::uint32_t height = 8U,
    const std::size_t pages = 8U,
    const std::size_t bytes = 1024U * 1024U)
{
    auto limits = lightmaps::GoldSrcWorldLightmapImportLimits{};
    limits.atlas_width = width;
    limits.maximum_atlas_dimension = height;
    limits.maximum_atlas_pages = pages;
    limits.maximum_total_atlas_rgba_bytes = bytes;
    return limits;
}

[[nodiscard]] std::vector<fixture::SurfaceDescription> surfaces(
    const std::size_t count,
    const std::size_t bytes_per_surface = 12U)
{
    std::vector<fixture::SurfaceDescription> descriptions(count);
    for (std::size_t index = 0U; index < count; ++index) {
        descriptions[index].lightmap_offset =
            static_cast<std::uint32_t>(index * bytes_per_surface);
    }
    return descriptions;
}

[[nodiscard]] std::array<std::uint8_t, 4U> pixel(
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

[[nodiscard]] bool overlaps(
    const hlclient::assets::WorldLightmapRectangle& left,
    const hlclient::assets::WorldLightmapRectangle& right) noexcept
{
    return left.x < right.x + right.width && right.x < left.x + left.width &&
        left.y < right.y + right.height && right.y < left.y + left.height;
}

} // namespace

TEST_CASE("World lightmap atlas uses deterministic non-rotating shelf placement",
    "[world-lightmap][atlas][packing]")
{
    SECTION("one padded rectangle starts at the page origin")
    {
        const auto lighting = fixture::sequential_rgb_samples(4U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(),
            fixture::make_bsp_with_lighting(lighting),
            atlas_limits());
        REQUIRE(imported);
        REQUIRE(imported.lightmap_set->page_count() == 1U);
        const auto& page = imported.lightmap_set->pages()[0U];
        const auto& binding = imported.lightmap_set->bindings()[0U];
        CHECK(page.width == 8U);
        CHECK(page.height == 4U);
        CHECK(binding.padded_rectangle.x == 0U);
        CHECK(binding.padded_rectangle.y == 0U);
        CHECK(binding.padded_rectangle.width == 4U);
        CHECK(binding.padded_rectangle.height == 4U);
        CHECK(binding.inner_rectangle.x == 1U);
        CHECK(binding.inner_rectangle.y == 1U);
        CHECK(binding.inner_rectangle.width == 2U);
        CHECK(binding.inner_rectangle.height == 2U);
    }

    SECTION("multiple rectangles fill shelves in source-surface order")
    {
        const auto descriptions = surfaces(4U);
        const auto lighting = fixture::sequential_rgb_samples(16U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(descriptions),
            fixture::make_bsp_with_lighting(lighting),
            atlas_limits());
        REQUIRE(imported);
        REQUIRE(imported.lightmap_set->binding_count() == 4U);
        const auto bindings = imported.lightmap_set->bindings();
        CHECK(bindings[0U].padded_rectangle.x == 0U);
        CHECK(bindings[0U].padded_rectangle.y == 0U);
        CHECK(bindings[1U].padded_rectangle.x == 4U);
        CHECK(bindings[1U].padded_rectangle.y == 0U);
        CHECK(bindings[2U].padded_rectangle.x == 0U);
        CHECK(bindings[2U].padded_rectangle.y == 4U);
        CHECK(bindings[3U].padded_rectangle.x == 4U);
        CHECK(bindings[3U].padded_rectangle.y == 4U);
    }

    SECTION("a full page rolls over without changing surface order")
    {
        const auto descriptions = surfaces(5U);
        const auto lighting = fixture::sequential_rgb_samples(20U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(descriptions),
            fixture::make_bsp_with_lighting(lighting),
            atlas_limits());
        REQUIRE(imported);
        REQUIRE(imported.lightmap_set->page_count() == 2U);
        CHECK(imported.lightmap_set->bindings()[3U].atlas_page_index == 0U);
        CHECK(imported.lightmap_set->bindings()[4U].atlas_page_index == 1U);
        CHECK(imported.lightmap_set->bindings()[4U].padded_rectangle.x == 0U);
        CHECK(imported.lightmap_set->bindings()[4U].padded_rectangle.y == 0U);
    }

    SECTION("repeated packing produces byte-identical placement and pages")
    {
        const auto descriptions = surfaces(5U);
        const auto lighting = fixture::sequential_rgb_samples(20U);
        const auto bsp = fixture::make_bsp_with_lighting(lighting);
        const auto world = fixture::make_world(descriptions);
        const auto first = lightmaps::GoldSrcWorldLightmapImporter::import(
            world, bsp, atlas_limits());
        const auto second = lightmaps::GoldSrcWorldLightmapImporter::import(
            world, bsp, atlas_limits());
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(first.lightmap_set->page_count() ==
            second.lightmap_set->page_count());
        REQUIRE(first.lightmap_set->binding_count() ==
            second.lightmap_set->binding_count());
        for (std::size_t index = 0U;
             index < first.lightmap_set->binding_count();
             ++index) {
            const auto& left = first.lightmap_set->bindings()[index];
            const auto& right = second.lightmap_set->bindings()[index];
            CHECK(left.atlas_page_index == right.atlas_page_index);
            CHECK(left.padded_rectangle.x == right.padded_rectangle.x);
            CHECK(left.padded_rectangle.y == right.padded_rectangle.y);
            CHECK(left.inner_rectangle.x == right.inner_rectangle.x);
            CHECK(left.inner_rectangle.y == right.inner_rectangle.y);
        }
        for (std::size_t page = 0U; page < first.lightmap_set->page_count();
             ++page) {
            for (std::size_t slot = 0U; slot < 4U; ++slot) {
                CHECK(first.lightmap_set->pages()[page]
                          .style_slot_images[slot]
                          .rgba_pixels ==
                    second.lightmap_set->pages()[page]
                        .style_slot_images[slot]
                        .rgba_pixels);
            }
        }
    }

    SECTION("rectangles are never rotated to make an invalid width fit")
    {
        fixture::SurfaceDescription description;
        description.maximum_s = 64.0F; // 5x2 inner; 7x4 padded.
        const auto lighting = fixture::sequential_rgb_samples(10U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting),
            atlas_limits(6U, 8U));
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::
                    atlas_rectangle_limit_exceeded);
        CHECK_FALSE(imported.lightmap_set.has_value());
    }
}

TEST_CASE("World lightmap atlas duplicates each valid edge and corner",
    "[world-lightmap][atlas][padding]")
{
    const std::vector<std::byte> lighting{
        std::byte{1U}, std::byte{2U}, std::byte{3U},
        std::byte{4U}, std::byte{5U}, std::byte{6U},
        std::byte{7U}, std::byte{8U}, std::byte{9U},
        std::byte{10U}, std::byte{11U}, std::byte{12U},
    };
    const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
        fixture::make_world(),
        fixture::make_bsp_with_lighting(lighting),
        atlas_limits());
    REQUIRE(imported);
    const auto& binding = imported.lightmap_set->bindings()[0U];
    const auto& image = imported.lightmap_set->pages()[0U].style_slot_images[0U];

    CHECK(binding.inner_rectangle.x - binding.padded_rectangle.x == 1U);
    CHECK(binding.inner_rectangle.y - binding.padded_rectangle.y == 1U);
    CHECK(binding.padded_rectangle.width - binding.inner_rectangle.width == 2U);
    CHECK(binding.padded_rectangle.height - binding.inner_rectangle.height == 2U);

    const auto top_left = std::array<std::uint8_t, 4U>{1U, 2U, 3U, 255U};
    const auto top_right = std::array<std::uint8_t, 4U>{4U, 5U, 6U, 255U};
    const auto bottom_left = std::array<std::uint8_t, 4U>{7U, 8U, 9U, 255U};
    const auto bottom_right =
        std::array<std::uint8_t, 4U>{10U, 11U, 12U, 255U};
    CHECK(pixel(image, 0U, 0U) == top_left);
    CHECK(pixel(image, 1U, 0U) == top_left);
    CHECK(pixel(image, 2U, 0U) == top_right);
    CHECK(pixel(image, 3U, 0U) == top_right);
    CHECK(pixel(image, 0U, 1U) == top_left);
    CHECK(pixel(image, 3U, 1U) == top_right);
    CHECK(pixel(image, 0U, 2U) == bottom_left);
    CHECK(pixel(image, 3U, 2U) == bottom_right);
    CHECK(pixel(image, 0U, 3U) == bottom_left);
    CHECK(pixel(image, 1U, 3U) == bottom_left);
    CHECK(pixel(image, 2U, 3U) == bottom_right);
    CHECK(pixel(image, 3U, 3U) == bottom_right);
}

TEST_CASE("All four atlas style images share dimensions and placement",
    "[world-lightmap][atlas][styles]")
{
    fixture::SurfaceDescription description;
    description.styles = fixture::styles({0U, 1U, 2U, 3U});
    std::vector<std::byte> lighting;
    lighting.reserve(48U);
    for (std::uint8_t slot = 0U; slot < 4U; ++slot) {
        for (std::size_t sample = 0U; sample < 4U; ++sample) {
            lighting.push_back(static_cast<std::byte>(10U + slot));
            lighting.push_back(static_cast<std::byte>(20U + slot));
            lighting.push_back(static_cast<std::byte>(30U + slot));
        }
    }
    const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
        fixture::make_world(description),
        fixture::make_bsp_with_lighting(lighting),
        atlas_limits());
    REQUIRE(imported);
    const auto& page = imported.lightmap_set->pages()[0U];
    const auto& binding = imported.lightmap_set->bindings()[0U];
    for (std::uint8_t slot = 0U; slot < 4U; ++slot) {
        const auto& image = page.style_slot_images[slot];
        CHECK(image.width == page.width);
        CHECK(image.height == page.height);
        CHECK(pixel(image,
                  binding.inner_rectangle.x,
                  binding.inner_rectangle.y) ==
            std::array<std::uint8_t, 4U>{static_cast<std::uint8_t>(10U + slot),
                static_cast<std::uint8_t>(20U + slot),
                static_cast<std::uint8_t>(30U + slot),
                255U});
    }
}

TEST_CASE("World lightmap atlas enforces page and byte limits transactionally",
    "[world-lightmap][atlas][limits]")
{
    SECTION("oversized rectangle is a typed failure")
    {
        fixture::SurfaceDescription description;
        description.maximum_s = 48.0F; // 4x2 inner; 6x4 padded.
        const auto lighting = fixture::sequential_rgb_samples(8U);
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(description),
            fixture::make_bsp_with_lighting(lighting),
            atlas_limits(4U, 4U));
        REQUIRE_FALSE(imported);
        REQUIRE(imported.error);
        CHECK(imported.error->binding_status == hlclient::assets::
                WorldSurfaceLightmapBindingStatus::atlas_limit_exceeded);
    }

    SECTION("page limit fails only on the first surface requiring page plus one")
    {
        const auto descriptions = surfaces(2U);
        const auto lighting = fixture::sequential_rgb_samples(8U);
        auto limits = atlas_limits(4U, 4U, 2U);
        REQUIRE(lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(descriptions),
            fixture::make_bsp_with_lighting(lighting),
            limits));
        limits.maximum_atlas_pages = 1U;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(descriptions),
            fixture::make_bsp_with_lighting(lighting),
            limits);
        REQUIRE_FALSE(imported);
        CHECK_FALSE(imported.lightmap_set.has_value());
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::atlas_page_limit_exceeded);
        CHECK(imported.error->surface_index == 1U);
    }

    SECTION("total four-layer RGBA limit accepts exact bytes and rejects one less")
    {
        const auto lighting = fixture::sequential_rgb_samples(4U);
        auto limits = atlas_limits(4U, 4U, 1U, 256U);
        REQUIRE(lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(),
            fixture::make_bsp_with_lighting(lighting),
            limits));
        limits.maximum_total_atlas_rgba_bytes = 255U;
        const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
            fixture::make_world(),
            fixture::make_bsp_with_lighting(lighting),
            limits);
        REQUIRE_FALSE(imported);
        CHECK_FALSE(imported.lightmap_set.has_value());
        REQUIRE(imported.error);
        CHECK(imported.error->code == lightmaps::
                GoldSrcWorldLightmapImportErrorCode::atlas_memory_limit_exceeded);
    }
}

TEST_CASE("World lightmap atlas UVs address source texel centers without overlap",
    "[world-lightmap][atlas][uv]")
{
    const auto descriptions = surfaces(4U);
    const auto lighting = fixture::sequential_rgb_samples(16U);
    const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
        fixture::make_world(descriptions),
        fixture::make_bsp_with_lighting(lighting),
        atlas_limits());
    REQUIRE(imported);
    const auto& page = imported.lightmap_set->pages()[0U];
    const auto& first = imported.lightmap_set->bindings()[0U];
    const auto first_u =
        (static_cast<double>(first.inner_rectangle.x) + 0.5) / page.width;
    const auto first_v =
        (static_cast<double>(first.inner_rectangle.y) + 0.5) / page.height;
    const auto last_u =
        (static_cast<double>(first.inner_rectangle.x) +
            first.sample_width - 1.0 + 0.5) /
        page.width;
    const auto last_v =
        (static_cast<double>(first.inner_rectangle.y) +
            first.sample_height - 1.0 + 0.5) /
        page.height;
    CHECK(first_u == Approx(1.5 / 8.0));
    CHECK(first_v == Approx(1.5 / 8.0));
    CHECK(last_u == Approx(2.5 / 8.0));
    CHECK(last_v == Approx(2.5 / 8.0));

    const auto bindings = imported.lightmap_set->bindings();
    for (std::size_t left = 0U; left < bindings.size(); ++left) {
        for (std::size_t right = left + 1U; right < bindings.size(); ++right) {
            if (bindings[left].atlas_page_index ==
                bindings[right].atlas_page_index) {
                CHECK_FALSE(overlaps(bindings[left].padded_rectangle,
                    bindings[right].padded_rectangle));
            }
        }
    }
}

TEST_CASE("World lightmap set factory enforces padded atlas publication invariants",
    "[world-lightmap][atlas][validation]")
{
    const auto imported = lightmaps::GoldSrcWorldLightmapImporter::import(
        fixture::make_world(),
        fixture::make_bsp_with_lighting(fixture::sequential_rgb_samples(4U)),
        atlas_limits());
    REQUIRE(imported);
    std::vector<hlclient::assets::WorldLightmapAtlasPage> pages{
        imported.lightmap_set->pages().begin(),
        imported.lightmap_set->pages().end()};
    std::vector<hlclient::assets::WorldSurfaceLightmapBinding> bindings{
        imported.lightmap_set->bindings().begin(),
        imported.lightmap_set->bindings().end()};

    SECTION("zero-width padding is rejected")
    {
        bindings[0U].padded_rectangle = bindings[0U].inner_rectangle;
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 1U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_surface_binding);
    }

    SECTION("a non-duplicated border pixel is rejected in every style layer")
    {
        pages[0U].style_slot_images[0U].rgba_pixels[0U] = std::byte{0x7fU};
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 1U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_surface_binding);
    }

    SECTION("every atlas pixel requires opaque alpha")
    {
        pages[0U].style_slot_images[0U].rgba_pixels[3U] = std::byte{0U};
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 1U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_atlas_page);
    }

    SECTION("unused style slots retain zero RGB inside occupied rectangles")
    {
        const auto offset =
            (static_cast<std::size_t>(bindings[0U].inner_rectangle.y) *
                    pages[0U].width +
                bindings[0U].inner_rectangle.x) *
            4U;
        pages[0U].style_slot_images[1U].rgba_pixels[offset] = std::byte{1U};
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 1U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_surface_binding);
    }

    SECTION("unoccupied atlas texels retain zero RGB")
    {
        const auto unoccupied_offset =
            static_cast<std::size_t>(bindings[0U].padded_rectangle.width) * 4U;
        pages[0U].style_slot_images[0U].rgba_pixels[unoccupied_offset] =
            std::byte{1U};
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 1U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_atlas_page);
    }

    SECTION("source style IDs require the exact 255 terminator profile")
    {
        bindings[0U].source_styles.style_ids[1U] = 1U;
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 1U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_surface_binding);
    }

    SECTION("retained atlas pages require at least one resolved binding")
    {
        pages.push_back(pages[0U]);
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 1U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_atlas_page);
        CHECK(created.error->element_index == 1U);
        CHECK_FALSE(created.lightmap_set.has_value());
    }

    SECTION("padded rectangles on one page cannot overlap")
    {
        auto second = bindings[0U];
        second.surface_index = 1U;
        bindings.push_back(second);
        const auto created = hlclient::assets::WorldLightmapSet::create(
            std::move(pages), std::move(bindings), 2U);
        REQUIRE_FALSE(created);
        REQUIRE(created.error);
        CHECK(created.error->code == hlclient::assets::
                WorldLightmapSetErrorCode::invalid_surface_binding);
        CHECK(created.error->element_index == 1U);
    }
}

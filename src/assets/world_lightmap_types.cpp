#include <hlclient/assets/world_lightmap_types.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::assets {
namespace {

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] WorldLightmapSetCreateResult fail(
    const WorldLightmapSetErrorCode code,
    const std::optional<std::size_t> index,
    std::string context)
{
    return WorldLightmapSetCreateResult{
        std::nullopt,
        WorldLightmapSetError{code, index, std::move(context)},
    };
}

[[nodiscard]] bool rectangle_in_page(
    const WorldLightmapRectangle& rectangle,
    const WorldLightmapAtlasPage& page) noexcept
{
    if (rectangle.width == 0U || rectangle.height == 0U ||
        rectangle.x > page.width || rectangle.y > page.height) {
        return false;
    }
    return rectangle.width <= page.width - rectangle.x &&
        rectangle.height <= page.height - rectangle.y;
}

[[nodiscard]] bool empty_rectangle(
    const WorldLightmapRectangle& rectangle) noexcept
{
    return rectangle.x == 0U && rectangle.y == 0U && rectangle.width == 0U &&
        rectangle.height == 0U;
}

[[nodiscard]] bool valid_style_terminator_profile(
    const WorldSurfaceLightStyles& styles) noexcept
{
    if (styles.style_count > kWorldLightmapStyleSlotCount) {
        return false;
    }
    for (std::size_t slot = 0U; slot < styles.style_ids.size(); ++slot) {
        const bool retained_style = slot < styles.style_count;
        if (retained_style == (styles.style_ids[slot] == 0xFFU)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool equal_rgba_pixel(
    const WorldLightmapImage& image,
    const std::uint32_t left_x,
    const std::uint32_t left_y,
    const std::uint32_t right_x,
    const std::uint32_t right_y) noexcept
{
    const auto left_offset =
        (static_cast<std::size_t>(left_y) * image.width + left_x) * 4U;
    const auto right_offset =
        (static_cast<std::size_t>(right_y) * image.width + right_x) * 4U;
    return std::equal(image.rgba_pixels.begin() + left_offset,
        image.rgba_pixels.begin() + left_offset + 4U,
        image.rgba_pixels.begin() + right_offset);
}

[[nodiscard]] bool black_rgb_pixel(
    const WorldLightmapImage& image,
    const std::uint32_t x,
    const std::uint32_t y) noexcept
{
    const auto offset =
        (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return image.rgba_pixels[offset] == std::byte{0U} &&
        image.rgba_pixels[offset + 1U] == std::byte{0U} &&
        image.rgba_pixels[offset + 2U] == std::byte{0U};
}

} // namespace

std::string_view to_string(const WorldLightmapSetErrorCode code) noexcept
{
    switch (code) {
    case WorldLightmapSetErrorCode::invalid_configuration:
        return "invalid_configuration";
    case WorldLightmapSetErrorCode::binding_count_mismatch:
        return "binding_count_mismatch";
    case WorldLightmapSetErrorCode::atlas_page_count_limit_exceeded:
        return "atlas_page_count_limit_exceeded";
    case WorldLightmapSetErrorCode::invalid_atlas_page:
        return "invalid_atlas_page";
    case WorldLightmapSetErrorCode::invalid_surface_binding:
        return "invalid_surface_binding";
    case WorldLightmapSetErrorCode::total_atlas_rgba_bytes_limit_exceeded:
        return "total_atlas_rgba_bytes_limit_exceeded";
    case WorldLightmapSetErrorCode::unable_to_retain_lightmap_set:
        return "unable_to_retain_lightmap_set";
    }
    return "unknown";
}

WorldLightmapSet::WorldLightmapSet(
    std::vector<WorldLightmapAtlasPage> pages,
    std::vector<WorldSurfaceLightmapBinding> bindings,
    WorldLightmapStatistics statistics) noexcept
    : pages_{std::move(pages)},
      bindings_{std::move(bindings)},
      statistics_{statistics}
{
}

WorldLightmapSetCreateResult WorldLightmapSet::create(
    std::vector<WorldLightmapAtlasPage> pages,
    std::vector<WorldSurfaceLightmapBinding> bindings,
    const std::size_t expected_surface_count,
    const WorldLightmapSetLimits& limits)
{
    if (limits.maximum_surface_binding_count == 0U ||
        limits.maximum_atlas_page_count == 0U ||
        limits.maximum_atlas_dimension == 0U ||
        limits.maximum_total_atlas_rgba_bytes == 0U) {
        return fail(WorldLightmapSetErrorCode::invalid_configuration,
            std::nullopt,
            "World lightmap-set limits must all be positive");
    }
    if (expected_surface_count > limits.maximum_surface_binding_count ||
        bindings.size() != expected_surface_count) {
        return fail(WorldLightmapSetErrorCode::binding_count_mismatch,
            bindings.size(),
            "Surface binding count does not equal the expected world surface count");
    }
    if (pages.size() > limits.maximum_atlas_page_count) {
        return fail(WorldLightmapSetErrorCode::atlas_page_count_limit_exceeded,
            pages.size(),
            "Lightmap atlas page count exceeds the configured limit");
    }

    WorldLightmapStatistics statistics;
    statistics.surface_binding_count = bindings.size();
    statistics.atlas_page_count = pages.size();
    std::size_t total_rgba_bytes = 0U;

    for (std::size_t page_index = 0U; page_index < pages.size(); ++page_index) {
        const auto& page = pages[page_index];
        if (page.width == 0U || page.height == 0U ||
            page.width > limits.maximum_atlas_dimension ||
            page.height > limits.maximum_atlas_dimension) {
            return fail(WorldLightmapSetErrorCode::invalid_atlas_page,
                page_index,
                "Atlas page dimensions are zero or exceed the configured limit");
        }
        std::size_t pixel_count = 0U;
        std::size_t expected_bytes = 0U;
        if (!checked_multiply(static_cast<std::size_t>(page.width),
                static_cast<std::size_t>(page.height), pixel_count) ||
            !checked_multiply(pixel_count, 4U, expected_bytes)) {
            return fail(WorldLightmapSetErrorCode::invalid_atlas_page,
                page_index,
                "Atlas page RGBA size overflows the host size type");
        }
        for (const auto& image : page.style_slot_images) {
            if (image.width != page.width || image.height != page.height ||
                image.pixel_format != WorldLightmapPixelFormat::rgba8 ||
                image.rgba_pixels.size() != expected_bytes) {
                return fail(WorldLightmapSetErrorCode::invalid_atlas_page,
                    page_index,
                    "All four style images must have identical dimensions and exact RGBA storage");
            }
            for (std::size_t alpha = 3U; alpha < image.rgba_pixels.size();
                 alpha += 4U) {
                if (image.rgba_pixels[alpha] != std::byte{0xFFU}) {
                    return fail(WorldLightmapSetErrorCode::invalid_atlas_page,
                        page_index,
                        "Every retained lightmap texel must have opaque alpha");
                }
            }
            if (!checked_add(total_rgba_bytes, expected_bytes, total_rgba_bytes) ||
                total_rgba_bytes > limits.maximum_total_atlas_rgba_bytes) {
                return fail(
                    WorldLightmapSetErrorCode::total_atlas_rgba_bytes_limit_exceeded,
                    page_index,
                    "Aggregate atlas RGBA storage exceeds the configured limit");
            }
        }
    }

    try {
        std::vector<std::vector<std::uint8_t>> occupied_pages;
        std::vector<std::uint8_t> referenced_pages(
            pages.size(), std::uint8_t{0U});
        occupied_pages.reserve(pages.size());
        for (const auto& page : pages) {
            occupied_pages.emplace_back(
                static_cast<std::size_t>(page.width) * page.height,
                std::uint8_t{0U});
        }

        for (std::size_t binding_index = 0U; binding_index < bindings.size();
             ++binding_index) {
            const auto& binding = bindings[binding_index];
            if (binding.surface_index != binding_index ||
                !is_renderable(binding.status) ||
                binding.selected_static_source_style_slot != 0U ||
                !valid_style_terminator_profile(binding.source_styles) ||
                binding.compatibility_profile !=
                    WorldLightmapCompatibilityProfile::goldsrc_rgb_lightmap_v1 ||
                binding.evidence_profile != WorldLightmapEvidenceProfile::
                                                valve_public_tools_and_synthetic_fixtures) {
                return fail(WorldLightmapSetErrorCode::invalid_surface_binding,
                    binding_index,
                    "Surface binding order, status, style profile or baseline slot is invalid");
            }
            if (binding.status ==
                WorldSurfaceLightmapBindingStatus::unlit_no_lightmap) {
                if (binding.atlas_page_index || binding.sample_width == 0U ||
                    binding.sample_height == 0U ||
                    binding.source_styles.style_count != 0U ||
                    !empty_rectangle(binding.inner_rectangle) ||
                    !empty_rectangle(binding.padded_rectangle)) {
                    return fail(WorldLightmapSetErrorCode::invalid_surface_binding,
                        binding_index,
                        "Unlit binding must retain extents without atlas data, rectangles or source styles");
                }
                ++statistics.unlit_surface_count;
                continue;
            }

            if (!binding.atlas_page_index ||
                *binding.atlas_page_index >= pages.size() ||
                binding.source_styles.style_count == 0U ||
                binding.sample_width != binding.inner_rectangle.width ||
                binding.sample_height != binding.inner_rectangle.height) {
                return fail(WorldLightmapSetErrorCode::invalid_surface_binding,
                    binding_index,
                    "Resolved binding does not reference a valid page, inner size or style profile");
            }
            const auto page_index = *binding.atlas_page_index;
            referenced_pages[page_index] = 1U;
            const auto& page = pages[page_index];
            if (!rectangle_in_page(binding.inner_rectangle, page) ||
                !rectangle_in_page(binding.padded_rectangle, page) ||
                binding.inner_rectangle.x != binding.padded_rectangle.x + 1U ||
                binding.inner_rectangle.y != binding.padded_rectangle.y + 1U ||
                binding.padded_rectangle.width !=
                    binding.inner_rectangle.width + 2U ||
                binding.padded_rectangle.height !=
                    binding.inner_rectangle.height + 2U) {
                return fail(WorldLightmapSetErrorCode::invalid_surface_binding,
                    binding_index,
                    "Resolved binding must retain an exact one-texel padded atlas placement");
            }

            const auto padded_end_x = binding.padded_rectangle.x +
                binding.padded_rectangle.width;
            const auto padded_end_y = binding.padded_rectangle.y +
                binding.padded_rectangle.height;
            const auto inner_end_x =
                binding.inner_rectangle.x + binding.inner_rectangle.width;
            const auto inner_end_y =
                binding.inner_rectangle.y + binding.inner_rectangle.height;
            auto& occupied = occupied_pages[page_index];
            for (std::uint32_t y = binding.padded_rectangle.y; y < padded_end_y;
                 ++y) {
                for (std::uint32_t x = binding.padded_rectangle.x;
                     x < padded_end_x;
                     ++x) {
                    const auto pixel_index =
                        static_cast<std::size_t>(y) * page.width + x;
                    if (occupied[pixel_index] != 0U) {
                        return fail(WorldLightmapSetErrorCode::invalid_surface_binding,
                            binding_index,
                            "Resolved padded atlas rectangles overlap");
                    }
                    occupied[pixel_index] = 1U;

                    const auto source_x = std::clamp(x,
                        binding.inner_rectangle.x,
                        inner_end_x - 1U);
                    const auto source_y = std::clamp(y,
                        binding.inner_rectangle.y,
                        inner_end_y - 1U);
                    for (const auto& image : page.style_slot_images) {
                        if (!equal_rgba_pixel(image, x, y, source_x, source_y)) {
                            return fail(
                                WorldLightmapSetErrorCode::invalid_surface_binding,
                                binding_index,
                                "Every atlas style layer must duplicate the exact one-texel border");
                        }
                    }
                    for (std::size_t style_slot =
                             binding.source_styles.style_count;
                         style_slot < page.style_slot_images.size();
                         ++style_slot) {
                        if (!black_rgb_pixel(
                                page.style_slot_images[style_slot], x, y)) {
                            return fail(
                                WorldLightmapSetErrorCode::invalid_surface_binding,
                                binding_index,
                                "Unused surface style slots must retain zero RGB texels");
                        }
                    }
                }
            }

            ++statistics.resolved_surface_count;
            statistics.retained_source_style_count +=
                binding.source_styles.style_count;
            std::size_t sample_count = 0U;
            if (!checked_multiply(static_cast<std::size_t>(binding.sample_width),
                    static_cast<std::size_t>(binding.sample_height), sample_count) ||
                !checked_multiply(sample_count,
                    static_cast<std::size_t>(binding.source_styles.style_count),
                    sample_count) ||
                !checked_add(statistics.total_source_sample_count,
                    sample_count,
                    statistics.total_source_sample_count)) {
                return fail(WorldLightmapSetErrorCode::invalid_surface_binding,
                    binding_index,
                    "Resolved binding source sample count overflows the host size type");
            }
        }
        for (std::size_t page_index = 0U; page_index < pages.size();
             ++page_index) {
            const auto& page = pages[page_index];
            const auto& occupied = occupied_pages[page_index];
            for (std::uint32_t y = 0U; y < page.height; ++y) {
                for (std::uint32_t x = 0U; x < page.width; ++x) {
                    const auto pixel_index =
                        static_cast<std::size_t>(y) * page.width + x;
                    if (occupied[pixel_index] != 0U) {
                        continue;
                    }
                    for (const auto& image : page.style_slot_images) {
                        if (!black_rgb_pixel(image, x, y)) {
                            return fail(
                                WorldLightmapSetErrorCode::invalid_atlas_page,
                                page_index,
                                "Unoccupied atlas texels must retain zero RGB values");
                        }
                    }
                }
            }
        }
        for (std::size_t page_index = 0U; page_index < referenced_pages.size();
             ++page_index) {
            if (referenced_pages[page_index] == 0U) {
                return fail(WorldLightmapSetErrorCode::invalid_atlas_page,
                    page_index,
                    "Every retained atlas page must be referenced by a surface binding");
            }
        }
        statistics.total_atlas_rgba_byte_count = total_rgba_bytes;

        return WorldLightmapSetCreateResult{
            WorldLightmapSet{
                std::move(pages), std::move(bindings), statistics},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(WorldLightmapSetErrorCode::unable_to_retain_lightmap_set,
            std::nullopt,
            "Unable to retain the owning world lightmap set");
    } catch (const std::length_error&) {
        return fail(WorldLightmapSetErrorCode::unable_to_retain_lightmap_set,
            std::nullopt,
            "World lightmap set exceeds an owning container limit");
    }
}

std::span<const WorldLightmapAtlasPage> WorldLightmapSet::pages() const noexcept
{
    return pages_;
}

std::span<const WorldSurfaceLightmapBinding> WorldLightmapSet::bindings()
    const noexcept
{
    return bindings_;
}

std::size_t WorldLightmapSet::page_count() const noexcept
{
    return pages_.size();
}

std::size_t WorldLightmapSet::binding_count() const noexcept
{
    return bindings_.size();
}

const WorldSurfaceLightmapBinding* WorldLightmapSet::binding_for_surface(
    const std::size_t surface_index) const noexcept
{
    if (surface_index >= bindings_.size() ||
        bindings_[surface_index].surface_index != surface_index) {
        return nullptr;
    }
    return &bindings_[surface_index];
}

bool WorldLightmapSet::complete_for_world_surfaces() const noexcept
{
    return std::ranges::all_of(bindings_, [](const auto& binding) {
        return is_renderable(binding.status);
    });
}

const WorldLightmapStatistics& WorldLightmapSet::statistics() const noexcept
{
    return statistics_;
}

WorldLightmapCompatibilityProfile WorldLightmapSet::compatibility_profile()
    const noexcept
{
    return WorldLightmapCompatibilityProfile::goldsrc_rgb_lightmap_v1;
}

WorldLightmapEvidenceProfile WorldLightmapSet::evidence_profile() const noexcept
{
    return WorldLightmapEvidenceProfile::valve_public_tools_and_synthetic_fixtures;
}

} // namespace hlclient::assets

#include "world_render_test_fixture.hpp"
#include "missing_texture_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"

#include <hlclient/assets/asset_source.hpp>
#include <hlclient/assets/world_lightmap_types.hpp>
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_texture_source_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_worldspawn_wad_references.hpp>
#include <hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/app/entity_follow_presentation.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>

#include <glad/gl.h>
#include <SDL3/SDL_video.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests::world_render_fixture;
namespace goldsrc_fixture = hlclient::tests;
namespace indexed = hlclient::goldsrc::indexed_texture;
namespace lightmaps = hlclient::goldsrc::lightmaps;
namespace opengl = hlclient::renderer::opengl;
namespace renderer = hlclient::renderer;
namespace wad3 = hlclient::goldsrc::wad3;
namespace world_render = hlclient::world_render;
namespace world_preview = hlclient::world_preview;
namespace world_scene = hlclient::world_scene_render;
namespace world_spatial = hlclient::world_spatial;

struct ActualOpenGlVersion {
    std::uint32_t major{0U};
    std::uint32_t minor{0U};
};

[[nodiscard]] bool parse_version_component(
    const std::string_view text,
    std::size_t& cursor,
    std::uint32_t& value) noexcept
{
    if (cursor >= text.size() || text[cursor] < '0' || text[cursor] > '9') {
        return false;
    }

    value = 0U;
    while (cursor < text.size() && text[cursor] >= '0' &&
           text[cursor] <= '9') {
        const auto digit = static_cast<std::uint32_t>(text[cursor] - '0');
        if (value >
            ((std::numeric_limits<std::uint32_t>::max)() - digit) / 10U) {
            return false;
        }
        value = (value * 10U) + digit;
        ++cursor;
    }
    return true;
}

[[nodiscard]] std::optional<ActualOpenGlVersion>
parse_actual_open_gl_version(const std::string_view text) noexcept
{
    ActualOpenGlVersion version;
    std::size_t cursor = 0U;
    if (!parse_version_component(text, cursor, version.major) ||
        cursor >= text.size() || text[cursor] != '.') {
        return std::nullopt;
    }
    ++cursor;
    if (!parse_version_component(text, cursor, version.minor)) {
        return std::nullopt;
    }
    if (cursor < text.size() && text[cursor] != '.' && text[cursor] != ' ') {
        return std::nullopt;
    }
    return version;
}

[[nodiscard]] constexpr bool supports_required_open_gl_version(
    const ActualOpenGlVersion version) noexcept
{
    return version.major > 3U ||
        (version.major == 3U && version.minor >= 3U);
}

[[nodiscard]] constexpr bool supports_required_open_gl_profile(
    const GLint profile_mask) noexcept
{
    return (profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
}

[[nodiscard]] std::optional<ActualOpenGlVersion>
current_actual_open_gl_version() noexcept
{
    const auto address = SDL_GL_GetProcAddress("glGetString");
    if (address == nullptr) {
        return std::nullopt;
    }
    static_assert(
        sizeof(PFNGLGETSTRINGPROC) == sizeof(SDL_FunctionPointer));
    const auto get_string = std::bit_cast<PFNGLGETSTRINGPROC>(address);
    const auto* version_bytes = get_string(GL_VERSION);
    if (version_bytes == nullptr) {
        return std::nullopt;
    }

    constexpr std::size_t maximum_version_bytes = 256U;
    const auto* version_text =
        reinterpret_cast<const char*>(version_bytes);
    std::size_t byte_count = 0U;
    while (byte_count < maximum_version_bytes &&
           version_text[byte_count] != '\0') {
        ++byte_count;
    }
    if (byte_count == 0U || byte_count == maximum_version_bytes) {
        return std::nullopt;
    }
    return parse_actual_open_gl_version(
        std::string_view{version_text, byte_count});
}

[[nodiscard]] std::optional<GLint>
current_actual_open_gl_profile_mask() noexcept
{
    const auto get_integer_address = SDL_GL_GetProcAddress("glGetIntegerv");
    const auto get_error_address = SDL_GL_GetProcAddress("glGetError");
    if (get_integer_address == nullptr || get_error_address == nullptr) {
        return std::nullopt;
    }
    static_assert(
        sizeof(PFNGLGETINTEGERVPROC) == sizeof(SDL_FunctionPointer));
    static_assert(sizeof(PFNGLGETERRORPROC) == sizeof(SDL_FunctionPointer));
    const auto get_integer =
        std::bit_cast<PFNGLGETINTEGERVPROC>(get_integer_address);
    const auto get_error = std::bit_cast<PFNGLGETERRORPROC>(get_error_address);
    if (get_error() != GL_NO_ERROR) {
        return std::nullopt;
    }

    GLint profile_mask = 0;
    get_integer(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    if (get_error() != GL_NO_ERROR) {
        return std::nullopt;
    }
    return profile_mask;
}

class HiddenOpenGlContext final {
public:
    HiddenOpenGlContext()
        : runtime_{std::make_unique<hlclient::platform::SdlRuntime>()},
          window_{std::make_unique<hlclient::platform::SdlWindow>(
              hlclient::platform::SdlWindowConfig{
                  "HL Client OpenGL world test", 96, 96, true})}
    {
    }

    void initialize_renderer()
    {
        renderer_ = std::make_unique<opengl::OpenGlRenderer>();
    }

    [[nodiscard]] hlclient::platform::SdlWindow& window() noexcept
    {
        return *window_;
    }

    [[nodiscard]] opengl::OpenGlRenderer& renderer() noexcept
    {
        return *renderer_;
    }

    void release_renderer() noexcept
    {
        renderer_.reset();
    }

private:
    // Explicit ownership order: renderer/resources, context/window, SDL.
    std::unique_ptr<hlclient::platform::SdlRuntime> runtime_;
    std::unique_ptr<hlclient::platform::SdlWindow> window_;
    std::unique_ptr<opengl::OpenGlRenderer> renderer_;
};

[[nodiscard]] std::unique_ptr<HiddenOpenGlContext> try_create_context()
{
    try {
        return std::make_unique<HiddenOpenGlContext>();
    } catch (const std::exception& error) {
        if (hlclient::platform::classify_opengl_startup_capability_failure(
                error) !=
            hlclient::platform::OpenGlStartupCapabilityFailure::none) {
            return nullptr;
        }
        throw;
    }
}

[[nodiscard]] assets::WorldAsset make_vertical_world()
{
    auto world = fixture::make_world();
    world.bounds = {{-1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 1.0F}};
    world.vertices = {
        {{-1.0F, 0.0F, -1.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F}},
        {{1.0F, 0.0F, -1.0F}, {0.0F, -1.0F, 0.0F}, {16.0F, 0.0F}},
        {{1.0F, 0.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {16.0F, 16.0F}},
        {{-1.0F, 0.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, 16.0F}},
    };
    world.surfaces[0].bounds = world.bounds;
    return world;
}

[[nodiscard]] assets::WorldTextureSet make_frame_texture_set(
    const bool masked,
    const std::uint8_t red,
    const bool flat_sky = false)
{
    auto texture = fixture::make_texture(masked);
    if (flat_sky) texture.name = "sky";
    for (auto& mip : texture.mip_levels) {
        for (std::uint32_t y = 0U; y < mip.height; ++y) {
            for (std::uint32_t x = 0U; x < mip.width; ++x) {
                const auto offset =
                    (static_cast<std::size_t>(y) * mip.width + x) * 4U;
                mip.rgba_pixels[offset] = std::byte{red};
                mip.rgba_pixels[offset + 1U] = std::byte{0x60};
                mip.rgba_pixels[offset + 2U] = std::byte{0x28};
                const auto alpha = static_cast<std::uint8_t>(
                    masked && x >= mip.width / 2U ? 0U : 0xFFU);
                mip.rgba_pixels[offset + 3U] = std::byte{alpha};
            }
        }
    }
    assets::WorldMaterialTextureBinding binding;
    binding.material_index = 0U;
    binding.status =
        assets::WorldMaterialTextureBindingStatus::resolved_embedded;
    binding.texture_asset_index = 0U;
    binding.source_bsp_texture_index = 0U;
    auto created = assets::WorldTextureSet::create(
        {std::move(texture)}, {binding}, {}, 1U);
    if (!created || !created.texture_set) {
        throw std::runtime_error{"Unable to build the OpenGL texture fixture"};
    }
    return std::move(*created.texture_set);
}

[[nodiscard]] assets::WorldLightmapSet make_frame_lightmap_set(
    const bool unlit,
    const std::size_t page_count,
    const std::size_t selected_page)
{
    assets::WorldSurfaceLightmapBinding binding;
    binding.surface_index = 0U;
    if (unlit) {
        binding.status =
            assets::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
        binding.sample_width = 2U;
        binding.sample_height = 2U;
        auto created = assets::WorldLightmapSet::create({}, {binding}, 1U);
        if (!created || !created.lightmap_set) {
            throw std::runtime_error{"Unable to build the unlit OpenGL fixture"};
        }
        return std::move(*created.lightmap_set);
    }

    std::vector<assets::WorldLightmapAtlasPage> pages;
    pages.reserve(page_count);
    for (std::size_t page_index = 0U; page_index < page_count; ++page_index) {
        auto page = fixture::make_page(
            static_cast<std::uint8_t>(0x90U + page_index));
        for (std::size_t layer = 0U; layer < 1U; ++layer) {
            auto& pixels = page.style_slot_images[layer].rgba_pixels;
            for (std::size_t pixel = 0U; pixel < pixels.size() / 4U; ++pixel) {
                const auto offset = pixel * 4U;
                const auto x = static_cast<std::uint32_t>(pixel % page.width);
                const auto y = static_cast<std::uint32_t>(pixel / page.width);
                const auto source_x = std::clamp(x, 1U, 2U);
                const auto source_y = std::clamp(y, 1U, 2U);
                const auto shade = static_cast<std::uint8_t>(
                    0xB0U +
                    ((static_cast<std::size_t>(source_y) * page.width + source_x +
                         layer + page_index) %
                        0x30U));
                pixels[offset] = std::byte{shade};
                pixels[offset + 1U] = std::byte{shade};
                pixels[offset + 2U] = std::byte{shade};
                pixels[offset + 3U] = std::byte{0xFF};
            }
        }
        pages.push_back(std::move(page));
    }
    binding.status = assets::WorldSurfaceLightmapBindingStatus::resolved;
    binding.atlas_page_index = selected_page;
    binding.inner_rectangle = {1U, 1U, 2U, 2U};
    binding.padded_rectangle = {0U, 0U, 4U, 4U};
    binding.sample_width = 2U;
    binding.sample_height = 2U;
    binding.source_styles.style_count = 1U;
    binding.source_styles.style_ids = {0U, 0xFFU, 0xFFU, 0xFFU};
    std::vector<assets::WorldSurfaceLightmapBinding> bindings{binding};
    for (std::size_t page_index = 0U; page_index < page_count; ++page_index) {
        if (page_index == selected_page) {
            continue;
        }
        auto additional = binding;
        additional.surface_index = bindings.size();
        additional.atlas_page_index = page_index;
        bindings.push_back(additional);
    }
    const auto surface_count = bindings.size();
    auto created = assets::WorldLightmapSet::create(
        std::move(pages), std::move(bindings), surface_count);
    if (!created || !created.lightmap_set) {
        const auto classification = created.error
            ? assets::to_string(created.error->code)
            : std::string_view{"missing_error"};
        throw std::runtime_error{
            "Unable to build the OpenGL lightmap fixture: " +
            std::string{classification}};
    }
    return std::move(*created.lightmap_set);
}

[[nodiscard]] std::shared_ptr<const world_render::WorldRenderPackage>
make_frame_package(
    const bool masked,
    const bool unlit,
    const std::size_t page_count,
    const std::size_t selected_page,
    const std::uint8_t red,
    const bool flat_sky = false)
{
    auto world = make_vertical_world();
    world.materials[0].texture_name = masked ? "{MASKED" : "STONE";
    if (flat_sky) {
        world.materials[0].texture_name = "sky";
        world.surfaces[0].special_surface = true;
    }
    const auto surface_count = unlit ? 1U : page_count;
    const auto template_vertices = world.vertices;
    const auto template_surface = world.surfaces[0U];
    for (std::size_t surface_index = 1U; surface_index < surface_count;
         ++surface_index) {
        const auto first_vertex = world.vertices.size();
        const auto first_index = world.indices.size();
        const auto shift = static_cast<float>(surface_index) * 3.0F;
        for (auto vertex : template_vertices) {
            vertex.position.x += shift;
            world.vertices.push_back(vertex);
        }
        for (const auto index : std::array<std::uint32_t, 6U>{
                 0U, 1U, 2U, 0U, 2U, 3U}) {
            world.indices.push_back(
                static_cast<std::uint32_t>(first_vertex) + index);
        }
        auto surface = template_surface;
        surface.first_vertex = static_cast<std::uint32_t>(first_vertex);
        surface.first_index = static_cast<std::uint32_t>(first_index);
        surface.source_surface_ordinal =
            static_cast<std::uint32_t>(surface_index);
        surface.bounds.minimum.x += shift;
        surface.bounds.maximum.x += shift;
        world.surfaces.push_back(surface);
        world.bounds.maximum.x = surface.bounds.maximum.x;
    }
    world.statistics.emitted_vertex_count = world.vertices.size();
    world.statistics.emitted_triangle_count = world.indices.size() / 3U;
    world.statistics.emitted_surface_count = world.surfaces.size();
    auto textures = make_frame_texture_set(masked, red, flat_sky);
    auto lightmaps = make_frame_lightmap_set(unlit, page_count, selected_page);
    world_render::WorldRenderPackageBuilder builder;
    auto built = builder.build(
        assets::TexturedWorldAsset{std::move(world), std::move(textures)},
        std::move(lightmaps));
    if (!built || !built.package) {
        throw std::runtime_error{"Unable to build the OpenGL world package fixture"};
    }
    return std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] std::shared_ptr<const world_render::WorldRenderPackage>
make_prepared_render_package(const std::size_t surface_count)
{
    auto world = fixture::make_world(surface_count);
    for (auto& vertex : world.vertices) {
        vertex.position.z = vertex.position.y - 8.0F;
        vertex.position.y = 0.0F;
        vertex.normal = {0.0F, -1.0F, 0.0F};
    }
    for (auto& surface : world.surfaces) {
        surface.bounds = {
            {surface.bounds.minimum.x, 0.0F, -8.0F},
            {surface.bounds.maximum.x, 0.0F, 8.0F}};
    }
    world.bounds = {{0.0F, 0.0F, -8.0F},
        {static_cast<float>(surface_count - 1U) * 32.0F + 16.0F,
            0.0F, 8.0F}};
    std::vector<assets::WorldSurfaceLightmapBinding> bindings;
    bindings.reserve(surface_count);
    for (std::size_t surface = 0U; surface < surface_count; ++surface) {
        assets::WorldSurfaceLightmapBinding binding;
        binding.surface_index = surface;
        binding.status =
            assets::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
        binding.sample_width = 2U;
        binding.sample_height = 2U;
        bindings.push_back(binding);
    }
    auto lightmaps = assets::WorldLightmapSet::create(
        {}, std::move(bindings), surface_count);
    if (!lightmaps || !lightmaps.lightmap_set) {
        throw std::runtime_error{"Unable to create prepared render lightmaps"};
    }
    auto built = world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world), make_frame_texture_set(false, 0x74U)},
        std::move(*lightmaps.lightmap_set));
    if (!built || !built.package) {
        throw std::runtime_error{"Unable to create prepared render package"};
    }
    return std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] renderer::RenderScene make_scene(
    std::shared_ptr<const world_render::WorldRenderPackage> package,
    const renderer::RenderCullMode cull_mode = renderer::RenderCullMode::none)
{
    renderer::RenderScene scene;
    scene.camera.position = {0.0F, -2.0F, 0.0F};
    scene.camera.target = {0.0F, 0.0F, 0.0F};
    scene.camera.up = {0.0F, 0.0F, 1.0F};
    scene.camera.vertical_field_of_view_radians = 1.0471975512F;
    scene.camera.near_plane = 0.1F;
    scene.camera.far_plane = 10.0F;
    scene.static_world.emplace(renderer::RenderStaticWorld{
        std::move(package),
        cull_mode,
        renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    return scene;
}

[[nodiscard]] std::array<std::uint8_t, 4U> read_pixel(
    const int x,
    const int y)
{
    std::array<std::uint8_t, 4U> pixel{};
    glReadBuffer(GL_BACK);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    return pixel;
}

[[nodiscard]] bool approximately_clear(
    const std::array<std::uint8_t, 4U>& pixel,
    const renderer::ClearColor color) noexcept
{
    const auto channel = [](const float value) {
        return static_cast<int>(std::lround(value * 255.0F));
    };
    return std::abs(static_cast<int>(pixel[0]) - channel(color.red)) <= 2 &&
        std::abs(static_cast<int>(pixel[1]) - channel(color.green)) <= 2 &&
        std::abs(static_cast<int>(pixel[2]) - channel(color.blue)) <= 2 &&
        std::abs(static_cast<int>(pixel[3]) - channel(color.alpha)) <= 2;
}

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text)
{
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

void populate_masked_embedded_texture(std::vector<std::byte>& bsp_bytes)
{
    const auto texture_lump = static_cast<std::size_t>(
        goldsrc_fixture::synthetic_read_i32le(
            bsp_bytes,
            goldsrc_fixture::synthetic_lump_descriptor_offset(
                goldsrc_fixture::SyntheticBspLumpId::textures)));
    const auto record_relative = static_cast<std::size_t>(
        goldsrc_fixture::synthetic_read_i32le(
            bsp_bytes, texture_lump + 4U));
    const auto record = texture_lump + record_relative;

    constexpr std::array<std::uint32_t, 4U> dimensions{16U, 8U, 4U, 2U};
    std::size_t pixel_byte_count = 0U;
    for (std::size_t level = 0U; level < dimensions.size(); ++level) {
        const auto level_offset = static_cast<std::size_t>(
            goldsrc_fixture::synthetic_read_u32le(
                bsp_bytes, record + 24U + (level * 4U)));
        const auto dimension = dimensions[level];
        for (std::uint32_t y = 0U; y < dimension; ++y) {
            for (std::uint32_t x = 0U; x < dimension; ++x) {
                const auto index = static_cast<std::size_t>(y) * dimension + x;
                bsp_bytes[record + level_offset + index] =
                    static_cast<std::byte>(x >= dimension / 2U
                            ? 255U
                            : 48U + static_cast<std::uint8_t>(x + y + level));
            }
        }
        pixel_byte_count += static_cast<std::size_t>(dimension) * dimension;
    }

    const auto palette_count_offset = record + 40U + pixel_byte_count;
    goldsrc_fixture::synthetic_write_u16le(
        bsp_bytes, palette_count_offset, 256U);
    for (std::size_t index = 0U; index < 256U; ++index) {
        bsp_bytes[palette_count_offset + 2U + (index * 3U)] =
            static_cast<std::byte>(index);
        bsp_bytes[palette_count_offset + 2U + (index * 3U) + 1U] =
            static_cast<std::byte>(255U - index);
        bsp_bytes[palette_count_offset + 2U + (index * 3U) + 2U] =
            static_cast<std::byte>(0x40U + (index % 0x80U));
    }
}

[[nodiscard]] std::vector<std::byte> make_full_pipeline_bsp()
{
    goldsrc_fixture::SyntheticBspBuilder builder;
    builder.lump(goldsrc_fixture::SyntheticBspLumpId::entities) = bytes_of(
        "{\n\"classname\" \"worldspawn\"\n"
        "\"_wad\" \"C:\\\\compiler\\\\stage.wad;\"\n}\n");

    constexpr std::array vertices{
        goldsrc_fixture::SyntheticBspVector3{-32.0F, -32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{0.0F, -32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{0.0F, 32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{-32.0F, 32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{32.0F, -32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{32.0F, 32.0F, 0.0F},
    };
    constexpr std::array edges{
        goldsrc_fixture::SyntheticBspEdge{0U, 0U},
        goldsrc_fixture::SyntheticBspEdge{0U, 1U},
        goldsrc_fixture::SyntheticBspEdge{1U, 2U},
        goldsrc_fixture::SyntheticBspEdge{2U, 3U},
        goldsrc_fixture::SyntheticBspEdge{3U, 0U},
        goldsrc_fixture::SyntheticBspEdge{1U, 4U},
        goldsrc_fixture::SyntheticBspEdge{4U, 5U},
        goldsrc_fixture::SyntheticBspEdge{5U, 2U},
        goldsrc_fixture::SyntheticBspEdge{2U, 1U},
    };
    constexpr std::array<std::int32_t, 8U> surfedges{
        -4, -3, -2, -1, -8, -7, -6, -5};

    std::array faces{
        goldsrc_fixture::SyntheticBspFace{},
        goldsrc_fixture::SyntheticBspFace{},
    };
    faces[0U].light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
    faces[0U].light_offset = 0;
    faces[1U].first_surfedge = 4;
    faces[1U].texinfo_index = 1;
    faces[1U].light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
    faces[1U].light_offset = 12;

    std::array texinfo{
        goldsrc_fixture::SyntheticBspTexinfo{},
        goldsrc_fixture::SyntheticBspTexinfo{},
    };
    texinfo[0U].s_vector = {0.5F, 0.0F, 0.0F, 16.0F};
    texinfo[0U].t_vector = {0.0F, 0.25F, 0.0F, 8.0F};
    texinfo[1U].s_vector = {0.5F, 0.0F, 0.0F, 0.0F};
    texinfo[1U].t_vector = {0.0F, 0.25F, 0.0F, 8.0F};
    texinfo[1U].miptex_index = 1;

    auto node = goldsrc_fixture::SyntheticBspNode{};
    node.minimum = {-33, -33, -1};
    node.maximum = {33, 33, 1};
    node.face_count = 2U;
    std::array leaves{
        goldsrc_fixture::SyntheticBspLeaf{},
        goldsrc_fixture::SyntheticBspLeaf{},
    };
    leaves[0U].contents = -2;
    leaves[0U].marksurface_count = 0U;
    leaves[1U].minimum = {-33, -33, -1};
    leaves[1U].maximum = {33, 33, 1};
    leaves[1U].marksurface_count = 2U;
    constexpr std::array<std::uint16_t, 2U> marksurfaces{0U, 1U};
    auto model = goldsrc_fixture::SyntheticBspModel{};
    model.minimum = {-33.0F, -33.0F, -1.0F};
    model.maximum = {33.0F, 33.0F, 1.0F};
    model.face_count = 2;

    auto embedded = goldsrc_fixture::synthetic_embedded_texture(
        "{MASKED", 16U, 16U);
    constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
    embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
    auto external =
        goldsrc_fixture::synthetic_external_texture("WAD_TEXTURE");
    external.width = 16U;
    external.height = 16U;
    const std::array<std::optional<goldsrc_fixture::SyntheticBspMipTexture>, 2U>
        textures{embedded, external};

    auto& lighting = builder.lump(
        goldsrc_fixture::SyntheticBspLumpId::lighting);
    lighting.resize(24U);
    for (std::size_t sample = 0U; sample < 8U; ++sample) {
        lighting[sample * 3U] =
            static_cast<std::byte>(0xA0U + (sample * 7U));
        lighting[sample * 3U + 1U] =
            static_cast<std::byte>(0xB0U + (sample * 5U));
        lighting[sample * 3U + 2U] =
            static_cast<std::byte>(0xC0U + (sample * 3U));
    }

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_texinfo(texinfo)
        .set_nodes(std::span{&node, 1U})
        .set_leaves(leaves)
        .set_marksurfaces(marksurfaces)
        .set_models(std::span{&model, 1U})
        .set_texture_directory(textures);
    auto bytes = builder.build();
    populate_masked_embedded_texture(bytes);
    return bytes;
}

struct ImportedFullPipelineWorld {
    assets::AssetSource source;
    assets::WorldAsset world;
};

[[nodiscard]] ImportedFullPipelineWorld import_full_pipeline_world()
{
    auto bytes = make_full_pipeline_bsp();
    assets::AssetSourceMetadata metadata;
    metadata.content_size = bytes.size();
    auto created = assets::AssetSource::create(
        std::filesystem::path{"maps/opengl_integration.bsp"},
        std::move(bytes),
        std::move(metadata));
    if (!created || !created.source) {
        throw std::runtime_error{"Unable to retain the synthetic integration BSP"};
    }
    auto source = std::move(*created.source);
    const bsp::GoldSrcBspWorldImporter importer;
    auto imported = importer.import(source);
    if (!imported) {
        throw std::runtime_error{imported.error().context};
    }
    return {std::move(source), std::move(imported).value()};
}

[[nodiscard]] assets::WorldTextureSet import_full_pipeline_textures(
    const ImportedFullPipelineWorld& imported,
    const goldsrc_fixture::SyntheticWad3Fixture& wad_source)
{
    auto sources = bsp::GoldSrcBspTextureSourceParser::parse(
        imported.source.bytes(), imported.world.materials);
    if (!sources || !sources.document) {
        throw std::runtime_error{"Unable to parse BSP texture sources"};
    }
    const auto entity_lump = imported.source.bytes().subspan(
        sources.document->entity_lump_offset(),
        sources.document->entity_lump_byte_count());
    auto references =
        bsp::GoldSrcEntityLumpParser::parse_worldspawn_wad_references(
            entity_lump);
    if (!references || !references.references ||
        references.references->size() != 1U ||
        references.references->references()[0U].normalized_basename !=
            "STAGE.WAD") {
        throw std::runtime_error{"Unable to resolve the synthetic WAD declaration"};
    }

    auto catalog_result =
        wad3::GoldSrcWad3CatalogParser::parse(wad_source.bytes);
    if (!catalog_result || !catalog_result.catalog) {
        throw std::runtime_error{"Unable to parse the synthetic WAD3 catalog"};
    }
    const auto& catalog = *catalog_result.catalog;

    std::vector<assets::WorldTextureAsset> textures;
    std::vector<assets::WorldMaterialTextureBinding> bindings;
    bindings.reserve(imported.world.materials.size());
    for (std::size_t material_index = 0U;
         material_index < imported.world.materials.size(); ++material_index) {
        const auto& material = imported.world.materials[material_index];
        if (!material.source_texture_index || !material.texture_name ||
            !material.width || !material.height) {
            throw std::runtime_error{"Synthetic world material metadata is incomplete"};
        }
        const auto* source = sources.document->source_for_texture_index(
            *material.source_texture_index);
        if (source == nullptr) {
            throw std::runtime_error{"Synthetic BSP texture source is unavailable"};
        }

        assets::WorldMaterialTextureBinding binding;
        binding.material_index = material_index;
        binding.source_bsp_texture_index = material.source_texture_index;
        binding.texture_asset_index = textures.size();
        if (source->storage ==
            bsp::GoldSrcBspTextureSourceStorage::embedded) {
            if (!source->source_record_offset ||
                !source->source_record_byte_count) {
                throw std::runtime_error{"Embedded miptex range is unavailable"};
            }
            const auto record = imported.source.bytes().subspan(
                *source->source_record_offset,
                *source->source_record_byte_count);
            auto decoded = indexed::GoldSrcIndexedTextureDecoder::decode(
                record,
                indexed::GoldSrcMiptexSourceProfile::bsp_embedded,
                assets::WorldTextureSourceKind::embedded_bsp,
                source->canonical_source_texture_index);
            if (!decoded || !decoded.texture) {
                throw std::runtime_error{"Unable to decode the embedded miptex"};
            }
            binding.status =
                assets::WorldMaterialTextureBindingStatus::resolved_embedded;
            textures.push_back(std::move(*decoded.texture));
        } else if (source->storage ==
            bsp::GoldSrcBspTextureSourceStorage::external_reference) {
            const auto* entry = catalog.find_miptex(*material.texture_name);
            if (entry == nullptr) {
                throw std::runtime_error{"WAD3 miptex resolution failed"};
            }
            auto decoded = wad3::GoldSrcWad3TextureDecoder::decode(
                wad_source.bytes,
                *entry,
                wad3::GoldSrcWad3TextureRequest{
                    *material.texture_name,
                    *material.width,
                    *material.height,
                    *material.source_texture_index,
                    references.references->references()[0U].declaration_ordinal,
                });
            if (!decoded || !decoded.texture) {
                throw std::runtime_error{"Unable to decode the external WAD3 miptex"};
            }
            binding.status =
                assets::WorldMaterialTextureBindingStatus::resolved_wad3;
            binding.source_archive_ordinal =
                references.references->references()[0U].declaration_ordinal;
            textures.push_back(std::move(*decoded.texture));
        } else {
            throw std::runtime_error{"Synthetic BSP texture source is missing"};
        }
        bindings.push_back(binding);
    }

    assets::WorldTextureArchiveMetadata archive;
    archive.declaration_ordinal =
        references.references->references()[0U].declaration_ordinal;
    archive.basename_byte_count =
        references.references->references()[0U].basename.size();
    archive.status = assets::WorldTextureArchiveStatus::resolved;
    archive.catalog_entry_count = catalog.entry_count();
    archive.textures_supplied_count = 1U;
    archive.source_byte_count = wad_source.bytes.size();
    auto created = assets::WorldTextureSet::create(
        std::move(textures),
        std::move(bindings),
        {archive},
        imported.world.materials.size());
    if (!created || !created.texture_set) {
        throw std::runtime_error{"Unable to build the integration texture set"};
    }
    return std::move(*created.texture_set);
}

[[nodiscard]] std::shared_ptr<const world_render::WorldRenderPackage>
make_full_pipeline_package()
{
    auto imported = import_full_pipeline_world();
    auto wad_source =
        goldsrc_fixture::synthetic_valid_wad3("WAD_TEXTURE");
    auto textures = import_full_pipeline_textures(imported, wad_source);
    auto imported_lightmaps = lightmaps::GoldSrcWorldLightmapImporter::import(
        imported.world, imported.source.bytes());
    if (!imported_lightmaps || !imported_lightmaps.lightmap_set) {
        throw std::runtime_error{"Unable to import the synthetic RGB lightmaps"};
    }

    world_render::WorldRenderPackageBuilder builder;
    auto built = builder.build(
        assets::TexturedWorldAsset{
            std::move(imported.world), std::move(textures)},
        std::move(*imported_lightmaps.lightmap_set));
    if (!built || !built.package) {
        throw std::runtime_error{"Unable to build the full integration package"};
    }
    return std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] renderer::RenderScene make_full_pipeline_scene(
    std::shared_ptr<const world_render::WorldRenderPackage> package)
{
    renderer::RenderScene scene;
    scene.camera.position = {0.0F, 0.0F, 80.0F};
    scene.camera.target = {0.0F, 0.0F, 0.0F};
    scene.camera.up = {0.0F, 1.0F, 0.0F};
    scene.camera.vertical_field_of_view_radians = 1.0471975512F;
    scene.camera.near_plane = 0.1F;
    scene.camera.far_plane = 200.0F;
    scene.static_world.emplace(renderer::RenderStaticWorld{
        std::move(package),
        renderer::RenderCullMode::none,
        renderer::RenderBaselineLightStylePolicy::source_slot_zero,
    });
    return scene;
}

[[nodiscard]] std::shared_ptr<const world_render::WorldRenderPackage>
make_boundary_junction_package(const float boundary_offset)
{
    // One pentagon meets two quads along A-M and M-B. Both neighboring
    // faces reference the same BSP edges, not independently rounded copies.
    // M is a compiler-style boundary split, even when it is collinear.
    goldsrc_fixture::SyntheticBspBuilder builder;
    const std::array vertices{
        goldsrc_fixture::SyntheticBspVector3{992.0F, -32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{1024.0F, -32.0F, 0.0F}, // A
        goldsrc_fixture::SyntheticBspVector3{
            1024.0F + boundary_offset, 0.0F, 0.0F}, // M
        goldsrc_fixture::SyntheticBspVector3{1024.0F, 32.0F, 0.0F}, // B
        goldsrc_fixture::SyntheticBspVector3{992.0F, 32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{1056.0F, -32.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{1056.0F, 0.0F, 0.0F},
        goldsrc_fixture::SyntheticBspVector3{1056.0F, 32.0F, 0.0F},
    };
    constexpr std::array edges{
        goldsrc_fixture::SyntheticBspEdge{0U, 0U},
        goldsrc_fixture::SyntheticBspEdge{0U, 4U},
        goldsrc_fixture::SyntheticBspEdge{4U, 3U},
        goldsrc_fixture::SyntheticBspEdge{3U, 2U},
        goldsrc_fixture::SyntheticBspEdge{2U, 1U},
        goldsrc_fixture::SyntheticBspEdge{1U, 0U},
        goldsrc_fixture::SyntheticBspEdge{2U, 6U},
        goldsrc_fixture::SyntheticBspEdge{6U, 5U},
        goldsrc_fixture::SyntheticBspEdge{5U, 1U},
        goldsrc_fixture::SyntheticBspEdge{3U, 7U},
        goldsrc_fixture::SyntheticBspEdge{7U, 6U},
    };
    // Valve-clockwise loops. Negative surfedges give the neighbors the
    // identical shared segment with the opposite traversal direction.
    constexpr std::array<std::int32_t, 13U> surfedges{
        1, 2, 3, 4, 5, -4, 6, 7, 8, -3, 9, 10, -6};
    std::array faces{
        goldsrc_fixture::SyntheticBspFace{},
        goldsrc_fixture::SyntheticBspFace{},
        goldsrc_fixture::SyntheticBspFace{},
    };
    faces[0U].surfedge_count = 5;
    faces[1U].first_surfedge = 5;
    faces[2U].first_surfedge = 9;

    auto node = goldsrc_fixture::SyntheticBspNode{};
    node.minimum = {991, -33, -1};
    node.maximum = {1057, 33, 1};
    node.face_count = 3U;
    std::array leaves{
        goldsrc_fixture::SyntheticBspLeaf{},
        goldsrc_fixture::SyntheticBspLeaf{},
    };
    leaves[0U].contents = -2;
    leaves[0U].marksurface_count = 0U;
    leaves[1U].minimum = node.minimum;
    leaves[1U].maximum = node.maximum;
    leaves[1U].marksurface_count = 3U;
    constexpr std::array<std::uint16_t, 3U> marksurfaces{0U, 1U, 2U};
    auto model = goldsrc_fixture::SyntheticBspModel{};
    model.minimum = {991.0F, -33.0F, -1.0F};
    model.maximum = {1057.0F, 33.0F, 1.0F};
    model.face_count = 3;

    auto embedded = goldsrc_fixture::synthetic_embedded_texture(
        "STONE", 16U, 16U);
    constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
    embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
    const std::array<std::optional<goldsrc_fixture::SyntheticBspMipTexture>, 1U>
        textures{embedded};
    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_nodes(std::span{&node, 1U})
        .set_leaves(leaves)
        .set_marksurfaces(marksurfaces)
        .set_models(std::span{&model, 1U})
        .set_texture_directory(textures);
    auto bytes = builder.build();
    const auto texture_lump = static_cast<std::size_t>(
        goldsrc_fixture::synthetic_read_i32le(bytes,
            goldsrc_fixture::synthetic_lump_descriptor_offset(
                goldsrc_fixture::SyntheticBspLumpId::textures)));
    const auto texture_record = texture_lump + static_cast<std::size_t>(
        goldsrc_fixture::synthetic_read_i32le(bytes, texture_lump + 4U));
    goldsrc_fixture::synthetic_write_u16le(
        bytes, texture_record + 40U + pixel_byte_count, 256U);

    assets::AssetSourceMetadata metadata;
    metadata.content_size = bytes.size();
    auto source = assets::AssetSource::create(
        std::filesystem::path{"maps/opengl_boundary_junction.bsp"},
        std::move(bytes), std::move(metadata));
    if (!source || !source.source) {
        throw std::runtime_error{"Unable to retain the boundary-junction BSP"};
    }
    const bsp::GoldSrcBspWorldImporter importer;
    auto imported = importer.import(*source.source);
    if (!imported) {
        throw std::runtime_error{imported.error().context};
    }
    auto world = std::move(imported).value();
    auto imported_lightmaps = lightmaps::GoldSrcWorldLightmapImporter::import(
        world, source.source->bytes());
    if (!imported_lightmaps || !imported_lightmaps.lightmap_set) {
        throw std::runtime_error{"Unable to import boundary-junction lightmaps"};
    }
    world_render::WorldRenderPackageBuilder package_builder;
    auto built = package_builder.build(
        assets::TexturedWorldAsset{
            std::move(world), make_frame_texture_set(false, 0x30U)},
        std::move(*imported_lightmaps.lightmap_set));
    if (!built || !built.package) {
        throw std::runtime_error{"Unable to build the boundary-junction package"};
    }
    return std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] world_spatial::WorldSpatialPackage make_visibility_spatial(
    const std::size_t world_surface_count)
{
    const assets::WorldBounds world_bounds{
        {-8.0F, -2.0F, -2.0F}, {8.0F, 2.0F, 2.0F}};
    world_spatial::WorldSpatialNode node;
    node.plane_index = 0U;
    node.children = {
        world_spatial::WorldSpatialNodeChild{
            world_spatial::WorldSpatialNodeChildKind::leaf, 2U},
        world_spatial::WorldSpatialNodeChild{
            world_spatial::WorldSpatialNodeChildKind::leaf, 1U},
    };
    node.bounds = world_bounds;

    world_spatial::WorldSpatialLeaf leaf_zero;
    leaf_zero.source_leaf_index = 0U;
    leaf_zero.bounds = world_bounds;
    leaf_zero.surface_membership.source_leaf_index = 0U;
    leaf_zero.solid_or_special = true;

    world_spatial::WorldSpatialLeaf leaf_one;
    leaf_one.source_leaf_index = 1U;
    leaf_one.bounds = {{-8.0F, -2.0F, -2.0F}, {1.5F, 2.0F, 2.0F}};
    leaf_one.pvs_row_index = 0U;
    leaf_one.pvs_bit_addressable = true;
    leaf_one.surface_membership.source_leaf_index = 1U;
    leaf_one.surface_membership.source_marksurface_count = 1U;
    leaf_one.surface_membership.world_surface_indices = {0U};

    world_spatial::WorldSpatialLeaf leaf_two;
    leaf_two.source_leaf_index = 2U;
    leaf_two.bounds = {{1.5F, -2.0F, -2.0F}, {8.0F, 2.0F, 2.0F}};
    leaf_two.pvs_row_index = 1U;
    leaf_two.pvs_bit_addressable = true;
    leaf_two.surface_membership.source_leaf_index = 2U;
    if (world_surface_count > 1U) {
        leaf_two.surface_membership.source_marksurface_count = 1U;
        leaf_two.surface_membership.world_surface_indices = {1U};
    }

    return world_spatial::WorldSpatialPackage{
        {world_spatial::WorldSpatialPlane{
            {1.0F, 0.0F, 0.0F}, 1.5F, 0}},
        {node},
        {leaf_zero, leaf_one, leaf_two},
        world_spatial::WorldPvsTable{
            1U,
            2U,
            {{std::byte{0x01U}}, {std::byte{0x02U}},
                {std::byte{0x03U}}},
            {std::nullopt, 0U, 1U},
            2U},
        world_spatial::WorldSpatialModelMetadata{0U, 2U, world_bounds},
        world_spatial::WorldSpatialStatistics{
            1U, 1U, 3U, world_surface_count, world_surface_count, 3U, 3U},
        world_spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        world_spatial::WorldSpatialEvidenceProfile::
            canonical_validated_bsp_records};
}

[[nodiscard]] std::shared_ptr<const world_scene::WorldSceneRenderPackage>
make_visibility_scene(
    std::shared_ptr<const world_render::WorldRenderPackage> world,
    std::shared_ptr<const world_render::WorldRenderPackage> brush = {},
    const std::span<const float> brush_x_translations = {})
{
    std::vector<world_scene::BrushSubmodelRenderModel> models;
    std::vector<world_scene::BrushSubmodelRenderInstance> instances;
    if (brush) {
        std::vector<std::uint32_t> render_surface_indices;
        render_surface_indices.reserve(brush->surface_ranges().size());
        for (std::size_t index = 0U; index < brush->surface_ranges().size();
             ++index) {
            render_surface_indices.push_back(
                static_cast<std::uint32_t>(index));
        }
        models.emplace_back(
            1U, brush->bounds(), std::move(render_surface_indices));

        for (std::size_t index = 0U; index < brush_x_translations.size();
             ++index) {
            const auto translation = brush_x_translations[index];
            world_scene::BrushSubmodelRenderInstance instance;
            instance.source_instance_index = static_cast<std::uint32_t>(index);
            instance.source_entity_ordinal = index + 1U;
            instance.source_model_index = 1U;
            instance.model_transform.values[12U] = translation;
            instance.transformed_bounds = brush->bounds();
            instance.transformed_bounds.minimum.x += translation;
            instance.transformed_bounds.maximum.x += translation;
            instance.touched_leaf_indices = {1U};
            instance.support_status = world_scene::
                BrushSubmodelRenderSupportStatus::supported_static_opaque;
            instances.push_back(std::move(instance));
        }
    }
    world_scene::BrushSubmodelRenderLibrary library{
        std::move(brush), std::move(models)};

    const auto world_surface_count = world->surface_ranges().size();
    world_scene::WorldSceneRenderPackageBuilder builder;
    auto built = builder.build(
        std::move(world),
        make_visibility_spatial(world_surface_count),
        std::move(library),
        std::move(instances));
    if (!built || !built.package) {
        throw std::runtime_error{
            "Unable to build OpenGL visibility scene fixture"};
    }
    return std::make_shared<const world_scene::WorldSceneRenderPackage>(
        std::move(*built.package));
}

struct BrushSceneFixtureInstance {
    renderer::RenderMatrix4 model_transform{};
    std::vector<std::uint32_t> touched_leaf_indices{1U};
    world_scene::BrushSubmodelRenderSupportStatus support_status{
        world_scene::BrushSubmodelRenderSupportStatus::supported_static_opaque};
};

[[nodiscard]] renderer::RenderMatrix4 make_brush_fixture_transform(
    const float x,
    const float y,
    const float z,
    const float cosine = 1.0F,
    const float sine = 0.0F) noexcept
{
    renderer::RenderMatrix4 transform;
    transform.values[0U] = cosine;
    transform.values[2U] = -sine;
    transform.values[8U] = sine;
    transform.values[10U] = cosine;
    transform.values[12U] = x;
    transform.values[13U] = y;
    transform.values[14U] = z;
    return transform;
}

[[nodiscard]] assets::WorldBounds transform_brush_fixture_bounds(
    const assets::WorldBounds& local_bounds,
    const renderer::RenderMatrix4& transform) noexcept
{
    assets::WorldBounds result{};
    for (std::size_t corner = 0U; corner < 8U; ++corner) {
        const auto transformed = renderer::transform(transform,
            renderer::RenderHomogeneousVector{
                (corner & 1U) == 0U
                    ? local_bounds.minimum.x
                    : local_bounds.maximum.x,
                (corner & 2U) == 0U
                    ? local_bounds.minimum.y
                    : local_bounds.maximum.y,
                (corner & 4U) == 0U
                    ? local_bounds.minimum.z
                    : local_bounds.maximum.z,
                1.0F});
        const assets::AssetVector3 point{
            transformed.x, transformed.y, transformed.z};
        if (corner == 0U) {
            result = {point, point};
            continue;
        }
        result.minimum.x = std::min(result.minimum.x, point.x);
        result.minimum.y = std::min(result.minimum.y, point.y);
        result.minimum.z = std::min(result.minimum.z, point.z);
        result.maximum.x = std::max(result.maximum.x, point.x);
        result.maximum.y = std::max(result.maximum.y, point.y);
        result.maximum.z = std::max(result.maximum.z, point.z);
    }
    return result;
}

[[nodiscard]] std::shared_ptr<const world_scene::WorldSceneRenderPackage>
make_profiled_brush_scene(
    std::shared_ptr<const world_render::WorldRenderPackage> world,
    std::shared_ptr<const world_render::WorldRenderPackage> brush,
    const std::span<const BrushSceneFixtureInstance> fixture_instances)
{
    std::vector<std::uint32_t> render_surface_indices;
    render_surface_indices.reserve(brush->surface_ranges().size());
    for (std::size_t index = 0U; index < brush->surface_ranges().size();
         ++index) {
        render_surface_indices.push_back(
            static_cast<std::uint32_t>(index));
    }
    std::vector<world_scene::BrushSubmodelRenderModel> models;
    models.emplace_back(
        1U, brush->bounds(), std::move(render_surface_indices));
    world_scene::BrushSubmodelRenderLibrary library{
        brush, std::move(models)};

    std::vector<world_scene::BrushSubmodelRenderInstance> instances;
    instances.reserve(fixture_instances.size());
    for (std::size_t index = 0U; index < fixture_instances.size(); ++index) {
        const auto& fixture_instance = fixture_instances[index];
        world_scene::BrushSubmodelRenderInstance instance;
        instance.source_instance_index = static_cast<std::uint32_t>(index);
        instance.source_entity_ordinal = index + 1U;
        instance.source_model_index = 1U;
        instance.model_transform = fixture_instance.model_transform;
        instance.transformed_bounds = transform_brush_fixture_bounds(
            brush->bounds(), fixture_instance.model_transform);
        instance.touched_leaf_indices = fixture_instance.touched_leaf_indices;
        instance.support_status = fixture_instance.support_status;
        instances.push_back(std::move(instance));
    }

    world_scene::WorldSceneRenderPackageBuilder builder;
    auto built = builder.build(
        std::move(world),
        make_visibility_spatial(1U),
        std::move(library),
        std::move(instances));
    if (!built || !built.package) {
        throw std::runtime_error{
            "Unable to build profiled OpenGL brush scene fixture"};
    }
    return std::make_shared<const world_scene::WorldSceneRenderPackage>(
        std::move(*built.package));
}

[[nodiscard]] world_preview::WorldPreviewSceneOptions visibility_options(
    const hlclient::world_visibility::WorldVisibilityMode mode,
    const bool brushes)
{
    world_preview::WorldPreviewSceneOptions options;
    options.camera_mode = world_preview::WorldPreviewCameraMode::spawn;
    options.spawn_camera = world_preview::WorldPreviewSpawnCameraDescriptor{
        {0.0F, -2.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}};
    options.visibility_mode = mode;
    options.pvs_fallback_policy =
        hlclient::world_visibility::WorldPvsFallbackPolicy::fail_closed;
    options.brush_submodels = brushes
        ? world_preview::WorldPreviewBrushSubmodelsMode::static_instances
        : world_preview::WorldPreviewBrushSubmodelsMode::off;
    options.visibility_extent = {96, 96};
    return options;
}

TEST_CASE("OpenGL renderer error classifications are bounded and stable",
          "[renderer][opengl]")
{
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::shader_compile_failed) ==
        "shader_compile_failed");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::program_link_failed) ==
        "program_link_failed");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::invalid_world_package) ==
        "invalid_world_package");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::buffer_upload_failed) ==
        "buffer_upload_failed");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::texture_upload_failed) ==
        "texture_upload_failed");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::lightmap_upload_failed) ==
        "lightmap_upload_failed");
    CHECK(opengl::to_string(opengl::OpenGlRendererErrorCode::camera_invalid) ==
        "camera_invalid");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::draw_range_invalid) ==
        "draw_range_invalid");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::gl_operation_failed) ==
        "gl_operation_failed");
    CHECK(opengl::to_string(
              opengl::OpenGlRendererErrorCode::unable_to_retain_resources) ==
        "unable_to_retain_resources");
}

TEST_CASE("OpenGL actual-version capability parsing is bounded",
          "[renderer][opengl][capability]")
{
    const auto legacy = parse_actual_open_gl_version("1.1.0");
    REQUIRE(legacy.has_value());
    CHECK(legacy->major == 1U);
    CHECK(legacy->minor == 1U);
    CHECK_FALSE(supports_required_open_gl_version(*legacy));

    const auto insufficient = parse_actual_open_gl_version("3.2");
    REQUIRE(insufficient.has_value());
    CHECK_FALSE(supports_required_open_gl_version(*insufficient));

    const auto required =
        parse_actual_open_gl_version("3.3.0 - Build 1");
    REQUIRE(required.has_value());
    CHECK(supports_required_open_gl_version(*required));

    const auto newer = parse_actual_open_gl_version("4.6 Mesa");
    REQUIRE(newer.has_value());
    CHECK(supports_required_open_gl_version(*newer));

    CHECK_FALSE(supports_required_open_gl_profile(0));
    CHECK_FALSE(supports_required_open_gl_profile(
        GL_CONTEXT_COMPATIBILITY_PROFILE_BIT));
    CHECK(supports_required_open_gl_profile(GL_CONTEXT_CORE_PROFILE_BIT));
    CHECK(supports_required_open_gl_profile(
        GL_CONTEXT_CORE_PROFILE_BIT | GL_CONTEXT_COMPATIBILITY_PROFILE_BIT));

    CHECK_FALSE(parse_actual_open_gl_version("").has_value());
    CHECK_FALSE(
        parse_actual_open_gl_version("OpenGL 3.3").has_value());
    CHECK_FALSE(parse_actual_open_gl_version("3").has_value());
    CHECK_FALSE(parse_actual_open_gl_version("3.x").has_value());
    CHECK_FALSE(parse_actual_open_gl_version(
                    "42949672960.3")
                    .has_value());
}

TEST_CASE("OpenGL actual context capability requires a Core profile",
          "[renderer][opengl][capability][actual-context]")
{
    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }

    context->initialize_renderer();
    CHECK(supports_required_open_gl_profile(*actual_profile_mask));
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("E32 optional GPU timing preserves pixels and never waits for pending results",
          "[e32][renderer][opengl][movement-diagnostics]")
{
    auto context=try_create_context();
    if(!context)SKIP("OpenGL 3.3 Core context unavailable on this host");
    const auto version=current_actual_open_gl_version();
    REQUIRE(version);
    if(!supports_required_open_gl_version(*version))SKIP("OpenGL 3.3 Core context unavailable on this host");
    const auto profile=current_actual_open_gl_profile_mask();REQUIRE(profile);
    if(!supports_required_open_gl_profile(*profile))SKIP("OpenGL Core profile unavailable");
    context->initialize_renderer();auto& gpu=context->renderer();
    constexpr renderer::RenderExtent extent{96,96};
    auto scene=make_scene(make_frame_package(false,false,1U,0U,0xE0U));
    gpu.render(scene,extent);
    const auto baseline=gpu.observe_framebuffer(extent,scene.clear_color,true);
    CHECK(gpu.gpu_timing_statistics().allocated_queries==0U);
    CHECK_FALSE(gpu.poll_gpu_timing());
    gpu.render_with_gpu_timing(scene,extent,42U);
    // A test-only framebuffer observation completes the pixels. Production
    // uses availability polling only and introduces no readback or glFinish.
    const auto timed=gpu.observe_framebuffer(extent,scene.clear_color,true);
    CHECK(timed.color_signature==baseline.color_signature);
    CHECK(gpu.gpu_timing_statistics().allocated_queries==8U);
    const auto first=gpu.poll_gpu_timing();
    const auto check_passes=[](const renderer::opengl::OpenGlGpuFrameTiming& result) {
        CHECK(result.status==renderer::opengl::OpenGlGpuTimingStatus::ready);
        CHECK(result.valid_pass_mask==0x3fU);
        CHECK(result.valid_world_subpass_mask==0x07U);
        double sum{};
        for(const auto elapsed:result.pass_elapsed_ms) {
            CHECK(std::isfinite(elapsed));CHECK(elapsed>=0.0F);sum+=elapsed;
        }
        // The total uses the pre-existing elapsed query; timestamp segments
        // begin/end inside it, with allowance only for float encoding error.
        CHECK(sum<=result.elapsed_ms+0.01);
        double world_sum{};
        for(std::size_t subpass=0;subpass<3U;++subpass) {
            const auto elapsed=result.world_subpass_elapsed_ms[subpass];
            CHECK(std::isfinite(elapsed));CHECK(elapsed>=0.0F);
            world_sum+=elapsed;
        }
        CHECK(std::fabs(world_sum-result.pass_elapsed_ms[
            static_cast<std::size_t>(renderer::opengl::OpenGlGpuPass::world)])<0.01);
    };
    if(first) {CHECK(first->frame_number==42U);CHECK(std::isfinite(first->elapsed_ms));CHECK(first->elapsed_ms>=0.0);check_passes(*first);}
    CHECK(gpu.gpu_timing_statistics().allocated_pass_queries==128U);
    CHECK(gpu.gpu_timing_statistics().pass_timestamps_supported);
    std::size_t observations=first ? 1U:0U;
    for(std::uint64_t id=43U;id<60U;++id) {
        gpu.render_with_gpu_timing(scene,extent,id);
        context->window().swap_buffers();
        for(std::size_t poll=0;poll<8U;++poll) {
            const auto result=gpu.poll_gpu_timing();if(!result)break;
            ++observations;CHECK(result->frame_number>=42U);CHECK(result->frame_number<=id);
            CHECK(std::isfinite(result->elapsed_ms));CHECK(result->elapsed_ms>=0.0);
            check_passes(*result);
        }
        CHECK(gpu.gpu_timing_statistics().pending<=8U);
    }
    CHECK(observations>0U);
    CHECK(gpu.gpu_timing_statistics().allocated_queries==8U);
    CHECK(gpu.statistics().upload_count==1U);
    // A caller's active timer is not nested, ended, replaced or deleted.
    GLuint foreign{};glGenQueries(1,&foreign);REQUIRE(foreign!=0U);
    glBeginQuery(GL_TIME_ELAPSED,foreign);
    const auto unavailable=gpu.gpu_timing_statistics().unavailable;
    gpu.render_with_gpu_timing(scene,extent,100U);
    GLint still_foreign{};glGetQueryiv(GL_TIME_ELAPSED,GL_CURRENT_QUERY,&still_foreign);
    CHECK(static_cast<GLuint>(still_foreign)==foreign);
    CHECK(gpu.gpu_timing_statistics().unavailable==unavailable+1U);
    CHECK(gpu.gpu_timing_statistics().last_submission_status==renderer::opengl::OpenGlGpuTimingStatus::invalid);
    glEndQuery(GL_TIME_ELAPSED);glDeleteQueries(1,&foreign);
    auto invalid=scene;invalid.requested_msaa_samples=3;
    CHECK_THROWS_AS(gpu.render_with_gpu_timing(invalid,extent,101U),std::invalid_argument);
    GLint active{};glGetQueryiv(GL_TIME_ELAPSED,GL_CURRENT_QUERY,&active);CHECK(active==0);
    gpu.render(scene,extent);
    CHECK(glGetError()==GL_NO_ERROR);

    gpu.reset_session_resources();
    CHECK(gpu.gpu_timing_statistics().allocated_pass_queries==0U);
    for(std::uint64_t id=200;id<209;++id)gpu.render_with_gpu_timing(scene,extent,id);
    CHECK(gpu.gpu_timing_statistics().pending==8U);
    CHECK(gpu.gpu_timing_statistics().capacity_drops==1U);
    CHECK(gpu.gpu_timing_statistics().last_submission_status==renderer::opengl::OpenGlGpuTimingStatus::capacity_dropped);
    // Deliberately unavailable results must never call GL_QUERY_RESULT. This
    // test-only entry-point substitution does not change the production path.
    const auto original_available=glad_glGetQueryObjectiv;
    const auto original_result=glad_glGetQueryObjectui64v;
    static std::size_t pending_availability_calls{},pending_result_calls{};
    pending_availability_calls=pending_result_calls=0U;
    struct RestoreQueries {
        PFNGLGETQUERYOBJECTIVPROC available;
        PFNGLGETQUERYOBJECTUI64VPROC result;
        ~RestoreQueries(){glad_glGetQueryObjectiv=available;glad_glGetQueryObjectui64v=result;}
        static void GLAD_API_PTR unavailable(GLuint,GLenum name,GLint* value) {
            if(name==GL_QUERY_RESULT_AVAILABLE)++pending_availability_calls;
            *value=GL_FALSE;
        }
        static void GLAD_API_PTR result_attempt(GLuint,GLenum,GLuint64*){++pending_result_calls;}
    } restore_queries{original_available,original_result};
    glad_glGetQueryObjectiv=&RestoreQueries::unavailable;
    glad_glGetQueryObjectui64v=&RestoreQueries::result_attempt;
    CHECK_FALSE(gpu.poll_gpu_timing());
    CHECK(pending_availability_calls==8U);
    CHECK(pending_result_calls==0U);
    CHECK(gpu.gpu_timing_statistics().pending==8U);
    glad_glGetQueryObjectiv=original_available;glad_glGetQueryObjectui64v=original_result;
    gpu.reset_session_resources();
    CHECK(gpu.gpu_timing_statistics().pending==0U);
    CHECK(gpu.gpu_timing_statistics().allocated_pass_queries==0U);
    CHECK_FALSE(gpu.poll_gpu_timing());

    // Query capability absence keeps the legacy whole-frame observation but
    // cannot manufacture six zero-duration successful passes.
    const auto original_counter=glad_glQueryCounter;
    struct RestoreCounter {
        PFNGLQUERYCOUNTERPROC value;
        ~RestoreCounter(){glad_glQueryCounter=value;}
    } restore_counter{original_counter};
    glad_glQueryCounter=nullptr;
    gpu.render_with_gpu_timing(scene,extent,300U);
    glad_glQueryCounter=original_counter;
    (void)gpu.observe_framebuffer(extent,scene.clear_color);
    const auto unsupported=gpu.poll_gpu_timing();
    REQUIRE(unsupported);
    CHECK(unsupported->frame_number==300U);
    CHECK(unsupported->status==renderer::opengl::OpenGlGpuTimingStatus::unsupported);
    CHECK(unsupported->valid_pass_mask==0U);
    CHECK(unsupported->valid_world_subpass_mask==0U);
    CHECK(std::isfinite(unsupported->elapsed_ms));
    CHECK(gpu.gpu_timing_statistics().allocated_pass_queries==0U);
    gpu.reset_session_resources();

    gpu.render_with_gpu_timing(scene,extent,301U);
    (void)gpu.observe_framebuffer(extent,scene.clear_color);
    static std::size_t injected_result_index{};
    injected_result_index=0U;
    struct NonmonotonicQueries {
        static void GLAD_API_PTR available(GLuint,GLenum,GLint* value){*value=GL_TRUE;}
        static void GLAD_API_PTR result(GLuint,GLenum,GLuint64* value) {
            // Whole-frame elapsed, first boundary, then a backwards timestamp.
            *value=(injected_result_index++==1U) ? 2'000'000U:1'000'000U;
        }
    };
    glad_glGetQueryObjectiv=&NonmonotonicQueries::available;
    glad_glGetQueryObjectui64v=&NonmonotonicQueries::result;
    const auto disjoint=gpu.poll_gpu_timing();
    glad_glGetQueryObjectiv=original_available;glad_glGetQueryObjectui64v=original_result;
    REQUIRE(disjoint);
    CHECK(disjoint->frame_number==301U);
    CHECK(disjoint->status==renderer::opengl::OpenGlGpuTimingStatus::disjoint);
    CHECK(disjoint->valid_pass_mask==0U);
    CHECK(disjoint->valid_world_subpass_mask==0U);
    CHECK_FALSE(std::isfinite(disjoint->elapsed_ms));
    CHECK(gpu.gpu_timing_statistics().disjoint_results==1U);
    CHECK(glGetError()==GL_NO_ERROR);

    gpu.reset_session_resources();
    gpu.render_with_gpu_timing(scene,extent,400U);
    gpu.render_with_gpu_timing(scene,extent,401U);
    struct QueryFailure {
        static void GLAD_API_PTR available(GLuint,GLenum,GLint* value) {
            *value=GL_FALSE;
            glEnable(0xFFFFFFFFU); // Known invalid enum: inject only in optional query scope.
        }
    };
    glad_glGetQueryObjectiv=&QueryFailure::available;
    const auto failed_first=gpu.poll_gpu_timing();
    glad_glGetQueryObjectiv=original_available;
    REQUIRE(failed_first);CHECK(failed_first->frame_number==400U);
    CHECK(failed_first->status==renderer::opengl::OpenGlGpuTimingStatus::invalid);
    CHECK(failed_first->valid_pass_mask==0U);
    CHECK(failed_first->valid_world_subpass_mask==0U);
    CHECK_FALSE(std::isfinite(failed_first->elapsed_ms));
    const auto failed_second=gpu.poll_gpu_timing();
    REQUIRE(failed_second);CHECK(failed_second->frame_number==401U);
    CHECK(failed_second->status==renderer::opengl::OpenGlGpuTimingStatus::invalid);
    CHECK(failed_second->valid_pass_mask==0U);
    CHECK(failed_second->valid_world_subpass_mask==0U);
    CHECK_FALSE(std::isfinite(failed_second->elapsed_ms));
    CHECK_FALSE(gpu.poll_gpu_timing());CHECK(gpu.gpu_timing_statistics().pending==0U);
    CHECK(gpu.gpu_timing_statistics().invalid_results==2U);
    gpu.render_with_gpu_timing(scene,extent,402U);
    CHECK(gpu.gpu_timing_statistics().last_submission_status==renderer::opengl::OpenGlGpuTimingStatus::invalid);
    CHECK(gpu.gpu_timing_statistics().submitted==2U);
    CHECK(glGetError()==GL_NO_ERROR);
    gpu.reset_session_resources();
    gpu.render_with_gpu_timing(scene,extent,403U);
    (void)gpu.observe_framebuffer(extent,scene.clear_color);
    const auto recovered=gpu.poll_gpu_timing();REQUIRE(recovered);
    CHECK(recovered->frame_number==403U);check_passes(*recovered);

    // A genuine renderer error must not be consumed as an optional timer
    // error. Inject in SkyDrawState construction (before a pass boundary),
    // then in its destruction (after the last pass, before the final query).
    static PFNGLBINDSAMPLERPROC original_sampler{};
    static std::size_t sampler_calls{},fail_sampler_call{};
    original_sampler=glad_glBindSampler;
    struct SamplerRenderFailure {
        PFNGLBINDSAMPLERPROC saved;
        ~SamplerRenderFailure(){glad_glBindSampler=saved;}
        static void GLAD_API_PTR bind(GLuint unit,GLuint sampler) {
            original_sampler(unit,sampler);
            if(unit==2U && ++sampler_calls==fail_sampler_call)glEnable(0xFFFFFFFFU);
        }
    } restore_sampler{original_sampler};
    renderer::RenderScene empty_scene;
    for(const auto injection:{1U,2U}) {
        gpu.reset_session_resources();sampler_calls=0;fail_sampler_call=injection;
        glad_glBindSampler=&SamplerRenderFailure::bind;
        CHECK_THROWS_AS(gpu.render_with_gpu_timing(empty_scene,extent,500U+injection),
            renderer::opengl::OpenGlRendererError);
        glad_glBindSampler=original_sampler;
        CHECK(sampler_calls>=injection);
        GLint owned_query{};glGetQueryiv(GL_TIME_ELAPSED,GL_CURRENT_QUERY,&owned_query);
        CHECK(owned_query==0);CHECK(glGetError()==GL_NO_ERROR);
        CHECK(gpu.gpu_timing_statistics().pending==0U);
        CHECK(gpu.gpu_timing_statistics().last_submission_status==renderer::opengl::OpenGlGpuTimingStatus::invalid);
        // Rendering errors do not permanently disable the query capability.
        gpu.render_with_gpu_timing(scene,extent,510U+injection);
        (void)gpu.observe_framebuffer(extent,scene.clear_color);
        const auto next=gpu.poll_gpu_timing();REQUIRE(next);
        CHECK(next->frame_number==510U+injection);check_passes(*next);
    }
}

TEST_CASE("World draw bindings reuse only immutable inputs inside one render frame",
          "[gauss-71b97][renderer][opengl][world-bind-cache]")
{
    auto context=try_create_context();
    if(!context)SKIP("OpenGL 3.3 Core context unavailable on this host");
    context->initialize_renderer();
    auto& gpu=context->renderer();
    constexpr renderer::RenderExtent extent{96,96};
    const auto make_two_batch_package=[] {
        fixture::FixtureOptions options;options.atlas_page_count=2;
        auto built=fixture::make_package(options);
        REQUIRE(built);REQUIRE(built.package);
        return std::make_shared<const world_render::WorldRenderPackage>(std::move(*built.package));
    };
    const auto package=make_two_batch_package();
    REQUIRE(package->draw_batches().size()==2U);
    auto scene=make_scene(package);
    scene.camera.position={24,8,60};scene.camera.target={24,8,0};scene.camera.up={0,1,0};
    scene.camera.far_plane=128; // The vertical-wall helper's 10-unit range cannot reach these floor patches.
    const auto render_checked=[&] {
        const auto before=gpu.statistics();
        gpu.render(scene,extent);
        const auto& after=gpu.statistics();
        // No triangles or draws are suppressed. Both ranges use the same
        // base texture but different atlas pages; only the redundant bind
        // and unchanged program-local material uniforms may be omitted.
        CHECK(after.draw_call_count-before.draw_call_count==2U);
        CHECK(after.triangle_count-before.triangle_count==4U);
        CHECK(after.base_texture_bind_count-before.base_texture_bind_count==1U);
        CHECK(after.lightmap_bind_count-before.lightmap_bind_count==2U);
        const bool has_point_light=scene.transient_world_light &&
            renderer::valid_render_point_light(*scene.transient_world_light) &&
            scene.transient_world_light->intensity>0.0F;
        const bool has_dynamic_light=has_point_light || !scene.effect_lights().empty();
        // Both opaque static-world programs need only the lightmap material
        // uniform; neither has sky/mask branches or a camera uniform.
        CHECK(after.material_uniform_upload_count-before.material_uniform_upload_count==
            1U);
        CHECK(after.program_bind_count-before.program_bind_count==1U);
        CHECK(after.vertex_array_bind_count-before.vertex_array_bind_count==1U);
        CHECK(after.sky_texture_bind_count-before.sky_texture_bind_count==1U);
        CHECK(after.camera_upload_count-before.camera_upload_count==0U);
        // Lit opaque draws retain the original dynamic-light values and model
        // transform; the no-light opaque program needs only the MVP matrix.
        CHECK(after.light_upload_count-before.light_upload_count==
            (has_dynamic_light ? 1U : 0U));
        CHECK(after.transform_upload_count-before.transform_upload_count==
            (has_dynamic_light ? 2U : 1U));
        CHECK(glGetError()==GL_NO_ERROR);
        return gpu.observe_framebuffer(extent,scene.clear_color,true);
    };
    const auto baseline=render_checked();REQUIRE(baseline);
    const auto left=read_pixel(26,48),right=read_pixel(70,48);
    REQUIRE_FALSE(approximately_clear(left,scene.clear_color));
    REQUIRE_FALSE(approximately_clear(right,scene.clear_color));
    CHECK(render_checked().color_signature==baseline.color_signature);

    scene.transient_world_light=renderer::RenderPointLight{{8,8,4},16,1,{1,.5F,.25F}};
    REQUIRE(render_checked());CHECK(read_pixel(26,48)[0]>left[0]);
    CHECK(read_pixel(70,48)==right);
    scene.transient_world_light.reset();
    CHECK(render_checked().color_signature==baseline.color_signature);
    scene.world_effect_lights[0]={{40,8,4},16,1,{.25F,.5F,1}};
    scene.world_effect_light_count=1;
    REQUIRE(render_checked());CHECK(read_pixel(70,48)[2]>right[2]);
    CHECK(read_pixel(26,48)==left);
    scene.world_effect_light_count=0;
    CHECK(render_checked().color_signature==baseline.color_signature);

    scene.camera.position.x+=4;scene.camera.target.x+=4;
    CHECK(render_checked().color_signature!=baseline.color_signature);
    scene.camera.position.x-=4;scene.camera.target.x-=4;
    CHECK(render_checked().color_signature==baseline.color_signature);
    scene.static_world->package=make_two_batch_package();
    CHECK(scene.static_world->package!=package);
    CHECK(render_checked().color_signature==baseline.color_signature);
    gpu.reset_session_resources();
    CHECK(render_checked().color_signature==baseline.color_signature);
}

TEST_CASE("OpenGL renderer uploads, caches and draws a synthetic static world",
          "[renderer][opengl][world-frame]")
{
    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    constexpr renderer::RenderExtent extent{96, 96};
    auto& gl_renderer = context->renderer();

    renderer::RenderScene clear_scene;
    gl_renderer.render(clear_scene, extent);
    CHECK(gl_renderer.statistics().rendered_frame_count == 1U);
    CHECK(gl_renderer.statistics().upload_count == 0U);
    CHECK_FALSE(gl_renderer.statistics().world_present);

    auto masked_package = make_frame_package(true, false, 1U, 0U, 0xE0U);
    const auto masked_revision = masked_package->resource_revision();
    auto masked_scene = make_scene(masked_package);
    gl_renderer.render(masked_scene, extent);

    const auto opaque_sample = read_pixel(extent.width / 4, extent.height / 2);
    const auto masked_sample =
        read_pixel((extent.width * 3) / 4, extent.height / 2);
    CHECK_FALSE(approximately_clear(opaque_sample, masked_scene.clear_color));
    CHECK(approximately_clear(masked_sample, masked_scene.clear_color));
    CHECK(glGetError() == GL_NO_ERROR);

    const auto& first = gl_renderer.statistics();
    CHECK(first.world_present);
    CHECK(first.active_world_resources);
    CHECK(first.package_revision == masked_revision);
    CHECK(first.upload_count == 1U);
    CHECK(first.rendered_frame_count == 2U);
    CHECK(first.draw_call_count == 1U);
    CHECK(first.triangle_count == 2U);
    CHECK(first.base_texture_bind_count == 1U);
    CHECK(first.lightmap_bind_count == 1U);
    CHECK(first.uploaded_base_texture_count == 1U);
    CHECK(first.uploaded_base_mip_level_count == 4U);
    CHECK(first.uploaded_lightmap_page_count == 1U);
    CHECK(first.uploaded_lightmap_layer_count == 4U);
    CHECK(first.uploaded_white_lightmap_count == 0U);
    CHECK(first.last_extent == extent);
    context->window().swap_buffers();

    // Same immutable identity/revision is reused, while backface culling and
    // the second consecutive frame exercise the required draw state.
    masked_scene.static_world->cull_mode = renderer::RenderCullMode::back;
    gl_renderer.render(masked_scene, extent);
    context->window().swap_buffers();
    CHECK(gl_renderer.statistics().upload_count == 1U);
    CHECK(gl_renderer.statistics().rendered_frame_count == 3U);
    CHECK(gl_renderer.statistics().draw_call_count == 2U);
    CHECK(gl_renderer.statistics().triangle_count == 4U);

    // Hash equality is only diagnostic: a distinct immutable package instance
    // must never reuse GPU pixels solely because its 64-bit content IDs match.
    auto duplicate_masked_package =
        make_frame_package(true, false, 1U, 0U, 0xE0U);
    REQUIRE(duplicate_masked_package != masked_package);
    CHECK(duplicate_masked_package->resource_identity() ==
        masked_package->resource_identity());
    auto duplicate_masked_scene = make_scene(duplicate_masked_package);
    gl_renderer.render(duplicate_masked_scene, extent);
    CHECK(gl_renderer.statistics().upload_count == 2U);
    CHECK(gl_renderer.statistics().world_resource_release_count == 1U);

    auto unlit_package = make_frame_package(false, true, 0U, 0U, 0xD0U);
    const auto unlit_revision = unlit_package->resource_revision();
    auto unlit_scene = make_scene(unlit_package);
    gl_renderer.render(unlit_scene, extent);
    CHECK(gl_renderer.statistics().upload_count == 3U);
    CHECK(gl_renderer.statistics().package_revision == unlit_revision);
    CHECK(gl_renderer.statistics().world_resource_release_count == 2U);
    CHECK(gl_renderer.statistics().uploaded_lightmap_page_count == 0U);
    CHECK(gl_renderer.statistics().uploaded_lightmap_layer_count == 0U);
    CHECK(gl_renderer.statistics().uploaded_white_lightmap_count == 1U);

    auto multipage_package =
        make_frame_package(false, false, 2U, 1U, 0xC0U);
    auto multipage_scene = make_scene(multipage_package);
    gl_renderer.render(multipage_scene, extent);
    CHECK(gl_renderer.statistics().upload_count == 4U);
    CHECK(gl_renderer.statistics().world_resource_release_count == 3U);
    CHECK(gl_renderer.statistics().uploaded_lightmap_page_count == 2U);
    CHECK(gl_renderer.statistics().uploaded_lightmap_layer_count == 8U);
    CHECK(gl_renderer.statistics().uploaded_white_lightmap_count == 0U);

    auto invalid_camera = multipage_scene;
    invalid_camera.camera.target = invalid_camera.camera.position;
    try {
        gl_renderer.render(invalid_camera, extent);
        FAIL("Invalid camera unexpectedly rendered");
    } catch (const opengl::OpenGlRendererError& error) {
        CHECK(error.code() == opengl::OpenGlRendererErrorCode::camera_invalid);
    }
    CHECK(gl_renderer.statistics().upload_count == 4U);
    CHECK(gl_renderer.statistics().failed_upload_count == 0U);

    auto invalid_cull = multipage_scene;
    invalid_cull.static_world->cull_mode =
        static_cast<renderer::RenderCullMode>(0x7fU);
    try {
        gl_renderer.render(invalid_cull, extent);
        FAIL("Invalid culling mode unexpectedly rendered");
    } catch (const opengl::OpenGlRendererError& error) {
        CHECK(error.code() ==
            opengl::OpenGlRendererErrorCode::invalid_world_package);
    }
    CHECK(gl_renderer.statistics().upload_count == 4U);

    renderer::RenderScene missing_package;
    missing_package.static_world.emplace();
    try {
        gl_renderer.render(missing_package, extent);
        FAIL("Missing world package unexpectedly rendered");
    } catch (const opengl::OpenGlRendererError& error) {
        CHECK(error.code() ==
            opengl::OpenGlRendererErrorCode::invalid_world_package);
    }

    // A zero drawable extent is a bounded successful skip and causes no
    // upload or draw. The active complete package remains cacheable.
    gl_renderer.render(multipage_scene, renderer::RenderExtent{0, 0});
    CHECK(gl_renderer.statistics().upload_count == 4U);
    CHECK(gl_renderer.statistics().last_extent == renderer::RenderExtent{0, 0});
    CHECK(glGetError() == GL_NO_ERROR);

    // Explicitly destroy the renderer while the context remains current.
    // Its transactional RAII teardown must leave no GL error behind.
    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("OpenGL uniform sky fills finite polygon gaps without replacing world depth",
          "[renderer][opengl][sky-background][no-stock]")
{
    auto context = try_create_context();
    if (!context) SKIP("OpenGL 3.3 Core context unavailable on this host");
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version);
    if (!supports_required_open_gl_version(*actual_version))
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    const auto actual_profile = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile);
    if (!supports_required_open_gl_profile(*actual_profile))
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    context->initialize_renderer();
    auto& gl_renderer = context->renderer();
    constexpr renderer::RenderExtent extent{96,96};
    const auto sky = make_frame_package(false,true,0U,0U,0xe0U,true);
    REQUIRE(sky->flat_sky_background());
    auto scene = make_scene(sky);
    scene.clear_color = {.035F,.055F,.14F,1.0F};
    gl_renderer.render(scene,extent);
    const auto sky_pixel = read_pixel(48,48);
    REQUIRE(sky_pixel == std::array<std::uint8_t,4>{224,96,40,255});
    // Corners are outside the finite BSP polygon: before this fix they exposed
    // the connected client's navy clear color instead of the same sky.
    CHECK(read_pixel(0,0) == sky_pixel);
    CHECK(read_pixel(95,95) == sky_pixel);
    float surface_depth = 1.0F, background_depth = 0.0F;
    glReadPixels(48,48,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&surface_depth);
    glReadPixels(0,0,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&background_depth);
    CHECK(surface_depth < 1.0F);
    CHECK(background_depth == 1.0F);

    // Finite sky clipping is permitted; the environment remains behind it.
    scene.camera.far_plane = 1.5F;
    gl_renderer.render(scene,extent);
    CHECK(read_pixel(48,48) == sky_pixel);
    glReadPixels(48,48,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&background_depth);
    CHECK(background_depth == 1.0F);

    const auto opaque = make_frame_package(false,true,0U,0U,0x30U);
    std::array<BrushSceneFixtureInstance,1U> instances;
    instances[0U].model_transform = make_brush_fixture_transform(0,-.25F,0);
    auto mixed = make_profiled_brush_scene(sky,opaque,instances);
    auto mixed_options = visibility_options(
        hlclient::world_visibility::WorldVisibilityMode::all,true);
    // Keep the near opaque quad well inside the frame. At y=-2 its projected
    // edge nearly reaches the corner pixel, which is not a background probe.
    mixed_options.spawn_camera->position.y = -3.0F;
    world_preview::WorldPreviewSceneSource source{mixed,mixed_options};
    auto mixed_scene = hlclient::client::build_render_scene(source.world_state());
    gl_renderer.render(mixed_scene,extent);
    CHECK(read_pixel(48,48) == std::array<std::uint8_t,4>{48,96,40,255});
    CHECK(read_pixel(0,0) == sky_pixel);

    // A non-sky package and teardown keep their explicitly requested backdrop.
    auto ordinary = make_scene(opaque);
    gl_renderer.render(ordinary,extent);
    CHECK(approximately_clear(read_pixel(0,0),ordinary.clear_color));
    renderer::RenderScene empty;
    gl_renderer.render(empty,extent);
    CHECK(approximately_clear(read_pixel(48,48),empty.clear_color));
    CHECK(glGetError() == GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("Original six-face environment has stable direction depth portals and seamless cube edges",
    "[skybox][opengl][actual-context][no-stock]") {
    auto context=try_create_context();if(!context)SKIP("OpenGL 3.3 Core unavailable");
    context->initialize_renderer();auto& backend=context->renderer();
    constexpr renderer::RenderExtent extent{96,96};
    auto cube=std::make_shared<assets::SkyboxAsset>();
    constexpr std::array<std::array<std::uint8_t,4>,6> colors{{
        {220,20,30,255},{30,210,40,255},{40,50,200,255},
        {200,190,60,255},{180,70,190,255},{60,170,180,255}}};
    for(std::size_t f=0;f<6;++f) {
        auto& face=cube->faces[f];face.width=face.height=4;face.pixels.resize(64);
        for(std::size_t p=0;p<16;++p)for(std::size_t c=0;c<4;++c)
            face.pixels[p*4+c]=static_cast<std::byte>(colors[f][c]);
    }
    renderer::RenderScene scene;scene.skybox=cube;scene.camera.position={0,0,0};
    constexpr std::array<assets::AssetVector3,6> directions{{{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}}};
    for(std::size_t face=0;face<6;++face) {
        scene.camera.target=directions[face];scene.camera.up=face==2 || face==3 ? assets::AssetVector3{0,1,0} : assets::AssetVector3{0,0,1};
        backend.render(scene,extent);CHECK(read_pixel(48,48)==colors[face]);
        // Infinite backdrop has no parallax and writes no scene depth.
        scene.camera.position={143,-79,200};scene.camera.target={143+directions[face].x,-79+directions[face].y,200+directions[face].z};
        backend.render(scene,extent);CHECK(read_pixel(48,48)==colors[face]);
        float depth=0;glReadPixels(48,48,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);CHECK(depth==1.0F);
        scene.camera.position={0,0,0};
    }
    auto gradient=std::make_shared<assets::SkyboxAsset>(*cube);
    for(auto& face:gradient->faces)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
        face.pixels[(y*4+x)*4]=static_cast<std::byte>(x*80);
        face.pixels[(y*4+x)*4+1]=static_cast<std::byte>(y*80);
    }
    scene.skybox=gradient;scene.camera.target={1,0,0};scene.camera.up={0,0,1};
    backend.render(scene,extent);
    CHECK(read_pixel(80,48)[0]>read_pixel(16,48)[0]); // Side panel not mirrored.
    CHECK(read_pixel(48,80)[1]<read_pixel(48,16)[1]); // Top-left source rows applied once.
    const auto unit_up_center=read_pixel(48,48);
    // Canonical camera accepts a scaled, almost forward up vector. Its
    // normalized side is valid even though the raw cross is below1e-6.
    scene.camera.up={0.001F,0,0.0000001F};
    REQUIRE(renderer::is_valid(scene.camera));
    backend.render(scene,extent);CHECK(read_pixel(48,48)==unit_up_center);
    scene.camera.up={0,0,1};
    auto seamless=std::make_shared<assets::SkyboxAsset>(*cube);
    for(auto& face:seamless->faces)for(std::size_t p=0;p<16;++p)for(std::size_t c=0;c<4;++c)
        face.pixels[p*4+c]=static_cast<std::byte>(colors[0][c]);
    scene.skybox=seamless;scene.camera.target={1,1,1};
    backend.render(scene,extent);
    for(int y=4;y<96;y+=8)for(int x=4;x<96;x+=8)CHECK(read_pixel(x,y)==colors[0]);
    const auto sky=make_frame_package(false,true,0U,0U,0xe0U,true);
    REQUIRE(sky->materials()[0].sky_surface);
    auto portal=make_scene(sky);portal.skybox=cube;
    backend.render(portal,extent);
    CHECK(read_pixel(48,48)==colors[4]);CHECK(read_pixel(0,0)==colors[4]);
    float portal_depth=1;glReadPixels(48,48,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&portal_depth);CHECK(portal_depth<1);
    const auto opaque=make_frame_package(false,true,0U,0U,0x30U);
    std::array<BrushSceneFixtureInstance,1> instances;
    instances[0].model_transform=make_brush_fixture_transform(0,-.25F,0);
    auto mixed=make_profiled_brush_scene(sky,opaque,instances);
    auto options=visibility_options(hlclient::world_visibility::WorldVisibilityMode::all,true);
    options.spawn_camera->position.y=-3;
    world_preview::WorldPreviewSceneSource source{mixed,options};
    auto mixed_scene=hlclient::client::build_render_scene(source.world_state());mixed_scene.skybox=cube;
    backend.render(mixed_scene,extent);
    CHECK(read_pixel(48,48)==std::array<std::uint8_t,4>{48,96,40,255});
    CHECK(read_pixel(0,0)==colors[4]);
    // An opaque object behind a sky portal cannot bleed through its finite depth.
    instances[0].model_transform=make_brush_fixture_transform(0,.25F,0);
    auto behind=make_profiled_brush_scene(sky,opaque,instances);
    world_preview::WorldPreviewSceneSource behind_source{behind,options};
    auto behind_scene=hlclient::client::build_render_scene(behind_source.world_state());behind_scene.skybox=cube;
    backend.render(behind_scene,extent);CHECK(read_pixel(48,48)==colors[4]);
    renderer::RenderScene reset;backend.render(reset,extent);
    CHECK(approximately_clear(read_pixel(48,48),reset.clear_color));CHECK(glGetError()==GL_NO_ERROR);
    context->release_renderer();
}

TEST_CASE("Production OpenGL draws a typed missing-texture image without hiding later world passes",
    "[renderer][opengl][world-textures][external-map-compat]")
{
    auto context = try_create_context();
    if (!context) SKIP("OpenGL 3.3 Core context unavailable on this host");
    const auto version = current_actual_open_gl_version();
    const auto profile = current_actual_open_gl_profile_mask();
    REQUIRE(version);
    REQUIRE(profile);
    if (!supports_required_open_gl_version(*version) || !supports_required_open_gl_profile(*profile))
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    context->initialize_renderer();
    auto world = make_vertical_world();
    world.materials[0U].texture_storage = assets::WorldTextureStorage::external_reference;
    world.materials[0U].texture_name = "ABSENT";
    auto built = world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world), goldsrc_fixture::make_missing_texture_set()},
        fixture::make_lightmap_set(fixture::FixtureOptions{false, true, 0U, 0U}));
    REQUIRE(built);
    auto package = std::make_shared<const world_render::WorldRenderPackage>(std::move(*built.package));
    auto scene = make_scene(package);
    auto& gl_renderer = context->renderer();
    constexpr renderer::RenderExtent extent{96, 96};
    gl_renderer.render(scene, extent);
    const auto left = read_pixel(24, 36);
    const auto right = read_pixel(72, 36);
    CHECK_FALSE(approximately_clear(left, scene.clear_color));
    CHECK_FALSE(approximately_clear(right, scene.clear_color));
    CHECK(left != right);
    CHECK(left[1U] == 0U);
    CHECK(right[1U] == 0U);
    CHECK(static_cast<unsigned>(left[0U]) + right[0U] > 200U);
    CHECK(gl_renderer.statistics().uploaded_base_texture_count == 1U);
    CHECK(gl_renderer.statistics().uploaded_base_mip_level_count == 4U);
    CHECK(glGetError() == GL_NO_ERROR);
    auto normal = make_scene(make_frame_package(false, false, 1U, 0U, 0xC0U));
    gl_renderer.render(normal, extent);
    CHECK_FALSE(approximately_clear(read_pixel(24, 36), normal.clear_color));
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("E4.1 transient light changes nearby wall pixels without changing a distant patch",
          "[weapon-visuals][opengl][actual-context]")
{
    auto context = try_create_context();
    if (!context) SKIP("OpenGL 3.3 Core context unavailable on this host");
    context->initialize_renderer();
    constexpr renderer::RenderExtent extent{96,96};
    auto scene = make_scene(make_frame_package(false,false,1U,0U,0x50U));
    context->renderer().render(scene,extent);
    const auto baseline_near=read_pixel(48,48);
    const auto baseline_far=read_pixel(78,48);
    REQUIRE_FALSE(approximately_clear(baseline_near,scene.clear_color));
    REQUIRE_FALSE(approximately_clear(baseline_far,scene.clear_color));
    scene.transient_world_light=renderer::RenderPointLight{{0.0F,-0.05F,0.0F},
        0.45F,1.0F,{1.0F,0.62F,0.28F}};
    context->renderer().render(scene,extent);
    const auto lit_near=read_pixel(48,48);
    const auto lit_far=read_pixel(78,48);
    CHECK(lit_near[0]>baseline_near[0]);
    CHECK(lit_near[1]>baseline_near[1]);
    CHECK(lit_far==baseline_far);
    CHECK(glGetError()==GL_NO_ERROR);
    scene.transient_world_light.reset();
    context->renderer().render(scene,extent);
    CHECK(read_pixel(48,48)==baseline_near);
    context->release_renderer();
}

TEST_CASE("Opaque dynamic world shaders match the legacy brush color and resolved depth",
          "[opaque-dynamic][msaa4-zero-light][msaa4-unrolled-effects][renderer][opengl][actual-context][no-stock]")
{
    auto context=try_create_context();
    if(!context)SKIP("OpenGL 3.3 Core unavailable");
    context->initialize_renderer();
    auto& gpu=context->renderer();
    static PFNGLSHADERSOURCEPROC original_unrolled_shader_source{};
    static unsigned compiled_unrolled_effect_sources{};
    original_unrolled_shader_source=glad_glShaderSource;
    struct UnrolledEffectSourceProbe {
        UnrolledEffectSourceProbe() {
            compiled_unrolled_effect_sources=0U;
            glad_glShaderSource=&source;
        }
        ~UnrolledEffectSourceProbe(){glad_glShaderSource=original_unrolled_shader_source;}
        static void GLAD_API_PTR source(const GLuint shader,const GLsizei count,
            const GLchar* const* strings,const GLint* lengths) {
            std::string combined;
            for(GLsizei i=0;i<count;++i) {
                if(lengths && lengths[i]>=0)
                    combined.append(strings[i],static_cast<std::size_t>(lengths[i]));
                else combined.append(strings[i]);
            }
            if(combined.find("#define HLC_FULL_EIGHT_EFFECT_LIGHTS")!=std::string::npos)
                ++compiled_unrolled_effect_sources;
            original_unrolled_shader_source(shader,count,strings,lengths);
        }
    } unrolled_effect_source_probe;
    // Capture only the target identity during clear. All readbacks/resolves
    // below are offline correctness work, outside the production render path.
    static PFNGLCLEARPROC original_clear{};
    static GLint depth_source{};
    original_clear=glad_glClear;
    struct DepthTargetCapture {
        DepthTargetCapture(){depth_source=0;glad_glClear=&clear;}
        ~DepthTargetCapture(){glad_glClear=original_clear;}
        static void GLAD_API_PTR clear(const GLbitfield mask) {
            if((mask&(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))==
                (GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&depth_source);
            original_clear(mask);
        }
    };
    static PFNGLDRAWELEMENTSPROC original_draw_elements{};
    static std::uint64_t skipped_world_draws{},remaining_world_draws{},forwarded_brush_draws{};
    original_draw_elements=glad_glDrawElements;
    struct LegacyOnlyDraws {
        explicit LegacyOnlyDraws(const std::uint64_t world_draws) {
            remaining_world_draws=world_draws;
            skipped_world_draws=forwarded_brush_draws=0U;
            glad_glDrawElements=&draw;
        }
        ~LegacyOnlyDraws(){glad_glDrawElements=original_draw_elements;}
        static void GLAD_API_PTR draw(const GLenum mode,const GLsizei count,
            const GLenum type,const void* indices) {
            if(remaining_world_draws!=0U) {
                --remaining_world_draws;++skipped_world_draws;return;
            }
            ++forwarded_brush_draws;
            original_draw_elements(mode,count,type,indices);
        }
    };
    static std::uint64_t observed_no_light_draws{},observed_lit_draws{};
    struct WorldProgramProbe {
        WorldProgramProbe() {
            observed_no_light_draws=observed_lit_draws=0U;
            glad_glDrawElements=&draw;
        }
        ~WorldProgramProbe(){glad_glDrawElements=original_draw_elements;}
        static void GLAD_API_PTR draw(const GLenum mode,const GLsizei count,
            const GLenum type,const void* indices) {
            GLint program{};glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            if(glGetUniformLocation(static_cast<GLuint>(program),"u_effect_light_count")<0)
                ++observed_no_light_draws;
            else ++observed_lit_draws;
            original_draw_elements(mode,count,type,indices);
        }
    };
    struct Pixels {std::vector<std::uint8_t> color;std::vector<float> depth;};
    const auto capture=[&](const renderer::RenderScene& scene,
                           const renderer::RenderExtent extent) {
        {DepthTargetCapture target;gpu.render(scene,extent);}
        Pixels result;
        const auto count=static_cast<std::size_t>(extent.width)*extent.height;
        result.color.resize(count*4U);result.depth.resize(count);
        glReadBuffer(GL_BACK);
        glReadPixels(0,0,extent.width,extent.height,GL_RGBA,GL_UNSIGNED_BYTE,
            result.color.data());
        GLint old_read{},old_draw{},old_renderbuffer{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&old_renderbuffer);
        GLuint target{},depth{};
        if(depth_source!=0) {
            // Same format/extent as SceneTarget; this compares nearest-resolved
            // depth exactly, not each individual multisample depth value.
            glGenFramebuffers(1,&target);glGenRenderbuffers(1,&depth);
            glBindRenderbuffer(GL_RENDERBUFFER,depth);
            glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,
                extent.width,extent.height);
            glBindFramebuffer(GL_FRAMEBUFFER,target);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,
                GL_RENDERBUFFER,depth);
            glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
            REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(depth_source));
            glBlitFramebuffer(0,0,extent.width,extent.height,0,0,extent.width,
                extent.height,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,target);
        }
        glReadPixels(0,0,extent.width,extent.height,GL_DEPTH_COMPONENT,GL_FLOAT,
            result.depth.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(old_read));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(old_draw));
        glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(old_renderbuffer));
        if(depth)glDeleteRenderbuffers(1,&depth);
        if(target)glDeleteFramebuffers(1,&target);
        REQUIRE(glGetError()==GL_NO_ERROR);
        REQUIRE(std::any_of(result.depth.begin(),result.depth.end(),
            [](const float value){return value<1.0F;}));
        return result;
    };
    const auto alpha_package=[] {
        auto texture=fixture::make_texture(false);
        for(auto& mip:texture.mip_levels)
            for(std::size_t i=3U;i<mip.rgba_pixels.size();i+=4U)
                mip.rgba_pixels[i]=std::byte{93};
        assets::WorldMaterialTextureBinding binding;
        binding.material_index=0U;binding.texture_asset_index=0U;
        binding.source_bsp_texture_index=0U;
        binding.status=assets::WorldMaterialTextureBindingStatus::resolved_embedded;
        auto textures=assets::WorldTextureSet::create({std::move(texture)},
            {binding},{},1U);
        REQUIRE(textures);REQUIRE(textures.texture_set);
        auto built=world_render::WorldRenderPackageBuilder{}.build(
            assets::TexturedWorldAsset{make_vertical_world(),
                std::move(*textures.texture_set)},make_frame_lightmap_set(false,1U,0U));
        REQUIRE(built);REQUIRE(built.package);
        return std::make_shared<const world_render::WorldRenderPackage>(
            std::move(*built.package));
    }();
    const std::array packages{alpha_package,
        make_frame_package(false,true,0U,0U,0x50U),make_full_pipeline_package(),
        make_frame_package(false,true,0U,0U,0x50U,true)};
    auto sky=std::make_shared<assets::SkyboxAsset>();
    for(auto& face:sky->faces) {
        face.width=face.height=2;face.pixels.resize(16U);
        for(std::size_t i=0;i<face.pixels.size();i+=4U) {
            face.pixels[i]=std::byte{53};face.pixels[i+1U]=std::byte{99};
            face.pixels[i+2U]=std::byte{173};face.pixels[i+3U]=std::byte{255};
        }
    }
    constexpr std::array sample_modes{0U,2U,4U,8U};
    constexpr unsigned light_states=35U;
    std::array<unsigned,4U> actual_modes{},compared_pairs{};
    for(std::size_t mode=0;mode<sample_modes.size();++mode) {
        const auto samples=sample_modes[mode];
        for(std::size_t layout=0;layout<packages.size();++layout) {
            const auto& package=packages[layout];
            auto scene=layout==2U ? make_full_pipeline_scene(package):make_scene(package);
            const auto camera=scene.camera;
            auto map=make_profiled_brush_scene(package,package,{});
            scene.static_world->scene_package=map;
            scene.requested_msaa_samples=samples;
            if(layout==3U)scene.skybox=sky;
            auto brushes=std::make_shared<world_scene::RuntimeBrushRenderFrame>();
            brushes->scene_identity=map->resource_identity();
            brushes->generation=brushes->revision=1U;
            world_scene::RuntimeBrushRenderInstance instance;
            instance.entity_number=100U;instance.model_slot=10U;instance.source_model_index=1U;
            instance.transformed_bounds=package->bounds();
            brushes->instances.push_back(instance);
            REQUIRE(world_scene::valid_runtime_brush_frame(*brushes,*map));
            for(unsigned state=0;state<light_states;++state) {
                CAPTURE(samples,layout,state);
                scene.camera=camera;scene.transient_world_light.reset();
                scene.world_effect_light_count=0U;
                scene.image_adjustments={};scene.screen_effects={};
                const renderer::RenderPointLight nearby_light{{0.0F,-0.2F,0.0F},
                    1.25F,0.45F,{0.25F,0.5F,0.75F}};
                if(state==1U)scene.transient_world_light=nearby_light;
                if(state>=2U && state<=8U) {
                    scene.world_effect_lights[0]=nearby_light;scene.world_effect_light_count=1U;
                }
                if(state==3U) {
                    scene.world_effect_lights[0]={{0.4F,-0.35F,0.2F},0.75F,.8F,{.8F,.2F,.1F}};
                    scene.camera.position.x+=.1F;scene.camera.target.x+=.1F;
                }
                if(state==4U)scene.world_effect_lights[0].center={1000,1000,1000};
                if(state==5U || state==6U) {
                    scene.transient_world_light=nearby_light;scene.world_effect_light_count=8U;
                    for(std::size_t i=0;i<scene.world_effect_lights.size();++i) {
                        scene.world_effect_lights[i]=nearby_light;
                        scene.world_effect_lights[i].center.x=static_cast<float>(i)*.15F-.5F;
                        scene.world_effect_lights[i].intensity=.1F+static_cast<float>(i)*.025F;
                        scene.world_effect_lights[i].radius_units=.4F+static_cast<float>(i)*.2F;
                    }
                    if(state==6U) {
                        std::reverse(scene.world_effect_lights.begin(),scene.world_effect_lights.end());
                        scene.world_effect_lights[0].color={.9F,.1F,.3F};
                        scene.camera.vertical_field_of_view_radians=.7F;
                        gpu.reset_session_resources();
                    }
                }
                if(state==7U)scene.world_effect_lights[0]={{0,-1,0},1,.75F,{1,1,1}};
                if(state==8U) {
                    scene.transient_world_light=nearby_light;
                    scene.transient_world_light->radius_units=0;
                    scene.world_effect_light_count=8U;
                    for(auto& light:scene.world_effect_lights)light=nearby_light;
                    scene.world_effect_lights[0].center.x=std::numeric_limits<float>::quiet_NaN();
                    scene.world_effect_lights[1].radius_units=0;
                    scene.world_effect_lights[2].intensity=-1;
                    scene.world_effect_lights[3].intensity=0;
                    scene.world_effect_lights[4].color[0]=std::numeric_limits<float>::infinity();
                }
                // Conservative zero-contribution controls. These same inputs
                // run against B0 and B2: the unchanged identity-brush shader is
                // the independent color/depth oracle, not another optimized
                // static-world render. Program receipts distinguish whether
                // a savings path actually ran without making B0 fail merely
                // for retaining its original lit program.
                if(state>=10U) {
                    scene.world_effect_light_count=1U;
                    scene.world_effect_lights[0]=nearby_light;
                }
                if(state==10U)scene.world_effect_lights[0]={{0,-1,0},1,.75F,{1,1,1}};
                if(state==11U)scene.world_effect_lights[0]={{0,-1.125F,0},1,.75F,{1,1,1}};
                if(state==12U)scene.world_effect_lights[0]={{0,-4,0},1,.75F,{1,1,1}};
                if(state==13U) {
                    scene.world_effect_light_count=2U;
                    scene.world_effect_lights[1]={{1000,1000,1000},1,.75F,{1,1,1}};
                }
                if(state==14U) {
                    scene.world_effect_light_count=8U;
                    for(std::size_t i=0;i<scene.world_effect_lights.size();++i)
                        scene.world_effect_lights[i]={{1000+static_cast<float>(i),1000,1000},
                            1,.75F,{1,1,1}};
                    scene.transient_world_light=scene.world_effect_lights[0];
                    std::reverse(scene.world_effect_lights.begin(),scene.world_effect_lights.end());
                }
                if(state==15U)scene.world_effect_lights[0].center={2097152,0,0};
                if(state==16U) {
                    scene.world_effect_light_count=3U;
                    scene.world_effect_lights[0].intensity=0;
                    scene.world_effect_lights[1]=nearby_light;
                    scene.world_effect_lights[1].radius_units=0;
                    scene.world_effect_lights[2]=nearby_light;
                    scene.world_effect_lights[2].center.x=std::numeric_limits<float>::quiet_NaN();
                }
                if(state==17U)scene.world_effect_lights[0].color={0,0,0};
                // Numeric edge controls preserve the original arithmetic for
                // tiny radii and huge deltas. Odd-sized viewports
                // also exercise the central pixel of the symmetric world quad.
                if(state==18U)scene.world_effect_lights[0]={{0,3.0e-30F,0},
                    1.0e-30F,.45F,{.25F,.5F,.75F}};
                if(state==19U) {
                    const auto tiny=std::numeric_limits<float>::denorm_min();
                    scene.world_effect_lights[0]={{0,3.0F*tiny,0},
                        tiny,.45F,{.25F,.5F,.75F}};
                }
                if(state==20U)scene.world_effect_lights[0]={{0,-3,0},
                    std::nextafter(1.0F,0.0F),.75F,{1,1,1}};
                if(state==21U)scene.world_effect_lights[0]={{0,-2,0},1,.75F,{1,1,1}};
                if(state==22U)scene.world_effect_lights[0]={{0,
                    -std::nextafter(2.0F,std::numeric_limits<float>::infinity()),0},
                    1,.75F,{1,1,1}};
                if(state==23U)scene.world_effect_lights[0]={{0,-.25F,0},
                    4,.25F,{.1F,.3F,.2F}};
                if(state==24U)scene.world_effect_lights[0]={{0,-8192,0},
                    4096,.75F,{1,1,1}};
                if(state==25U)scene.world_effect_lights[0]={{0,
                    -std::nextafter(8192.0F,std::numeric_limits<float>::infinity()),0},
                    4096,.75F,{1,1,1}};
                if(state==26U)scene.world_effect_lights[0]={{1.0e18F,0,0},1,.75F,{1,1,1}};
                if(state==27U)scene.world_effect_lights[0]={{
                    std::nextafter(1.0e18F,0.0F),0,0},1,.75F,{1,1,1}};
                if(state==28U)scene.world_effect_lights[0]={{1.0e20F,0,0},1,.75F,{1,1,1}};
                if(state==29U) {
                    scene.transient_world_light=nearby_light;
                    scene.transient_world_light->radius_units=std::numeric_limits<float>::quiet_NaN();
                    scene.world_effect_lights[0].center.x=std::numeric_limits<float>::infinity();
                }
                if(state==30U) {
                    scene.world_effect_light_count=8U;
                    for(auto& light:scene.world_effect_lights)
                        light={{1000,1000,1000},1,.2F,{1,1,1}};
                    scene.world_effect_lights[0].radius_units=std::numeric_limits<float>::quiet_NaN();
                    scene.world_effect_lights[3].intensity=0;
                    scene.world_effect_lights[7]=nearby_light;
                    scene.transient_world_light=nearby_light;
                }
                if(state==31U) {
                    scene.transient_world_light=renderer::RenderPointLight{{0,3.0e-30F,0},
                        1.0e-30F,.45F,{.25F,.5F,.75F}};
                    scene.world_effect_light_count=2U;
                    scene.world_effect_lights[0]={{1000,1000,1000},1,.2F,{1,1,1}};
                    scene.world_effect_lights[1]=nearby_light;
                }
                if(state>=32U) {
                    // Complete the 0..8 compacted-count matrix. Distinct,
                    // low-intensity colors/centers make each ordered slot
                    // observable without washing the whole frame to white.
                    scene.world_effect_light_count=state==32U ? 4U:state==33U ? 5U:7U;
                    for(std::size_t i=0;i<scene.world_effect_light_count;++i) {
                        const auto slot=static_cast<float>(i);
                        scene.world_effect_lights[i]={{-.45F+.12F*slot,
                            -.2F-.025F*slot,.2F-.04F*slot},1.3F+.1F*slot,
                            .02F+.009F*slot,{.15F+.08F*slot,.65F-.06F*slot,.25F+.03F*slot}};
                    }
                    scene.transient_world_light=nearby_light;
                    scene.transient_world_light->intensity=.03F;
                    scene.transient_world_light->color={.35F,.25F,.15F};
                }
                if(state>=8U && state<=9U) {
                    scene.image_adjustments={.1F,1.04F,1.1F};
                    scene.screen_effects.overlay_rgba={.8F,.2F,.1F,.25F};
                }
                const auto extent=state==7U ? renderer::RenderExtent{64,48}:
                    (state==18U || state==19U || state==31U) ?
                        renderer::RenderExtent{95,95}:renderer::RenderExtent{96,96};
                const auto before_world=gpu.statistics().draw_call_count;
                Pixels optimized;
                {WorldProgramProbe probe;optimized=capture(scene,extent);}
                const auto world_draws=gpu.statistics().draw_call_count-before_world;
                REQUIRE(world_draws!=0U);
                CHECK(observed_no_light_draws+observed_lit_draws==world_draws);
                if(layout<2U) {
                    // Tangent/near-boundary, mixed contributing lights, and
                    // out-of-proof-domain inputs must retain the lit shader.
                    if(state==10U || state==11U || state==13U || state==15U || state==17U)
                        CHECK(observed_no_light_draws==0U);
                    std::cout<<"[msaa4-zero-light-program] samples="<<samples
                        <<" layout="<<layout<<" state="<<state
                        <<" no_light_draws="<<observed_no_light_draws
                        <<" lit_draws="<<observed_lit_draws<<'\n';
                }
                actual_modes[mode]=gpu.scene_target_statistics().actual_samples;
                if(gpu.scene_target_statistics().actual_samples!=samples) {
                    WARN("Requested MSAA mode is unavailable; mode comparison not run");
                    break;
                }
                auto oracle=scene;oracle.static_world->runtime_brushes=brushes;
                // Suppress the oracle's first N static-world submissions, where
                // N is observed from the identical optimized frame above. Only
                // the identity brush's unchanged general program populates its
                // color/depth; a bad optimized depth cannot hide legacy samples.
                // Sky/postprocessing use DrawArrays and are not intercepted.
                const auto before=gpu.statistics().brush_draw_call_count;
                Pixels reference;
                {
                    LegacyOnlyDraws isolate(world_draws);
                    reference=capture(oracle,extent);
                }
                const auto brush_draws=gpu.statistics().brush_draw_call_count-before;
                REQUIRE(brush_draws!=0U);
                REQUIRE(remaining_world_draws==0U);
                CHECK(skipped_world_draws==world_draws);
                CHECK(forwarded_brush_draws==brush_draws);
                CHECK(optimized.color==reference.color);
                CHECK(optimized.depth==reference.depth);
                std::cout<<"[msaa4-fragment-light-guard-case] requested="<<samples
                    <<" actual="<<gpu.scene_target_statistics().actual_samples
                    <<" layout="<<layout<<" state="<<state
                    <<" color_exact="<<(optimized.color==reference.color)
                    <<" depth_exact="<<(optimized.depth==reference.depth)<<'\n';
                ++compared_pairs[mode];
            }
        }
    }
    for(std::size_t mode=0;mode<sample_modes.size();++mode)
        std::cout<<"[opaque-dynamic-coverage] requested="<<sample_modes[mode]
            <<" actual="<<actual_modes[mode]<<" exact_color_resolved_depth_pairs="
            <<compared_pairs[mode]<<" expected_pairs="<<packages.size()*light_states<<'\n';
    std::cout<<"[msaa4-unrolled-effects] compiled_sources="<<compiled_unrolled_effect_sources
        <<" tested_states="<<light_states<<" layouts="<<packages.size()<<'\n';
    context->release_renderer();
    CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("Opaque dynamic shader failures release partial resources and recover next frame",
          "[opaque-dynamic-failure][renderer][opengl][actual-context][no-stock]")
{
    auto context=try_create_context();
    if(!context)SKIP("OpenGL 3.3 Core unavailable");
    context->initialize_renderer();auto& gpu=context->renderer();
    constexpr renderer::RenderExtent extent{96,96};
    auto scene=make_scene(make_frame_package(false,false,1U,0U,0x50U));
    scene.requested_msaa_samples=8U;
    scene.transient_world_light=renderer::RenderPointLight{{0,-.2F,0},1.5F,.5F,{1,.4F,.2F}};
    gpu.render(scene,extent);
    const auto baseline=gpu.observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(baseline);
    static PFNGLSHADERSOURCEPROC original_source{};
    static PFNGLGETSHADERIVPROC original_shader_status{};
    static PFNGLATTACHSHADERPROC original_attach{};
    static PFNGLGETPROGRAMIVPROC original_program_status{};
    original_source=glad_glShaderSource;original_shader_status=glad_glGetShaderiv;
    original_attach=glad_glAttachShader;original_program_status=glad_glGetProgramiv;
    static GLuint targeted_shader{},targeted_program{};
    static bool fail_compile{},target_full_eight{};
    struct InjectFailure {
        InjectFailure() {
            targeted_shader=targeted_program=0U;
            glad_glShaderSource=&source;glad_glGetShaderiv=&shader_status;
            glad_glAttachShader=&attach;glad_glGetProgramiv=&program_status;
        }
        ~InjectFailure() {
            glad_glShaderSource=original_source;glad_glGetShaderiv=original_shader_status;
            glad_glAttachShader=original_attach;glad_glGetProgramiv=original_program_status;
        }
        static void GLAD_API_PTR source(const GLuint shader,const GLsizei count,
            const GLchar* const* strings,const GLint* lengths) {
            original_source(shader,count,strings,lengths);
            if(count!=1 || strings==nullptr || strings[0]==nullptr)return;
            const auto text=lengths && lengths[0]>=0
                ? std::string_view{strings[0],static_cast<std::size_t>(lengths[0])}
                : std::string_view{strings[0]};
            // Identify the new lit opaque fragment, never a generic shader
            // allocation ordinal that another independent pass could change.
            if(text.find("uniform int u_effect_light_count;")!=std::string_view::npos &&
                text.find("u_sky_surface")==std::string_view::npos &&
                (text.find("#define HLC_FULL_EIGHT_EFFECT_LIGHTS")!=std::string_view::npos)==target_full_eight)
                targeted_shader=shader;
        }
        static void GLAD_API_PTR shader_status(const GLuint shader,const GLenum name,GLint* value) {
            original_shader_status(shader,name,value);
            if(fail_compile && shader==targeted_shader && name==GL_COMPILE_STATUS)*value=GL_FALSE;
        }
        static void GLAD_API_PTR attach(const GLuint program,const GLuint shader) {
            original_attach(program,shader);
            if(shader==targeted_shader)targeted_program=program;
        }
        static void GLAD_API_PTR program_status(const GLuint program,const GLenum name,GLint* value) {
            original_program_status(program,name,value);
            if(!fail_compile && program==targeted_program && name==GL_LINK_STATUS)*value=GL_FALSE;
        }
    };
    for(const bool full_eight:{false,true}) for(const bool compile_failure:{true,false}) {
        CAPTURE(full_eight,compile_failure);
        gpu.reset_session_resources();fail_compile=compile_failure;target_full_eight=full_eight;
        glEnable(GL_SCISSOR_TEST);glScissor(3,4,20,21);
        GLint before_read{},before_draw{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&before_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&before_draw);
        const auto before_multisample=glIsEnabled(GL_MULTISAMPLE);
        std::optional<opengl::OpenGlRendererErrorCode> failure;
        {
            InjectFailure injection;
            try {gpu.render(scene,extent);}
            catch(const opengl::OpenGlRendererError& error){failure=error.code();}
        }
        REQUIRE(targeted_shader!=0U);REQUIRE(failure);
        CHECK(*failure==(compile_failure ? opengl::OpenGlRendererErrorCode::shader_compile_failed:
            opengl::OpenGlRendererErrorCode::program_link_failed));
        CHECK(glIsShader(targeted_shader)==GL_FALSE);
        if(!compile_failure) {
            REQUIRE(targeted_program!=0U);
            CHECK(glIsProgram(targeted_program)==GL_FALSE);
        }
        CHECK(gpu.statistics().failed_upload_count==1U);
        CHECK_FALSE(gpu.statistics().active_world_resources);
        GLint restored{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&restored);CHECK(restored==before_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&restored);CHECK(restored==before_draw);
        CHECK(glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE);
        CHECK(glIsEnabled(GL_MULTISAMPLE)==before_multisample);
        CHECK(glGetError()==GL_NO_ERROR);
        gpu.render(scene,extent);
        const auto recovered=gpu.observe_framebuffer(extent,scene.clear_color,true);
        REQUIRE(recovered);CHECK(recovered.rgba8==baseline.rgba8);
        CHECK(gpu.statistics().active_world_resources);
        CHECK(gpu.statistics().failed_upload_count==1U);
        CHECK(glGetError()==GL_NO_ERROR);
        glDisable(GL_SCISSOR_TEST);
    }
    context->release_renderer();CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("E20 committed EF_LIGHT changes actual wall pixels and clears without a local timer",
          "[e20][opengl][actual-context][renderer]") {
    auto context=try_create_context();if(!context) SKIP("OpenGL 3.3 Core unavailable");
    context->initialize_renderer();constexpr renderer::RenderExtent extent{96,96};
    auto baseline_scene=make_scene(make_frame_package(false,false,1U,0U,0x50U));
    context->renderer().render(baseline_scene,extent);const auto baseline=read_pixel(48,48);
    hlclient::client::RuntimeClientObservationState s;s.generation=1;
    s.entity_metadata={1,hlclient::client::RuntimeObservationFreshness::observed_in_record,
        hlclient::client::RuntimeObservationCompleteness::complete_reconstruction,
        hlclient::client::RuntimeObservationSource{1,1,1,0,8}};
    hlclient::client::RuntimePacketEntityObservation entity;entity.entity_number=40;entity.model_index=74;
    entity.origin={0.0,-.05,0.0};entity.effects=64;s.packet_entities.push_back(entity);
    hlclient::app::EntityFollowPresentation effects;effects.observe(s);
    auto scene=baseline_scene;effects.present(s.packet_entities,scene,0);
    REQUIRE(scene.world_effect_light_count==1);context->renderer().render(scene,extent);
    CHECK(read_pixel(48,48)[0]>baseline[0]);
    s.packet_entities[0].origin.x=500.0;s.entity_metadata.source->record_ordinal=2;effects.observe(s);
    scene=baseline_scene;effects.present(s.packet_entities,scene,.1);context->renderer().render(scene,extent);
    CHECK(read_pixel(48,48)==baseline); // light is not frozen at ignition
    s.packet_entities[0].origin.x=0.0;s.packet_entities[0].effects=0;
    s.entity_metadata.source->record_ordinal=3;effects.observe(s);
    scene=baseline_scene;effects.present(s.packet_entities,scene,.2);
    CHECK(scene.world_effect_light_count==0);context->renderer().render(scene,extent);CHECK(read_pixel(48,48)==baseline);
    s.packet_entities.clear();s.entity_metadata.source->record_ordinal=4;effects.observe(s);
    scene=baseline_scene;effects.present({},scene,.3);context->renderer().render(scene,extent);CHECK(read_pixel(48,48)==baseline);
    CHECK(glGetError()==GL_NO_ERROR);
}

TEST_CASE("Beam scene light reaches translated runtime brush pixels without lighting distant brushes",
          "[e22][e22-scene-light][renderer][opengl][actual-context][no-stock]") {
    auto context=try_create_context();if(!context)SKIP("OpenGL 3.3 Core unavailable");
    context->initialize_renderer();constexpr renderer::RenderExtent extent{96,96};
    auto brush_world=make_vertical_world();
    for(auto& vertex:brush_world.vertices) {
        vertex.position.x*=64;vertex.position.z*=64;
    }
    brush_world.bounds={{-64,0,-64},{64,0,64}};
    brush_world.surfaces[0].bounds=brush_world.bounds;
    auto built=world_render::WorldRenderPackageBuilder{}.build(
        assets::TexturedWorldAsset{std::move(brush_world),make_frame_texture_set(false,80)},
        make_frame_lightmap_set(false,1,0));
    REQUIRE(built);
    auto brush=std::make_shared<const world_render::WorldRenderPackage>(std::move(*built.package));
    auto world=make_frame_package(false,false,1,0,80);
    auto map=make_profiled_brush_scene(world,brush,{});
    auto frame=std::make_shared<world_scene::RuntimeBrushRenderFrame>();
    frame->scene_identity=map->resource_identity();frame->generation=frame->revision=1;
    for(unsigned i=0;i<2;++i) {
        world_scene::RuntimeBrushRenderInstance instance;
        instance.entity_number=100+i;instance.model_slot=10;instance.source_model_index=1;
        instance.model_transform=make_brush_fixture_transform(512.F+400.F*i,0,0);
        instance.transformed_bounds=transform_brush_fixture_bounds(brush->bounds(),instance.model_transform);
        frame->instances.push_back(instance);
    }
    REQUIRE(world_scene::valid_runtime_brush_frame(*frame,*map));
    auto scene=make_scene(world);
    scene.static_world->scene_package=map;scene.static_world->runtime_brushes=frame;
    scene.camera.position={712,-600,0};scene.camera.target={712,0,0};scene.camera.far_plane=2000;
    auto& renderer=context->renderer();renderer.render(scene,extent);
    const auto near_baseline=read_pixel(20,48),far_baseline=read_pixel(76,48);
    REQUIRE_FALSE(approximately_clear(near_baseline,scene.clear_color));
    REQUIRE_FALSE(approximately_clear(far_baseline,scene.clear_color));
    const auto before=renderer.statistics().runtime_brush_submitted_count;
    // The selected beam profile (192 radius, .65 intensity) at .7 opacity.
    // Only the existing neutral batch is supplied: no visible ribbon/halo can
    // account for these changed material pixels or hide a missing light upload.
    scene.world_effect_lights[0]={{512,-16,0},192,.65F*.7F,{.5F,.5F,1}};
    scene.world_effect_light_count=1;renderer.render(scene,extent);
    CHECK(renderer.statistics().runtime_brush_submitted_count==before+2);
    const auto lit=read_pixel(20,48);
    CHECK(lit[0]>near_baseline[0]);CHECK(lit[1]>near_baseline[1]);CHECK(lit[2]>near_baseline[2]);
    CHECK(read_pixel(76,48)==far_baseline);
    scene.world_effect_light_count=0;renderer.render(scene,extent);
    CHECK(read_pixel(20,48)==near_baseline);CHECK(read_pixel(76,48)==far_baseline);
    renderer.render_with_gpu_timing(scene,extent,901U);
    const auto timing_pixels=renderer.observe_framebuffer(extent,scene.clear_color,true);
    REQUIRE(timing_pixels);
    const auto brush_timing=renderer.poll_gpu_timing();
    if(brush_timing && brush_timing->status==renderer::opengl::OpenGlGpuTimingStatus::ready) {
        CHECK(brush_timing->frame_number==901U);
        CHECK(brush_timing->valid_world_subpass_mask==0x0fU);
    }
    CHECK(glGetError()==GL_NO_ERROR);context->release_renderer();
}

TEST_CASE("Full synthetic GoldSrc pipeline renders an OpenGL world frame",
          "[renderer][opengl][integration][full-pipeline]")
{
    // The complete CPU package is built before SDL exists, entirely from
    // owning in-memory BSP/WAD fixtures. This intentionally performs no
    // network operation, filesystem lookup, or file write.
    auto package = make_full_pipeline_package();
    REQUIRE(package != nullptr);
    CHECK(package->statistics().source_surface_count == 2U);
    CHECK(package->statistics().triangle_count == 4U);
    CHECK(package->statistics().batch_count == 2U);
    CHECK(package->statistics().masked_batch_count == 1U);
    CHECK(package->statistics().opaque_batch_count == 1U);
    REQUIRE(package->textured_world().textures.texture_count() == 2U);
    const auto& texture_statistics =
        package->textured_world().textures.statistics();
    CHECK(texture_statistics.embedded_texture_count == 1U);
    CHECK(texture_statistics.wad3_texture_count == 1U);
    CHECK(texture_statistics.masked_texture_count == 1U);
    CHECK(texture_statistics.wad_archive_resolved_count == 1U);
    CHECK(texture_statistics.unresolved_material_count == 0U);
    CHECK(package->lightmaps().binding_count() == 2U);
    CHECK(package->lightmaps().page_count() == 1U);

    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    constexpr renderer::RenderExtent extent{96, 96};
    auto scene = make_full_pipeline_scene(package);
    auto& gl_renderer = context->renderer();
    gl_renderer.render(scene, extent);
    context->window().swap_buffers();
    gl_renderer.render(scene, extent);

    const auto masked_opaque_sample = read_pixel(23, 48);
    const auto masked_transparent_sample = read_pixel(40, 48);
    const auto external_wad_sample = read_pixel(64, 48);
    CHECK_FALSE(approximately_clear(
        masked_opaque_sample, scene.clear_color));
    CHECK(approximately_clear(masked_transparent_sample, scene.clear_color));
    CHECK_FALSE(approximately_clear(external_wad_sample, scene.clear_color));
    CHECK(glGetError() == GL_NO_ERROR);

    const auto& statistics = gl_renderer.statistics();
    CHECK(statistics.world_present);
    CHECK(statistics.active_world_resources);
    CHECK(statistics.package_revision == package->resource_revision());
    CHECK(statistics.upload_count == 1U);
    CHECK(statistics.rendered_frame_count == 2U);
    CHECK(statistics.draw_call_count == 4U);
    CHECK(statistics.triangle_count == 8U);
    CHECK(statistics.base_texture_bind_count == 4U);
    // Switching between the opaque fast program and the masked program
    // invalidates their texture-bind caches once per program per frame.
    CHECK(statistics.lightmap_bind_count == 4U);
    CHECK(statistics.uploaded_base_texture_count == 2U);
    CHECK(statistics.uploaded_base_mip_level_count == 8U);
    CHECK(statistics.uploaded_lightmap_page_count == 1U);
    CHECK(statistics.uploaded_lightmap_layer_count == 4U);
    CHECK(statistics.uploaded_white_lightmap_count == 0U);
    CHECK(statistics.failed_upload_count == 0U);
    CHECK(statistics.last_extent == extent);

    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("Production OpenGL keeps BSP boundary junctions opaque without seam leaks",
          "[renderer][opengl][integration][bsp-boundary-seam]")
{
    // The second offset is exactly representable and smaller than the old
    // cleanup tolerance at x=1024 (approximately 1/256). Removing M from the
    // pentagon alone opens a physical gap about five framebuffer pixels wide
    // at this camera distance. This makes the regression independent of a
    // driver's subpixel rounding behavior for exactly collinear T-junctions.
    constexpr std::array boundary_offsets{0.0F, 1.0F / 512.0F};
    std::array<std::shared_ptr<const world_render::WorldRenderPackage>, 2U>
        packages;
    for (std::size_t index = 0U; index < packages.size(); ++index) {
        packages[index] = make_boundary_junction_package(boundary_offsets[index]);
        const auto& world = packages[index]->textured_world().world;
        REQUIRE(world.surfaces.size() == 3U);
        CHECK(world.surfaces[0U].vertex_count == 5U);
        CHECK(packages[index]->statistics().triangle_count == 7U);
        CHECK(packages[index]->statistics().masked_batch_count == 0U);
        CHECK(packages[index]->lightmaps().binding_count() == 3U);
        CHECK(packages[index]->lightmaps().page_count() == 0U);
        const auto retained_junctions = std::count_if(
            world.vertices.begin(), world.vertices.end(),
            [offset = boundary_offsets[index]](const auto& vertex) {
                return vertex.position.x == 1024.0F + offset &&
                    vertex.position.y == 0.0F && vertex.position.z == 0.0F;
            });
        CHECK(retained_junctions == 3);
    }

    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    constexpr renderer::RenderExtent extent{96, 96};
    constexpr int sample_size = 24;
    constexpr int sample_start = (extent.width - sample_size) / 2;
    struct BoundaryCameraPose {
        float along_seam;
        float eye_offset_x;
        float eye_offset_y;
    };
    // Preserve the original reproducer first, then translate along both
    // shared segments and look obliquely from either side of the junction.
    constexpr std::array camera_poses{
        BoundaryCameraPose{0.0F, 0.0F, 0.0F},
        BoundaryCameraPose{-16.0F, 0.0F, 0.0F},
        BoundaryCameraPose{16.0F, 0.0F, 0.0F},
        BoundaryCameraPose{-8.0F, 1.0F / 64.0F, 1.0F / 128.0F},
        BoundaryCameraPose{8.0F, -1.0F / 64.0F, -1.0F / 128.0F},
    };
    constexpr std::size_t repeated_frame_count = 2U;
    for (std::size_t index = 0U; index < packages.size(); ++index) {
        CAPTURE(boundary_offsets[index]);
        auto scene = make_full_pipeline_scene(packages[index]);
        scene.clear_color = {1.0F, 0.0F, 1.0F, 1.0F};
        scene.camera.near_plane = 1.0F / 1024.0F;
        scene.camera.far_plane = 128.0F;
        scene.static_world->cull_mode = renderer::RenderCullMode::back;
        for (std::size_t pose_index = 0U;
             pose_index < camera_poses.size(); ++pose_index) {
            CAPTURE(pose_index);
            const auto& pose = camera_poses[pose_index];
            const auto segment_fraction =
                1.0F - std::abs(pose.along_seam) / 32.0F;
            const auto center_x = 1024.0F +
                boundary_offsets[index] * segment_fraction * 0.5F;
            scene.camera.position = {
                center_x + pose.eye_offset_x,
                pose.along_seam + pose.eye_offset_y, 1.0F / 32.0F};
            scene.camera.target = {center_x, pose.along_seam, 0.0F};
            for (std::size_t frame_index = 0U;
                 frame_index < repeated_frame_count; ++frame_index) {
                CAPTURE(frame_index);
                context->renderer().render(scene, extent);

                std::array<std::uint8_t, sample_size * sample_size * 4U>
                    pixels{};
                std::array<float, sample_size * sample_size> depths{};
                glReadBuffer(GL_BACK);
                glReadPixels(sample_start, sample_start, sample_size, sample_size,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                glReadPixels(sample_start, sample_start, sample_size, sample_size,
                    GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());
                std::size_t uncovered_color_pixels = 0U;
                std::size_t unwritten_depth_pixels = 0U;
                for (std::size_t pixel = 0U; pixel < depths.size(); ++pixel) {
                    const auto offset = pixel * 4U;
                    // Opaque, unlit, constant-color material: no sampling or
                    // lighting tolerance can hide a bright seam leak.
                    if (pixels[offset] != 0x30U ||
                        pixels[offset + 1U] != 0x60U ||
                        pixels[offset + 2U] != 0x28U ||
                        pixels[offset + 3U] != 0xFFU) {
                        ++uncovered_color_pixels;
                    }
                    if (!(depths[pixel] > 0.0F && depths[pixel] < 1.0F)) {
                        ++unwritten_depth_pixels;
                    }
                }
                CHECK(uncovered_color_pixels == 0U);
                CHECK(unwritten_depth_pixels == 0U);
                CHECK(context->renderer().statistics().world_present);
                CHECK(context->renderer().statistics().active_world_resources);
                CHECK(context->renderer().statistics().failed_upload_count == 0U);
                CHECK(glGetError() == GL_NO_ERROR);
            }
        }
    }
    CHECK(context->renderer().statistics().rendered_frame_count ==
        packages.size() * camera_poses.size() * repeated_frame_count);
}

TEST_CASE("OpenGL framebuffer observer reads the completed default back frame",
          "[renderer][opengl][framebuffer-observer]")
{
    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    constexpr renderer::RenderExtent extent{96, 96};
    auto& gl_renderer = context->renderer();

    // Loading/clear-only is a valid observation, not an unavailable readback.
    renderer::RenderScene loading_scene;
    gl_renderer.render(loading_scene, extent);
    const auto loading = gl_renderer.observe_framebuffer(
        extent, loading_scene.clear_color);
    REQUIRE(loading);
    CHECK(loading.status ==
        opengl::OpenGlFramebufferObservationStatus::valid);
    CHECK(loading.read_framebuffer == 0U);
    CHECK(loading.read_buffer == GL_BACK);
    CHECK(loading.sampled_pixel_count == 96U * 96U);
    CHECK(loading.non_clear_pixel_count == 0U);

    // The next scene uses the real renderer upload/draw path and must not be
    // confused with the preceding loading frame.
    auto package = make_frame_package(false, false, 1U, 0U, 0xC0U);
    auto scene = make_scene(package);
    gl_renderer.render(scene, extent);

    GLint original_pack_alignment = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &original_pack_alignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    GLuint unrelated_read_framebuffer = 0U;
    glGenFramebuffers(1, &unrelated_read_framebuffer);
    REQUIRE(unrelated_read_framebuffer != 0U);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, unrelated_read_framebuffer);

    const auto visible =
        gl_renderer.observe_framebuffer(extent, scene.clear_color);
    REQUIRE(visible);
    CHECK(visible.status ==
        opengl::OpenGlFramebufferObservationStatus::valid);
    CHECK(visible.read_framebuffer == 0U);
    CHECK(visible.read_buffer == GL_BACK);
    CHECK(visible.non_clear_pixel_count > 0U);
    CHECK(visible.has_non_clear_bounds);

    GLint restored_read_framebuffer = 0;
    GLint restored_pack_alignment = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restored_read_framebuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &restored_pack_alignment);
    CHECK(restored_read_framebuffer ==
        static_cast<GLint>(unrelated_read_framebuffer));
    CHECK(restored_pack_alignment == 8);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0U);
    glDeleteFramebuffers(1, &unrelated_read_framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, original_pack_alignment);
    CHECK(glGetError() == GL_NO_ERROR);

    context->release_renderer();
}

TEST_CASE("OpenGL scene visibility changes commands without resource reupload",
    "[renderer][opengl][m4.4][visibility-frame]")
{
    auto world = make_frame_package(false, false, 2U, 0U, 0xC0U);
    auto scene_package = make_visibility_scene(world);
    auto equivalent_scene_package = make_visibility_scene(world);
    REQUIRE(equivalent_scene_package != scene_package);
    CHECK(equivalent_scene_package->resource_identity() ==
        scene_package->resource_identity());

    world_preview::WorldPreviewSceneSource all_source{
        scene_package,
        visibility_options(
            hlclient::world_visibility::WorldVisibilityMode::all, false)};
    auto all_scene = hlclient::client::build_render_scene(
        all_source.world_state());
    REQUIRE(all_scene.static_world);
    REQUIRE(all_scene.static_world->visible_draw_list);
    REQUIRE(all_scene.static_world->visibility_summary);
    CHECK(all_scene.static_world->visible_draw_list->commands().size() == 2U);

    world_preview::WorldPreviewSceneSource equivalent_all_source{
        equivalent_scene_package,
        visibility_options(
            hlclient::world_visibility::WorldVisibilityMode::all, false)};
    auto equivalent_all_scene = hlclient::client::build_render_scene(
        equivalent_all_source.world_state());
    REQUIRE(equivalent_all_scene.static_world);
    REQUIRE(equivalent_all_scene.static_world->visible_draw_list);
    REQUIRE(equivalent_all_scene.static_world->visibility_summary);
    CHECK(equivalent_all_scene.static_world->visibility_summary->revision ==
        all_scene.static_world->visibility_summary->revision);
    CHECK(equivalent_all_scene.static_world->visible_draw_list
              ->result_signature() ==
        all_scene.static_world->visible_draw_list->result_signature());

    world_preview::WorldPreviewSceneSource pvs_source{
        scene_package,
        visibility_options(
            hlclient::world_visibility::WorldVisibilityMode::pvs_only,
            false)};
    auto pvs_scene = hlclient::client::build_render_scene(
        pvs_source.world_state());
    REQUIRE(pvs_scene.static_world->visible_draw_list);
    REQUIRE(pvs_scene.static_world->visibility_summary);
    CHECK(pvs_scene.static_world->visibility_summary->revision ==
        all_scene.static_world->visibility_summary->revision);
    CHECK(pvs_scene.static_world->visible_draw_list->result_signature() !=
        all_scene.static_world->visible_draw_list->result_signature());
    CHECK(pvs_scene.static_world->visible_draw_list->commands().size() == 1U);

    world_preview::WorldPreviewSceneSource frustum_source{
        scene_package,
        visibility_options(
            hlclient::world_visibility::WorldVisibilityMode::frustum_only,
            false)};
    auto frustum_scene = hlclient::client::build_render_scene(
        frustum_source.world_state());
    REQUIRE(frustum_scene.static_world->visible_draw_list);
    REQUIRE(frustum_scene.static_world->visibility_summary);
    CHECK(frustum_scene.static_world->visibility_summary->revision ==
        all_scene.static_world->visibility_summary->revision);
    CHECK(frustum_scene.static_world->visible_draw_list->result_signature() !=
        pvs_scene.static_world->visible_draw_list->result_signature());
    CHECK(frustum_scene.static_world->visible_draw_list->commands().size() == 1U);

    world_preview::WorldPreviewSceneSource combined_source{
        scene_package,
        visibility_options(
            hlclient::world_visibility::WorldVisibilityMode::pvs_and_frustum,
            false)};
    auto combined_scene = hlclient::client::build_render_scene(
        combined_source.world_state());
    REQUIRE(combined_scene.static_world->visible_draw_list);
    REQUIRE(combined_scene.static_world->visibility_summary);
    CHECK(combined_scene.static_world->visibility_summary->revision ==
        all_scene.static_world->visibility_summary->revision);
    CHECK(combined_scene.static_world->visible_draw_list->result_signature() !=
        frustum_scene.static_world->visible_draw_list->result_signature());
    CHECK(combined_scene.static_world->visible_draw_list->commands().size() == 1U);

    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();
    constexpr renderer::RenderExtent extent{96, 96};
    auto& gl_renderer = context->renderer();
    gl_renderer.render(all_scene, extent);
    gl_renderer.render(equivalent_all_scene, extent);
    CHECK(gl_renderer.statistics().upload_count == 1U);
    CHECK(gl_renderer.statistics().scene_upload_count == 1U);
    gl_renderer.render(pvs_scene, extent);
    const auto visible_sample = read_pixel(48, 48);
    const auto hidden_sample = read_pixel(90, 48);
    CHECK_FALSE(approximately_clear(visible_sample, pvs_scene.clear_color));
    CHECK(approximately_clear(hidden_sample, pvs_scene.clear_color));
    gl_renderer.render(frustum_scene, extent);
    gl_renderer.render(combined_scene, extent);

    const auto& statistics = gl_renderer.statistics();
    CHECK(statistics.scene_present);
    CHECK(statistics.upload_count == 1U);
    CHECK(statistics.scene_upload_count == 1U);
    CHECK(statistics.brush_upload_count == 0U);
    CHECK(statistics.visibility_update_count == 4U);
    CHECK(statistics.draw_call_count == 7U);
    CHECK(statistics.rendered_command_count == 7U);
    CHECK(statistics.visible_world_surface_count == 1U);
    CHECK(statistics.visible_brush_instance_count == 0U);
    CHECK(glGetError() == GL_NO_ERROR);

    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("Prepared static-world draws match original all-batch color and depth across layouts and resize",
    "[renderer][opengl][integration][prepared-world]")
{
    const std::array packages{
        make_prepared_render_package(33U),
        make_prepared_render_package(288U),
        make_frame_package(false, false, 2U, 0U, 0xA0U),
    };
    REQUIRE(packages[0U]->prepared_draw_blocks().size() == 3U);
    REQUIRE(packages[1U]->prepared_draw_blocks().size() == 18U);
    REQUIRE(packages[2U]->draw_batches().size() == 2U);
    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();

    struct FramePixels {
        std::vector<std::uint8_t> rgba;
        std::vector<float> depth;
    };
    const auto capture = [](const renderer::RenderExtent extent) {
        FramePixels pixels;
        const auto count = static_cast<std::size_t>(extent.width) *
            static_cast<std::size_t>(extent.height);
        pixels.rgba.resize(count * 4U);
        pixels.depth.resize(count);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, extent.width, extent.height, GL_RGBA,
            GL_UNSIGNED_BYTE, pixels.rgba.data());
        glReadPixels(0, 0, extent.width, extent.height, GL_DEPTH_COMPONENT,
            GL_FLOAT, pixels.depth.data());
        return pixels;
    };
    constexpr std::array extents{
        renderer::RenderExtent{96, 96},
        renderer::RenderExtent{48, 48},
        renderer::RenderExtent{96, 48},
    };
    struct ProjectionCase {
        float vertical_fov;
        float near_plane;
        float far_plane;
    };
    constexpr std::array projections{
        ProjectionCase{1.0471975512F, 0.1F, 10.0F},
        ProjectionCase{0.35F, 1.95F, 10.0F},
        ProjectionCase{1.45F, 2.05F, 10.0F},
        ProjectionCase{1.0471975512F, 0.1F, 1.95F},
        ProjectionCase{1.0471975512F, 0.1F, 2.05F},
    };
    auto& gl_renderer = context->renderer();
    const auto rejected_before = gl_renderer.statistics()
        .prepared_world_rejected_block_count;
    const auto bypass_before = gl_renderer.statistics()
        .prepared_world_all_batch_bypass_frame_count;
    for (std::size_t layout = 0U; layout < packages.size(); ++layout) {
        auto optimized = make_scene(packages[layout]);
        optimized.static_world->scene_package =
            make_visibility_scene(packages[layout]);
        // No visible list: this is the normal game's immutable package path.
        REQUIRE_FALSE(optimized.static_world->visible_draw_list);
        auto original = optimized;
        // The existing no-scene-package branch draws the exact original
        // material batches. It is the pixel/depth oracle in this binary.
        original.static_world->scene_package.reset();
        for (const auto extent : extents) {
            for (std::size_t pose = 0U; pose < 2U; ++pose) {
                const auto x = layout != 2U
                    ? (pose == 0U ? 8.0F : 72.0F)
                    : (pose == 0U ? 0.0F : 2.0F);
                optimized.camera.position.x = x;
                optimized.camera.target.x = x;
                for (const auto projection : projections) {
                    optimized.camera.vertical_field_of_view_radians =
                        projection.vertical_fov;
                    optimized.camera.near_plane = projection.near_plane;
                    optimized.camera.far_plane = projection.far_plane;
                    original.camera = optimized.camera;
                    CAPTURE(layout, extent.width, extent.height, pose,
                        projection.vertical_fov, projection.near_plane,
                        projection.far_plane);
                    gl_renderer.render(optimized, extent);
                    const auto selected = capture(extent);
                    gl_renderer.render(original, extent);
                    const auto all_world = capture(extent);
                    CHECK(selected.rgba == all_world.rgba);
                    CHECK(selected.depth == all_world.depth);
                    CHECK(glGetError() == GL_NO_ERROR);
                }
            }
        }
    }
    CHECK(gl_renderer.statistics().prepared_world_rejected_block_count >
        rejected_before);
    CHECK(gl_renderer.statistics().prepared_world_all_batch_bypass_frame_count >
        bypass_before);
    CHECK(gl_renderer.statistics().prepared_world_probed_block_count > 0U);
    CHECK(gl_renderer.statistics().failed_upload_count == 0U);
    auto receipt_scene=make_scene(packages[0U]);
    receipt_scene.static_world->scene_package=
        make_visibility_scene(packages[0U]);
    gl_renderer.render_with_gpu_timing(receipt_scene,{96,96},1U);
    CHECK_FALSE(gl_renderer.statistics().last_static_world_frame);
    gl_renderer.render_with_gpu_timing(receipt_scene,{96,96},2U,true);
    const auto& receipt=gl_renderer.statistics().last_static_world_frame;
    REQUIRE(receipt);
    CHECK(receipt->frame_number==2U);
    CHECK(receipt->path!=renderer::opengl::OpenGlStaticWorldPath::none);
    CHECK(receipt->resource_id==packages[0U]->resource_id());
    CHECK(receipt->resource_revision==packages[0U]->resource_revision());
    CHECK(receipt->api_draws.has_value());
    CHECK(receipt->selection_preparation_ms.has_value());
    CHECK_FALSE(receipt->prepared_rebuilds);
    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("OpenGL brush instances share one aggregate GPU upload",
    "[gauss-71b97][renderer][opengl][m4.4][brush-frame]")
{
    auto world = make_frame_package(false, true, 0U, 0U, 0x30U);
    auto brushes = make_frame_package(false, true, 0U, 0U, 0xE0U);
    constexpr std::array translations{-1.0F, 1.0F};
    auto scene_package = make_visibility_scene(world, brushes, translations);
    world_preview::WorldPreviewSceneSource source{
        scene_package,
        visibility_options(
            hlclient::world_visibility::WorldVisibilityMode::all, true)};
    auto scene = hlclient::client::build_render_scene(source.world_state());
    REQUIRE(scene.static_world);
    REQUIRE(scene.static_world->visible_draw_list);
    CHECK(scene.static_world->visible_draw_list->commands().size() == 3U);

    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();
    constexpr renderer::RenderExtent extent{96, 96};
    auto& gl_renderer = context->renderer();
    gl_renderer.render(scene, extent);
    REQUIRE(source.update(hlclient::client::FrameTime{0.0}));
    scene = hlclient::client::build_render_scene(source.world_state());
    gl_renderer.render(scene, extent);

    const auto left_instance_sample = read_pixel(20, 48);
    const auto right_instance_sample = read_pixel(76, 48);
    CHECK_FALSE(approximately_clear(left_instance_sample, scene.clear_color));
    CHECK_FALSE(approximately_clear(right_instance_sample, scene.clear_color));
    const auto& statistics = gl_renderer.statistics();
    CHECK(statistics.upload_count == 1U);
    CHECK(statistics.scene_upload_count == 1U);
    CHECK(statistics.brush_upload_count == 1U);
    CHECK(statistics.visibility_update_count == 2U);
    CHECK(statistics.rendered_command_count == 6U);
    CHECK(statistics.draw_call_count == 6U);
    CHECK(statistics.brush_draw_call_count == 4U);
    // Each frame binds world and brush resources once, while both translated
    // brush instances still receive their own matrices. No binding state is
    // allowed to survive the render boundary.
    CHECK(statistics.program_bind_count == 4U);
    CHECK(statistics.vertex_array_bind_count == 4U);
    CHECK(statistics.camera_upload_count == 2U);
    // The opaque world uses the no-light program without camera or model
    // transform uniforms; brush instances retain the original program and
    // upload their own light state once per frame.
    CHECK(statistics.light_upload_count == 2U);
    CHECK(statistics.sky_texture_bind_count == 4U);
    CHECK(statistics.transform_upload_count == 10U);
    CHECK(statistics.visible_brush_instance_count == 2U);
    CHECK(statistics.failed_upload_count == 0U);
    CHECK(glGetError() == GL_NO_ERROR);

    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

TEST_CASE("OpenGL brush profiles and CPU visibility bound framebuffer work",
    "[renderer][opengl][m4.4][brush-profiles]")
{
    auto world = make_frame_package(false, true, 0U, 0U, 0x30U);
    auto brushes = make_frame_package(true, false, 1U, 0U, 0xE0U);
    REQUIRE(brushes->materials().size() == 1U);
    CHECK(brushes->materials()[0U].base_texture_alpha_mode ==
        assets::WorldTextureAlphaMode::masked_index_255);
    CHECK(brushes->materials()[0U].lightmap_mode ==
        world_render::WorldRenderLightmapMode::atlas);
    CHECK(brushes->materials()[0U].lightmap_atlas_page_index == 0U);

    constexpr float diagonal = 0.707106769F;
    std::array<BrushSceneFixtureInstance, 4U> fixture_instances;
    fixture_instances[0U].model_transform = make_brush_fixture_transform(
        0.0F, -0.25F, 0.0F, diagonal, diagonal);
    fixture_instances[1U].model_transform =
        make_brush_fixture_transform(-2.5F, -0.25F, 0.0F);
    fixture_instances[1U].touched_leaf_indices = {2U};
    fixture_instances[2U].model_transform =
        make_brush_fixture_transform(5.0F, -0.25F, 0.0F);
    fixture_instances[3U].model_transform =
        make_brush_fixture_transform(2.5F, -0.25F, 0.0F);
    fixture_instances[3U].support_status = world_scene::
        BrushSubmodelRenderSupportStatus::unsupported_rendermode;

    auto scene_package = make_profiled_brush_scene(
        world, brushes, fixture_instances);
    CHECK(scene_package->statistics().supported_brush_instance_count == 3U);
    CHECK(scene_package->statistics().unsupported_brush_instance_count == 1U);

    const auto make_options = [](const auto mode) {
        auto options = visibility_options(mode, true);
        options.spawn_camera->position = {0.0F, -6.0F, 0.0F};
        return options;
    };
    world_preview::WorldPreviewSceneSource all_source{
        scene_package,
        make_options(hlclient::world_visibility::WorldVisibilityMode::all)};
    auto all_scene =
        hlclient::client::build_render_scene(all_source.world_state());
    REQUIRE(all_scene.static_world);
    REQUIRE(all_scene.static_world->visible_draw_list);
    REQUIRE(all_scene.static_world->visibility_summary);
    CHECK(all_scene.static_world->visibility_summary
              ->visible_brush_instance_count == 3U);
    CHECK(all_scene.static_world->visible_draw_list->commands().size() == 4U);
    CHECK(all_scene.static_world->visible_draw_list->statistics()
              .masked_command_count == 3U);
    for (const auto& command :
        all_scene.static_world->visible_draw_list->commands()) {
        if (command.object_kind != hlclient::world_visibility::
                WorldVisibleObjectKind::brush_instance_surface) {
            continue;
        }
        CHECK(command.alpha_mode ==
            assets::WorldTextureAlphaMode::masked_index_255);
        CHECK(command.lightmap_mode ==
            world_render::WorldRenderLightmapMode::atlas);
        CHECK(command.lightmap_atlas_page_index == 0U);
        CHECK(command.source_instance_index != 3U);
    }

    world_preview::WorldPreviewSceneSource pvs_source{
        scene_package,
        make_options(
            hlclient::world_visibility::WorldVisibilityMode::pvs_only)};
    REQUIRE(pvs_source.update(hlclient::client::FrameTime{0.0}));
    auto pvs_scene =
        hlclient::client::build_render_scene(pvs_source.world_state());
    REQUIRE(pvs_scene.static_world->visible_draw_list);
    REQUIRE(pvs_scene.static_world->visibility_summary);
    CHECK(pvs_scene.static_world->visibility_summary
              ->visible_brush_instance_count == 2U);
    CHECK(pvs_scene.static_world->visible_draw_list->commands().size() == 3U);

    world_preview::WorldPreviewSceneSource frustum_source{
        scene_package,
        make_options(
            hlclient::world_visibility::WorldVisibilityMode::frustum_only)};
    REQUIRE(frustum_source.update(hlclient::client::FrameTime{0.0}));
    REQUIRE(frustum_source.update(hlclient::client::FrameTime{0.0}));
    auto frustum_scene =
        hlclient::client::build_render_scene(frustum_source.world_state());
    REQUIRE(frustum_scene.static_world->visible_draw_list);
    REQUIRE(frustum_scene.static_world->visibility_summary);
    CHECK(frustum_scene.static_world->visibility_summary
              ->visible_brush_instance_count == 2U);
    CHECK(frustum_scene.static_world->visible_draw_list->commands().size() ==
        3U);

    world_preview::WorldPreviewSceneSource combined_source{
        scene_package,
        make_options(
            hlclient::world_visibility::WorldVisibilityMode::
                pvs_and_frustum)};
    for (std::size_t update = 0U; update < 3U; ++update) {
        REQUIRE(combined_source.update(hlclient::client::FrameTime{0.0}));
    }
    auto combined_scene =
        hlclient::client::build_render_scene(combined_source.world_state());
    REQUIRE(combined_scene.static_world->visible_draw_list);
    REQUIRE(combined_scene.static_world->visibility_summary);
    CHECK(combined_scene.static_world->visibility_summary
              ->visible_brush_instance_count == 1U);
    CHECK(combined_scene.static_world->visible_draw_list->commands().size() ==
        2U);

    auto context = try_create_context();
    if (!context) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_version = current_actual_open_gl_version();
    REQUIRE(actual_version.has_value());
    if (!supports_required_open_gl_version(*actual_version)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    const auto actual_profile_mask = current_actual_open_gl_profile_mask();
    REQUIRE(actual_profile_mask.has_value());
    if (!supports_required_open_gl_profile(*actual_profile_mask)) {
        SKIP("OpenGL 3.3 Core context unavailable on this host");
    }
    context->initialize_renderer();
    constexpr renderer::RenderExtent extent{96, 96};
    auto& gl_renderer = context->renderer();

    gl_renderer.render(all_scene, extent);
    const auto rotated_opaque_sample = read_pixel(42, 48);
    const auto rotated_masked_sample = read_pixel(54, 48);
    const auto rotated_outside_sample = read_pixel(36, 60);
    const auto pvs_candidate_sample = read_pixel(8, 48);
    CHECK(static_cast<int>(rotated_opaque_sample[0U]) >
        static_cast<int>(rotated_masked_sample[0U]) + 40);
    CHECK(std::abs(static_cast<int>(rotated_outside_sample[0U]) -
              static_cast<int>(rotated_masked_sample[0U])) <= 8);
    CHECK_FALSE(
        approximately_clear(pvs_candidate_sample, all_scene.clear_color));

    auto unsupported_probe = all_scene;
    unsupported_probe.camera.position.x = 2.5F;
    unsupported_probe.camera.target.x = 2.5F;
    gl_renderer.render(unsupported_probe, extent);
    CHECK(approximately_clear(read_pixel(42, 48), all_scene.clear_color));

    auto all_frustum_probe = all_scene;
    all_frustum_probe.camera.position.x = 5.0F;
    all_frustum_probe.camera.target.x = 5.0F;
    gl_renderer.render(all_frustum_probe, extent);
    CHECK_FALSE(
        approximately_clear(read_pixel(42, 48), all_scene.clear_color));
    CHECK(gl_renderer.statistics().upload_count == 1U);

    gl_renderer.render(pvs_scene, extent);
    CHECK(approximately_clear(read_pixel(8, 48), pvs_scene.clear_color));

    auto frustum_probe = frustum_scene;
    frustum_probe.camera.position.x = 5.0F;
    frustum_probe.camera.target.x = 5.0F;
    gl_renderer.render(frustum_probe, extent);
    CHECK(approximately_clear(read_pixel(42, 48), frustum_scene.clear_color));

    gl_renderer.render(combined_scene, extent);
    const auto& statistics = gl_renderer.statistics();
    CHECK(statistics.upload_count == 1U);
    CHECK(statistics.scene_upload_count == 1U);
    CHECK(statistics.brush_upload_count == 1U);
    CHECK(statistics.visibility_update_count == 4U);
    CHECK(statistics.rendered_command_count == 20U);
    CHECK(statistics.draw_call_count == 20U);
    CHECK(statistics.brush_draw_call_count == 14U);
    CHECK(statistics.uploaded_base_texture_count == 2U);
    CHECK(statistics.uploaded_base_mip_level_count == 8U);
    CHECK(statistics.uploaded_lightmap_page_count == 1U);
    CHECK(statistics.uploaded_lightmap_layer_count == 4U);
    CHECK(statistics.uploaded_white_lightmap_count == 1U);
    CHECK(statistics.failed_upload_count == 0U);
    CHECK(statistics.active_world_resources);
    CHECK(glGetError() == GL_NO_ERROR);

    // Release every scene/world/brush GL object while its owning context is
    // still current. HiddenOpenGlContext destroys the window only afterwards.
    context->release_renderer();
    CHECK(glGetError() == GL_NO_ERROR);
}

} // namespace

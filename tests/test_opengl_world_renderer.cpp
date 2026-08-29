#include "world_render_test_fixture.hpp"
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
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_scene.hpp>
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
    const std::uint8_t red)
{
    auto texture = fixture::make_texture(masked);
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
    const std::uint8_t red)
{
    auto world = make_vertical_world();
    world.materials[0].texture_name = masked ? "{MASKED" : "STONE";
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
    auto textures = make_frame_texture_set(masked, red);
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

TEST_CASE("OpenGL brush instances share one aggregate GPU upload",
    "[renderer][opengl][m4.4][brush-frame]")
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

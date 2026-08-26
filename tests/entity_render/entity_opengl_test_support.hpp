#pragma once

#include "entity_render_test_fixture.hpp"

#include <hlclient/assets/sprite_asset_types.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_scene.hpp>

#include <glad/gl.h>
#include <SDL3/SDL_video.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::tests::entity_opengl_fixture {

namespace assets = hlclient::assets;
namespace entity = hlclient::entity_render;
namespace fixture = hlclient::tests::entity_render_fixture;
namespace opengl = hlclient::renderer::opengl;
namespace renderer = hlclient::renderer;

struct ActualOpenGlVersion {
    std::uint32_t major{0U};
    std::uint32_t minor{0U};
};

[[nodiscard]] inline std::optional<ActualOpenGlVersion> actual_version() noexcept
{
    const auto address = SDL_GL_GetProcAddress("glGetString");
    if (address == nullptr) {
        return std::nullopt;
    }
    static_assert(sizeof(PFNGLGETSTRINGPROC) == sizeof(SDL_FunctionPointer));
    const auto get_string = std::bit_cast<PFNGLGETSTRINGPROC>(address);
    const auto* bytes = get_string(GL_VERSION);
    if (bytes == nullptr) {
        return std::nullopt;
    }
    const auto* text = reinterpret_cast<const char*>(bytes);
    std::uint32_t components[2U]{};
    std::size_t cursor = 0U;
    for (std::size_t component = 0U; component < 2U; ++component) {
        if (text[cursor] < '0' || text[cursor] > '9') {
            return std::nullopt;
        }
        while (text[cursor] >= '0' && text[cursor] <= '9') {
            components[component] =
                components[component] * 10U +
                static_cast<std::uint32_t>(text[cursor] - '0');
            ++cursor;
        }
        if (component == 0U) {
            if (text[cursor] != '.') {
                return std::nullopt;
            }
            ++cursor;
        }
    }
    return ActualOpenGlVersion{components[0U], components[1U]};
}

[[nodiscard]] inline bool capable_context() noexcept
{
    const auto version = actual_version();
    if (!version ||
        (version->major < 3U ||
            (version->major == 3U && version->minor < 3U))) {
        return false;
    }
    const auto integer_address = SDL_GL_GetProcAddress("glGetIntegerv");
    if (integer_address == nullptr) {
        return false;
    }
    static_assert(
        sizeof(PFNGLGETINTEGERVPROC) == sizeof(SDL_FunctionPointer));
    const auto get_integer =
        std::bit_cast<PFNGLGETINTEGERVPROC>(integer_address);
    GLint profile = 0;
    get_integer(GL_CONTEXT_PROFILE_MASK, &profile);
    return (profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
}

class HiddenContext final {
public:
    HiddenContext()
        : runtime_{std::make_unique<platform::SdlRuntime>()},
          window_{std::make_unique<platform::SdlWindow>(
              platform::SdlWindowConfig{
                  "HL Client entity rendering test", 96, 96, true})}
    {
    }

    void initialize_renderer()
    {
        renderer_ = std::make_unique<opengl::OpenGlRenderer>();
    }

    [[nodiscard]] opengl::OpenGlRenderer& renderer() noexcept
    {
        return *renderer_;
    }

    void release_renderer() noexcept { renderer_.reset(); }

private:
    std::unique_ptr<platform::SdlRuntime> runtime_;
    std::unique_ptr<platform::SdlWindow> window_;
    std::unique_ptr<opengl::OpenGlRenderer> renderer_;
};

[[nodiscard]] inline std::unique_ptr<HiddenContext> try_context() noexcept
{
    try {
        return std::make_unique<HiddenContext>();
    } catch (...) {
        return {};
    }
}

struct SceneAndFrame {
    std::shared_ptr<const entity::EntitySceneRenderPackage> package;
    std::shared_ptr<const entity::EntityRenderFrame> frame;
};

[[nodiscard]] inline std::array<float, 16U> pose_matrix(
    const float translation_x = 0.0F) noexcept
{
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        translation_x, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] inline std::shared_ptr<const entity::EntityRenderFrame>
make_frame(
    const entity::EntitySceneRenderPackage& package,
    const std::uint64_t revision,
    const std::size_t studio_count,
    const std::size_t sprite_count,
    const float pose_translation_x = 0.0F,
    const std::uint32_t sprite_frame = 0U,
    const std::uint32_t body_value = 0U,
    const std::uint32_t skin_family_index = 0U)
{
    entity::EntityRenderFrameBuildInput input;
    input.resource_id = 0xE100U;
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
    if (studio_count != 0U) {
        REQUIRE(package.studio_assets().size() == 1U);
        input.studio_poses.push_back({
            package.studio_assets()[0U]->source_identity(),
            {pose_matrix(pose_translation_x)},
        });
        for (std::size_t index = 0U; index < studio_count; ++index) {
            entity::StudioEntityRenderInstance instance;
            instance.entity_number = static_cast<std::uint32_t>(index + 1U);
            instance.studio_asset_index = 0U;
            instance.pose_index = 0U;
            instance.body_value = body_value;
            instance.skin_family_index = skin_family_index;
            instance.transform.origin = {
                -1.5F + static_cast<float>(index) * 2.5F, 0.0F, 0.0F};
            instance.transform.rotation_degrees = {90.0F, 0.0F, 0.0F};
            instance.transform.uniform_scale = 1.5F;
            instance.interpolated_bounds = {
                {-4.0F, -1.0F, -2.0F}, {4.0F, 1.0F, 4.0F}};
            input.studio_instances.push_back(instance);
        }
    }
    if (sprite_count != 0U) {
        REQUIRE(package.sprite_assets().size() == 1U);
        for (std::size_t index = 0U; index < sprite_count; ++index) {
            entity::SpriteEntityRenderInstance instance;
            instance.entity_number =
                static_cast<std::uint32_t>(studio_count + index + 1U);
            instance.sprite_asset_index = 0U;
            instance.selected_frame_index = sprite_frame;
            instance.orientation = package.sprite_assets()[0U]->orientation();
            instance.texture_format_support =
                package.sprite_assets()[0U]->texture_support_status();
            instance.transform.origin = {
                -2.0F + static_cast<float>(index) * 4.0F, 0.0F, 1.0F};
            instance.transform.uniform_scale = 0.35F;
            instance.bounds = {{-4.0F, -1.0F, -2.0F}, {4.0F, 1.0F, 4.0F}};
            input.sprite_instances.push_back(instance);
        }
    }
    auto built = entity::EntityRenderFrameBuilder{}.build(
        package, std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const entity::EntityRenderFrame>(
        std::move(*built.frame));
}

[[nodiscard]] inline SceneAndFrame studio_scene(
    const std::uint64_t revision = 1U,
    const float pose_translation_x = 0.0F,
    const std::uint32_t body_value = 0U,
    const std::uint32_t skin_family_index = 0U,
    const bool include_second_skin_family = false)
{
    auto package_result = fixture::scene_package(
        fixture::render_assets(true,
            false,
            assets::SpriteTextureFormat::normal,
            include_second_skin_family));
    REQUIRE(package_result);
    auto package = std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*package_result.package));
    auto frame = make_frame(
        *package,
        revision,
        2U,
        0U,
        pose_translation_x,
        0U,
        body_value,
        skin_family_index);
    return {std::move(package), std::move(frame)};
}

[[nodiscard]] inline SceneAndFrame sprite_scene(
    const std::uint64_t revision = 1U,
    const std::uint32_t selected_frame = 0U,
    const assets::SpriteTextureFormat texture_format =
        assets::SpriteTextureFormat::normal,
    const assets::SpriteOrientation orientation =
        assets::SpriteOrientation::view_parallel)
{
    auto package_result = fixture::scene_package(
        fixture::render_assets(false, true, texture_format, false, orientation));
    REQUIRE(package_result);
    auto package = std::make_shared<const entity::EntitySceneRenderPackage>(
        std::move(*package_result.package));
    auto frame = make_frame(
        *package, revision, 0U, 2U, 0.0F, selected_frame);
    return {std::move(package), std::move(frame)};
}

[[nodiscard]] inline renderer::RenderScene render_scene(
    const SceneAndFrame& entities)
{
    renderer::RenderScene scene;
    scene.clear_color = {0.02F, 0.03F, 0.04F, 1.0F};
    scene.camera.position = {0.0F, -12.0F, 3.0F};
    scene.camera.target = {0.0F, 0.0F, 1.0F};
    scene.camera.up = {0.0F, 0.0F, 1.0F};
    scene.camera.near_plane = 0.1F;
    scene.camera.far_plane = 128.0F;
    const auto& statistics = entities.frame->statistics();
    scene.dynamic_entities.emplace(renderer::RenderDynamicEntities{
        entities.package,
        entities.frame,
        {
            statistics.candidate_count,
            statistics.visible_count,
            statistics.studio_instance_count,
            statistics.sprite_instance_count,
            statistics.unsupported_instance_count,
        },
    });
    return scene;
}

[[nodiscard]] inline std::vector<std::byte> framebuffer()
{
    std::vector<std::byte> pixels(96U * 96U * 4U);
    glFinish();
    glReadPixels(0, 0, 96, 96, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

[[nodiscard]] inline bool has_non_clear_pixel(
    const std::vector<std::byte>& pixels,
    const std::array<std::byte, 4U> clear) noexcept
{
    for (std::size_t offset = 0U; offset + 3U < pixels.size(); offset += 4U) {
        if (pixels[offset] != clear[0U] || pixels[offset + 1U] != clear[1U] ||
            pixels[offset + 2U] != clear[2U] ||
            pixels[offset + 3U] != clear[3U]) {
            return true;
        }
    }
    return false;
}

} // namespace hlclient::tests::entity_opengl_fixture

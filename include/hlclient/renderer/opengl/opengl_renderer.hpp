#pragma once

#include <hlclient/renderer/renderer.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hlclient::renderer::opengl {

enum class OpenGlRendererErrorCode {
    shader_compile_failed,
    program_link_failed,
    invalid_world_package,
    buffer_upload_failed,
    texture_upload_failed,
    lightmap_upload_failed,
    camera_invalid,
    draw_range_invalid,
    gl_operation_failed,
    unable_to_retain_resources,
    invalid_entity_scene,
    entity_capability_unsupported,
    entity_buffer_upload_failed,
    entity_texture_upload_failed,
    entity_draw_invalid,
};

[[nodiscard]] std::string_view to_string(OpenGlRendererErrorCode code) noexcept;

class OpenGlRendererError final : public std::runtime_error {
public:
    OpenGlRendererError(OpenGlRendererErrorCode code, std::string context);

    [[nodiscard]] OpenGlRendererErrorCode code() const noexcept;

private:
    OpenGlRendererErrorCode code_;
};

struct OpenGlWorldRendererStatistics {
    std::uint64_t package_revision{0U};
    std::uint64_t upload_count{0U};
    std::uint64_t scene_upload_count{0U};
    std::uint64_t brush_upload_count{0U};
    std::uint64_t visibility_update_count{0U};
    std::uint64_t rendered_frame_count{0U};
    std::uint64_t draw_call_count{0U};
    std::uint64_t brush_draw_call_count{0U};
    std::uint64_t rendered_command_count{0U};
    std::uint64_t triangle_count{0U};
    std::uint64_t base_texture_bind_count{0U};
    std::uint64_t lightmap_bind_count{0U};
    RenderExtent last_extent{};
    bool world_present{false};
    bool scene_present{false};
    std::uint64_t scene_revision{0U};
    std::uint64_t visibility_revision{0U};
    std::uint64_t visible_world_surface_count{0U};
    std::uint64_t visible_brush_instance_count{0U};

    // Bounded count-only diagnostics used by capability-gated resource tests.
    // They intentionally expose neither OpenGL names nor source pixels.
    std::uint64_t uploaded_base_texture_count{0U};
    std::uint64_t uploaded_base_mip_level_count{0U};
    std::uint64_t uploaded_lightmap_page_count{0U};
    std::uint64_t uploaded_lightmap_layer_count{0U};
    std::uint64_t uploaded_white_lightmap_count{0U};
    std::uint64_t world_resource_release_count{0U};
    std::uint64_t failed_upload_count{0U};
    bool active_world_resources{false};
};

struct OpenGlEntityRendererStatistics {
    std::uint64_t entity_scene_revision{0U};
    std::uint64_t entity_frame_revision{0U};
    std::uint64_t studio_asset_upload_count{0U};
    std::uint64_t sprite_asset_upload_count{0U};
    std::uint64_t entity_frame_count{0U};
    std::uint64_t studio_draw_count{0U};
    std::uint64_t sprite_draw_count{0U};
    std::uint64_t model_texture_bind_count{0U};
    std::uint64_t sprite_texture_bind_count{0U};
    std::uint64_t pose_ubo_update_count{0U};
    std::uint64_t visible_entity_count{0U};
    std::uint64_t entity_resource_release_count{0U};
    bool entity_scene_present{false};
    bool active_entity_resources{false};
};

class OpenGlRenderer final : public IRenderer {
public:
    // An OpenGL 3.3 Core context must be current on the calling thread.
    OpenGlRenderer();
    ~OpenGlRenderer() noexcept override;

    OpenGlRenderer(const OpenGlRenderer&) = delete;
    OpenGlRenderer& operator=(const OpenGlRenderer&) = delete;
    OpenGlRenderer(OpenGlRenderer&&) = delete;
    OpenGlRenderer& operator=(OpenGlRenderer&&) = delete;

    [[nodiscard]] const RendererInfo& information() const noexcept override;
    void render(const RenderScene& scene, RenderExtent extent) override;
    [[nodiscard]] const OpenGlWorldRendererStatistics& statistics() const noexcept;
    [[nodiscard]] const OpenGlEntityRendererStatistics&
    entity_statistics() const noexcept;

private:
    class Implementation;

    RendererInfo information_;
    std::unique_ptr<Implementation> implementation_;
    bool loader_initialized_{false};
};

} // namespace hlclient::renderer::opengl

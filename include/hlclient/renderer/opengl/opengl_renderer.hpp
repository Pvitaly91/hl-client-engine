#pragma once

#include <hlclient/renderer/renderer.hpp>

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::renderer::opengl {

enum class OpenGlGpuPass : std::uint8_t {
    world, studio_viewmodel, effects_decals, msaa_resolve,
    image_adjustments, hud_console
};
inline constexpr std::size_t kOpenGlGpuPassCount = 6U;
// Optional detail of the existing world pass. These intervals do not change
// the meaning or validity bits of OpenGlGpuPass::world.
enum class OpenGlWorldGpuSubpass : std::uint8_t {
    target_setup_clear, sky, static_world, runtime_brushes
};
inline constexpr std::size_t kOpenGlWorldGpuSubpassCount = 4U;
enum class OpenGlGpuTimingStatus : std::uint8_t {
    not_observed, pending, ready, unsupported, disjoint, invalid, capacity_dropped
};
[[nodiscard]] std::string_view to_string(OpenGlGpuTimingStatus status) noexcept;
[[nodiscard]] std::string_view to_string(OpenGlGpuPass pass) noexcept;
[[nodiscard]] std::string_view to_string(OpenGlWorldGpuSubpass pass) noexcept;

struct OpenGlGpuFrameTiming final {
    std::uint64_t frame_number{};
    double elapsed_ms{};
    // Non-overlapping timestamp segments are summed by pass; Studio and
    // effects may alternate in the actual ordered draw stream. No unavailable
    // pass is represented by a valid zero: consumers must check this mask.
    std::array<float, kOpenGlGpuPassCount> pass_elapsed_ms{};
    std::uint8_t valid_pass_mask{};
    // A zero duration is meaningful only when the corresponding bit is set.
    // Unsupported, pending, disjoint, and invalid samples leave this mask 0.
    std::array<float, kOpenGlWorldGpuSubpassCount> world_subpass_elapsed_ms{};
    std::uint8_t valid_world_subpass_mask{};
    OpenGlGpuTimingStatus status{OpenGlGpuTimingStatus::not_observed};
};
struct OpenGlGpuTimingStatistics final {
    std::uint64_t submitted{},completed{},capacity_drops{},unavailable{};
    std::size_t pending{},allocated_queries{};
    std::size_t allocated_pass_queries{};
    std::uint64_t disjoint_results{},invalid_results{};
    OpenGlGpuTimingStatus last_submission_status{OpenGlGpuTimingStatus::not_observed};
    bool pass_timestamps_supported{},disjoint_detection_supported{};
};

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
    std::uint64_t runtime_brush_submitted_count{0U};
    std::uint64_t runtime_brush_culled_count{0U};
    // Cumulative static-world block decisions. A full-batch fallback is one
    // material batch, not a lost frame or a missing surface.
    std::uint64_t prepared_world_selected_block_count{0U};
    std::uint64_t prepared_world_rejected_block_count{0U};
    std::uint64_t prepared_world_full_batch_fallback_count{0U};
    std::uint64_t prepared_world_probed_block_count{0U};
    std::uint64_t prepared_world_all_batch_bypass_frame_count{0U};
    std::uint64_t rendered_command_count{0U};
    std::uint64_t triangle_count{0U};
    std::uint64_t base_texture_bind_count{0U};
    std::uint64_t lightmap_bind_count{0U};
    std::uint64_t program_bind_count{0U};
    std::uint64_t vertex_array_bind_count{0U};
    std::uint64_t sky_texture_bind_count{0U};
    std::uint64_t material_uniform_upload_count{0U};
    std::uint64_t transform_upload_count{0U};
    std::uint64_t camera_upload_count{0U};
    std::uint64_t light_upload_count{0U};
    // World transient quads only: actual GPU submissions, not visible pixels.
    std::uint64_t transient_flash_upload_count{0U};
    std::uint64_t transient_flash_draw_count{0U};
    std::uint64_t transient_flash_quad_count{0U};
    std::uint64_t transient_flash_storage_orphan_count{0U};
    std::size_t transient_flash_storage_bytes_high_water{0U};
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
    std::uint64_t studio_pass_uniform_upload_count{0U};
    std::uint64_t visible_entity_count{0U};
    std::uint64_t entity_resource_release_count{0U};
    bool entity_scene_present{false};
    bool active_entity_resources{false};
};

// Count-only, bounded diagnostics. Submitted is not proof of visible pixels.
struct OpenGlHudRendererStatistics {
    std::uint64_t texture_upload_count{}, sprite_draw_count{};
    std::size_t cached_texture_count{}, cached_texture_bytes{};
    std::size_t last_sprite_command_count{}, last_rendered_sprite_count{};
    std::size_t last_invalid_commands{}, last_missing_binding{}, last_revision_mismatch{};
    std::size_t last_invalid_frame{}, last_invalid_crop{}, last_unsupported_material{};
    std::size_t last_resource_budget{}, last_empty_clip{};
};

// Count-only bounded evidence. A submitted/drawn batch does not establish
// visible pixels; resource caches retain owning immutable values, not names.
struct OpenGlDecalRendererStatistics {
    std::uint64_t texture_upload_count{}, geometry_upload_count{};
    std::uint64_t submitted_batch_count{}, rendered_batch_count{}, rejected_batch_count{};
    std::uint64_t missing_resource{}, invalid_texture{}, invalid_geometry{},
        invalid_transform{}, unsupported_material{}, batch_limit{}, resource_budget{},world_unavailable{};
    std::size_t cached_texture_count{}, cached_geometry_count{};
    std::size_t last_submitted{}, last_rendered{}, last_rejected{};
};

struct OpenGlImageAdjustmentStatistics {
    std::uint64_t adjusted_frames{}, neutral_frames{}, storage_reallocations{};
    std::uint64_t framebuffer_color_copies{}, resolved_texture_reads{};
    std::size_t active_texture_count{};
    RenderExtent last_extent{};
};

enum class OpenGlMsaaFallback : std::uint8_t {
    none, disabled, unsupported_count, memory_budget, allocation_failed,
    framebuffer_incomplete, attachment_samples_mismatch
};
[[nodiscard]] inline constexpr std::string_view to_string(OpenGlMsaaFallback value) noexcept {
    switch(value) {
    case OpenGlMsaaFallback::none: return "none";
    case OpenGlMsaaFallback::disabled: return "disabled";
    case OpenGlMsaaFallback::unsupported_count: return "unsupported_count";
    case OpenGlMsaaFallback::memory_budget: return "memory_budget";
    case OpenGlMsaaFallback::allocation_failed: return "allocation_failed";
    case OpenGlMsaaFallback::framebuffer_incomplete: return "framebuffer_incomplete";
    case OpenGlMsaaFallback::attachment_samples_mismatch: return "attachment_samples_mismatch";
    }
    return "unavailable";
}
struct OpenGlSceneTargetStatistics {
    unsigned requested_samples{}, actual_samples{}, maximum_samples{};
    bool framebuffer_complete{};
    RenderExtent scene_extent{}, resolve_extent{}, hud_extent{};
    std::array<int,4> viewport{};
    OpenGlMsaaFallback fallback_reason{OpenGlMsaaFallback::disabled};
    std::uint64_t storage_allocations{}, storage_releases{}, resolved_frames{};
    std::uint64_t output_color_blits{}, direct_adjustment_frames{};
    // Null until the existing one-time default-back-buffer capability check
    // is needed. This is a cached observation, not an extra GL query.
    std::optional<bool> cached_default_back_output_compatible{};
    std::size_t active_framebuffers{}, active_attachments{};
};

enum class OpenGlFramebufferObservationStatus : std::uint8_t {
    valid,
    default_back_buffer_unavailable,
};

[[nodiscard]] std::string_view to_string(
    OpenGlFramebufferObservationStatus status) noexcept;

struct OpenGlFramebufferObservation {
    OpenGlFramebufferObservationStatus status{
        OpenGlFramebufferObservationStatus::default_back_buffer_unavailable};
    std::uint32_t read_framebuffer{0U};
    std::uint32_t read_buffer{0U};
    std::uint64_t sampled_pixel_count{0U};
    std::uint64_t non_clear_pixel_count{0U};
    std::uint64_t color_signature{0U};
    int minimum_x{0};
    int minimum_y{0};
    int maximum_x{0};
    int maximum_y{0};
    bool has_non_clear_bounds{false};
    // Opt-in bounded readback, bottom row first. No paths or filesystem I/O.
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == OpenGlFramebufferObservationStatus::valid;
    }
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
    // Between connection attempts, on the current context's owner thread.
    // Release per-session resources/statistics without destroying the window,
    // context or the persistent screen UI and its owned background texture.
    void reset_session_resources();
    void render(const RenderScene& scene, RenderExtent extent) override;
    // Optional nonblocking GPU observation. Ordinary render() allocates no
    // queries. Results retain the original frame identity, not the poll frame.
    void render_with_gpu_timing(const RenderScene& scene, RenderExtent extent,
        std::uint64_t frame_number);
    [[nodiscard]] std::optional<OpenGlGpuFrameTiming> poll_gpu_timing();
    [[nodiscard]] const OpenGlGpuTimingStatistics& gpu_timing_statistics() const noexcept;
    [[nodiscard]] const OpenGlWorldRendererStatistics& statistics() const noexcept;
    [[nodiscard]] const OpenGlEntityRendererStatistics&
    entity_statistics() const noexcept;
    [[nodiscard]] const OpenGlHudRendererStatistics& hud_statistics() const noexcept;
    [[nodiscard]] const OpenGlDecalRendererStatistics& decal_statistics() const noexcept;
    [[nodiscard]] const OpenGlImageAdjustmentStatistics& image_adjustment_statistics() const noexcept;
    [[nodiscard]] const OpenGlSceneTargetStatistics& scene_target_statistics() const noexcept;
    // Reads the renderer-owned default back framebuffer after render(). The
    // observer selects GL_BACK explicitly and restores the caller's read-FBO,
    // read-buffer, pixel-pack-buffer and pack-layout state before returning.
    [[nodiscard]] OpenGlFramebufferObservation observe_framebuffer(
        RenderExtent extent,
        ClearColor expected_clear_color, bool retain_pixels = false) const;

private:
    class Implementation;

    RendererInfo information_;
    std::unique_ptr<Implementation> implementation_;
    bool loader_initialized_{false};
};

} // namespace hlclient::renderer::opengl

#pragma once

#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace hlclient::world_preview {

enum class WorldPreviewCameraMode {
    static_camera,
    orbit,
    spawn,
    free_flight,
    entity_first_person,
    player_walk,
};

enum class WorldPreviewBrushSubmodelsMode {
    off,
    static_instances,
};

struct WorldPreviewSpawnCameraDescriptor {
    assets::AssetVector3 position{};
    assets::AssetVector3 forward{1.0F, 0.0F, 0.0F};
    assets::AssetVector3 up{0.0F, 0.0F, 1.0F};
};

struct WorldPreviewSceneOptions {
    WorldPreviewCameraMode camera_mode{WorldPreviewCameraMode::static_camera};
    client::PreviewWorldCullMode cull_mode{client::PreviewWorldCullMode::none};
    assets::AssetVector3 isometric_direction{1.0F, -1.0F, 0.75F};
    float minimum_radius{16.0F};
    float camera_distance_factor{3.0F};
    float orbit_angular_velocity_radians_per_second{0.125F};
    float vertical_field_of_view_radians{1.0471975512F};
    world_visibility::WorldVisibilityMode visibility_mode{
        world_visibility::WorldVisibilityMode::all};
    world_visibility::WorldPvsFallbackPolicy pvs_fallback_policy{
        world_visibility::WorldPvsFallbackPolicy::frustum_only};
    WorldPreviewBrushSubmodelsMode brush_submodels{
        WorldPreviewBrushSubmodelsMode::off};
    renderer::RenderExtent visibility_extent{1'280, 720};
    std::optional<WorldPreviewSpawnCameraDescriptor> spawn_camera;
};

// Diagnostic bounds-derived source only. It never reads entity/spawn state,
// accepts gameplay input, creates user commands, or mutates network state.
class WorldPreviewSceneSource final
    : public client::IClientSceneSource,
      public client::IInteractiveCameraPublicationTarget {
public:
    explicit WorldPreviewSceneSource(
        std::shared_ptr<const world_render::WorldRenderPackage> package,
        WorldPreviewSceneOptions options = {});
    explicit WorldPreviewSceneSource(
        std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
            package,
        WorldPreviewSceneOptions options = {});

    [[nodiscard]] client::SceneUpdateResult set_render_extent(
        renderer::RenderExtent extent) override;
    [[nodiscard]] client::SceneUpdateResult update(client::FrameTime elapsed) override;
    [[nodiscard]] const client::ClientWorldState& world_state() const noexcept override;
    void publish_camera_seed(
        const client::RenderCameraState& camera) noexcept override;
    [[nodiscard]] bool publish_interactive_camera(
        const client::RenderCameraState& camera,
        const client::InteractiveCameraMetadata& metadata) noexcept override;
    [[nodiscard]] bool publish_dynamic_entities(
        std::shared_ptr<const entity_render::EntitySceneRenderPackage> package,
        std::shared_ptr<const entity_render::EntityRenderFrame> frame) noexcept;
    [[nodiscard]] assets::AssetVector3 world_center() const noexcept;
    [[nodiscard]] float world_radius() const noexcept;
    [[nodiscard]] const WorldPreviewSceneOptions& options() const noexcept;
    [[nodiscard]] bool spawn_camera_applied() const noexcept;
    [[nodiscard]] std::uint32_t fallback_warning_count() const noexcept;

private:
    void initialize_bounds(const assets::WorldBounds& bounds);
    void validate_options(bool scene_package_available) const;
    void build_scene_adapters();
    [[nodiscard]] bool update_camera() noexcept;
    [[nodiscard]] client::SceneUpdateResult update_visibility();

    client::ClientWorldState world_state_;
    WorldPreviewSceneOptions options_{};
    assets::AssetVector3 world_center_{};
    assets::AssetVector3 base_direction_{};
    float world_radius_{0.0F};
    bool spawn_camera_applied_{false};
    std::uint32_t reported_fallback_reason_mask_{0U};
    std::uint32_t fallback_warning_count_{0U};
    std::uint64_t next_visibility_revision_{1U};
    std::optional<client::RenderCameraState> last_visibility_camera_;
    std::optional<renderer::RenderExtent> last_visibility_extent_;
    std::shared_ptr<const world_visibility::WorldVisibilitySet>
        last_visibility_state_;
    std::shared_ptr<const world_visibility::WorldVisibleDrawList>
        last_visible_draw_list_;
    std::vector<world_visibility::WorldVisibilityBrushInstanceInput>
        visibility_brush_instances_;
    std::vector<world_visibility::WorldVisibleBrushModelInput>
        visible_brush_models_;
    std::vector<world_visibility::WorldVisibleBrushInstanceDrawInput>
        visible_brush_instances_;
};

} // namespace hlclient::world_preview

#pragma once

#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/world_render/world_render_types.hpp>

#include <memory>

namespace hlclient::world_preview {

enum class WorldPreviewCameraMode {
    static_camera,
    orbit,
};

struct WorldPreviewSceneOptions {
    WorldPreviewCameraMode camera_mode{WorldPreviewCameraMode::static_camera};
    client::PreviewWorldCullMode cull_mode{client::PreviewWorldCullMode::none};
    assets::AssetVector3 isometric_direction{1.0F, -1.0F, 0.75F};
    float minimum_radius{16.0F};
    float camera_distance_factor{3.0F};
    float orbit_angular_velocity_radians_per_second{0.125F};
    float vertical_field_of_view_radians{1.0471975512F};
};

// Diagnostic bounds-derived source only. It never reads entity/spawn state,
// accepts gameplay input, creates user commands, or mutates network state.
class WorldPreviewSceneSource final : public client::IClientSceneSource {
public:
    explicit WorldPreviewSceneSource(
        std::shared_ptr<const world_render::WorldRenderPackage> package,
        WorldPreviewSceneOptions options = {});

    [[nodiscard]] client::SceneUpdateResult update(client::FrameTime elapsed) override;
    [[nodiscard]] const client::ClientWorldState& world_state() const noexcept override;
    [[nodiscard]] assets::AssetVector3 world_center() const noexcept;
    [[nodiscard]] float world_radius() const noexcept;
    [[nodiscard]] const WorldPreviewSceneOptions& options() const noexcept;

private:
    [[nodiscard]] bool update_camera() noexcept;

    client::ClientWorldState world_state_;
    WorldPreviewSceneOptions options_{};
    assets::AssetVector3 world_center_{};
    assets::AssetVector3 base_direction_{};
    float world_radius_{0.0F};
};

} // namespace hlclient::world_preview

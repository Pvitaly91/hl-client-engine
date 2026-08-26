#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

namespace hlclient::world_render {
class WorldRenderPackage;
}

namespace hlclient::world_scene_render {
class WorldSceneRenderPackage;
}

namespace hlclient::world_visibility {
class WorldVisibilitySet;
class WorldVisibleDrawList;
}

namespace hlclient::entity_render {
class EntitySceneRenderPackage;
class EntityRenderFrame;
}

namespace hlclient::client {

struct RenderCameraState {
    assets::AssetVector3 position{0.0F, -1.0F, 0.0F};
    assets::AssetVector3 target{0.0F, 0.0F, 0.0F};
    assets::AssetVector3 up{0.0F, 0.0F, 1.0F};
    float vertical_field_of_view_radians{1.0471975512F};
    float near_plane{0.1F};
    float far_plane{4'096.0F};
};

enum class PreviewWorldCullMode {
    none,
    back,
};

struct PreviewRenderOptions {
    PreviewWorldCullMode cull_mode{PreviewWorldCullMode::none};
};

class ClientWorldState final {
public:
    void reset() noexcept;
    void advance(std::chrono::duration<double> elapsed) noexcept;
    void set_connection_requested(bool requested) noexcept;
    void set_static_world(
        std::shared_ptr<const world_render::WorldRenderPackage> package) noexcept;
    void set_world_scene(
        std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
            package) noexcept;
    void clear_static_world() noexcept;
    [[nodiscard]] bool set_world_visibility(
        std::shared_ptr<const world_visibility::WorldVisibilitySet> visibility,
        std::shared_ptr<const world_visibility::WorldVisibleDrawList>
            draw_list) noexcept;
    void clear_world_visibility() noexcept;
    [[nodiscard]] bool set_dynamic_entities(
        std::shared_ptr<const entity_render::EntitySceneRenderPackage> package,
        std::shared_ptr<const entity_render::EntityRenderFrame> frame) noexcept;
    void clear_dynamic_entities() noexcept;
    void set_camera(const RenderCameraState& camera) noexcept;
    [[nodiscard]] bool set_preview_render_options(
        const PreviewRenderOptions& options) noexcept;

    [[nodiscard]] double elapsed_seconds() const noexcept;
    [[nodiscard]] bool connection_requested() const noexcept;
    [[nodiscard]] const std::shared_ptr<const world_render::WorldRenderPackage>&
    static_world() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const world_scene_render::WorldSceneRenderPackage>&
    world_scene() const noexcept;
    [[nodiscard]] const std::shared_ptr<const world_visibility::WorldVisibilitySet>&
    world_visibility() const noexcept;
    [[nodiscard]] const std::shared_ptr<const world_visibility::WorldVisibleDrawList>&
    visible_draw_list() const noexcept;
    [[nodiscard]] const RenderCameraState& camera() const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const entity_render::EntitySceneRenderPackage>&
    entity_scene() const noexcept;
    [[nodiscard]] const std::shared_ptr<const entity_render::EntityRenderFrame>&
    entity_frame() const noexcept;
    [[nodiscard]] std::uint64_t world_revision() const noexcept;
    [[nodiscard]] std::uint64_t scene_revision() const noexcept;
    [[nodiscard]] std::uint64_t visibility_revision() const noexcept;
    [[nodiscard]] std::uint64_t entity_scene_revision() const noexcept;
    [[nodiscard]] std::uint64_t entity_frame_revision() const noexcept;
    [[nodiscard]] const PreviewRenderOptions& preview_render_options() const noexcept;

private:
    double elapsed_seconds_{0.0};
    bool connection_requested_{false};
    std::shared_ptr<const world_render::WorldRenderPackage> static_world_;
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> world_scene_;
    std::shared_ptr<const world_visibility::WorldVisibilitySet> world_visibility_;
    std::shared_ptr<const world_visibility::WorldVisibleDrawList> visible_draw_list_;
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> entity_scene_;
    std::shared_ptr<const entity_render::EntityRenderFrame> entity_frame_;
    RenderCameraState camera_{};
    std::uint64_t world_revision_{0U};
    std::uint64_t scene_revision_{0U};
    std::uint64_t visibility_revision_{0U};
    std::uint64_t entity_scene_revision_{0U};
    std::uint64_t entity_frame_revision_{0U};
    PreviewRenderOptions preview_render_options_{};
};

} // namespace hlclient::client

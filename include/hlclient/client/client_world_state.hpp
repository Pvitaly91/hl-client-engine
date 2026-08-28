#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

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

    [[nodiscard]] friend bool operator==(
        const RenderCameraState& left,
        const RenderCameraState& right) noexcept
    {
        return left.position.x == right.position.x &&
            left.position.y == right.position.y &&
            left.position.z == right.position.z &&
            left.target.x == right.target.x &&
            left.target.y == right.target.y &&
            left.target.z == right.target.z &&
            left.up.x == right.up.x && left.up.y == right.up.y &&
            left.up.z == right.up.z &&
            left.vertical_field_of_view_radians ==
                right.vertical_field_of_view_radians &&
            left.near_plane == right.near_plane &&
            left.far_plane == right.far_plane;
    }
};

enum class PreviewWorldCullMode {
    none,
    back,
};

struct PreviewRenderOptions {
    PreviewWorldCullMode cull_mode{PreviewWorldCullMode::none};
};

enum class InteractiveCameraMode {
    free_flight_world,
    entity_first_person,
    player_walk,
};

enum class ControlledEntityCameraStatus {
    not_applicable,
    anchored,
    anchor_missing,
};

// Compact client-owned metadata only. Input snapshots and gameplay intents
// remain outside ClientWorldState and never cross the renderer boundary.
struct InteractiveCameraMetadata {
    std::uint64_t input_revision{0U};
    std::uint64_t camera_revision{0U};
    InteractiveCameraMode mode{InteractiveCameraMode::free_flight_world};
    std::optional<std::uint32_t> controlled_entity;
    ControlledEntityCameraStatus controlled_entity_status{
        ControlledEntityCameraStatus::not_applicable};
};

// Narrow renderer-neutral publication boundary used by the interactive camera
// controller. Implementations may preserve additional scene-source invariants
// without exposing mutable world/resource state to the controller.
class IInteractiveCameraPublicationTarget {
public:
    virtual ~IInteractiveCameraPublicationTarget() = default;

    virtual void publish_camera_seed(
        const RenderCameraState& camera) noexcept = 0;
    [[nodiscard]] virtual bool publish_interactive_camera(
        const RenderCameraState& camera,
        const InteractiveCameraMetadata& metadata) noexcept = 0;
};

class ClientWorldState final : public IInteractiveCameraPublicationTarget {
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
    [[nodiscard]] bool set_interactive_camera(
        const RenderCameraState& camera,
        const InteractiveCameraMetadata& metadata) noexcept;
    void publish_camera_seed(
        const RenderCameraState& camera) noexcept override
    {
        set_camera(camera);
    }
    [[nodiscard]] bool publish_interactive_camera(
        const RenderCameraState& camera,
        const InteractiveCameraMetadata& metadata) noexcept override
    {
        return set_interactive_camera(camera, metadata);
    }
    void clear_interactive_camera_metadata() noexcept;
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
    [[nodiscard]] const std::optional<InteractiveCameraMetadata>&
    interactive_camera_metadata() const noexcept;
    [[nodiscard]] std::uint64_t input_revision() const noexcept;
    [[nodiscard]] std::uint64_t camera_revision() const noexcept;
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
    std::optional<InteractiveCameraMetadata> interactive_camera_metadata_;
    std::uint64_t world_revision_{0U};
    std::uint64_t scene_revision_{0U};
    std::uint64_t visibility_revision_{0U};
    std::uint64_t entity_scene_revision_{0U};
    std::uint64_t entity_frame_revision_{0U};
    PreviewRenderOptions preview_render_options_{};
};

} // namespace hlclient::client

#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/gameplay_camera/entity_first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_snapshot.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace hlclient::entity_render {
class EntityRenderFrame;
}

namespace hlclient::interactive_preview {

enum class InteractivePreviewMode {
    free_flight_world,
    entity_first_person,
};

enum class InteractivePreviewErrorCode {
    invalid_configuration,
    non_monotonic_input_sequence,
    intent_build_failed,
    anchor_resolution_failed,
    camera_update_failed,
    camera_conversion_failed,
    client_world_publication_rejected,
    unable_to_retain_state,
};

[[nodiscard]] std::string_view to_string(
    InteractivePreviewErrorCode code) noexcept;

struct InteractivePreviewError {
    InteractivePreviewErrorCode code{
        InteractivePreviewErrorCode::invalid_configuration};
    std::string_view context;
};

struct InteractivePreviewStatistics {
    std::uint64_t update_attempt_count{0U};
    std::uint64_t published_update_count{0U};
    std::uint64_t changed_camera_count{0U};
    std::uint64_t unchanged_camera_count{0U};
    std::uint64_t duration_clamp_count{0U};
    std::uint64_t anchor_missing_count{0U};
    std::uint64_t focus_reset_count{0U};
    std::uint64_t capture_request_count{0U};
    std::uint64_t release_request_count{0U};
};

enum class InteractivePreviewUpdateStatus {
    unchanged,
    camera_updated,
    duration_clamped,
    anchor_missing_camera_frozen,
};

struct InteractivePreviewUpdateResult {
    InteractivePreviewUpdateStatus status{
        InteractivePreviewUpdateStatus::unchanged};
    std::optional<InteractivePreviewError> error;
    std::uint64_t input_sequence{0U};
    std::uint64_t camera_revision{0U};
    bool camera_revision_changed{false};
    bool capture_mouse_requested{false};
    bool release_mouse_requested{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class InteractivePreviewController final {
public:
    struct CreationResult;

    InteractivePreviewController(const InteractivePreviewController&) = delete;
    InteractivePreviewController& operator=(
        const InteractivePreviewController&) = delete;
    InteractivePreviewController(InteractivePreviewController&&) noexcept =
        default;
    InteractivePreviewController& operator=(InteractivePreviewController&&) =
        delete;
    ~InteractivePreviewController() = default;

    [[nodiscard]] static CreationResult create(
        InteractivePreviewMode mode,
        gameplay_input::GameplayInputBindings bindings,
        gameplay_camera::FirstPersonCameraConfig camera_config,
        gameplay_camera::GameplayCameraState initial_camera,
        std::optional<std::uint32_t> controlled_entity = std::nullopt,
        assets::AssetVector3 explicit_eye_offset =
            assets::AssetVector3{0.0F, 0.0F, 28.0F}) noexcept;

    // Convenience boundary used by the offline viewers. It derives an exact
    // Z-up yaw/pitch seed and bounded camera planes from their already-valid
    // renderer-neutral preview camera, then installs project-owned defaults.
    [[nodiscard]] static CreationResult create_project_default_v1(
        InteractivePreviewMode mode,
        const client::RenderCameraState& initial_render_camera,
        std::optional<std::uint32_t> controlled_entity = std::nullopt,
        assets::AssetVector3 explicit_eye_offset =
            assets::AssetVector3{0.0F, 0.0F, 28.0F}) noexcept;

    [[nodiscard]] InteractivePreviewUpdateResult update(
        const input::InputSnapshot& snapshot,
        double elapsed_duration_seconds,
        client::IInteractiveCameraPublicationTarget& publication_target,
        const entity_render::EntityRenderFrame* entity_frame = nullptr) noexcept;

    // Canonicalizes the preview source's diagnostic camera to the exact
    // renderer payload represented by this controller before frame sampling.
    // This preserves zero-input camera/visibility stability on frame one.
    [[nodiscard]] bool seed_world_state_camera(
        client::IInteractiveCameraPublicationTarget& publication_target)
        const noexcept;

    [[nodiscard]] InteractivePreviewMode mode() const noexcept;
    [[nodiscard]] std::shared_ptr<const gameplay_camera::GameplayCameraState>
    camera() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> controlled_entity()
        const noexcept;
    [[nodiscard]] const assets::AssetVector3& explicit_eye_offset()
        const noexcept;
    [[nodiscard]] std::uint64_t last_input_sequence() const noexcept;
    [[nodiscard]] const InteractivePreviewStatistics& statistics()
        const noexcept;

private:
    InteractivePreviewController(
        InteractivePreviewMode mode,
        std::shared_ptr<const gameplay_input::GameplayInputBindings> bindings,
        std::shared_ptr<const gameplay_camera::FirstPersonCameraConfig>
            camera_config,
        std::shared_ptr<const gameplay_camera::GameplayCameraState> camera,
        std::optional<std::uint32_t> controlled_entity,
        assets::AssetVector3 explicit_eye_offset) noexcept;

    InteractivePreviewMode mode_{InteractivePreviewMode::free_flight_world};
    std::shared_ptr<const gameplay_input::GameplayInputBindings> bindings_;
    std::shared_ptr<const gameplay_camera::FirstPersonCameraConfig>
        camera_config_;
    std::shared_ptr<const gameplay_camera::GameplayCameraState> camera_;
    std::optional<std::uint32_t> controlled_entity_;
    assets::AssetVector3 explicit_eye_offset_{0.0F, 0.0F, 28.0F};
    std::uint64_t last_input_sequence_{0U};
    InteractivePreviewStatistics statistics_{};
};

struct InteractivePreviewController::CreationResult {
    std::optional<InteractivePreviewController> controller;
    std::optional<InteractivePreviewError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return controller.has_value();
    }
};

} // namespace hlclient::interactive_preview

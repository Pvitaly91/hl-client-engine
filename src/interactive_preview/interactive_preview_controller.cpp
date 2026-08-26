#include <hlclient/interactive_preview/interactive_preview_controller.hpp>

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/gameplay_input/gameplay_input_limits.hpp>
#include <hlclient/renderer/render_camera_math.hpp>

#include <cmath>
#include <new>
#include <utility>

namespace hlclient::interactive_preview {
namespace {

[[nodiscard]] constexpr double gameplay_fov_from_renderer(
    const float renderer_fov_radians) noexcept
{
    constexpr auto exact_minimum = gameplay_input::
        kGameplayInputSafetyHardLimits.minimum_vertical_fov_radians;
    constexpr auto renderer_minimum = static_cast<float>(exact_minimum);
    return renderer_fov_radians == renderer_minimum
        ? exact_minimum
        : static_cast<double>(renderer_fov_radians);
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool camera_matches_config(
    const gameplay_camera::GameplayCameraState& camera,
    const gameplay_camera::FirstPersonCameraConfig& config) noexcept
{
    const auto& position = camera.position();
    const auto magnitude_squared =
        static_cast<double>(position.x) * static_cast<double>(position.x) +
        static_cast<double>(position.y) * static_cast<double>(position.y) +
        static_cast<double>(position.z) * static_cast<double>(position.z);
    const auto position_limit = config.maximum_position_magnitude();
    return std::isfinite(magnitude_squared) &&
        magnitude_squared <= position_limit * position_limit &&
        camera.pitch_degrees() >= config.minimum_pitch_degrees() &&
        camera.pitch_degrees() <= config.maximum_pitch_degrees() &&
        camera.vertical_fov_radians() == config.vertical_fov_radians() &&
        camera.near_plane() == config.near_plane() &&
        camera.far_plane() == config.far_plane() &&
        camera.revision() <= config.maximum_camera_revisions();
}

[[nodiscard]] InteractivePreviewUpdateResult fail(
    const InteractivePreviewErrorCode code,
    const std::string_view context,
    const std::uint64_t input_sequence,
    const std::uint64_t camera_revision) noexcept
{
    return {
        InteractivePreviewUpdateStatus::unchanged,
        InteractivePreviewError{code, context},
        input_sequence,
        camera_revision,
        false,
        false,
        false,
    };
}

struct AnchorResolution {
    std::optional<gameplay_camera::EntityFirstPersonCameraAnchor> anchor;
};

[[nodiscard]] std::optional<AnchorResolution> resolve_anchor(
    const entity_render::EntityRenderFrame& frame,
    const std::uint32_t entity_number,
    const assets::AssetVector3& eye_offset) noexcept
{
    std::optional<assets::AssetVector3> origin;
    for (const auto& instance : frame.studio_instances()) {
        if (instance.entity_number == entity_number) {
            if (origin) {
                return std::nullopt;
            }
            origin = instance.transform.origin;
        }
    }
    for (const auto& instance : frame.sprite_instances()) {
        if (instance.entity_number == entity_number) {
            if (origin) {
                return std::nullopt;
            }
            origin = instance.transform.origin;
        }
    }
    if (!origin) {
        return AnchorResolution{std::nullopt};
    }
    auto created = gameplay_camera::EntityFirstPersonCameraAnchor::create({
        entity_number,
        *origin,
        eye_offset,
        std::nullopt,
        std::nullopt,
        gameplay_camera::GameplayCameraSourceFrameIdentity{
            frame.resource_id(),
            frame.resource_revision(),
            frame.frame_signature(),
        },
        gameplay_camera::GameplayCameraAnchorEvidenceProfile::
            explicit_synthetic_playback_v1,
    });
    if (!created || !created.anchor) {
        return std::nullopt;
    }
    return AnchorResolution{std::move(created.anchor)};
}

} // namespace

std::string_view to_string(const InteractivePreviewErrorCode code) noexcept
{
    switch (code) {
    case InteractivePreviewErrorCode::invalid_configuration:
        return "invalid_configuration";
    case InteractivePreviewErrorCode::non_monotonic_input_sequence:
        return "non_monotonic_input_sequence";
    case InteractivePreviewErrorCode::intent_build_failed:
        return "intent_build_failed";
    case InteractivePreviewErrorCode::anchor_resolution_failed:
        return "anchor_resolution_failed";
    case InteractivePreviewErrorCode::camera_update_failed:
        return "camera_update_failed";
    case InteractivePreviewErrorCode::camera_conversion_failed:
        return "camera_conversion_failed";
    case InteractivePreviewErrorCode::client_world_publication_rejected:
        return "client_world_publication_rejected";
    case InteractivePreviewErrorCode::unable_to_retain_state:
        return "unable_to_retain_state";
    }
    return "unknown";
}

InteractivePreviewController::InteractivePreviewController(
    const InteractivePreviewMode mode,
    std::shared_ptr<const gameplay_input::GameplayInputBindings> bindings,
    std::shared_ptr<const gameplay_camera::FirstPersonCameraConfig> camera_config,
    std::shared_ptr<const gameplay_camera::GameplayCameraState> camera,
    const std::optional<std::uint32_t> controlled_entity,
    const assets::AssetVector3 explicit_eye_offset) noexcept
    : mode_{mode},
      bindings_{std::move(bindings)},
      camera_config_{std::move(camera_config)},
      camera_{std::move(camera)},
      controlled_entity_{controlled_entity},
      explicit_eye_offset_{explicit_eye_offset}
{
}

InteractivePreviewController::CreationResult
InteractivePreviewController::create(
    const InteractivePreviewMode mode,
    gameplay_input::GameplayInputBindings bindings,
    gameplay_camera::FirstPersonCameraConfig camera_config,
    gameplay_camera::GameplayCameraState initial_camera,
    const std::optional<std::uint32_t> controlled_entity,
    const assets::AssetVector3 explicit_eye_offset) noexcept
{
    const bool free_flight = mode == InteractivePreviewMode::free_flight_world;
    const bool entity_camera =
        mode == InteractivePreviewMode::entity_first_person;
    const bool mode_matches =
        (free_flight && initial_camera.mode() ==
                gameplay_camera::GameplayCameraMode::free_flight) ||
        (entity_camera && initial_camera.mode() ==
                gameplay_camera::GameplayCameraMode::entity_first_person);
    const bool entity_anchor_matches = !entity_camera ||
        (controlled_entity &&
            initial_camera.anchor_metadata().entity_number ==
                controlled_entity);
    if ((!free_flight && !entity_camera) || !mode_matches ||
        !entity_anchor_matches ||
        !camera_matches_config(initial_camera, camera_config) ||
        (free_flight && controlled_entity) ||
        (entity_camera && (!controlled_entity || *controlled_entity == 0U)) ||
        !finite_vector(explicit_eye_offset) ||
        initial_camera.compatibility_profile() !=
            gameplay_camera::GameplayCameraCompatibilityProfile::
                local_first_person_z_up_v1) {
        return {
            std::nullopt,
            InteractivePreviewError{
                InteractivePreviewErrorCode::invalid_configuration,
                "Interactive preview configuration is invalid"},
        };
    }
    try {
        auto retained_bindings =
            std::make_shared<const gameplay_input::GameplayInputBindings>(
                std::move(bindings));
        auto retained_config =
            std::make_shared<const gameplay_camera::FirstPersonCameraConfig>(
                std::move(camera_config));
        auto retained_camera =
            std::make_shared<const gameplay_camera::GameplayCameraState>(
                std::move(initial_camera));
        InteractivePreviewController controller{
            mode,
            std::move(retained_bindings),
            std::move(retained_config),
            std::move(retained_camera),
            controlled_entity,
            explicit_eye_offset};
        return {
            std::optional<InteractivePreviewController>{std::move(controller)},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return {
            std::nullopt,
            InteractivePreviewError{
                InteractivePreviewErrorCode::unable_to_retain_state,
                "Interactive preview allocation is unavailable"},
        };
    }
}

InteractivePreviewController::CreationResult
InteractivePreviewController::create_project_default_v1(
    const InteractivePreviewMode mode,
    const client::RenderCameraState& initial_render_camera,
    const std::optional<std::uint32_t> controlled_entity,
    const assets::AssetVector3 explicit_eye_offset) noexcept
{
    const renderer::RenderCamera validated_render_camera{
        initial_render_camera.position,
        initial_render_camera.target,
        initial_render_camera.up,
        initial_render_camera.vertical_field_of_view_radians,
        initial_render_camera.near_plane,
        initial_render_camera.far_plane,
    };
    const auto forward_x = static_cast<double>(
        initial_render_camera.target.x - initial_render_camera.position.x);
    const auto forward_y = static_cast<double>(
        initial_render_camera.target.y - initial_render_camera.position.y);
    const auto forward_z = static_cast<double>(
        initial_render_camera.target.z - initial_render_camera.position.z);
    const auto horizontal = std::hypot(forward_x, forward_y);
    const auto length = std::hypot(horizontal, forward_z);
    if (!renderer::is_valid(validated_render_camera) ||
        !finite_vector(initial_render_camera.position) ||
        !finite_vector(initial_render_camera.target) ||
        !finite_vector(initial_render_camera.up) ||
        !std::isfinite(initial_render_camera.vertical_field_of_view_radians) ||
        !std::isfinite(initial_render_camera.near_plane) ||
        !std::isfinite(initial_render_camera.far_plane) || length <= 0.0) {
        return {
            std::nullopt,
            InteractivePreviewError{
                InteractivePreviewErrorCode::invalid_configuration,
                "Initial preview render camera is invalid"},
        };
    }

    constexpr double radians_to_degrees =
        57.295779513082320876798154814105;
    gameplay_camera::GameplayCameraStateCreateInfo camera_info;
    camera_info.position = initial_render_camera.position;
    camera_info.yaw_degrees =
        std::atan2(forward_y, forward_x) * radians_to_degrees;
    camera_info.pitch_degrees =
        std::atan2(forward_z, horizontal) * radians_to_degrees;
    camera_info.vertical_fov_radians = gameplay_fov_from_renderer(
        initial_render_camera.vertical_field_of_view_radians);
    camera_info.near_plane = initial_render_camera.near_plane;
    camera_info.far_plane = initial_render_camera.far_plane;
    camera_info.mode = mode == InteractivePreviewMode::free_flight_world
        ? gameplay_camera::GameplayCameraMode::free_flight
        : gameplay_camera::GameplayCameraMode::entity_first_person;
    if (mode == InteractivePreviewMode::entity_first_person &&
        controlled_entity && *controlled_entity != 0U) {
        camera_info.anchor_metadata = {
            gameplay_camera::GameplayCameraAnchorStatus::
                anchor_entity_missing,
            controlled_entity,
            std::nullopt,
            gameplay_camera::GameplayCameraAnchorEvidenceProfile::
                explicit_synthetic_playback_v1,
        };
    }
    auto camera = gameplay_camera::GameplayCameraState::create(camera_info);

    gameplay_camera::FirstPersonCameraConfigCreateInfo config_info;
    config_info.vertical_fov_radians = gameplay_fov_from_renderer(
        initial_render_camera.vertical_field_of_view_radians);
    config_info.near_plane = initial_render_camera.near_plane;
    config_info.far_plane = initial_render_camera.far_plane;
    auto config = gameplay_camera::FirstPersonCameraConfig::create(config_info);
    auto bindings = gameplay_input::GameplayInputBindings::project_default_v1();
    if (!camera || !camera.state || !config || !config.config || !bindings ||
        !bindings.bindings) {
        return {
            std::nullopt,
            InteractivePreviewError{
                InteractivePreviewErrorCode::invalid_configuration,
                "Project-default interactive preview state is unavailable"},
        };
    }
    return create(
        mode,
        std::move(*bindings.bindings),
        std::move(*config.config),
        std::move(*camera.state),
        controlled_entity,
        explicit_eye_offset);
}

InteractivePreviewUpdateResult InteractivePreviewController::update(
    const input::InputSnapshot& snapshot,
    const double elapsed_duration_seconds,
    client::IInteractiveCameraPublicationTarget& publication_target,
    const entity_render::EntityRenderFrame* entity_frame) noexcept
{
    ++statistics_.update_attempt_count;
    if (snapshot.sequence() == 0U ||
        snapshot.sequence() <= last_input_sequence_) {
        return fail(
            InteractivePreviewErrorCode::non_monotonic_input_sequence,
            "Interactive input sequence did not advance",
            snapshot.sequence(),
            camera_->revision());
    }

    const gameplay_input::GameplayInputIntentBuilder intent_builder;
    auto built_intent = intent_builder.build(
        snapshot,
        *bindings_,
        camera_config_->mouse_look_config(),
        elapsed_duration_seconds);
    if (!built_intent || !built_intent.intent) {
        return fail(
            InteractivePreviewErrorCode::intent_build_failed,
            "Gameplay input intent construction failed",
            snapshot.sequence(),
            camera_->revision());
    }
    const auto& intent = *built_intent.intent;

    gameplay_camera::GameplayCameraUpdateResult camera_update;
    if (mode_ == InteractivePreviewMode::free_flight_world) {
        camera_update = gameplay_camera::LocalFreeFlightCameraController{}.update(
            *camera_, intent, elapsed_duration_seconds, *camera_config_);
    } else {
        std::optional<gameplay_camera::EntityFirstPersonCameraAnchor> anchor;
        if (entity_frame != nullptr) {
            auto resolved = resolve_anchor(
                *entity_frame, *controlled_entity_, explicit_eye_offset_);
            if (!resolved) {
                return fail(
                    InteractivePreviewErrorCode::anchor_resolution_failed,
                    "Entity first-person anchor resolution failed",
                    snapshot.sequence(),
                    camera_->revision());
            }
            if (resolved->anchor) {
                anchor.emplace(std::move(*resolved->anchor));
            }
        }
        camera_update = gameplay_camera::EntityFirstPersonCameraController{}.update(
            *camera_, intent, anchor, *camera_config_);
    }

    const bool nonfatal_anchor_missing = camera_update.error &&
        camera_update.error->code ==
            gameplay_camera::GameplayCameraErrorCode::anchor_missing;
    if (!camera_update.camera ||
        (camera_update.error && !nonfatal_anchor_missing)) {
        return fail(
            InteractivePreviewErrorCode::camera_update_failed,
            "First-person camera update failed",
            snapshot.sequence(),
            camera_->revision());
    }
    auto render_camera =
        gameplay_camera::build_render_camera(*camera_update.camera);
    if (!render_camera || !render_camera.camera ||
        !renderer::is_valid(*render_camera.camera)) {
        return fail(
            InteractivePreviewErrorCode::camera_conversion_failed,
            "First-person render-camera conversion failed",
            snapshot.sequence(),
            camera_->revision());
    }

    const auto client_mode = mode_ == InteractivePreviewMode::free_flight_world
        ? client::InteractiveCameraMode::free_flight_world
        : client::InteractiveCameraMode::entity_first_person;
    const auto anchor_status = mode_ == InteractivePreviewMode::free_flight_world
        ? client::ControlledEntityCameraStatus::not_applicable
        : nonfatal_anchor_missing
            ? client::ControlledEntityCameraStatus::anchor_missing
            : client::ControlledEntityCameraStatus::anchored;
    const client::RenderCameraState client_camera{
        render_camera.camera->position,
        render_camera.camera->target,
        render_camera.camera->up,
        render_camera.camera->vertical_field_of_view_radians,
        render_camera.camera->near_plane,
        render_camera.camera->far_plane,
    };
    std::shared_ptr<const gameplay_camera::GameplayCameraState> retained_camera;
    try {
        retained_camera =
            std::make_shared<const gameplay_camera::GameplayCameraState>(
                std::move(*camera_update.camera));
    } catch (const std::bad_alloc&) {
        return fail(
            InteractivePreviewErrorCode::unable_to_retain_state,
            "Interactive camera allocation is unavailable",
            snapshot.sequence(),
            camera_->revision());
    }
    if (!publication_target.publish_interactive_camera(
            client_camera,
            client::InteractiveCameraMetadata{
                snapshot.sequence(),
                retained_camera->revision(),
                client_mode,
                controlled_entity_,
                anchor_status,
            })) {
        return fail(
            InteractivePreviewErrorCode::client_world_publication_rejected,
            "Client world rejected the interactive camera",
            snapshot.sequence(),
            camera_->revision());
    }
    camera_ = std::move(retained_camera);
    last_input_sequence_ = snapshot.sequence();
    ++statistics_.published_update_count;
    if (camera_update.revision_changed) {
        ++statistics_.changed_camera_count;
    } else {
        ++statistics_.unchanged_camera_count;
    }
    if (camera_update.status ==
        gameplay_camera::GameplayCameraUpdateStatus::duration_clamped) {
        ++statistics_.duration_clamp_count;
    }
    if (nonfatal_anchor_missing) {
        ++statistics_.anchor_missing_count;
    }
    if (snapshot.reset_reason() != input::InputResetReason::none) {
        ++statistics_.focus_reset_count;
    }
    if (intent.capture_mouse_requested()) {
        ++statistics_.capture_request_count;
    }
    if (intent.release_mouse_requested()) {
        ++statistics_.release_request_count;
    }

    const auto status = nonfatal_anchor_missing
        ? InteractivePreviewUpdateStatus::anchor_missing_camera_frozen
        : camera_update.status ==
                gameplay_camera::GameplayCameraUpdateStatus::duration_clamped
            ? InteractivePreviewUpdateStatus::duration_clamped
            : camera_update.revision_changed
                ? InteractivePreviewUpdateStatus::camera_updated
                : InteractivePreviewUpdateStatus::unchanged;
    return {
        status,
        std::nullopt,
        snapshot.sequence(),
        camera_->revision(),
        camera_update.revision_changed,
        intent.capture_mouse_requested(),
        intent.release_mouse_requested(),
    };
}

bool InteractivePreviewController::seed_world_state_camera(
    client::IInteractiveCameraPublicationTarget& publication_target)
    const noexcept
{
    const auto built = gameplay_camera::build_render_camera(*camera_);
    if (!built || !built.camera || !renderer::is_valid(*built.camera)) {
        return false;
    }
    publication_target.publish_camera_seed(client::RenderCameraState{
        built.camera->position,
        built.camera->target,
        built.camera->up,
        built.camera->vertical_field_of_view_radians,
        built.camera->near_plane,
        built.camera->far_plane,
    });
    return true;
}

InteractivePreviewMode InteractivePreviewController::mode() const noexcept
{
    return mode_;
}

std::shared_ptr<const gameplay_camera::GameplayCameraState>
InteractivePreviewController::camera() const noexcept
{
    return camera_;
}

std::optional<std::uint32_t>
InteractivePreviewController::controlled_entity() const noexcept
{
    return controlled_entity_;
}

const assets::AssetVector3&
InteractivePreviewController::explicit_eye_offset() const noexcept
{
    return explicit_eye_offset_;
}

std::uint64_t InteractivePreviewController::last_input_sequence() const noexcept
{
    return last_input_sequence_;
}

const InteractivePreviewStatistics&
InteractivePreviewController::statistics() const noexcept
{
    return statistics_;
}

} // namespace hlclient::interactive_preview

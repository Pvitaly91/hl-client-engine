#include <hlclient/gameplay_camera/entity_first_person_camera.hpp>

#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::gameplay_camera {
namespace {

constexpr double kHardPitchLimit = 89.9;

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_source_frame_identity(
    const GameplayCameraSourceFrameIdentity& identity) noexcept
{
    return identity.resource_id != 0U && identity.resource_revision != 0U &&
        identity.frame_signature != 0U;
}

[[nodiscard]] bool valid_intent(
    const gameplay_input::GameplayInputIntent& intent) noexcept
{
    return std::isfinite(intent.forward_axis()) &&
        std::isfinite(intent.side_axis()) &&
        std::isfinite(intent.vertical_axis()) &&
        std::isfinite(intent.look_delta_yaw_degrees()) &&
        std::isfinite(intent.look_delta_pitch_degrees()) &&
        std::isfinite(intent.sample_duration_seconds()) &&
        intent.forward_axis() >= -1.0 && intent.forward_axis() <= 1.0 &&
        intent.side_axis() >= -1.0 && intent.side_axis() <= 1.0 &&
        intent.vertical_axis() >= -1.0 && intent.vertical_axis() <= 1.0 &&
        intent.sample_duration_seconds() >= 0.0;
}

[[nodiscard]] bool within_position_limit(
    const assets::AssetVector3& value,
    const double limit) noexcept
{
    if (!finite(value) || !std::isfinite(limit) || limit <= 0.0) {
        return false;
    }
    const auto x = static_cast<double>(value.x);
    const auto y = static_cast<double>(value.y);
    const auto z = static_cast<double>(value.z);
    const auto squared = x * x + y * y + z * z;
    return std::isfinite(squared) && squared <= limit * limit;
}

[[nodiscard]] double distance(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    const auto x = static_cast<double>(left.x) - static_cast<double>(right.x);
    const auto y = static_cast<double>(left.y) - static_cast<double>(right.y);
    const auto z = static_cast<double>(left.z) - static_cast<double>(right.z);
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] bool equal_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool previous_camera_matches_config(
    const GameplayCameraState& previous,
    const FirstPersonCameraConfig& config) noexcept
{
    return previous.pitch_degrees() >= config.minimum_pitch_degrees() &&
        previous.pitch_degrees() <= config.maximum_pitch_degrees() &&
        previous.vertical_fov_radians() == config.vertical_fov_radians() &&
        previous.near_plane() == config.near_plane() &&
        previous.far_plane() == config.far_plane();
}

[[nodiscard]] GameplayCameraUpdateResult fail_update(
    const GameplayCameraErrorCode code,
    const std::string_view context,
    const gameplay_input::GameplayInputIntent& intent) noexcept
{
    GameplayCameraUpdateResult result;
    result.error = GameplayCameraError{code, context};
    result.focused = intent.focused();
    result.captured = intent.captured();
    return result;
}

} // namespace

std::string_view to_string(
    const EntityFirstPersonCameraAnchorErrorCode code) noexcept
{
    switch (code) {
    case EntityFirstPersonCameraAnchorErrorCode::invalid_entity_number:
        return "invalid_entity_number";
    case EntityFirstPersonCameraAnchorErrorCode::non_finite_origin:
        return "non_finite_origin";
    case EntityFirstPersonCameraAnchorErrorCode::non_finite_eye_offset:
        return "non_finite_eye_offset";
    case EntityFirstPersonCameraAnchorErrorCode::invalid_initial_angles:
        return "invalid_initial_angles";
    case EntityFirstPersonCameraAnchorErrorCode::
        invalid_source_frame_identity:
        return "invalid_source_frame_identity";
    case EntityFirstPersonCameraAnchorErrorCode::unsupported_evidence_profile:
        return "unsupported_evidence_profile";
    case EntityFirstPersonCameraAnchorErrorCode::anchor_entity_missing:
        return "anchor_entity_missing";
    }
    return "unknown";
}

EntityFirstPersonCameraAnchor::EntityFirstPersonCameraAnchor(
    const EntityFirstPersonCameraAnchorCreateInfo& create_info) noexcept
    : entity_number_(create_info.entity_number),
      interpolated_origin_(create_info.interpolated_origin),
      explicit_local_eye_offset_(create_info.explicit_local_eye_offset),
      initial_yaw_degrees_(create_info.initial_yaw_degrees),
      initial_pitch_degrees_(create_info.initial_pitch_degrees),
      source_frame_identity_(create_info.source_frame_identity),
      evidence_profile_(create_info.evidence_profile)
{
}

EntityFirstPersonCameraAnchor::CreationResult
EntityFirstPersonCameraAnchor::create(
    const EntityFirstPersonCameraAnchorCreateInfo& create_info) noexcept
{
    if (create_info.entity_number == 0U) {
        return {std::nullopt,
            EntityFirstPersonCameraAnchorError{
                EntityFirstPersonCameraAnchorErrorCode::invalid_entity_number,
                create_info.entity_number,
                "synthetic controlled entity number must be non-zero"}};
    }
    if (!finite(create_info.interpolated_origin)) {
        return {std::nullopt,
            EntityFirstPersonCameraAnchorError{
                EntityFirstPersonCameraAnchorErrorCode::non_finite_origin,
                create_info.entity_number,
                "interpolated entity origin is non-finite"}};
    }
    if (!finite(create_info.explicit_local_eye_offset)) {
        return {std::nullopt,
            EntityFirstPersonCameraAnchorError{
                EntityFirstPersonCameraAnchorErrorCode::non_finite_eye_offset,
                create_info.entity_number,
                "explicit eye offset is non-finite"}};
    }
    if ((create_info.initial_yaw_degrees &&
            !std::isfinite(*create_info.initial_yaw_degrees)) ||
        (create_info.initial_pitch_degrees &&
            (!std::isfinite(*create_info.initial_pitch_degrees) ||
                *create_info.initial_pitch_degrees < -kHardPitchLimit ||
                *create_info.initial_pitch_degrees > kHardPitchLimit))) {
        return {std::nullopt,
            EntityFirstPersonCameraAnchorError{
                EntityFirstPersonCameraAnchorErrorCode::invalid_initial_angles,
                create_info.entity_number,
                "initial anchor angles violate finite camera bounds"}};
    }
    if (!valid_source_frame_identity(create_info.source_frame_identity)) {
        return {std::nullopt,
            EntityFirstPersonCameraAnchorError{
                EntityFirstPersonCameraAnchorErrorCode::
                    invalid_source_frame_identity,
                create_info.entity_number,
                "anchor source frame identity must be complete"}};
    }
    if (create_info.evidence_profile !=
        GameplayCameraAnchorEvidenceProfile::
            explicit_synthetic_playback_v1) {
        return {std::nullopt,
            EntityFirstPersonCameraAnchorError{
                EntityFirstPersonCameraAnchorErrorCode::
                    unsupported_evidence_profile,
                create_info.entity_number,
                "stock player eye-height semantics remain evidence-pending"}};
    }

    auto normalized = create_info;
    if (normalized.initial_yaw_degrees) {
        normalized.initial_yaw_degrees =
            normalize_yaw_degrees(*normalized.initial_yaw_degrees);
        if (!normalized.initial_yaw_degrees) {
            return {std::nullopt,
                EntityFirstPersonCameraAnchorError{
                    EntityFirstPersonCameraAnchorErrorCode::
                        invalid_initial_angles,
                    create_info.entity_number,
                    "initial anchor yaw could not be normalized"}};
        }
    }
    return {std::optional<EntityFirstPersonCameraAnchor>{
                EntityFirstPersonCameraAnchor{normalized}},
        std::nullopt};
}

std::uint32_t EntityFirstPersonCameraAnchor::entity_number() const noexcept
{
    return entity_number_;
}

const assets::AssetVector3&
EntityFirstPersonCameraAnchor::interpolated_origin() const noexcept
{
    return interpolated_origin_;
}

const assets::AssetVector3&
EntityFirstPersonCameraAnchor::explicit_local_eye_offset() const noexcept
{
    return explicit_local_eye_offset_;
}

std::optional<double>
EntityFirstPersonCameraAnchor::initial_yaw_degrees() const noexcept
{
    return initial_yaw_degrees_;
}

std::optional<double>
EntityFirstPersonCameraAnchor::initial_pitch_degrees() const noexcept
{
    return initial_pitch_degrees_;
}

GameplayCameraSourceFrameIdentity
EntityFirstPersonCameraAnchor::source_frame_identity() const noexcept
{
    return source_frame_identity_;
}

GameplayCameraAnchorEvidenceProfile
EntityFirstPersonCameraAnchor::evidence_profile() const noexcept
{
    return evidence_profile_;
}

GameplayCameraUpdateResult EntityFirstPersonCameraController::update(
    const GameplayCameraState& previous,
    const gameplay_input::GameplayInputIntent& intent,
    const std::optional<EntityFirstPersonCameraAnchor>& anchor,
    const FirstPersonCameraConfig& config) const noexcept
{
    if (previous.mode() != GameplayCameraMode::entity_first_person ||
        previous.compatibility_profile() !=
            GameplayCameraCompatibilityProfile::local_first_person_z_up_v1 ||
        previous.evidence_profile() != GameplayCameraEvidenceProfile::
            project_owned_local_first_person_camera_v1) {
        return fail_update(GameplayCameraErrorCode::invalid_input,
            "entity camera controller received an incompatible camera state",
            intent);
    }
    if (!valid_intent(intent)) {
        return fail_update(GameplayCameraErrorCode::invalid_input,
            "entity camera controller received an invalid intent", intent);
    }
    if (!within_position_limit(
            previous.position(), config.maximum_position_magnitude())) {
        return fail_update(GameplayCameraErrorCode::position_limit_exceeded,
            "camera position exceeds the configured safety limit", intent);
    }
    if (previous.revision() > config.maximum_camera_revisions()) {
        return fail_update(GameplayCameraErrorCode::revision_limit_exceeded,
            "camera revision exceeds the configured safety limit", intent);
    }
    if (!previous_camera_matches_config(previous, config)) {
        return fail_update(GameplayCameraErrorCode::invalid_input,
            "entity camera state does not match the active camera configuration",
            intent);
    }

    if (!anchor) {
        GameplayCameraStateCreateInfo frozen_info;
        frozen_info.position = previous.position();
        frozen_info.yaw_degrees = previous.yaw_degrees();
        frozen_info.pitch_degrees = previous.pitch_degrees();
        frozen_info.vertical_fov_radians = previous.vertical_fov_radians();
        frozen_info.near_plane = previous.near_plane();
        frozen_info.far_plane = previous.far_plane();
        frozen_info.mode = GameplayCameraMode::entity_first_person;
        frozen_info.anchor_metadata = previous.anchor_metadata();
        if (frozen_info.anchor_metadata.entity_number) {
            frozen_info.anchor_metadata.status =
                GameplayCameraAnchorStatus::anchor_entity_missing;
            frozen_info.anchor_metadata.source_frame_identity.reset();
            frozen_info.anchor_metadata.evidence_profile =
                GameplayCameraAnchorEvidenceProfile::
                    explicit_synthetic_playback_v1;
        }
        frozen_info.revision = previous.revision();
        frozen_info.compatibility_profile = previous.compatibility_profile();
        frozen_info.evidence_profile = previous.evidence_profile();
        auto frozen = GameplayCameraState::create(frozen_info);
        if (!frozen) {
            return fail_update(GameplayCameraErrorCode::camera_validation_failed,
                "last valid entity camera could not be frozen", intent);
        }
        GameplayCameraUpdateResult result;
        result.camera.emplace(std::move(*frozen.state));
        result.status = GameplayCameraUpdateStatus::anchor_entity_missing;
        result.error = GameplayCameraError{GameplayCameraErrorCode::anchor_missing,
            "controlled entity is absent; last valid camera is frozen"};
        result.focused = intent.focused();
        result.captured = intent.captured();
        return result;
    }

    const auto origin = anchor->interpolated_origin();
    const auto offset = anchor->explicit_local_eye_offset();
    const auto next_x = static_cast<double>(origin.x) +
        static_cast<double>(offset.x);
    const auto next_y = static_cast<double>(origin.y) +
        static_cast<double>(offset.y);
    const auto next_z = static_cast<double>(origin.z) +
        static_cast<double>(offset.z);
    if (!std::isfinite(next_x) || !std::isfinite(next_y) ||
        !std::isfinite(next_z) ||
        std::abs(next_x) > std::numeric_limits<float>::max() ||
        std::abs(next_y) > std::numeric_limits<float>::max() ||
        std::abs(next_z) > std::numeric_limits<float>::max()) {
        return fail_update(GameplayCameraErrorCode::non_finite_camera,
            "entity origin plus explicit eye offset is not representable",
            intent);
    }
    const assets::AssetVector3 position{static_cast<float>(next_x),
        static_cast<float>(next_y),
        static_cast<float>(next_z)};
    if (!within_position_limit(position, config.maximum_position_magnitude())) {
        return fail_update(GameplayCameraErrorCode::position_limit_exceeded,
            "entity camera position exceeds the configured safety limit",
            intent);
    }

    const auto same_controlled_entity =
        previous.anchor_metadata().entity_number &&
        *previous.anchor_metadata().entity_number == anchor->entity_number();
    auto yaw = same_controlled_entity
        ? previous.yaw_degrees()
        : anchor->initial_yaw_degrees().value_or(previous.yaw_degrees());
    auto pitch = same_controlled_entity
        ? previous.pitch_degrees()
        : anchor->initial_pitch_degrees().value_or(previous.pitch_degrees());
    if (intent.focused() && intent.captured()) {
        yaw += intent.look_delta_yaw_degrees();
        pitch += intent.look_delta_pitch_degrees();
    }
    const auto normalized = normalize_yaw_degrees(yaw);
    const auto clamped = clamp_pitch_degrees(pitch,
        config.minimum_pitch_degrees(),
        config.maximum_pitch_degrees());
    if (!normalized || !clamped) {
        return fail_update(GameplayCameraErrorCode::non_finite_camera,
            "entity camera look update produced a non-finite result",
            intent);
    }
    yaw = *normalized;
    pitch = *clamped;

    const auto changed = !equal_vector(position, previous.position()) ||
        yaw != previous.yaw_degrees() || pitch != previous.pitch_degrees() ||
        config.vertical_fov_radians() != previous.vertical_fov_radians() ||
        config.near_plane() != previous.near_plane() ||
        config.far_plane() != previous.far_plane();
    auto revision = previous.revision();
    if (changed) {
        if (revision >= config.maximum_camera_revisions() ||
            revision == std::numeric_limits<std::uint64_t>::max()) {
            return fail_update(
                GameplayCameraErrorCode::revision_limit_exceeded,
                "entity camera revision cannot be incremented", intent);
        }
        ++revision;
    }

    GameplayCameraStateCreateInfo create_info;
    create_info.position = position;
    create_info.yaw_degrees = yaw;
    create_info.pitch_degrees = pitch;
    create_info.vertical_fov_radians = config.vertical_fov_radians();
    create_info.near_plane = config.near_plane();
    create_info.far_plane = config.far_plane();
    create_info.mode = GameplayCameraMode::entity_first_person;
    create_info.anchor_metadata.status = GameplayCameraAnchorStatus::attached;
    create_info.anchor_metadata.entity_number = anchor->entity_number();
    create_info.anchor_metadata.source_frame_identity =
        anchor->source_frame_identity();
    create_info.anchor_metadata.evidence_profile = anchor->evidence_profile();
    create_info.revision = revision;
    create_info.compatibility_profile = previous.compatibility_profile();
    create_info.evidence_profile = previous.evidence_profile();
    auto created = GameplayCameraState::create(create_info);
    if (!created) {
        return fail_update(GameplayCameraErrorCode::camera_validation_failed,
            "updated entity camera failed validation", intent);
    }

    GameplayCameraUpdateResult result;
    result.camera.emplace(std::move(*created.state));
    result.status = changed ? GameplayCameraUpdateStatus::updated
                            : GameplayCameraUpdateStatus::unchanged;
    result.movement_distance = distance(position, previous.position());
    result.revision_changed = changed;
    result.focused = intent.focused();
    result.captured = intent.captured();
    return result;
}

} // namespace hlclient::gameplay_camera

#pragma once

#include <hlclient/gameplay_camera/first_person_camera.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::gameplay_camera {

enum class EntityFirstPersonCameraAnchorErrorCode {
    invalid_entity_number,
    non_finite_origin,
    non_finite_eye_offset,
    invalid_initial_angles,
    invalid_source_frame_identity,
    unsupported_evidence_profile,
    anchor_entity_missing,
};

[[nodiscard]] std::string_view to_string(
    EntityFirstPersonCameraAnchorErrorCode code) noexcept;

struct EntityFirstPersonCameraAnchorError {
    EntityFirstPersonCameraAnchorErrorCode code{
        EntityFirstPersonCameraAnchorErrorCode::invalid_entity_number};
    std::uint32_t entity_number{0U};
    std::string_view context;
};

struct EntityFirstPersonCameraAnchorCreateInfo {
    std::uint32_t entity_number{0U};
    assets::AssetVector3 interpolated_origin{};
    // Explicit project fixture metadata. This is added directly in source
    // coordinates; no stock clientdata/hull eye-height inference occurs.
    assets::AssetVector3 explicit_local_eye_offset{};
    std::optional<double> initial_yaw_degrees;
    std::optional<double> initial_pitch_degrees;
    GameplayCameraSourceFrameIdentity source_frame_identity{};
    GameplayCameraAnchorEvidenceProfile evidence_profile{
        GameplayCameraAnchorEvidenceProfile::
            explicit_synthetic_playback_v1};
};

class EntityFirstPersonCameraAnchor final {
public:
    struct CreationResult;

    EntityFirstPersonCameraAnchor(
        const EntityFirstPersonCameraAnchor&) = default;
    EntityFirstPersonCameraAnchor(
        EntityFirstPersonCameraAnchor&&) noexcept = default;
    EntityFirstPersonCameraAnchor& operator=(
        const EntityFirstPersonCameraAnchor&) = delete;
    EntityFirstPersonCameraAnchor& operator=(
        EntityFirstPersonCameraAnchor&&) = delete;
    ~EntityFirstPersonCameraAnchor() = default;

    [[nodiscard]] static CreationResult create(
        const EntityFirstPersonCameraAnchorCreateInfo& create_info) noexcept;

    [[nodiscard]] std::uint32_t entity_number() const noexcept;
    [[nodiscard]] const assets::AssetVector3& interpolated_origin()
        const noexcept;
    [[nodiscard]] const assets::AssetVector3& explicit_local_eye_offset()
        const noexcept;
    [[nodiscard]] std::optional<double> initial_yaw_degrees() const noexcept;
    [[nodiscard]] std::optional<double> initial_pitch_degrees() const noexcept;
    [[nodiscard]] GameplayCameraSourceFrameIdentity source_frame_identity()
        const noexcept;
    [[nodiscard]] GameplayCameraAnchorEvidenceProfile evidence_profile()
        const noexcept;

private:
    explicit EntityFirstPersonCameraAnchor(
        const EntityFirstPersonCameraAnchorCreateInfo& create_info) noexcept;

    std::uint32_t entity_number_{0U};
    assets::AssetVector3 interpolated_origin_{};
    assets::AssetVector3 explicit_local_eye_offset_{};
    std::optional<double> initial_yaw_degrees_;
    std::optional<double> initial_pitch_degrees_;
    GameplayCameraSourceFrameIdentity source_frame_identity_{};
    GameplayCameraAnchorEvidenceProfile evidence_profile_{
        GameplayCameraAnchorEvidenceProfile::
            explicit_synthetic_playback_v1};
};

struct EntityFirstPersonCameraAnchor::CreationResult {
    std::optional<EntityFirstPersonCameraAnchor> anchor;
    std::optional<EntityFirstPersonCameraAnchorError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return anchor.has_value();
    }
};

class EntityFirstPersonCameraController final {
public:
    [[nodiscard]] GameplayCameraUpdateResult update(
        const GameplayCameraState& previous,
        const gameplay_input::GameplayInputIntent& intent,
        const std::optional<EntityFirstPersonCameraAnchor>& anchor,
        const FirstPersonCameraConfig& config) const noexcept;
};

} // namespace hlclient::gameplay_camera

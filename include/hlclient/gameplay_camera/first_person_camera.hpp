#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/renderer/render_scene.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::gameplay_camera {

// Project-owned camera semantics. The pending profile is a typed boundary and
// is never executed by the M4.6.1 controllers.
enum class GameplayCameraCompatibilityProfile {
    local_first_person_z_up_v1,
    stock_view_angles_evidence_pending,
};

enum class GameplayCameraEvidenceProfile {
    project_owned_local_first_person_camera_v1,
    evidence_pending_m4_6_2,
};

enum class GameplayCameraMode {
    static_view,
    orbit,
    spawn,
    free_flight,
    entity_first_person,
};

enum class GameplayCameraAnchorStatus {
    none,
    attached,
    anchor_entity_missing,
};

enum class GameplayCameraAnchorEvidenceProfile {
    none,
    explicit_synthetic_playback_v1,
    stock_player_eye_height_evidence_pending,
};

struct GameplayCameraSourceFrameIdentity {
    std::uint64_t resource_id{0U};
    std::uint64_t resource_revision{0U};
    std::uint64_t frame_signature{0U};

    [[nodiscard]] friend bool operator==(
        const GameplayCameraSourceFrameIdentity&,
        const GameplayCameraSourceFrameIdentity&) = default;
};

struct GameplayCameraAnchorMetadata {
    GameplayCameraAnchorStatus status{GameplayCameraAnchorStatus::none};
    std::optional<std::uint32_t> entity_number;
    std::optional<GameplayCameraSourceFrameIdentity> source_frame_identity;
    GameplayCameraAnchorEvidenceProfile evidence_profile{
        GameplayCameraAnchorEvidenceProfile::none};

    [[nodiscard]] friend bool operator==(
        const GameplayCameraAnchorMetadata&,
        const GameplayCameraAnchorMetadata&) = default;
};

enum class GameplayCameraErrorCode {
    invalid_configuration,
    unsupported_compatibility_profile,
    invalid_input,
    invalid_duration,
    non_finite_camera,
    position_limit_exceeded,
    revision_limit_exceeded,
    anchor_missing,
    camera_validation_failed,
};

[[nodiscard]] std::string_view to_string(
    GameplayCameraCompatibilityProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    GameplayCameraEvidenceProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(GameplayCameraMode mode) noexcept;
[[nodiscard]] std::string_view to_string(
    GameplayCameraAnchorStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    GameplayCameraAnchorEvidenceProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(GameplayCameraErrorCode code) noexcept;

struct GameplayCameraError {
    GameplayCameraErrorCode code{
        GameplayCameraErrorCode::invalid_configuration};
    std::string_view context;
};

struct GameplayCameraStateCreateInfo {
    assets::AssetVector3 position{};
    double yaw_degrees{0.0};
    double pitch_degrees{0.0};
    double vertical_fov_radians{1.0471975511965976};
    double near_plane{0.1};
    double far_plane{4'096.0};
    GameplayCameraMode mode{GameplayCameraMode::free_flight};
    GameplayCameraAnchorMetadata anchor_metadata{};
    std::uint64_t revision{1U};
    GameplayCameraCompatibilityProfile compatibility_profile{
        GameplayCameraCompatibilityProfile::local_first_person_z_up_v1};
    GameplayCameraEvidenceProfile evidence_profile{
        GameplayCameraEvidenceProfile::
            project_owned_local_first_person_camera_v1};
};

// Owning immutable value. Controllers can derive a new value but callers
// cannot mutate a published camera in place.
class GameplayCameraState final {
public:
    struct CreationResult;

    GameplayCameraState(const GameplayCameraState&) = default;
    GameplayCameraState(GameplayCameraState&&) noexcept = default;
    GameplayCameraState& operator=(const GameplayCameraState&) = delete;
    GameplayCameraState& operator=(GameplayCameraState&&) = delete;
    ~GameplayCameraState() = default;

    [[nodiscard]] static CreationResult create(
        const GameplayCameraStateCreateInfo& create_info) noexcept;

    [[nodiscard]] const assets::AssetVector3& position() const noexcept;
    [[nodiscard]] double yaw_degrees() const noexcept;
    [[nodiscard]] double pitch_degrees() const noexcept;
    [[nodiscard]] double vertical_fov_radians() const noexcept;
    [[nodiscard]] double near_plane() const noexcept;
    [[nodiscard]] double far_plane() const noexcept;
    [[nodiscard]] GameplayCameraMode mode() const noexcept;
    [[nodiscard]] const GameplayCameraAnchorMetadata& anchor_metadata()
        const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] GameplayCameraCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] GameplayCameraEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class LocalFreeFlightCameraController;
    friend class EntityFirstPersonCameraController;

    explicit GameplayCameraState(
        const GameplayCameraStateCreateInfo& create_info) noexcept;

    assets::AssetVector3 position_{};
    double yaw_degrees_{0.0};
    double pitch_degrees_{0.0};
    double vertical_fov_radians_{1.0471975511965976};
    double near_plane_{0.1};
    double far_plane_{4'096.0};
    GameplayCameraMode mode_{GameplayCameraMode::free_flight};
    GameplayCameraAnchorMetadata anchor_metadata_{};
    std::uint64_t revision_{1U};
    GameplayCameraCompatibilityProfile compatibility_profile_{
        GameplayCameraCompatibilityProfile::local_first_person_z_up_v1};
    GameplayCameraEvidenceProfile evidence_profile_{
        GameplayCameraEvidenceProfile::
            project_owned_local_first_person_camera_v1};
};

struct GameplayCameraState::CreationResult {
    std::optional<GameplayCameraState> state;
    std::optional<GameplayCameraError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

struct FirstPersonCameraConfigCreateInfo {
    gameplay_input::MouseLookConfig mouse_look_config{};
    double minimum_pitch_degrees{-89.0};
    double maximum_pitch_degrees{89.0};
    double base_move_speed{320.0};
    double speed_multiplier{2.0};
    double maximum_frame_duration_seconds{0.1};
    double vertical_fov_radians{1.0471975511965976};
    double near_plane{0.1};
    double far_plane{4'096.0};
    double maximum_position_magnitude{10'000'000.0};
    std::uint64_t maximum_camera_revisions{
        UINT64_C(0xFFFFFFFFFFFFFFFF)};
};

// Immutable validated configuration. project_default_v1 values are preview
// constants, not GoldSrc cvars, sensitivity, movement or FOV semantics.
class FirstPersonCameraConfig final {
public:
    struct CreationResult;

    FirstPersonCameraConfig(const FirstPersonCameraConfig&) = default;
    FirstPersonCameraConfig(FirstPersonCameraConfig&&) noexcept = default;
    FirstPersonCameraConfig& operator=(const FirstPersonCameraConfig&) = delete;
    FirstPersonCameraConfig& operator=(FirstPersonCameraConfig&&) = delete;
    ~FirstPersonCameraConfig() = default;

    [[nodiscard]] static CreationResult create(
        const FirstPersonCameraConfigCreateInfo& create_info) noexcept;
    [[nodiscard]] static FirstPersonCameraConfig project_default_v1() noexcept;

    [[nodiscard]] const gameplay_input::MouseLookConfig& mouse_look_config()
        const noexcept;
    [[nodiscard]] double minimum_pitch_degrees() const noexcept;
    [[nodiscard]] double maximum_pitch_degrees() const noexcept;
    [[nodiscard]] double base_move_speed() const noexcept;
    [[nodiscard]] double speed_multiplier() const noexcept;
    [[nodiscard]] double maximum_frame_duration_seconds() const noexcept;
    [[nodiscard]] double vertical_fov_radians() const noexcept;
    [[nodiscard]] double near_plane() const noexcept;
    [[nodiscard]] double far_plane() const noexcept;
    [[nodiscard]] double maximum_position_magnitude() const noexcept;
    [[nodiscard]] std::uint64_t maximum_camera_revisions() const noexcept;

private:
    explicit FirstPersonCameraConfig(
        const FirstPersonCameraConfigCreateInfo& create_info) noexcept;

    gameplay_input::MouseLookConfig mouse_look_config_{};
    double minimum_pitch_degrees_{-89.0};
    double maximum_pitch_degrees_{89.0};
    double base_move_speed_{320.0};
    double speed_multiplier_{2.0};
    double maximum_frame_duration_seconds_{0.1};
    double vertical_fov_radians_{1.0471975511965976};
    double near_plane_{0.1};
    double far_plane_{4'096.0};
    double maximum_position_magnitude_{10'000'000.0};
    std::uint64_t maximum_camera_revisions_{UINT64_C(0xFFFFFFFFFFFFFFFF)};
};

struct FirstPersonCameraConfig::CreationResult {
    std::optional<FirstPersonCameraConfig> config;
    std::optional<GameplayCameraError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return config.has_value();
    }
};

enum class GameplayCameraUpdateStatus {
    unchanged,
    updated,
    duration_clamped,
    anchor_entity_missing,
};

[[nodiscard]] std::string_view to_string(
    GameplayCameraUpdateStatus status) noexcept;

struct GameplayCameraUpdateResult {
    GameplayCameraUpdateResult() = default;
    GameplayCameraUpdateResult(const GameplayCameraUpdateResult&) = default;
    GameplayCameraUpdateResult(GameplayCameraUpdateResult&&) noexcept = default;
    GameplayCameraUpdateResult& operator=(
        const GameplayCameraUpdateResult& other);
    GameplayCameraUpdateResult& operator=(
        GameplayCameraUpdateResult&& other) noexcept;
    ~GameplayCameraUpdateResult() = default;

    // On a non-fatal missing-anchor result this contains the frozen prior
    // camera alongside a typed anchor_missing diagnostic.
    std::optional<GameplayCameraState> camera;
    GameplayCameraUpdateStatus status{GameplayCameraUpdateStatus::unchanged};
    std::optional<GameplayCameraError> error;
    double movement_distance{0.0};
    double requested_duration_seconds{0.0};
    double applied_duration_seconds{0.0};
    bool revision_changed{false};
    bool focused{false};
    bool captured{false};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return camera.has_value() && !error.has_value();
    }
};

struct RenderCameraBuildResult {
    std::optional<renderer::RenderCamera> camera;
    std::optional<GameplayCameraError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return camera.has_value();
    }
};

// All directions use source-native, right-handed Z-up coordinates. Yaw zero
// faces +X, positive yaw rotates toward +Y and positive pitch looks upward.
[[nodiscard]] std::optional<double> normalize_yaw_degrees(
    double yaw_degrees) noexcept;
[[nodiscard]] std::optional<double> clamp_pitch_degrees(
    double pitch_degrees,
    double minimum_pitch_degrees,
    double maximum_pitch_degrees) noexcept;
[[nodiscard]] assets::AssetVector3 world_up() noexcept;
[[nodiscard]] std::optional<assets::AssetVector3> forward_from_yaw_pitch(
    double yaw_degrees,
    double pitch_degrees) noexcept;
[[nodiscard]] std::optional<assets::AssetVector3> horizontal_forward_from_yaw(
    double yaw_degrees) noexcept;
[[nodiscard]] std::optional<assets::AssetVector3> right_from_yaw(
    double yaw_degrees) noexcept;
[[nodiscard]] RenderCameraBuildResult build_render_camera(
    const GameplayCameraState& state) noexcept;

class LocalFreeFlightCameraController final {
public:
    [[nodiscard]] GameplayCameraUpdateResult update(
        const GameplayCameraState& previous,
        const gameplay_input::GameplayInputIntent& intent,
        double elapsed_duration_seconds,
        const FirstPersonCameraConfig& config) const noexcept;
};

} // namespace hlclient::gameplay_camera

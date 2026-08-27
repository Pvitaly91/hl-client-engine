#include <hlclient/gameplay_camera/first_person_camera.hpp>

#include <hlclient/gameplay_input/gameplay_input_limits.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::gameplay_camera {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kDegreesToRadians = kPi / 180.0;
// The gameplay camera API enforces the exact documented double-precision
// bounds. Renderer-origin float adaptation is owned by the narrow interactive
// preview adapter rather than weakening this public safety envelope.
constexpr double kMinimumFovRadians =
    gameplay_input::kGameplayInputSafetyHardLimits.
        minimum_vertical_fov_radians;
constexpr double kMaximumFovRadians =
    gameplay_input::kGameplayInputSafetyHardLimits.
        maximum_vertical_fov_radians;
constexpr double kHardPitchLimit = 89.9;
constexpr double kHardMaximumFrameDurationSeconds = 1.0;
constexpr double kHardMaximumFarPlane = 100'000'000.0;
constexpr double kHardMaximumPositionMagnitude = 10'000'000.0;
constexpr double kMinimumMouseSensitivity = 0.001;
constexpr double kMaximumMouseSensitivity = 10.0;
constexpr double kMaximumMouseLookDelta = 360.0;
constexpr double kMinimumMoveSpeed = 1.0;
constexpr double kMaximumMoveSpeed = 10'000.0;
constexpr double kMinimumSpeedMultiplier = 1.0;
constexpr double kMaximumSpeedMultiplier = 16.0;

[[nodiscard]] bool finite(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] double squared_magnitude(
    const assets::AssetVector3& value) noexcept
{
    return static_cast<double>(value.x) * static_cast<double>(value.x) +
        static_cast<double>(value.y) * static_cast<double>(value.y) +
        static_cast<double>(value.z) * static_cast<double>(value.z);
}

[[nodiscard]] bool within_position_limit(
    const assets::AssetVector3& value,
    const double limit) noexcept
{
    if (!finite(value) || !std::isfinite(limit) || limit <= 0.0) {
        return false;
    }
    const auto magnitude_squared = squared_magnitude(value);
    return std::isfinite(magnitude_squared) &&
        magnitude_squared <= limit * limit;
}

[[nodiscard]] bool equal_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] double vector_distance(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    const auto x = static_cast<double>(left.x) - static_cast<double>(right.x);
    const auto y = static_cast<double>(left.y) - static_cast<double>(right.y);
    const auto z = static_cast<double>(left.z) - static_cast<double>(right.z);
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] bool valid_anchor_metadata(
    const GameplayCameraMode mode,
    const GameplayCameraAnchorMetadata& metadata) noexcept
{
    switch (mode) {
    case GameplayCameraMode::static_view:
    case GameplayCameraMode::orbit:
    case GameplayCameraMode::spawn:
    case GameplayCameraMode::free_flight:
    case GameplayCameraMode::entity_first_person:
        break;
    default:
        return false;
    }
    switch (metadata.status) {
    case GameplayCameraAnchorStatus::none:
    case GameplayCameraAnchorStatus::attached:
    case GameplayCameraAnchorStatus::anchor_entity_missing:
        break;
    default:
        return false;
    }
    if (mode != GameplayCameraMode::entity_first_person) {
        return metadata.status == GameplayCameraAnchorStatus::none &&
            !metadata.entity_number.has_value() &&
            !metadata.source_frame_identity.has_value() &&
            metadata.evidence_profile ==
                GameplayCameraAnchorEvidenceProfile::none;
    }
    if (metadata.status == GameplayCameraAnchorStatus::none) {
        return !metadata.entity_number.has_value() &&
            !metadata.source_frame_identity.has_value() &&
            metadata.evidence_profile ==
                GameplayCameraAnchorEvidenceProfile::none;
    }
    if (!metadata.entity_number || *metadata.entity_number == 0U ||
        metadata.evidence_profile != GameplayCameraAnchorEvidenceProfile::
            explicit_synthetic_playback_v1) {
        return false;
    }
    if (metadata.status == GameplayCameraAnchorStatus::anchor_entity_missing) {
        return !metadata.source_frame_identity.has_value();
    }
    if (metadata.status != GameplayCameraAnchorStatus::attached) {
        return false;
    }
    if (!metadata.source_frame_identity) {
        return false;
    }
    return metadata.source_frame_identity->resource_id != 0U &&
        metadata.source_frame_identity->resource_revision != 0U &&
        metadata.source_frame_identity->frame_signature != 0U;
}

[[nodiscard]] bool valid_state_create_info(
    const GameplayCameraStateCreateInfo& create_info,
    GameplayCameraError& error) noexcept
{
    if (create_info.compatibility_profile !=
            GameplayCameraCompatibilityProfile::local_first_person_z_up_v1 ||
        create_info.evidence_profile != GameplayCameraEvidenceProfile::
            project_owned_local_first_person_camera_v1) {
        error = {GameplayCameraErrorCode::unsupported_compatibility_profile,
            "only the project-owned local first-person profile is executable"};
        return false;
    }
    if (!finite(create_info.position) ||
        !std::isfinite(create_info.yaw_degrees) ||
        !std::isfinite(create_info.pitch_degrees) ||
        !std::isfinite(create_info.vertical_fov_radians) ||
        !std::isfinite(create_info.near_plane) ||
        !std::isfinite(create_info.far_plane)) {
        error = {GameplayCameraErrorCode::non_finite_camera,
            "camera state contains a non-finite value"};
        return false;
    }
    if (!within_position_limit(
            create_info.position, kHardMaximumPositionMagnitude)) {
        error = {GameplayCameraErrorCode::position_limit_exceeded,
            "camera position exceeds the project safety limit"};
        return false;
    }
    if (create_info.pitch_degrees < -kHardPitchLimit ||
        create_info.pitch_degrees > kHardPitchLimit ||
        create_info.vertical_fov_radians < kMinimumFovRadians ||
        create_info.vertical_fov_radians > kMaximumFovRadians ||
        create_info.near_plane <= 0.0 ||
        create_info.far_plane <= create_info.near_plane ||
        create_info.far_plane > kHardMaximumFarPlane ||
        create_info.revision == 0U ||
        !valid_anchor_metadata(create_info.mode, create_info.anchor_metadata)) {
        error = {GameplayCameraErrorCode::invalid_configuration,
            "camera state violates a bounded project invariant"};
        return false;
    }
    return true;
}

[[nodiscard]] bool valid_config_create_info(
    const FirstPersonCameraConfigCreateInfo& create_info) noexcept
{
    const auto& mouse = create_info.mouse_look_config;
    const auto representable_as_float = [](const double value) noexcept {
        return std::isfinite(static_cast<float>(value));
    };
    const auto representable_projection = [&create_info]() noexcept {
        const auto field_of_view =
            static_cast<float>(create_info.vertical_fov_radians);
        const auto near_plane = static_cast<float>(create_info.near_plane);
        const auto far_plane = static_cast<float>(create_info.far_plane);
        if (!std::isfinite(field_of_view) || !std::isfinite(near_plane) ||
            !std::isfinite(far_plane) || near_plane <= 0.0F ||
            far_plane <= near_plane) {
            return false;
        }
        const auto tangent = std::tan(field_of_view * 0.5F);
        const auto depth_denominator = near_plane - far_plane;
        return std::isfinite(tangent) && tangent > 0.0F &&
            std::isfinite((far_plane + near_plane) / depth_denominator) &&
            std::isfinite(
                (2.0F * far_plane * near_plane) / depth_denominator);
    };
    return std::isfinite(mouse.degrees_per_pixel_x) &&
        std::isfinite(mouse.degrees_per_pixel_y) &&
        std::isfinite(mouse.maximum_delta_per_frame) &&
        mouse.degrees_per_pixel_x >= kMinimumMouseSensitivity &&
        mouse.degrees_per_pixel_x <= kMaximumMouseSensitivity &&
        mouse.degrees_per_pixel_y >= kMinimumMouseSensitivity &&
        mouse.degrees_per_pixel_y <= kMaximumMouseSensitivity &&
        mouse.maximum_delta_per_frame > 0.0 &&
        mouse.maximum_delta_per_frame <= kMaximumMouseLookDelta &&
        std::isfinite(create_info.minimum_pitch_degrees) &&
        std::isfinite(create_info.maximum_pitch_degrees) &&
        create_info.minimum_pitch_degrees >= -kHardPitchLimit &&
        create_info.maximum_pitch_degrees <= kHardPitchLimit &&
        create_info.minimum_pitch_degrees <
            create_info.maximum_pitch_degrees &&
        std::isfinite(create_info.base_move_speed) &&
        create_info.base_move_speed >= kMinimumMoveSpeed &&
        create_info.base_move_speed <= kMaximumMoveSpeed &&
        std::isfinite(create_info.speed_multiplier) &&
        create_info.speed_multiplier >= kMinimumSpeedMultiplier &&
        create_info.speed_multiplier <= kMaximumSpeedMultiplier &&
        create_info.base_move_speed * create_info.speed_multiplier <=
            kMaximumMoveSpeed &&
        std::isfinite(create_info.maximum_frame_duration_seconds) &&
        create_info.maximum_frame_duration_seconds > 0.0 &&
        create_info.maximum_frame_duration_seconds <=
            kHardMaximumFrameDurationSeconds &&
        std::isfinite(create_info.vertical_fov_radians) &&
        create_info.vertical_fov_radians >= kMinimumFovRadians &&
        create_info.vertical_fov_radians <= kMaximumFovRadians &&
        std::isfinite(create_info.near_plane) &&
        std::isfinite(create_info.far_plane) &&
        create_info.near_plane > 0.0 &&
        create_info.far_plane > create_info.near_plane &&
        create_info.far_plane <= kHardMaximumFarPlane &&
        std::isfinite(create_info.maximum_position_magnitude) &&
        create_info.maximum_position_magnitude >= 1.0 &&
        create_info.maximum_position_magnitude <=
            kHardMaximumPositionMagnitude &&
        create_info.maximum_camera_revisions > 0U &&
        representable_as_float(create_info.vertical_fov_radians) &&
        representable_as_float(create_info.near_plane) &&
        representable_as_float(create_info.far_plane) &&
        representable_projection();
}

[[nodiscard]] bool valid_intent(
    const gameplay_input::GameplayInputIntent& intent) noexcept
{
    constexpr double kMaximumAxisMagnitude = 1.0;
    return std::isfinite(intent.forward_axis()) &&
        std::isfinite(intent.side_axis()) &&
        std::isfinite(intent.vertical_axis()) &&
        std::isfinite(intent.look_delta_yaw_degrees()) &&
        std::isfinite(intent.look_delta_pitch_degrees()) &&
        std::isfinite(intent.sample_duration_seconds()) &&
        intent.forward_axis() >= -kMaximumAxisMagnitude &&
        intent.forward_axis() <= kMaximumAxisMagnitude &&
        intent.side_axis() >= -kMaximumAxisMagnitude &&
        intent.side_axis() <= kMaximumAxisMagnitude &&
        intent.vertical_axis() >= -kMaximumAxisMagnitude &&
        intent.vertical_axis() <= kMaximumAxisMagnitude &&
        intent.sample_duration_seconds() >= 0.0;
}

[[nodiscard]] GameplayCameraUpdateResult fail_update(
    const GameplayCameraErrorCode code,
    const std::string_view context,
    const gameplay_input::GameplayInputIntent& intent,
    const double requested_duration_seconds = 0.0) noexcept
{
    GameplayCameraUpdateResult result;
    result.error = GameplayCameraError{code, context};
    result.focused = intent.focused();
    result.captured = intent.captured();
    result.requested_duration_seconds = requested_duration_seconds;
    return result;
}

[[nodiscard]] std::optional<std::uint64_t> next_revision(
    const GameplayCameraState& previous,
    const FirstPersonCameraConfig& config) noexcept
{
    if (previous.revision() >= config.maximum_camera_revisions() ||
        previous.revision() == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return previous.revision() + 1U;
}

} // namespace

std::string_view to_string(
    const GameplayCameraCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case GameplayCameraCompatibilityProfile::local_first_person_z_up_v1:
        return "local_first_person_z_up_v1";
    case GameplayCameraCompatibilityProfile::
        stock_view_angles_evidence_pending:
        return "stock_view_angles_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(const GameplayCameraEvidenceProfile profile) noexcept
{
    switch (profile) {
    case GameplayCameraEvidenceProfile::
        project_owned_local_first_person_camera_v1:
        return "project_owned_local_first_person_camera_v1";
    case GameplayCameraEvidenceProfile::evidence_pending_m4_6_2:
        return "evidence_pending_m4_6_2";
    }
    return "unknown";
}

std::string_view to_string(const GameplayCameraMode mode) noexcept
{
    switch (mode) {
    case GameplayCameraMode::static_view: return "static";
    case GameplayCameraMode::orbit: return "orbit";
    case GameplayCameraMode::spawn: return "spawn";
    case GameplayCameraMode::free_flight: return "free_flight";
    case GameplayCameraMode::entity_first_person:
        return "entity_first_person";
    }
    return "unknown";
}

std::string_view to_string(const GameplayCameraAnchorStatus status) noexcept
{
    switch (status) {
    case GameplayCameraAnchorStatus::none: return "none";
    case GameplayCameraAnchorStatus::attached: return "attached";
    case GameplayCameraAnchorStatus::anchor_entity_missing:
        return "anchor_entity_missing";
    }
    return "unknown";
}

std::string_view to_string(
    const GameplayCameraAnchorEvidenceProfile profile) noexcept
{
    switch (profile) {
    case GameplayCameraAnchorEvidenceProfile::none: return "none";
    case GameplayCameraAnchorEvidenceProfile::
        explicit_synthetic_playback_v1:
        return "explicit_synthetic_playback_v1";
    case GameplayCameraAnchorEvidenceProfile::
        stock_player_eye_height_evidence_pending:
        return "stock_player_eye_height_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(const GameplayCameraErrorCode code) noexcept
{
    switch (code) {
    case GameplayCameraErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GameplayCameraErrorCode::unsupported_compatibility_profile:
        return "unsupported_compatibility_profile";
    case GameplayCameraErrorCode::invalid_input: return "invalid_input";
    case GameplayCameraErrorCode::invalid_duration: return "invalid_duration";
    case GameplayCameraErrorCode::non_finite_camera:
        return "non_finite_camera";
    case GameplayCameraErrorCode::position_limit_exceeded:
        return "position_limit_exceeded";
    case GameplayCameraErrorCode::revision_limit_exceeded:
        return "revision_limit_exceeded";
    case GameplayCameraErrorCode::anchor_missing: return "anchor_missing";
    case GameplayCameraErrorCode::camera_validation_failed:
        return "camera_validation_failed";
    }
    return "unknown";
}

GameplayCameraState::GameplayCameraState(
    const GameplayCameraStateCreateInfo& create_info) noexcept
    : position_(create_info.position),
      yaw_degrees_(create_info.yaw_degrees),
      pitch_degrees_(create_info.pitch_degrees),
      vertical_fov_radians_(create_info.vertical_fov_radians),
      near_plane_(create_info.near_plane),
      far_plane_(create_info.far_plane),
      mode_(create_info.mode),
      anchor_metadata_(create_info.anchor_metadata),
      revision_(create_info.revision),
      compatibility_profile_(create_info.compatibility_profile),
      evidence_profile_(create_info.evidence_profile)
{
}

GameplayCameraState::CreationResult GameplayCameraState::create(
    const GameplayCameraStateCreateInfo& create_info) noexcept
{
    GameplayCameraError error;
    if (!valid_state_create_info(create_info, error)) {
        return {std::nullopt, error};
    }
    const auto normalized_yaw = normalize_yaw_degrees(create_info.yaw_degrees);
    if (!normalized_yaw) {
        return {std::nullopt,
            GameplayCameraError{GameplayCameraErrorCode::non_finite_camera,
                "camera yaw could not be normalized"}};
    }
    auto normalized = create_info;
    normalized.yaw_degrees = *normalized_yaw;
    GameplayCameraState candidate{normalized};
    if (!forward_from_yaw_pitch(
            candidate.yaw_degrees(), candidate.pitch_degrees())) {
        return {std::nullopt,
            GameplayCameraError{
                GameplayCameraErrorCode::camera_validation_failed,
                "camera state does not produce a finite camera basis"}};
    }
    return {std::optional<GameplayCameraState>{std::move(candidate)},
        std::nullopt};
}

const assets::AssetVector3& GameplayCameraState::position() const noexcept
{
    return position_;
}

double GameplayCameraState::yaw_degrees() const noexcept
{
    return yaw_degrees_;
}

double GameplayCameraState::pitch_degrees() const noexcept
{
    return pitch_degrees_;
}

double GameplayCameraState::vertical_fov_radians() const noexcept
{
    return vertical_fov_radians_;
}

double GameplayCameraState::near_plane() const noexcept
{
    return near_plane_;
}

double GameplayCameraState::far_plane() const noexcept
{
    return far_plane_;
}

GameplayCameraMode GameplayCameraState::mode() const noexcept
{
    return mode_;
}

const GameplayCameraAnchorMetadata& GameplayCameraState::anchor_metadata()
    const noexcept
{
    return anchor_metadata_;
}

std::uint64_t GameplayCameraState::revision() const noexcept
{
    return revision_;
}

GameplayCameraCompatibilityProfile GameplayCameraState::compatibility_profile()
    const noexcept
{
    return compatibility_profile_;
}

GameplayCameraEvidenceProfile GameplayCameraState::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

FirstPersonCameraConfig::FirstPersonCameraConfig(
    const FirstPersonCameraConfigCreateInfo& create_info) noexcept
    : mouse_look_config_(create_info.mouse_look_config),
      minimum_pitch_degrees_(create_info.minimum_pitch_degrees),
      maximum_pitch_degrees_(create_info.maximum_pitch_degrees),
      base_move_speed_(create_info.base_move_speed),
      speed_multiplier_(create_info.speed_multiplier),
      maximum_frame_duration_seconds_(
          create_info.maximum_frame_duration_seconds),
      vertical_fov_radians_(create_info.vertical_fov_radians),
      near_plane_(create_info.near_plane),
      far_plane_(create_info.far_plane),
      maximum_position_magnitude_(create_info.maximum_position_magnitude),
      maximum_camera_revisions_(create_info.maximum_camera_revisions)
{
}

FirstPersonCameraConfig::CreationResult FirstPersonCameraConfig::create(
    const FirstPersonCameraConfigCreateInfo& create_info) noexcept
{
    if (!valid_config_create_info(create_info)) {
        return {std::nullopt,
            GameplayCameraError{GameplayCameraErrorCode::invalid_configuration,
                "first-person camera configuration violates a safety bound"}};
    }
    return {std::optional<FirstPersonCameraConfig>{
                FirstPersonCameraConfig{create_info}},
        std::nullopt};
}

FirstPersonCameraConfig FirstPersonCameraConfig::project_default_v1() noexcept
{
    return FirstPersonCameraConfig{FirstPersonCameraConfigCreateInfo{}};
}

const gameplay_input::MouseLookConfig&
FirstPersonCameraConfig::mouse_look_config() const noexcept
{
    return mouse_look_config_;
}

double FirstPersonCameraConfig::minimum_pitch_degrees() const noexcept
{
    return minimum_pitch_degrees_;
}

double FirstPersonCameraConfig::maximum_pitch_degrees() const noexcept
{
    return maximum_pitch_degrees_;
}

double FirstPersonCameraConfig::base_move_speed() const noexcept
{
    return base_move_speed_;
}

double FirstPersonCameraConfig::speed_multiplier() const noexcept
{
    return speed_multiplier_;
}

double FirstPersonCameraConfig::maximum_frame_duration_seconds() const noexcept
{
    return maximum_frame_duration_seconds_;
}

double FirstPersonCameraConfig::vertical_fov_radians() const noexcept
{
    return vertical_fov_radians_;
}

double FirstPersonCameraConfig::near_plane() const noexcept
{
    return near_plane_;
}

double FirstPersonCameraConfig::far_plane() const noexcept
{
    return far_plane_;
}

double FirstPersonCameraConfig::maximum_position_magnitude() const noexcept
{
    return maximum_position_magnitude_;
}

std::uint64_t FirstPersonCameraConfig::maximum_camera_revisions() const noexcept
{
    return maximum_camera_revisions_;
}

std::string_view to_string(const GameplayCameraUpdateStatus status) noexcept
{
    switch (status) {
    case GameplayCameraUpdateStatus::unchanged: return "unchanged";
    case GameplayCameraUpdateStatus::updated: return "updated";
    case GameplayCameraUpdateStatus::duration_clamped:
        return "duration_clamped";
    case GameplayCameraUpdateStatus::anchor_entity_missing:
        return "anchor_entity_missing";
    }
    return "unknown";
}

GameplayCameraUpdateResult& GameplayCameraUpdateResult::operator=(
    const GameplayCameraUpdateResult& other)
{
    if (this == &other) {
        return *this;
    }
    camera.reset();
    if (other.camera) {
        camera.emplace(*other.camera);
    }
    status = other.status;
    error = other.error;
    movement_distance = other.movement_distance;
    requested_duration_seconds = other.requested_duration_seconds;
    applied_duration_seconds = other.applied_duration_seconds;
    revision_changed = other.revision_changed;
    focused = other.focused;
    captured = other.captured;
    return *this;
}

GameplayCameraUpdateResult& GameplayCameraUpdateResult::operator=(
    GameplayCameraUpdateResult&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    camera.reset();
    if (other.camera) {
        camera.emplace(std::move(*other.camera));
    }
    status = other.status;
    error = other.error;
    movement_distance = other.movement_distance;
    requested_duration_seconds = other.requested_duration_seconds;
    applied_duration_seconds = other.applied_duration_seconds;
    revision_changed = other.revision_changed;
    focused = other.focused;
    captured = other.captured;
    return *this;
}

std::optional<double> normalize_yaw_degrees(const double yaw_degrees) noexcept
{
    if (!std::isfinite(yaw_degrees)) {
        return std::nullopt;
    }
    auto normalized = std::fmod(yaw_degrees + 180.0, 360.0);
    if (!std::isfinite(normalized)) {
        return std::nullopt;
    }
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    normalized -= 180.0;
    if (normalized == 180.0) {
        normalized = -180.0;
    }
    // Avoid publishing a negative zero after exact wrap boundaries.
    if (normalized == 0.0) {
        normalized = 0.0;
    }
    return normalized;
}

std::optional<double> clamp_pitch_degrees(
    const double pitch_degrees,
    const double minimum_pitch_degrees,
    const double maximum_pitch_degrees) noexcept
{
    if (!std::isfinite(pitch_degrees) ||
        !std::isfinite(minimum_pitch_degrees) ||
        !std::isfinite(maximum_pitch_degrees) ||
        minimum_pitch_degrees < -kHardPitchLimit ||
        maximum_pitch_degrees > kHardPitchLimit ||
        minimum_pitch_degrees >= maximum_pitch_degrees) {
        return std::nullopt;
    }
    return std::clamp(
        pitch_degrees, minimum_pitch_degrees, maximum_pitch_degrees);
}

assets::AssetVector3 world_up() noexcept
{
    return {0.0F, 0.0F, 1.0F};
}

std::optional<assets::AssetVector3> forward_from_yaw_pitch(
    const double yaw_degrees,
    const double pitch_degrees) noexcept
{
    if (!std::isfinite(yaw_degrees) || !std::isfinite(pitch_degrees)) {
        return std::nullopt;
    }
    const auto yaw = yaw_degrees * kDegreesToRadians;
    const auto pitch = pitch_degrees * kDegreesToRadians;
    const auto cosine_pitch = std::cos(pitch);
    const assets::AssetVector3 result{
        static_cast<float>(cosine_pitch * std::cos(yaw)),
        static_cast<float>(cosine_pitch * std::sin(yaw)),
        static_cast<float>(std::sin(pitch)),
    };
    return finite(result) ? std::optional{result} : std::nullopt;
}

std::optional<assets::AssetVector3> horizontal_forward_from_yaw(
    const double yaw_degrees) noexcept
{
    if (!std::isfinite(yaw_degrees)) {
        return std::nullopt;
    }
    const auto yaw = yaw_degrees * kDegreesToRadians;
    const assets::AssetVector3 result{
        static_cast<float>(std::cos(yaw)),
        static_cast<float>(std::sin(yaw)),
        0.0F,
    };
    return finite(result) ? std::optional{result} : std::nullopt;
}

std::optional<assets::AssetVector3> right_from_yaw(
    const double yaw_degrees) noexcept
{
    if (!std::isfinite(yaw_degrees)) {
        return std::nullopt;
    }
    const auto yaw = yaw_degrees * kDegreesToRadians;
    // forward cross world_up: at yaw zero the local right direction is -Y.
    const assets::AssetVector3 result{
        static_cast<float>(std::sin(yaw)),
        static_cast<float>(-std::cos(yaw)),
        0.0F,
    };
    return finite(result) ? std::optional{result} : std::nullopt;
}

GameplayCameraUpdateResult LocalFreeFlightCameraController::update(
    const GameplayCameraState& previous,
    const gameplay_input::GameplayInputIntent& intent,
    const double elapsed_duration_seconds,
    const FirstPersonCameraConfig& config) const noexcept
{
    if (previous.mode() != GameplayCameraMode::free_flight ||
        previous.compatibility_profile() !=
            GameplayCameraCompatibilityProfile::local_first_person_z_up_v1 ||
        previous.evidence_profile() != GameplayCameraEvidenceProfile::
            project_owned_local_first_person_camera_v1) {
        return fail_update(GameplayCameraErrorCode::invalid_input,
            "free-flight controller received an incompatible camera state",
            intent,
            elapsed_duration_seconds);
    }
    if (!valid_intent(intent)) {
        return fail_update(GameplayCameraErrorCode::invalid_input,
            "free-flight controller received an invalid intent",
            intent,
            elapsed_duration_seconds);
    }
    if (!std::isfinite(elapsed_duration_seconds) ||
        elapsed_duration_seconds < 0.0) {
        return fail_update(GameplayCameraErrorCode::invalid_duration,
            "camera duration must be finite and non-negative",
            intent,
            elapsed_duration_seconds);
    }
    if (!within_position_limit(
            previous.position(), config.maximum_position_magnitude())) {
        return fail_update(GameplayCameraErrorCode::position_limit_exceeded,
            "camera position exceeds the configured safety limit",
            intent,
            elapsed_duration_seconds);
    }
    if (previous.revision() > config.maximum_camera_revisions()) {
        return fail_update(GameplayCameraErrorCode::revision_limit_exceeded,
            "camera revision exceeds the configured safety limit",
            intent,
            elapsed_duration_seconds);
    }

    const auto applied_duration = std::min(
        elapsed_duration_seconds, config.maximum_frame_duration_seconds());
    auto yaw = previous.yaw_degrees();
    auto pitch = previous.pitch_degrees();
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
            "camera look update produced a non-finite result",
            intent,
            elapsed_duration_seconds);
    }
    yaw = *normalized;
    pitch = *clamped;

    auto position = previous.position();
    double movement_distance = 0.0;
    if (intent.focused() && applied_duration > 0.0) {
        const auto horizontal_forward = horizontal_forward_from_yaw(yaw);
        const auto right = right_from_yaw(yaw);
        if (!horizontal_forward || !right) {
            return fail_update(GameplayCameraErrorCode::non_finite_camera,
                "camera movement basis could not be constructed",
                intent,
                elapsed_duration_seconds);
        }
        auto vertical_axis = static_cast<double>(intent.vertical_axis());
        if (gameplay_input::gameplay_button_is_set(
                intent.held_buttons(), gameplay_input::GameplayButton::jump)) {
            vertical_axis += 1.0;
        }
        if (gameplay_input::gameplay_button_is_set(
                intent.held_buttons(), gameplay_input::GameplayButton::duck)) {
            vertical_axis -= 1.0;
        }
        vertical_axis = std::clamp(vertical_axis, -1.0, 1.0);

        auto movement_x =
            static_cast<double>(horizontal_forward->x) * intent.forward_axis() +
            static_cast<double>(right->x) * intent.side_axis();
        auto movement_y =
            static_cast<double>(horizontal_forward->y) * intent.forward_axis() +
            static_cast<double>(right->y) * intent.side_axis();
        auto movement_z = vertical_axis;
        const auto movement_length_squared = movement_x * movement_x +
            movement_y * movement_y + movement_z * movement_z;
        if (!std::isfinite(movement_length_squared)) {
            return fail_update(GameplayCameraErrorCode::invalid_input,
                "camera movement vector is non-finite",
                intent,
                elapsed_duration_seconds);
        }
        if (movement_length_squared > 1.0) {
            const auto inverse_length = 1.0 / std::sqrt(movement_length_squared);
            movement_x *= inverse_length;
            movement_y *= inverse_length;
            movement_z *= inverse_length;
        }
        auto speed = config.base_move_speed();
        if (gameplay_input::gameplay_button_is_set(
                intent.held_buttons(), gameplay_input::GameplayButton::speed)) {
            speed *= config.speed_multiplier();
        }
        const auto displacement_scale = speed * applied_duration;
        const auto next_x = static_cast<double>(position.x) +
            movement_x * displacement_scale;
        const auto next_y = static_cast<double>(position.y) +
            movement_y * displacement_scale;
        const auto next_z = static_cast<double>(position.z) +
            movement_z * displacement_scale;
        if (!std::isfinite(next_x) || !std::isfinite(next_y) ||
            !std::isfinite(next_z) ||
            std::abs(next_x) > std::numeric_limits<float>::max() ||
            std::abs(next_y) > std::numeric_limits<float>::max() ||
            std::abs(next_z) > std::numeric_limits<float>::max()) {
            return fail_update(GameplayCameraErrorCode::non_finite_camera,
                "camera position update is not representable",
                intent,
                elapsed_duration_seconds);
        }
        const assets::AssetVector3 next_position{static_cast<float>(next_x),
            static_cast<float>(next_y),
            static_cast<float>(next_z)};
        if (!within_position_limit(
                next_position, config.maximum_position_magnitude())) {
            return fail_update(GameplayCameraErrorCode::position_limit_exceeded,
                "camera position exceeds the configured safety limit",
                intent,
                elapsed_duration_seconds);
        }
        position = next_position;
        movement_distance = vector_distance(position, previous.position());
    }

    const auto changed = !equal_vector(position, previous.position()) ||
        yaw != previous.yaw_degrees() || pitch != previous.pitch_degrees() ||
        config.vertical_fov_radians() != previous.vertical_fov_radians() ||
        config.near_plane() != previous.near_plane() ||
        config.far_plane() != previous.far_plane();
    auto revision = previous.revision();
    if (changed) {
        const auto incremented = next_revision(previous, config);
        if (!incremented) {
            return fail_update(
                GameplayCameraErrorCode::revision_limit_exceeded,
                "camera revision cannot be incremented",
                intent,
                elapsed_duration_seconds);
        }
        revision = *incremented;
    }

    GameplayCameraStateCreateInfo create_info;
    create_info.position = position;
    create_info.yaw_degrees = yaw;
    create_info.pitch_degrees = pitch;
    create_info.vertical_fov_radians = config.vertical_fov_radians();
    create_info.near_plane = config.near_plane();
    create_info.far_plane = config.far_plane();
    create_info.mode = GameplayCameraMode::free_flight;
    create_info.revision = revision;
    create_info.compatibility_profile = previous.compatibility_profile();
    create_info.evidence_profile = previous.evidence_profile();
    auto created = GameplayCameraState::create(create_info);
    if (!created) {
        return fail_update(GameplayCameraErrorCode::camera_validation_failed,
            "updated free-flight camera failed validation",
            intent,
            elapsed_duration_seconds);
    }

    GameplayCameraUpdateResult result;
    result.camera.emplace(std::move(*created.state));
    result.status = changed
        ? (elapsed_duration_seconds > applied_duration
                ? GameplayCameraUpdateStatus::duration_clamped
                : GameplayCameraUpdateStatus::updated)
        : (elapsed_duration_seconds > applied_duration
                ? GameplayCameraUpdateStatus::duration_clamped
                : GameplayCameraUpdateStatus::unchanged);
    result.movement_distance = movement_distance;
    result.requested_duration_seconds = elapsed_duration_seconds;
    result.applied_duration_seconds = applied_duration;
    result.revision_changed = changed;
    result.focused = intent.focused();
    result.captured = intent.captured();
    return result;
}

} // namespace hlclient::gameplay_camera

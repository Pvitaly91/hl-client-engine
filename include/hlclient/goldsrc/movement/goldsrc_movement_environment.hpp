#pragma once

#include <hlclient/goldsrc/move_vars.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::movement {

inline constexpr float kGoldSrcMovementEnvironmentHardMaximumMagnitude =
    kMaximumMoveVarsNumericMagnitude;

enum class GoldSrcMovementEnvironmentProfile : std::uint8_t {
    movevars_dry_walk_subset_v1,
    stock_pm_move_full_compatibility_evidence_pending,
};

enum class GoldSrcMovementEnvironmentEvidenceProfile : std::uint8_t {
    captured_movevars_and_public_valve_pm_shared,
    project_owned_offline_baseline_and_public_valve_pm_shared,
    stock_pm_move_full_compatibility_evidence_pending,
};

enum class GoldSrcMovementEnvironmentSourceProfile : std::uint8_t {
    captured_movevars_v1,
    project_owned_offline_baseline_v1,
};

struct GoldSrcDeferredMovementEnvironmentFields {
    float water_acceleration{0.0F};
    float water_friction{0.0F};
    float edge_friction{0.0F};
    float bounce{0.0F};
    float z_maximum{0.0F};
    float wave_height{0.0F};

    [[nodiscard]] friend bool operator==(
        const GoldSrcDeferredMovementEnvironmentFields&,
        const GoldSrcDeferredMovementEnvironmentFields&) = default;
};

struct GoldSrcMovementEnvironmentValues {
    float gravity{0.0F};
    float stop_speed{0.0F};
    float maximum_speed{0.0F};
    float acceleration{0.0F};
    float air_acceleration{0.0F};
    float friction{0.0F};
    float step_size{0.0F};
    float maximum_velocity{0.0F};
    float entity_gravity{0.0F};
    GoldSrcDeferredMovementEnvironmentFields deferred{};

    [[nodiscard]] friend bool operator==(
        const GoldSrcMovementEnvironmentValues&,
        const GoldSrcMovementEnvironmentValues&) = default;
};

struct GoldSrcMovementEnvironmentLimits {
    float maximum_numeric_magnitude{
        kGoldSrcMovementEnvironmentHardMaximumMagnitude};
};

[[nodiscard]] bool valid_goldsrc_movement_environment_limits(
    const GoldSrcMovementEnvironmentLimits& limits) noexcept;

enum class GoldSrcMovementEnvironmentErrorCode : std::uint8_t {
    invalid_limits,
    unsupported_profile,
    stock_evidence_pending,
    non_finite_value,
    magnitude_limit_exceeded,
    invalid_gravity,
    invalid_stop_speed,
    invalid_maximum_speed,
    invalid_acceleration,
    invalid_air_acceleration,
    invalid_friction,
    invalid_step_size,
    invalid_maximum_velocity,
    invalid_entity_gravity,
};

struct GoldSrcMovementEnvironmentError {
    GoldSrcMovementEnvironmentErrorCode code{
        GoldSrcMovementEnvironmentErrorCode::invalid_limits};
    std::string_view context;
};

[[nodiscard]] std::optional<GoldSrcMovementEnvironmentError>
validate_goldsrc_movement_environment_values(
    const GoldSrcMovementEnvironmentValues& values,
    const GoldSrcMovementEnvironmentLimits& limits = {}) noexcept;

class GoldSrcMovementEnvironmentBuilder;

class GoldSrcMovementEnvironment final {
public:
    GoldSrcMovementEnvironment(const GoldSrcMovementEnvironment&) = default;
    GoldSrcMovementEnvironment(GoldSrcMovementEnvironment&&) noexcept = default;
    GoldSrcMovementEnvironment& operator=(
        const GoldSrcMovementEnvironment&) = delete;
    GoldSrcMovementEnvironment& operator=(GoldSrcMovementEnvironment&&) = delete;
    ~GoldSrcMovementEnvironment() = default;

    [[nodiscard]] float gravity() const noexcept;
    [[nodiscard]] float stop_speed() const noexcept;
    [[nodiscard]] float maximum_speed() const noexcept;
    [[nodiscard]] float acceleration() const noexcept;
    [[nodiscard]] float air_acceleration() const noexcept;
    [[nodiscard]] float friction() const noexcept;
    [[nodiscard]] float step_size() const noexcept;
    [[nodiscard]] float maximum_velocity() const noexcept;
    [[nodiscard]] float entity_gravity() const noexcept;
    [[nodiscard]] const GoldSrcDeferredMovementEnvironmentFields& deferred()
        const noexcept;
    [[nodiscard]] const GoldSrcMovementEnvironmentValues& values() const noexcept;
    [[nodiscard]] GoldSrcMovementEnvironmentProfile profile() const noexcept;
    [[nodiscard]] GoldSrcMovementEnvironmentEvidenceProfile evidence_profile()
        const noexcept;
    [[nodiscard]] GoldSrcMovementEnvironmentSourceProfile source_profile()
        const noexcept;
    [[nodiscard]] std::optional<MoveVarsCompatibilityProfile>
    source_move_vars_profile() const noexcept;
    [[nodiscard]] std::optional<MoveVarsEvidenceProfile>
    source_move_vars_evidence_profile() const noexcept;

private:
    friend class GoldSrcMovementEnvironmentBuilder;

    GoldSrcMovementEnvironment(
        GoldSrcMovementEnvironmentValues values,
        GoldSrcMovementEnvironmentProfile profile,
        GoldSrcMovementEnvironmentEvidenceProfile evidence_profile,
        GoldSrcMovementEnvironmentSourceProfile source_profile,
        std::optional<MoveVarsCompatibilityProfile> source_move_vars_profile,
        std::optional<MoveVarsEvidenceProfile> source_move_vars_evidence_profile)
        noexcept;

    GoldSrcMovementEnvironmentValues values_{};
    GoldSrcMovementEnvironmentProfile profile_{
        GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1};
    GoldSrcMovementEnvironmentEvidenceProfile evidence_profile_{
        GoldSrcMovementEnvironmentEvidenceProfile::
            captured_movevars_and_public_valve_pm_shared};
    GoldSrcMovementEnvironmentSourceProfile source_profile_{
        GoldSrcMovementEnvironmentSourceProfile::captured_movevars_v1};
    std::optional<MoveVarsCompatibilityProfile> source_move_vars_profile_;
    std::optional<MoveVarsEvidenceProfile> source_move_vars_evidence_profile_;
};

struct GoldSrcMovementEnvironmentBuildResult {
    std::optional<GoldSrcMovementEnvironment> environment;
    std::optional<GoldSrcMovementEnvironmentError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return environment.has_value();
    }
};

class GoldSrcMovementEnvironmentBuilder final {
public:
    [[nodiscard]] static GoldSrcMovementEnvironmentBuildResult from_move_vars(
        const MoveVarsState& move_vars,
        GoldSrcMovementEnvironmentProfile profile =
            GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1,
        const GoldSrcMovementEnvironmentLimits& limits = {}) noexcept;

    [[nodiscard]] static GoldSrcMovementEnvironmentBuildResult
    project_owned_offline_baseline(
        const GoldSrcMovementEnvironmentLimits& limits = {}) noexcept;
};

[[nodiscard]] std::string_view to_string(
    GoldSrcMovementEnvironmentProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    GoldSrcMovementEnvironmentEvidenceProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    GoldSrcMovementEnvironmentSourceProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    GoldSrcMovementEnvironmentErrorCode code) noexcept;

} // namespace hlclient::goldsrc::movement

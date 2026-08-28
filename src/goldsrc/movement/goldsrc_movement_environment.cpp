#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>

#include <array>
#include <cmath>
#include <utility>

namespace hlclient::goldsrc::movement {
namespace {

[[nodiscard]] GoldSrcMovementEnvironmentBuildResult failure(
    const GoldSrcMovementEnvironmentErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, GoldSrcMovementEnvironmentError{code, context}};
}

[[nodiscard]] bool executable_profile(
    const GoldSrcMovementEnvironmentProfile profile) noexcept
{
    return profile ==
        GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1;
}

[[nodiscard]] GoldSrcMovementEnvironmentValues values_from_move_vars(
    const MoveVarsState& move_vars) noexcept
{
    return {
        move_vars.gravity(),
        move_vars.stop_speed(),
        move_vars.maximum_speed(),
        move_vars.acceleration(),
        move_vars.air_acceleration(),
        move_vars.friction(),
        move_vars.step_size(),
        move_vars.maximum_velocity(),
        move_vars.entity_gravity(),
        {
            move_vars.water_acceleration(),
            move_vars.water_friction(),
            move_vars.edge_friction(),
            move_vars.bounce(),
            move_vars.z_maximum(),
            move_vars.wave_height(),
        },
    };
}

[[nodiscard]] GoldSrcMovementEnvironmentValues offline_baseline_values() noexcept
{
    // Project-owned deterministic fixture values. They intentionally mirror the
    // accepted local MoveVars fixture, but are not advertised as server state.
    return {
        800.0F,
        100.0F,
        320.0F,
        10.0F,
        10.0F,
        4.0F,
        18.0F,
        2'000.0F,
        1.0F,
        {10.0F, 1.0F, 2.0F, 1.0F, 4'096.0F, 0.0F},
    };
}

} // namespace

bool valid_goldsrc_movement_environment_limits(
    const GoldSrcMovementEnvironmentLimits& limits) noexcept
{
    return std::isfinite(limits.maximum_numeric_magnitude) &&
        limits.maximum_numeric_magnitude > 0.0F &&
        limits.maximum_numeric_magnitude <=
            kGoldSrcMovementEnvironmentHardMaximumMagnitude;
}

std::optional<GoldSrcMovementEnvironmentError>
validate_goldsrc_movement_environment_values(
    const GoldSrcMovementEnvironmentValues& values,
    const GoldSrcMovementEnvironmentLimits& limits) noexcept
{
    if (!valid_goldsrc_movement_environment_limits(limits)) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_limits,
            "movement-environment limits are invalid"};
    }
    const std::array all_values{
        values.gravity,
        values.stop_speed,
        values.maximum_speed,
        values.acceleration,
        values.air_acceleration,
        values.friction,
        values.step_size,
        values.maximum_velocity,
        values.entity_gravity,
        values.deferred.water_acceleration,
        values.deferred.water_friction,
        values.deferred.edge_friction,
        values.deferred.bounce,
        values.deferred.z_maximum,
        values.deferred.wave_height,
    };
    for (const auto value : all_values) {
        if (!std::isfinite(value)) {
            return GoldSrcMovementEnvironmentError{
                GoldSrcMovementEnvironmentErrorCode::non_finite_value,
                "movement environment contains a non-finite value"};
        }
        if (std::abs(value) > limits.maximum_numeric_magnitude) {
            return GoldSrcMovementEnvironmentError{
                GoldSrcMovementEnvironmentErrorCode::magnitude_limit_exceeded,
                "movement environment value exceeds its safety magnitude"};
        }
    }

    if (values.gravity <= 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_gravity,
            "movement gravity must be positive"};
    }
    if (values.stop_speed < 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_stop_speed,
            "movement stop speed must be non-negative"};
    }
    if (values.maximum_speed <= 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_maximum_speed,
            "movement maximum speed must be positive"};
    }
    if (values.acceleration < 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_acceleration,
            "movement acceleration must be non-negative"};
    }
    if (values.air_acceleration < 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_air_acceleration,
            "movement air acceleration must be non-negative"};
    }
    if (values.friction < 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_friction,
            "movement friction must be non-negative"};
    }
    if (values.step_size < 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_step_size,
            "movement step size must be non-negative"};
    }
    if (values.maximum_velocity <= 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_maximum_velocity,
            "movement maximum velocity must be positive"};
    }
    if (values.entity_gravity <= 0.0F) {
        return GoldSrcMovementEnvironmentError{
            GoldSrcMovementEnvironmentErrorCode::invalid_entity_gravity,
            "movement entity gravity must be positive"};
    }
    return std::nullopt;
}

GoldSrcMovementEnvironment::GoldSrcMovementEnvironment(
    GoldSrcMovementEnvironmentValues values,
    const GoldSrcMovementEnvironmentProfile profile,
    const GoldSrcMovementEnvironmentEvidenceProfile evidence_profile,
    const GoldSrcMovementEnvironmentSourceProfile source_profile,
    const std::optional<MoveVarsCompatibilityProfile> source_move_vars_profile,
    const std::optional<MoveVarsEvidenceProfile> source_move_vars_evidence_profile)
    noexcept
    : values_{values},
      profile_{profile},
      evidence_profile_{evidence_profile},
      source_profile_{source_profile},
      source_move_vars_profile_{source_move_vars_profile},
      source_move_vars_evidence_profile_{source_move_vars_evidence_profile}
{
}

float GoldSrcMovementEnvironment::gravity() const noexcept
{
    return values_.gravity;
}
float GoldSrcMovementEnvironment::stop_speed() const noexcept
{
    return values_.stop_speed;
}
float GoldSrcMovementEnvironment::maximum_speed() const noexcept
{
    return values_.maximum_speed;
}
float GoldSrcMovementEnvironment::acceleration() const noexcept
{
    return values_.acceleration;
}
float GoldSrcMovementEnvironment::air_acceleration() const noexcept
{
    return values_.air_acceleration;
}
float GoldSrcMovementEnvironment::friction() const noexcept
{
    return values_.friction;
}
float GoldSrcMovementEnvironment::step_size() const noexcept
{
    return values_.step_size;
}
float GoldSrcMovementEnvironment::maximum_velocity() const noexcept
{
    return values_.maximum_velocity;
}
float GoldSrcMovementEnvironment::entity_gravity() const noexcept
{
    return values_.entity_gravity;
}
const GoldSrcDeferredMovementEnvironmentFields&
GoldSrcMovementEnvironment::deferred() const noexcept
{
    return values_.deferred;
}
const GoldSrcMovementEnvironmentValues& GoldSrcMovementEnvironment::values()
    const noexcept
{
    return values_;
}
GoldSrcMovementEnvironmentProfile GoldSrcMovementEnvironment::profile()
    const noexcept
{
    return profile_;
}
GoldSrcMovementEnvironmentEvidenceProfile
GoldSrcMovementEnvironment::evidence_profile() const noexcept
{
    return evidence_profile_;
}
GoldSrcMovementEnvironmentSourceProfile GoldSrcMovementEnvironment::source_profile()
    const noexcept
{
    return source_profile_;
}
std::optional<MoveVarsCompatibilityProfile>
GoldSrcMovementEnvironment::source_move_vars_profile() const noexcept
{
    return source_move_vars_profile_;
}
std::optional<MoveVarsEvidenceProfile>
GoldSrcMovementEnvironment::source_move_vars_evidence_profile() const noexcept
{
    return source_move_vars_evidence_profile_;
}

GoldSrcMovementEnvironmentBuildResult
GoldSrcMovementEnvironmentBuilder::from_move_vars(
    const MoveVarsState& move_vars,
    const GoldSrcMovementEnvironmentProfile profile,
    const GoldSrcMovementEnvironmentLimits& limits) noexcept
{
    if (!valid_goldsrc_movement_environment_limits(limits)) {
        return failure(GoldSrcMovementEnvironmentErrorCode::invalid_limits,
            "movement-environment limits are invalid");
    }
    if (profile == GoldSrcMovementEnvironmentProfile::
            stock_pm_move_full_compatibility_evidence_pending) {
        return failure(
            GoldSrcMovementEnvironmentErrorCode::stock_evidence_pending,
            "full stock PM_Move environment evidence is pending");
    }
    if (!executable_profile(profile)) {
        return failure(GoldSrcMovementEnvironmentErrorCode::unsupported_profile,
            "movement-environment profile is unsupported");
    }
    if (move_vars.compatibility_profile() !=
            MoveVarsCompatibilityProfile::stock_protocol_48_build_10210 ||
        move_vars.evidence_profile() !=
            MoveVarsEvidenceProfile::stock_capture_and_public_valve_header) {
        return failure(GoldSrcMovementEnvironmentErrorCode::unsupported_profile,
            "source MoveVars profile is unsupported");
    }

    const auto values = values_from_move_vars(move_vars);
    if (const auto error =
            validate_goldsrc_movement_environment_values(values, limits)) {
        return {std::nullopt, error};
    }
    GoldSrcMovementEnvironment environment{
        values,
        profile,
        GoldSrcMovementEnvironmentEvidenceProfile::
            captured_movevars_and_public_valve_pm_shared,
        GoldSrcMovementEnvironmentSourceProfile::captured_movevars_v1,
        move_vars.compatibility_profile(),
        move_vars.evidence_profile(),
    };
    return {std::move(environment), std::nullopt};
}

GoldSrcMovementEnvironmentBuildResult
GoldSrcMovementEnvironmentBuilder::project_owned_offline_baseline(
    const GoldSrcMovementEnvironmentLimits& limits) noexcept
{
    if (!valid_goldsrc_movement_environment_limits(limits)) {
        return failure(GoldSrcMovementEnvironmentErrorCode::invalid_limits,
            "movement-environment limits are invalid");
    }
    const auto values = offline_baseline_values();
    if (const auto error =
            validate_goldsrc_movement_environment_values(values, limits)) {
        return {std::nullopt, error};
    }
    GoldSrcMovementEnvironment environment{
        values,
        GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1,
        GoldSrcMovementEnvironmentEvidenceProfile::
            project_owned_offline_baseline_and_public_valve_pm_shared,
        GoldSrcMovementEnvironmentSourceProfile::
            project_owned_offline_baseline_v1,
        std::nullopt,
        std::nullopt,
    };
    return {std::move(environment), std::nullopt};
}

std::string_view to_string(const GoldSrcMovementEnvironmentProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1:
        return "movevars_dry_walk_subset_v1";
    case GoldSrcMovementEnvironmentProfile::
            stock_pm_move_full_compatibility_evidence_pending:
        return "stock_pm_move_full_compatibility_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(
    const GoldSrcMovementEnvironmentEvidenceProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcMovementEnvironmentEvidenceProfile::
            captured_movevars_and_public_valve_pm_shared:
        return "captured_movevars_and_public_valve_pm_shared";
    case GoldSrcMovementEnvironmentEvidenceProfile::
            project_owned_offline_baseline_and_public_valve_pm_shared:
        return "project_owned_offline_baseline_and_public_valve_pm_shared";
    case GoldSrcMovementEnvironmentEvidenceProfile::
            stock_pm_move_full_compatibility_evidence_pending:
        return "stock_pm_move_full_compatibility_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(
    const GoldSrcMovementEnvironmentSourceProfile profile) noexcept
{
    switch (profile) {
    case GoldSrcMovementEnvironmentSourceProfile::captured_movevars_v1:
        return "captured_movevars_v1";
    case GoldSrcMovementEnvironmentSourceProfile::
            project_owned_offline_baseline_v1:
        return "project_owned_offline_baseline_v1";
    }
    return "unknown";
}

std::string_view to_string(
    const GoldSrcMovementEnvironmentErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcMovementEnvironmentErrorCode::invalid_limits:
        return "invalid_limits";
    case GoldSrcMovementEnvironmentErrorCode::unsupported_profile:
        return "unsupported_profile";
    case GoldSrcMovementEnvironmentErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case GoldSrcMovementEnvironmentErrorCode::non_finite_value:
        return "non_finite_value";
    case GoldSrcMovementEnvironmentErrorCode::magnitude_limit_exceeded:
        return "magnitude_limit_exceeded";
    case GoldSrcMovementEnvironmentErrorCode::invalid_gravity:
        return "invalid_gravity";
    case GoldSrcMovementEnvironmentErrorCode::invalid_stop_speed:
        return "invalid_stop_speed";
    case GoldSrcMovementEnvironmentErrorCode::invalid_maximum_speed:
        return "invalid_maximum_speed";
    case GoldSrcMovementEnvironmentErrorCode::invalid_acceleration:
        return "invalid_acceleration";
    case GoldSrcMovementEnvironmentErrorCode::invalid_air_acceleration:
        return "invalid_air_acceleration";
    case GoldSrcMovementEnvironmentErrorCode::invalid_friction:
        return "invalid_friction";
    case GoldSrcMovementEnvironmentErrorCode::invalid_step_size:
        return "invalid_step_size";
    case GoldSrcMovementEnvironmentErrorCode::invalid_maximum_velocity:
        return "invalid_maximum_velocity";
    case GoldSrcMovementEnvironmentErrorCode::invalid_entity_gravity:
        return "invalid_entity_gravity";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::movement

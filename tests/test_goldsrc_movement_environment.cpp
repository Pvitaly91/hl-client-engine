#include "move_vars_test_fixture.hpp"

#include <hlclient/goldsrc/movement/goldsrc_movement_environment.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

namespace fixture = hlclient::test::move_vars_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace movement = hlclient::goldsrc::movement;

[[nodiscard]] goldsrc::MoveVarsState parse_values(
    const fixture::Values& values = {})
{
    auto parsed = goldsrc::MoveVarsParser{}.parse(
        fixture::move_vars_message(values), 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.state);
    return std::move(*parsed.state);
}

TEST_CASE("Movement environment maps executable and deferred MoveVars exactly",
          "[goldsrc][movement][environment]")
{
    const auto move_vars = parse_values();
    const auto built =
        movement::GoldSrcMovementEnvironmentBuilder::from_move_vars(move_vars);
    REQUIRE(built);
    REQUIRE(built.environment);
    REQUIRE_FALSE(built.error);
    const auto& environment = *built.environment;

    CHECK(environment.gravity() == 800.0F);
    CHECK(environment.stop_speed() == 100.0F);
    CHECK(environment.maximum_speed() == 320.0F);
    CHECK(environment.acceleration() == 10.0F);
    CHECK(environment.air_acceleration() == 10.0F);
    CHECK(environment.friction() == 4.0F);
    CHECK(environment.step_size() == 18.0F);
    CHECK(environment.maximum_velocity() == 2'000.0F);
    CHECK(environment.entity_gravity() == 1.0F);
    CHECK(environment.deferred() ==
        movement::GoldSrcDeferredMovementEnvironmentFields{
            10.0F, 1.0F, 2.0F, 1.0F, 4'096.0F, 0.0F});

    CHECK(environment.profile() ==
        movement::GoldSrcMovementEnvironmentProfile::
            movevars_dry_walk_subset_v1);
    CHECK(environment.evidence_profile() ==
        movement::GoldSrcMovementEnvironmentEvidenceProfile::
            captured_movevars_and_public_valve_pm_shared);
    CHECK(environment.source_profile() ==
        movement::GoldSrcMovementEnvironmentSourceProfile::
            captured_movevars_v1);
    REQUIRE(environment.source_move_vars_profile());
    CHECK(*environment.source_move_vars_profile() ==
        goldsrc::MoveVarsCompatibilityProfile::stock_protocol_48_build_10210);
    REQUIRE(environment.source_move_vars_evidence_profile());
    CHECK(*environment.source_move_vars_evidence_profile() ==
        goldsrc::MoveVarsEvidenceProfile::stock_capture_and_public_valve_header);

    static_assert(std::is_copy_constructible_v<
        movement::GoldSrcMovementEnvironment>);
    static_assert(std::is_move_constructible_v<
        movement::GoldSrcMovementEnvironment>);
    static_assert(!std::is_copy_assignable_v<
        movement::GoldSrcMovementEnvironment>);
    static_assert(!std::is_move_assignable_v<
        movement::GoldSrcMovementEnvironment>);
}

TEST_CASE("Project-owned offline movement baseline is explicit and provenance-safe",
          "[goldsrc][movement][environment][offline]")
{
    const auto built = movement::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    REQUIRE(built);
    REQUIRE(built.environment);
    const auto& environment = *built.environment;

    CHECK(environment.gravity() == 800.0F);
    CHECK(environment.stop_speed() == 100.0F);
    CHECK(environment.maximum_speed() == 320.0F);
    CHECK(environment.acceleration() == 10.0F);
    CHECK(environment.air_acceleration() == 10.0F);
    CHECK(environment.friction() == 4.0F);
    CHECK(environment.step_size() == 18.0F);
    CHECK(environment.maximum_velocity() == 2'000.0F);
    CHECK(environment.entity_gravity() == 1.0F);
    CHECK(environment.source_profile() ==
        movement::GoldSrcMovementEnvironmentSourceProfile::
            project_owned_offline_baseline_v1);
    CHECK(environment.evidence_profile() ==
        movement::GoldSrcMovementEnvironmentEvidenceProfile::
            project_owned_offline_baseline_and_public_valve_pm_shared);
    CHECK_FALSE(environment.source_move_vars_profile());
    CHECK_FALSE(environment.source_move_vars_evidence_profile());
}

TEST_CASE("Movement environment validation rejects every invalid executable field",
          "[goldsrc][movement][environment][validation]")
{
    struct InvalidField {
        float fixture::Values::*member;
        float value;
        movement::GoldSrcMovementEnvironmentErrorCode expected;
    };
    constexpr std::array invalid_fields{
        InvalidField{&fixture::Values::gravity, 0.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_gravity},
        InvalidField{&fixture::Values::stop_speed, -1.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_stop_speed},
        InvalidField{&fixture::Values::maximum_speed, 0.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_maximum_speed},
        InvalidField{&fixture::Values::acceleration, -1.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_acceleration},
        InvalidField{&fixture::Values::air_acceleration, -1.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_air_acceleration},
        InvalidField{&fixture::Values::friction, -1.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_friction},
        InvalidField{&fixture::Values::step_size, -1.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_step_size},
        InvalidField{&fixture::Values::maximum_velocity, 0.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_maximum_velocity},
        InvalidField{&fixture::Values::entity_gravity, 0.0F,
            movement::GoldSrcMovementEnvironmentErrorCode::invalid_entity_gravity},
    };

    for (const auto& test : invalid_fields) {
        auto values = fixture::Values{};
        values.*(test.member) = test.value;
        const auto move_vars = parse_values(values);
        const auto built =
            movement::GoldSrcMovementEnvironmentBuilder::from_move_vars(move_vars);
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code == test.expected);
        CHECK_FALSE(built.environment);
        CHECK_FALSE(built.error->context.empty());
    }
}

TEST_CASE("Movement environment validation is finite bounded and fail-closed",
          "[goldsrc][movement][environment][security]")
{
    const auto baseline = movement::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    REQUIRE(baseline);
    auto values = baseline.environment->values();

    values.deferred.wave_height =
        std::numeric_limits<float>::quiet_NaN();
    auto error = movement::validate_goldsrc_movement_environment_values(values);
    REQUIRE(error);
    CHECK(error->code ==
        movement::GoldSrcMovementEnvironmentErrorCode::non_finite_value);

    values = baseline.environment->values();
    values.deferred.z_maximum =
        movement::kGoldSrcMovementEnvironmentHardMaximumMagnitude + 1.0F;
    error = movement::validate_goldsrc_movement_environment_values(values);
    REQUIRE(error);
    CHECK(error->code ==
        movement::GoldSrcMovementEnvironmentErrorCode::magnitude_limit_exceeded);

    CHECK_FALSE(movement::valid_goldsrc_movement_environment_limits({0.0F}));
    CHECK_FALSE(movement::valid_goldsrc_movement_environment_limits({
        movement::kGoldSrcMovementEnvironmentHardMaximumMagnitude + 1.0F}));
    CHECK_FALSE(movement::valid_goldsrc_movement_environment_limits({
        std::numeric_limits<float>::infinity()}));

    const auto limited = movement::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline({500.0F});
    REQUIRE_FALSE(limited);
    REQUIRE(limited.error);
    CHECK(limited.error->code ==
        movement::GoldSrcMovementEnvironmentErrorCode::magnitude_limit_exceeded);
}

TEST_CASE("Pending and unknown movement environment profiles cannot execute",
          "[goldsrc][movement][environment][profile]")
{
    const auto move_vars = parse_values();
    const auto pending =
        movement::GoldSrcMovementEnvironmentBuilder::from_move_vars(
            move_vars,
            movement::GoldSrcMovementEnvironmentProfile::
                stock_pm_move_full_compatibility_evidence_pending);
    REQUIRE_FALSE(pending);
    REQUIRE(pending.error);
    CHECK(pending.error->code ==
        movement::GoldSrcMovementEnvironmentErrorCode::stock_evidence_pending);

    const auto unknown =
        movement::GoldSrcMovementEnvironmentBuilder::from_move_vars(
            move_vars,
            static_cast<movement::GoldSrcMovementEnvironmentProfile>(255U));
    REQUIRE_FALSE(unknown);
    REQUIRE(unknown.error);
    CHECK(unknown.error->code ==
        movement::GoldSrcMovementEnvironmentErrorCode::unsupported_profile);
}

} // namespace

#include "local_movement_test_fixture.hpp"

#include <hlclient/prediction/prediction_state_comparison.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

namespace movement = hlclient::movement;
namespace prediction = hlclient::prediction;
namespace fixture = hlclient::tests::local_movement;

[[nodiscard]] movement::LocalPlayerMovementState make_state(
    const movement::LocalPlayerMovementStateCreateInfo& info)
{
    const auto created = movement::LocalPlayerMovementState::create(info);
    if (!created.state) {
        std::terminate();
    }
    return *created.state;
}

void check_invalid_config(
    const prediction::PredictionStateComparisonConfig& config)
{
    CHECK_FALSE(prediction::valid_prediction_state_comparison_config(config));
    const auto state = fixture::make_state();
    const auto result =
        prediction::compare_prediction_states(state, state, config);
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.metrics);
    REQUIRE(result.error);
    CHECK(result.error->code ==
        prediction::PredictionErrorCode::invalid_configuration);
    CHECK_FALSE(result.error->context.empty());
}

} // namespace

TEST_CASE("Prediction state comparison reports an exact identical state",
    "[prediction][state-comparison][exact]")
{
    const auto predicted = fixture::make_state(
        {10.0F, -4.0F, 36.0F}, {3.0F, 4.0F, 0.0F},
        movement::PlayerMovementMode::walking,
        movement::PlayerMovementHull::standing, 5U, 0x0002U, 1.0F, 1.0F,
        50'000'000U, 9U);
    const auto authoritative = predicted;

    const auto compared =
        prediction::compare_prediction_states(predicted, authoritative);

    REQUIRE(compared);
    REQUIRE(compared.metrics);
    CHECK_FALSE(compared.error);
    const auto& metrics = *compared.metrics;
    CHECK(metrics.position_delta.x == 0.0F);
    CHECK(metrics.position_delta.y == 0.0F);
    CHECK(metrics.position_delta.z == 0.0F);
    CHECK(metrics.position_error_magnitude == 0.0);
    CHECK(metrics.horizontal_position_error == 0.0);
    CHECK(metrics.vertical_position_error == 0.0);
    CHECK(metrics.velocity_error_magnitude == 0.0);
    CHECK(metrics.shortest_path_angle_deltas.x == 0.0F);
    CHECK(metrics.shortest_path_angle_deltas.y == 0.0F);
    CHECK(metrics.shortest_path_angle_deltas.z == 0.0F);
    CHECK_FALSE(metrics.hull_mismatch);
    CHECK_FALSE(metrics.mode_mismatch);
    CHECK_FALSE(metrics.grounded_mismatch);
    CHECK_FALSE(metrics.contents_mismatch);
    CHECK_FALSE(metrics.old_buttons_mismatch);
    CHECK_FALSE(metrics.simulation_time_mismatch);
    CHECK(metrics.exact_physical_state_match);
    CHECK(metrics.exact_state_signature_match);
}

TEST_CASE("Prediction state revision is publication metadata, not physical state",
    "[prediction][state-comparison][revision]")
{
    const auto predicted = fixture::make_state();
    auto authoritative_info =
        movement::local_player_movement_state_create_info(predicted);
    ++authoritative_info.state_revision;
    const auto authoritative = make_state(authoritative_info);

    const auto compared =
        prediction::compare_prediction_states(predicted, authoritative);

    REQUIRE(compared.metrics);
    CHECK(compared.metrics->exact_physical_state_match);
    CHECK_FALSE(compared.metrics->exact_state_signature_match);
    CHECK(compared.metrics->position_error_magnitude == 0.0);
    CHECK(compared.metrics->velocity_error_magnitude == 0.0);
}

TEST_CASE("Prediction state comparison exposes signed position and velocity deltas",
    "[prediction][state-comparison][error-metrics]")
{
    const auto predicted = fixture::make_state(
        {1.0F, 2.0F, 3.0F}, {4.0F, 5.0F, 6.0F},
        movement::PlayerMovementMode::airborne);
    auto authoritative_info =
        movement::local_player_movement_state_create_info(predicted);
    authoritative_info.origin = {4.0F, -2.0F, 15.0F};
    authoritative_info.velocity = {-4.0F, 20.0F, 6.0F};
    const auto authoritative = make_state(authoritative_info);

    const auto compared =
        prediction::compare_prediction_states(predicted, authoritative);

    REQUIRE(compared.metrics);
    const auto& metrics = *compared.metrics;
    CHECK(metrics.position_delta.x == 3.0F);
    CHECK(metrics.position_delta.y == -4.0F);
    CHECK(metrics.position_delta.z == 12.0F);
    CHECK(metrics.position_error_magnitude == Catch::Approx(13.0));
    CHECK(metrics.horizontal_position_error == Catch::Approx(5.0));
    CHECK(metrics.vertical_position_error == Catch::Approx(12.0));
    CHECK(metrics.velocity_delta.x == -8.0F);
    CHECK(metrics.velocity_delta.y == 15.0F);
    CHECK(metrics.velocity_delta.z == 0.0F);
    CHECK(metrics.velocity_error_magnitude == Catch::Approx(17.0));
    CHECK_FALSE(metrics.exact_physical_state_match);
    CHECK_FALSE(metrics.exact_state_signature_match);
}

TEST_CASE("Prediction state comparison uses shortest wrapped angle deltas",
    "[prediction][state-comparison][angles]")
{
    const auto predicted_base = fixture::make_state();
    auto predicted_info =
        movement::local_player_movement_state_create_info(predicted_base);
    predicted_info.view_angles = {179.0F, -179.0F, 350.0F};
    auto authoritative_info = predicted_info;
    authoritative_info.view_angles = {-179.0F, 179.0F, 10.0F};
    const auto predicted = make_state(predicted_info);
    const auto authoritative = make_state(authoritative_info);

    const auto compared =
        prediction::compare_prediction_states(predicted, authoritative);

    REQUIRE(compared.metrics);
    CHECK(compared.metrics->shortest_path_angle_deltas.x ==
        Catch::Approx(2.0F));
    CHECK(compared.metrics->shortest_path_angle_deltas.y ==
        Catch::Approx(-2.0F));
    CHECK(compared.metrics->shortest_path_angle_deltas.z ==
        Catch::Approx(20.0F));
    CHECK_FALSE(compared.metrics->exact_physical_state_match);
}

TEST_CASE("Prediction state comparison identifies discrete simulation mismatches",
    "[prediction][state-comparison][discrete-metrics]")
{
    const auto predicted = fixture::make_state();
    auto authoritative_info =
        movement::local_player_movement_state_create_info(predicted);
    authoritative_info.hull = movement::PlayerMovementHull::ducked;
    authoritative_info.mode = movement::PlayerMovementMode::airborne;
    authoritative_info.ground = {};
    authoritative_info.view_offset = {0.0F, 0.0F, 12.0F};
    authoritative_info.last_valid_contents =
        movement::PlayerMovementContents::water;
    authoritative_info.old_buttons = 0x0001U;
    authoritative_info.simulation_time_nanoseconds = 1U;
    const auto authoritative = make_state(authoritative_info);

    const auto compared =
        prediction::compare_prediction_states(predicted, authoritative);

    REQUIRE(compared.metrics);
    CHECK(compared.metrics->hull_mismatch);
    CHECK(compared.metrics->mode_mismatch);
    CHECK(compared.metrics->grounded_mismatch);
    CHECK(compared.metrics->contents_mismatch);
    CHECK(compared.metrics->old_buttons_mismatch);
    CHECK(compared.metrics->simulation_time_mismatch);
    CHECK_FALSE(compared.metrics->exact_physical_state_match);
    CHECK_FALSE(compared.metrics->exact_state_signature_match);
}

TEST_CASE("Prediction state comparison validates every bounded threshold",
    "[prediction][state-comparison][configuration]")
{
    prediction::PredictionStateComparisonConfig defaults;
    REQUIRE(prediction::valid_prediction_state_comparison_config(defaults));

    SECTION("negative scalar")
    {
        auto config = defaults;
        config.velocity_exact_epsilon = -1.0;
        check_invalid_config(config);
    }
    SECTION("non-finite scalar")
    {
        auto config = defaults;
        config.maximum_acceptable_authoritative_position_error =
            (std::numeric_limits<double>::quiet_NaN)();
        check_invalid_config(config);
    }
    SECTION("visual threshold exceeds small correction")
    {
        auto config = defaults;
        config.visual_no_offset_epsilon =
            config.small_correction_maximum + 1.0;
        check_invalid_config(config);
    }
    SECTION("small correction exceeds snap threshold")
    {
        auto config = defaults;
        config.small_correction_maximum =
            config.large_correction_snap_threshold + 1.0;
        check_invalid_config(config);
    }
}

TEST_CASE("Prediction comparison cannot receive non-finite normalized states",
    "[prediction][state-comparison][input-normalization]")
{
    auto info = movement::local_player_movement_state_create_info(
        fixture::make_state());
    info.velocity.z = (std::numeric_limits<float>::quiet_NaN)();
    const auto rejected = movement::LocalPlayerMovementState::create(info);
    CHECK_FALSE(rejected.state);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        movement::LocalPlayerMovementStateErrorCode::non_finite_velocity);
}

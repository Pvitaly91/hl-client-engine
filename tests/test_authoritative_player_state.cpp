#include "local_movement_test_fixture.hpp"

#include <hlclient/prediction/authoritative_player_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <utility>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace movement = hlclient::movement;
namespace prediction = hlclient::prediction;
namespace fixture = hlclient::tests::local_movement;

[[nodiscard]] goldsrc::GoldSrcUserCmdSequence sequence(
    const std::uint32_t value)
{
    const auto created = goldsrc::GoldSrcUserCmdSequence::create(value);
    if (!created) {
        std::terminate();
    }
    return *created;
}

[[nodiscard]] prediction::PredictionSessionIdentity valid_session()
{
    const auto spawn = fixture::make_state();
    prediction::PredictionSessionIdentity session;
    session.session_generation = 3U;
    session.prediction_generation = 5U;
    session.collision_world_primary = 0x101U;
    session.collision_world_secondary = 0x202U;
    session.collision_world_revision = 7U;
    session.collision_scene_signature = 0x303U;
    session.movement_environment_signature = 0x404U;
    session.movement_config_signature = 0x505U;
    session.spawn_initial_state_signature =
        movement::local_player_movement_state_signature(spawn);
    return session;
}

[[nodiscard]] prediction::AuthoritativePlayerUpdateIdentityCreateInfo
valid_update_info(
    const std::uint32_t acknowledged_sequence = 7U,
    const prediction::AuthoritativePlayerDiscontinuity discontinuity =
        prediction::AuthoritativePlayerDiscontinuity::normal)
{
    prediction::AuthoritativePlayerUpdateIdentityCreateInfo info;
    info.session = valid_session();
    info.update_ordinal = 11U;
    info.acknowledgement =
        prediction::AuthoritativeCommandAcknowledgement::for_sequence(
            sequence(acknowledged_sequence));
    info.synthetic_authority_time_nanoseconds = 123'000'000;
    info.discontinuity = discontinuity;
    return info;
}

void check_identity_error(
    const prediction::AuthoritativePlayerUpdateIdentity::CreationResult& result,
    const prediction::PredictionErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.identity);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
}

void check_state_error(
    const prediction::AuthoritativePlayerState::CreationResult& result,
    const prediction::PredictionErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
}

} // namespace

TEST_CASE("Authoritative command acknowledgement represents exact or no command",
    "[prediction][authoritative-state][acknowledgement]")
{
    const auto none = prediction::AuthoritativeCommandAcknowledgement::none();
    CHECK_FALSE(none.has_sequence());
    CHECK_FALSE(none.sequence());

    const auto exact =
        prediction::AuthoritativeCommandAcknowledgement::for_sequence(
            sequence(19U));
    REQUIRE(exact.has_sequence());
    REQUIRE(exact.sequence());
    CHECK(exact.sequence()->value() == 19U);

    CHECK_FALSE(goldsrc::GoldSrcUserCmdSequence::create(0U));
    const auto invalid =
        prediction::AuthoritativeCommandAcknowledgement::for_sequence(
            goldsrc::GoldSrcUserCmdSequence{});
    CHECK(invalid == none);
}

TEST_CASE("Authoritative update identity retains exact synthetic authority facts",
    "[prediction][authoritative-state][identity]")
{
    const auto info = valid_update_info(
        7U, prediction::AuthoritativePlayerDiscontinuity::teleport);
    const auto created = prediction::AuthoritativePlayerUpdateIdentity::create(
        info);

    REQUIRE(created);
    REQUIRE(created.identity);
    CHECK_FALSE(created.error);
    CHECK(created.identity->session() == info.session);
    CHECK(created.identity->update_ordinal() == info.update_ordinal);
    CHECK(created.identity->acknowledgement() == info.acknowledgement);
    CHECK(created.identity->synthetic_authority_time_nanoseconds() ==
        info.synthetic_authority_time_nanoseconds);
    CHECK(created.identity->discontinuity() ==
        prediction::AuthoritativePlayerDiscontinuity::teleport);
}

TEST_CASE("Authoritative update identity enforces acknowledgement boundaries",
    "[prediction][authoritative-state][identity-validation]")
{
    SECTION("normal updates require an acknowledgement")
    {
        auto info = valid_update_info();
        info.acknowledgement =
            prediction::AuthoritativeCommandAcknowledgement::none();
        check_identity_error(
            prediction::AuthoritativePlayerUpdateIdentity::create(info),
            prediction::PredictionErrorCode::
                authoritative_acknowledgement_missing);
    }
    SECTION("teleports require an acknowledgement")
    {
        auto info = valid_update_info(
            7U, prediction::AuthoritativePlayerDiscontinuity::teleport);
        info.acknowledgement =
            prediction::AuthoritativeCommandAcknowledgement::none();
        check_identity_error(
            prediction::AuthoritativePlayerUpdateIdentity::create(info),
            prediction::PredictionErrorCode::
                authoritative_acknowledgement_missing);
    }
    SECTION("hard resets require the no-command anchor")
    {
        auto info = valid_update_info(
            7U,
            prediction::AuthoritativePlayerDiscontinuity::
                respawn_or_hard_reset);
        check_identity_error(
            prediction::AuthoritativePlayerUpdateIdentity::create(info),
            prediction::PredictionErrorCode::invalid_authoritative_state);
    }
    SECTION("authority ordinals are one based")
    {
        auto info = valid_update_info();
        info.update_ordinal = 0U;
        check_identity_error(
            prediction::AuthoritativePlayerUpdateIdentity::create(info),
            prediction::PredictionErrorCode::invalid_authority_update_ordinal);
    }
    SECTION("synthetic authority time is non-negative")
    {
        auto info = valid_update_info();
        info.synthetic_authority_time_nanoseconds = -1;
        check_identity_error(
            prediction::AuthoritativePlayerUpdateIdentity::create(info),
            prediction::PredictionErrorCode::invalid_authoritative_state);
    }
    SECTION("discontinuity enum must be recognized")
    {
        auto info = valid_update_info();
        info.discontinuity =
            static_cast<prediction::AuthoritativePlayerDiscontinuity>(255U);
        check_identity_error(
            prediction::AuthoritativePlayerUpdateIdentity::create(info),
            prediction::PredictionErrorCode::invalid_authoritative_state);
    }
    SECTION("session identity must be complete")
    {
        auto info = valid_update_info();
        info.session.collision_scene_signature = 0U;
        check_identity_error(
            prediction::AuthoritativePlayerUpdateIdentity::create(info),
            prediction::PredictionErrorCode::invalid_session_identity);
    }
}

TEST_CASE("Synthetic authoritative state retains a complete acknowledged state",
    "[prediction][authoritative-state][complete-state]")
{
    auto identity_result =
        prediction::AuthoritativePlayerUpdateIdentity::create(
            valid_update_info(13U));
    REQUIRE(identity_result.identity);
    auto movement_state = fixture::make_state(
        {12.0F, -3.0F, 36.0F}, {40.0F, 2.0F, 0.0F},
        movement::PlayerMovementMode::walking,
        movement::PlayerMovementHull::standing, 13U, 0x0020U, 1.0F, 1.0F,
        130'000'000U, 8U);
    const auto expected_signature =
        movement::local_player_movement_state_signature(movement_state);

    auto created = prediction::AuthoritativePlayerState::
        from_synthetic_complete_state(
            std::move(*identity_result.identity), std::move(movement_state));

    REQUIRE(created);
    REQUIRE(created.state);
    CHECK_FALSE(created.error);
    CHECK(created.state->update_identity().update_ordinal() == 11U);
    REQUIRE(created.state->acknowledgement().sequence());
    CHECK(created.state->acknowledgement().sequence()->value() == 13U);
    CHECK(created.state->movement_state().source_command_sequence() == 13U);
    CHECK(created.state->state_signature() == expected_signature);
    CHECK(created.state->compatibility_profile() == prediction::
        PredictionCompatibilityProfile::
            synthetic_authoritative_reconciliation_v1);
    CHECK(created.state->evidence_profile() == prediction::
        PredictionEvidenceProfile::
            project_typed_authoritative_states_and_independent_fixtures);
}

TEST_CASE("Authoritative discontinuities preserve their distinct boundaries",
    "[prediction][authoritative-state][discontinuity]")
{
    SECTION("normal")
    {
        auto identity = prediction::AuthoritativePlayerUpdateIdentity::create(
            valid_update_info(1U));
        REQUIRE(identity.identity);
        auto state = prediction::AuthoritativePlayerState::
            from_synthetic_complete_state(
                std::move(*identity.identity), fixture::make_state(
                    {0.0F, 0.0F, 36.0F}, {},
                    movement::PlayerMovementMode::walking,
                    movement::PlayerMovementHull::standing, 1U));
        REQUIRE(state.state);
        CHECK(state.state->update_identity().discontinuity() ==
            prediction::AuthoritativePlayerDiscontinuity::normal);
    }
    SECTION("teleport")
    {
        auto identity = prediction::AuthoritativePlayerUpdateIdentity::create(
            valid_update_info(
                1U, prediction::AuthoritativePlayerDiscontinuity::teleport));
        REQUIRE(identity.identity);
        auto state = prediction::AuthoritativePlayerState::
            from_synthetic_complete_state(
                std::move(*identity.identity), fixture::make_state(
                    {1'000.0F, -1'000.0F, 512.0F}, {},
                    movement::PlayerMovementMode::airborne,
                    movement::PlayerMovementHull::standing, 1U));
        REQUIRE(state.state);
        CHECK(state.state->update_identity().discontinuity() ==
            prediction::AuthoritativePlayerDiscontinuity::teleport);
    }
    SECTION("hard reset")
    {
        auto info = valid_update_info(
            1U,
            prediction::AuthoritativePlayerDiscontinuity::
                respawn_or_hard_reset);
        info.acknowledgement =
            prediction::AuthoritativeCommandAcknowledgement::none();
        auto identity = prediction::AuthoritativePlayerUpdateIdentity::create(
            info);
        REQUIRE(identity.identity);
        auto state = prediction::AuthoritativePlayerState::
            from_synthetic_complete_state(
                std::move(*identity.identity), fixture::make_state());
        REQUIRE(state.state);
        CHECK_FALSE(state.state->acknowledgement().has_sequence());
        CHECK(state.state->movement_state().source_command_sequence() == 0U);
        CHECK(state.state->update_identity().discontinuity() == prediction::
            AuthoritativePlayerDiscontinuity::respawn_or_hard_reset);
    }
}

TEST_CASE("Authoritative state rejects sequence and profile mismatches",
    "[prediction][authoritative-state][state-validation]")
{
    SECTION("complete state sequence must equal the exact acknowledgement")
    {
        auto identity = prediction::AuthoritativePlayerUpdateIdentity::create(
            valid_update_info(2U));
        REQUIRE(identity.identity);
        auto result = prediction::AuthoritativePlayerState::
            from_synthetic_complete_state(
                std::move(*identity.identity), fixture::make_state(
                    {0.0F, 0.0F, 36.0F}, {},
                    movement::PlayerMovementMode::walking,
                    movement::PlayerMovementHull::standing, 3U));
        check_state_error(result,
            prediction::PredictionErrorCode::invalid_authoritative_state);
    }
    SECTION("unsupported movement modes stay outside synthetic authority")
    {
        auto unsupported_info =
            movement::local_player_movement_state_create_info(
                fixture::make_state(
                    {0.0F, 0.0F, 36.0F}, {},
                    movement::PlayerMovementMode::walking,
                    movement::PlayerMovementHull::standing, 4U));
        unsupported_info.mode = movement::PlayerMovementMode::unsupported_liquid;
        unsupported_info.ground = {};
        const auto unsupported =
            movement::LocalPlayerMovementState::create(unsupported_info);
        REQUIRE(unsupported.state);
        auto identity = prediction::AuthoritativePlayerUpdateIdentity::create(
            valid_update_info(4U));
        REQUIRE(identity.identity);
        auto result = prediction::AuthoritativePlayerState::
            from_synthetic_complete_state(
                std::move(*identity.identity), *unsupported.state);
        check_state_error(result,
            prediction::PredictionErrorCode::invalid_authoritative_state);
    }
}

TEST_CASE("Authoritative state receives only normalized finite movement states",
    "[prediction][authoritative-state][input-normalization]")
{
    auto info = movement::local_player_movement_state_create_info(
        fixture::make_state());
    info.origin.x = (std::numeric_limits<float>::infinity)();
    const auto non_finite = movement::LocalPlayerMovementState::create(info);
    CHECK_FALSE(non_finite.state);
    REQUIRE(non_finite.error);
    CHECK(non_finite.error->code ==
        movement::LocalPlayerMovementStateErrorCode::non_finite_origin);
}

TEST_CASE("Stock authoritative state sources remain evidence-gated",
    "[prediction][authoritative-state][stock-evidence]")
{
    auto info = valid_update_info();
    info.session.prediction_profile = prediction::
        PredictionCompatibilityProfile::
            stock_protocol_48_authoritative_reconciliation_evidence_pending;
    check_identity_error(
        prediction::AuthoritativePlayerUpdateIdentity::create(info),
        prediction::PredictionErrorCode::stock_evidence_pending);

    prediction::StockAuthoritativePlayerStateSourceEvidencePending source;
    const auto polled = source.poll_next();
    CHECK_FALSE(polled);
    CHECK_FALSE(polled.state);
    REQUIRE(polled.error);
    CHECK(polled.error->code ==
        prediction::PredictionErrorCode::stock_evidence_pending);
    CHECK_FALSE(polled.error->context.empty());
}

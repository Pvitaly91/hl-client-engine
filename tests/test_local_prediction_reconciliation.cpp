#include "local_movement_test_fixture.hpp"

#include <hlclient/prediction/prediction_reconciliation.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_movement = hlclient::goldsrc::movement;
namespace movement = hlclient::movement;
namespace prediction = hlclient::prediction;
namespace fixture = hlclient::tests::local_movement;

[[nodiscard]] goldsrc_movement::LocalMovementCollisionSessionIdentity
collision_identity(
    const std::uint64_t revision = 1U,
    const std::uint64_t scene_signature = 0xC0111510U) noexcept
{
    return {
        goldsrc_movement::LocalMovementCollisionProfile::
            explicit_synthetic_static_brush_v1,
        0x11112222U,
        0x33334444U,
        revision,
        scene_signature,
    };
}

class IdentityCollision final : public goldsrc_movement::ILocalMovementCollision {
public:
    explicit IdentityCollision(
        const bool with_floor = true,
        goldsrc_movement::LocalMovementCollisionSessionIdentity identity =
            collision_identity())
        : collision_{with_floor}, identity_{identity}
    {
    }

    void add_positive_x_wall(const float surface_x)
    {
        collision_.add_positive_x_wall(surface_x);
    }

    void add_ceiling(const float surface_z)
    {
        collision_.add_ceiling(surface_z);
    }

    void add_step(
        const float minimum_x,
        const float maximum_x,
        const float minimum_y,
        const float maximum_y,
        const float height)
    {
        collision_.add_step(
            minimum_x, maximum_x, minimum_y, maximum_y, height);
    }

    void fail_traces(const bool value = true) noexcept
    {
        collision_.fail_traces(value);
    }

    void return_malformed_position(const bool value = true) noexcept
    {
        return_malformed_position_ = value;
    }

    void return_position_contents(
        const goldsrc_movement::LocalMovementCollisionContents contents)
        noexcept
    {
        position_contents_ = contents;
    }

    [[nodiscard]] goldsrc_movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return collision_.profile();
    }

    [[nodiscard]] bool valid() const noexcept override
    {
        return collision_.valid();
    }

    [[nodiscard]] std::optional<
        goldsrc_movement::LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept override
    {
        return identity_;
    }

    [[nodiscard]] goldsrc_movement::LocalMovementPointContentsQueryResult
    point_contents(
        const hlclient::assets::AssetVector3& point,
        hlclient::collision::CollisionQueryScratch& scratch,
        const goldsrc_movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return collision_.point_contents(point, scratch, config);
    }

    [[nodiscard]] goldsrc_movement::LocalMovementPositionQueryResult
    test_position(
        const hlclient::assets::AssetVector3& origin,
        const movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const goldsrc_movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        auto result = collision_.test_position(origin, hull, scratch, config);
        if (return_malformed_position_ && result.result) {
            result.result->status =
                goldsrc_movement::LocalMovementPositionStatus::free;
            result.result->hit.emplace(movement::PlayerMovementHitIdentity{});
        }
        if (position_contents_ && result.result) {
            result.result->contents = *position_contents_;
        }
        return result;
    }

    [[nodiscard]] goldsrc_movement::LocalMovementTraceQueryResult trace_hull(
        const hlclient::assets::AssetVector3& start,
        const hlclient::assets::AssetVector3& end,
        const movement::PlayerMovementHull hull,
        hlclient::collision::CollisionQueryScratch& scratch,
        const goldsrc_movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return collision_.trace_hull(start, end, hull, scratch, config);
    }

private:
    fixture::DeterministicLocalMovementCollision collision_;
    goldsrc_movement::LocalMovementCollisionSessionIdentity identity_;
    bool return_malformed_position_{false};
    std::optional<goldsrc_movement::LocalMovementCollisionContents>
        position_contents_;
};

[[nodiscard]] prediction::PredictionSessionIdentity make_session(
    const movement::LocalPlayerMovementState& initial_state,
    const goldsrc_movement::ILocalMovementCollision& collision,
    const goldsrc_movement::GoldSrcMovementEnvironment& environment,
    const goldsrc_movement::GoldSrcLocalMovementConfig& movement_config = {},
    const std::uint64_t session_generation = 1U,
    const std::uint64_t prediction_generation = 1U)
{
    const auto created = prediction::create_prediction_session_identity(
        session_generation, prediction_generation, collision, environment,
        movement_config, initial_state);
    REQUIRE(created.session);
    REQUIRE_FALSE(created.error);
    return *created.session;
}

[[nodiscard]] std::shared_ptr<const prediction::LocalPredictionHistoryState>
make_history(
    const movement::LocalPlayerMovementState& initial_state,
    const prediction::PredictionSessionIdentity& session,
    const prediction::LocalPredictionHistoryLimits& limits = {})
{
    const auto created = prediction::LocalPredictionHistoryState::create_initial(
        initial_state, session, limits);
    REQUIRE(created.history);
    REQUIRE_FALSE(created.error);
    return created.history;
}

[[nodiscard]] std::shared_ptr<const prediction::LocalPredictionHistoryState>
append_command(
    const prediction::LocalPredictionHistoryState& history,
    const goldsrc::GoldSrcUserCmdState& command,
    const goldsrc_movement::GoldSrcMovementEnvironment& environment,
    const goldsrc_movement::ILocalMovementCollision& collision,
    const goldsrc_movement::GoldSrcLocalMovementConfig& movement_config = {})
{
    goldsrc_movement::GoldSrcLocalMovementScratch scratch;
    auto simulated = goldsrc_movement::GoldSrcLocalMovementKernel::simulate(
        *history.current_predicted_state(), command, environment, collision,
        scratch, movement_config);
    REQUIRE(simulated);
    REQUIRE(simulated.state);

    prediction::PredictedCommandAppend append;
    append.command = std::make_shared<const goldsrc::GoldSrcUserCmdState>(
        command);
    append.pre_command_state = history.current_predicted_state();
    append.post_command_state =
        std::make_shared<const movement::LocalPlayerMovementState>(
            std::move(*simulated.state));
    append.simulation_statistics = simulated.statistics;
    append.touch_summary = prediction::summarize_prediction_touches(
        simulated.touches, simulated.statistics.start_solid_count != 0U,
        simulated.statistics.all_solid_count != 0U);
    const std::array appends{append};
    const auto appended =
        prediction::append_local_prediction_commands(history, appends);
    REQUIRE(appended.history);
    REQUIRE_FALSE(appended.error);
    return appended.history;
}

[[nodiscard]] std::shared_ptr<const prediction::LocalPredictionHistoryState>
append_commands(
    std::shared_ptr<const prediction::LocalPredictionHistoryState> history,
    const std::span<const goldsrc::GoldSrcUserCmdState> commands,
    const goldsrc_movement::GoldSrcMovementEnvironment& environment,
    const goldsrc_movement::ILocalMovementCollision& collision,
    const goldsrc_movement::GoldSrcLocalMovementConfig& movement_config = {})
{
    for (const auto& command : commands) {
        history = append_command(
            *history, command, environment, collision, movement_config);
    }
    return history;
}

[[nodiscard]] std::vector<goldsrc::GoldSrcUserCmdState> linear_commands(
    const std::uint32_t count,
    const float forward_move = 120.0F,
    const float side_move = 0.0F,
    const std::uint8_t msec = 10U,
    const std::uint16_t buttons = 0U)
{
    std::vector<goldsrc::GoldSrcUserCmdState> commands;
    commands.reserve(count);
    for (std::uint32_t value = 1U; value <= count; ++value) {
        commands.push_back(fixture::make_command(
            value, msec, forward_move, side_move, buttons));
    }
    return commands;
}

struct BuiltCampaign {
    prediction::PredictionSessionIdentity session{};
    std::shared_ptr<const prediction::LocalPredictionHistoryState> history;
    std::vector<goldsrc::GoldSrcUserCmdState> commands;
};

[[nodiscard]] BuiltCampaign build_campaign(
    const movement::LocalPlayerMovementState& initial_state,
    const std::vector<goldsrc::GoldSrcUserCmdState>& commands,
    const goldsrc_movement::GoldSrcMovementEnvironment& environment,
    const goldsrc_movement::ILocalMovementCollision& collision,
    const goldsrc_movement::GoldSrcLocalMovementConfig& movement_config = {},
    const prediction::LocalPredictionHistoryLimits& limits = {})
{
    auto session = make_session(
        initial_state, collision, environment, movement_config);
    auto history = make_history(initial_state, session, limits);
    history = append_commands(
        std::move(history), commands, environment, collision, movement_config);
    return {session, std::move(history), commands};
}

[[nodiscard]] movement::LocalPlayerMovementState state_from_info(
    const movement::LocalPlayerMovementStateCreateInfo& info)
{
    const auto created = movement::LocalPlayerMovementState::create(info);
    REQUIRE(created.state);
    REQUIRE_FALSE(created.error);
    return *created.state;
}

[[nodiscard]] movement::LocalPlayerMovementState offset_state(
    const movement::LocalPlayerMovementState& state,
    const hlclient::assets::AssetVector3 delta)
{
    auto info = movement::local_player_movement_state_create_info(state);
    info.origin.x += delta.x;
    info.origin.y += delta.y;
    info.origin.z += delta.z;
    ++info.state_revision;
    return state_from_info(info);
}

[[nodiscard]] movement::LocalPlayerMovementState revision_only_state(
    const movement::LocalPlayerMovementState& state)
{
    auto info = movement::local_player_movement_state_create_info(state);
    ++info.state_revision;
    return state_from_info(info);
}

[[nodiscard]] movement::LocalPlayerMovementState state_for_sequence(
    const movement::LocalPlayerMovementState& state,
    const std::uint32_t sequence)
{
    auto info = movement::local_player_movement_state_create_info(state);
    info.source_command_sequence = sequence;
    info.simulation_time_nanoseconds += 10'000'000U;
    ++info.state_revision;
    return state_from_info(info);
}

[[nodiscard]] prediction::AuthoritativePlayerState make_authority(
    const prediction::PredictionSessionIdentity& session,
    const std::uint64_t ordinal,
    const std::uint32_t acknowledged_sequence,
    const movement::LocalPlayerMovementState& movement_state,
    const prediction::AuthoritativePlayerDiscontinuity discontinuity =
        prediction::AuthoritativePlayerDiscontinuity::normal,
    const std::optional<std::int64_t> synthetic_authority_time_nanoseconds =
        std::nullopt)
{
    const auto sequence =
        goldsrc::GoldSrcUserCmdSequence::create(acknowledged_sequence);
    REQUIRE(sequence);
    prediction::AuthoritativePlayerUpdateIdentityCreateInfo identity_info;
    identity_info.session = session;
    identity_info.update_ordinal = ordinal;
    identity_info.acknowledgement =
        prediction::AuthoritativeCommandAcknowledgement::for_sequence(
            *sequence);
    identity_info.synthetic_authority_time_nanoseconds =
        synthetic_authority_time_nanoseconds.value_or(
            static_cast<std::int64_t>(
                movement_state.simulation_time_nanoseconds()));
    identity_info.discontinuity = discontinuity;
    auto identity =
        prediction::AuthoritativePlayerUpdateIdentity::create(identity_info);
    REQUIRE(identity.identity);
    REQUIRE_FALSE(identity.error);
    auto state = prediction::AuthoritativePlayerState::
        from_synthetic_complete_state(
            std::move(*identity.identity), movement_state);
    REQUIRE(state.state);
    REQUIRE_FALSE(state.error);
    return std::move(*state.state);
}

[[nodiscard]] prediction::AuthoritativePlayerState make_hard_reset_authority(
    const prediction::PredictionSessionIdentity& session,
    const std::uint64_t ordinal,
    const movement::LocalPlayerMovementState& movement_state)
{
    prediction::AuthoritativePlayerUpdateIdentityCreateInfo identity_info;
    identity_info.session = session;
    identity_info.update_ordinal = ordinal;
    identity_info.acknowledgement =
        prediction::AuthoritativeCommandAcknowledgement::none();
    identity_info.synthetic_authority_time_nanoseconds =
        static_cast<std::int64_t>(
            movement_state.simulation_time_nanoseconds());
    identity_info.discontinuity = prediction::
        AuthoritativePlayerDiscontinuity::respawn_or_hard_reset;
    auto identity =
        prediction::AuthoritativePlayerUpdateIdentity::create(identity_info);
    REQUIRE(identity.identity);
    auto state = prediction::AuthoritativePlayerState::
        from_synthetic_complete_state(
            std::move(*identity.identity), movement_state);
    REQUIRE(state.state);
    return std::move(*state.state);
}

[[nodiscard]] prediction::PredictionReconciliationResult reconcile(
    const prediction::LocalPredictionHistoryState& history,
    const prediction::AuthoritativePlayerState& authoritative,
    const goldsrc_movement::GoldSrcMovementEnvironment& environment,
    const goldsrc_movement::ILocalMovementCollision& collision,
    const goldsrc_movement::GoldSrcLocalMovementConfig& movement_config = {},
    const prediction::PredictionReconciliationConfig& config = {})
{
    goldsrc_movement::GoldSrcLocalMovementScratch scratch;
    return prediction::LocalPlayerPredictionReconciler::reconcile(
        history, authoritative, environment, collision, scratch,
        movement_config, config);
}

[[nodiscard]] movement::LocalPlayerMovementState direct_replay(
    const movement::LocalPlayerMovementState& initial_state,
    const std::span<const goldsrc::GoldSrcUserCmdState> commands,
    const goldsrc_movement::GoldSrcMovementEnvironment& environment,
    const goldsrc_movement::ILocalMovementCollision& collision,
    const goldsrc_movement::GoldSrcLocalMovementConfig& movement_config = {})
{
    std::optional<movement::LocalPlayerMovementState> state{initial_state};
    goldsrc_movement::GoldSrcLocalMovementScratch scratch;
    for (const auto& command : commands) {
        auto simulated = goldsrc_movement::GoldSrcLocalMovementKernel::simulate(
            *state, command, environment, collision, scratch, movement_config);
        REQUIRE(simulated.state);
        REQUIRE_FALSE(simulated.error);
        state.emplace(std::move(*simulated.state));
    }
    return *state;
}

void check_reconciliation_error(
    const prediction::PredictionReconciliationResult& result,
    const prediction::LocalPredictionHistoryState& original,
    const prediction::PredictionErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
    REQUIRE(result.history);
    REQUIRE(result.corrected_current_state);
    CHECK_FALSE(result.history_changed);
    CHECK(prediction::local_prediction_history_signature(*result.history) ==
        prediction::local_prediction_history_signature(original));
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) ==
        movement::local_player_movement_state_signature(
            *original.current_predicted_state()));
}

struct AcceptedFixture {
    BuiltCampaign campaign;
    prediction::AuthoritativePlayerState authority;
    std::shared_ptr<const prediction::LocalPredictionHistoryState> accepted;
};

[[nodiscard]] AcceptedFixture accepted_fixture(
    const goldsrc_movement::GoldSrcMovementEnvironment& environment,
    const goldsrc_movement::ILocalMovementCollision& collision)
{
    auto campaign = build_campaign(
        fixture::make_state(), linear_commands(3U), environment, collision);
    const auto* entry = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(1U));
    REQUIRE(entry != nullptr);
    auto authority = make_authority(
        campaign.session, 10U, 1U, *entry->post_command_state());
    auto accepted = reconcile(
        *campaign.history, authority, environment, collision);
    REQUIRE(accepted.history);
    REQUIRE_FALSE(accepted.error);
    return {std::move(campaign), std::move(authority),
        std::move(accepted.history)};
}

} // namespace

TEST_CASE("Exact acknowledgement takes the retained-state fast path",
    "[prediction][reconciliation][exact][fast-path]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(4U), environment, collision);
    const auto* acknowledged = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(2U));
    REQUIRE(acknowledged != nullptr);
    const auto authority = make_authority(
        campaign.session, 1U, 2U, *acknowledged->post_command_state());
    const auto before_current_signature =
        movement::local_player_movement_state_signature(
            *campaign.history->current_predicted_state());

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    REQUIRE(result.history);
    CHECK(result.history_changed);
    CHECK(result.correction_class == prediction::PredictionCorrectionClass::exact);
    CHECK(result.replay_statistics.replayed_command_count == 0U);
    CHECK_FALSE(result.replay_statistics.first_sequence);
    CHECK_FALSE(result.replay_statistics.last_sequence);
    REQUIRE(result.acknowledgement_metrics);
    CHECK(result.acknowledgement_metrics->exact_physical_state_match);
    CHECK(result.acknowledgement_metrics->exact_state_signature_match);
    CHECK(result.history->size() == 2U);
    REQUIRE(result.history->oldest_command_sequence());
    CHECK(result.history->oldest_command_sequence()->value() == 3U);
    CHECK(result.history->anchor().acknowledgement().sequence()->value() == 2U);
    CHECK(result.history->statistics().total_acknowledged_commands == 2U);
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) == before_current_signature);
}

TEST_CASE("Exact physical authority bypasses replay even when metadata differs",
    "[prediction][reconciliation][exact][fast-path][delayed]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(20U), environment, collision);
    const auto* acknowledged = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(12U));
    REQUIRE(acknowledged != nullptr);
    const auto authority_state =
        revision_only_state(*acknowledged->post_command_state());
    const auto authority =
        make_authority(campaign.session, 1U, 12U, authority_state);
    prediction::PredictionReconciliationConfig config;
    config.limits.maximum_replay_commands = 1U;

    const auto result = reconcile(
        *campaign.history, authority, environment, collision, {}, config);

    REQUIRE(result);
    REQUIRE(result.acknowledgement_metrics);
    CHECK(result.acknowledgement_metrics->exact_physical_state_match);
    CHECK_FALSE(result.acknowledgement_metrics->exact_state_signature_match);
    CHECK(result.correction_class == prediction::PredictionCorrectionClass::exact);
    CHECK(result.replay_statistics.replayed_command_count == 0U);
    CHECK(result.history->size() == 8U);
    REQUIRE(result.history->oldest_command_sequence());
    REQUIRE(result.history->newest_command_sequence());
    CHECK(result.history->oldest_command_sequence()->value() == 13U);
    CHECK(result.history->newest_command_sequence()->value() == 20U);
    CHECK(result.history->anchor().state_signature() ==
        movement::local_player_movement_state_signature(authority_state));
}

TEST_CASE("Delayed acknowledgement replays commands thirteen through twenty",
    "[prediction][reconciliation][delayed][depth-eight]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(20U), environment, collision);
    const auto* acknowledged = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(12U));
    REQUIRE(acknowledged != nullptr);
    const auto corrected = offset_state(
        *acknowledged->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 12U, corrected);
    const auto expected = direct_replay(corrected,
        std::span{campaign.commands}.subspan(12U), environment, collision);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    CHECK(result.replay_statistics.replayed_command_count == 8U);
    REQUIRE(result.replay_statistics.first_sequence);
    REQUIRE(result.replay_statistics.last_sequence);
    CHECK(result.replay_statistics.first_sequence->value() == 13U);
    CHECK(result.replay_statistics.last_sequence->value() == 20U);
    CHECK(result.history->size() == 8U);
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) ==
        movement::local_player_movement_state_signature(expected));
}

TEST_CASE("Correction at the newest command becomes the new empty anchor",
    "[prediction][reconciliation][newest][correction]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(3U), environment, collision);
    const auto corrected = offset_state(
        *campaign.history->current_predicted_state(), {0.0F, 0.5F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 3U, corrected);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    CHECK(result.history->size() == 0U);
    CHECK(result.replay_statistics.replayed_command_count == 0U);
    CHECK(result.correction_class ==
        prediction::PredictionCorrectionClass::small_visual_correction);
    REQUIRE(result.current_correction_metrics);
    CHECK(result.current_correction_metrics->position_error_magnitude ==
        Catch::Approx(0.5));
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) ==
        movement::local_player_movement_state_signature(corrected));
    CHECK(result.history->anchor().state_signature() ==
        authority.state_signature());
}

TEST_CASE("Correction replays exactly one unacknowledged command",
    "[prediction][reconciliation][one-unacknowledged]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(3U), environment, collision);
    const auto* acknowledged = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(2U));
    REQUIRE(acknowledged != nullptr);
    const auto corrected = offset_state(
        *acknowledged->post_command_state(), {0.0F, 0.5F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 2U, corrected);
    const auto direct = direct_replay(
        corrected, std::span{campaign.commands}.subspan(2U), environment,
        collision);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    CHECK(result.history->size() == 1U);
    CHECK(result.replay_statistics.replayed_command_count == 1U);
    REQUIRE(result.replay_statistics.first_sequence);
    REQUIRE(result.replay_statistics.last_sequence);
    CHECK(result.replay_statistics.first_sequence->value() == 3U);
    CHECK(result.replay_statistics.last_sequence->value() == 3U);
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) ==
        movement::local_player_movement_state_signature(direct));
    const auto* replayed = result.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(3U));
    REQUIRE(replayed != nullptr);
    CHECK(replayed->pre_state_signature() ==
        movement::local_player_movement_state_signature(corrected));
}

TEST_CASE("Correction replays and trims many retained commands deterministically",
    "[prediction][reconciliation][many-unacknowledged][trim]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(8U), environment, collision);
    const auto* acknowledged = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(2U));
    REQUIRE(acknowledged != nullptr);
    const auto corrected = offset_state(
        *acknowledged->post_command_state(), {0.25F, 0.5F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 2U, corrected);
    const auto direct = direct_replay(
        corrected, std::span{campaign.commands}.subspan(2U), environment,
        collision);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    CHECK(result.history->size() == 6U);
    CHECK(result.replay_statistics.replayed_command_count == 6U);
    REQUIRE(result.history->oldest_command_sequence());
    REQUIRE(result.history->newest_command_sequence());
    CHECK(result.history->oldest_command_sequence()->value() == 3U);
    CHECK(result.history->newest_command_sequence()->value() == 8U);
    CHECK(result.history->find_exact(
              *goldsrc::GoldSrcUserCmdSequence::create(2U)) == nullptr);
    CHECK(result.history->statistics().total_acknowledged_commands == 2U);
    CHECK(result.history->statistics().total_replayed_commands == 6U);
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) ==
        movement::local_player_movement_state_signature(direct));
}

TEST_CASE("Exact newest acknowledgement has no commands to replay",
    "[prediction][reconciliation][no-unacknowledged]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(2U), environment, collision);
    const auto authority = make_authority(
        campaign.session, 1U, 2U,
        *campaign.history->current_predicted_state());

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    CHECK(result.history->size() == 0U);
    CHECK(result.correction_class == prediction::PredictionCorrectionClass::exact);
    CHECK(result.replay_statistics.replayed_command_count == 0U);
    CHECK(result.history->current_predicted_state() ==
        result.history->anchor().movement_state());
}

TEST_CASE("Accepted authority classifies stale duplicate and conflicting updates",
    "[prediction][reconciliation][authority-ordering]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();

    SECTION("stale update is ignored by default")
    {
        const auto accepted = accepted_fixture(environment, collision);
        const auto* entry = accepted.campaign.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(2U));
        REQUIRE(entry != nullptr);
        const auto stale_authority = make_authority(
            accepted.campaign.session, 9U, 2U,
            *entry->post_command_state());
        const auto result = reconcile(
            *accepted.accepted, stale_authority, environment, collision);
        REQUIRE(result);
        CHECK(result.stale_ignored);
        CHECK_FALSE(result.duplicate_ignored);
        CHECK_FALSE(result.history_changed);
        CHECK(prediction::local_prediction_history_signature(*result.history) ==
            prediction::local_prediction_history_signature(
                *accepted.accepted));
    }

    SECTION("strict stale policy returns a typed failure")
    {
        const auto accepted = accepted_fixture(environment, collision);
        const auto* entry = accepted.campaign.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(2U));
        REQUIRE(entry != nullptr);
        const auto stale_authority = make_authority(
            accepted.campaign.session, 9U, 2U,
            *entry->post_command_state());
        prediction::PredictionReconciliationConfig config;
        config.ignore_stale_updates = false;
        const auto result = reconcile(
            *accepted.accepted, stale_authority, environment, collision, {},
            config);
        check_reconciliation_error(result, *accepted.accepted,
            prediction::PredictionErrorCode::stale_authoritative_update);
    }

    SECTION("identical authority is an ignored duplicate")
    {
        const auto accepted = accepted_fixture(environment, collision);
        const auto result = reconcile(
            *accepted.accepted, accepted.authority, environment, collision);
        REQUIRE(result);
        CHECK(result.duplicate_ignored);
        CHECK_FALSE(result.stale_ignored);
        CHECK_FALSE(result.history_changed);
    }

    SECTION("same accepted identity with different state conflicts")
    {
        const auto accepted = accepted_fixture(environment, collision);
        const auto conflicting_state = offset_state(
            accepted.authority.movement_state(), {0.0F, 0.5F, 0.0F});
        const auto conflict = make_authority(
            accepted.campaign.session, 10U, 1U, conflicting_state);
        const auto result = reconcile(
            *accepted.accepted, conflict, environment, collision);
        check_reconciliation_error(result, *accepted.accepted,
            prediction::PredictionErrorCode::conflicting_authoritative_update);
    }

    SECTION("same ordinal and state with changed authority time conflicts")
    {
        const auto accepted = accepted_fixture(environment, collision);
        const auto conflict = make_authority(accepted.campaign.session, 10U,
            1U, accepted.authority.movement_state(),
            prediction::AuthoritativePlayerDiscontinuity::normal,
            accepted.authority.update_identity().
                    synthetic_authority_time_nanoseconds() +
                1);
        const auto result = reconcile(
            *accepted.accepted, conflict, environment, collision);
        check_reconciliation_error(result, *accepted.accepted,
            prediction::PredictionErrorCode::conflicting_authoritative_update);
    }

    SECTION("new ordinal with the same acknowledgement and state is accepted")
    {
        const auto accepted = accepted_fixture(environment, collision);
        const auto repeated_ack = make_authority(accepted.campaign.session,
            11U, 1U, accepted.authority.movement_state(),
            prediction::AuthoritativePlayerDiscontinuity::teleport);
        const auto result = reconcile(
            *accepted.accepted, repeated_ack, environment, collision);
        REQUIRE(result);
        CHECK(result.history_changed);
        CHECK_FALSE(result.duplicate_ignored);
        CHECK(result.correction_class ==
            prediction::PredictionCorrectionClass::teleport_snap);
    }

    SECTION("new ordinal cannot change an already accepted acknowledgement")
    {
        const auto accepted = accepted_fixture(environment, collision);
        const auto conflicting_state = offset_state(
            accepted.authority.movement_state(), {0.0F, 0.5F, 0.0F});
        const auto conflict = make_authority(
            accepted.campaign.session, 11U, 1U, conflicting_state);
        const auto result = reconcile(
            *accepted.accepted, conflict, environment, collision);
        check_reconciliation_error(result, *accepted.accepted,
            prediction::PredictionErrorCode::conflicting_authoritative_update);
    }
}

TEST_CASE("Reconciliation rejects future and stale acknowledgements",
    "[prediction][reconciliation][acknowledgement-boundary]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();

    SECTION("future acknowledgement")
    {
        const auto campaign = build_campaign(
            fixture::make_state(), linear_commands(2U), environment, collision);
        const auto future_state = state_for_sequence(
            *campaign.history->current_predicted_state(), 3U);
        const auto future =
            make_authority(campaign.session, 1U, 3U, future_state);
        const auto result = reconcile(
            *campaign.history, future, environment, collision);
        check_reconciliation_error(result, *campaign.history,
            prediction::PredictionErrorCode::future_acknowledgement);
        REQUIRE(result.error->command_sequence);
        CHECK(result.error->command_sequence->value() == 3U);
    }

    SECTION("acknowledgement before an accepted anchor is stale")
    {
        const auto campaign = build_campaign(
            fixture::make_state(), linear_commands(3U), environment, collision);
        const auto* second = campaign.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(2U));
        REQUIRE(second != nullptr);
        const auto first_authority = make_authority(
            campaign.session, 10U, 2U, *second->post_command_state());
        const auto accepted = reconcile(
            *campaign.history, first_authority, environment, collision);
        REQUIRE(accepted.history);
        const auto* first = campaign.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(1U));
        REQUIRE(first != nullptr);
        const auto stale_authority = make_authority(
            campaign.session, 11U, 1U, *first->post_command_state());
        const auto result = reconcile(
            *accepted.history, stale_authority, environment, collision);
        REQUIRE(result);
        REQUIRE(result.history);
        REQUIRE(result.corrected_current_state);
        CHECK(result.stale_ignored);
        CHECK_FALSE(result.history_changed);
        CHECK(prediction::local_prediction_history_signature(*result.history) ==
            prediction::local_prediction_history_signature(*accepted.history));
        CHECK(movement::local_player_movement_state_signature(
                  *result.corrected_current_state) ==
            movement::local_player_movement_state_signature(
                *accepted.history->current_predicted_state()));

        prediction::PredictionReconciliationConfig strict;
        strict.ignore_stale_updates = false;
        const auto strict_result = reconcile(*accepted.history,
            stale_authority, environment, collision, {}, strict);
        check_reconciliation_error(strict_result, *accepted.history,
            prediction::PredictionErrorCode::stale_authoritative_update);
    }
}

TEST_CASE("Reconciliation binds session environment config and collision identity",
    "[prediction][reconciliation][runtime-identity]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto commands = linear_commands(1U);

    SECTION("authoritative session mismatch")
    {
        const auto campaign = build_campaign(
            fixture::make_state(), commands, environment, collision);
        auto other_session = campaign.session;
        ++other_session.session_generation;
        const auto authority = make_authority(
            other_session, 1U, 1U,
            *campaign.history->current_predicted_state());
        const auto result = reconcile(
            *campaign.history, authority, environment, collision);
        check_reconciliation_error(result, *campaign.history,
            prediction::PredictionErrorCode::prediction_session_mismatch);
    }

    SECTION("movement environment signature mismatch")
    {
        const auto initial = fixture::make_state();
        auto session = make_session(initial, collision, environment);
        ++session.movement_environment_signature;
        auto history = make_history(initial, session);
        history = append_commands(
            std::move(history), commands, environment, collision);
        const auto authority = make_authority(
            session, 1U, 1U, *history->current_predicted_state());
        const auto result = reconcile(
            *history, authority, environment, collision);
        check_reconciliation_error(result, *history,
            prediction::PredictionErrorCode::movement_environment_mismatch);
    }

    SECTION("movement config signature mismatch")
    {
        const auto campaign = build_campaign(
            fixture::make_state(), commands, environment, collision);
        auto changed_config = goldsrc_movement::GoldSrcLocalMovementConfig{};
        ++changed_config.maximum_substeps_per_command;
        const auto authority = make_authority(
            campaign.session, 1U, 1U,
            *campaign.history->current_predicted_state());
        const auto result = reconcile(
            *campaign.history, authority, environment, collision,
            changed_config);
        check_reconciliation_error(result, *campaign.history,
            prediction::PredictionErrorCode::movement_config_mismatch);
    }

    SECTION("collision world identity mismatch")
    {
        const auto campaign = build_campaign(
            fixture::make_state(), commands, environment, collision);
        IdentityCollision other_collision{
            true, collision_identity(2U, 0xC0111511U)};
        const auto authority = make_authority(
            campaign.session, 1U, 1U,
            *campaign.history->current_predicted_state());
        const auto result = reconcile(
            *campaign.history, authority, environment, other_collision);
        check_reconciliation_error(result, *campaign.history,
            prediction::PredictionErrorCode::collision_world_mismatch);
    }
}

TEST_CASE("Reconciliation rejects authoritative state embedded in solid",
    "[prediction][reconciliation][authority-blocking]")
{
    IdentityCollision collision;
    collision.add_positive_x_wall(32.0F);
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(1U, 0.0F), environment,
        collision);
    auto blocking_info = movement::local_player_movement_state_create_info(
        *campaign.history->current_predicted_state());
    blocking_info.origin.x = 20.0F;
    ++blocking_info.state_revision;
    const auto blocking_state = state_from_info(blocking_info);
    const auto authority =
        make_authority(campaign.session, 1U, 1U, blocking_state);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    check_reconciliation_error(result, *campaign.history,
        prediction::PredictionErrorCode::authoritative_state_blocking);
}

TEST_CASE("Reconciliation rejects malformed successful authority collision data",
    "[prediction][reconciliation][authority-collision-validation]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(1U, 0.0F), environment,
        collision);
    const auto authority = make_authority(campaign.session, 1U, 1U,
        *campaign.history->current_predicted_state());
    collision.return_malformed_position();

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    check_reconciliation_error(result, *campaign.history,
        prediction::PredictionErrorCode::invalid_authoritative_state);
}

TEST_CASE("Reconciliation rejects unsupported or inconsistent authority contents",
    "[prediction][reconciliation][authority-collision-validation][contents]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(1U, 0.0F), environment,
        collision);
    const auto authority = make_authority(campaign.session, 1U, 1U,
        *campaign.history->current_predicted_state());

    SECTION("water")
    {
        collision.return_position_contents({
            movement::PlayerMovementContents::water, -3});
    }
    SECTION("slime")
    {
        collision.return_position_contents({
            movement::PlayerMovementContents::slime, -4});
    }
    SECTION("lava")
    {
        collision.return_position_contents({
            movement::PlayerMovementContents::lava, -5});
    }
    SECTION("current")
    {
        collision.return_position_contents({
            movement::PlayerMovementContents::current, -9});
    }
    SECTION("dry category differs from the normalized state")
    {
        collision.return_position_contents({
            movement::PlayerMovementContents::sky, -6});
    }

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    check_reconciliation_error(result, *campaign.history,
        prediction::PredictionErrorCode::invalid_authoritative_state);
}

TEST_CASE("Replay failure and work limits preserve the original publication",
    "[prediction][reconciliation][transactional-failure]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(3U), environment, collision);
    const auto* first = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(1U));
    REQUIRE(first != nullptr);
    const auto corrected = offset_state(
        *first->post_command_state(), {0.0F, 0.5F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 1U, corrected);

    SECTION("movement replay query fails")
    {
        collision.fail_traces();
        const auto result = reconcile(
            *campaign.history, authority, environment, collision);
        check_reconciliation_error(result, *campaign.history,
            prediction::PredictionErrorCode::prediction_replay_failed);
    }

    SECTION("bounded replay command limit fails before mutation")
    {
        prediction::PredictionReconciliationConfig config;
        config.limits.maximum_replay_commands = 1U;
        const auto result = reconcile(
            *campaign.history, authority, environment, collision, {}, config);
        check_reconciliation_error(result, *campaign.history,
            prediction::PredictionErrorCode::prediction_replay_limit_exceeded);
    }

    SECTION("bounded replay time fails before mutation")
    {
        prediction::PredictionReconciliationConfig config;
        config.limits.maximum_replay_time_nanoseconds = 19'999'999U;
        const auto result = reconcile(
            *campaign.history, authority, environment, collision, {}, config);
        check_reconciliation_error(result, *campaign.history,
            prediction::PredictionErrorCode::prediction_replay_limit_exceeded);
    }
}

TEST_CASE("Jump replay consumes one edge and preserves held-button continuity",
    "[prediction][reconciliation][jump]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const std::vector commands{
        fixture::make_command(1U),
        fixture::make_command(2U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump),
        fixture::make_command(3U, 10U, 0.0F, 0.0F,
            goldsrc::kSyntheticGoldSrcButtonJump),
    };
    const auto campaign = build_campaign(
        fixture::make_state(), commands, environment, collision);
    const auto* first = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(1U));
    REQUIRE(first != nullptr);
    const auto corrected = offset_state(
        *first->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 1U, corrected);
    const auto direct = direct_replay(
        corrected, std::span{commands}.subspan(1U), environment, collision);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    CHECK(result.replay_statistics.replayed_command_count == 2U);
    const auto* jump = result.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(2U));
    const auto* held = result.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(3U));
    REQUIRE(jump != nullptr);
    REQUIRE(held != nullptr);
    CHECK(jump->simulation_statistics().jump_count == 1U);
    CHECK(held->simulation_statistics().jump_count == 0U);
    CHECK((result.corrected_current_state->old_buttons() &
        goldsrc::kSyntheticGoldSrcButtonJump) != 0U);
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) ==
        movement::local_player_movement_state_signature(direct));
}

TEST_CASE("Duck replay retains transitions and blocked stand state",
    "[prediction][reconciliation][duck]")
{
    const auto environment = fixture::make_environment();

    SECTION("duck transition is replayed once")
    {
        IdentityCollision collision;
        const std::vector commands{
            fixture::make_command(1U),
            fixture::make_command(2U, 10U, 0.0F, 0.0F,
                goldsrc::kSyntheticGoldSrcButtonDuck),
        };
        const auto campaign = build_campaign(
            fixture::make_state(), commands, environment, collision);
        const auto* first = campaign.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(1U));
        REQUIRE(first != nullptr);
        const auto corrected = offset_state(
            *first->post_command_state(), {0.0F, 0.25F, 0.0F});
        const auto authority =
            make_authority(campaign.session, 1U, 1U, corrected);
        const auto result = reconcile(
            *campaign.history, authority, environment, collision);
        REQUIRE(result);
        const auto* duck = result.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(2U));
        REQUIRE(duck != nullptr);
        CHECK(duck->simulation_statistics().duck_enter_count == 1U);
        CHECK(duck->simulation_statistics().duck_exit_count == 0U);
        CHECK(result.corrected_current_state->hull() ==
            movement::PlayerMovementHull::ducked);
        CHECK(result.corrected_current_state->view_offset().z ==
            goldsrc_movement::kValveDuckViewOffsetZ);
    }

    SECTION("blocked stand remains ducked without a duplicate transition")
    {
        IdentityCollision collision;
        collision.add_ceiling(52.0F);
        const auto initial = fixture::make_state(
            {0.0F, 0.0F, 18.0F}, {}, movement::PlayerMovementMode::walking,
            movement::PlayerMovementHull::ducked);
        const std::vector commands{
            fixture::make_command(1U, 10U, 0.0F, 0.0F,
                goldsrc::kSyntheticGoldSrcButtonDuck),
            fixture::make_command(2U),
        };
        const auto campaign = build_campaign(
            initial, commands, environment, collision);
        const auto* first = campaign.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(1U));
        REQUIRE(first != nullptr);
        const auto corrected = offset_state(
            *first->post_command_state(), {0.0F, 0.1F, 0.0F});
        const auto authority =
            make_authority(campaign.session, 1U, 1U, corrected);
        const auto direct = direct_replay(
            corrected, std::span{commands}.subspan(1U), environment,
            collision);
        const auto result = reconcile(
            *campaign.history, authority, environment, collision);
        REQUIRE(result);
        const auto* stand = result.history->find_exact(
            *goldsrc::GoldSrcUserCmdSequence::create(2U));
        REQUIRE(stand != nullptr);
        CHECK(stand->simulation_statistics().stand_blocked_count == 1U);
        CHECK(stand->simulation_statistics().duck_exit_count == 0U);
        CHECK(result.corrected_current_state->hull() ==
            movement::PlayerMovementHull::ducked);
        CHECK(result.corrected_current_state->view_offset().z ==
            goldsrc_movement::kValveDuckViewOffsetZ);
        CHECK(movement::local_player_movement_state_signature(
                  *result.corrected_current_state) ==
            movement::local_player_movement_state_signature(direct));
    }
}

TEST_CASE("Wall correction replays bounded zero-progress contact safely",
    "[prediction][reconciliation][wall][zero-progress]")
{
    IdentityCollision collision;
    collision.add_positive_x_wall(32.0F);
    const auto environment = fixture::make_environment();
    const auto initial = fixture::make_state(
        {16.0F, 0.0F, 36.0F}, {0.0F, 80.0F, 0.0F});
    const auto commands = linear_commands(64U, 320.0F, -80.0F);
    const auto campaign = build_campaign(
        initial, commands, environment, collision);
    const auto* first = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(1U));
    REQUIRE(first != nullptr);
    const auto corrected = offset_state(
        *first->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 1U, corrected);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    CHECK(result.replay_statistics.replayed_command_count == 63U);
    CHECK(result.history->size() == 63U);
    std::size_t zero_progress_count = 0U;
    for (const auto& entry : result.history->entries()) {
        CHECK(entry.simulation_statistics().start_solid_count == 0U);
        CHECK(entry.simulation_statistics().all_solid_count == 0U);
        if (std::abs(entry.post_command_state()->origin().x - 16.0F) <=
            1.0e-4F) {
            ++zero_progress_count;
        }
    }
    CHECK(zero_progress_count == result.history->size());
    CHECK(result.corrected_current_state->origin().x ==
        Catch::Approx(16.0F).margin(1.0e-4F));
    CHECK(result.corrected_current_state->velocity().y > 0.0F);
    CHECK(prediction::local_prediction_history_signature(*result.history) !=
        0U);
}

TEST_CASE("Step candidate replay matches uninterrupted selected-path metadata",
    "[prediction][reconciliation][step]")
{
    IdentityCollision collision;
    collision.add_step(20.0F, 100.0F, -64.0F, 64.0F, 12.0F);
    const auto environment = fixture::make_environment();
    const std::vector commands{
        fixture::make_command(1U),
        fixture::make_command(2U, 100U, 320.0F),
    };
    const auto campaign = build_campaign(
        fixture::make_state(), commands, environment, collision);
    const auto* first = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(1U));
    REQUIRE(first != nullptr);
    const auto corrected = offset_state(
        *first->post_command_state(), {0.0F, 0.1F, 0.0F});
    const auto authority =
        make_authority(campaign.session, 1U, 1U, corrected);
    goldsrc_movement::GoldSrcLocalMovementScratch direct_scratch;
    const auto direct = goldsrc_movement::GoldSrcLocalMovementKernel::simulate(
        corrected, commands[1U], environment, collision, direct_scratch);
    REQUIRE(direct.state);
    REQUIRE(direct.statistics.step_attempt_count > 0U);
    REQUIRE(direct.statistics.step_success_count > 0U);
    for (const auto& touch : direct.touches) {
        CHECK(touch.phase != movement::PlayerMovementPhase::direct_slide);
    }
    const auto direct_touch_summary = prediction::summarize_prediction_touches(
        direct.touches, direct.statistics.start_solid_count != 0U,
        direct.statistics.all_solid_count != 0U);

    const auto result = reconcile(
        *campaign.history, authority, environment, collision);

    REQUIRE(result);
    const auto* replayed = result.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(2U));
    REQUIRE(replayed != nullptr);
    CHECK(replayed->simulation_statistics().step_attempt_count ==
        direct.statistics.step_attempt_count);
    CHECK(replayed->simulation_statistics().step_success_count ==
        direct.statistics.step_success_count);
    CHECK(replayed->touch_summary().deterministic_signature ==
        direct_touch_summary.deterministic_signature);
    CHECK(movement::local_player_movement_state_signature(
              *result.corrected_current_state) ==
        movement::local_player_movement_state_signature(*direct.state));
}

TEST_CASE("Hard reset requires a new generation and clears cross-generation replay",
    "[prediction][reconciliation][hard-reset]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto campaign = build_campaign(
        fixture::make_state(), linear_commands(3U), environment, collision);
    const auto reset_state = fixture::make_state(
        {-24.0F, 12.0F, 36.0F}, {}, movement::PlayerMovementMode::walking,
        movement::PlayerMovementHull::standing, 0U, 0U, 1.0F, 1.0F, 0U,
        1U);
    const auto replacement_session = make_session(
        reset_state, collision, environment, {}, 2U, 2U);
    const auto reset_authority =
        make_hard_reset_authority(replacement_session, 20U, reset_state);

    const auto reset = reconcile(
        *campaign.history, reset_authority, environment, collision);

    REQUIRE(reset);
    REQUIRE(reset.history);
    CHECK(reset.history_changed);
    CHECK(reset.correction_class ==
        prediction::PredictionCorrectionClass::hard_reset);
    CHECK(reset.history->session() == replacement_session);
    CHECK(reset.history->size() == 0U);
    CHECK_FALSE(reset.history->oldest_command_sequence());
    CHECK_FALSE(reset.history->newest_command_sequence());
    CHECK_FALSE(reset.history->anchor().acknowledgement().has_sequence());
    REQUIRE(reset.history->anchor().authority_update_ordinal());
    CHECK(*reset.history->anchor().authority_update_ordinal() == 20U);
    CHECK(reset.history->statistics().total_acknowledged_commands == 3U);
    CHECK(reset.replay_statistics.replayed_command_count == 0U);
    CHECK_FALSE(reset.acknowledgement_metrics);
    CHECK_FALSE(reset.current_correction_metrics);
    CHECK(movement::local_player_movement_state_signature(
              *reset.corrected_current_state) ==
        movement::local_player_movement_state_signature(reset_state));

    const auto duplicate_reset = reconcile(
        *reset.history, reset_authority, environment, collision);
    REQUIRE(duplicate_reset);
    CHECK(duplicate_reset.duplicate_ignored);
    CHECK_FALSE(duplicate_reset.history_changed);
    CHECK(prediction::local_prediction_history_signature(
              *duplicate_reset.history) ==
        prediction::local_prediction_history_signature(*reset.history));

    const auto conflicting_reset_state =
        offset_state(reset_state, {0.0F, 0.5F, 0.0F});
    const auto conflicting_reset = make_hard_reset_authority(
        replacement_session, 20U, conflicting_reset_state);
    const auto rejected_conflict = reconcile(
        *reset.history, conflicting_reset, environment, collision);
    check_reconciliation_error(rejected_conflict, *reset.history,
        prediction::PredictionErrorCode::conflicting_authoritative_update);

    const auto first_new_command = fixture::make_command(1U);
    const auto first_new_history = append_command(
        *reset.history, first_new_command, environment, collision);
    REQUIRE(first_new_history->newest_command_sequence());
    CHECK(first_new_history->newest_command_sequence()->value() == 1U);
    const auto* first_new_entry = first_new_history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(1U));
    REQUIRE(first_new_entry != nullptr);
    CHECK(first_new_entry->prediction_generation() == 2U);

    const auto* old_first = campaign.history->find_exact(
        *goldsrc::GoldSrcUserCmdSequence::create(1U));
    REQUIRE(old_first != nullptr);
    const auto old_authority = make_authority(
        campaign.session, 99U, 1U, *old_first->post_command_state());
    const auto rejected_old = reconcile(
        *reset.history, old_authority, environment, collision);
    check_reconciliation_error(rejected_old, *reset.history,
        prediction::PredictionErrorCode::prediction_session_mismatch);

    const auto mismatched_session = make_session(
        reset_state, collision, environment, {}, 3U, 3U);
    const auto mismatched_reset_state =
        offset_state(reset_state, {0.0F, 0.5F, 0.0F});
    const auto mismatched_reset = make_hard_reset_authority(
        mismatched_session, 21U, mismatched_reset_state);
    const auto rejected_mismatched_spawn = reconcile(
        *campaign.history, mismatched_reset, environment, collision);
    check_reconciliation_error(rejected_mismatched_spawn, *campaign.history,
        prediction::PredictionErrorCode::hard_reset_generation_required);
}

TEST_CASE("Hard reset rejects session tuple rollback transactionally",
    "[prediction][reconciliation][hard-reset][session-order]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    const auto initial = fixture::make_state();
    const auto session = make_session(
        initial, collision, environment, {}, 5U, 7U);
    const auto history = make_history(initial, session);
    const auto reset_state = fixture::make_state(
        {-24.0F, 12.0F, 36.0F}, {}, movement::PlayerMovementMode::walking,
        movement::PlayerMovementHull::standing, 0U, 0U, 1.0F, 1.0F, 0U,
        1U);

    SECTION("session generation cannot roll back")
    {
        const auto replacement = make_session(
            reset_state, collision, environment, {}, 4U, 100U);
        const auto authority =
            make_hard_reset_authority(replacement, 1U, reset_state);
        const auto result = reconcile(
            *history, authority, environment, collision);
        check_reconciliation_error(result, *history,
            prediction::PredictionErrorCode::hard_reset_generation_required);
    }

    SECTION("prediction generation cannot roll back within a session")
    {
        const auto replacement = make_session(
            reset_state, collision, environment, {}, 5U, 6U);
        const auto authority =
            make_hard_reset_authority(replacement, 1U, reset_state);
        const auto result = reconcile(
            *history, authority, environment, collision);
        check_reconciliation_error(result, *history,
            prediction::PredictionErrorCode::hard_reset_generation_required);
    }
}

TEST_CASE("Hard reset obeys history revision and reconciliation limits",
    "[prediction][reconciliation][hard-reset][limits]")
{
    IdentityCollision collision;
    const auto environment = fixture::make_environment();
    prediction::LocalPredictionHistoryLimits history_limits;
    history_limits.maximum_history_revision = 2U;
    const auto initial = fixture::make_state();
    const auto session = make_session(initial, collision, environment);
    const auto history = make_history(initial, session, history_limits);
    const auto reset_state = fixture::make_state(
        {-24.0F, 12.0F, 36.0F}, {}, movement::PlayerMovementMode::walking,
        movement::PlayerMovementHull::standing, 0U, 0U, 1.0F, 1.0F, 0U,
        1U);
    const auto replacement = make_session(
        reset_state, collision, environment, {}, 1U, 2U);
    const auto authority =
        make_hard_reset_authority(replacement, 1U, reset_state);

    SECTION("history revision limit")
    {
        auto bounded = history_limits;
        bounded.maximum_history_revision = history->revision();
        const auto bounded_history = make_history(initial, session, bounded);
        const auto result = reconcile(
            *bounded_history, authority, environment, collision);
        check_reconciliation_error(result, *bounded_history,
            prediction::PredictionErrorCode::revision_exhausted);
    }

    SECTION("prediction revision limit")
    {
        prediction::PredictionReconciliationConfig config;
        config.limits.maximum_prediction_revision = history->revision();
        const auto result = reconcile(
            *history, authority, environment, collision, {}, config);
        check_reconciliation_error(result, *history,
            prediction::PredictionErrorCode::revision_exhausted);
    }

    SECTION("reconciliation byte limit")
    {
        prediction::PredictionReconciliationConfig config;
        config.limits.maximum_reconciliation_bytes =
            sizeof(movement::LocalPlayerMovementState) - 1U;
        const auto result = reconcile(
            *history, authority, environment, collision, {}, config);
        check_reconciliation_error(result, *history,
            prediction::PredictionErrorCode::prediction_history_full);
    }
}

TEST_CASE("Ten-thousand wall reconciliation publications remain bounded",
    "[prediction][reconciliation][wall][.stress]")
{
    IdentityCollision collision;
    collision.add_positive_x_wall(32.0F);
    const auto environment = fixture::make_environment();
    const auto initial = fixture::make_state(
        {16.0F, 0.0F, 36.0F}, {0.0F, 40.0F, 0.0F});
    const auto session = make_session(initial, collision, environment);
    auto history = make_history(initial, session);
    std::size_t maximum_history_size = 0U;

    for (std::uint32_t value = 1U; value <= 10'000U; ++value) {
        const auto command =
            fixture::make_command(value, 10U, 320.0F, -20.0F);
        history = append_command(*history, command, environment, collision);
        maximum_history_size =
            (std::max)(maximum_history_size, history->size());
        const auto authority_state =
            revision_only_state(*history->current_predicted_state());
        const auto authority = make_authority(
            session, static_cast<std::uint64_t>(value), value,
            authority_state);
        const auto reconciled = reconcile(
            *history, authority, environment, collision);
        REQUIRE(reconciled.history);
        REQUIRE_FALSE(reconciled.error);
        history = reconciled.history;
        maximum_history_size =
            (std::max)(maximum_history_size, history->size());
    }

    CHECK(maximum_history_size == 1U);
    CHECK(history->size() == 0U);
    CHECK(history->anchor().acknowledgement().sequence()->value() == 10'000U);
    CHECK(history->current_predicted_state()->origin().x ==
        Catch::Approx(16.0F).margin(1.0e-4F));
}

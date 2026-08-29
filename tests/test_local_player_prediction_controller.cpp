#include "literal_movement_bsp_fixture.hpp"
#include "local_movement_test_fixture.hpp"

#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <hlclient/local_player/local_player_prediction_controller.hpp>
#include <hlclient/local_player/player_walk_failure_latch.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace collision = hlclient::collision;
namespace fixture = hlclient::tests::local_movement;
namespace gameplay_camera = hlclient::gameplay_camera;
namespace gameplay = hlclient::gameplay_input;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_collision = hlclient::goldsrc::collision;
namespace input = hlclient::input;
namespace literal = hlclient::tests::literal_movement_bsp;
namespace local_player = hlclient::local_player;
namespace movement = hlclient::goldsrc::movement;
namespace player = hlclient::movement;
namespace prediction = hlclient::prediction;

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
literal_world_package()
{
    const auto parsed = bsp::GoldSrcBspParser::parse(
        literal::make_bsp_v30());
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    const auto built = goldsrc_collision::GoldSrcCollisionWorldBuilder::build(
        parsed.document->collision_source);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.package);
    return built.package;
}

[[nodiscard]] std::shared_ptr<const collision::CollisionWorldPackage>
clone_with_source_revision(
    const std::shared_ptr<const collision::CollisionWorldPackage>& source,
    const std::uint64_t source_revision)
{
    REQUIRE(source != nullptr);
    auto identity = source->identity();
    identity.source_revision = source_revision;
    return std::make_shared<const collision::CollisionWorldPackage>(
        std::vector<collision::CollisionPlane>{
            source->planes().begin(), source->planes().end()},
        std::vector<collision::CollisionNode>{
            source->nodes().begin(), source->nodes().end()},
        std::vector<collision::CollisionLeaf>{
            source->leaves().begin(), source->leaves().end()},
        std::vector<collision::CollisionClipnode>{
            source->clipnodes().begin(), source->clipnodes().end()},
        std::vector<collision::CollisionModel>{
            source->models().begin(), source->models().end()},
        identity, source->statistics(), source->compatibility_profile(),
        source->evidence_profile());
}

class FaultInjectingLiteralCollision final
    : public movement::ILocalMovementCollision {
public:
    explicit FaultInjectingLiteralCollision(
        std::shared_ptr<const collision::CollisionWorldPackage> package)
        noexcept
        : world_{std::move(package)}
    {
    }

    void fail_traces(const bool value = true) noexcept
    {
        fail_traces_ = value;
    }

    void mismatch_session_identity(const bool value = true) noexcept
    {
        mismatch_session_identity_ = value;
    }

    void override_session_identity(
        const movement::LocalMovementCollisionProfile profile,
        const std::uint64_t scene_signature) noexcept
    {
        identity_profile_override_ = profile;
        scene_signature_override_ = scene_signature;
    }

    [[nodiscard]] movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return identity_profile_override_.value_or(world_.profile());
    }

    [[nodiscard]] bool valid() const noexcept override
    {
        return world_.valid();
    }

    [[nodiscard]] std::optional<
        movement::LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept override
    {
        auto identity = world_.session_identity();
        if (identity && identity_profile_override_) {
            identity->profile = *identity_profile_override_;
        }
        if (identity && scene_signature_override_) {
            identity->scene_signature = *scene_signature_override_;
        }
        if (identity && mismatch_session_identity_) {
            ++identity->scene_signature;
        }
        return identity;
    }

    [[nodiscard]] movement::LocalMovementPointContentsQueryResult
    point_contents(
        const assets::AssetVector3& point,
        collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return world_.point_contents(point, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementPositionQueryResult test_position(
        const assets::AssetVector3& origin,
        const player::PlayerMovementHull hull,
        collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        return world_.test_position(origin, hull, scratch, config);
    }

    [[nodiscard]] movement::LocalMovementTraceQueryResult trace_hull(
        const assets::AssetVector3& start,
        const assets::AssetVector3& end,
        const player::PlayerMovementHull hull,
        collision::CollisionQueryScratch& scratch,
        const movement::LocalMovementCollisionQueryConfig& config)
        const override
    {
        if (fail_traces_) {
            movement::LocalMovementCollisionError error;
            error.code =
                movement::LocalMovementCollisionErrorCode::world_query_failed;
            return {std::nullopt, error};
        }
        auto traced = world_.trace_hull(start, end, hull, scratch, config);
        if (traced.result) {
            traced.result->collision_profile = profile();
        }
        return traced;
    }

private:
    movement::WorldOnlyMovementCollision world_;
    std::optional<movement::LocalMovementCollisionProfile>
        identity_profile_override_;
    std::optional<std::uint64_t> scene_signature_override_;
    bool fail_traces_{false};
    bool mismatch_session_identity_{false};
};

enum class IntentKind : std::uint8_t {
    neutral,
    forward,
    duck_forward,
    jump_forward,
    focus_lost,
};

[[nodiscard]] gameplay::GameplayInputIntent make_intent(
    const IntentKind kind)
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    if (kind == IntentKind::forward || kind == IntentKind::duck_forward ||
        kind == IntentKind::jump_forward) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::w));
    }
    if (kind == IntentKind::duck_forward) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::left_control));
    }
    if (kind == IntentKind::jump_forward || kind == IntentKind::focus_lost) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::space));
    }
    if (kind == IntentKind::focus_lost) {
        tracker.apply_event(input::InputEvent::focus_lost());
    }
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();

    auto bindings = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    REQUIRE(bindings.bindings);
    auto built = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, {}, 0.010);
    REQUIRE(built);
    REQUIRE(built.intent);
    return std::move(*built.intent);
}

[[nodiscard]] goldsrc::GoldSrcUserCmdSequence sequence(
    const std::uint32_t value)
{
    const auto created = goldsrc::GoldSrcUserCmdSequence::create(value);
    REQUIRE(created);
    return *created;
}

[[nodiscard]] prediction::AuthoritativePlayerState make_authority(
    const prediction::PredictionSessionIdentity& session,
    const std::uint64_t update_ordinal,
    const player::LocalPlayerMovementState& state,
    const prediction::AuthoritativePlayerDiscontinuity discontinuity =
        prediction::AuthoritativePlayerDiscontinuity::normal)
{
    prediction::AuthoritativePlayerUpdateIdentityCreateInfo identity_info;
    identity_info.session = session;
    identity_info.update_ordinal = update_ordinal;
    identity_info.acknowledgement =
        prediction::AuthoritativeCommandAcknowledgement::for_sequence(
            sequence(state.source_command_sequence()));
    identity_info.synthetic_authority_time_nanoseconds =
        static_cast<std::int64_t>(state.simulation_time_nanoseconds());
    identity_info.discontinuity = discontinuity;
    auto identity = prediction::AuthoritativePlayerUpdateIdentity::create(
        identity_info);
    REQUIRE(identity);
    REQUIRE(identity.identity);

    auto authoritative =
        prediction::AuthoritativePlayerState::from_synthetic_complete_state(
            std::move(*identity.identity), state);
    REQUIRE(authoritative);
    REQUIRE(authoritative.state);
    return std::move(*authoritative.state);
}

[[nodiscard]] prediction::AuthoritativePlayerState make_hard_reset_authority(
    const prediction::PredictionSessionIdentity& session,
    const std::uint64_t update_ordinal,
    const player::LocalPlayerMovementState& state)
{
    prediction::AuthoritativePlayerUpdateIdentityCreateInfo identity_info;
    identity_info.session = session;
    identity_info.update_ordinal = update_ordinal;
    identity_info.acknowledgement =
        prediction::AuthoritativeCommandAcknowledgement::none();
    identity_info.synthetic_authority_time_nanoseconds =
        static_cast<std::int64_t>(state.simulation_time_nanoseconds());
    identity_info.discontinuity = prediction::
        AuthoritativePlayerDiscontinuity::respawn_or_hard_reset;
    auto identity = prediction::AuthoritativePlayerUpdateIdentity::create(
        identity_info);
    REQUIRE(identity);
    REQUIRE(identity.identity);
    auto authoritative =
        prediction::AuthoritativePlayerState::from_synthetic_complete_state(
            std::move(*identity.identity), state);
    REQUIRE(authoritative);
    REQUIRE(authoritative.state);
    return std::move(*authoritative.state);
}

[[nodiscard]] player::LocalPlayerMovementState position_corrected_state(
    const player::LocalPlayerMovementState& source,
    const assets::AssetVector3 delta)
{
    auto info = player::local_player_movement_state_create_info(source);
    info.origin.x += delta.x;
    info.origin.y += delta.y;
    info.origin.z += delta.z;
    if (info.ground.grounded) {
        info.ground.contact_position.x += delta.x;
        info.ground.contact_position.y += delta.y;
        info.ground.contact_position.z += delta.z;
    }
    ++info.state_revision;
    auto created = player::LocalPlayerMovementState::create(info);
    REQUIRE(created);
    REQUIRE(created.state);
    return std::move(*created.state);
}

[[nodiscard]] local_player::LocalPlayerMovementControllerConfig
movement_config_with_camera_revision_limit(const std::uint64_t maximum_revision)
{
    gameplay_camera::FirstPersonCameraConfigCreateInfo info;
    info.maximum_camera_revisions = maximum_revision;
    auto created = gameplay_camera::FirstPersonCameraConfig::create(info);
    REQUIRE(created);
    REQUIRE(created.config);
    return {{}, {}, {}, std::move(*created.config)};
}

struct ControllerFixture {
    std::shared_ptr<const collision::CollisionWorldPackage> package{
        literal_world_package()};
    FaultInjectingLiteralCollision collision{package};
    player::LocalPlayerMovementState initial{fixture::make_state()};
    movement::GoldSrcMovementEnvironment environment{
        fixture::make_environment()};
    local_player::LocalPlayerMovementControllerConfig movement_config{};
    prediction::PredictionSessionIdentity session{};

    ControllerFixture()
    {
        initialize_session();
    }

    ControllerFixture(
        const movement::LocalMovementCollisionProfile collision_profile,
        const std::uint64_t scene_signature)
    {
        collision.override_session_identity(
            collision_profile, scene_signature);
        initialize_session();
    }

    void initialize_session()
    {
        const auto identity = collision.session_identity();
        REQUIRE(identity);
        REQUIRE(identity->valid());
        auto created = prediction::create_prediction_session_identity(1U, 1U,
            collision, environment, movement_config.movement, initial);
        REQUIRE(created);
        REQUIRE(created.session);
        session = *created.session;
    }

    [[nodiscard]] local_player::LocalPlayerPredictionController::CreateResult
    create_controller(
        const local_player::LocalPlayerPredictionControllerConfig& config = {})
        const
    {
        local_player::LocalPlayerMovementController movement_controller{
            initial, environment, movement_config};
        return local_player::LocalPlayerPredictionController::create(
            std::move(movement_controller), session, config);
    }

    [[nodiscard]] std::unique_ptr<
        local_player::LocalPlayerPredictionController>
    controller(
        const local_player::LocalPlayerPredictionControllerConfig& config = {})
        const
    {
        auto created = create_controller(config);
        REQUIRE(created);
        REQUIRE(created.controller);
        return std::move(created.controller);
    }
};

void check_prediction_error(
    const local_player::LocalPlayerPredictionController::CreateResult& result,
    const prediction::PredictionErrorCode expected)
{
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.controller);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

void check_same_vector(
    const assets::AssetVector3& actual,
    const assets::AssetVector3& expected,
    const float margin = 1.0e-5F)
{
    CHECK(actual.x == Catch::Approx(expected.x).margin(margin));
    CHECK(actual.y == Catch::Approx(expected.y).margin(margin));
    CHECK(actual.z == Catch::Approx(expected.z).margin(margin));
}

void check_event_types(
    const local_player::LocalPlayerPredictionOperationResult& result,
    const std::initializer_list<
        local_player::LocalPlayerPredictionEventType> expected)
{
    REQUIRE(result.events.size() == expected.size());
    CHECK(result.events.size() <= result.events.capacity());
    std::size_t index = 0U;
    for (const auto type : expected) {
        CAPTURE(index);
        CHECK(result.events[index].type == type);
        ++index;
    }
}

} // namespace

TEST_CASE("Local prediction controller validates creation and BSP session identity",
    "[local-player][prediction][controller][create]")
{
    ControllerFixture values;

    SECTION("a matching literal BSP session creates an empty controller")
    {
        auto created = values.create_controller();
        REQUIRE(created);
        REQUIRE(created.controller);
        CHECK(created.controller->session() == values.session);
        CHECK(created.controller->history()->size() == 0U);
        CHECK(created.controller->history()->session() == values.session);
        CHECK(created.controller->status() ==
            local_player::LocalPlayerPredictionControllerStatus::idle);
    }

    SECTION("a different spawn publication is rejected")
    {
        auto mismatched_initial = fixture::make_state(
            {1.0F, 0.0F, 36.0F});
        local_player::LocalPlayerMovementController movement_controller{
            std::move(mismatched_initial), values.environment,
            values.movement_config};
        const auto created =
            local_player::LocalPlayerPredictionController::create(
                std::move(movement_controller), values.session);
        check_prediction_error(created,
            prediction::PredictionErrorCode::invalid_session_identity);
    }

    SECTION("invalid history limits are rejected")
    {
        local_player::LocalPlayerPredictionControllerConfig config;
        config.history.maximum_entries = 0U;
        const auto created = values.create_controller(config);
        check_prediction_error(created,
            prediction::PredictionErrorCode::invalid_configuration);
    }

    SECTION("the stock evidence-pending profile is not executable")
    {
        auto stock_session = values.session;
        stock_session.prediction_profile = prediction::
            PredictionCompatibilityProfile::
                stock_protocol_48_authoritative_reconciliation_evidence_pending;
        local_player::LocalPlayerMovementController movement_controller{
            values.initial, values.environment, values.movement_config};
        const auto created =
            local_player::LocalPlayerPredictionController::create(
                std::move(movement_controller), stock_session);
        check_prediction_error(created,
            prediction::PredictionErrorCode::stock_evidence_pending);
    }

    SECTION("an active collision identity mismatch fails before prediction")
    {
        auto controller = values.controller();
        const auto state_signature =
            player::local_player_movement_state_signature(
                *controller->history()->current_predicted_state());
        const auto history_revision = controller->history()->revision();
        values.collision.mismatch_session_identity();
        movement::GoldSrcLocalMovementScratch scratch;
        const auto updated = controller->update_local_input(
            0, make_intent(IntentKind::neutral), values.collision, scratch);
        REQUIRE_FALSE(updated);
        REQUIRE(updated.prediction_error);
        CHECK(updated.prediction_error->code ==
            prediction::PredictionErrorCode::collision_world_mismatch);
        CHECK(updated.status ==
            local_player::LocalPlayerPredictionControllerStatus::failed);
        CHECK(controller->history()->revision() == history_revision);
        CHECK(player::local_player_movement_state_signature(
                  *controller->history()->current_predicted_state()) ==
            state_signature);
    }
}

TEST_CASE("Local prediction controller publishes no-command and batched updates",
    "[local-player][prediction][controller][local-update]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch scratch;

    const auto no_command = controller->update_local_input(
        0, make_intent(IntentKind::neutral), values.collision, scratch);
    REQUIRE(no_command);
    CHECK(no_command.command_count == 0U);
    CHECK(no_command.history->size() == 0U);
    CHECK(no_command.status ==
        local_player::LocalPlayerPredictionControllerStatus::
            awaiting_authority);

    const auto multiple = controller->update_local_input(
        30'000'000, make_intent(IntentKind::forward), values.collision,
        scratch);
    REQUIRE(multiple);
    REQUIRE(multiple.event);
    CHECK(multiple.event->type == local_player::
        LocalPlayerPredictionEventType::predicted_command_appended);
    check_event_types(multiple, {
        local_player::LocalPlayerPredictionEventType::
            predicted_command_appended,
        local_player::LocalPlayerPredictionEventType::
            prediction_history_advanced});
    CHECK(multiple.command_count == 3U);
    CHECK(multiple.event->command_count == 3U);
    REQUIRE(multiple.history);
    REQUIRE(multiple.player_state);
    REQUIRE(multiple.camera);
    CHECK(multiple.history->size() == 3U);
    CHECK(multiple.history->entries()[0U].command_sequence().value() == 1U);
    CHECK(multiple.history->entries()[1U].command_sequence().value() == 2U);
    CHECK(multiple.history->entries()[2U].command_sequence().value() == 3U);
    CHECK(multiple.history->current_predicted_state() ==
        multiple.player_state);
    CHECK(multiple.player_state->source_command_sequence() == 3U);
    CHECK(controller->movement_controller().player_state().
            source_command_sequence() == 3U);
    CHECK(controller->statistics().predicted_commands == 3U);
    CHECK(controller->statistics().history_high_water_mark == 3U);
    CHECK(multiple.events[1U].history_revision == multiple.history->revision());
    check_same_vector(multiple.camera->position(),
        controller->movement_controller().camera().position());
}

TEST_CASE("Prediction history backpressure abandons the movement plan atomically",
    "[local-player][prediction][controller][backpressure][one-shot]")
{
    ControllerFixture values;
    local_player::LocalPlayerPredictionControllerConfig config;
    config.history.maximum_entries = 1U;
    config.history.maximum_authority_delay_commands = 1U;
    auto controller = values.controller(config);
    movement::GoldSrcLocalMovementScratch scratch;
    REQUIRE(controller->update_local_input(
        0, make_intent(IntentKind::neutral), values.collision, scratch));

    const auto history_before = controller->history();
    const auto history_signature =
        prediction::local_prediction_history_signature(*history_before);
    const auto state_signature =
        player::local_player_movement_state_signature(
            controller->movement_controller().player_state());
    const auto movement_revision =
        controller->movement_controller().revision();
    const auto camera_revision =
        controller->movement_controller().camera().revision();

    const auto blocked = controller->update_local_input(20'000'000,
        make_intent(IntentKind::jump_forward), values.collision, scratch);
    REQUIRE_FALSE(blocked);
    REQUIRE(blocked.prediction_error);
    CHECK(blocked.prediction_error->code == prediction::PredictionErrorCode::
        prediction_history_backpressure);
    CHECK(blocked.status == local_player::
        LocalPlayerPredictionControllerStatus::history_backpressure);
    REQUIRE(blocked.event);
    CHECK(blocked.event->type ==
        local_player::LocalPlayerPredictionEventType::prediction_backpressure);
    check_event_types(blocked, {
        local_player::LocalPlayerPredictionEventType::prediction_backpressure});
    CHECK(controller->history() == history_before);
    CHECK(prediction::local_prediction_history_signature(
              *controller->history()) == history_signature);
    CHECK(controller->history()->size() == 0U);
    CHECK(player::local_player_movement_state_signature(
              controller->movement_controller().player_state()) ==
        state_signature);
    // Capturing the pending jump edge is the only publication allowed before
    // append preflight; the abandoned movement/scheduler plan adds no second
    // revision.
    CHECK(controller->movement_controller().revision() ==
        movement_revision + 1U);
    CHECK(controller->movement_controller().camera().revision() ==
        camera_revision);
    CHECK(gameplay::gameplay_button_is_set(
        controller->movement_controller().pending_one_shots(),
        gameplay::GameplayButton::jump));
    CHECK(controller->statistics().history_backpressure_count == 1U);
    CHECK(controller->statistics().predicted_commands == 0U);

    // The failed 20 ms batch did not advance the scheduler. Retrying the
    // exact-limit 10 ms command commits the retained edge exactly once.
    const auto retry = controller->update_local_input(10'000'000,
        make_intent(IntentKind::neutral), values.collision, scratch);
    REQUIRE(retry);
    CHECK(retry.command_count == 1U);
    REQUIRE(retry.history);
    REQUIRE(retry.history->find_exact(sequence(1U)) != nullptr);
    const auto* entry = retry.history->find_exact(sequence(1U));
    REQUIRE(entry != nullptr);
    CHECK((entry->command()->buttons() &
              goldsrc::kSyntheticGoldSrcButtonJump) != 0U);
    CHECK(controller->movement_controller().pending_one_shots() == 0U);
    CHECK(controller->statistics().predicted_commands == 1U);
}

TEST_CASE("Prediction controller clears pending one-shots on focus loss",
    "[local-player][prediction][controller][focus-loss][one-shot]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch scratch;

    const auto captured = controller->update_local_input(0,
        make_intent(IntentKind::jump_forward), values.collision, scratch);
    REQUIRE(captured);
    CHECK(captured.command_count == 0U);
    CHECK(gameplay::gameplay_button_is_set(
        controller->movement_controller().pending_one_shots(),
        gameplay::GameplayButton::jump));

    const auto lost = controller->update_local_input(5'000'000,
        make_intent(IntentKind::focus_lost), values.collision, scratch);
    REQUIRE(lost);
    CHECK(lost.command_count == 0U);
    CHECK(controller->movement_controller().pending_one_shots() == 0U);

    const auto next = controller->update_local_input(10'000'000,
        make_intent(IntentKind::neutral), values.collision, scratch);
    REQUIRE(next);
    CHECK(next.command_count == 1U);
    REQUIRE(next.history);
    const auto* entry = next.history->find_exact(sequence(1U));
    REQUIRE(entry != nullptr);
    CHECK((entry->command()->buttons() &
              goldsrc::kSyntheticGoldSrcButtonJump) == 0U);
    CHECK(entry->simulation_statistics().jump_count == 0U);
}

TEST_CASE("Prediction controller accepts exact duplicate and stale authority",
    "[local-player][prediction][controller][authority]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch scratch;
    REQUIRE(controller->update_local_input(
        0, make_intent(IntentKind::neutral), values.collision, scratch));
    REQUIRE(controller->update_local_input(10'000'000,
        make_intent(IntentKind::forward), values.collision, scratch));
    const auto* entry = controller->history()->find_exact(sequence(1U));
    REQUIRE(entry != nullptr);
    REQUIRE(entry->post_command_state());
    const auto exact_state = *entry->post_command_state();
    const auto authority = make_authority(values.session, 2U, exact_state);

    const auto exact = controller->apply_authoritative_state(
        authority, 0.200, values.collision, scratch);
    REQUIRE(exact);
    REQUIRE(exact.event);
    CHECK(exact.event->type == local_player::
        LocalPlayerPredictionEventType::acknowledgement_accepted);
    CHECK(exact.event->correction_class ==
        prediction::PredictionCorrectionClass::exact);
    check_event_types(exact, {
        local_player::LocalPlayerPredictionEventType::
            authoritative_update_received,
        local_player::LocalPlayerPredictionEventType::
            acknowledgement_accepted,
        local_player::LocalPlayerPredictionEventType::misprediction_measured,
        local_player::LocalPlayerPredictionEventType::history_trimmed,
        local_player::LocalPlayerPredictionEventType::
            prediction_history_advanced});
    CHECK(exact.events[3U].command_count == 1U);
    CHECK(exact.events[4U].history_revision == exact.history->revision());
    CHECK(exact.status == local_player::
        LocalPlayerPredictionControllerStatus::replay_complete);
    CHECK(exact.history->size() == 0U);
    REQUIRE(exact.history->anchor().authority_update_ordinal());
    CHECK(*exact.history->anchor().authority_update_ordinal() == 2U);
    CHECK(controller->statistics().accepted_acknowledgements == 1U);
    CHECK(controller->statistics().exact_match_count == 1U);

    const auto accepted_history = controller->history();
    const auto accepted_state_signature =
        player::local_player_movement_state_signature(
            *controller->history()->current_predicted_state());
    const auto duplicate = controller->apply_authoritative_state(
        authority, 0.210, values.collision, scratch);
    REQUIRE(duplicate);
    CHECK(duplicate.status == local_player::
        LocalPlayerPredictionControllerStatus::awaiting_authority);
    check_event_types(duplicate, {
        local_player::LocalPlayerPredictionEventType::
            authoritative_update_received});
    CHECK(controller->history() == accepted_history);
    CHECK(controller->statistics().duplicate_updates == 1U);
    CHECK(controller->statistics().accepted_acknowledgements == 1U);

    const auto stale_authority =
        make_authority(values.session, 1U, exact_state);
    const auto stale = controller->apply_authoritative_state(
        stale_authority, 0.220, values.collision, scratch);
    REQUIRE(stale);
    REQUIRE(stale.event);
    CHECK(stale.event->type == local_player::LocalPlayerPredictionEventType::
        authoritative_update_ignored_stale);
    check_event_types(stale, {
        local_player::LocalPlayerPredictionEventType::
            authoritative_update_received,
        local_player::LocalPlayerPredictionEventType::
            authoritative_update_ignored_stale});
    CHECK(stale.status ==
        local_player::LocalPlayerPredictionControllerStatus::authority_stale);
    CHECK(controller->history() == accepted_history);
    CHECK(player::local_player_movement_state_signature(
              *controller->history()->current_predicted_state()) ==
        accepted_state_signature);
    CHECK(controller->statistics().stale_updates == 1U);
    CHECK(controller->statistics().authoritative_updates == 3U);
}

TEST_CASE("Corrected authority replays commands and smooths only the camera",
    "[local-player][prediction][controller][replay][visual]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch movement_scratch;
    REQUIRE(controller->update_local_input(0,
        make_intent(IntentKind::neutral), values.collision, movement_scratch));
    REQUIRE(controller->update_local_input(30'000'000,
        make_intent(IntentKind::forward), values.collision, movement_scratch));
    REQUIRE(controller->history()->size() == 3U);
    const auto* first = controller->history()->find_exact(sequence(1U));
    REQUIRE(first != nullptr);
    REQUIRE(first->post_command_state());
    const auto corrected_after_first = position_corrected_state(
        *first->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto authority =
        make_authority(values.session, 1U, corrected_after_first);
    const auto old_displayed_eye =
        controller->movement_controller().camera().position();

    const auto reconciled = controller->apply_authoritative_state(
        authority, 1.0, values.collision, movement_scratch);
    REQUIRE(reconciled);
    REQUIRE(reconciled.event);
    REQUIRE(reconciled.player_state);
    REQUIRE(reconciled.camera);
    CHECK(reconciled.event->type == local_player::
        LocalPlayerPredictionEventType::command_replay_completed);
    CHECK(reconciled.event->correction_class == prediction::
        PredictionCorrectionClass::small_visual_correction);
    check_event_types(reconciled, {
        local_player::LocalPlayerPredictionEventType::
            authoritative_update_received,
        local_player::LocalPlayerPredictionEventType::
            acknowledgement_accepted,
        local_player::LocalPlayerPredictionEventType::misprediction_measured,
        local_player::LocalPlayerPredictionEventType::command_replay_started,
        local_player::LocalPlayerPredictionEventType::command_replayed,
        local_player::LocalPlayerPredictionEventType::command_replayed,
        local_player::LocalPlayerPredictionEventType::command_replay_completed,
        local_player::LocalPlayerPredictionEventType::history_trimmed,
        local_player::LocalPlayerPredictionEventType::
            prediction_history_advanced,
        local_player::LocalPlayerPredictionEventType::
            visual_correction_started,
        local_player::LocalPlayerPredictionEventType::
            visual_correction_active});
    REQUIRE(reconciled.events[4U].command_sequence);
    CHECK(reconciled.events[4U].command_sequence->value() == 2U);
    REQUIRE(reconciled.events[5U].command_sequence);
    CHECK(reconciled.events[5U].command_sequence->value() == 3U);
    CHECK(reconciled.events[3U].command_count == 2U);
    CHECK(reconciled.events[6U].command_count == 2U);
    CHECK(reconciled.replay_depth == 2U);
    CHECK(reconciled.status == local_player::
        LocalPlayerPredictionControllerStatus::visual_correction_active);
    CHECK(reconciled.history->size() == 2U);
    REQUIRE(reconciled.history->newest_command_sequence());
    CHECK(reconciled.history->newest_command_sequence()->value() == 3U);
    CHECK(reconciled.player_state->source_command_sequence() == 3U);
    CHECK(reconciled.player_state->origin().y ==
        Catch::Approx(0.25F).margin(1.0e-4F));
    CHECK(controller->visual_correction().active());
    CHECK(controller->statistics().replay_count == 1U);
    CHECK(controller->statistics().replayed_command_count == 2U);
    CHECK(controller->statistics().maximum_replay_depth == 2U);
    CHECK(controller->statistics().small_corrections == 1U);

    const auto corrected_state_signature =
        player::local_player_movement_state_signature(
            controller->movement_controller().player_state());
    const auto corrected_physical_eye =
        controller->movement_controller().camera().position();
    const auto history_signature =
        prediction::local_prediction_history_signature(*controller->history());
    collision::CollisionWorldQuery visual_query{values.package};
    collision::CollisionQueryScratch visual_scratch;

    const auto start = controller->sample_camera(
        1.0, &visual_query, visual_scratch);
    REQUIRE(start);
    check_event_types(start, {
        local_player::LocalPlayerPredictionEventType::
            visual_correction_active});
    REQUIRE(start.camera);
    check_same_vector(start.camera->position(), old_displayed_eye, 1.0e-4F);
    CHECK(player::local_player_movement_state_signature(
              controller->movement_controller().player_state()) ==
        corrected_state_signature);
    check_same_vector(controller->movement_controller().camera().position(),
        corrected_physical_eye);
    const auto start_camera_revision = start.camera->revision();
    CHECK(controller->visual_correction().camera_publication_revision() ==
        start_camera_revision);

    const auto midpoint = controller->sample_camera(
        1.050, &visual_query, visual_scratch);
    REQUIRE(midpoint);
    check_event_types(midpoint, {
        local_player::LocalPlayerPredictionEventType::
            visual_correction_active});
    REQUIRE(midpoint.camera);
    CHECK(midpoint.camera->position().y ==
        Catch::Approx(0.125F).margin(1.0e-4F));
    CHECK(prediction::local_prediction_history_signature(
              *controller->history()) == history_signature);
    const auto midpoint_camera_revision = midpoint.camera->revision();
    CHECK(start_camera_revision < midpoint_camera_revision);
    CHECK(controller->visual_correction().camera_publication_revision() ==
        midpoint_camera_revision);

    const auto completion = controller->sample_camera(
        1.101, &visual_query, visual_scratch);
    REQUIRE(completion);
    REQUIRE(completion.camera);
    REQUIRE(completion.event);
    CHECK(completion.event->type == local_player::
        LocalPlayerPredictionEventType::visual_correction_completed);
    check_event_types(completion, {
        local_player::LocalPlayerPredictionEventType::
            visual_correction_completed});
    CHECK(completion.status == local_player::
        LocalPlayerPredictionControllerStatus::awaiting_authority);
    check_same_vector(completion.camera->position(), corrected_physical_eye);
    CHECK(midpoint_camera_revision < completion.camera->revision());
    CHECK(completion.camera->revision() <= controller->movement_controller()
        .config().camera.maximum_camera_revisions());
    CHECK(controller->visual_correction().camera_publication_revision() ==
        completion.camera->revision());
    CHECK_FALSE(controller->visual_correction().active());
    CHECK(player::local_player_movement_state_signature(
              controller->movement_controller().player_state()) ==
        corrected_state_signature);
    CHECK(controller->statistics().authoritative_updates == 1U);
    CHECK(controller->statistics().replayed_command_count == 2U);

    const auto inactive = controller->sample_camera(
        1.200, nullptr, visual_scratch);
    REQUIRE(inactive);
    REQUIRE(inactive.camera);
    CHECK_FALSE(inactive.event);
    check_event_types(inactive, {});
    CHECK(inactive.camera->revision() == completion.camera->revision());

    const auto next_update = controller->update_local_input(40'000'000,
        make_intent(IntentKind::forward), values.collision, movement_scratch);
    REQUIRE(next_update);
    REQUIRE(next_update.camera);
    CHECK(inactive.camera->revision() < next_update.camera->revision());
    check_same_vector(next_update.camera->position(),
        controller->movement_controller().camera().position());
    CHECK(controller->visual_correction().camera_publication_revision() ==
        next_update.camera->revision());
}

TEST_CASE("Prediction controller publishes a teleport snap in its first sample",
    "[local-player][prediction][controller][visual][teleport][one-sample]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch movement_scratch;
    REQUIRE(controller->update_local_input(0,
        make_intent(IntentKind::neutral), values.collision, movement_scratch));
    const auto predicted = controller->update_local_input(10'000'000,
        make_intent(IntentKind::neutral), values.collision, movement_scratch);
    REQUIRE(predicted);
    REQUIRE(predicted.camera);
    const auto old_displayed_eye = predicted.camera->position();
    const auto* first = controller->history()->find_exact(sequence(1U));
    REQUIRE(first != nullptr);
    REQUIRE(first->post_command_state());
    const auto teleported = position_corrected_state(
        *first->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto authority = make_authority(values.session, 1U, teleported,
        prediction::AuthoritativePlayerDiscontinuity::teleport);

    const auto reconciled = controller->apply_authoritative_state(
        authority, 1.0, values.collision, movement_scratch);
    REQUIRE(reconciled);
    REQUIRE(reconciled.event);
    REQUIRE(reconciled.camera);
    CHECK(reconciled.event->correction_class ==
        prediction::PredictionCorrectionClass::teleport_snap);
    CHECK(controller->statistics().teleports == 1U);
    CHECK_FALSE(controller->visual_correction().active());
    const auto& physical_camera =
        controller->movement_controller().camera();
    check_same_vector(reconciled.camera->position(), physical_camera.position());
    CHECK(reconciled.camera->yaw_degrees() == physical_camera.yaw_degrees());
    CHECK(reconciled.camera->pitch_degrees() == physical_camera.pitch_degrees());
    CHECK(reconciled.camera->position().y !=
        Catch::Approx(old_displayed_eye.y).margin(1.0e-4F));

    collision::CollisionQueryScratch visual_scratch;
    const auto first_render_sample = controller->sample_camera(
        1.0, nullptr, visual_scratch);
    REQUIRE(first_render_sample);
    REQUIRE(first_render_sample.camera);
    CHECK_FALSE(first_render_sample.event);
    check_same_vector(
        first_render_sample.camera->position(), physical_camera.position());
    CHECK(first_render_sample.camera->revision() ==
        reconciled.camera->revision());
    CHECK_FALSE(controller->visual_correction().active());
}

TEST_CASE("Prediction controller enforces the configured visual camera revision limit",
    "[local-player][prediction][controller][visual][revision]")
{
    constexpr std::uint64_t maximum_camera_revision = 5U;
    ControllerFixture values;
    local_player::LocalPlayerMovementController movement_controller{
        values.initial, values.environment,
        movement_config_with_camera_revision_limit(maximum_camera_revision)};
    auto created = local_player::LocalPlayerPredictionController::create(
        std::move(movement_controller), values.session);
    REQUIRE(created);
    REQUIRE(created.controller);
    auto controller = std::move(created.controller);

    movement::GoldSrcLocalMovementScratch movement_scratch;
    REQUIRE(controller->update_local_input(0,
        make_intent(IntentKind::neutral), values.collision, movement_scratch));
    REQUIRE(controller->update_local_input(20'000'000,
        make_intent(IntentKind::forward), values.collision, movement_scratch));
    const auto* first = controller->history()->find_exact(sequence(1U));
    REQUIRE(first != nullptr);
    REQUIRE(first->post_command_state());
    const auto authority = make_authority(values.session, 1U,
        position_corrected_state(
            *first->post_command_state(), {0.0F, 0.25F, 0.0F}));
    const auto reconciled = controller->apply_authoritative_state(
        authority, 1.0, values.collision, movement_scratch);
    REQUIRE(reconciled);
    REQUIRE(controller->visual_correction().active());

    SECTION("the camera limit is enforced transactionally")
    {
        collision::CollisionWorldQuery visual_query{values.package};
        collision::CollisionQueryScratch visual_scratch;
        const auto start = controller->sample_camera(
            1.0, &visual_query, visual_scratch);
        REQUIRE(start);
        REQUIRE(start.camera);
        CHECK(start.camera->revision() < maximum_camera_revision);
        const auto quarter = controller->sample_camera(
            1.025, &visual_query, visual_scratch);
        REQUIRE(quarter);
        REQUIRE(quarter.camera);
        const auto at_limit = controller->sample_camera(
            1.050, &visual_query, visual_scratch);
        REQUIRE(at_limit);
        REQUIRE(at_limit.camera);
        CHECK(at_limit.camera->revision() == maximum_camera_revision);
        CHECK(controller->visual_correction().camera_publication_revision() ==
            maximum_camera_revision);

        const auto residual_before_failure =
            controller->visual_correction().current_residual_offset();
        const auto exhausted = controller->sample_camera(
            1.075, &visual_query, visual_scratch);
        REQUIRE_FALSE(exhausted);
        REQUIRE(exhausted.prediction_error);
        CHECK(exhausted.prediction_error->code ==
            prediction::PredictionErrorCode::revision_exhausted);
        CHECK(exhausted.status ==
            local_player::LocalPlayerPredictionControllerStatus::failed);
        check_event_types(exhausted, {
            local_player::LocalPlayerPredictionEventType::
                reconciliation_failed});
        CHECK(controller->visual_correction().camera_publication_revision() ==
            maximum_camera_revision);
        check_same_vector(
            controller->visual_correction().current_residual_offset(),
            residual_before_failure);
    }

    SECTION("active smoothing reserves presentation revision before movement commit")
    {
        collision::CollisionWorldQuery visual_query{values.package};
        collision::CollisionQueryScratch visual_scratch;
        REQUIRE(controller->sample_camera(
            1.0, &visual_query, visual_scratch));
        REQUIRE(controller->sample_camera(
            1.025, &visual_query, visual_scratch));
        const auto at_limit = controller->sample_camera(
            1.050, &visual_query, visual_scratch);
        REQUIRE(at_limit);
        REQUIRE(at_limit.camera);
        CHECK(at_limit.camera->revision() == maximum_camera_revision);
        REQUIRE(controller->visual_correction().active());

        const auto movement_signature_before =
            player::local_player_movement_state_signature(
                controller->movement_controller().player_state());
        const auto movement_revision_before =
            controller->movement_controller().revision();
        const auto physical_camera_revision_before =
            controller->movement_controller().camera().revision();
        const auto pending_one_shots_before =
            controller->movement_controller().pending_one_shots();
        const auto history_before = controller->history();
        const auto history_signature_before =
            prediction::local_prediction_history_signature(*history_before);
        const auto statistics_before = controller->statistics();
        const auto residual_before =
            controller->visual_correction().current_residual_offset();
        const auto sample_time_before =
            controller->visual_correction().last_sample_monotonic_time_seconds();

        const auto exhausted = controller->update_local_input(40'000'000,
            make_intent(IntentKind::forward), values.collision,
            movement_scratch);
        REQUIRE_FALSE(exhausted);
        REQUIRE(exhausted.prediction_error);
        CHECK(exhausted.prediction_error->code ==
            prediction::PredictionErrorCode::revision_exhausted);
        check_event_types(exhausted, {
            local_player::LocalPlayerPredictionEventType::
                reconciliation_failed});
        CHECK(controller->movement_controller().revision() ==
            movement_revision_before);
        CHECK(player::local_player_movement_state_signature(
                  controller->movement_controller().player_state()) ==
            movement_signature_before);
        CHECK(controller->movement_controller().camera().revision() ==
            physical_camera_revision_before);
        CHECK(controller->movement_controller().pending_one_shots() ==
            pending_one_shots_before);
        CHECK(controller->history() == history_before);
        CHECK(prediction::local_prediction_history_signature(
                  *controller->history()) == history_signature_before);
        CHECK(controller->statistics().predicted_commands ==
            statistics_before.predicted_commands);
        CHECK(controller->statistics().history_high_water_mark ==
            statistics_before.history_high_water_mark);
        CHECK(controller->visual_correction().camera_publication_revision() ==
            maximum_camera_revision);
        CHECK(controller->visual_correction()
                  .last_sample_monotonic_time_seconds() == sample_time_before);
        check_same_vector(
            controller->visual_correction().current_residual_offset(),
            residual_before);
    }

    SECTION("a post-completion physical update preflights the presentation cap")
    {
        collision::CollisionWorldQuery visual_query{values.package};
        collision::CollisionQueryScratch visual_scratch;
        REQUIRE(controller->sample_camera(
            1.0, &visual_query, visual_scratch));
        REQUIRE(controller->sample_camera(
            1.050, &visual_query, visual_scratch));
        const auto completion = controller->sample_camera(
            1.101, &visual_query, visual_scratch);
        REQUIRE(completion);
        REQUIRE(completion.camera);
        REQUIRE(completion.event);
        CHECK(completion.camera->revision() == maximum_camera_revision);
        REQUIRE_FALSE(controller->visual_correction().active());

        const auto movement_signature_before =
            player::local_player_movement_state_signature(
                controller->movement_controller().player_state());
        const auto movement_revision_before =
            controller->movement_controller().revision();
        const auto history_before = controller->history();
        const auto history_signature_before =
            prediction::local_prediction_history_signature(*history_before);
        const auto physical_camera_revision_before =
            controller->movement_controller().camera().revision();

        const auto exhausted = controller->update_local_input(40'000'000,
            make_intent(IntentKind::forward), values.collision,
            movement_scratch);
        REQUIRE_FALSE(exhausted);
        REQUIRE(exhausted.prediction_error);
        CHECK(exhausted.prediction_error->code ==
            prediction::PredictionErrorCode::revision_exhausted);
        check_event_types(exhausted, {
            local_player::LocalPlayerPredictionEventType::
                reconciliation_failed});
        CHECK(controller->movement_controller().revision() ==
            movement_revision_before);
        CHECK(player::local_player_movement_state_signature(
                  controller->movement_controller().player_state()) ==
            movement_signature_before);
        CHECK(controller->movement_controller().camera().revision() ==
            physical_camera_revision_before);
        CHECK(controller->history() == history_before);
        CHECK(prediction::local_prediction_history_signature(
                  *controller->history()) == history_signature_before);
        CHECK(controller->visual_correction().camera_publication_revision() ==
            maximum_camera_revision);
    }

    SECTION("an otherwise identical raw camera world with another revision is rejected")
    {
        const auto mismatched_revision =
            values.package->identity().source_revision + 1U;
        const collision::CollisionWorldQuery mismatched_query{
            clone_with_source_revision(values.package, mismatched_revision)};
        collision::CollisionQueryScratch visual_scratch;
        const auto residual_before_failure =
            controller->visual_correction().current_residual_offset();

        const auto mismatched = controller->sample_camera(
            1.0, &mismatched_query, visual_scratch);
        REQUIRE_FALSE(mismatched);
        REQUIRE(mismatched.prediction_error);
        CHECK(mismatched.prediction_error->code ==
            prediction::PredictionErrorCode::collision_world_mismatch);
        CHECK(mismatched.status ==
            local_player::LocalPlayerPredictionControllerStatus::failed);
        check_event_types(mismatched, {
            local_player::LocalPlayerPredictionEventType::
                reconciliation_failed});
        CHECK(controller->visual_correction().camera_publication_revision() <
            maximum_camera_revision);
        check_same_vector(
            controller->visual_correction().current_residual_offset(),
            residual_before_failure);
    }
}

TEST_CASE("Active prediction camera requires the complete world-only session identity",
    "[local-player][prediction][controller][visual][collision][identity]")
{
    const auto exercise_rejection = [](
        const movement::LocalMovementCollisionProfile session_profile,
        const std::uint64_t session_scene_signature) {
        ControllerFixture values{session_profile, session_scene_signature};
        auto controller = values.controller();
        movement::GoldSrcLocalMovementScratch movement_scratch;
        REQUIRE(controller->update_local_input(0,
            make_intent(IntentKind::neutral), values.collision,
            movement_scratch));
        REQUIRE(controller->update_local_input(20'000'000,
            make_intent(IntentKind::forward), values.collision,
            movement_scratch));
        const auto* first =
            controller->history()->find_exact(sequence(1U));
        REQUIRE(first != nullptr);
        REQUIRE(first->post_command_state());
        const auto authority = make_authority(values.session, 1U,
            position_corrected_state(
                *first->post_command_state(), {0.0F, 0.25F, 0.0F}));
        REQUIRE(controller->apply_authoritative_state(
            authority, 1.0, values.collision, movement_scratch));
        REQUIRE(controller->visual_correction().active());

        const auto movement_signature_before =
            player::local_player_movement_state_signature(
                controller->movement_controller().player_state());
        const auto movement_revision_before =
            controller->movement_controller().revision();
        const auto history_before = controller->history();
        const auto history_signature_before =
            prediction::local_prediction_history_signature(*history_before);
        const auto residual_before =
            controller->visual_correction().current_residual_offset();
        const auto publication_revision_before =
            controller->visual_correction().camera_publication_revision();
        collision::CollisionWorldQuery raw_world_query{values.package};
        collision::CollisionQueryScratch visual_scratch;

        const auto rejected = controller->sample_camera(
            1.0, &raw_world_query, visual_scratch);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.prediction_error);
        CHECK(rejected.prediction_error->code ==
            prediction::PredictionErrorCode::collision_world_mismatch);
        CHECK(rejected.status ==
            local_player::LocalPlayerPredictionControllerStatus::failed);
        check_event_types(rejected, {
            local_player::LocalPlayerPredictionEventType::
                reconciliation_failed});
        CHECK(controller->movement_controller().revision() ==
            movement_revision_before);
        CHECK(player::local_player_movement_state_signature(
                  controller->movement_controller().player_state()) ==
            movement_signature_before);
        CHECK(controller->history() == history_before);
        CHECK(prediction::local_prediction_history_signature(
                  *controller->history()) == history_signature_before);
        CHECK(controller->visual_correction().camera_publication_revision() ==
            publication_revision_before);
        check_same_vector(
            controller->visual_correction().current_residual_offset(),
            residual_before);
    };

    SECTION("a world-only query with a different scene signature is rejected")
    {
        exercise_rejection(
            movement::LocalMovementCollisionProfile::world_only_v1, 2U);
    }

    SECTION("a non-world-only session rejects the raw world-only query")
    {
        exercise_rejection(movement::LocalMovementCollisionProfile::
                explicit_synthetic_static_brush_v1,
            0x1234U);
    }
}

TEST_CASE("Hard reset clears active smoothing and the old command generation",
    "[local-player][prediction][controller][hard-reset][visual]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch movement_scratch;
    REQUIRE(controller->update_local_input(0,
        make_intent(IntentKind::neutral), values.collision, movement_scratch));
    REQUIRE(controller->update_local_input(20'000'000,
        make_intent(IntentKind::forward), values.collision, movement_scratch));
    const auto* first = controller->history()->find_exact(sequence(1U));
    REQUIRE(first != nullptr);
    const auto corrected_after_first = position_corrected_state(
        *first->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto correction =
        make_authority(values.session, 1U, corrected_after_first);
    const auto reconciled = controller->apply_authoritative_state(
        correction, 1.0, values.collision, movement_scratch);
    REQUIRE(reconciled);
    REQUIRE(controller->visual_correction().active());
    REQUIRE(controller->history()->size() == 1U);

    const auto replacement_session =
        prediction::create_prediction_session_identity(2U, 2U,
            values.collision, values.environment,
            values.movement_config.movement, values.initial);
    REQUIRE(replacement_session);
    REQUIRE(replacement_session.session);
    const auto hard_reset = make_hard_reset_authority(
        *replacement_session.session, 2U, values.initial);
    const auto reset = controller->apply_authoritative_state(
        hard_reset, 1.025, values.collision, movement_scratch);

    REQUIRE(reset);
    REQUIRE(reset.event);
    CHECK(reset.event->type ==
        local_player::LocalPlayerPredictionEventType::hard_reset_applied);
    check_event_types(reset, {
        local_player::LocalPlayerPredictionEventType::
            authoritative_update_received,
        local_player::LocalPlayerPredictionEventType::hard_reset_applied,
        local_player::LocalPlayerPredictionEventType::history_trimmed,
        local_player::LocalPlayerPredictionEventType::
            prediction_history_advanced});
    CHECK(reset.status ==
        local_player::LocalPlayerPredictionControllerStatus::hard_reset);
    CHECK_FALSE(controller->visual_correction().active());
    CHECK(controller->history()->size() == 0U);
    CHECK(controller->session() == *replacement_session.session);
    CHECK_FALSE(controller->history()->anchor().acknowledgement().has_sequence());
    CHECK(player::local_player_movement_state_signature(
              controller->movement_controller().player_state()) ==
        player::local_player_movement_state_signature(values.initial));
    CHECK(controller->statistics().hard_resets == 1U);
    CHECK(controller->statistics().accepted_acknowledgements == 1U);
    CHECK(reset.events[2U].command_count == 1U);
}

TEST_CASE("Composed prediction route replays, smooths, snaps, resets, and continues",
    "[local-player][prediction][controller][integration][full-route]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch movement_scratch;
    collision::CollisionQueryScratch visual_scratch;
    collision::CollisionWorldQuery visual_query{values.package};

    REQUIRE(controller->update_local_input(0,
        make_intent(IntentKind::neutral), values.collision,
        movement_scratch));
    REQUIRE(controller->update_local_input(10'000'000,
        make_intent(IntentKind::forward), values.collision,
        movement_scratch));
    const auto queued_jump = controller->update_local_input(11'000'000,
        make_intent(IntentKind::jump_forward), values.collision,
        movement_scratch);
    REQUIRE(queued_jump);
    CHECK(queued_jump.command_count == 0U);
    const auto second_command = controller->update_local_input(20'000'000,
        make_intent(IntentKind::duck_forward), values.collision,
        movement_scratch);
    REQUIRE(second_command);
    CHECK(second_command.command_count == 1U);
    REQUIRE(controller->history()->size() == 2U);
    const auto* first = controller->history()->find_exact(sequence(1U));
    const auto* second = controller->history()->find_exact(sequence(2U));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first->post_command_state());
    REQUIRE(second->command());
    CHECK((second->command()->buttons() &
              goldsrc::kSyntheticGoldSrcButtonJump) != 0U);
    CHECK((second->command()->buttons() &
              goldsrc::kSyntheticGoldSrcButtonDuck) != 0U);
    CHECK(second->simulation_statistics().jump_count == 1U);
    CHECK(second->simulation_statistics().duck_enter_count == 1U);
    const auto replayed_command = second->command();
    const auto original_jump_count =
        second->simulation_statistics().jump_count;
    const auto original_duck_enter_count =
        second->simulation_statistics().duck_enter_count;
    const auto original_duck_exit_count =
        second->simulation_statistics().duck_exit_count;

    const auto corrected_after_first = position_corrected_state(
        *first->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto delayed_correction =
        make_authority(values.session, 1U, corrected_after_first);
    const auto corrected = controller->apply_authoritative_state(
        delayed_correction, 1.0, values.collision, movement_scratch);
    REQUIRE(corrected);
    CHECK(corrected.replay_depth == 1U);
    CHECK(corrected.status == local_player::
        LocalPlayerPredictionControllerStatus::visual_correction_active);
    REQUIRE(controller->visual_correction().active());
    const auto* replayed_second =
        controller->history()->find_exact(sequence(2U));
    REQUIRE(replayed_second != nullptr);
    CHECK(replayed_second->command() == replayed_command);
    CHECK(replayed_second->simulation_statistics().jump_count ==
        original_jump_count);
    CHECK(replayed_second->simulation_statistics().duck_enter_count ==
        original_duck_enter_count);
    CHECK(replayed_second->simulation_statistics().duck_exit_count ==
        original_duck_exit_count);
    std::size_t replayed_event_count = 0U;
    for (const auto& event : corrected.events) {
        if (event.type == local_player::
                LocalPlayerPredictionEventType::command_replayed) {
            ++replayed_event_count;
            REQUIRE(event.command_sequence);
            CHECK(event.command_sequence->value() == 2U);
        }
    }
    CHECK(replayed_event_count == 1U);

    const auto smoothing_start = controller->sample_camera(
        1.0, &visual_query, visual_scratch);
    const auto smoothing_midpoint = controller->sample_camera(
        1.050, &visual_query, visual_scratch);
    const auto smoothing_complete = controller->sample_camera(
        1.101, &visual_query, visual_scratch);
    REQUIRE(smoothing_start);
    REQUIRE(smoothing_midpoint);
    REQUIRE(smoothing_complete);
    REQUIRE(smoothing_start.camera);
    REQUIRE(smoothing_midpoint.camera);
    REQUIRE(smoothing_complete.camera);
    CHECK(smoothing_start.camera->revision() <
        smoothing_midpoint.camera->revision());
    CHECK(smoothing_midpoint.camera->revision() <
        smoothing_complete.camera->revision());
    CHECK_FALSE(controller->visual_correction().active());

    const auto continued = controller->update_local_input(30'000'000,
        make_intent(IntentKind::forward), values.collision,
        movement_scratch);
    REQUIRE(continued);
    CHECK(continued.command_count == 1U);
    REQUIRE(controller->history()->find_exact(sequence(3U)) != nullptr);
    const auto* acknowledged_second =
        controller->history()->find_exact(sequence(2U));
    REQUIRE(acknowledged_second != nullptr);
    const auto teleport = make_authority(values.session, 2U,
        *acknowledged_second->post_command_state(),
        prediction::AuthoritativePlayerDiscontinuity::teleport);
    const auto snapped = controller->apply_authoritative_state(
        teleport, 1.2, values.collision, movement_scratch);
    REQUIRE(snapped);
    REQUIRE(snapped.event);
    CHECK(snapped.replay_depth == 1U);
    CHECK(snapped.event->correction_class ==
        prediction::PredictionCorrectionClass::teleport_snap);
    CHECK_FALSE(controller->visual_correction().active());
    CHECK(controller->statistics().teleports == 1U);
    CHECK(controller->statistics().replay_count == 2U);
    CHECK(controller->statistics().replayed_command_count == 2U);
    REQUIRE(controller->history()->size() == 1U);

    const auto continued_after_snap = controller->update_local_input(
        40'000'000, make_intent(IntentKind::forward), values.collision,
        movement_scratch);
    REQUIRE(continued_after_snap);
    CHECK(continued_after_snap.command_count == 1U);
    REQUIRE(controller->history()->size() == 2U);

    const auto replacement_session =
        prediction::create_prediction_session_identity(2U, 2U,
            values.collision, values.environment,
            values.movement_config.movement, values.initial);
    REQUIRE(replacement_session);
    REQUIRE(replacement_session.session);
    const auto hard_reset = make_hard_reset_authority(
        *replacement_session.session, 3U, values.initial);
    const auto reset = controller->apply_authoritative_state(
        hard_reset, 1.3, values.collision, movement_scratch);
    REQUIRE(reset);
    CHECK(reset.status ==
        local_player::LocalPlayerPredictionControllerStatus::hard_reset);
    CHECK_FALSE(controller->visual_correction().active());
    CHECK(controller->history()->size() == 0U);
    CHECK(controller->session() == *replacement_session.session);
    CHECK(controller->statistics().hard_resets == 1U);

    const auto reset_scheduler = controller->update_local_input(50'000'000,
        make_intent(IntentKind::neutral), values.collision,
        movement_scratch);
    REQUIRE(reset_scheduler);
    CHECK(reset_scheduler.command_count == 0U);
    const auto continued_new_generation = controller->update_local_input(
        60'000'000, make_intent(IntentKind::forward), values.collision,
        movement_scratch);
    REQUIRE(continued_new_generation);
    CHECK(continued_new_generation.command_count == 1U);
    REQUIRE(controller->history()->newest_command_sequence());
    CHECK(controller->history()->newest_command_sequence()->value() == 1U);
    CHECK(controller->history()->session() == *replacement_session.session);
    CHECK(controller->statistics().predicted_commands == 5U);
    CHECK(controller->statistics().small_corrections == 1U);
    CHECK(controller->statistics().teleports == 1U);
    CHECK(controller->statistics().hard_resets == 1U);
}

TEST_CASE("Replay failure preserves the last valid publication and cancel clears input",
    "[local-player][prediction][controller][replay-failure][cancel]")
{
    ControllerFixture values;
    auto controller = values.controller();
    movement::GoldSrcLocalMovementScratch scratch;
    REQUIRE(controller->update_local_input(
        0, make_intent(IntentKind::neutral), values.collision, scratch));
    REQUIRE(controller->update_local_input(20'000'000,
        make_intent(IntentKind::forward), values.collision, scratch));
    const auto* first = controller->history()->find_exact(sequence(1U));
    REQUIRE(first != nullptr);
    const auto corrected_after_first = position_corrected_state(
        *first->post_command_state(), {0.0F, 0.25F, 0.0F});
    const auto authority =
        make_authority(values.session, 1U, corrected_after_first);

    const auto queued_edge = controller->update_local_input(21'000'000,
        make_intent(IntentKind::jump_forward), values.collision, scratch);
    REQUIRE(queued_edge);
    CHECK(queued_edge.command_count == 0U);
    CHECK(gameplay::gameplay_button_is_set(
        controller->movement_controller().pending_one_shots(),
        gameplay::GameplayButton::jump));

    const auto history_before = controller->history();
    const auto history_signature =
        prediction::local_prediction_history_signature(*history_before);
    const auto state_signature =
        player::local_player_movement_state_signature(
            controller->movement_controller().player_state());
    const auto camera_revision =
        controller->movement_controller().camera().revision();
    const auto movement_revision =
        controller->movement_controller().revision();
    values.collision.fail_traces();

    const auto failed = controller->apply_authoritative_state(
        authority, 1.0, values.collision, scratch);
    REQUIRE_FALSE(failed);
    REQUIRE(failed.prediction_error);
    CHECK(failed.prediction_error->code ==
        prediction::PredictionErrorCode::prediction_replay_failed);
    CHECK(failed.status ==
        local_player::LocalPlayerPredictionControllerStatus::replay_failed);
    check_event_types(failed, {
        local_player::LocalPlayerPredictionEventType::
            authoritative_update_received,
        local_player::LocalPlayerPredictionEventType::reconciliation_failed});
    CHECK(controller->history() == history_before);
    CHECK(prediction::local_prediction_history_signature(
              *controller->history()) == history_signature);
    CHECK(player::local_player_movement_state_signature(
              controller->movement_controller().player_state()) ==
        state_signature);
    CHECK(controller->movement_controller().camera().revision() ==
        camera_revision);
    CHECK(controller->movement_controller().revision() == movement_revision);
    CHECK(gameplay::gameplay_button_is_set(
        controller->movement_controller().pending_one_shots(),
        gameplay::GameplayButton::jump));
    CHECK(controller->statistics().accepted_acknowledgements == 0U);
    CHECK(controller->statistics().replay_failures == 1U);

    controller->cancel();
    CHECK(controller->status() ==
        local_player::LocalPlayerPredictionControllerStatus::cancelled);
    CHECK(controller->movement_controller().pending_one_shots() == 0U);
    CHECK_FALSE(controller->visual_correction().active());
    values.collision.fail_traces(false);
    const auto after_cancel = controller->update_local_input(30'000'000,
        make_intent(IntentKind::neutral), values.collision, scratch);
    REQUIRE(after_cancel);
    CHECK(after_cancel.status ==
        local_player::LocalPlayerPredictionControllerStatus::cancelled);
    CHECK(after_cancel.command_count == 0U);
    CHECK(prediction::local_prediction_history_signature(
              *controller->history()) == history_signature);
    CHECK(player::local_player_movement_state_signature(
              controller->movement_controller().player_state()) ==
        state_signature);
}

TEST_CASE("Prediction movement failures retain player-walk latch behavior",
    "[local-player][prediction][controller][failure-latch]")
{
    ControllerFixture values;
    local_player::LocalPlayerMovementController movement_controller{
        values.initial, values.environment, values.movement_config};
    movement::GoldSrcLocalMovementScratch scratch;
    REQUIRE(movement_controller.update(
        0, make_intent(IntentKind::neutral), values.collision, scratch));
    const auto state_signature = player::local_player_movement_state_signature(
        movement_controller.player_state());
    const auto camera_revision = movement_controller.camera().revision();
    values.collision.fail_traces();

    const auto failed = movement_controller.update(10'000'000,
        make_intent(IntentKind::jump_forward), values.collision, scratch);
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error);
    CHECK(failed.error->code == local_player::
        LocalPlayerMovementControllerErrorCode::movement_simulation_failed);
    REQUIRE(failed.error->movement_error);
    CHECK(gameplay::gameplay_button_is_set(
        movement_controller.pending_one_shots(),
        gameplay::GameplayButton::jump));

    local_player::PlayerWalkFailureLatch latch;
    const auto decision = latch.latch(failed,
        local_player::PlayerWalkFailureContext{
            7U,
            movement_controller.player_state().source_command_sequence(),
            state_signature,
            camera_revision,
            0U,
            true,
            scratch.last_diagnostic});
    CHECK(decision.newly_latched);
    CHECK(decision.clear_input_requested);
    CHECK(decision.release_mouse_capture_requested);
    CHECK(decision.keep_rendering);
    CHECK(latch.failure_latched());
    CHECK_FALSE(latch.simulation_enabled());
    REQUIRE(latch.summary());
    CHECK(latch.summary()->controller_error == local_player::
        LocalPlayerMovementControllerErrorCode::movement_simulation_failed);
    CHECK(latch.summary()->movement_error == movement::
        LocalMovementSimulationErrorCode::collision_query_failed);

    movement_controller.discard_pending_input();
    CHECK(movement_controller.pending_one_shots() == 0U);
    CHECK(player::local_player_movement_state_signature(
              movement_controller.player_state()) == state_signature);
    CHECK(movement_controller.camera().revision() == camera_revision);

    const auto repeated = latch.latch(failed, {});
    CHECK_FALSE(repeated.newly_latched);
    CHECK_FALSE(repeated.clear_input_requested);
    CHECK_FALSE(repeated.release_mouse_capture_requested);
    CHECK(repeated.keep_rendering);
}

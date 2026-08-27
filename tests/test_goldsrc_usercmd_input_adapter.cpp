#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string_view>
#include <utility>

namespace {

namespace camera = hlclient::gameplay_camera;
namespace gameplay = hlclient::gameplay_input;
namespace goldsrc = hlclient::goldsrc;
namespace input = hlclient::input;

[[nodiscard]] gameplay::GameplayInputIntent intent_from_events(
    const std::initializer_list<input::InputEvent> events)
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    for (const auto& event : events) {
        tracker.apply_event(event);
    }
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    auto bindings = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    auto built = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot,
        *bindings.bindings,
        gameplay::MouseLookConfig{},
        0.01);
    INFO((built.error ? built.error->message : std::string_view{}));
    REQUIRE(built);
    return std::move(*built.intent);
}

[[nodiscard]] camera::GameplayCameraState make_camera(
    const double yaw = 0.0,
    const double pitch = 0.0)
{
    camera::GameplayCameraStateCreateInfo info;
    info.yaw_degrees = yaw;
    info.pitch_degrees = pitch;
    auto created = camera::GameplayCameraState::create(info);
    REQUIRE(created);
    return std::move(*created.state);
}

[[nodiscard]] goldsrc::GoldSrcUserCmdBuildContext build_context()
{
    goldsrc::GoldSrcUserCmdBuildContext context;
    context.command_sequence = *goldsrc::GoldSrcUserCmdSequence::create(7U);
    context.command_msec = 10U;
    context.command_sample_duration_nanoseconds = 10'000'000U;
    context.command_sample_time_nanoseconds = 70'000'000;
    context.lerp_msec = 25U;
    context.light_level = 4U;
    context.movement_speeds = {320.0F, 280.0F, 240.0F};
    return context;
}

[[nodiscard]] goldsrc::GoldSrcUserCmdBuildResult build(
    const gameplay::GameplayInputIntent& intent,
    const camera::GameplayCameraState& camera_state,
    const goldsrc::GoldSrcUserCmdBuildContext& context)
{
    return goldsrc::GoldSrcUserCmdInputAdapter{}.build(
        intent, camera_state, context);
}

TEST_CASE("Synthetic usercmd adapter maps idle axes and final camera explicitly",
          "[goldsrc][usercmd][input-adapter]")
{
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto result = build(intent, make_camera(135.0, -37.5), build_context());
    REQUIRE(result);
    REQUIRE(result.command);
    CHECK(result.command->forward_move() == 0.0F);
    CHECK(result.command->side_move() == 0.0F);
    CHECK(result.command->up_move() == 0.0F);
    CHECK(result.command->view_angles()[0U] == Catch::Approx(-37.5F));
    CHECK(result.command->view_angles()[1U] == Catch::Approx(135.0F));
    CHECK(result.command->view_angles()[2U] == 0.0F);
    CHECK(result.command->msec() == 10U);
    CHECK(result.command->lerp_msec() == 25U);
    CHECK(result.command->light_level() == 4U);
    CHECK(result.command->source_input_sequence() == intent.input_sequence());
}

TEST_CASE("Synthetic usercmd adapter maps movement without vertical preview leakage",
          "[goldsrc][usercmd][input-adapter][movement]")
{
    const auto camera_state = make_camera();
    const auto context = build_context();
    const auto verify = [&](const std::initializer_list<input::InputEvent> events,
                            const float forward,
                            const float side) {
        const auto result = build(intent_from_events(events), camera_state, context);
        REQUIRE(result);
        CHECK(result.command->forward_move() == Catch::Approx(forward));
        CHECK(result.command->side_move() == Catch::Approx(side));
        CHECK(result.command->up_move() == 0.0F);
    };
    verify({input::InputEvent::focus_gained(),
               input::InputEvent::key_pressed(input::PhysicalKey::w)},
        320.0F,
        0.0F);
    verify({input::InputEvent::focus_gained(),
               input::InputEvent::key_pressed(input::PhysicalKey::s)},
        -280.0F,
        0.0F);
    verify({input::InputEvent::focus_gained(),
               input::InputEvent::key_pressed(input::PhysicalKey::a)},
        0.0F,
        -240.0F);
    verify({input::InputEvent::focus_gained(),
               input::InputEvent::key_pressed(input::PhysicalKey::d)},
        0.0F,
        240.0F);
    verify({input::InputEvent::focus_gained(),
               input::InputEvent::key_pressed(input::PhysicalKey::w),
               input::InputEvent::key_pressed(input::PhysicalKey::s),
               input::InputEvent::key_pressed(input::PhysicalKey::a),
               input::InputEvent::key_pressed(input::PhysicalKey::d)},
        0.0F,
        0.0F);
    verify({input::InputEvent::focus_gained(),
               input::InputEvent::key_pressed(input::PhysicalKey::w),
               input::InputEvent::key_pressed(input::PhysicalKey::d),
               input::InputEvent::key_pressed(input::PhysicalKey::space)},
        320.0F,
        240.0F);
}

TEST_CASE("Synthetic usercmd adapter uses an explicit button translation table",
          "[goldsrc][usercmd][input-adapter][buttons]")
{
    const auto intent = intent_from_events({
        input::InputEvent::focus_gained(),
        input::InputEvent::capture_acquired(),
        input::InputEvent::key_pressed(input::PhysicalKey::space),
        input::InputEvent::key_pressed(input::PhysicalKey::left_control),
        input::InputEvent::key_pressed(input::PhysicalKey::e),
        input::InputEvent::key_pressed(input::PhysicalKey::r),
        input::InputEvent::key_pressed(input::PhysicalKey::left_shift),
        input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::left),
        input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::right),
    });
    const auto result = build(intent, make_camera(), build_context());
    REQUIRE(result);
    constexpr auto expected = goldsrc::kSyntheticGoldSrcButtonAttack |
        goldsrc::kSyntheticGoldSrcButtonJump |
        goldsrc::kSyntheticGoldSrcButtonDuck |
        goldsrc::kSyntheticGoldSrcButtonUse |
        goldsrc::kSyntheticGoldSrcButtonAttack2 |
        goldsrc::kSyntheticGoldSrcButtonRun |
        goldsrc::kSyntheticGoldSrcButtonReload;
    CHECK(result.command->buttons() == expected);

    const auto walk = gameplay::gameplay_button_mask(gameplay::GameplayButton::walk);
    const auto scoreboard =
        gameplay::gameplay_button_mask(gameplay::GameplayButton::scoreboard);
    const auto mapped =
        goldsrc::GoldSrcUserCmdButtonMapping::synthetic_explicit_v1(
            walk | scoreboard);
    CHECK(mapped.wire_buttons == 0U);
    CHECK(mapped.ignored_actions == (walk | scoreboard));
}

TEST_CASE("Scoreboard metadata is ignored or rejected under explicit strict mode",
          "[goldsrc][usercmd][input-adapter][unmapped]")
{
    const auto intent = intent_from_events({input::InputEvent::focus_gained(),
        input::InputEvent::key_pressed(input::PhysicalKey::tab)});
    auto context = build_context();
    const auto ignored = build(intent, make_camera(), context);
    REQUIRE(ignored);
    CHECK(ignored.command->buttons() == 0U);
    CHECK(gameplay::gameplay_button_is_set(
        ignored.ignored_actions, gameplay::GameplayButton::scoreboard));

    context.strict_unmapped_actions = true;
    const auto rejected = build(intent, make_camera(), context);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::GoldSrcUserCmdInputAdapterErrorCode::unsupported_action);
}

TEST_CASE("Focus loss produces a neutral command and cannot consume an impulse",
          "[goldsrc][usercmd][input-adapter][focus][one-shot]")
{
    const auto intent = intent_from_events({
        input::InputEvent::focus_gained(),
        input::InputEvent::key_pressed(input::PhysicalKey::w),
        input::InputEvent::key_pressed(input::PhysicalKey::space),
        input::InputEvent::focus_lost(),
    });
    auto context = build_context();
    context.impulse = 17U;
    const auto result = build(intent, make_camera(), context);
    REQUIRE(result);
    CHECK(result.command->forward_move() == 0.0F);
    CHECK(result.command->buttons() == 0U);
    CHECK(result.command->impulse() == 0U);
    CHECK_FALSE(result.one_shot_plan);
}

TEST_CASE("Impulse consumption is an explicit history-insertion transaction",
          "[goldsrc][usercmd][input-adapter][one-shot]")
{
    auto context = build_context();
    context.impulse = 42U;
    auto result = build(
        intent_from_events({input::InputEvent::focus_gained()}),
        make_camera(),
        context);
    REQUIRE(result);
    CHECK(result.command->impulse() == 42U);
    REQUIRE(result.one_shot_plan);
    CHECK(result.one_shot_plan->consumes_impulse());
    CHECK_FALSE(result.one_shot_plan->committed());
    CHECK_FALSE(result.one_shot_plan->commit_after_history_insert(
        *goldsrc::GoldSrcUserCmdSequence::create(8U)));
    CHECK_FALSE(result.one_shot_plan->committed());

    auto retry = build(
        intent_from_events({input::InputEvent::focus_gained()}),
        make_camera(),
        context);
    REQUIRE(retry.one_shot_plan);
    CHECK(retry.one_shot_plan->commit_after_history_insert(
        context.command_sequence));
    CHECK(retry.one_shot_plan->committed());
    CHECK_FALSE(retry.one_shot_plan->commit_after_history_insert(
        context.command_sequence));
}

TEST_CASE("Weapon selection and stock mappings fail before command publication",
          "[goldsrc][usercmd][input-adapter][profile][weapon]")
{
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    auto context = build_context();
    context.weapon_selection = 1U;
    const auto weapon_result = build(intent, camera_state, context);
    REQUIRE_FALSE(weapon_result);
    CHECK_FALSE(weapon_result.command);
    REQUIRE(weapon_result.error);
    CHECK(weapon_result.error->code == goldsrc::GoldSrcUserCmdInputAdapterErrorCode::
        unsupported_weapon_selection);

    context.weapon_selection.reset();
    context.compatibility_profile =
        goldsrc::GoldSrcUserCmdCompatibilityProfile::
            stock_protocol_48_evidence_pending;
    context.mapping_profile = goldsrc::GoldSrcUserCmdInputMappingProfile::
        stock_protocol_48_evidence_pending;
    const auto stock_result = build(intent, camera_state, context);
    REQUIRE_FALSE(stock_result);
    CHECK_FALSE(stock_result.command);
    REQUIRE(stock_result.error);
    CHECK(stock_result.error->code ==
        goldsrc::GoldSrcUserCmdInputAdapterErrorCode::stock_evidence_pending);
}

TEST_CASE("Pressed gameplay buttons are explicit move-only one-shot capabilities",
          "[goldsrc][usercmd][input-adapter][buttons][one-shot][transactional]")
{
    auto context = build_context();
    const auto jump =
        gameplay::gameplay_button_mask(gameplay::GameplayButton::jump);
    context.one_shot_buttons = jump;
    auto result = build(
        intent_from_events({input::InputEvent::focus_gained()}),
        make_camera(),
        context);
    REQUIRE(result);
    REQUIRE(result.command);
    CHECK(result.command->buttons() ==
          goldsrc::kSyntheticGoldSrcButtonJump);
    REQUIRE(result.one_shot_plan);
    CHECK(result.one_shot_plan->consumes_buttons() == jump);

    auto moved = std::move(*result.one_shot_plan);
    CHECK_FALSE(result.one_shot_plan->commit_after_history_insert(
        context.command_sequence));
    CHECK(moved.commit_after_history_insert(context.command_sequence));
    CHECK(moved.committed());

    context.one_shot_buttons = gameplay::GameplayButtonMask{1U} << 31U;
    const auto unknown = build(
        intent_from_events({input::InputEvent::focus_gained()}),
        make_camera(),
        context);
    REQUIRE_FALSE(unknown);
    REQUIRE(unknown.error);
    CHECK(unknown.error->code ==
        goldsrc::GoldSrcUserCmdInputAdapterErrorCode::invalid_context);
}

} // namespace

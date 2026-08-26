#include <hlclient/gameplay_input/gameplay_input_intent.hpp>
#include <hlclient/gameplay_input/gameplay_input_limits.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {

namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;

using Catch::Approx;

static_assert(!std::is_default_constructible_v<gameplay::GameplayInputIntent>);
static_assert(!std::is_copy_assignable_v<gameplay::GameplayInputIntent>);
static_assert(!std::is_move_assignable_v<gameplay::GameplayInputIntent>);

[[nodiscard]] gameplay::GameplayInputBindings default_bindings()
{
    auto built = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(built);
    return std::move(*built.bindings);
}

TEST_CASE("Gameplay intent derives exact axes look wheel and project button masks",
          "[gameplay-input][intent][mapping]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::s));
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::d));
    tracker.apply_event(
        input::InputEvent::key_pressed(input::PhysicalKey::space));
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::e));
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::right));
    tracker.apply_event(input::InputEvent::mouse_motion(12, -6));
    tracker.apply_event(input::InputEvent::mouse_wheel(1.5, -2.0));
    const auto snapshot = tracker.publish_snapshot();

    auto look = gameplay::MouseLookConfig{};
    look.degrees_per_pixel_y = 0.20;
    const auto bindings = default_bindings();
    const auto built = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, look, 0.016);
    REQUIRE(built);
    const auto& intent = *built.intent;

    CHECK(intent.input_sequence() == snapshot.sequence());
    CHECK(intent.forward_axis() == 0.0F);
    CHECK(intent.side_axis() == 1.0F);
    CHECK(intent.vertical_axis() == 0.0F);
    CHECK(intent.look_delta_yaw_degrees() == Approx(-1.2));
    CHECK(intent.look_delta_pitch_degrees() == Approx(1.2));
    CHECK(intent.wheel_delta_x() == Approx(1.5));
    CHECK(intent.wheel_delta_y() == Approx(-2.0));
    CHECK(intent.focused());
    CHECK(intent.captured());
    CHECK(intent.sample_duration_seconds() == Approx(0.016));
    CHECK(gameplay::gameplay_button_is_set(
        intent.held_buttons(), gameplay::GameplayButton::jump));
    CHECK(gameplay::gameplay_button_is_set(
        intent.pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK(gameplay::gameplay_button_is_set(
        intent.held_buttons(), gameplay::GameplayButton::use));
    CHECK(gameplay::gameplay_button_is_set(
        intent.held_buttons(), gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        intent.held_buttons(), gameplay::GameplayButton::attack_secondary));
    CHECK_FALSE(intent.capture_mouse_requested());
    CHECK_FALSE(intent.release_mouse_requested());
    CHECK(intent.compatibility_profile() ==
          gameplay::GameplayInputCompatibilityProfile::
              local_keyboard_mouse_intent_v1);
    CHECK(intent.evidence_profile() ==
          gameplay::GameplayInputEvidenceProfile::
              project_owned_input_semantics);
}

TEST_CASE("Gameplay intent cancels opposite movement actions exactly",
          "[gameplay-input][intent][axes]")
{
    const std::array custom_bindings{
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_forward,
            input::PhysicalKey::w),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_backward,
            input::PhysicalKey::s),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_left,
            input::PhysicalKey::a),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_right,
            input::PhysicalKey::d),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_up,
            input::PhysicalKey::up),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_down,
            input::PhysicalKey::down),
    };
    auto published =
        gameplay::GameplayInputBindingsBuilder{}.build(custom_bindings);
    REQUIRE(published);

    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    for (const auto key : {input::PhysicalKey::w,
             input::PhysicalKey::s,
             input::PhysicalKey::a,
             input::PhysicalKey::d,
             input::PhysicalKey::up,
             input::PhysicalKey::down}) {
        tracker.apply_event(input::InputEvent::key_pressed(key));
    }
    const auto snapshot = tracker.publish_snapshot();
    const auto built = gameplay::GameplayInputIntentBuilder{}.build(snapshot,
        *published.bindings,
        {},
        0.01);
    REQUIRE(built);
    CHECK(built.intent->forward_axis() == 0.0F);
    CHECK(built.intent->side_axis() == 0.0F);
    CHECK(built.intent->vertical_axis() == 0.0F);
}

TEST_CASE("Gameplay intent makes uncaptured left click a capture gesture only",
          "[gameplay-input][intent][capture]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_motion(90, -45));
    const auto uncaptured = tracker.publish_snapshot();
    const auto bindings = default_bindings();
    const auto capture = gameplay::GameplayInputIntentBuilder{}.build(
        uncaptured, bindings, {}, 0.01);
    REQUIRE(capture);
    CHECK(capture.intent->capture_mouse_requested());
    CHECK_FALSE(capture.intent->release_mouse_requested());
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        capture.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        capture.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(capture.intent->look_delta_yaw_degrees() == 0.0);
    CHECK(capture.intent->look_delta_pitch_degrees() == 0.0);
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto captured_snapshot = tracker.publish_snapshot();
    const auto captured = gameplay::GameplayInputIntentBuilder{}.build(
        captured_snapshot, bindings, {}, 0.01);
    REQUIRE(captured);
    CHECK(captured.intent->captured());
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        captured.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        captured.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_primary));
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto deliberate_attack_snapshot = tracker.publish_snapshot();
    const auto deliberate_attack = gameplay::GameplayInputIntentBuilder{}.build(
        deliberate_attack_snapshot, bindings, {}, 0.01);
    REQUIRE(deliberate_attack);
    CHECK(gameplay::gameplay_button_is_set(
        deliberate_attack.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        deliberate_attack.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_primary));
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::key_pressed(
        input::PhysicalKey::escape));
    const auto release_snapshot = tracker.publish_snapshot();
    const auto release = gameplay::GameplayInputIntentBuilder{}.build(
        release_snapshot, bindings, {}, 0.01);
    REQUIRE(release);
    CHECK(release.intent->release_mouse_requested());
    CHECK_FALSE(release.intent->capture_mouse_requested());
}

TEST_CASE("Uncaptured physical left is consumed across custom bindings",
          "[gameplay-input][intent][capture][custom-bindings]")
{
    const std::array shared_left{
        gameplay::InputBinding::mouse_button(
            gameplay::GameplayInputAction::move_forward,
            input::PhysicalMouseButton::left),
        gameplay::InputBinding::mouse_button(
            gameplay::GameplayInputAction::jump,
            input::PhysicalMouseButton::left),
    };
    auto bindings = gameplay::GameplayInputBindingsBuilder{}.build(
        shared_left, {}, true);
    REQUIRE(bindings);

    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto snapshot = tracker.publish_snapshot();
    const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, {}, 0.016);
    REQUIRE(intent);
    CHECK(intent.intent->capture_mouse_requested());
    CHECK_FALSE(intent.intent->release_mouse_requested());
    CHECK(intent.intent->forward_axis() == 0.0F);
    CHECK(intent.intent->held_buttons() == 0U);
    CHECK(intent.intent->pressed_buttons() == 0U);
}

TEST_CASE("Escape release has precedence over same-frame click capture",
          "[gameplay-input][intent][capture][release][ordering]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::key_pressed(
        input::PhysicalKey::escape));
    const auto snapshot = tracker.publish_snapshot();
    const auto bindings = default_bindings();
    const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, {}, 0.016);
    REQUIRE(intent);
    CHECK_FALSE(intent.intent->capture_mouse_requested());
    CHECK(intent.intent->release_mouse_requested());
    CHECK(intent.intent->held_buttons() == 0U);
    CHECK(intent.intent->pressed_buttons() == 0U);
}

TEST_CASE("Gameplay intent button edges follow immutable input frame edges",
          "[gameplay-input][intent][edges]")
{
    input::InputStateTracker tracker;
    const auto bindings = default_bindings();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(
        input::InputEvent::key_pressed(input::PhysicalKey::space));
    const auto first_snapshot = tracker.publish_snapshot();
    const auto first_intent = gameplay::GameplayInputIntentBuilder{}.build(
        first_snapshot, bindings, {}, 0.01);
    REQUIRE(first_intent);
    CHECK(gameplay::gameplay_button_is_set(
        first_intent.intent->held_buttons(), gameplay::GameplayButton::jump));
    CHECK(gameplay::gameplay_button_is_set(
        first_intent.intent->pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        first_intent.intent->released_buttons(), gameplay::GameplayButton::jump));
    tracker.end_frame();

    tracker.begin_frame();
    const auto second_snapshot = tracker.publish_snapshot();
    const auto second_intent = gameplay::GameplayInputIntentBuilder{}.build(
        second_snapshot, bindings, {}, 0.01);
    REQUIRE(second_intent);
    CHECK(gameplay::gameplay_button_is_set(
        second_intent.intent->held_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        second_intent.intent->pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        second_intent.intent->released_buttons(), gameplay::GameplayButton::jump));
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(
        input::InputEvent::key_released(input::PhysicalKey::space));
    const auto third_snapshot = tracker.publish_snapshot();
    const auto third_intent = gameplay::GameplayInputIntentBuilder{}.build(
        third_snapshot, bindings, {}, 0.01);
    REQUIRE(third_intent);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        third_intent.intent->held_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        third_intent.intent->pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK(gameplay::gameplay_button_is_set(
        third_intent.intent->released_buttons(), gameplay::GameplayButton::jump));
}

TEST_CASE("Multiple physical bindings publish only aggregate action transitions",
          "[gameplay-input][intent][edges][shared-action]")
{
    const std::array shared_action{
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::jump,
            input::PhysicalKey::w),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::jump,
            input::PhysicalKey::up),
    };
    const auto built_bindings =
        gameplay::GameplayInputBindingsBuilder{}.build(shared_action);
    REQUIRE(built_bindings);
    const auto& bindings = *built_bindings.bindings;
    input::InputStateTracker tracker;

    const auto publish_intent = [&]() {
        const auto snapshot = tracker.publish_snapshot();
        const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
            snapshot, bindings, {}, 0.016);
        REQUIRE(intent);
        tracker.end_frame();
        return *intent.intent;
    };

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    const auto first = publish_intent();
    CHECK(gameplay::gameplay_button_is_set(
        first.held_buttons(), gameplay::GameplayButton::jump));
    CHECK(gameplay::gameplay_button_is_set(
        first.pressed_buttons(), gameplay::GameplayButton::jump));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::up));
    const auto second = publish_intent();
    CHECK(gameplay::gameplay_button_is_set(
        second.held_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        second.pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        second.released_buttons(), gameplay::GameplayButton::jump));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::key_released(input::PhysicalKey::w));
    const auto third = publish_intent();
    CHECK(gameplay::gameplay_button_is_set(
        third.held_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        third.pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        third.released_buttons(), gameplay::GameplayButton::jump));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::key_released(input::PhysicalKey::up));
    const auto fourth = publish_intent();
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        fourth.held_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        fourth.pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK(gameplay::gameplay_button_is_set(
        fourth.released_buttons(), gameplay::GameplayButton::jump));
}

TEST_CASE("Focus reset permits an explicit post-regain action press in the same frame",
          "[gameplay-input][intent][edges][focus-reset][ordering]")
{
    const std::array jump_binding{
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::jump,
            input::PhysicalKey::w),
    };
    const auto bindings =
        gameplay::GameplayInputBindingsBuilder{}.build(jump_binding);
    REQUIRE(bindings);
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    static_cast<void>(tracker.publish_snapshot());
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_lost());
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(snapshot.reset_reason() == input::InputResetReason::focus_lost);
    REQUIRE(snapshot.key_held_at_frame_start(input::PhysicalKey::w));
    REQUIRE(snapshot.key_pressed(input::PhysicalKey::w));

    const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, {}, 0.016);
    REQUIRE(intent);
    CHECK(gameplay::gameplay_button_is_set(
        intent.intent->held_buttons(), gameplay::GameplayButton::jump));
    CHECK(gameplay::gameplay_button_is_set(
        intent.intent->pressed_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->released_buttons(), gameplay::GameplayButton::jump));
}

TEST_CASE("Capture reset permits a fresh captured mouse action press",
          "[gameplay-input][intent][edges][capture-reset][ordering]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    static_cast<void>(tracker.publish_snapshot());
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(snapshot.captured());
    REQUIRE(snapshot.mouse_button_held_at_frame_start(
        input::PhysicalMouseButton::left));
    REQUIRE(snapshot.statistics().discarded_pre_capture_button_count == 1U);
    REQUIRE(snapshot.mouse_button_pressed(input::PhysicalMouseButton::left));

    const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, {}, 0.016);
    REQUIRE(intent);
    CHECK(gameplay::gameplay_button_is_set(
        intent.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        intent.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_primary));
}

TEST_CASE("Capture acquisition releases a held non-gesture mouse action",
          "[gameplay-input][intent][edges][capture-reset][ordering]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::right));
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto gesture_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto gesture = gameplay::GameplayInputIntentBuilder{}.build(
        gesture_snapshot, bindings, {}, 0.016);
    REQUIRE(gesture);
    REQUIRE(gesture.intent->capture_mouse_requested());
    REQUIRE(gameplay::gameplay_button_is_set(
        gesture.intent->held_buttons(),
        gameplay::GameplayButton::attack_secondary));
    REQUIRE(gameplay::gameplay_button_is_set(
        gesture.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_secondary));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto captured_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(captured_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::left));
    REQUIRE(captured_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::right));
    REQUIRE_FALSE(captured_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::middle));
    REQUIRE_FALSE(captured_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::count));
    const auto captured = gameplay::GameplayInputIntentBuilder{}.build(
        captured_snapshot, bindings, {}, 0.016);
    REQUIRE(captured);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        captured.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        captured.intent->released_buttons(),
        gameplay::GameplayButton::attack_secondary));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::right));
    const auto delayed_up_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE_FALSE(delayed_up_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::left));
    REQUIRE_FALSE(delayed_up_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::right));
    const auto delayed_up = gameplay::GameplayInputIntentBuilder{}.build(
        delayed_up_snapshot, bindings, {}, 0.016);
    REQUIRE(delayed_up);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        delayed_up.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        delayed_up.intent->released_buttons(),
        gameplay::GameplayButton::attack_secondary));
}

TEST_CASE("Capture acquisition retains a pre-capture mouse-up release edge",
          "[gameplay-input][intent][edges][capture-reset][ordering]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::right));
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto gesture_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto gesture = gameplay::GameplayInputIntentBuilder{}.build(
        gesture_snapshot, bindings, {}, 0.016);
    REQUIRE(gesture);
    REQUIRE(gesture.intent->capture_mouse_requested());
    REQUIRE(gameplay::gameplay_button_is_set(
        gesture.intent->held_buttons(),
        gameplay::GameplayButton::attack_secondary));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::right));
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto captured_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(captured_snapshot.statistics().
        discarded_pre_capture_button_count == 2U);
    REQUIRE(captured_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::left));
    REQUIRE(captured_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::right));
    REQUIRE_FALSE(captured_snapshot.mouse_button_released(
        input::PhysicalMouseButton::right));

    const auto captured = gameplay::GameplayInputIntentBuilder{}.build(
        captured_snapshot, bindings, {}, 0.016);
    REQUIRE(captured);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        captured.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        captured.intent->released_buttons(),
        gameplay::GameplayButton::attack_secondary));
}

TEST_CASE("Capture acquisition cannot turn an uncaptured click into attack edges",
          "[gameplay-input][intent][edges][capture-reset][ordering]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(snapshot.captured());
    REQUIRE(snapshot.statistics().discarded_pre_capture_button_count == 1U);
    REQUIRE(snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::left));

    const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, {}, 0.016);
    REQUIRE(intent);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
}

TEST_CASE("Captured attack releases across same-frame release and reacquire",
          "[gameplay-input][intent][edges][capture-reset][ordering]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto attack_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto attack = gameplay::GameplayInputIntentBuilder{}.build(
        attack_snapshot, bindings, {}, 0.016);
    REQUIRE(attack);
    REQUIRE(gameplay::gameplay_button_is_set(
        attack.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_released());
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto reacquired_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(reacquired_snapshot.capture_state_at_frame_start() ==
        input::InputCaptureState::captured);
    REQUIRE(reacquired_snapshot.captured());
    REQUIRE(reacquired_snapshot.mouse_button_discarded_by_capture(
        input::PhysicalMouseButton::left));
    const auto reacquired = gameplay::GameplayInputIntentBuilder{}.build(
        reacquired_snapshot, bindings, {}, 0.016);
    REQUIRE(reacquired);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        reacquired.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        reacquired.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    const auto delayed_up_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto delayed_up = gameplay::GameplayInputIntentBuilder{}.build(
        delayed_up_snapshot, bindings, {}, 0.016);
    REQUIRE(delayed_up);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        delayed_up.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
}

TEST_CASE("Capture release emits one attack release before delayed button up",
          "[gameplay-input][intent][edges][capture-release][ordering]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto attack_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto attack = gameplay::GameplayInputIntentBuilder{}.build(
        attack_snapshot, bindings, {}, 0.016);
    REQUIRE(attack);
    REQUIRE(gameplay::gameplay_button_is_set(
        attack.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_released());
    const auto release_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(release_snapshot.capture_state_at_frame_start() ==
        input::InputCaptureState::captured);
    REQUIRE_FALSE(release_snapshot.captured());
    REQUIRE(release_snapshot.mouse_button_held_at_frame_start(
        input::PhysicalMouseButton::left));
    REQUIRE(release_snapshot.mouse_button_held(
        input::PhysicalMouseButton::left));
    const auto release = gameplay::GameplayInputIntentBuilder{}.build(
        release_snapshot, bindings, {}, 0.016);
    REQUIRE(release);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        release.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        release.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        release.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    const auto delayed_up_snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    const auto delayed_up = gameplay::GameplayInputIntentBuilder{}.build(
        delayed_up_snapshot, bindings, {}, 0.016);
    REQUIRE(delayed_up);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        delayed_up.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
}

TEST_CASE("Uncaptured click and shared binding cannot fabricate attack release",
          "[gameplay-input][intent][edges][capture-release][aggregate]")
{
    const std::array attack_bindings{
        gameplay::InputBinding::mouse_button(
            gameplay::GameplayInputAction::attack_primary,
            input::PhysicalMouseButton::left),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::attack_primary,
            input::PhysicalKey::space),
    };
    const auto built_bindings =
        gameplay::GameplayInputBindingsBuilder{}.build(attack_bindings);
    REQUIRE(built_bindings);
    const auto& bindings = *built_bindings.bindings;

    input::InputStateTracker uncaptured_tracker;
    uncaptured_tracker.begin_frame();
    uncaptured_tracker.apply_event(input::InputEvent::focus_gained());
    uncaptured_tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    uncaptured_tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    const auto click_snapshot = uncaptured_tracker.publish_snapshot();
    uncaptured_tracker.end_frame();
    const auto click = gameplay::GameplayInputIntentBuilder{}.build(
        click_snapshot, bindings, {}, 0.016);
    REQUIRE(click);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        click.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));

    input::InputStateTracker shared_tracker;
    shared_tracker.begin_frame();
    shared_tracker.apply_event(input::InputEvent::focus_gained());
    shared_tracker.apply_event(input::InputEvent::capture_acquired());
    shared_tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    shared_tracker.apply_event(input::InputEvent::key_pressed(
        input::PhysicalKey::space));
    static_cast<void>(shared_tracker.publish_snapshot());
    shared_tracker.end_frame();

    shared_tracker.begin_frame();
    shared_tracker.apply_event(input::InputEvent::capture_released());
    const auto shared_snapshot = shared_tracker.publish_snapshot();
    shared_tracker.end_frame();
    const auto shared = gameplay::GameplayInputIntentBuilder{}.build(
        shared_snapshot, bindings, {}, 0.016);
    REQUIRE(shared);
    CHECK(gameplay::gameplay_button_is_set(
        shared.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        shared.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
}

TEST_CASE("Focus loss cannot turn an uncaptured left gesture into an action release",
          "[gameplay-input][intent][edges][capture-release][focus-reset]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::focus_lost());
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE_FALSE(snapshot.focused());
    REQUIRE(snapshot.capture_state_at_frame_start() ==
        input::InputCaptureState::released);
    REQUIRE(snapshot.mouse_button_released(
        input::PhysicalMouseButton::left));

    const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, {}, 0.016);
    REQUIRE(intent);
    CHECK_FALSE(intent.intent->capture_mouse_requested());
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->pressed_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
}

TEST_CASE("Captured attack releases across same-frame focus loss and regain",
          "[gameplay-input][intent][edges][capture-release][focus-reset]")
{
    const auto bindings = default_bindings();
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    static_cast<void>(tracker.publish_snapshot());
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_lost());
    tracker.apply_event(input::InputEvent::focus_gained());
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(snapshot.reset_reason() == input::InputResetReason::focus_lost);
    REQUIRE(snapshot.capture_state_at_frame_start() ==
        input::InputCaptureState::captured);
    REQUIRE(snapshot.focused());
    REQUIRE_FALSE(snapshot.captured());

    const auto intent = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, {}, 0.016);
    REQUIRE(intent);
    CHECK_FALSE(gameplay::gameplay_button_is_set(
        intent.intent->held_buttons(),
        gameplay::GameplayButton::attack_primary));
    CHECK(gameplay::gameplay_button_is_set(
        intent.intent->released_buttons(),
        gameplay::GameplayButton::attack_primary));
}

TEST_CASE("Gameplay mouse look is captured-only and independent of sample duration",
          "[gameplay-input][intent][mouse-look][timing]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_motion(20, 30));
    const auto snapshot = tracker.publish_snapshot();
    const auto bindings = default_bindings();

    gameplay::MouseLookConfig normal;
    normal.degrees_per_pixel_x = 0.5;
    normal.degrees_per_pixel_y = 0.25;
    const auto short_frame = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, normal, 0.001);
    const auto long_frame = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, normal, 0.20);
    const auto frame_16_ms = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, normal, 0.016);
    const auto frame_33_ms = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, normal, 0.033);
    const auto frame_100_ms = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, normal, 0.100);
    REQUIRE(short_frame);
    REQUIRE(long_frame);
    REQUIRE(frame_16_ms);
    REQUIRE(frame_33_ms);
    REQUIRE(frame_100_ms);
    CHECK(short_frame.intent->look_delta_yaw_degrees() == Approx(-10.0));
    CHECK(short_frame.intent->look_delta_pitch_degrees() == Approx(-7.5));
    CHECK(long_frame.intent->look_delta_yaw_degrees() ==
          short_frame.intent->look_delta_yaw_degrees());
    CHECK(long_frame.intent->look_delta_pitch_degrees() ==
          short_frame.intent->look_delta_pitch_degrees());
    CHECK(frame_16_ms.intent->look_delta_yaw_degrees() ==
        frame_33_ms.intent->look_delta_yaw_degrees());
    CHECK(frame_33_ms.intent->look_delta_yaw_degrees() ==
        frame_100_ms.intent->look_delta_yaw_degrees());
    CHECK(frame_16_ms.intent->look_delta_pitch_degrees() ==
        frame_33_ms.intent->look_delta_pitch_degrees());
    CHECK(frame_33_ms.intent->look_delta_pitch_degrees() ==
        frame_100_ms.intent->look_delta_pitch_degrees());

    normal.invert_y = true;
    normal.maximum_delta_per_frame = 5.0;
    const auto inverted = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, normal, 0.01);
    REQUIRE(inverted);
    CHECK(inverted.intent->look_delta_yaw_degrees() == Approx(-5.0));
    CHECK(inverted.intent->look_delta_pitch_degrees() == Approx(5.0));
}

TEST_CASE("Gameplay intent rejects invalid local configuration and stock mapping",
          "[gameplay-input][intent][limits][evidence]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    const auto snapshot = tracker.publish_snapshot();
    const auto bindings = default_bindings();
    const gameplay::GameplayInputIntentBuilder builder;

    const auto exact = builder.build(snapshot, bindings, {}, 0.25);
    REQUIRE(exact);
    const auto over = builder.build(
        snapshot, bindings, {}, std::nextafter(0.25, 1.0));
    REQUIRE_FALSE(over);
    CHECK(over.error->code ==
          gameplay::GameplayInputIntentErrorCode::
              sample_duration_out_of_range);

    const auto non_finite = builder.build(snapshot,
        bindings,
        {},
        std::numeric_limits<double>::infinity());
    REQUIRE_FALSE(non_finite);
    CHECK(non_finite.error->code ==
          gameplay::GameplayInputIntentErrorCode::
              non_finite_sample_duration);

    auto invalid_look = gameplay::MouseLookConfig{};
    invalid_look.degrees_per_pixel_x =
        std::numeric_limits<double>::quiet_NaN();
    const auto invalid = builder.build(
        snapshot, bindings, invalid_look, 0.01);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code ==
          gameplay::GameplayInputIntentErrorCode::invalid_mouse_look_config);

    const auto stock = builder.build(snapshot,
        bindings,
        {},
        0.01,
        {},
        gameplay::GameplayInputCompatibilityProfile::
            stock_usercmd_mapping_evidence_pending);
    REQUIRE_FALSE(stock);
    CHECK(stock.error->code ==
          gameplay::GameplayInputIntentErrorCode::
              compatibility_evidence_pending);
    CHECK(gameplay::to_string(stock.error->code) ==
          "compatibility_evidence_pending");

    auto exact_hard_look = gameplay::MouseLookConfig{};
    exact_hard_look.degrees_per_pixel_x =
        gameplay::kGameplayInputIntentHardLimits.maximum_degrees_per_pixel;
    exact_hard_look.degrees_per_pixel_y =
        gameplay::kGameplayInputIntentHardLimits.maximum_degrees_per_pixel;
    exact_hard_look.maximum_delta_per_frame =
        gameplay::kGameplayInputIntentHardLimits.
            maximum_look_delta_degrees_per_axis;
    CHECK(gameplay::valid_mouse_look_config(exact_hard_look,
        gameplay::kGameplayInputIntentHardLimits));

    auto over_hard_sensitivity = exact_hard_look;
    over_hard_sensitivity.degrees_per_pixel_x = std::nextafter(
        gameplay::kGameplayInputIntentHardLimits.maximum_degrees_per_pixel,
        std::numeric_limits<double>::infinity());
    CHECK_FALSE(gameplay::valid_mouse_look_config(over_hard_sensitivity,
        gameplay::kGameplayInputIntentHardLimits));

    auto over_hard_delta = exact_hard_look;
    over_hard_delta.maximum_delta_per_frame = std::nextafter(
        gameplay::kGameplayInputIntentHardLimits.
            maximum_look_delta_degrees_per_axis,
        std::numeric_limits<double>::infinity());
    CHECK_FALSE(gameplay::valid_mouse_look_config(over_hard_delta,
        gameplay::kGameplayInputIntentHardLimits));
}

TEST_CASE("Gameplay input safety envelope exposes every bounded path",
          "[gameplay-input][limits][safety]")
{
    const gameplay::GameplayInputLimits defaults;
    REQUIRE(gameplay::valid_gameplay_input_limits(defaults));
    REQUIRE(gameplay::valid_gameplay_input_limits(
        gameplay::kGameplayInputSafetyHardLimits));
    CHECK(defaults.maximum_events_per_frame == 1'024U);
    CHECK(defaults.maximum_actions ==
        static_cast<std::size_t>(gameplay::GameplayInputAction::count));
    CHECK(defaults.maximum_bindings == 64U);
    CHECK(defaults.maximum_mouse_delta == 1'000'000);
    CHECK(defaults.maximum_wheel_delta == 10'000.0);
    CHECK(defaults.maximum_frame_duration_seconds == 0.1);
    CHECK(defaults.maximum_camera_position_magnitude == 10'000'000.0);
    CHECK(defaults.maximum_camera_speed == 10'000.0);
    CHECK(defaults.minimum_mouse_sensitivity == 0.001);
    CHECK(defaults.maximum_mouse_sensitivity == 10.0);
    CHECK(defaults.minimum_vertical_fov_radians ==
        Approx(0.3490658503988659));
    CHECK(defaults.maximum_vertical_fov_radians ==
        Approx(2.443460952792061));
    CHECK(defaults.maximum_camera_revisions ==
        std::numeric_limits<std::uint64_t>::max());

    auto invalid = gameplay::kGameplayInputSafetyHardLimits;
    ++invalid.maximum_events_per_frame;
    CHECK_FALSE(gameplay::valid_gameplay_input_limits(invalid));
    invalid = gameplay::kGameplayInputSafetyHardLimits;
    ++invalid.maximum_actions;
    CHECK_FALSE(gameplay::valid_gameplay_input_limits(invalid));
    invalid = gameplay::kGameplayInputSafetyHardLimits;
    ++invalid.maximum_bindings;
    CHECK_FALSE(gameplay::valid_gameplay_input_limits(invalid));
    invalid = defaults;
    invalid.minimum_mouse_sensitivity =
        std::nextafter(0.001, 0.0);
    CHECK_FALSE(gameplay::valid_gameplay_input_limits(invalid));
    invalid = defaults;
    invalid.maximum_camera_speed = 10'000.01;
    CHECK_FALSE(gameplay::valid_gameplay_input_limits(invalid));
    invalid = defaults;
    invalid.maximum_camera_position_magnitude = 0.5;
    CHECK_FALSE(gameplay::valid_gameplay_input_limits(invalid));
    invalid = defaults;
    invalid.maximum_camera_revisions = 0U;
    CHECK_FALSE(gameplay::valid_gameplay_input_limits(invalid));
}

TEST_CASE("Gameplay intent clears movement and look after focus loss",
          "[gameplay-input][intent][focus-loss]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(
        input::InputEvent::key_pressed(input::PhysicalKey::space));
    tracker.apply_event(input::InputEvent::mouse_motion(50, 25));
    tracker.apply_event(input::InputEvent::focus_lost());
    const auto snapshot = tracker.publish_snapshot();
    const auto bindings = default_bindings();
    const auto built = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, bindings, {}, 0.01);
    REQUIRE(built);
    CHECK_FALSE(built.intent->focused());
    CHECK_FALSE(built.intent->captured());
    CHECK(built.intent->forward_axis() == 0.0F);
    CHECK(built.intent->look_delta_yaw_degrees() == 0.0);
    CHECK(built.intent->look_delta_pitch_degrees() == 0.0);
    CHECK(built.intent->held_buttons() == 0U);
    CHECK(built.intent->pressed_buttons() == 0U);
    CHECK(gameplay::gameplay_button_is_set(
        built.intent->released_buttons(), gameplay::GameplayButton::jump));
    CHECK_FALSE(built.intent->capture_mouse_requested());
    CHECK_FALSE(built.intent->release_mouse_requested());
}

} // namespace

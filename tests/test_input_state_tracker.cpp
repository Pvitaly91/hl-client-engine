#include <hlclient/input/input_source.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

namespace input = hlclient::input;

template <typename Callable>
void require_input_error(const input::InputStateErrorCode expected, Callable&& callable)
{
    try {
        std::forward<Callable>(callable)();
        FAIL("Expected an InputStateException");
    } catch (const input::InputStateException& error) {
        CHECK(error.code() == expected);
    }
}

TEST_CASE("Input tracker preserves held state and resets per-frame edges", "[input][tracker]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w, true));
    tracker.apply_event(
        input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::right));
    tracker.apply_event(input::InputEvent::mouse_motion(10, -4));
    tracker.apply_event(input::InputEvent::mouse_motion(-3, 9));
    tracker.apply_event(input::InputEvent::mouse_wheel(0.25, 1.0));
    const auto first = tracker.publish_snapshot();

    CHECK(first.sequence() == 1U);
    CHECK(first.compatibility_profile() ==
        input::InputCompatibilityProfile::local_keyboard_mouse_input_v1);
    CHECK(first.focused());
    CHECK(first.captured());
    CHECK(first.key_held(input::PhysicalKey::w));
    CHECK(first.key_pressed(input::PhysicalKey::w));
    CHECK_FALSE(first.key_released(input::PhysicalKey::w));
    CHECK(first.mouse_button_held(input::PhysicalMouseButton::right));
    CHECK(first.mouse_button_pressed(input::PhysicalMouseButton::right));
    CHECK(first.relative_mouse_delta() == input::RelativeMouseDelta{7, 5});
    CHECK(first.wheel_delta().horizontal == Catch::Approx(0.25));
    CHECK(first.wheel_delta().vertical == Catch::Approx(1.0));
    CHECK(first.statistics().processed_event_count == 8U);
    CHECK(first.statistics().ignored_repeat_count == 1U);
    tracker.end_frame();

    tracker.begin_frame();
    const auto second = tracker.publish_snapshot();
    CHECK(second.sequence() == 2U);
    CHECK(second.key_held(input::PhysicalKey::w));
    CHECK_FALSE(second.key_pressed(input::PhysicalKey::w));
    CHECK_FALSE(second.key_released(input::PhysicalKey::w));
    CHECK(second.mouse_button_held(input::PhysicalMouseButton::right));
    CHECK(second.relative_mouse_delta() == input::RelativeMouseDelta{});
    CHECK(second.wheel_delta() == input::MouseWheelDelta{});
    CHECK(second.statistics().processed_event_count == 0U);
    tracker.end_frame();
}

TEST_CASE("Repeat and duplicate releases never invent input edges", "[input][tracker]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::a, true));
    tracker.apply_event(input::InputEvent::key_released(input::PhysicalKey::a));
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::d));
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::d));
    tracker.apply_event(input::InputEvent::key_released(input::PhysicalKey::d));
    tracker.apply_event(input::InputEvent::key_released(input::PhysicalKey::d));
    const auto snapshot = tracker.publish_snapshot();

    CHECK_FALSE(snapshot.key_held(input::PhysicalKey::a));
    CHECK_FALSE(snapshot.key_pressed(input::PhysicalKey::a));
    CHECK_FALSE(snapshot.key_released(input::PhysicalKey::a));
    CHECK_FALSE(snapshot.key_held(input::PhysicalKey::d));
    CHECK(snapshot.key_pressed(input::PhysicalKey::d));
    CHECK(snapshot.key_released(input::PhysicalKey::d));
    CHECK(snapshot.statistics().ignored_repeat_count == 2U);
    CHECK(snapshot.statistics().ignored_duplicate_release_count == 2U);
    tracker.end_frame();
}

TEST_CASE("Focus loss clears every held input in exact event order", "[input][tracker][focus]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(
        input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_motion(50, 60));
    tracker.apply_event(input::InputEvent::focus_lost());
    tracker.apply_event(input::InputEvent::key_released(input::PhysicalKey::w));
    const auto snapshot = tracker.publish_snapshot();

    CHECK_FALSE(snapshot.focused());
    CHECK_FALSE(snapshot.captured());
    CHECK_FALSE(snapshot.key_held(input::PhysicalKey::w));
    CHECK(snapshot.key_released(input::PhysicalKey::w));
    CHECK_FALSE(snapshot.mouse_button_held(input::PhysicalMouseButton::left));
    CHECK(snapshot.mouse_button_released(input::PhysicalMouseButton::left));
    CHECK(snapshot.relative_mouse_delta() == input::RelativeMouseDelta{});
    CHECK(snapshot.reset_reason() == input::InputResetReason::focus_lost);
    CHECK(snapshot.statistics().released_by_focus_loss_count == 2U);
    CHECK(snapshot.statistics().ignored_duplicate_release_count == 1U);
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_motion(100, 100));
    const auto regained = tracker.publish_snapshot();
    CHECK(regained.focused());
    CHECK_FALSE(regained.captured());
    CHECK_FALSE(regained.key_held(input::PhysicalKey::w));
    CHECK(regained.relative_mouse_delta() == input::RelativeMouseDelta{});
    CHECK(regained.statistics().ignored_uncaptured_motion_count == 1U);
    tracker.end_frame();
}

TEST_CASE("Unfocused presses cannot become stuck state after focus regain", "[input][tracker][focus]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(
        input::InputEvent::mouse_button_pressed(input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::mouse_motion(10, 20));
    tracker.apply_event(input::InputEvent::mouse_wheel(1.0, 1.0));
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto snapshot = tracker.publish_snapshot();
    CHECK(snapshot.focused());
    CHECK(snapshot.captured());
    CHECK_FALSE(snapshot.key_held(input::PhysicalKey::w));
    CHECK_FALSE(snapshot.mouse_button_held(input::PhysicalMouseButton::left));
    CHECK(snapshot.relative_mouse_delta() == input::RelativeMouseDelta{});
    CHECK(snapshot.wheel_delta() == input::MouseWheelDelta{});
    CHECK(snapshot.statistics().ignored_unfocused_event_count == 4U);
    tracker.end_frame();
}

TEST_CASE("Focus loss discards earlier press edges across same-frame regain",
    "[input][tracker][focus][ordering]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    tracker.apply_event(input::InputEvent::focus_lost());
    tracker.apply_event(input::InputEvent::focus_gained());
    const auto snapshot = tracker.publish_snapshot();
    CHECK(snapshot.focused());
    CHECK_FALSE(snapshot.captured());
    CHECK_FALSE(snapshot.key_held(input::PhysicalKey::w));
    CHECK_FALSE(snapshot.key_pressed(input::PhysicalKey::w));
    CHECK(snapshot.key_released(input::PhysicalKey::w));
    CHECK_FALSE(snapshot.mouse_button_held(input::PhysicalMouseButton::left));
    CHECK_FALSE(snapshot.mouse_button_pressed(
        input::PhysicalMouseButton::left));
    CHECK(snapshot.mouse_button_released(
        input::PhysicalMouseButton::left));
    CHECK(snapshot.reset_reason() == input::InputResetReason::focus_lost);
    tracker.end_frame();
}

TEST_CASE("Capture acquisition discards pre-capture mouse-button state",
    "[input][tracker][capture]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto gesture = tracker.publish_snapshot();
    CHECK(gesture.mouse_button_held(input::PhysicalMouseButton::left));
    CHECK(gesture.mouse_button_pressed(input::PhysicalMouseButton::left));
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_acquired());
    const auto captured = tracker.publish_snapshot();
    CHECK(captured.captured());
    CHECK_FALSE(captured.mouse_button_held(input::PhysicalMouseButton::left));
    CHECK_FALSE(captured.mouse_button_pressed(input::PhysicalMouseButton::left));
    CHECK_FALSE(captured.mouse_button_released(input::PhysicalMouseButton::left));
    CHECK(captured.statistics().discarded_pre_capture_button_count == 1U);
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    const auto physical_release = tracker.publish_snapshot();
    CHECK_FALSE(
        physical_release.mouse_button_released(input::PhysicalMouseButton::left));
    CHECK(physical_release.statistics().ignored_duplicate_release_count == 1U);
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto deliberate_attack = tracker.publish_snapshot();
    CHECK(deliberate_attack.mouse_button_held(input::PhysicalMouseButton::left));
    CHECK(deliberate_attack.mouse_button_pressed(
        input::PhysicalMouseButton::left));
    tracker.end_frame();
}

TEST_CASE("Capture acquisition suppresses duplicate mouse down until release",
    "[input][tracker][capture][ordering]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    static_cast<void>(tracker.publish_snapshot());
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto duplicate_down = tracker.publish_snapshot();
    CHECK(duplicate_down.capture_state_at_frame_start() ==
        input::InputCaptureState::released);
    CHECK(duplicate_down.captured());
    CHECK_FALSE(duplicate_down.mouse_button_held(
        input::PhysicalMouseButton::left));
    CHECK_FALSE(duplicate_down.mouse_button_pressed(
        input::PhysicalMouseButton::left));
    CHECK(duplicate_down.statistics().discarded_pre_capture_button_count ==
        1U);
    CHECK(duplicate_down.statistics().ignored_repeat_count == 1U);
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_released(
        input::PhysicalMouseButton::left));
    const auto clearing_up = tracker.publish_snapshot();
    CHECK_FALSE(clearing_up.mouse_button_released(
        input::PhysicalMouseButton::left));
    CHECK(clearing_up.statistics().ignored_duplicate_release_count == 1U);
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_button_pressed(
        input::PhysicalMouseButton::left));
    const auto fresh_down = tracker.publish_snapshot();
    CHECK(fresh_down.capture_state_at_frame_start() ==
        input::InputCaptureState::captured);
    CHECK(fresh_down.mouse_button_held(input::PhysicalMouseButton::left));
    CHECK(fresh_down.mouse_button_pressed(input::PhysicalMouseButton::left));
    tracker.end_frame();
}

TEST_CASE("Mouse and wheel accumulation clamp only at configured bounds", "[input][tracker][limits]")
{
    input::InputStateLimits limits;
    limits.maximum_relative_mouse_delta_per_axis = 10;
    limits.maximum_wheel_delta_per_axis = 2.0;
    input::InputStateTracker tracker{limits};
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    tracker.apply_event(input::InputEvent::mouse_motion(6, -6));
    tracker.apply_event(input::InputEvent::mouse_motion(4, -4));
    tracker.apply_event(input::InputEvent::mouse_wheel(1.0, -1.0));
    tracker.apply_event(input::InputEvent::mouse_wheel(1.0, -1.0));
    auto exact = tracker.publish_snapshot();
    CHECK(exact.relative_mouse_delta() == input::RelativeMouseDelta{10, -10});
    CHECK(exact.wheel_delta() == input::MouseWheelDelta{2.0, -2.0});
    CHECK(exact.statistics().relative_mouse_clamp_count == 0U);
    CHECK(exact.statistics().wheel_clamp_count == 0U);
    tracker.end_frame();

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_motion(11, -11));
    tracker.apply_event(input::InputEvent::mouse_wheel(2.01, -2.01));
    const auto clamped = tracker.publish_snapshot();
    CHECK(clamped.relative_mouse_delta() == input::RelativeMouseDelta{10, -10});
    CHECK(clamped.wheel_delta() == input::MouseWheelDelta{2.0, -2.0});
    CHECK(clamped.statistics().relative_mouse_clamp_count == 2U);
    CHECK(clamped.statistics().wheel_clamp_count == 2U);
    tracker.end_frame();
}

TEST_CASE("Input frame and event limits reject limit plus one transactionally", "[input][tracker][limits]")
{
    input::InputStateLimits limits;
    limits.maximum_events_per_frame = 2U;
    limits.maximum_input_frames = 1U;
    input::InputStateTracker tracker{limits};
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    require_input_error(input::InputStateErrorCode::event_limit_exceeded, [&] {
        tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::a));
    });
    const auto snapshot = tracker.publish_snapshot();
    CHECK(snapshot.statistics().processed_event_count == 2U);
    CHECK(snapshot.key_held(input::PhysicalKey::w));
    CHECK_FALSE(snapshot.key_held(input::PhysicalKey::a));
    tracker.end_frame();

    require_input_error(input::InputStateErrorCode::input_frame_limit_exceeded, [&] {
        tracker.begin_frame();
    });
    REQUIRE(tracker.last_published_snapshot() != nullptr);
    CHECK(tracker.last_published_snapshot()->sequence() == 1U);
}

TEST_CASE("Invalid input can be cancelled without changing persistent state", "[input][tracker][transaction]")
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    const auto baseline = tracker.publish_snapshot();
    tracker.end_frame();
    REQUIRE(baseline.focused());

    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
    require_input_error(input::InputStateErrorCode::non_finite_wheel_delta, [&] {
        tracker.apply_event(input::InputEvent::mouse_wheel(
            std::numeric_limits<double>::quiet_NaN(), 0.0));
    });
    tracker.cancel_frame();

    tracker.begin_frame();
    const auto after_cancel = tracker.publish_snapshot();
    CHECK(after_cancel.focused());
    CHECK_FALSE(after_cancel.key_held(input::PhysicalKey::w));
    CHECK(after_cancel.sequence() == 2U);
    tracker.end_frame();
}

TEST_CASE("Null and scripted sources publish deterministic bounded frame events", "[input][source]")
{
    input::NullInputSource null_source;
    input::InputEvent event = input::InputEvent::focus_lost();
    null_source.begin_frame();
    REQUIRE(null_source.poll_event(event));
    CHECK(event.type() == input::InputEventType::focus_gained);
    CHECK_FALSE(null_source.poll_event(event));
    null_source.end_frame();
    null_source.begin_frame();
    CHECK_FALSE(null_source.poll_event(event));
    null_source.end_frame();

    input::ScriptedInputSource source{{
        {input::InputEvent::focus_gained(), input::InputEvent::capture_acquired()},
        {input::InputEvent::key_pressed(input::PhysicalKey::w)},
        {input::InputEvent::key_released(input::PhysicalKey::w)},
    }};
    CHECK(source.frame_count() == 3U);
    CHECK(source.total_event_count() == 4U);
    for (std::size_t frame = 0U; frame < 3U; ++frame) {
        source.begin_frame();
        std::size_t count = 0U;
        while (source.poll_event(event)) {
            ++count;
        }
        CHECK(count == (frame == 0U ? 2U : 1U));
        source.end_frame();
    }
    CHECK(source.exhausted());
    source.begin_frame();
    CHECK_FALSE(source.poll_event(event));
    source.end_frame();
}

TEST_CASE("Null input source rejects invalid or contradictory state",
          "[input][source][limits][enum]")
{
    const auto require_invalid = [](const input::NullInputSourceConfig config) {
        try {
            const input::NullInputSource source{config};
            static_cast<void>(source);
            FAIL("Expected an InputSourceException");
        } catch (const input::InputSourceException& error) {
            CHECK(error.code() ==
                input::InputSourceErrorCode::invalid_configuration);
        }
    };

    require_invalid(input::NullInputSourceConfig{
        static_cast<input::InputFocusState>(0xffU),
        input::InputCaptureState::released});
    require_invalid(input::NullInputSourceConfig{
        input::InputFocusState::focused,
        static_cast<input::InputCaptureState>(0xffU)});
    require_invalid(input::NullInputSourceConfig{
        input::InputFocusState::unfocused,
        input::InputCaptureState::captured});
}

TEST_CASE("Scripted sources enforce exact construction limits", "[input][source][limits]")
{
    input::ScriptedInputSourceLimits limits;
    limits.maximum_frames = 1U;
    limits.maximum_events_per_frame = 2U;
    limits.maximum_total_events = 2U;
    CHECK_NOTHROW(input::ScriptedInputSource{
        {{input::InputEvent::focus_gained(), input::InputEvent::focus_lost()}}, limits});

    try {
        const input::ScriptedInputSource too_many{{
            {
                input::InputEvent::focus_gained(),
                input::InputEvent::focus_lost(),
                input::InputEvent::focus_gained(),
            },
        }, limits};
        static_cast<void>(too_many);
        FAIL("Expected an InputSourceException");
    } catch (const input::InputSourceException& error) {
        CHECK(error.code() == input::InputSourceErrorCode::per_frame_event_limit_exceeded);
    }
}

} // namespace

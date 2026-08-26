#include <hlclient/input/input_state_tracker.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::input {
namespace {

[[nodiscard]] constexpr std::size_t key_index(const PhysicalKey key) noexcept
{
    return static_cast<std::size_t>(key);
}

[[nodiscard]] constexpr std::size_t mouse_button_index(
    const PhysicalMouseButton button) noexcept
{
    return static_cast<std::size_t>(button);
}

[[noreturn]] void fail(const InputStateErrorCode code, const char* message)
{
    throw InputStateException{code, message};
}

[[nodiscard]] std::int32_t bounded_sum(
    const std::int32_t current,
    const std::int32_t incoming,
    const std::int32_t magnitude_limit,
    std::size_t& clamp_count) noexcept
{
    const auto sum = static_cast<std::int64_t>(current) + static_cast<std::int64_t>(incoming);
    const auto limit = static_cast<std::int64_t>(magnitude_limit);
    if (sum > limit) {
        ++clamp_count;
        return magnitude_limit;
    }
    if (sum < -limit) {
        ++clamp_count;
        return -magnitude_limit;
    }
    return static_cast<std::int32_t>(sum);
}

[[nodiscard]] double bounded_sum(
    const double current,
    const double incoming,
    const double magnitude_limit,
    std::size_t& clamp_count) noexcept
{
    const auto sum = current + incoming;
    if (!std::isfinite(sum)) {
        ++clamp_count;
        return std::signbit(incoming) ? -magnitude_limit : magnitude_limit;
    }
    if (sum > magnitude_limit) {
        ++clamp_count;
        return magnitude_limit;
    }
    if (sum < -magnitude_limit) {
        ++clamp_count;
        return -magnitude_limit;
    }
    return sum;
}

} // namespace

InputStateException::InputStateException(
    const InputStateErrorCode code,
    std::string message)
    : std::runtime_error{std::move(message)}, code_{code}
{
}

InputStateTracker::InputStateTracker(const InputStateLimits limits) : limits_{limits}
{
    if (limits_.maximum_events_per_frame == 0U ||
        limits_.maximum_events_per_frame > InputStateLimits::hard_maximum_events_per_frame ||
        limits_.maximum_relative_mouse_delta_per_axis <= 0 ||
        limits_.maximum_relative_mouse_delta_per_axis >
            InputStateLimits::hard_maximum_relative_mouse_delta_per_axis ||
        !std::isfinite(limits_.maximum_wheel_delta_per_axis) ||
        limits_.maximum_wheel_delta_per_axis <= 0.0 ||
        limits_.maximum_wheel_delta_per_axis >
            InputStateLimits::hard_maximum_wheel_delta_per_axis ||
        limits_.maximum_input_frames == 0U) {
        fail(InputStateErrorCode::invalid_limits, "Input state limits are invalid");
    }
}

void InputStateTracker::begin_frame()
{
    if (frame_active_) {
        fail(InputStateErrorCode::frame_already_active, "An input frame is already active");
    }
    if (published_frame_count_ == std::numeric_limits<std::uint64_t>::max()) {
        fail(InputStateErrorCode::sequence_exhausted, "The input snapshot sequence is exhausted");
    }
    if (published_frame_count_ >= limits_.maximum_input_frames) {
        fail(InputStateErrorCode::input_frame_limit_exceeded, "The input frame limit is exhausted");
    }

    frame_start_state_ = persistent_;
    pressed_keys_.reset();
    released_keys_.reset();
    pressed_mouse_buttons_.reset();
    released_mouse_buttons_.reset();
    capture_discarded_mouse_buttons_.reset();
    relative_mouse_delta_ = {};
    wheel_delta_ = {};
    reset_reason_ = InputResetReason::none;
    statistics_ = {};
    frame_active_ = true;
    frame_published_ = false;
}

void InputStateTracker::require_active_unpublished() const
{
    if (!frame_active_) {
        fail(InputStateErrorCode::frame_not_active, "No input frame is active");
    }
    if (frame_published_) {
        fail(InputStateErrorCode::frame_already_published, "The input frame is already published");
    }
}

void InputStateTracker::validate_event(const InputEvent& event) const
{
    switch (event.type()) {
    case InputEventType::key_pressed:
    case InputEventType::key_released:
        if (!is_valid(event.key())) {
            fail(InputStateErrorCode::invalid_key, "Input event contains an invalid physical key");
        }
        break;
    case InputEventType::mouse_button_pressed:
    case InputEventType::mouse_button_released:
        if (!is_valid(event.mouse_button())) {
            fail(
                InputStateErrorCode::invalid_mouse_button,
                "Input event contains an invalid physical mouse button");
        }
        break;
    case InputEventType::mouse_wheel: {
        const auto delta = event.wheel_delta();
        if (!std::isfinite(delta.horizontal) || !std::isfinite(delta.vertical)) {
            fail(
                InputStateErrorCode::non_finite_wheel_delta,
                "Input event contains a non-finite wheel delta");
        }
        break;
    }
    case InputEventType::capture_acquired:
        if (persistent_.focus_state != InputFocusState::focused) {
            fail(
                InputStateErrorCode::capture_acquired_while_unfocused,
                "Relative mouse capture cannot be acquired while unfocused");
        }
        break;
    case InputEventType::focus_gained:
    case InputEventType::focus_lost:
    case InputEventType::mouse_motion:
    case InputEventType::capture_released:
        break;
    }
}

void InputStateTracker::apply_focus_lost() noexcept
{
    released_keys_ |= persistent_.held_keys;
    released_mouse_buttons_ |= persistent_.held_mouse_buttons;
    statistics_.released_by_focus_loss_count +=
        persistent_.held_keys.count() + persistent_.held_mouse_buttons.count();
    persistent_.held_keys.reset();
    persistent_.held_mouse_buttons.reset();
    persistent_.suppressed_mouse_buttons_until_release.reset();
    // Press edges observed before the loss belong to the discarded focus
    // domain. A later focus-gained event in the same platform frame must not
    // resurrect them as actions or a capture gesture.
    pressed_keys_.reset();
    pressed_mouse_buttons_.reset();
    persistent_.focus_state = InputFocusState::unfocused;
    persistent_.capture_state = InputCaptureState::released;
    relative_mouse_delta_ = {};
    wheel_delta_ = {};
    reset_reason_ = InputResetReason::focus_lost;
}

void InputStateTracker::accumulate_relative_mouse(const RelativeMouseDelta delta) noexcept
{
    relative_mouse_delta_.x = bounded_sum(
        relative_mouse_delta_.x,
        delta.x,
        limits_.maximum_relative_mouse_delta_per_axis,
        statistics_.relative_mouse_clamp_count);
    relative_mouse_delta_.y = bounded_sum(
        relative_mouse_delta_.y,
        delta.y,
        limits_.maximum_relative_mouse_delta_per_axis,
        statistics_.relative_mouse_clamp_count);
}

void InputStateTracker::accumulate_wheel(const MouseWheelDelta delta) noexcept
{
    wheel_delta_.horizontal = bounded_sum(
        wheel_delta_.horizontal,
        delta.horizontal,
        limits_.maximum_wheel_delta_per_axis,
        statistics_.wheel_clamp_count);
    wheel_delta_.vertical = bounded_sum(
        wheel_delta_.vertical,
        delta.vertical,
        limits_.maximum_wheel_delta_per_axis,
        statistics_.wheel_clamp_count);
}

void InputStateTracker::apply_event(const InputEvent& event)
{
    require_active_unpublished();
    validate_event(event);
    if (statistics_.processed_event_count >= limits_.maximum_events_per_frame) {
        fail(InputStateErrorCode::event_limit_exceeded, "The per-frame input event limit is exhausted");
    }

    switch (event.type()) {
    case InputEventType::focus_gained:
        persistent_.focus_state = InputFocusState::focused;
        persistent_.capture_state = InputCaptureState::released;
        break;
    case InputEventType::focus_lost:
        apply_focus_lost();
        break;
    case InputEventType::key_pressed: {
        if (persistent_.focus_state != InputFocusState::focused) {
            ++statistics_.ignored_unfocused_event_count;
            break;
        }
        const auto index = key_index(event.key());
        if (event.repeated() || persistent_.held_keys.test(index)) {
            ++statistics_.ignored_repeat_count;
            break;
        }
        persistent_.held_keys.set(index);
        pressed_keys_.set(index);
        break;
    }
    case InputEventType::key_released: {
        const auto index = key_index(event.key());
        if (!persistent_.held_keys.test(index)) {
            ++statistics_.ignored_duplicate_release_count;
            break;
        }
        persistent_.held_keys.reset(index);
        released_keys_.set(index);
        break;
    }
    case InputEventType::mouse_button_pressed: {
        if (persistent_.focus_state != InputFocusState::focused) {
            ++statistics_.ignored_unfocused_event_count;
            break;
        }
        const auto index = mouse_button_index(event.mouse_button());
        if (persistent_.suppressed_mouse_buttons_until_release.test(index)) {
            ++statistics_.ignored_repeat_count;
            break;
        }
        if (persistent_.held_mouse_buttons.test(index)) {
            ++statistics_.ignored_repeat_count;
            break;
        }
        persistent_.held_mouse_buttons.set(index);
        pressed_mouse_buttons_.set(index);
        break;
    }
    case InputEventType::mouse_button_released: {
        const auto index = mouse_button_index(event.mouse_button());
        if (persistent_.suppressed_mouse_buttons_until_release.test(index)) {
            persistent_.suppressed_mouse_buttons_until_release.reset(index);
            ++statistics_.ignored_duplicate_release_count;
            break;
        }
        if (!persistent_.held_mouse_buttons.test(index)) {
            ++statistics_.ignored_duplicate_release_count;
            break;
        }
        persistent_.held_mouse_buttons.reset(index);
        released_mouse_buttons_.set(index);
        break;
    }
    case InputEventType::mouse_motion:
        if (persistent_.focus_state != InputFocusState::focused) {
            ++statistics_.ignored_unfocused_event_count;
        } else if (persistent_.capture_state != InputCaptureState::captured) {
            ++statistics_.ignored_uncaptured_motion_count;
        } else {
            accumulate_relative_mouse(event.relative_mouse_delta());
        }
        break;
    case InputEventType::mouse_wheel:
        if (persistent_.focus_state != InputFocusState::focused) {
            ++statistics_.ignored_unfocused_event_count;
        } else {
            accumulate_wheel(event.wheel_delta());
        }
        break;
    case InputEventType::capture_acquired:
        if (persistent_.capture_state == InputCaptureState::released) {
            const auto discarded_buttons =
                persistent_.held_mouse_buttons |
                pressed_mouse_buttons_ |
                released_mouse_buttons_;
            statistics_.discarded_pre_capture_button_count +=
                discarded_buttons.count();
            capture_discarded_mouse_buttons_ |=
                discarded_buttons;
            persistent_.suppressed_mouse_buttons_until_release |=
                persistent_.held_mouse_buttons;
            persistent_.held_mouse_buttons.reset();
            pressed_mouse_buttons_.reset();
            released_mouse_buttons_.reset();
        }
        persistent_.capture_state = InputCaptureState::captured;
        break;
    case InputEventType::capture_released:
        persistent_.capture_state = InputCaptureState::released;
        relative_mouse_delta_ = {};
        break;
    }
    ++statistics_.processed_event_count;
}

InputSnapshot InputStateTracker::publish_snapshot()
{
    require_active_unpublished();
    const auto sequence = published_frame_count_ + 1U;
    auto snapshot = InputSnapshot{
        sequence,
        persistent_.focus_state,
        persistent_.capture_state,
        frame_start_state_.capture_state,
        frame_start_state_.held_keys,
        persistent_.held_keys,
        pressed_keys_,
        released_keys_,
        frame_start_state_.held_mouse_buttons,
        persistent_.held_mouse_buttons,
        pressed_mouse_buttons_,
        released_mouse_buttons_,
        capture_discarded_mouse_buttons_,
        relative_mouse_delta_,
        wheel_delta_,
        reset_reason_,
        statistics_,
    };
    last_published_snapshot_.emplace(snapshot);
    published_frame_count_ = sequence;
    frame_published_ = true;
    return snapshot;
}

void InputStateTracker::end_frame()
{
    if (!frame_active_) {
        fail(InputStateErrorCode::frame_not_active, "No input frame is active");
    }
    if (!frame_published_) {
        fail(InputStateErrorCode::frame_not_published, "The input frame was not published");
    }
    frame_active_ = false;
    frame_published_ = false;
}

void InputStateTracker::cancel_frame() noexcept
{
    if (!frame_active_) {
        return;
    }
    if (!frame_published_) {
        persistent_ = frame_start_state_;
    }
    frame_active_ = false;
    frame_published_ = false;
}

} // namespace hlclient::input

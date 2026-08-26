#include <hlclient/input/input_source.hpp>

#include <limits>
#include <utility>

namespace hlclient::input {
namespace {

[[noreturn]] void fail(const InputSourceErrorCode code, const char* message)
{
    throw InputSourceException{code, message};
}

void validate_script_limits(const ScriptedInputSourceLimits& limits)
{
    if (limits.maximum_frames == 0U ||
        limits.maximum_frames > ScriptedInputSourceLimits::hard_maximum_frames ||
        limits.maximum_events_per_frame == 0U ||
        limits.maximum_events_per_frame >
            ScriptedInputSourceLimits::hard_maximum_events_per_frame ||
        limits.maximum_total_events == 0U ||
        limits.maximum_total_events > ScriptedInputSourceLimits::hard_maximum_total_events) {
        fail(InputSourceErrorCode::invalid_configuration, "Scripted input limits are invalid");
    }
}

} // namespace

InputSourceException::InputSourceException(
    const InputSourceErrorCode code,
    std::string message)
    : std::runtime_error{std::move(message)}, code_{code}
{
}

NullInputSource::NullInputSource(const NullInputSourceConfig config) : config_{config}
{
    const bool valid_focus = config_.focus_state == InputFocusState::focused ||
        config_.focus_state == InputFocusState::unfocused;
    const bool valid_capture =
        config_.capture_state == InputCaptureState::captured ||
        config_.capture_state == InputCaptureState::released;
    if (!valid_focus || !valid_capture) {
        fail(InputSourceErrorCode::invalid_configuration,
            "Null input source state enums are invalid");
    }
    if (config_.capture_state == InputCaptureState::captured &&
        config_.focus_state != InputFocusState::focused) {
        fail(
            InputSourceErrorCode::invalid_configuration,
            "A null input source cannot be captured while unfocused");
    }
}

void NullInputSource::begin_frame()
{
    if (frame_active_) {
        fail(InputSourceErrorCode::frame_already_active, "A null input frame is already active");
    }
    next_initial_event_ = 0U;
    frame_active_ = true;
}

bool NullInputSource::poll_event(InputEvent& event)
{
    if (!frame_active_) {
        fail(InputSourceErrorCode::frame_not_active, "No null input frame is active");
    }
    if (initial_state_published_) {
        return false;
    }

    if (next_initial_event_ == 0U) {
        event = config_.focus_state == InputFocusState::focused
            ? InputEvent::focus_gained()
            : InputEvent::focus_lost();
        ++next_initial_event_;
        return true;
    }
    if (next_initial_event_ == 1U &&
        config_.capture_state == InputCaptureState::captured) {
        event = InputEvent::capture_acquired();
        ++next_initial_event_;
        return true;
    }
    return false;
}

void NullInputSource::end_frame()
{
    if (!frame_active_) {
        fail(InputSourceErrorCode::frame_not_active, "No null input frame is active");
    }
    const auto required_events = config_.capture_state == InputCaptureState::captured ? 2U : 1U;
    if (!initial_state_published_ && next_initial_event_ < required_events) {
        fail(InputSourceErrorCode::frame_not_consumed, "The null input frame was not consumed");
    }
    initial_state_published_ = true;
    frame_active_ = false;
}

ScriptedInputSource::ScriptedInputSource(
    Script frames,
    const ScriptedInputSourceLimits limits)
    : frames_{std::move(frames)}, limits_{limits}
{
    validate_script_limits(limits_);
    if (frames_.size() > limits_.maximum_frames) {
        fail(InputSourceErrorCode::frame_limit_exceeded, "The scripted input frame limit is exceeded");
    }

    for (const auto& frame : frames_) {
        if (frame.size() > limits_.maximum_events_per_frame) {
            fail(
                InputSourceErrorCode::per_frame_event_limit_exceeded,
                "A scripted input frame exceeds its event limit");
        }
        if (frame.size() > std::numeric_limits<std::size_t>::max() - total_event_count_ ||
            total_event_count_ + frame.size() > limits_.maximum_total_events) {
            fail(
                InputSourceErrorCode::total_event_limit_exceeded,
                "The scripted input total event limit is exceeded");
        }
        total_event_count_ += frame.size();
    }
}

void ScriptedInputSource::begin_frame()
{
    if (frame_active_) {
        fail(
            InputSourceErrorCode::frame_already_active,
            "A scripted input frame is already active");
    }
    event_index_ = 0U;
    frame_active_ = true;
}

bool ScriptedInputSource::poll_event(InputEvent& event)
{
    if (!frame_active_) {
        fail(InputSourceErrorCode::frame_not_active, "No scripted input frame is active");
    }
    if (frame_index_ >= frames_.size() || event_index_ >= frames_[frame_index_].size()) {
        return false;
    }
    event = frames_[frame_index_][event_index_];
    ++event_index_;
    return true;
}

void ScriptedInputSource::end_frame()
{
    if (!frame_active_) {
        fail(InputSourceErrorCode::frame_not_active, "No scripted input frame is active");
    }
    if (frame_index_ < frames_.size() && event_index_ != frames_[frame_index_].size()) {
        fail(InputSourceErrorCode::frame_not_consumed, "The scripted input frame was not consumed");
    }
    if (frame_index_ < frames_.size()) {
        ++frame_index_;
    }
    frame_active_ = false;
}

} // namespace hlclient::input

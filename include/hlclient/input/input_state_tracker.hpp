#pragma once

#include <hlclient/input/input_snapshot.hpp>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace hlclient::input {

struct InputStateLimits final {
    static constexpr std::size_t default_maximum_events_per_frame = 1'024U;
    static constexpr std::size_t hard_maximum_events_per_frame = 8'192U;
    static constexpr std::int32_t default_maximum_relative_mouse_delta_per_axis =
        1'000'000;
    static constexpr std::int32_t hard_maximum_relative_mouse_delta_per_axis =
        std::numeric_limits<std::int32_t>::max();
    static constexpr double default_maximum_wheel_delta_per_axis = 10'000.0;
    static constexpr double hard_maximum_wheel_delta_per_axis = 1'000'000'000.0;

    std::size_t maximum_events_per_frame{default_maximum_events_per_frame};
    std::int32_t maximum_relative_mouse_delta_per_axis{
        default_maximum_relative_mouse_delta_per_axis};
    double maximum_wheel_delta_per_axis{default_maximum_wheel_delta_per_axis};
    std::uint64_t maximum_input_frames{std::numeric_limits<std::uint64_t>::max()};
};

enum class InputStateErrorCode : std::uint8_t {
    invalid_limits,
    frame_already_active,
    frame_not_active,
    frame_already_published,
    frame_not_published,
    event_limit_exceeded,
    invalid_key,
    invalid_mouse_button,
    non_finite_wheel_delta,
    capture_acquired_while_unfocused,
    input_frame_limit_exceeded,
    sequence_exhausted,
};

class InputStateException final : public std::runtime_error {
public:
    InputStateException(InputStateErrorCode code, std::string message);

    [[nodiscard]] InputStateErrorCode code() const noexcept { return code_; }

private:
    InputStateErrorCode code_;
};

class InputStateTracker final {
public:
    explicit InputStateTracker(InputStateLimits limits = {});

    void begin_frame();
    void apply_event(const InputEvent& event);
    [[nodiscard]] InputSnapshot publish_snapshot();
    void end_frame();
    void cancel_frame() noexcept;

    [[nodiscard]] bool frame_active() const noexcept { return frame_active_; }
    [[nodiscard]] std::uint64_t published_frame_count() const noexcept
    {
        return published_frame_count_;
    }
    [[nodiscard]] const InputSnapshot* last_published_snapshot() const noexcept
    {
        return last_published_snapshot_ ? &*last_published_snapshot_ : nullptr;
    }
    [[nodiscard]] const InputStateLimits& limits() const noexcept { return limits_; }

private:
    using KeyBits = std::bitset<physical_key_count>;
    using MouseButtonBits = std::bitset<physical_mouse_button_count>;

    struct PersistentState final {
        InputFocusState focus_state{InputFocusState::unfocused};
        InputCaptureState capture_state{InputCaptureState::released};
        KeyBits held_keys{};
        MouseButtonBits held_mouse_buttons{};
        MouseButtonBits suppressed_mouse_buttons_until_release{};
    };

    void require_active_unpublished() const;
    void validate_event(const InputEvent& event) const;
    void apply_focus_lost() noexcept;
    void accumulate_relative_mouse(RelativeMouseDelta delta) noexcept;
    void accumulate_wheel(MouseWheelDelta delta) noexcept;

    InputStateLimits limits_{};
    PersistentState persistent_{};
    PersistentState frame_start_state_{};
    KeyBits pressed_keys_{};
    KeyBits released_keys_{};
    MouseButtonBits pressed_mouse_buttons_{};
    MouseButtonBits released_mouse_buttons_{};
    MouseButtonBits capture_discarded_mouse_buttons_{};
    RelativeMouseDelta relative_mouse_delta_{};
    MouseWheelDelta wheel_delta_{};
    InputResetReason reset_reason_{InputResetReason::none};
    InputStateStatistics statistics_{};
    std::uint64_t published_frame_count_{0U};
    bool frame_active_{false};
    bool frame_published_{false};
    std::optional<InputSnapshot> last_published_snapshot_{};
};

} // namespace hlclient::input

#include <hlclient/gameplay_input/gameplay_input_intent.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace hlclient::gameplay_input {

namespace {

struct ActionStates {
    GameplayInputActionMask held{0U};
    GameplayInputActionMask pressed{0U};
    GameplayInputActionMask released{0U};
};

[[nodiscard]] GameplayInputIntentBuildResult failure(
    const GameplayInputIntentErrorCode code,
    const std::string_view message) noexcept
{
    GameplayInputIntentBuildResult result;
    result.error = GameplayInputIntentError{code, message};
    return result;
}

[[nodiscard]] bool action_is_set(
    const GameplayInputActionMask actions,
    const GameplayInputAction action) noexcept
{
    return gameplay_input_action_is_set(actions, action);
}

[[nodiscard]] float opposing_axis(
    const GameplayInputActionMask held_actions,
    const GameplayInputAction positive,
    const GameplayInputAction negative) noexcept
{
    const auto positive_value = action_is_set(held_actions, positive) ? 1 : 0;
    const auto negative_value = action_is_set(held_actions, negative) ? 1 : 0;
    return static_cast<float>(positive_value - negative_value);
}

[[nodiscard]] GameplayButtonMask buttons_from_actions(
    const GameplayInputActionMask actions) noexcept
{
    struct Mapping {
        GameplayInputAction action;
        GameplayButton button;
    };
    constexpr std::array mappings{
        Mapping{GameplayInputAction::jump, GameplayButton::jump},
        Mapping{GameplayInputAction::duck, GameplayButton::duck},
        Mapping{GameplayInputAction::use, GameplayButton::use},
        Mapping{GameplayInputAction::reload, GameplayButton::reload},
        Mapping{GameplayInputAction::speed, GameplayButton::speed},
        Mapping{GameplayInputAction::walk, GameplayButton::walk},
        Mapping{GameplayInputAction::attack_primary,
            GameplayButton::attack_primary},
        Mapping{GameplayInputAction::attack_secondary,
            GameplayButton::attack_secondary},
        Mapping{GameplayInputAction::scoreboard, GameplayButton::scoreboard},
    };

    GameplayButtonMask result = 0U;
    for (const auto& mapping : mappings) {
        if (action_is_set(actions, mapping.action)) {
            result |= gameplay_button_mask(mapping.button);
        }
    }
    return result;
}

[[nodiscard]] bool is_uncaptured_left_capture_gesture(
    const InputBinding& binding,
    const bool captured) noexcept
{
    return !captured &&
        binding.kind() == InputBindingKind::mouse_button &&
        binding.physical_mouse_button() == input::PhysicalMouseButton::left;
}

[[nodiscard]] ActionStates collect_action_states(
    const input::InputSnapshot& snapshot,
    const GameplayInputBindings& bindings,
    const bool focused,
    const bool captured) noexcept
{
    GameplayInputActionMask held_at_frame_start = 0U;
    GameplayInputActionMask held_now = 0U;
    GameplayInputActionMask physical_pressed = 0U;
    GameplayInputActionMask physical_released = 0U;
    for (const auto& binding : bindings.bindings()) {
        if (is_uncaptured_left_capture_gesture(binding, captured)) {
            // The uncaptured left button is a capture gesture, never an
            // action press. If it represented a held action at the previous
            // captured frame boundary, crossing into the uncaptured domain
            // still publishes exactly one release edge. Ordinary uncaptured
            // clicks cannot fabricate that release.
            if (snapshot.capture_state_at_frame_start() ==
                    input::InputCaptureState::captured &&
                snapshot.mouse_button_held_at_frame_start(
                    input::PhysicalMouseButton::left)) {
                physical_released |=
                    gameplay_input_action_mask(binding.action());
            }
            continue;
        }

        bool held_at_start = false;
        bool held = false;
        bool pressed = false;
        bool released = false;
        if (binding.kind() == InputBindingKind::key &&
            binding.physical_key()) {
            held_at_start = snapshot.key_held_at_frame_start(
                *binding.physical_key());
            held = snapshot.key_held(*binding.physical_key());
            pressed = snapshot.key_pressed(*binding.physical_key());
            released = snapshot.key_released(*binding.physical_key());
        } else if (binding.kind() == InputBindingKind::mouse_button &&
                   binding.physical_mouse_button()) {
            const auto button = *binding.physical_mouse_button();
            held_at_start = snapshot.mouse_button_held_at_frame_start(button);
            held = snapshot.mouse_button_held(button);
            pressed = snapshot.mouse_button_pressed(button);
            released = snapshot.mouse_button_released(button);
            if (snapshot.mouse_button_discarded_by_capture(button)) {
                // Capture acquisition starts a fresh physical-button domain.
                // Preserve the release edge of an action published at the
                // previous frame boundary, but never turn the uncaptured left
                // capture gesture into an attack release.
                released = released ||
                    (held_at_start &&
                        (button != input::PhysicalMouseButton::left ||
                            snapshot.capture_state_at_frame_start() ==
                                input::InputCaptureState::captured));
                held_at_start = false;
            }
        }

        const auto bit = gameplay_input_action_mask(binding.action());
        if (held_at_start) {
            held_at_frame_start |= bit;
        }
        if (focused && held) {
            held_now |= bit;
        }
        if (focused && pressed) {
            physical_pressed |= bit;
        }
        if (released) {
            physical_released |= bit;
        }
    }
    if (snapshot.reset_reason() == input::InputResetReason::focus_lost) {
        held_at_frame_start = 0U;
    }
    // Action edges are transitions of the OR-reduced physical bindings at
    // the frame boundary. Pressing a second binding for an already-held
    // action or releasing one while another remains held cannot fabricate an
    // action edge.
    return ActionStates{
        held_now,
        physical_pressed & ~held_at_frame_start,
        physical_released & ~held_now,
    };
}

} // namespace

bool valid_gameplay_input_intent_limits(
    const GameplayInputIntentLimits& limits) noexcept
{
    return std::isfinite(limits.maximum_sample_duration_seconds) &&
        limits.maximum_sample_duration_seconds > 0.0 &&
        limits.maximum_sample_duration_seconds <=
            kGameplayInputIntentHardLimits.maximum_sample_duration_seconds &&
        std::isfinite(limits.maximum_degrees_per_pixel) &&
        limits.maximum_degrees_per_pixel > 0.0 &&
        limits.maximum_degrees_per_pixel <=
            kGameplayInputIntentHardLimits.maximum_degrees_per_pixel &&
        std::isfinite(limits.maximum_look_delta_degrees_per_axis) &&
        limits.maximum_look_delta_degrees_per_axis > 0.0 &&
        limits.maximum_look_delta_degrees_per_axis <=
            kGameplayInputIntentHardLimits.
                maximum_look_delta_degrees_per_axis;
}

bool valid_mouse_look_config(
    const MouseLookConfig& config,
    const GameplayInputIntentLimits& limits) noexcept
{
    return valid_gameplay_input_intent_limits(limits) &&
        std::isfinite(config.degrees_per_pixel_x) &&
        config.degrees_per_pixel_x >= 0.001 &&
        config.degrees_per_pixel_x <= limits.maximum_degrees_per_pixel &&
        std::isfinite(config.degrees_per_pixel_y) &&
        config.degrees_per_pixel_y >= 0.001 &&
        config.degrees_per_pixel_y <= limits.maximum_degrees_per_pixel &&
        std::isfinite(config.maximum_delta_per_frame) &&
        config.maximum_delta_per_frame > 0.0 &&
        config.maximum_delta_per_frame <=
            limits.maximum_look_delta_degrees_per_axis;
}

std::string_view to_string(const GameplayInputIntentErrorCode code) noexcept
{
    switch (code) {
    case GameplayInputIntentErrorCode::invalid_limits:
        return "invalid_limits";
    case GameplayInputIntentErrorCode::invalid_mouse_look_config:
        return "invalid_mouse_look_config";
    case GameplayInputIntentErrorCode::non_finite_sample_duration:
        return "non_finite_sample_duration";
    case GameplayInputIntentErrorCode::sample_duration_out_of_range:
        return "sample_duration_out_of_range";
    case GameplayInputIntentErrorCode::non_finite_look_delta:
        return "non_finite_look_delta";
    case GameplayInputIntentErrorCode::compatibility_evidence_pending:
        return "compatibility_evidence_pending";
    }
    return "unknown";
}

GameplayInputIntent::GameplayInputIntent(
    const std::uint64_t input_sequence,
    const float forward_axis,
    const float side_axis,
    const float vertical_axis,
    const double look_delta_yaw_degrees,
    const double look_delta_pitch_degrees,
    const GameplayButtonMask held_buttons,
    const GameplayButtonMask pressed_buttons,
    const GameplayButtonMask released_buttons,
    const double wheel_delta_x,
    const double wheel_delta_y,
    const bool focused,
    const bool captured,
    const double sample_duration_seconds,
    const bool capture_mouse_requested,
    const bool release_mouse_requested) noexcept
    : input_sequence_(input_sequence),
      forward_axis_(forward_axis),
      side_axis_(side_axis),
      vertical_axis_(vertical_axis),
      look_delta_yaw_degrees_(look_delta_yaw_degrees),
      look_delta_pitch_degrees_(look_delta_pitch_degrees),
      held_buttons_(held_buttons),
      pressed_buttons_(pressed_buttons),
      released_buttons_(released_buttons),
      wheel_delta_x_(wheel_delta_x),
      wheel_delta_y_(wheel_delta_y),
      focused_(focused),
      captured_(captured),
      sample_duration_seconds_(sample_duration_seconds),
      capture_mouse_requested_(capture_mouse_requested),
      release_mouse_requested_(release_mouse_requested)
{
}

std::uint64_t GameplayInputIntent::input_sequence() const noexcept
{
    return input_sequence_;
}

float GameplayInputIntent::forward_axis() const noexcept
{
    return forward_axis_;
}

float GameplayInputIntent::side_axis() const noexcept
{
    return side_axis_;
}

float GameplayInputIntent::vertical_axis() const noexcept
{
    return vertical_axis_;
}

double GameplayInputIntent::look_delta_yaw_degrees() const noexcept
{
    return look_delta_yaw_degrees_;
}

double GameplayInputIntent::look_delta_pitch_degrees() const noexcept
{
    return look_delta_pitch_degrees_;
}

GameplayButtonMask GameplayInputIntent::held_buttons() const noexcept
{
    return held_buttons_;
}

GameplayButtonMask GameplayInputIntent::pressed_buttons() const noexcept
{
    return pressed_buttons_;
}

GameplayButtonMask GameplayInputIntent::released_buttons() const noexcept
{
    return released_buttons_;
}

double GameplayInputIntent::wheel_delta_x() const noexcept
{
    return wheel_delta_x_;
}

double GameplayInputIntent::wheel_delta_y() const noexcept
{
    return wheel_delta_y_;
}

bool GameplayInputIntent::focused() const noexcept
{
    return focused_;
}

bool GameplayInputIntent::captured() const noexcept
{
    return captured_;
}

double GameplayInputIntent::sample_duration_seconds() const noexcept
{
    return sample_duration_seconds_;
}

bool GameplayInputIntent::capture_mouse_requested() const noexcept
{
    return capture_mouse_requested_;
}

bool GameplayInputIntent::release_mouse_requested() const noexcept
{
    return release_mouse_requested_;
}

GameplayInputCompatibilityProfile
GameplayInputIntent::compatibility_profile() const noexcept
{
    return GameplayInputCompatibilityProfile::local_keyboard_mouse_intent_v1;
}

GameplayInputEvidenceProfile GameplayInputIntent::evidence_profile()
    const noexcept
{
    return GameplayInputEvidenceProfile::project_owned_input_semantics;
}

GameplayInputIntentBuildResult::operator bool() const noexcept
{
    return intent.has_value();
}

GameplayInputIntentBuildResult GameplayInputIntentBuilder::build(
    const input::InputSnapshot& snapshot,
    const GameplayInputBindings& bindings,
    const MouseLookConfig& mouse_look,
    const double sample_duration_seconds,
    const GameplayInputIntentLimits& limits,
    const GameplayInputCompatibilityProfile compatibility_profile)
    const noexcept
{
    if (!valid_gameplay_input_intent_limits(limits)) {
        return failure(GameplayInputIntentErrorCode::invalid_limits,
            "Gameplay input intent limits are invalid");
    }
    if (compatibility_profile != GameplayInputCompatibilityProfile::
            local_keyboard_mouse_intent_v1) {
        return failure(
            GameplayInputIntentErrorCode::compatibility_evidence_pending,
            "Stock usercmd input mapping remains evidence-pending for M4.6.2");
    }
    if (!valid_mouse_look_config(mouse_look, limits)) {
        return failure(
            GameplayInputIntentErrorCode::invalid_mouse_look_config,
            "Mouse-look configuration is non-finite or outside bounded project limits");
    }
    if (!std::isfinite(sample_duration_seconds)) {
        return failure(
            GameplayInputIntentErrorCode::non_finite_sample_duration,
            "Gameplay input sample duration must be finite");
    }
    if (sample_duration_seconds < 0.0 ||
        sample_duration_seconds > limits.maximum_sample_duration_seconds) {
        return failure(
            GameplayInputIntentErrorCode::sample_duration_out_of_range,
            "Gameplay input sample duration is outside the configured range");
    }

    const bool focused = snapshot.focused();
    const bool captured = focused && snapshot.captured();
    const auto actions =
        collect_action_states(snapshot, bindings, focused, captured);
    const auto mouse_delta = snapshot.relative_mouse_delta();

    double yaw_delta = 0.0;
    double pitch_delta = 0.0;
    if (captured) {
        // In the project Z-up basis positive yaw turns +X toward +Y (left),
        // so physical motion to the right produces a negative yaw delta.
        yaw_delta = -static_cast<double>(mouse_delta.x) *
            mouse_look.degrees_per_pixel_x;
        pitch_delta = static_cast<double>(mouse_delta.y) *
            mouse_look.degrees_per_pixel_y *
            (mouse_look.invert_y ? 1.0 : -1.0);
        if (!std::isfinite(yaw_delta) || !std::isfinite(pitch_delta)) {
            return failure(GameplayInputIntentErrorCode::non_finite_look_delta,
                "Mouse-look arithmetic produced a non-finite delta");
        }
        yaw_delta = std::clamp(yaw_delta,
            -mouse_look.maximum_delta_per_frame,
            mouse_look.maximum_delta_per_frame);
        pitch_delta = std::clamp(pitch_delta,
            -mouse_look.maximum_delta_per_frame,
            mouse_look.maximum_delta_per_frame);
    }

    const auto wheel = snapshot.wheel_delta();
    const bool release_requested = focused &&
        action_is_set(actions.pressed, GameplayInputAction::release_mouse);
    const bool capture_requested = !release_requested && focused && !captured &&
        (snapshot.mouse_button_pressed(input::PhysicalMouseButton::left) ||
            action_is_set(
                actions.pressed, GameplayInputAction::capture_mouse));

    GameplayInputIntentBuildResult result;
    GameplayInputIntent published(snapshot.sequence(),
        opposing_axis(actions.held,
            GameplayInputAction::move_forward,
            GameplayInputAction::move_backward),
        opposing_axis(actions.held,
            GameplayInputAction::move_right,
            GameplayInputAction::move_left),
        opposing_axis(actions.held,
            GameplayInputAction::move_up,
            GameplayInputAction::move_down),
        yaw_delta,
        pitch_delta,
        buttons_from_actions(actions.held),
        buttons_from_actions(actions.pressed),
        buttons_from_actions(actions.released),
        wheel.horizontal,
        wheel.vertical,
        focused,
        captured,
        sample_duration_seconds,
        capture_requested,
        release_requested);
    result.intent.emplace(std::move(published));
    return result;
}

} // namespace hlclient::gameplay_input

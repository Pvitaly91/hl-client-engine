#pragma once

#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/input/input_snapshot.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::gameplay_input {

enum class GameplayInputCompatibilityProfile : std::uint8_t {
    local_keyboard_mouse_intent_v1,
    stock_usercmd_mapping_evidence_pending,
};

enum class GameplayInputEvidenceProfile : std::uint8_t {
    project_owned_input_semantics,
    stock_usercmd_mapping_evidence_pending,
};

enum class GameplayButton : std::uint8_t {
    jump,
    duck,
    use,
    reload,
    speed,
    walk,
    attack_primary,
    attack_secondary,
    scoreboard,
    count,
};

using GameplayButtonMask = std::uint32_t;

[[nodiscard]] constexpr GameplayButtonMask gameplay_button_mask(
    const GameplayButton button) noexcept
{
    const auto index = static_cast<std::uint32_t>(button);
    return index < static_cast<std::uint32_t>(GameplayButton::count)
        ? GameplayButtonMask{1U} << index
        : GameplayButtonMask{0U};
}

[[nodiscard]] constexpr bool gameplay_button_is_set(
    const GameplayButtonMask mask,
    const GameplayButton button) noexcept
{
    const auto bit = gameplay_button_mask(button);
    return bit != 0U && (mask & bit) != 0U;
}

// These are project-owned preview sensitivities. They are not GoldSrc cl_*
// cvars, acceleration, pitch signs, or usercmd sampling semantics.
struct MouseLookConfig {
    double degrees_per_pixel_x{0.10};
    double degrees_per_pixel_y{0.10};
    bool invert_y{false};
    double maximum_delta_per_frame{180.0};
};

struct GameplayInputIntentLimits {
    double maximum_sample_duration_seconds{0.25};
    double maximum_degrees_per_pixel{10.0};
    double maximum_look_delta_degrees_per_axis{360.0};
};

inline constexpr GameplayInputIntentLimits kGameplayInputIntentHardLimits{
    1.0,
    10.0,
    360.0,
};

[[nodiscard]] bool valid_gameplay_input_intent_limits(
    const GameplayInputIntentLimits& limits) noexcept;
[[nodiscard]] bool valid_mouse_look_config(
    const MouseLookConfig& config,
    const GameplayInputIntentLimits& limits = {}) noexcept;

enum class GameplayInputIntentErrorCode : std::uint8_t {
    invalid_limits,
    invalid_mouse_look_config,
    non_finite_sample_duration,
    sample_duration_out_of_range,
    non_finite_look_delta,
    compatibility_evidence_pending,
};

[[nodiscard]] std::string_view to_string(
    GameplayInputIntentErrorCode code) noexcept;

struct GameplayInputIntentError {
    GameplayInputIntentErrorCode code{
        GameplayInputIntentErrorCode::invalid_limits};
    std::string_view message;
};

struct GameplayInputIntentBuildResult;

class GameplayInputIntent final {
public:
    GameplayInputIntent(const GameplayInputIntent&) = default;
    GameplayInputIntent(GameplayInputIntent&&) noexcept = default;
    GameplayInputIntent& operator=(const GameplayInputIntent&) = delete;
    GameplayInputIntent& operator=(GameplayInputIntent&&) noexcept = delete;
    ~GameplayInputIntent() = default;

    [[nodiscard]] std::uint64_t input_sequence() const noexcept;
    [[nodiscard]] float forward_axis() const noexcept;
    [[nodiscard]] float side_axis() const noexcept;
    [[nodiscard]] float vertical_axis() const noexcept;
    [[nodiscard]] double look_delta_yaw_degrees() const noexcept;
    [[nodiscard]] double look_delta_pitch_degrees() const noexcept;
    [[nodiscard]] GameplayButtonMask held_buttons() const noexcept;
    [[nodiscard]] GameplayButtonMask pressed_buttons() const noexcept;
    [[nodiscard]] GameplayButtonMask released_buttons() const noexcept;
    [[nodiscard]] double wheel_delta_x() const noexcept;
    [[nodiscard]] double wheel_delta_y() const noexcept;
    [[nodiscard]] bool focused() const noexcept;
    [[nodiscard]] bool captured() const noexcept;
    [[nodiscard]] double sample_duration_seconds() const noexcept;
    [[nodiscard]] bool capture_mouse_requested() const noexcept;
    [[nodiscard]] bool release_mouse_requested() const noexcept;
    [[nodiscard]] GameplayInputCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] GameplayInputEvidenceProfile evidence_profile() const noexcept;

private:
    friend class GameplayInputIntentBuilder;

    GameplayInputIntent(
        std::uint64_t input_sequence,
        float forward_axis,
        float side_axis,
        float vertical_axis,
        double look_delta_yaw_degrees,
        double look_delta_pitch_degrees,
        GameplayButtonMask held_buttons,
        GameplayButtonMask pressed_buttons,
        GameplayButtonMask released_buttons,
        double wheel_delta_x,
        double wheel_delta_y,
        bool focused,
        bool captured,
        double sample_duration_seconds,
        bool capture_mouse_requested,
        bool release_mouse_requested) noexcept;

    std::uint64_t input_sequence_{0U};
    float forward_axis_{0.0F};
    float side_axis_{0.0F};
    float vertical_axis_{0.0F};
    double look_delta_yaw_degrees_{0.0};
    double look_delta_pitch_degrees_{0.0};
    GameplayButtonMask held_buttons_{0U};
    GameplayButtonMask pressed_buttons_{0U};
    GameplayButtonMask released_buttons_{0U};
    double wheel_delta_x_{0.0};
    double wheel_delta_y_{0.0};
    bool focused_{false};
    bool captured_{false};
    double sample_duration_seconds_{0.0};
    bool capture_mouse_requested_{false};
    bool release_mouse_requested_{false};
};

struct GameplayInputIntentBuildResult {
    std::optional<GameplayInputIntent> intent;
    std::optional<GameplayInputIntentError> error;

    [[nodiscard]] explicit operator bool() const noexcept;
};

class GameplayInputIntentBuilder final {
public:
    // Stateless by design. InputStateTracker owns monotonic sequence policy;
    // this builder retains the exact published sequence without inventing a
    // command number or network sampling state.
    [[nodiscard]] GameplayInputIntentBuildResult build(
        const input::InputSnapshot& snapshot,
        const GameplayInputBindings& bindings,
        const MouseLookConfig& mouse_look,
        double sample_duration_seconds,
        const GameplayInputIntentLimits& limits = {},
        GameplayInputCompatibilityProfile compatibility_profile =
            GameplayInputCompatibilityProfile::
                local_keyboard_mouse_intent_v1) const noexcept;
};

} // namespace hlclient::gameplay_input

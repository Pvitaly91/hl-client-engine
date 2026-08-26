#pragma once

#include <hlclient/input/input_event.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::gameplay_input {

enum class GameplayInputAction : std::uint8_t {
    move_forward,
    move_backward,
    move_left,
    move_right,
    move_up,
    move_down,
    jump,
    duck,
    use,
    reload,
    speed,
    walk,
    attack_primary,
    attack_secondary,
    scoreboard,
    capture_mouse,
    release_mouse,
    count,
};

using GameplayInputActionMask = std::uint32_t;

[[nodiscard]] constexpr GameplayInputActionMask gameplay_input_action_mask(
    const GameplayInputAction action) noexcept
{
    const auto index = static_cast<std::uint32_t>(action);
    return index < static_cast<std::uint32_t>(GameplayInputAction::count)
        ? GameplayInputActionMask{1U} << index
        : GameplayInputActionMask{0U};
}

[[nodiscard]] constexpr bool gameplay_input_action_is_set(
    const GameplayInputActionMask mask,
    const GameplayInputAction action) noexcept
{
    const auto bit = gameplay_input_action_mask(action);
    return bit != 0U && (mask & bit) != 0U;
}

enum class InputBindingKind : std::uint8_t {
    key,
    mouse_button,
};

class InputBinding final {
public:
    [[nodiscard]] static InputBinding key(
        GameplayInputAction action,
        input::PhysicalKey physical_key) noexcept;
    [[nodiscard]] static InputBinding mouse_button(
        GameplayInputAction action,
        input::PhysicalMouseButton physical_button) noexcept;

    [[nodiscard]] GameplayInputAction action() const noexcept;
    [[nodiscard]] InputBindingKind kind() const noexcept;
    [[nodiscard]] const std::optional<input::PhysicalKey>& physical_key()
        const noexcept;
    [[nodiscard]] const std::optional<input::PhysicalMouseButton>&
    physical_mouse_button() const noexcept;

    [[nodiscard]] friend bool operator==(
        const InputBinding&,
        const InputBinding&) noexcept = default;

private:
    InputBinding(
        GameplayInputAction action,
        InputBindingKind kind,
        std::optional<input::PhysicalKey> physical_key,
        std::optional<input::PhysicalMouseButton> physical_button) noexcept;

    GameplayInputAction action_{GameplayInputAction::move_forward};
    InputBindingKind kind_{InputBindingKind::key};
    std::optional<input::PhysicalKey> physical_key_;
    std::optional<input::PhysicalMouseButton> physical_mouse_button_;
};

enum class GameplayInputBindingProfile : std::uint8_t {
    project_default_v1,
    project_custom_v1,
};

struct GameplayInputBindingLimits {
    std::size_t maximum_actions{
        static_cast<std::size_t>(GameplayInputAction::count)};
    std::size_t maximum_bindings_per_action{4U};
    std::size_t maximum_total_bindings{64U};
};

inline constexpr GameplayInputBindingLimits kGameplayInputBindingHardLimits{
    static_cast<std::size_t>(GameplayInputAction::count),
    16U,
    256U,
};

[[nodiscard]] bool valid_gameplay_input_binding_limits(
    const GameplayInputBindingLimits& limits) noexcept;

enum class GameplayInputBindingErrorCode : std::uint8_t {
    invalid_limits,
    invalid_profile,
    invalid_action,
    invalid_physical_input,
    duplicate_binding,
    ambiguous_physical_input,
    action_limit_exceeded,
    bindings_per_action_limit_exceeded,
    total_binding_limit_exceeded,
    unable_to_retain_bindings,
};

[[nodiscard]] std::string_view to_string(
    GameplayInputBindingErrorCode code) noexcept;

struct GameplayInputBindingError {
    GameplayInputBindingErrorCode code{
        GameplayInputBindingErrorCode::invalid_limits};
    std::optional<std::size_t> binding_index;
    std::string_view message;
};

struct GameplayInputBindingsBuildResult;

class GameplayInputBindings final {
public:
    GameplayInputBindings(const GameplayInputBindings&) = default;
    GameplayInputBindings(GameplayInputBindings&&) noexcept = default;
    GameplayInputBindings& operator=(const GameplayInputBindings&) = delete;
    GameplayInputBindings& operator=(GameplayInputBindings&&) noexcept = delete;
    ~GameplayInputBindings() = default;

    [[nodiscard]] static GameplayInputBindingsBuildResult project_default_v1(
        const GameplayInputBindingLimits& limits = {}) noexcept;

    [[nodiscard]] std::span<const InputBinding> bindings() const noexcept;
    [[nodiscard]] GameplayInputActionMask actions_for_key(
        input::PhysicalKey key) const noexcept;
    [[nodiscard]] GameplayInputActionMask actions_for_mouse_button(
        input::PhysicalMouseButton button) const noexcept;
    [[nodiscard]] std::size_t binding_count(
        GameplayInputAction action) const noexcept;
    [[nodiscard]] bool allows_shared_physical_inputs() const noexcept;
    [[nodiscard]] GameplayInputBindingProfile profile() const noexcept;
    [[nodiscard]] const GameplayInputBindingLimits& limits() const noexcept;

private:
    friend class GameplayInputBindingsBuilder;

    static constexpr std::size_t kKeyCount =
        static_cast<std::size_t>(input::PhysicalKey::count);
    static constexpr std::size_t kMouseButtonCount =
        static_cast<std::size_t>(input::PhysicalMouseButton::count);
    static constexpr std::size_t kActionCount =
        static_cast<std::size_t>(GameplayInputAction::count);

    GameplayInputBindings(
        std::vector<InputBinding> bindings,
        std::array<GameplayInputActionMask, kKeyCount> key_actions,
        std::array<GameplayInputActionMask, kMouseButtonCount>
            mouse_button_actions,
        std::array<std::size_t, kActionCount> binding_counts,
        bool allows_shared_physical_inputs,
        GameplayInputBindingProfile profile,
        GameplayInputBindingLimits limits) noexcept;

    std::vector<InputBinding> bindings_;
    std::array<GameplayInputActionMask, kKeyCount> key_actions_{};
    std::array<GameplayInputActionMask, kMouseButtonCount>
        mouse_button_actions_{};
    std::array<std::size_t, kActionCount> binding_counts_{};
    bool allows_shared_physical_inputs_{false};
    GameplayInputBindingProfile profile_{
        GameplayInputBindingProfile::project_custom_v1};
    GameplayInputBindingLimits limits_{};
};

struct GameplayInputBindingsBuildResult {
    std::optional<GameplayInputBindings> bindings;
    std::optional<GameplayInputBindingError> error;

    [[nodiscard]] explicit operator bool() const noexcept;
};

class GameplayInputBindingsBuilder final {
public:
    [[nodiscard]] GameplayInputBindingsBuildResult build(
        std::span<const InputBinding> bindings,
        const GameplayInputBindingLimits& limits = {},
        bool allow_shared_physical_inputs = false,
        GameplayInputBindingProfile profile =
            GameplayInputBindingProfile::project_custom_v1) const noexcept;
};

} // namespace hlclient::gameplay_input

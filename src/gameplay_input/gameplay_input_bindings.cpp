#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace hlclient::gameplay_input {

namespace {

[[nodiscard]] constexpr std::size_t action_index(
    const GameplayInputAction action) noexcept
{
    return static_cast<std::size_t>(action);
}

[[nodiscard]] constexpr std::size_t key_index(
    const input::PhysicalKey key) noexcept
{
    return static_cast<std::size_t>(key);
}

[[nodiscard]] constexpr std::size_t mouse_button_index(
    const input::PhysicalMouseButton button) noexcept
{
    return static_cast<std::size_t>(button);
}

[[nodiscard]] GameplayInputBindingsBuildResult failure(
    const GameplayInputBindingErrorCode code,
    const std::string_view message,
    const std::optional<std::size_t> binding_index = std::nullopt) noexcept
{
    GameplayInputBindingsBuildResult result;
    result.error = GameplayInputBindingError{
        code, binding_index, message};
    return result;
}

[[nodiscard]] std::array<InputBinding, 13U> project_default_bindings() noexcept
{
    return {
        InputBinding::key(GameplayInputAction::move_forward,
            input::PhysicalKey::w),
        InputBinding::key(GameplayInputAction::move_backward,
            input::PhysicalKey::s),
        InputBinding::key(GameplayInputAction::move_left,
            input::PhysicalKey::a),
        InputBinding::key(GameplayInputAction::move_right,
            input::PhysicalKey::d),
        InputBinding::key(GameplayInputAction::jump,
            input::PhysicalKey::space),
        InputBinding::key(GameplayInputAction::duck,
            input::PhysicalKey::left_control),
        InputBinding::key(GameplayInputAction::speed,
            input::PhysicalKey::left_shift),
        InputBinding::key(GameplayInputAction::use,
            input::PhysicalKey::e),
        InputBinding::key(GameplayInputAction::reload,
            input::PhysicalKey::r),
        InputBinding::mouse_button(GameplayInputAction::attack_primary,
            input::PhysicalMouseButton::left),
        InputBinding::mouse_button(GameplayInputAction::attack_secondary,
            input::PhysicalMouseButton::right),
        InputBinding::key(GameplayInputAction::scoreboard,
            input::PhysicalKey::tab),
        InputBinding::key(GameplayInputAction::release_mouse,
            input::PhysicalKey::escape),
    };
}

} // namespace

InputBinding InputBinding::key(
    const GameplayInputAction action,
    const input::PhysicalKey physical_key) noexcept
{
    return {action, InputBindingKind::key, physical_key, std::nullopt};
}

InputBinding InputBinding::mouse_button(
    const GameplayInputAction action,
    const input::PhysicalMouseButton physical_button) noexcept
{
    return {action,
        InputBindingKind::mouse_button,
        std::nullopt,
        physical_button};
}

InputBinding::InputBinding(
    const GameplayInputAction action,
    const InputBindingKind kind,
    const std::optional<input::PhysicalKey> physical_key,
    const std::optional<input::PhysicalMouseButton> physical_button) noexcept
    : action_(action),
      kind_(kind),
      physical_key_(physical_key),
      physical_mouse_button_(physical_button)
{
}

GameplayInputAction InputBinding::action() const noexcept
{
    return action_;
}

InputBindingKind InputBinding::kind() const noexcept
{
    return kind_;
}

const std::optional<input::PhysicalKey>& InputBinding::physical_key()
    const noexcept
{
    return physical_key_;
}

const std::optional<input::PhysicalMouseButton>&
InputBinding::physical_mouse_button() const noexcept
{
    return physical_mouse_button_;
}

bool valid_gameplay_input_binding_limits(
    const GameplayInputBindingLimits& limits) noexcept
{
    return limits.maximum_actions > 0U &&
        limits.maximum_actions <= kGameplayInputBindingHardLimits.maximum_actions &&
        limits.maximum_bindings_per_action > 0U &&
        limits.maximum_bindings_per_action <=
            kGameplayInputBindingHardLimits.maximum_bindings_per_action &&
        limits.maximum_total_bindings > 0U &&
        limits.maximum_total_bindings <=
            kGameplayInputBindingHardLimits.maximum_total_bindings &&
        limits.maximum_actions <= limits.maximum_total_bindings &&
        limits.maximum_bindings_per_action <= limits.maximum_total_bindings;
}

std::string_view to_string(const GameplayInputBindingErrorCode code) noexcept
{
    switch (code) {
    case GameplayInputBindingErrorCode::invalid_limits:
        return "invalid_limits";
    case GameplayInputBindingErrorCode::invalid_profile:
        return "invalid_profile";
    case GameplayInputBindingErrorCode::invalid_action:
        return "invalid_action";
    case GameplayInputBindingErrorCode::invalid_physical_input:
        return "invalid_physical_input";
    case GameplayInputBindingErrorCode::duplicate_binding:
        return "duplicate_binding";
    case GameplayInputBindingErrorCode::ambiguous_physical_input:
        return "ambiguous_physical_input";
    case GameplayInputBindingErrorCode::action_limit_exceeded:
        return "action_limit_exceeded";
    case GameplayInputBindingErrorCode::bindings_per_action_limit_exceeded:
        return "bindings_per_action_limit_exceeded";
    case GameplayInputBindingErrorCode::total_binding_limit_exceeded:
        return "total_binding_limit_exceeded";
    case GameplayInputBindingErrorCode::unable_to_retain_bindings:
        return "unable_to_retain_bindings";
    }
    return "unknown";
}

GameplayInputBindingsBuildResult::operator bool() const noexcept
{
    return bindings.has_value();
}

GameplayInputBindings::GameplayInputBindings(
    std::vector<InputBinding> bindings,
    std::array<GameplayInputActionMask, kKeyCount> key_actions,
    std::array<GameplayInputActionMask, kMouseButtonCount>
        mouse_button_actions,
    std::array<std::size_t, kActionCount> binding_counts,
    const bool allows_shared_physical_inputs,
    const GameplayInputBindingProfile profile,
    const GameplayInputBindingLimits limits) noexcept
    : bindings_(std::move(bindings)),
      key_actions_(key_actions),
      mouse_button_actions_(mouse_button_actions),
      binding_counts_(binding_counts),
      allows_shared_physical_inputs_(allows_shared_physical_inputs),
      profile_(profile),
      limits_(limits)
{
}

GameplayInputBindingsBuildResult GameplayInputBindings::project_default_v1(
    const GameplayInputBindingLimits& limits) noexcept
{
    const auto defaults = project_default_bindings();
    return GameplayInputBindingsBuilder{}.build(defaults,
        limits,
        false,
        GameplayInputBindingProfile::project_default_v1);
}

std::span<const InputBinding> GameplayInputBindings::bindings() const noexcept
{
    return bindings_;
}

GameplayInputActionMask GameplayInputBindings::actions_for_key(
    const input::PhysicalKey key) const noexcept
{
    const auto index = key_index(key);
    return index < key_actions_.size() ? key_actions_[index] : 0U;
}

GameplayInputActionMask GameplayInputBindings::actions_for_mouse_button(
    const input::PhysicalMouseButton button) const noexcept
{
    const auto index = mouse_button_index(button);
    return index < mouse_button_actions_.size()
        ? mouse_button_actions_[index]
        : 0U;
}

std::size_t GameplayInputBindings::binding_count(
    const GameplayInputAction action) const noexcept
{
    const auto index = action_index(action);
    return index < binding_counts_.size() ? binding_counts_[index] : 0U;
}

bool GameplayInputBindings::allows_shared_physical_inputs() const noexcept
{
    return allows_shared_physical_inputs_;
}

GameplayInputBindingProfile GameplayInputBindings::profile() const noexcept
{
    return profile_;
}

const GameplayInputBindingLimits& GameplayInputBindings::limits() const noexcept
{
    return limits_;
}

GameplayInputBindingsBuildResult GameplayInputBindingsBuilder::build(
    const std::span<const InputBinding> bindings,
    const GameplayInputBindingLimits& limits,
    const bool allow_shared_physical_inputs,
    const GameplayInputBindingProfile profile) const noexcept
{
    if (!valid_gameplay_input_binding_limits(limits)) {
        return failure(GameplayInputBindingErrorCode::invalid_limits,
            "Gameplay input binding limits are invalid");
    }
    if (profile != GameplayInputBindingProfile::project_default_v1 &&
        profile != GameplayInputBindingProfile::project_custom_v1) {
        return failure(GameplayInputBindingErrorCode::invalid_profile,
            "Gameplay input binding profile is invalid");
    }
    if (profile == GameplayInputBindingProfile::project_default_v1) {
        const auto defaults = project_default_bindings();
        if (allow_shared_physical_inputs ||
            bindings.size() != defaults.size() ||
            !std::equal(bindings.begin(), bindings.end(), defaults.begin())) {
            return failure(GameplayInputBindingErrorCode::invalid_profile,
                "The project-default profile requires its exact canonical binding set");
        }
    }
    if (bindings.size() > limits.maximum_total_bindings) {
        return failure(
            GameplayInputBindingErrorCode::total_binding_limit_exceeded,
            "Gameplay input binding count exceeds the configured limit");
    }

    constexpr auto key_count =
        static_cast<std::size_t>(input::PhysicalKey::count);
    constexpr auto mouse_button_count =
        static_cast<std::size_t>(input::PhysicalMouseButton::count);
    constexpr auto action_count =
        static_cast<std::size_t>(GameplayInputAction::count);
    std::array<GameplayInputActionMask, key_count> key_actions{};
    std::array<GameplayInputActionMask, mouse_button_count>
        mouse_button_actions{};
    std::array<std::size_t, action_count> binding_counts{};
    std::size_t distinct_actions = 0U;

    for (std::size_t index = 0U; index < bindings.size(); ++index) {
        const auto& binding = bindings[index];
        const auto current_action_index = action_index(binding.action());
        if (current_action_index >= action_count) {
            return failure(GameplayInputBindingErrorCode::invalid_action,
                "Gameplay input binding contains an invalid action",
                index);
        }
        if (binding_counts[current_action_index] == 0U) {
            if (distinct_actions == limits.maximum_actions) {
                return failure(
                    GameplayInputBindingErrorCode::action_limit_exceeded,
                    "Distinct gameplay action count exceeds the configured limit",
                    index);
            }
            ++distinct_actions;
        }
        if (binding_counts[current_action_index] ==
            limits.maximum_bindings_per_action) {
            return failure(
                GameplayInputBindingErrorCode::
                    bindings_per_action_limit_exceeded,
                "Gameplay action binding count exceeds the configured limit",
                index);
        }

        const auto action_bit = gameplay_input_action_mask(binding.action());
        GameplayInputActionMask* physical_action_mask = nullptr;
        if (binding.kind() == InputBindingKind::key &&
            binding.physical_key() && !binding.physical_mouse_button()) {
            const auto physical_index = key_index(*binding.physical_key());
            if (physical_index >= key_actions.size()) {
                return failure(
                    GameplayInputBindingErrorCode::invalid_physical_input,
                    "Gameplay key binding contains an invalid physical key",
                    index);
            }
            physical_action_mask = &key_actions[physical_index];
        } else if (binding.kind() == InputBindingKind::mouse_button &&
                   binding.physical_mouse_button() &&
                   !binding.physical_key()) {
            const auto physical_index =
                mouse_button_index(*binding.physical_mouse_button());
            if (physical_index >= mouse_button_actions.size()) {
                return failure(
                    GameplayInputBindingErrorCode::invalid_physical_input,
                    "Gameplay mouse binding contains an invalid physical button",
                    index);
            }
            physical_action_mask = &mouse_button_actions[physical_index];
        } else {
            return failure(
                GameplayInputBindingErrorCode::invalid_physical_input,
                "Gameplay binding physical-input discriminator is invalid",
                index);
        }

        if ((*physical_action_mask & action_bit) != 0U) {
            return failure(GameplayInputBindingErrorCode::duplicate_binding,
                "Exact gameplay input binding is duplicated",
                index);
        }
        if (*physical_action_mask != 0U && !allow_shared_physical_inputs) {
            return failure(
                GameplayInputBindingErrorCode::ambiguous_physical_input,
                "Physical input is already assigned to another action",
                index);
        }
        *physical_action_mask |= action_bit;
        ++binding_counts[current_action_index];
    }

    try {
        std::vector<InputBinding> owned(bindings.begin(), bindings.end());
        GameplayInputBindingsBuildResult result;
        GameplayInputBindings published(std::move(owned),
            key_actions,
            mouse_button_actions,
            binding_counts,
            allow_shared_physical_inputs,
            profile,
            limits);
        result.bindings.emplace(std::move(published));
        return result;
    } catch (const std::exception&) {
        return failure(GameplayInputBindingErrorCode::unable_to_retain_bindings,
            "Unable to retain immutable gameplay input bindings");
    }
}

} // namespace hlclient::gameplay_input

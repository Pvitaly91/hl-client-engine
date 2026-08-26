#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {

namespace gameplay = hlclient::gameplay_input;
namespace input = hlclient::input;

static_assert(!std::is_default_constructible_v<gameplay::InputBinding>);
static_assert(!std::is_constructible_v<gameplay::InputBinding,
    gameplay::GameplayInputAction,
    std::string_view>);
static_assert(!std::is_copy_assignable_v<gameplay::GameplayInputBindings>);
static_assert(!std::is_move_assignable_v<gameplay::GameplayInputBindings>);

TEST_CASE("Project gameplay bindings publish exact keyboard and mouse defaults",
          "[gameplay-input][bindings][defaults]")
{
    const auto built = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(built);
    const auto& bindings = *built.bindings;

    CHECK(bindings.profile() ==
          gameplay::GameplayInputBindingProfile::project_default_v1);
    CHECK_FALSE(bindings.allows_shared_physical_inputs());
    CHECK(bindings.bindings().size() == 13U);
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::w),
        gameplay::GameplayInputAction::move_forward));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::s),
        gameplay::GameplayInputAction::move_backward));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::a),
        gameplay::GameplayInputAction::move_left));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::d),
        gameplay::GameplayInputAction::move_right));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::space),
        gameplay::GameplayInputAction::jump));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::left_control),
        gameplay::GameplayInputAction::duck));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::left_shift),
        gameplay::GameplayInputAction::speed));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::e),
        gameplay::GameplayInputAction::use));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::r),
        gameplay::GameplayInputAction::reload));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::tab),
        gameplay::GameplayInputAction::scoreboard));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_key(input::PhysicalKey::escape),
        gameplay::GameplayInputAction::release_mouse));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_mouse_button(input::PhysicalMouseButton::left),
        gameplay::GameplayInputAction::attack_primary));
    CHECK(gameplay::gameplay_input_action_is_set(
        bindings.actions_for_mouse_button(input::PhysicalMouseButton::right),
        gameplay::GameplayInputAction::attack_secondary));

    CHECK(bindings.actions_for_key(input::PhysicalKey::up) == 0U);
    CHECK(bindings.actions_for_mouse_button(
              input::PhysicalMouseButton::middle) == 0U);
    CHECK(bindings.binding_count(gameplay::GameplayInputAction::move_up) == 0U);
    CHECK(bindings.binding_count(gameplay::GameplayInputAction::walk) == 0U);
    CHECK(bindings.binding_count(gameplay::GameplayInputAction::capture_mouse) ==
          0U);
}

TEST_CASE("Gameplay binding builder rejects duplicates and requires explicit sharing",
          "[gameplay-input][bindings][duplicates]")
{
    const std::array duplicate{
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_forward,
            input::PhysicalKey::w),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_forward,
            input::PhysicalKey::w),
    };
    const auto duplicate_result =
        gameplay::GameplayInputBindingsBuilder{}.build(duplicate);
    REQUIRE_FALSE(duplicate_result);
    REQUIRE(duplicate_result.error);
    CHECK(duplicate_result.error->code ==
          gameplay::GameplayInputBindingErrorCode::duplicate_binding);
    CHECK(duplicate_result.error->binding_index == 1U);

    const std::array shared{
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_forward,
            input::PhysicalKey::w),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::jump,
            input::PhysicalKey::w),
    };
    const auto rejected =
        gameplay::GameplayInputBindingsBuilder{}.build(shared);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          gameplay::GameplayInputBindingErrorCode::ambiguous_physical_input);

    const auto allowed =
        gameplay::GameplayInputBindingsBuilder{}.build(shared, {}, true);
    REQUIRE(allowed);
    CHECK(allowed.bindings->allows_shared_physical_inputs());
    const auto w_actions =
        allowed.bindings->actions_for_key(input::PhysicalKey::w);
    CHECK(gameplay::gameplay_input_action_is_set(
        w_actions, gameplay::GameplayInputAction::move_forward));
    CHECK(gameplay::gameplay_input_action_is_set(
        w_actions, gameplay::GameplayInputAction::jump));

    const std::array invalid_key{
        gameplay::InputBinding::key(gameplay::GameplayInputAction::jump,
            static_cast<input::PhysicalKey>(
                std::numeric_limits<std::uint8_t>::max())),
    };
    const auto invalid =
        gameplay::GameplayInputBindingsBuilder{}.build(invalid_key);
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error);
    CHECK(invalid.error->code ==
          gameplay::GameplayInputBindingErrorCode::invalid_physical_input);
}

TEST_CASE("Named project-default bindings cannot be spoofed by custom mappings",
          "[gameplay-input][bindings][defaults][profile]")
{
    const std::array spoofed{
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::jump,
            input::PhysicalKey::f1),
    };
    const auto spoofed_result = gameplay::GameplayInputBindingsBuilder{}.build(
        spoofed,
        {},
        false,
        gameplay::GameplayInputBindingProfile::project_default_v1);
    REQUIRE_FALSE(spoofed_result);
    REQUIRE(spoofed_result.error);
    CHECK(spoofed_result.error->code ==
        gameplay::GameplayInputBindingErrorCode::invalid_profile);

    const auto empty_result = gameplay::GameplayInputBindingsBuilder{}.build(
        std::span<const gameplay::InputBinding>{},
        {},
        true,
        gameplay::GameplayInputBindingProfile::project_default_v1);
    REQUIRE_FALSE(empty_result);
    REQUIRE(empty_result.error);
    CHECK(empty_result.error->code ==
        gameplay::GameplayInputBindingErrorCode::invalid_profile);
}

TEST_CASE("Gameplay binding limits accept exact values and reject limit plus one",
          "[gameplay-input][bindings][limits]")
{
    gameplay::GameplayInputBindingLimits limits;
    limits.maximum_actions = 2U;
    limits.maximum_bindings_per_action = 2U;
    limits.maximum_total_bindings = 4U;
    const std::array exact{
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_forward,
            input::PhysicalKey::w),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::move_forward,
            input::PhysicalKey::up),
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::jump,
            input::PhysicalKey::space),
        gameplay::InputBinding::mouse_button(
            gameplay::GameplayInputAction::jump,
            input::PhysicalMouseButton::middle),
    };
    REQUIRE(gameplay::GameplayInputBindingsBuilder{}.build(exact, limits));

    const std::array over_total{
        exact[0U],
        exact[1U],
        exact[2U],
        exact[3U],
        gameplay::InputBinding::key(
            gameplay::GameplayInputAction::jump,
            input::PhysicalKey::f1),
    };
    const auto total = gameplay::GameplayInputBindingsBuilder{}.build(
        over_total, limits);
    REQUIRE_FALSE(total);
    CHECK(total.error->code ==
          gameplay::GameplayInputBindingErrorCode::
              total_binding_limit_exceeded);

    auto per_action_limits = limits;
    per_action_limits.maximum_bindings_per_action = 1U;
    const auto per_action = gameplay::GameplayInputBindingsBuilder{}.build(
        std::span<const gameplay::InputBinding>{exact}.first(2U),
        per_action_limits);
    REQUIRE_FALSE(per_action);
    CHECK(per_action.error->code ==
          gameplay::GameplayInputBindingErrorCode::
              bindings_per_action_limit_exceeded);

    auto action_limits = limits;
    action_limits.maximum_actions = 1U;
    const std::array two_actions{exact[0U], exact[2U]};
    const auto actions = gameplay::GameplayInputBindingsBuilder{}.build(
        two_actions, action_limits);
    REQUIRE_FALSE(actions);
    CHECK(actions.error->code ==
          gameplay::GameplayInputBindingErrorCode::action_limit_exceeded);

    auto invalid_limits = gameplay::GameplayInputBindingLimits{};
    invalid_limits.maximum_total_bindings = 0U;
    const auto invalid = gameplay::GameplayInputBindingsBuilder{}.build(
        std::span<const gameplay::InputBinding>{}, invalid_limits);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code ==
          gameplay::GameplayInputBindingErrorCode::invalid_limits);

    const auto invalid_profile =
        gameplay::GameplayInputBindingsBuilder{}.build(
            std::span<const gameplay::InputBinding>{},
            {},
            false,
            static_cast<gameplay::GameplayInputBindingProfile>(0xffU));
    REQUIRE_FALSE(invalid_profile);
    REQUIRE(invalid_profile.error);
    CHECK(invalid_profile.error->code ==
        gameplay::GameplayInputBindingErrorCode::invalid_profile);

    CHECK(gameplay::to_string(
              gameplay::GameplayInputBindingErrorCode::duplicate_binding) ==
          "duplicate_binding");
}

} // namespace

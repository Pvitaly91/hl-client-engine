#include <hlclient/games/halflife/movement_policy.hpp>
#include <hlclient/goldsrc/reference_client_move.hpp>

#include <stdexcept>
#include <utility>

namespace hlclient::games::halflife {

game_api::GameMovementPolicy make_movement_policy()
{
    game_api::GameMovementPolicy policy;
    auto bindings = gameplay_input::GameplayInputBindings::project_default_v1();
    if (!bindings || !bindings.bindings)
        throw std::invalid_argument("Half-Life input bindings could not be built");
    policy.bindings =
        std::make_shared<const gameplay_input::GameplayInputBindings>(
            std::move(*bindings.bindings));
    policy.mouse_look = gameplay_input::MouseLookConfig{0.10, 0.10, false, 180.0};
    policy.live_buttons =
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::move_forward) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::move_backward) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::move_left) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::move_right) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::jump) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::duck) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::attack_primary) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::reload) |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::use);
    policy.capture_life_scoped_buttons =
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::use);
    policy.held_buttons = policy.live_buttons |
        gameplay_input::gameplay_button_mask(gameplay_input::GameplayButton::speed);
    policy.inventory_group_keys = {input::PhysicalKey::digit_1, input::PhysicalKey::digit_2,
        input::PhysicalKey::digit_3, input::PhysicalKey::digit_4, input::PhysicalKey::digit_5};
    policy.inventory_repeat_milliseconds = 100U;
    policy.movement_speeds = {400.0F, 400.0F, 400.0F};
    policy.speed_key_multiplier = 0.3F;
    policy.button_policy =
        goldsrc::GoldSrcReferenceButtonPolicy::jump_duck_primary_reload_use;
    // The unchanged kernel has explicit bounded/reference algorithms, rather
    // than a universal GoldSrc-mod simulation hook. Selection belongs here.
    policy.movement_config = goldsrc::movement::GoldSrcLocalMovementConfig{};
    policy.movement_config.ground_button_speed_limit_mask = goldsrc::kReferenceGoldSrcButtonUse;
    policy.movement_config.ground_button_speed_limit_multiplier = 1.0F / 3.0F;
    policy.environment_profile = goldsrc::movement::
        GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1;
    policy.derived_vertical_support = true;
    policy.reference_ladder = goldsrc::movement::ReferenceLadderMovementPolicy{
        200.0F, 0.333F, 270.0F};
    if (!game_api::valid_game_movement_policy(policy))
        throw std::invalid_argument("Half-Life movement policy is invalid");
    return policy;
}

} // namespace hlclient::games::halflife

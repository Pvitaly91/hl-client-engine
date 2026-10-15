#include <hlclient/game_api/movement_policy.hpp>

#include <cmath>
#include <algorithm>

namespace hlclient::game_api {

bool valid_game_movement_policy(const GameMovementPolicy& policy) noexcept
{
    const auto valid_speed = [](const float value) {
        return std::isfinite(value) && value > 0.0F && value <= 10'000.0F;
    };
    const bool valid_buttons =
        policy.button_policy == goldsrc::GoldSrcReferenceButtonPolicy::none ||
        policy.button_policy == goldsrc::GoldSrcReferenceButtonPolicy::jump_duck ||
        policy.button_policy ==
            goldsrc::GoldSrcReferenceButtonPolicy::jump_duck_primary_reload ||
        policy.button_policy == goldsrc::GoldSrcReferenceButtonPolicy::jump_duck_primary_reload_use ||
        policy.button_policy == goldsrc::GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use;
    constexpr auto all_actions =
        (gameplay_input::GameplayButtonMask{1U} <<
            static_cast<unsigned>(gameplay_input::GameplayButton::count)) - 1U;
    bool valid_inventory_keys = true;
    for (const auto key : policy.inventory_group_keys)
        valid_inventory_keys = valid_inventory_keys &&
            static_cast<unsigned>(key) < static_cast<unsigned>(input::PhysicalKey::count);
    valid_inventory_keys = valid_inventory_keys && (!policy.inventory_last_key ||
        static_cast<unsigned>(*policy.inventory_last_key) < static_cast<unsigned>(input::PhysicalKey::count));
    return policy.bindings &&
        policy.crouch_jump_physics_key.size() <= 31U &&
        std::ranges::all_of(policy.crouch_jump_physics_key, [](const unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_';
        }) &&
        (policy.crouch_jump_physics_key.empty() == !policy.movement_config.crouch_jump) &&
        (!policy.server_box_solid_code || (*policy.server_box_solid_code > 0U &&
            *policy.server_box_solid_code <= 65535U)) &&
        (!policy.server_player_solid_code || (*policy.server_player_solid_code > 0U &&
            *policy.server_player_solid_code <= 65535U)) &&
        goldsrc::movement::valid_server_point_box_exclusion_rules(policy.nonblocking_point_boxes) &&
        (policy.nonblocking_point_boxes.empty() || policy.server_box_solid_code.has_value()) &&
        (!policy.exclude_receiving_client_owned_boxes || policy.server_box_solid_code.has_value()) &&
        ((policy.live_buttons | policy.held_buttons) & ~all_actions) == 0U &&
        (policy.live_buttons & ~policy.held_buttons) == 0U &&
        (policy.capture_life_scoped_buttons & ~policy.live_buttons) == 0U &&
        valid_inventory_keys && policy.inventory_repeat_milliseconds > 0U &&
        policy.inventory_repeat_milliseconds <= 1'000U &&
        gameplay_input::valid_mouse_look_config(policy.mouse_look) &&
        valid_speed(policy.movement_speeds.forward_speed) &&
        valid_speed(policy.movement_speeds.backward_speed) &&
        valid_speed(policy.movement_speeds.side_speed) &&
        std::isfinite(policy.speed_key_multiplier) &&
        policy.speed_key_multiplier > 0.0F &&
        policy.speed_key_multiplier <= 1.0F && valid_buttons &&
        goldsrc::movement::valid_goldsrc_local_movement_config(
            policy.movement_config) &&
        (!policy.reference_ladder ||
         (std::isfinite(policy.reference_ladder->maximum_climb_speed) &&
          policy.reference_ladder->maximum_climb_speed > 0.0F &&
          policy.reference_ladder->maximum_climb_speed <= 1'000.0F &&
          std::isfinite(policy.reference_ladder->duck_speed_multiplier) &&
          policy.reference_ladder->duck_speed_multiplier > 0.0F &&
          policy.reference_ladder->duck_speed_multiplier <= 1.0F &&
          std::isfinite(policy.reference_ladder->jump_away_speed) &&
          policy.reference_ladder->jump_away_speed > 0.0F &&
          policy.reference_ladder->jump_away_speed <= 1'000.0F)) &&
        policy.environment_profile == goldsrc::movement::
            GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1;
}

} // namespace hlclient::game_api

#pragma once

#include <cstdint>
#include <optional>

namespace hlclient::game_api {
// Display selection is separate from a validated request and active weapon.
// Physical keys/buttons are translated by the application before this seam.
struct HudMenuInput final {
    std::optional<std::uint8_t> group_digit;
    int cycle_direction{};
    bool primary_pressed{}, primary_held{}, primary_released{}, cancel{};
    bool last_weapon_pressed{};
    double seconds{};
};
struct HudMenuResult final {
    std::optional<std::uint8_t> selection;
    bool consume_primary{}, cancel_consumed{}, menu_open{};
};
} // namespace hlclient::game_api

#pragma once

#include <hlclient/game_api/movement_policy.hpp>

namespace hlclient::games::halflife {

// Selects the existing bounded HL1 dry-walk/input rules. This does not claim
// that other games can implement their movement by changing only parameters.
[[nodiscard]] game_api::GameMovementPolicy make_movement_policy();

} // namespace hlclient::games::halflife

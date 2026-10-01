#pragma once

#include <cstdint>
#include <optional>

namespace hlclient::goldsrc {

struct GoldSrcUserCmdMovementSpeedConfig {
    float forward_speed{400.0F};
    float backward_speed{400.0F};
    float side_speed{400.0F};
};

// A missing client limit is unknown. Valve's explicit zero means no client
// clamp; a positive value limits the complete movement vector.
struct GoldSrcReferenceMovementPolicy final {
    float speed_key_multiplier{1.0F};
    std::optional<float> client_maxspeed;
};

enum class GoldSrcReferenceButtonPolicy : std::uint8_t {
    none,
    jump_duck,
    jump_duck_primary_reload,
    jump_duck_primary_reload_use,
};

} // namespace hlclient::goldsrc

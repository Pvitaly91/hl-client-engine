#pragma once
#include <hlclient/game_api/game_client_module.hpp>
#include <memory>
namespace hlclient::games::halflife {
[[nodiscard]] std::unique_ptr<game_api::IGameClientModule> make_half_life_client_module(
    bool mute_glock_fire_sound = false);
}

#pragma once
#include <hlclient/game_api/scenario.hpp>
namespace hlclient::games::halflife {
[[nodiscard]] game_api::GameScenarioDirective legacy_fire_reload_command(
    const client::RuntimeClientObservationState*, std::size_t phase, double phase_seconds) noexcept;
}

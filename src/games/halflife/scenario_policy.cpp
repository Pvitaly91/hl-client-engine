#include <hlclient/games/halflife/scenario_policy.hpp>
#include <algorithm>
#include <cmath>

namespace hlclient::games::halflife {
game_api::GameScenarioDirective legacy_fire_reload_command(
    const client::RuntimeClientObservationState* observed, const std::size_t phase_index,
    const double phase_seconds) noexcept {
    game_api::GameScenarioDirective result;
    if (!std::isfinite(phase_seconds) || phase_seconds < 0.0 || phase_seconds > 300.0)
        return result;
    const auto phase_ms = std::llround(phase_seconds * 1000.0);
    const auto active = observed ? observed->weapon_hud.active_weapon_id : std::nullopt;
    const auto named_active = [&](const std::string_view name) {
        if (!observed || !active) return false;
        return std::any_of(observed->weapon_hud.catalogue.begin(),
            observed->weapon_hud.catalogue.end(), [&](const auto& type) {
                return type.id == *active && type.command_name == name;
            });
    };
    const bool glock = named_active("weapon_9mmhandgun");
    const bool crowbar = named_active("weapon_crowbar");
    const bool attack =
        (phase_index == 1U && glock &&
            ((phase_ms >= 200 && phase_ms < 280) ||
             (phase_ms >= 700 && phase_ms < 780) ||
             (phase_ms >= 1200 && phase_ms < 1280))) ||
        (phase_index == 3U && crowbar && phase_ms >= 700 && phase_ms < 780);
    const bool reload = phase_index == 2U && glock && phase_ms >= 200 && phase_ms < 280;
    result.buttons = static_cast<std::uint16_t>((attack ? 1U : 0U) | (reload ? (1U << 13U) : 0U));
    result.forward = phase_index == 0U || phase_index == 4U;
    result.slow_walk = phase_index == 4U;
    return result;
}
}

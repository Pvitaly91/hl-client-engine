#pragma once
#include <hlclient/client/runtime_observation.hpp>
#include <string_view>
namespace hlclient::game_api {
enum class GameScenario : std::uint8_t { weapon_presentation, weapon_fire_reload, damage_respawn };
enum class DamageRespawnPhase : std::uint8_t {
    initial, death, release_press, new_life_movement, glock, crowbar, neutral, complete, blocked
};
[[nodiscard]] constexpr std::string_view to_string(DamageRespawnPhase phase) noexcept {
    switch (phase) {
    case DamageRespawnPhase::initial: return "initial";
    case DamageRespawnPhase::death: return "awaiting_own_death";
    case DamageRespawnPhase::release_press: return "release_then_respawn_press";
    case DamageRespawnPhase::new_life_movement: return "new_life_movement";
    case DamageRespawnPhase::glock: return "new_life_glock_binding";
    case DamageRespawnPhase::crowbar: return "new_life_crowbar_binding";
    case DamageRespawnPhase::neutral: return "neutral_finish";
    case DamageRespawnPhase::complete: return "complete";
    case DamageRespawnPhase::blocked: return "blocked";
    }
    return "unknown";
}
struct DamageRespawnScriptSnapshot final {
    DamageRespawnPhase phase{DamageRespawnPhase::initial};
    std::string_view blocker{"none"};
    bool kill_queued{}, respawn_input_submitted{}, server_alive{}, glock_bound{}, crowbar_bound{};
    std::size_t post_respawn_commands{}, post_respawn_samples{};
    std::uint64_t initial_epoch{}, deaths_before{}, damage_before{};
    std::optional<client::RuntimeObservationSource> last_sample;
};
struct GameScenarioDirective final {
    std::uint16_t buttons{};
    bool forward{}, request_self_kill{}, slow_walk{};
    std::optional<std::uint8_t> select_weapon;
};
}

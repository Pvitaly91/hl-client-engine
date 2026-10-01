#pragma once
#include <hlclient/client/runtime_observation.hpp>

namespace hlclient::games::halflife {
// A pure staged owner transition. Caller publishes only after its entire record
// and world bridge validate. Receiving entity is ServerInfo slot+1, never userid.
[[nodiscard]] client::LocalPlayerLifecycle advance_local_player_lifecycle(
    const client::RuntimeClientObservationState* previous,
    const client::RuntimeClientObservationState& candidate,
    std::optional<std::uint32_t> receiving_entity,
    const client::RuntimeWeaponHudObservation& hud,
    std::span<const client::RuntimeLifeEvent> life_events,
    const client::RuntimeWeaponHudObservation* previous_hud,
    const client::LocalPlayerLifecycle* previous_lifecycle) noexcept;

[[nodiscard]] inline client::LocalPlayerLifecycle advance_local_player_lifecycle(
    const client::RuntimeClientObservationState* previous,
    const client::RuntimeClientObservationState& candidate,
    std::optional<std::uint32_t> receiving_entity) noexcept {
    return advance_local_player_lifecycle(previous, candidate, receiving_entity,
        candidate.weapon_hud, candidate.life_events,
        previous ? &previous->weapon_hud : nullptr,
        previous ? &previous->lifecycle : nullptr);
}
}

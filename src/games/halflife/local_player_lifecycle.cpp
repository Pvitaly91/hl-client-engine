#include <hlclient/games/halflife/local_player_lifecycle.hpp>

namespace hlclient::games::halflife {
using namespace hlclient::client;
LocalPlayerLifecycle advance_local_player_lifecycle(
    const RuntimeClientObservationState* previous,
    const RuntimeClientObservationState& candidate,
    std::optional<std::uint32_t> receiving_entity,
    const RuntimeWeaponHudObservation& hud,
    std::span<const RuntimeLifeEvent> life_events,
    const RuntimeWeaponHudObservation* previous_hud,
    const LocalPlayerLifecycle* previous_lifecycle) noexcept {
    auto out = previous_lifecycle ? *previous_lifecycle : LocalPlayerLifecycle{};
    const auto prior_event_record = out.event_record_high_water;
    for (const auto& event : life_events) {
        if (event.source.record_ordinal <= prior_event_record) continue;
        out.event_record_high_water = event.source.record_ordinal;
        switch (event.kind) {
        case RuntimeLifeEventKind::damage:
            ++out.damage_events;
            out.last_damage_source = event.source;
            ++out.feedback_revision; // also preserve zero-amount type/status notifications
            out.health_before_damage = previous_hud && previous_hud->health
                ? std::optional<double>{*previous_hud->health}
                : previous && previous->receiving_client ? previous->receiving_client->health : std::nullopt;
            out.health_after_damage = hud.health
                ? std::optional<double>{*hud.health}
                : candidate.receiving_client ? candidate.receiving_client->health : std::nullopt;
            out.armor_before_damage = previous_hud ? previous_hud->armor : std::nullopt;
            out.armor_after_damage = hud.armor;
            out.health_before_source = previous_hud && previous_hud->health
                ? previous_hud->health_source
                : previous ? previous->client_metadata.source : std::nullopt;
            out.health_after_source = hud.health
                ? hud.health_source : candidate.client_metadata.source;
            out.armor_before_source = previous_hud ? previous_hud->armor_source : std::nullopt;
            out.armor_after_source = hud.armor_source;
            break;
        case RuntimeLifeEventKind::death_notice:
            if (receiving_entity && event.victim_entity == *receiving_entity &&
                !out.dead() && (!out.boundary_source ||
                    (!event.source.reassembled &&
                     event.source.record_ordinal > out.boundary_source->record_ordinal)))
                out.pending_death = event.source;
            break;
        case RuntimeLifeEventKind::reset_hud:
        case RuntimeLifeEventKind::init_hud:
            break; // presentation reset != a life boundary
        }
    }
    const bool fresh = candidate.client_metadata.freshness ==
        RuntimeObservationFreshness::observed_in_record &&
        candidate.client_metadata.source && candidate.receiving_client &&
        (!out.last_client_source ||
         *out.last_client_source != *candidate.client_metadata.source);
    // A local notice may follow an already committed zero-HP client sample.
    // Corroborate that exact retained sample, not the reliable completion
    // header. Still require a genuinely fresh sample to accept new life.
    if (!fresh && !(out.pending_death && candidate.receiving_client &&
        candidate.receiving_client->health && *candidate.receiving_client->health <= 0.0 &&
        candidate.client_metadata.source)) return out;
    const auto& client = *candidate.receiving_client;
    const auto& source = *candidate.client_metadata.source;
    if (fresh) out.last_client_source = source;
    out.dead_flag = client.dead_flag;
    // Require receiving-client view context. Missing health is not zero and
    // HP zero without a local notice/nonzero valid deadflag is not death.
    const bool context = client.origin.complete() && client.view_offset.z;
    const bool death_flag = client.dead_flag && *client.dead_flag >= 1U &&
        *client.dead_flag <= 4U;
    const bool dead_context = context && (death_flag ||
        (out.pending_death && client.health && *client.health <= 0.0));
    if (dead_context && !out.dead()) {
        out.state = LocalPlayerLifeState::dead;
        ++out.deaths;
        out.boundary_source = source; // exact clientdata, not reliable completion
        out.last_death_source = source;
        out.pre_death_model = previous && previous->receiving_client
            ? previous->receiving_client->viewmodel_index : std::nullopt;
        out.pre_death_weapon = previous_hud ? previous_hud->active_weapon_id : std::nullopt;
        out.pending_death.reset();
    }
    if (out.dead() && client.dead_flag == 3U)
        out.state = LocalPlayerLifeState::awaiting_respawn;
    // Positive HP in a newer non-reassembled clientdata sample establishes
    // new receiving-player life, including SDK builds which leave deadflag 0.
    const bool alive_context = context && client.health && *client.health > 0.0 &&
        (!client.dead_flag || *client.dead_flag == 0U);
    if (fresh && alive_context && !source.reassembled && out.pending_death &&
        (source.record_ordinal > out.pending_death->record_ordinal ||
         (source.record_ordinal == out.pending_death->record_ordinal &&
          source.start_bit_offset >= out.pending_death->end_bit_offset)))
        out.pending_death.reset(); // later alive evidence must not authorize a future unrelated HP zero
    const bool newer = !out.boundary_source ||
        (!source.reassembled && source.record_ordinal > out.boundary_source->record_ordinal &&
         ((source.source_transport_sequence - out.boundary_source->source_transport_sequence) &
             0x7fffffffU) != 0U &&
         ((source.source_transport_sequence - out.boundary_source->source_transport_sequence) &
             0x7fffffffU) < 0x40000000U);
    if (fresh && alive_context && (out.state == LocalPlayerLifeState::unknown ||
                          (out.dead() && newer))) {
        const bool respawn = out.dead();
        out.state = LocalPlayerLifeState::alive;
        ++out.life_epoch;
        if (respawn) ++out.respawns;
        out.boundary_source = source;
        if (respawn) out.pending_death.reset();
        if (respawn) {
            out.post_respawn_model = client.viewmodel_index;
            out.post_respawn_weapon = hud.active_weapon_id;
        }
    }
    if (out.state == LocalPlayerLifeState::alive && out.respawns > 0U) {
        // Model and CurWeapon may arrive after the life boundary; retain only
        // coherent post-death binding, never fabricate a spawn inventory.
        if (client.viewmodel_index && *client.viewmodel_index != 0U &&
            hud.active_source && out.last_death_source &&
            hud.active_source->record_ordinal >
                out.last_death_source->record_ordinal &&
            source.record_ordinal >= hud.active_source->record_ordinal) {
            out.post_respawn_model = client.viewmodel_index;
            out.post_respawn_weapon = hud.active_weapon_id;
        }
    }
    return out;
}
}

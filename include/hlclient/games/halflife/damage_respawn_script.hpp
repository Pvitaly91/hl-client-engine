#pragma once
#include <hlclient/game_api/scenario.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::games::halflife {
using game_api::DamageRespawnPhase;
using game_api::DamageRespawnScriptSnapshot;
using game_api::to_string;
// Narrow scenario policy; does not set HP, deadflag, position, inventory or
// server readiness. Repeated ordinary button pulses wait for authoritative
// alive confirmation; elapsed time alone can NEVER complete respawn.
class DamageRespawnScript final {
public:
    [[nodiscard]] const DamageRespawnScriptSnapshot& snapshot() const noexcept { return state_; }
    [[nodiscard]] game_api::GameScenarioDirective command(
        const client::RuntimeClientObservationState* observed, double seconds) noexcept {
        game_api::GameScenarioDirective out;
        if (!std::isfinite(seconds) || seconds < last_time_) {
            block("invalid_scenario_clock"); return out;
        }
        last_time_ = seconds;
        if (state_.phase == DamageRespawnPhase::complete ||
            state_.phase == DamageRespawnPhase::blocked) return out;
        if (seconds - phase_start_ > deadline()) {
            block(to_string(state_.phase)); return out;
        }
        if (!observed) return out;
        const auto& life = observed->lifecycle;
        const bool fresh = observed->client_metadata.freshness ==
            client::RuntimeObservationFreshness::observed_in_record &&
            observed->client_metadata.source &&
            (!state_.last_sample || *state_.last_sample != *observed->client_metadata.source);
        if (fresh) state_.last_sample = observed->client_metadata.source;
        if (state_.server_alive && fresh) ++state_.post_respawn_samples;
        switch (state_.phase) {
        case DamageRespawnPhase::initial:
            if (life.state == client::LocalPlayerLifeState::alive && fresh) {
                state_.initial_epoch = life.life_epoch;
                state_.deaths_before = life.deaths;
                state_.damage_before = life.damage_events;
                state_.kill_queued = true;
                out.request_self_kill = true;
                enter(DamageRespawnPhase::death, seconds);
            }
            break;
        case DamageRespawnPhase::death:
            if (life.dead() && life.deaths > state_.deaths_before)
                enter(DamageRespawnPhase::release_press, seconds);
            break;
        case DamageRespawnPhase::release_press:
            if (life.state == client::LocalPlayerLifeState::alive &&
                life.life_epoch > state_.initial_epoch) {
                state_.server_alive = true;
                enter(DamageRespawnPhase::new_life_movement, seconds);
            } else {
                const auto age = seconds - phase_start_;
                // At least 300ms neutral then 120ms held LMB, then release.
                // Server think cadence can sample the press; not a prediction.
                if (age >= 0.3 && std::fmod(age - 0.3, 0.5) < 0.12) {
                    out.buttons = 1U;
                    state_.respawn_input_submitted = true;
                }
            }
            break;
        case DamageRespawnPhase::new_life_movement:
            ++state_.post_respawn_commands;
            out.forward = seconds - phase_start_ < 0.4;
            if (seconds - phase_start_ >= 0.6 && state_.post_respawn_samples >= 2U)
                enter(DamageRespawnPhase::glock, seconds);
            break;
        case DamageRespawnPhase::glock:
        case DamageRespawnPhase::crowbar: {
            const bool glock = state_.phase == DamageRespawnPhase::glock;
            const std::string_view name = glock ? "weapon_9mmhandgun" : "weapon_crowbar";
            const auto type = std::find_if(observed->weapon_hud.catalogue.begin(),
                observed->weapon_hud.catalogue.end(),
                [name](const auto& value) { return value.command_name == name; });
            if (type == observed->weapon_hud.catalogue.end() ||
                !observed->receiving_client || !observed->receiving_client->owned_weapon_bits ||
                type->id >= 32U ||
                (*observed->receiving_client->owned_weapon_bits & (1U << type->id)) == 0U)
                break;
            const bool bound = observed->weapon_hud.active_weapon_id == type->id &&
                observed->receiving_client->viewmodel_index &&
                *observed->receiving_client->viewmodel_index != 0U &&
                (!life.last_death_source ||
                    (observed->weapon_hud.active_source &&
                     observed->weapon_hud.active_source->record_ordinal >
                         life.last_death_source->record_ordinal &&
                     observed->client_metadata.source &&
                     observed->client_metadata.source->record_ordinal >=
                         observed->weapon_hud.active_source->record_ordinal)) &&
                (glock || !glock_model_ ||
                 observed->receiving_client->viewmodel_index != glock_model_);
            if (bound) {
                if (glock) {
                    state_.glock_bound = true;
                    glock_model_ = observed->receiving_client->viewmodel_index;
                }
                else state_.crowbar_bound = true;
                enter(glock ? DamageRespawnPhase::crowbar : DamageRespawnPhase::neutral, seconds);
            } else if (!selection_requested_) {
                out.select_weapon = type->id;
                selection_requested_ = true;
            }
            break;
        }
        case DamageRespawnPhase::neutral:
            if (seconds - phase_start_ >= 0.4) enter(DamageRespawnPhase::complete, seconds);
            break;
        default: break;
        }
        return out;
    }
private:
    void enter(DamageRespawnPhase phase, double seconds) noexcept {
        state_.phase = phase; phase_start_ = seconds; selection_requested_ = false;
    }
    void block(std::string_view reason) noexcept {
        state_.phase = DamageRespawnPhase::blocked; state_.blocker = reason;
    }
    [[nodiscard]] double deadline() const noexcept {
        switch (state_.phase) {
        case DamageRespawnPhase::death: return 7.0;
        case DamageRespawnPhase::release_press: return 10.0;
        case DamageRespawnPhase::glock:
        case DamageRespawnPhase::crowbar: return 4.0;
        default: return 3.0;
        }
    }
    DamageRespawnScriptSnapshot state_;
    double last_time_{}, phase_start_{};
    bool selection_requested_{};
    std::optional<std::uint32_t> glock_model_;
};
}

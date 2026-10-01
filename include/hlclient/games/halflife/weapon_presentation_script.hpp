#pragma once
#include <hlclient/client/runtime_observation.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace hlclient::games::halflife {
// Only the explicit owned presentation-check mode uses this bounded driver.
// It requests actions once and waits for canonical evidence; it never retries
// an unconfirmed shot, fabricates ammo, or predicts a hit.
class WeaponPresentationScript final {
public:
  [[nodiscard]] std::uint16_t buttons(
      const client::RuntimeClientObservationState* state,
      std::size_t phase, double seconds) noexcept {
    if (!state || !state->receiving_client || !std::isfinite(seconds) ||
        seconds < 0.0 || seconds < last_time_ || state->generation == 0U ||
        state->publication_revision < last_revision_)
      return 0U;
    last_time_ = seconds;
    last_revision_ = state->publication_revision;
    if (generation_ == 0U) generation_ = state->generation;
    if (generation_ != state->generation) return 0U;
    if (!state->receiving_client->viewmodel_index ||
        *state->receiving_client->viewmodel_index == 0U ||
        (state->weapon_hud.active_source &&
         (!state->client_metadata.source ||
          state->client_metadata.source->record_ordinal <
              state->weapon_hud.active_source->record_ordinal))) return 0U;
    const auto active = state->weapon_hud.active_weapon_id;
    const auto type = std::find_if(state->weapon_hud.catalogue.begin(),
        state->weapon_hud.catalogue.end(),
        [active](const auto& item) { return item.id == active; });
    if (type == state->weapon_hud.catalogue.end() ||
        (active == 2U && type->command_name != "weapon_9mmhandgun") ||
        (active == 1U && type->command_name != "weapon_crowbar")) return 0U;
    if (active == 2U) {
      glock_model_ = state->receiving_client->viewmodel_index;
      std::optional<std::int32_t> clip;
      const auto slot = std::find_if(state->weapon_slots.begin(), state->weapon_slots.end(),
        [](const auto& value) { return value.weapon_id == 2U; });
      if (slot != state->weapon_slots.end()) clip = slot->clip;
      if (!clip && state->weapon_hud.clips[2U]) clip = state->weapon_hud.clips[2U];
      const auto reserve = type->primary_ammo_type >= 0 &&
          static_cast<std::size_t>(type->primary_ammo_type) < state->weapon_hud.reserve_ammo.size()
          ? state->weapon_hud.reserve_ammo[static_cast<std::size_t>(type->primary_ammo_type)]
          : std::optional<std::uint8_t>{};
      const auto ordinal = std::max(
        state->client_metadata.source ? state->client_metadata.source->record_ordinal : 0U,
        state->weapon_hud.last_message_source ? state->weapon_hud.last_message_source->record_ordinal : 0U);
      if (shot_pending_ && clip && shot_clip_ && ordinal > shot_ordinal_ && *clip == *shot_clip_ - 1) {
        ++shots_confirmed_; shot_pending_ = false;
      }
      if (reload_requested_ && clip && reload_clip_ && *clip > *reload_clip_ &&
          reserve && reload_reserve_ && *reserve < *reload_reserve_)
        reload_completed_ = true;
      if (phase == 1U && clip && *clip > 0 && !shot_pending_ &&
          shots_requested_ < 2U && shots_requested_ == shots_confirmed_ &&
          seconds - shot_at_ >= 0.7 &&
          (!state->receiving_client->next_weapon_attack ||
           *state->receiving_client->next_weapon_attack <= 0.001) &&
          (slot == state->weapon_slots.end() || !slot->next_primary_attack ||
           *slot->next_primary_attack <= 0.001)) {
        ++shots_requested_; shot_pending_ = true; shot_clip_ = clip;
        shot_ordinal_ = ordinal; shot_at_ = seconds;
        return 1U; // exactly one generated command pulse, no held-fire retry
      }
      if (phase == 2U && shots_confirmed_ == 2U && !reload_requested_ &&
          clip && *clip < 17 && reserve && *reserve > 0U && seconds - shot_at_ >= 0.7) {
        reload_requested_ = true; reload_clip_ = clip; reload_reserve_ = reserve;
        return 1U << 13U;
      }
    }
    if (phase == 3U && active == 1U && reload_completed_ && !swing_requested_ &&
        glock_model_ && state->receiving_client->viewmodel_index != glock_model_) {
      if (!crowbar_ready_at_) crowbar_ready_at_ = seconds;
      // Wait the new model/weapon binding AND its draw/cooldown window.
      if (seconds - *crowbar_ready_at_ < 0.7 ||
          (state->receiving_client->next_weapon_attack &&
           *state->receiving_client->next_weapon_attack > 0.001)) return 0U;
      swing_requested_ = true;
      return 1U;
    }
    return 0U;
  }
  [[nodiscard]] std::size_t shots_confirmed() const noexcept { return shots_confirmed_; }
  [[nodiscard]] bool reload_completed() const noexcept { return reload_completed_; }
private:
  std::uint64_t generation_{}, last_revision_{};
  std::size_t shots_requested_{}, shots_confirmed_{}, shot_ordinal_{};
  std::optional<std::int32_t> shot_clip_, reload_clip_;
  std::optional<std::uint8_t> reload_reserve_;
  std::optional<std::uint32_t> glock_model_;
  std::optional<double> crowbar_ready_at_;
  double shot_at_{-1.0}, last_time_{};
  bool shot_pending_{}, reload_requested_{}, reload_completed_{}, swing_requested_{};
};
} // namespace hlclient::games::halflife

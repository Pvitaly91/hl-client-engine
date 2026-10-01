#include <hlclient/games/halflife/action_evidence.hpp>
#include <algorithm>
#include <cmath>

namespace hlclient::games::halflife {
void HalfLifeActionEvidence::observe(
    const client::RuntimeClientObservationState& observed,
    const std::optional<game_api::GameActionTraffic>& traffic) noexcept {
  if (!use_window_started_ && traffic && traffic->use_new_submission_count > 0U) {
    use_window_started_ = true;
    snapshot_.use_health_before = last_health_;
    snapshot_.use_armor_before = last_armor_;
  }
  if (use_window_started_) {
    snapshot_.use_health_after = observed.weapon_hud.health;
    snapshot_.use_armor_after = observed.weapon_hud.armor;
  }
  last_health_ = observed.weapon_hud.health;
  last_armor_ = observed.weapon_hud.armor;
  const auto active_name = [&](const std::string_view name) {
    if (!observed.weapon_hud.active_weapon_id) return false;
    return std::any_of(observed.weapon_hud.catalogue.begin(),
        observed.weapon_hud.catalogue.end(), [&](const auto& type) {
          return type.id == *observed.weapon_hud.active_weapon_id &&
                 type.command_name == name;
        });
  };
  const bool glock = active_name("weapon_9mmhandgun");
  const bool crowbar = active_name("weapon_crowbar");
  bool fresh_glock_slot_clip = false;
  if (observed.weapon_hud.animation_source &&
      observed.weapon_hud.animation_source != snapshot_.last_animation_source) {
    snapshot_.last_animation_source = observed.weapon_hud.animation_source;
    snapshot_.last_animation_kind.reset();
    ++snapshot_.service_animation_count;
    if (traffic && traffic->attack_new_submission_count > 0U && glock &&
        traffic->generated_by_phase[1U] > 0U &&
        traffic->generated_by_phase[2U] == 0U)
      { ++snapshot_.primary_animation_count; snapshot_.last_animation_kind = 0U; }
    if (traffic && traffic->reload_new_submission_count > 0U && glock &&
        traffic->generated_by_phase[2U] > 0U &&
        traffic->generated_by_phase[3U] == 0U)
      { ++snapshot_.reload_animation_count; snapshot_.last_animation_kind = 1U; }
    if (traffic && traffic->attack_new_submission_count > 0U && crowbar &&
        traffic->generated_by_phase[3U] > 0U)
      { ++snapshot_.melee_animation_count; snapshot_.last_animation_kind = 2U; }
  }
  if (glock) {
    const auto type = std::find_if(observed.weapon_hud.catalogue.begin(),
        observed.weapon_hud.catalogue.end(), [&](const auto& item) {
          return item.id == *observed.weapon_hud.active_weapon_id;
        });
    if (type != observed.weapon_hud.catalogue.end() &&
        type->primary_ammo_type >= 0 &&
        static_cast<std::size_t>(type->primary_ammo_type) <
            observed.weapon_hud.reserve_ammo.size()) {
      const auto reserve = observed.weapon_hud.reserve_ammo[
          static_cast<std::size_t>(type->primary_ammo_type)];
      if (reserve) {
        if (traffic && traffic->reload_new_submission_count > 0U) {
          if (!snapshot_.reserve_before_reload) snapshot_.reserve_before_reload = last_glock_reserve_;
          snapshot_.reserve_after_reload = reserve;
        }
        last_glock_reserve_ = reserve;
      }
    }
  }
  if (observed.client_metadata.freshness ==
          hlclient::client::RuntimeObservationFreshness::observed_in_record &&
      observed.client_metadata.source &&
      observed.client_metadata.source != last_action_client_source_) {
    last_action_client_source_ = observed.client_metadata.source;
    if (traffic && traffic->attack_new_submission_count > 0U &&
        observed.receiving_client &&
        observed.receiving_client->punch_angle.complete()) {
      const auto& punch = observed.receiving_client->punch_angle;
      const auto magnitude = std::hypot(*punch.x, *punch.y, *punch.z);
      if (magnitude > 0.0) {
        ++snapshot_.server_punch_observations;
        snapshot_.maximum_server_punch_degrees =
            std::max(snapshot_.maximum_server_punch_degrees, magnitude);
      }
    }
    if (glock && observed.weapon_hud.active_weapon_id) {
      const auto slot = std::find_if(observed.weapon_slots.begin(),
          observed.weapon_slots.end(), [&](const auto& item) {
            return item.weapon_id &&
                *item.weapon_id == *observed.weapon_hud.active_weapon_id;
          });
      if (slot != observed.weapon_slots.end()) {
        if (slot->clip && *slot->clip >= 0) {
          fresh_glock_slot_clip = true;
          if (traffic && traffic->attack_new_submission_count > 0U &&
              last_glock_clip_ && *slot->clip < *last_glock_clip_) {
            if (!snapshot_.clip_before_fire) snapshot_.clip_before_fire = last_glock_clip_;
            snapshot_.server_confirmed_shots += static_cast<std::size_t>(
                *last_glock_clip_ - *slot->clip);
            snapshot_.clip_after_fire = slot->clip;
          }
          if (traffic && traffic->reload_new_submission_count > 0U &&
              last_glock_clip_ && *slot->clip > *last_glock_clip_) {
            ++snapshot_.server_confirmed_reload_completions;
            snapshot_.clip_after_reload = slot->clip;
          }
          last_glock_clip_ = slot->clip;
        }
        if (slot->in_reload) {
          if (traffic && traffic->reload_new_submission_count > 0U &&
              *slot->in_reload &&
              (!last_glock_in_reload_ || !*last_glock_in_reload_))
            ++snapshot_.server_confirmed_reload_starts;
          last_glock_in_reload_ = slot->in_reload;
        }
      }
    }
  }
  if (observed.weapon_hud.revision != last_action_hud_revision_) {
    last_action_hud_revision_ = observed.weapon_hud.revision;
    if (glock && !fresh_glock_slot_clip &&
        observed.weapon_hud.active_weapon_id &&
        *observed.weapon_hud.active_weapon_id <
            observed.weapon_hud.clips.size()) {
      const auto clip = observed.weapon_hud.clips[
          *observed.weapon_hud.active_weapon_id];
      if (clip && *clip >= 0) {
        if (traffic && traffic->attack_new_submission_count > 0U &&
            last_glock_clip_ && *clip < *last_glock_clip_) {
          if (!snapshot_.clip_before_fire) snapshot_.clip_before_fire = last_glock_clip_;
          snapshot_.server_confirmed_shots += static_cast<std::size_t>(
              *last_glock_clip_ - *clip);
          snapshot_.clip_after_fire = clip;
        }
        if (traffic && traffic->reload_new_submission_count > 0U &&
            last_glock_clip_ && *clip > *last_glock_clip_) {
          ++snapshot_.server_confirmed_reload_completions;
          snapshot_.clip_after_reload = clip;
        }
        last_glock_clip_ = clip;
      }
    }
  }
}
}

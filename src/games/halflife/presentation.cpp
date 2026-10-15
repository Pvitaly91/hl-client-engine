#include <hlclient/games/halflife/presentation.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/gauss.hpp>
#include <hlclient/games/halflife/egon.hpp>
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <hlclient/games/halflife/hand_grenade_profile.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace hlclient::games::halflife {
void HalfLifePresentation::bind_model(std::optional<game_api::LocalWeaponModelMetadata> model) {
  controller_.bind_model(std::move(model));
}
void HalfLifePresentation::observe(const client::RuntimeClientObservationState& state, double now) {
  advance_pickup_clock(now);
  observe_projection(state);
  controller_.configure_projection(horizontal_fov_);
  controller_.observe(state, now);
}
void HalfLifePresentation::submit(const game_api::LocalWeaponSubmittedCommand& command, double now) {
  const auto before=controller_.current_action_identity();
  controller_.submit(command, now);
  const auto after=controller_.current_action_identity();
  if(after && after!=before && (after->weapon_id==3U || after->weapon_id==6U) && after->kind==game_api::LocalWeaponAction::reload)
    reset_projection();
}
void HalfLifePresentation::cancel_uncommitted() noexcept { controller_.cancel_uncommitted(); }
game_api::LocalWeaponPresentationSnapshot HalfLifePresentation::sample(double now) noexcept {
  return controller_.sample(now);
}
void HalfLifePresentation::reset() noexcept { *this = HalfLifePresentation{}; }

void HalfLifePresentation::reset_projection() noexcept {
  horizontal_fov_=90.0;projection_status_=game_api::CameraIntent::ProjectionStatus::reset;
  projection_floor_=projection_source_;
}
void HalfLifePresentation::observe_projection(const client::RuntimeClientObservationState& s) noexcept {
  if(!s.generation || s.generation<projection_generation_) return;
  if(s.generation==projection_generation_ && s.publication_revision<projection_revision_) return;
  const auto newer=[](const auto& a,const auto& b) {
    return a.record_ordinal>b.record_ordinal || (a.record_ordinal==b.record_ordinal &&
        a.start_bit_offset>b.start_bit_offset);
  };
  if(s.generation!=projection_generation_) {
    projection_generation_=s.generation;projection_source_.reset();projection_floor_.reset();
    horizontal_fov_=90;projection_status_=game_api::CameraIntent::ProjectionStatus::reset;
    projection_weapon_=s.weapon_hud.active_weapon_id;
    projection_life_=s.lifecycle.life_epoch;projection_deaths_=s.lifecycle.deaths;
  }
  projection_revision_=s.publication_revision;
  const bool reset=s.lifecycle.dead() ||
      (s.receiving_client && s.receiving_client->health && *s.receiving_client->health<=0) ||
      projection_life_!=s.lifecycle.life_epoch || projection_deaths_!=s.lifecycle.deaths ||
      ((projection_weapon_==3U || projection_weapon_==6U) && s.weapon_hud.active_weapon_id!=projection_weapon_);
  projection_life_=s.lifecycle.life_epoch;projection_deaths_=s.lifecycle.deaths;
  projection_weapon_=s.weapon_hud.active_weapon_id;
  const auto candidate=[&](double value,const client::RuntimeObservationSource& source) {
    if(!source.record_identity || !source.record_ordinal ||
        (projection_source_ && !newer(source,*projection_source_)) ||
        (projection_floor_ && !newer(source,*projection_floor_))) return;
    projection_source_=source;
    if(!std::isfinite(value) || value<0 || value>179 || (value>0 && value<1)) {
      horizontal_fov_=90;projection_status_=game_api::CameraIntent::ProjectionStatus::invalid;return;
    }
    horizontal_fov_=value==0 ? 90 : value;
    projection_status_=game_api::CameraIntent::ProjectionStatus::server_confirmed;
  };
  if(s.client_metadata.generation==s.generation &&
      s.client_metadata.freshness==client::RuntimeObservationFreshness::observed_in_record &&
      s.client_metadata.source && s.receiving_client && s.receiving_client->field_of_view)
    candidate(*s.receiving_client->field_of_view,*s.client_metadata.source);
  if(s.weapon_hud.field_of_view_source)
    candidate(s.weapon_hud.field_of_view.value_or(0),*s.weapon_hud.field_of_view_source);
  const auto slot=std::find_if(s.weapon_slots.begin(),s.weapon_slots.end(),[&](const auto& w){return w.weapon_id==projection_weapon_;});
  if(reset || ((projection_weapon_==3U || projection_weapon_==6U) && slot!=s.weapon_slots.end() && slot->in_reload.value_or(false)))
    reset_projection();
}

void HalfLifePresentation::commit_pickups(game_api::GameRecordState& state) noexcept {
  if (pickup_life_ != state.lifecycle.life_epoch || pickup_deaths_ != state.lifecycle.deaths ||
      state.lifecycle.dead()) {
    for (auto& row : pickup_rows_) row = {};
    pickup_next_ = 0U;
    pickup_life_ = state.lifecycle.life_epoch;
    pickup_deaths_ = state.lifecycle.deaths;
    ++pickup_revision_;
  }
  if (state.lifecycle.dead()) return;
  for (auto& event : state.pickups) {
    if (state.lifecycle.last_death_source &&
        (event.source.reassembled || event.source.record_ordinal <=
             state.lifecycle.last_death_source->record_ordinal))
      continue; // Unprovable prior-life reliable completion is not new-life feedback.
    auto& row = pickup_rows_[pickup_next_++ % pickup_rows_.size()];
    row.event = std::move(event);
    row.expires_at.reset();
    ++pickup_received_;
    ++pickup_revision_;
  }
}

void HalfLifePresentation::advance_pickup_clock(double now) noexcept {
  if (!std::isfinite(now) || (pickup_clock_ && now < *pickup_clock_)) return;
  pickup_clock_ = now;
  for (auto& row : pickup_rows_) {
    if (!row.event) continue;
    if (!row.expires_at) row.expires_at = now + 5.0;
    else if (now >= *row.expires_at) { row = {}; ++pickup_revision_; }
  }
}

game_api::CameraIntent HalfLifePresentation::camera(
    const client::RuntimeClientObservationState& observation, double local_punch) const noexcept {
  game_api::CameraIntent result;
  result.horizontal_field_of_view_degrees=horizontal_fov_;
  result.projection_status=projection_status_;
  // Pinned Valve HUD policy: default FOV restores base sensitivity; other
  // confirmed FOV values use the default zoom_sensitivity_ratio of 1.2.
  // Derive both outputs from the same reconciled value, including reset floors.
  result.mouse_sensitivity_scale=horizontal_fov_==90.0 ? 1.0 : horizontal_fov_/90.0*1.2;
  result.allow_predicted_translation = !observation.lifecycle.dead();
  result.authoritative_translation_blend_seconds = observation.lifecycle.dead() ? 0.1 : 0.0;
  if (!observation.receiving_client || !observation.receiving_client->health) {
    result.status = game_api::CameraIntentStatus::health_unavailable; return result;
  }
  const auto& receiving = *observation.receiving_client;
  if (*receiving.health <= 0.0 && !observation.lifecycle.dead()) {
    result.status = game_api::CameraIntentStatus::receiving_client_not_alive; return result;
  }
  if (receiving.punch_angle.complete()) {
    const auto x = *receiving.punch_angle.x, y = *receiving.punch_angle.y, z = *receiving.punch_angle.z;
    if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
        std::abs(x) <= 45.0 && std::abs(y) <= 45.0 && std::abs(z) <= 45.0) {
      result.server_punch_angle = std::array{static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
      result.pitch_offset_degrees = -(*result.server_punch_angle)[0];
      result.yaw_offset_degrees = (*result.server_punch_angle)[1];
      result.roll_degrees = (*result.server_punch_angle)[2];
    }
  }
  if (!std::isfinite(local_punch) || std::abs(local_punch) > 10.0) {
    result.status = game_api::CameraIntentStatus::invalid_local_punch; return result;
  }
  // Half-Life punch pitch is downward-positive; the geometric camera uses up.
  result.local_pitch_offset_degrees = -local_punch;
  return result;
}

game_api::ViewmodelIntent HalfLifePresentation::viewmodel(
    const client::RuntimeClientObservationState& observation,
    const std::optional<game_api::LocalWeaponModelMetadata>& input_metadata,
    double now, std::optional<game_api::LocalWeaponVisual> local_visual) {
  game_api::ViewmodelIntent result;
  result.generation = observation.generation;
  result.publication_revision = observation.publication_revision;
  result.local_time_seconds = now;
  if (viewmodel_generation_ != observation.generation ||
      life_epoch_ != observation.lifecycle.life_epoch || life_deaths_ != observation.lifecycle.deaths) {
    viewmodel_generation_ = observation.generation;
    life_epoch_ = observation.lifecycle.life_epoch; life_deaths_ = observation.lifecycle.deaths;
    bound_viewmodel_index_.reset(); bound_weapon_id_.reset();
    animated_model_index_.reset(); animated_sequence_.reset();
    animation_source_.reset(); local_animation_restart_.reset();
  }
  if (!std::isfinite(now) || observation.lifecycle.dead() ||
      ((observation.weapon_hud.hide_flags.value_or(0U) & 5U) != 0U) ||
      (observation.lifecycle.last_death_source &&
       (!observation.weapon_hud.active_source || observation.weapon_hud.active_source->record_ordinal <=
            observation.lifecycle.last_death_source->record_ordinal)) ||
      !observation.receiving_client ||
      (observation.receiving_client->health && *observation.receiving_client->health <= 0.0) ||
      !observation.receiving_client->viewmodel_index || *observation.receiving_client->viewmodel_index == 0U ||
      !observation.weapon_hud.active_weapon_id || observation.weapon_hud.active_weapon_id == 0U) {
    bound_viewmodel_index_.reset(); bound_weapon_id_.reset(); animated_model_index_.reset();
    animation_source_.reset(); local_animation_restart_.reset();
    return result;
  }
  const auto satchel_metadata=controller_.satchel_presentation_model();
  const auto& metadata=satchel_metadata ? satchel_metadata : input_metadata;
  const auto index = satchel_metadata ? satchel_metadata->model_index : *observation.receiving_client->viewmodel_index;
  const auto active = observation.weapon_hud.active_weapon_id;
  // The exhaustible last throw cannot fall back to a retained server idle pose
  // while its authoritative inventory retirement is in flight.
  if (active==12U && controller_.hand_grenade_waiting_retirement()) return result;
  if (active==14U && controller_.satchel_waiting_retirement()) return result;
  if (active==13U && controller_.tripmine_waiting_retirement()) return result;
  if (active==15U && controller_.snark_waiting_retirement()) return result;
  result.model_index = index;
  if (!bound_viewmodel_index_) {
    bound_viewmodel_index_ = index; bound_weapon_id_ = active;
    model_bound_record_ordinal_ = observation.client_metadata.source
        ? observation.client_metadata.source->record_ordinal : 0U;
  } else if (*bound_viewmodel_index_ != index || bound_weapon_id_ != active) {
    if(active==14U && bound_weapon_id_==14U && metadata && satchel_sequences(*metadata)) {
      bound_viewmodel_index_=index;
      model_bound_record_ordinal_=observation.client_metadata.source ? observation.client_metadata.source->record_ordinal : 0U;
    } else {
    // Independent CurWeapon/clientdata channels cannot expose a half-transition.
    if (*bound_viewmodel_index_ != index && (!bound_weapon_id_ || (active && active != bound_weapon_id_))) {
      bound_viewmodel_index_ = index; bound_weapon_id_ = active;
      model_bound_record_ordinal_ = observation.client_metadata.source
          ? observation.client_metadata.source->record_ordinal : 0U;
    } else if (!bound_weapon_id_ && *bound_viewmodel_index_ == index && active) {
      bound_weapon_id_ = active;
    } else return result;
    }
  }
  if (!metadata || metadata->generation != observation.generation || metadata->model_index != index ||
      !metadata->resource_name.starts_with("models/v_") || !metadata->resource_name.ends_with(".mdl")) {
    result.status = game_api::ViewmodelStatus::unsupported_asset; return result;
  }
  const bool model_changed = animated_model_index_ != index;
  const bool valid_event = observation.weapon_hud.animation_source &&
      observation.weapon_hud.animation_source->record_ordinal >= model_bound_record_ordinal_;
  auto event_source = valid_event ? observation.weapon_hud.animation_source
      : std::optional<client::RuntimeObservationSource>{};
  auto sequence = valid_event && observation.weapon_hud.animation_sequence
      ? *observation.weapon_hud.animation_sequence : observation.receiving_client->weapon_animation.value_or(0U);
  auto body = valid_event ? observation.weapon_hud.animation_body.value_or(0U) : std::uint8_t{0U};
  if(!valid_event && active==3U && python_sequences(*metadata))
    body=static_cast<std::uint8_t>(maximum_clients_>1U);
  std::optional<std::uint64_t> local_restart;
  double requested_start_time = now;
  const bool standard_model = (active == 2U && metadata->resource_name == "models/v_9mmhandgun.mdl") ||
      (active == 1U && metadata->resource_name == "models/v_crowbar.mdl") ||
      (active == 4U && mp5_sequences(*metadata)) ||
      (active == 7U && shotgun_sequences(*metadata)) ||
      (active == 3U && python_sequences(*metadata)) ||
      (active == 6U && crossbow_sequences(*metadata)) ||
      (active == 8U && rpg_sequences(*metadata)) ||
      (active == 9U && gauss_sequences(*metadata)) ||
      (active == 10U && egon_sequences(*metadata)) ||
      (active == 11U && hornet_sequences(*metadata)) ||
      (active == 12U && hand_grenade_sequences(*metadata)) ||
      (active == 14U && satchel_sequences(*metadata)) ||
      (active == 13U && tripmine_sequences(*metadata)) ||
      (active == 15U && snark_sequences(*metadata));
  if (standard_model && local_visual && local_visual->sequence < metadata->sequences.size()) {
    const auto& candidate = metadata->sequences[local_visual->sequence];
    if ((candidate.frame_count > 1U || (active==10U && candidate.frame_count==1U)) && std::isfinite(candidate.fps) && candidate.fps > 0.0 &&
        now - local_visual->started_at_seconds >= 0.0 && metadata->selectable_bodies[local_visual->body]) {
      sequence = local_visual->sequence; body = local_visual->body; event_source.reset();
      local_restart = local_visual->restart_identity; requested_start_time = local_visual->started_at_seconds;
    }
  }
  if (sequence >= metadata->sequences.size() || metadata->sequences[sequence].frame_count == 0U) {
    if (model_changed || !animated_sequence_ || *animated_sequence_ >= metadata->sequences.size() ||
        metadata->sequences[*animated_sequence_].frame_count == 0U) {
      result.status = game_api::ViewmodelStatus::unsupported_pose; return result;
    }
    sequence = *animated_sequence_; event_source = animation_source_; body = animated_body_;
  }
  if (model_changed || animation_source_ != event_source || local_animation_restart_ != local_restart ||
      animated_sequence_ != sequence || animated_body_ != body) {
    animated_model_index_ = index; animation_source_ = event_source; local_animation_restart_ = local_restart;
    animated_sequence_ = sequence; animated_body_ = body; animation_started_at_ = requested_start_time;
  }
  result.status = game_api::ViewmodelStatus::ready;
  result.sequence = sequence; result.body = body;
  // Studio evaluator retains its original looping/clamping mechanism.
  result.frame_coordinate = std::max(0.0, (now - animation_started_at_) * metadata->sequences[sequence].fps);
  return result;
}

game_api::HudState HalfLifePresentation::hud(const client::RuntimeClientObservationState& observation, double now) {
  game_api::HudState hud;
  advance_pickup_clock(now);
  hud.inventory_feedback_revision = pickup_revision_;
  hud.inventory_notifications_received = pickup_received_;
  const auto hidden = observation.weapon_hud.hide_flags.value_or(0U);
  if ((hidden & (1U << 2U | 1U << 3U)) == 0U) {
    if (observation.receiving_client && observation.receiving_client->health)
      hud.health = static_cast<int>(std::lround(*observation.receiving_client->health));
    if (observation.weapon_hud.health) hud.health = *observation.weapon_hud.health;
    if (observation.weapon_hud.armor) hud.armor = *observation.weapon_hud.armor;
  }
  if (feedback_generation_ != observation.generation) {
    feedback_generation_ = observation.generation; feedback_life_epoch_ = 0U;
    feedback_revision_ = 0U; feedback_until_ = 0.0;
  }
  if (feedback_life_epoch_ != observation.lifecycle.life_epoch) {
    feedback_life_epoch_ = observation.lifecycle.life_epoch; feedback_until_ = 0.0;
    if (observation.lifecycle.respawns > 0U) feedback_revision_ = observation.lifecycle.feedback_revision;
  }
  if (feedback_revision_ != observation.lifecycle.feedback_revision) {
    feedback_revision_ = observation.lifecycle.feedback_revision; feedback_until_ = now + 0.6;
  }
  if (observation.lifecycle.dead()) hud.lifecycle_indicator = "DEAD - RELEASE THEN SPACE OR LMB";
  else if (now < feedback_until_) hud.lifecycle_indicator = "DAMAGE";
  if (observation.weapon_hud.active_weapon_id == 0U) hud.weapon_name = "NO WEAPON";
  if ((hidden & (1U | 1U << 2U)) == 0U &&
      (!observation.receiving_client || !observation.receiving_client->health ||
       *observation.receiving_client->health > 0.0) && observation.weapon_hud.active_weapon_id &&
      *observation.weapon_hud.active_weapon_id != 0U) {
    const auto id = *observation.weapon_hud.active_weapon_id;
    const auto found = std::find_if(observation.weapon_hud.catalogue.begin(), observation.weapon_hud.catalogue.end(),
        [id](const auto& type) { return type.id == id; });
    if (found != observation.weapon_hud.catalogue.end()) {
      hud.weapon_name = found->command_name;
      // The signed no-clip sentinel remains canonical server data. An absent
      // display field represents a weapon without a magazine, not negative ammo.
      if (id < observation.weapon_hud.clips.size() && observation.weapon_hud.clips[id] &&
          *observation.weapon_hud.clips[id] >= 0)
        hud.clip = *observation.weapon_hud.clips[id];
      if (found->primary_ammo_type >= 0 &&
               static_cast<std::size_t>(found->primary_ammo_type) < observation.weapon_hud.reserve_ammo.size() &&
               observation.weapon_hud.reserve_ammo[static_cast<std::size_t>(found->primary_ammo_type)])
        hud.primary_reserve = *observation.weapon_hud.reserve_ammo[static_cast<std::size_t>(found->primary_ammo_type)];
    }
  }
  const auto label = [](std::optional<int> value) {
    return !value ? std::string{"?"} : *value == -1 ? std::string{"-"} : std::to_string(*value);
  };
  auto text = "HP " + label(hud.health) + "  ARM " + label(hud.armor) + "\n";
  text += hud.weapon_name.empty() ? "WEAPON ?" : hud.weapon_name;
  if(observation.weapon_hud.active_weapon_id==1U && hud.weapon_name=="weapon_crowbar") {
    // Crowbar owns neither a clip nor an ammunition pool.
  } else if((observation.weapon_hud.active_weapon_id==9U && hud.weapon_name=="weapon_gauss") ||
      (observation.weapon_hud.active_weapon_id==10U && hud.weapon_name=="weapon_egon")) {
    hud.clip.reset();text += "  URANIUM " + label(hud.primary_reserve);
  } else if(observation.weapon_hud.active_weapon_id==11U && hornet_command(hud.weapon_name)) {
    hud.clip.reset();
    // No predicted reserve or recharge clock. This is the current assigned
    // AmmoX slot, including catch-up increases while another weapon was held.
    if(hud.primary_reserve && (*hud.primary_reserve<0 || *hud.primary_reserve>hornet_maximum_ammo))
      hud.primary_reserve.reset();
    hud.weapon_name="Hornet Gun";
    text="HP " + label(hud.health) + "  ARM " + label(hud.armor) + "\n" +
        hud.weapon_name + "  HORNETS " + label(hud.primary_reserve);
  } else if(observation.weapon_hud.active_weapon_id==12U && hand_grenade_command(hud.weapon_name)) {
    hud.clip.reset();
    if(hud.primary_reserve && (*hud.primary_reserve<0 || *hud.primary_reserve>hand_grenade_maximum_ammo))
      hud.primary_reserve.reset();
    hud.weapon_name="Hand Grenade";
    text="HP " + label(hud.health) + "  ARM " + label(hud.armor) + "\n" +
        hud.weapon_name + "  GRENADES " + label(hud.primary_reserve);
  } else if(observation.weapon_hud.active_weapon_id==13U && tripmine_command(hud.weapon_name)) {
    hud.clip.reset();
    if(hud.primary_reserve && (*hud.primary_reserve<0 || *hud.primary_reserve>tripmine_maximum_ammo)) hud.primary_reserve.reset();
    hud.weapon_name="Trip Mine";
    text="HP " + label(hud.health) + "  ARM " + label(hud.armor) + "\n" + hud.weapon_name + "  MINES " + label(hud.primary_reserve);
  } else if(observation.weapon_hud.active_weapon_id==14U && satchel_command(hud.weapon_name)) {
    hud.clip.reset();
    if(hud.primary_reserve && (*hud.primary_reserve<0 || *hud.primary_reserve>satchel_maximum_ammo)) hud.primary_reserve.reset();
    hud.weapon_name="Satchel Charge";
    text="HP " + label(hud.health) + "  ARM " + label(hud.armor) + "\n" + hud.weapon_name + "  SATCHELS " + label(hud.primary_reserve);
  } else if(observation.weapon_hud.active_weapon_id==15U && snark_command(hud.weapon_name)) {
    hud.clip.reset();
    if(hud.primary_reserve && (*hud.primary_reserve<0 || *hud.primary_reserve>snark_maximum_ammo))hud.primary_reserve.reset();
    hud.weapon_name="Snark";
    text="HP " + label(hud.health) + "  ARM " + label(hud.armor) + "\n" + hud.weapon_name + "  SNARKS " + label(hud.primary_reserve);
  } else text += "  CLIP " + label(hud.clip) + "  AMMO " + label(hud.primary_reserve);
  if (!hud.lifecycle_indicator.empty()) text += "\n" + hud.lifecycle_indicator;
  if (text.size() > 160U) text.resize(160U);
  hud.draw.rectangles.push_back({8.0F, 8.0F, 590.0F, 64.0F, {0.01F,0.02F,0.03F,0.78F}});
  hud.draw.texts.push_back({std::move(text),18.0F,16.0F,2.0F,12.0F,20.0F,{0.78F,0.92F,0.64F,1.0F}});
  if (std::isfinite(now) && !observation.lifecycle.dead() && (hidden & 4U) == 0U) {
    using Kind = game_api::GameRecordState::Pickup::Kind;
    for (std::size_t i = 0U; i < pickup_rows_.size(); ++i) {
      const auto& row = pickup_rows_[(pickup_next_ + i) % pickup_rows_.size()];
      if (!row.event || !row.expires_at) continue;
      const auto& event = *row.event;
      std::string pickup_label;
      if (event.kind == Kind::ammunition) {
        const auto glock = std::find_if(observation.weapon_hud.catalogue.begin(),
            observation.weapon_hud.catalogue.end(), [&](const auto& type) {
              return type.id == 2U && type.command_name == "weapon_9mmhandgun" &&
                     type.primary_ammo_type == event.identifier;
            });
        pickup_label = glock != observation.weapon_hud.catalogue.end() ? "GLOCK AMMO" :
            "AMMO " + std::to_string(event.identifier);
        if (event.amount) pickup_label += " +" + std::to_string(*event.amount);
      } else if (event.kind == Kind::weapon) {
        const auto type = std::find_if(observation.weapon_hud.catalogue.begin(),
            observation.weapon_hud.catalogue.end(),
            [&](const auto& value) { return value.id == event.identifier; });
        if (type != observation.weapon_hud.catalogue.end() && event.identifier == 2U &&
            type->command_name == "weapon_9mmhandgun") pickup_label = "GLOCK";
        else if (type != observation.weapon_hud.catalogue.end() && event.identifier == 1U &&
                 type->command_name == "weapon_crowbar") pickup_label = "CROWBAR";
        else if (type != observation.weapon_hud.catalogue.end() && event.identifier == 15U &&
                 snark_command(type->command_name)) pickup_label = "SNARK";
        else pickup_label = "WEAPON " + std::to_string(event.identifier) + " (UNSUPPORTED)";
      } else {
        pickup_label = event.item_token == "item_healthkit" ? "HEALTH KIT" :
            event.item_token == "item_battery" ? "BATTERY" :
            "ITEM " + event.item_token + " (UNSUPPORTED)";
      }
      // Item/weapon messages contain no exact numeric HP/armor/ammo gain.
      hud.inventory_feedback.push_back("PICKUP " + pickup_label);
    }
    if (!hud.inventory_feedback.empty()) {
      hud.draw.rectangles.push_back({8.0F,80.0F,650.0F,
          8.0F + 20.0F * static_cast<float>(hud.inventory_feedback.size()),
          {0.01F,0.02F,0.03F,0.78F}});
      for (std::size_t i = 0U; i < hud.inventory_feedback.size(); ++i)
        hud.draw.texts.push_back({hud.inventory_feedback[i],18.0F,
            84.0F + 20.0F * static_cast<float>(i),1.5F,9.0F,20.0F,
            {0.95F,0.82F,0.35F,1.0F}});
    }
  }
  return hud;
}
} // namespace hlclient::games::halflife

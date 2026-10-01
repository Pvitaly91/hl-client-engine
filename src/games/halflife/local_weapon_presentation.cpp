#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
#include <array>
#include <cmath>
namespace hlclient::games::halflife {
namespace {
constexpr std::array<std::uint32_t, 3U> misses{4U,5U,7U};
constexpr std::array<std::uint32_t, 3U> hits{3U,6U,8U};
bool finite_time(double t) noexcept { return std::isfinite(t) && t >= 0.0; }
bool newer(const client::RuntimeObservationSource& a,
           const std::optional<client::RuntimeObservationSource>& b) noexcept {
  return !b || a.record_ordinal > b->record_ordinal ||
    (a.record_ordinal == b->record_ordinal && a.start_bit_offset > b->start_bit_offset);
}
bool idle(std::uint8_t weapon, std::uint32_t seq) noexcept {
  return weapon == 2U ? seq <= 2U : seq == 0U;
}
}
bool LocalWeaponPresentationController::advance_time(double now) noexcept {
  if (!finite_time(now) || now < last_time_) {
    counters_.error = LocalWeaponPresentationError::invalid_time;
    return false;
  }
  last_time_ = now;
  return true;
}
bool LocalWeaponPresentationController::valid_pose(std::uint32_t seq, std::uint8_t body) const noexcept {
  if (!model_ || seq >= model_->sequences.size() || !model_->supported_bodies[body]) return false;
  const auto& data = model_->sequences[seq];
  return data.frame_count > 0U && std::isfinite(data.fps) && data.fps > 0.0;
}
bool LocalWeaponPresentationController::supported() const noexcept {
  if (!model_ || !weapon_id_ || canonical_dead_ || model_->generation != generation_ ||
      model_index_ != model_->model_index || model_->resource_revision == 0U) return false;
  const bool glock = *weapon_id_ == 2U && model_->resource_name == "models/v_9mmhandgun.mdl" &&
    model_->sequences.size() >= 10U;
  const bool crowbar = *weapon_id_ == 1U && model_->resource_name == "models/v_crowbar.mdl" &&
    model_->sequences.size() >= 9U;
  if (!glock && !crowbar) return false;
  for (auto seq : glock ? std::array{0U,3U,4U,5U,6U,0U,0U} :
                          std::array{0U,4U,5U,7U,3U,6U,8U})
    if (!valid_pose(seq, glock && (seq == 3U || seq == 4U) ? 2U :
                        !glock && seq != 0U ? 1U : 0U)) return false;
  return true;
}
void LocalWeaponPresentationController::bind_model(std::optional<LocalWeaponModelMetadata> model) {
  if (model_ == model) return;
  finish_pending(LocalWeaponActionStatus::server_rejected);
  action_.reset(); server_visual_.reset(); recoil_active_ = false;
  model_ = std::move(model); idle_sequence_ = 0U; idle_body_ = 0U;
  audio_.bind(model_);
}
void LocalWeaponPresentationController::publish_idle(double now) {
  if (!valid_pose(idle_sequence_, idle_body_)) {
    counters_.error = LocalWeaponPresentationError::unsupported_pose; return;
  }
  if (!server_visual_ || server_visual_->sequence != idle_sequence_ || server_visual_->body != idle_body_)
    server_visual_ = LocalWeaponVisual{idle_sequence_,idle_body_,++restart_identity_,now,
      LocalWeaponAnimationSource::clientdata_state,LocalWeaponActionStatus::retained_server_event};
}
void LocalWeaponPresentationController::finish_pending(LocalWeaponActionStatus status) noexcept {
  if (!action_ || action_->status != LocalWeaponActionStatus::predicted_pending) return;
  action_->status = status; action_->visual.status = status; counters_.status = status;
  if (status == LocalWeaponActionStatus::server_confirmed || status == LocalWeaponActionStatus::server_corrected) {
    ++counters_.actions_confirmed;
    if (status == LocalWeaponActionStatus::server_corrected) ++counters_.actions_corrected;
    switch (action_->identity.kind) {
    case LocalWeaponAction::primary_fire: ++counters_.primary_fire_confirmed; ++counters_.recoil_confirmed; break;
    case LocalWeaponAction::reload: ++counters_.reload_confirmed; break;
    case LocalWeaponAction::melee_swing: ++counters_.melee_swing_confirmed; break;
    }
  } else {
    ++counters_.actions_rejected;
    audio_.cancel();
    if (action_->identity.kind == LocalWeaponAction::primary_fire) {
      ++counters_.recoil_rejected; recoil_active_ = false;
    }
    action_.reset();
  }
}
void LocalWeaponPresentationController::start(LocalWeaponAction kind, std::uint32_t command,
    double at, std::uint32_t seq, std::uint8_t body, double deadline) {
  if (!valid_pose(seq,body)) { counters_.error = LocalWeaponPresentationError::unsupported_pose; return; }
  action_ = ActionState{{generation_,*weapon_id_,*model_index_,model_->resource_revision,
      command,kind,at,canonical_revision_},LocalWeaponActionStatus::predicted_pending,
      client_source_ ? client_source_->record_ordinal : model_bound_record_ordinal_,
      clip_,reserve_,deadline,{seq,body,++restart_identity_,at,
      LocalWeaponAnimationSource::provisional_command,LocalWeaponActionStatus::predicted_pending}};
  ++counters_.actions_started; counters_.status = LocalWeaponActionStatus::predicted_pending;
  audio_.action(action_->identity);
  if (kind == LocalWeaponAction::primary_fire) {
    ++counters_.primary_fire_starts; ++counters_.recoil_starts;
    recoil_started_at_ = at; recoil_active_ = !server_punch_nonzero_; last_primary_fire_at_ = at;
  } else if (kind == LocalWeaponAction::reload) ++counters_.reload_starts;
  else { ++counters_.melee_swing_starts; last_melee_swing_at_ = at; crowbar_cycle_ = (crowbar_cycle_ + 1U) % 3U; }
}
void LocalWeaponPresentationController::observe(const client::RuntimeClientObservationState& state, double now) {
  if (!finite_time(now)) return;
  if (state.generation == 0U || state.generation < generation_ ||
      (state.generation == generation_ && state.publication_revision < canonical_revision_)) {
    counters_.error = LocalWeaponPresentationError::stale_observation; return;
  }
  if (!advance_time(now)) return;
  if (generation_ != state.generation) {
    auto model = std::move(model_); auto audio = std::move(audio_);
    *this = LocalWeaponPresentationController{};
    audio_ = std::move(audio); audio_.bind(model);
    generation_ = state.generation; last_time_ = now; model_ = std::move(model);
  }
  const auto& hud = state.weapon_hud;
  if (life_epoch_ != state.lifecycle.life_epoch || life_deaths_ != state.lifecycle.deaths) {
    audio_.cancel();
    life_epoch_ = state.lifecycle.life_epoch;
    life_deaths_ = state.lifecycle.deaths;
    finish_pending(LocalWeaponActionStatus::server_rejected);
    action_.reset(); server_visual_.reset(); recoil_active_ = false;
    client_animation_sequence_.reset(); client_source_.reset(); hud_source_.reset();
    weapon_id_.reset(); model_index_.reset();
    event_history_.fill(std::nullopt);
    life_release_barrier_ = life_deaths_ != 0U;
  }
  // Conflicting exact duplicates fail BEFORE any canonical-derived publication.
  if (hud.animation_source && hud.animation_sequence && hud.animation_body)
    for (const auto& event : event_history_)
      if (event && event->source == *hud.animation_source &&
          (event->sequence != *hud.animation_sequence || event->body != *hud.animation_body)) {
        ++counters_.conflicting_duplicates;
        counters_.error = LocalWeaponPresentationError::conflicting_event; return;
      }
  const auto active = hud.active_weapon_id;
  const auto index = state.receiving_client ? state.receiving_client->viewmodel_index : std::nullopt;
  if (weapon_id_ != active || model_index_ != index) {
    audio_.cancel();
    finish_pending(LocalWeaponActionStatus::server_rejected);
    action_.reset(); server_visual_.reset(); client_source_.reset(); hud_source_.reset();
    clip_.reset(); reserve_.reset(); in_reload_.reset(); next_primary_attack_.reset();
    next_client_attack_.reset(); next_reload_.reset();
    client_animation_sequence_.reset(); recoil_active_ = false; server_punch_nonzero_ = false;
    idle_sequence_ = 0U; idle_body_ = 0U; weapon_id_ = active; model_index_ = index;
    model_bound_record_ordinal_ = state.client_metadata.source ? state.client_metadata.source->record_ordinal : 0U;
  }
  canonical_revision_ = state.publication_revision;
  canonical_dead_ = state.lifecycle.dead() ||
      (state.receiving_client && state.receiving_client->health &&
       *state.receiving_client->health <= 0.0);
  if (state.lifecycle.last_death_source &&
      (!hud.active_source || hud.active_source->record_ordinal <=
          state.lifecycle.last_death_source->record_ordinal))
    canonical_dead_ = true; // wait for server-owned new-life weapon binding
  if (canonical_dead_) finish_pending(LocalWeaponActionStatus::server_rejected);
  if (!supported()) { counters_.error = LocalWeaponPresentationError::unsupported_model; return; }
  counters_.error = LocalWeaponPresentationError::none;
  const auto old_clip = clip_; const auto old_reserve = reserve_;
  const auto old_reload = in_reload_; const auto old_timer = next_primary_attack_;
  const auto old_client_attack = next_client_attack_;
  const auto old_next_reload = next_reload_;
  bool fresh = false;
  std::optional<std::uint32_t> changed_client_animation;
  const bool fresh_client = state.client_metadata.generation == generation_ &&
    state.client_metadata.freshness == client::RuntimeObservationFreshness::observed_in_record &&
    state.client_metadata.source && newer(*state.client_metadata.source,client_source_);
  if (fresh_client) {
    fresh = true; client_source_ = state.client_metadata.source;
    timer_observed_at_ = now;
    const auto slot = std::find_if(state.weapon_slots.begin(),state.weapon_slots.end(),
      [active](const auto& item) { return item.weapon_id == active; });
    if (slot != state.weapon_slots.end()) {
      clip_ = slot->clip; in_reload_ = slot->in_reload; next_primary_attack_ = slot->next_primary_attack;
      next_reload_ = slot->next_reload;
    }
    if (state.receiving_client) {
      const auto& receiving = *state.receiving_client;
      next_client_attack_ = receiving.next_weapon_attack;
      const auto& punch = receiving.punch_angle;
      server_punch_nonzero_ = punch.complete() && std::isfinite(*punch.x) && std::isfinite(*punch.y) &&
        std::isfinite(*punch.z) && std::hypot(*punch.x,*punch.y,*punch.z) > 0.001;
      // Canonical nonzero punch REPLACES local punch for this action; no resurrection.
      if (server_punch_nonzero_) recoil_active_ = false;
      if (receiving.weapon_animation && receiving.weapon_animation != client_animation_sequence_) {
        if (client_animation_sequence_) changed_client_animation = receiving.weapon_animation;
        client_animation_sequence_ = receiving.weapon_animation;
        const auto seq = *client_animation_sequence_;
        if (valid_pose(seq,0U)) {
          if (idle(*active,seq)) idle_sequence_ = seq;
          // State is not a repeat event. Unchanged idle cannot erase a local action.
          if (!action_ && (!server_visual_ || server_visual_->sequence != seq))
            server_visual_ = LocalWeaponVisual{seq,0U,++restart_identity_,now,
              LocalWeaponAnimationSource::clientdata_state,LocalWeaponActionStatus::retained_server_event};
        } else counters_.error = LocalWeaponPresentationError::unsupported_pose;
      }
    }
  }
  // Independent CurWeapon/AmmoX records, not retained values, can confirm.
  if (hud.last_message_source && newer(*hud.last_message_source,hud_source_)) {
    hud_source_ = hud.last_message_source; fresh = true;
    if (!fresh_client && *active < hud.clips.size() && hud.clips[*active]) clip_ = hud.clips[*active];
  }
  const auto type = std::find_if(hud.catalogue.begin(),hud.catalogue.end(),
    [active](const auto& item) { return item.id == *active; });
  if (fresh && type != hud.catalogue.end() && type->primary_ammo_type >= 0 &&
      static_cast<std::size_t>(type->primary_ammo_type) < hud.reserve_ammo.size())
    reserve_ = hud.reserve_ammo[static_cast<std::size_t>(type->primary_ammo_type)];
  const bool post_action = action_ && canonical_revision_ > action_->identity.canonical_pre_revision &&
    ((fresh_client && client_source_->record_ordinal > action_->pre_record_ordinal) ||
     (hud_source_ && hud_source_->record_ordinal > action_->pre_record_ordinal));
  if (fresh && post_action) {
    const auto kind = action_->identity.kind;
    if (kind == LocalWeaponAction::primary_fire && old_clip && clip_ && action_->clip_before &&
        *clip_ == *action_->clip_before - 1 && *clip_ < *old_clip) {
      action_->visual.sequence = *clip_ == 0 ? 4U : 3U;
      action_->visual.source = LocalWeaponAnimationSource::confirmed_weapon_state;
      finish_pending(LocalWeaponActionStatus::server_confirmed);
    } else if (kind == LocalWeaponAction::reload &&
      ((in_reload_ && *in_reload_ && old_reload && !*old_reload) ||
       (next_client_attack_ && old_client_attack && *next_client_attack_ > *old_client_attack + 0.01) ||
       (next_reload_ && old_next_reload && *next_reload_ > *old_next_reload + 0.01) ||
       (clip_ && old_clip && *clip_ > *old_clip && reserve_ && old_reserve && *reserve_ < *old_reserve))) {
      action_->visual.source = LocalWeaponAnimationSource::confirmed_weapon_state;
      finish_pending(LocalWeaponActionStatus::server_confirmed);
    } else if (kind == LocalWeaponAction::melee_swing && old_timer && next_primary_attack_ &&
               *next_primary_attack_ > *old_timer + 0.01) {
      action_->visual.source = LocalWeaponAnimationSource::confirmed_weapon_state;
      finish_pending(LocalWeaponActionStatus::server_confirmed);
    }
  }
  // A changed current-record clientdata state has higher precedence than a
  // derived/predicted pose, but is never an automatic repeated restart event.
  if (changed_client_animation && action_ && fresh_client &&
      client_source_->record_ordinal > action_->pre_record_ordinal) {
    const auto seq = *changed_client_animation;
    if (!valid_pose(seq, action_->visual.body)) {
      counters_.error = LocalWeaponPresentationError::unsupported_pose;
    } else if (seq == action_->visual.sequence) {
      action_->visual.source = LocalWeaponAnimationSource::clientdata_state;
    } else {
      if (idle(*active, seq)) {
        finish_pending(LocalWeaponActionStatus::server_rejected);
        action_.reset();
        idle_sequence_ = seq;
        publish_idle(now);
      } else {
        ++counters_.actions_corrected;
        action_->visual.sequence = seq;
        action_->visual.restart_identity = ++restart_identity_;
        action_->visual.started_at_seconds = now;
        action_->visual.source = LocalWeaponAnimationSource::clientdata_state;
      }
    }
  }
  // Exact service event wins LAST over all lower-precedence transitions.
  if (hud.animation_source && hud.animation_sequence && hud.animation_body &&
      hud.animation_source->record_ordinal >= model_bound_record_ordinal_) {
    const SeenEvent event{*hud.animation_source,*hud.animation_sequence,*hud.animation_body};
    const bool seen = std::any_of(event_history_.begin(),event_history_.end(),
      [&](const auto& item) { return item && item->source == event.source; });
    if (!seen) {
      if (!valid_pose(event.sequence,event.body)) counters_.error = LocalWeaponPresentationError::unsupported_pose;
      else {
        event_history_[event_history_next_++ % event_history_.size()] = event;
        const bool related = action_ && event.source.record_ordinal > action_->pre_record_ordinal &&
          now <= action_->deadline;
        if (related) {
          const bool same = action_->visual.sequence == event.sequence && action_->visual.body == event.body;
          if (!same) {
            const bool different_sequence = action_->visual.sequence != event.sequence;
            if (action_->status != LocalWeaponActionStatus::predicted_pending) ++counters_.actions_corrected;
            action_->visual.sequence = event.sequence; action_->visual.body = event.body;
            if (different_sequence) {
              action_->visual.restart_identity = ++restart_identity_;
              action_->visual.started_at_seconds = now;
            }
            if (action_->identity.kind == LocalWeaponAction::primary_fire &&
                event.sequence != 3U && event.sequence != 4U) recoil_active_ = false;
          }
          finish_pending(same ? LocalWeaponActionStatus::server_confirmed : LocalWeaponActionStatus::server_corrected);
          if (!same) { action_->status = LocalWeaponActionStatus::server_corrected; action_->visual.status = action_->status; }
          action_->visual.source = LocalWeaponAnimationSource::service_event;
          if (idle(*active,event.sequence)) { server_visual_ = action_->visual; action_.reset(); }
        } else server_visual_ = LocalWeaponVisual{event.sequence,event.body,++restart_identity_,now,
          LocalWeaponAnimationSource::service_event,LocalWeaponActionStatus::retained_server_event};
        if (idle(*active,event.sequence)) { idle_sequence_ = event.sequence; idle_body_ = event.body; }
      }
    }
  }
  if (!server_visual_) publish_idle(now);
}
void LocalWeaponPresentationController::submit(const LocalWeaponSubmittedCommand& command, double now) {
  if (!finite_time(command.sample_end_seconds) || command.generation != generation_ ||
      command.sequence == 0U || !advance_time(now)) return;
  if (command.sequence <= last_command_sequence_) {
    const auto& previous = command_history_[command.sequence % command_history_.size()];
    if (previous && previous->sequence == command.sequence && *previous != command) {
      counters_.error = LocalWeaponPresentationError::conflicting_command; ++counters_.conflicting_duplicates;
    } else ++counters_.duplicate_submissions;
    return;
  }
  command_history_[command.sequence % command_history_.size()] = command;
  last_command_sequence_ = command.sequence;
  if (life_release_barrier_) {
    constexpr auto action_buttons = goldsrc::kReferenceGoldSrcButtonAttack |
        goldsrc::kReferenceGoldSrcButtonJump | goldsrc::kReferenceGoldSrcButtonDuck |
        goldsrc::kReferenceGoldSrcButtonUse | goldsrc::kReferenceGoldSrcButtonReload;
    if (!canonical_dead_ && (command.buttons & action_buttons) == 0U)
      life_release_barrier_ = false;
    return; // presentation only; immutable committed wire command is unchanged
  }
  const bool reload = (command.buttons & goldsrc::kReferenceGoldSrcButtonReload) != 0U;
  const bool reload_edge = reload && !reload_button_held_; reload_button_held_ = reload;
  if (!supported()) return;
  const auto at = std::clamp(command.sample_end_seconds,std::max(0.0,now - 0.25),now);
  const bool attack = (command.buttons & goldsrc::kReferenceGoldSrcButtonAttack) != 0U;
  const bool pending = action_ && action_->status == LocalWeaponActionStatus::predicted_pending;
  const bool reload_visual = action_ && action_->identity.kind == LocalWeaponAction::reload &&
    now < action_->deadline && (!in_reload_ || *in_reload_ ||
      (action_->clip_before && clip_ && *clip_ <= *action_->clip_before));
  // public HL1 CLIENT_WEAPONS uses decrement timers. Age only for visual
  // eligibility. No aged timer is published to canonical gameplay state.
  const auto timer_ready = [&](const std::optional<double> timer) {
    return !timer || (std::isfinite(*timer) &&
      *timer <= now - timer_observed_at_ + 0.001);
  };
  const bool ready = timer_ready(next_primary_attack_) && timer_ready(next_client_attack_);
  if (*weapon_id_ == 2U) {
    if (attack && clip_ && *clip_ > 0 && *clip_ <= 17 && (!in_reload_ || !*in_reload_) &&
        !pending && !reload_visual && at - last_primary_fire_at_ >= 0.3 && ready)
      start(LocalWeaponAction::primary_fire,command.sequence,at,*clip_ == 1 ? 4U : 3U,2U,at + 0.75);
    else if (reload_edge && clip_ && *clip_ >= 0 && *clip_ < 17 && reserve_ && *reserve_ > 0U &&
        (!in_reload_ || !*in_reload_) && !pending && !reload_visual && ready)
      start(LocalWeaponAction::reload,command.sequence,at,*clip_ == 0 ? 5U : 6U,0U,at + 2.5);
  } else if (*weapon_id_ == 1U && attack && !pending && at - last_melee_swing_at_ >= 0.5 && ready)
    start(LocalWeaponAction::melee_swing,command.sequence,at,misses[crowbar_cycle_],1U,at + 0.8);
}
bool LocalWeaponPresentationController::resolve_crowbar_world_hit(
    const LocalWeaponActionIdentity& identity, double now) noexcept {
  if (!action_ || action_->identity != identity ||
      identity.kind != LocalWeaponAction::melee_swing ||
      !finite_time(now) || now > action_->deadline ||
      action_->visual.source == LocalWeaponAnimationSource::service_event ||
      action_->visual.source == LocalWeaponAnimationSource::clientdata_state)
    return false;
  for (std::size_t i=0;i<misses.size();++i) {
    if (action_->visual.sequence != misses[i]) continue;
    if (!valid_pose(hits[i],1U)) return false;
    action_->visual.sequence=hits[i];
    action_->visual.restart_identity=++restart_identity_;
    action_->visual.started_at_seconds=identity.started_at_seconds;
    return true;
  }
  return false;
}
void LocalWeaponPresentationController::cancel_uncommitted() noexcept {
  audio_.cancel();
  finish_pending(LocalWeaponActionStatus::server_rejected); reload_button_held_ = false;
}
LocalWeaponPresentationSnapshot LocalWeaponPresentationController::sample(double now) noexcept {
  if (!advance_time(now)) return counters_;
  if (action_ && action_->status == LocalWeaponActionStatus::predicted_pending && now >= action_->deadline)
    finish_pending(LocalWeaponActionStatus::timed_out);
  auto result = counters_;
  if (action_) {
    result.action = action_->identity.kind; result.identity = action_->identity;
    result.status = action_->status; result.visual = action_->visual;
  } else result.visual = server_visual_;
  audio_.update(result,model_,now,!canonical_dead_ && supported());
  if (result.visual && valid_pose(result.visual->sequence,result.visual->body)) {
    const auto& data = model_->sequences[result.visual->sequence];
    const auto frame = std::max(0.0,now - result.visual->started_at_seconds) * data.fps;
    const auto terminal = static_cast<double>(data.frame_count - 1U);
    result.animation_completed = !data.looping && frame >= terminal;
    result.frame_coordinate = data.looping && terminal > 0.0 ? std::fmod(frame,terminal) : std::min(frame,terminal);
    if (result.animation_completed && !idle(*weapon_id_,result.visual->sequence)) {
      // The old retained idle may predate this action. Reusing its restart
      // token produced frame zero on this sample, then another frame zero on
      // every subsequent sample while action_ remained at its terminal pose.
      // A completed action crosses to exactly one new idle timeline; a newer
      // server-owned idle event keeps its own occurrence identity instead.
      const bool newer_server_idle = server_visual_ &&
          idle(*weapon_id_,server_visual_->sequence) &&
          server_visual_->started_at_seconds >= result.visual->started_at_seconds;
      action_.reset();
      if (!newer_server_idle) server_visual_.reset();
      publish_idle(now);
      result.action.reset(); result.identity.reset();
      result.visual = server_visual_; result.frame_coordinate = 0.0;
    }
  }
  if (recoil_active_ && !server_punch_nonzero_) {
    const auto elapsed = std::max(0.0,now - recoil_started_at_);
    // Derived V_DropPunchAngle recurrence on fixed 20ms physical ticks plus
    // residual fraction. V_PunchAxis sets -2, not an accumulated/random impulse.
    // This versioned timing adaptation is independent of render call count;
    // stock variable-frame-step rounding is NOT claimed identical.
    const auto ticks = std::floor(elapsed / 0.02);
    const auto fraction = elapsed - ticks * 0.02;
    const auto magnitude = std::max(0.0,22.0 * std::pow(0.99,ticks) * (1.0 - fraction * 0.5) - 20.0);
    result.local_punch_pitch_degrees = -magnitude;
    counters_.maximum_visual_recoil = std::max(counters_.maximum_visual_recoil,magnitude);
    result.maximum_visual_recoil = counters_.maximum_visual_recoil;
    if (magnitude == 0.0) recoil_active_ = false;
  }
  return result;
}
} // namespace hlclient::games::halflife

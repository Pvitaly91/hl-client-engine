#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/weapon_diagnostics.hpp>
#include <hlclient/goldsrc/reference_prediction_command.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
  return weapon == 2U ? seq <= 2U : weapon == 4U ? seq <= 1U :
    weapon == 3U ? seq==0U || seq==6U || seq==7U :
    weapon == 6U ? seq<=3U :
    weapon == 8U ? seq==0U || seq==1U || seq==8U || seq==9U :
    weapon == 11U || weapon == 15U ? seq<=2U : weapon == 13U ? seq<=1U || seq==4U : weapon == 12U || weapon == 14U ? seq<=1U :
    weapon == 7U ? seq == 0U || seq == 8U || seq == 9U : seq == 0U;
}
bool bullet_fire(LocalWeaponAction action) noexcept {
  return action == LocalWeaponAction::primary_fire || action == LocalWeaponAction::secondary_fire;
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
      model_->resource_revision == 0U) return false;
  if(*weapon_id_==14U && satchel_selected_) return satchel_sequences(*model_);
  if(model_index_ != model_->model_index) return false;
  const bool glock = *weapon_id_ == 2U && model_->resource_name == "models/v_9mmhandgun.mdl" &&
    model_->sequences.size() >= 10U;
  const bool crowbar = *weapon_id_ == 1U && model_->resource_name == "models/v_crowbar.mdl" &&
    model_->sequences.size() >= 9U;
  if (*weapon_id_==4U && mp5_selected_) return mp5_sequences(*model_);
  if (*weapon_id_==7U && shotgun_selected_) return shotgun_sequences(*model_);
  if (*weapon_id_==3U && python_selected_) return maximum_clients_>0U && python_sequences(*model_);
  if (*weapon_id_==6U && crossbow_selected_) return maximum_clients_>0U && crossbow_sequences(*model_);
  if (*weapon_id_==8U && rpg_selected_) return rpg_sequences(*model_);
  if (*weapon_id_==11U && hornet_selected_) return maximum_clients_>0U && hornet_sequences(*model_);
  if (*weapon_id_==12U && hand_grenade_selected_) return hand_grenade_sequences(*model_);
  if (*weapon_id_==13U && tripmine_selected_) return tripmine_sequences(*model_);
  if (*weapon_id_==15U && snark_selected_) return snark_sequences(*model_);
  if (!glock && !crowbar) return false;
  for (auto seq : glock ? std::array{0U,3U,4U,5U,6U,0U,0U} :
                          std::array{0U,4U,5U,7U,3U,6U,8U})
    if (!valid_pose(seq, glock && (seq == 3U || seq == 4U) ? 2U :
                        !glock && seq != 0U ? 1U : 0U)) return false;
  return true;
}
void LocalWeaponPresentationController::start_crossbow(std::uint32_t command,double at,
    int post_clip,int reserve,bool sniper) {
  const auto previous=action_ ? std::optional{action_->visual} : server_visual_;
  const auto role=crossbow_fire_sequence(post_clip,reserve);
  start(LocalWeaponAction::primary_fire,command,at,role.value_or(previous ? previous->sequence : idle_sequence_),
      role ? 1U : previous ? previous->body : idle_body_,at+2.25,false,!sniper);
  if(!action_) return;
  action_->crossbow_sniper=sniper;
  diagnostic_action(core::MultiplayerActionPhase::accepted);
  if(sniper) recoil_active_=false; // EV_FireCrossbow2 has no V_PunchAxis
  if(!role && previous) action_->visual=*previous; // no event animation when clip=reserve=0
}
void LocalWeaponPresentationController::bind_model(std::optional<LocalWeaponModelMetadata> model) {
  if(model && satchel_sequences(*model)) {
    const auto role=*satchel_model_role(model->resource_name);
    satchel_models_[role ? 1U : 0U]=*model;
    if(weapon_id_==14U && satchel_selected_ && model->generation==generation_) {
      (void)select_satchel_model(satchel_.charge_ready ? *satchel_.charge_ready!=0 : role);
      return; // One weapon, two poses: binding a server echo is not a new action.
    }
  }
  if (model_ == model) return;
  server_reload_predecessor_deadline_.reset();
  finish_pending(LocalWeaponActionStatus::server_rejected);
  action_.reset(); server_visual_.reset(); recoil_active_ = false;
  model_ = std::move(model); idle_sequence_ = 0U; idle_body_ = 0U;
  pending_fire_.fill(std::nullopt);provisional_rounds_=0;
  pending_launch_.reset();provisional_grenades_=0;
  clear_hornet_state();
  clear_hand_grenade_state();
  clear_satchel_state();
  clear_tripmine_state();
  clear_snark_state();
  snark_model_rebind_pending_=model_ && model_->resource_name=="models/v_squeak.mdl";
  audio_.bind(model_);
}
void LocalWeaponPresentationController::publish_idle(double now) {
  server_reload_predecessor_deadline_.reset();
  if (!valid_pose(idle_sequence_, idle_body_)) {
    counters_.error = LocalWeaponPresentationError::unsupported_pose; return;
  }
  if (!server_visual_ || server_visual_->sequence != idle_sequence_ || server_visual_->body != idle_body_)
    // Returning to an idle pose is not a new action. The derived idle timeline
    // stays at frame zero until the launch's pinned +5s idle boundary; a new
    // exact server animation event still replaces this lower-precedence pose.
    server_visual_ = LocalWeaponVisual{idle_sequence_,idle_body_,++restart_identity_,
      mp5_selected_ ? std::max(now,launch_idle_not_before_) : now,
      LocalWeaponAnimationSource::clientdata_state,LocalWeaponActionStatus::retained_server_event};
}
void LocalWeaponPresentationController::observe_shotgun_reload(
    std::optional<std::int32_t> previous_stage,
    std::optional<std::int32_t> previous_clip, double now) {
  if(!special_reload_state_) return;
  const auto stage=*special_reload_state_;
  if(stage<0 || stage>2) { counters_.error=LocalWeaponPresentationError::unsupported_pose;return; }
  const bool prior_active=previous_stage && *previous_stage>0 && *previous_stage<=2;
  const bool transferred=previous_clip && clip_ && *clip_>*previous_clip;
  std::optional<std::uint32_t> sequence;
  if(stage==2 && (!previous_stage || *previous_stage!=2 || transferred)) sequence=3U;
  else if(stage==1 && !prior_active && !transferred) sequence=5U;
  else if(stage==0 && prior_active) {
    // Numeric special-reload reset does not mean completion by itself:
    // firing/weapon changes can cancel it. Only completed inventory evidence
    // derives the pump pose; exact server animation still wins below.
    if((clip_ && *clip_>=8) || (reserve_ && *reserve_==0U)) sequence=4U;
    else if(!action_ || action_->identity.kind==LocalWeaponAction::reload) {
      action_.reset();server_visual_.reset();publish_idle(now);return;
    }
  }
  if(!sequence || !valid_pose(*sequence,0U)) return;
  if(stage>0 && action_ && action_->identity.kind!=LocalWeaponAction::reload) {
    finish_pending(LocalWeaponActionStatus::server_rejected);action_.reset();
  }
  if(action_ && action_->identity.kind==LocalWeaponAction::reload) {
    // A fresh 1 -> 2 stage transition is a NEW insert occurrence even though
    // the role/sequence equals the previous insert. Repeated stage2 records
    // never reach this branch, so they cannot restart it each RX/frame.
    if(action_->visual.sequence!=*sequence || stage==2 || transferred) {
      action_->visual.sequence=*sequence;action_->visual.body=0U;
      action_->visual.restart_identity=++restart_identity_;
      action_->visual.started_at_seconds=now;
    }
    action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
    action_->deadline=now+1.5;
  } else {
    // One confirmed stage occurrence, never an extra reload command or ammo
    // transfer. Imported Studio metadata decides animation progress.
    server_visual_=LocalWeaponVisual{*sequence,0U,++restart_identity_,now,
        LocalWeaponAnimationSource::confirmed_weapon_state,
        LocalWeaponActionStatus::retained_server_event};
  }
}
void LocalWeaponPresentationController::observe_conventional_reload(std::optional<bool> previous_reload,
    bool client_pose_published,std::uint32_t sequence,double now) {
  const auto maximum_clip=mp5_selected_ ? 50 : crossbow_selected_ ? 5 : 1;
  if(in_reload_!=true || previous_reload!=false || !clip_ || *clip_<0 ||
      *clip_>=maximum_clip || !valid_pose(sequence,0U)) return;
  // Stock auto-reload and held-R retries have no new local command edge. A
  // fresh m_fInReload occurrence authorizes the conventional reload pose
  // even when clientdata weaponanim retains the previous reload's role.
  // This is presentation of a server transition, not a local Reload/ammo edit.
  if(action_ && action_->identity.kind==LocalWeaponAction::reload) {
    finish_pending(LocalWeaponActionStatus::server_confirmed);
    action_->visual.sequence=sequence;action_->visual.body=0U;
    action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
    return; // Keep the provisional command's restart/time and confirmation count.
  }
  server_reload_predecessor_deadline_.reset();
  if(action_ && (bullet_fire(action_->identity.kind) ||
      (mp5_selected_ && action_->identity.kind==LocalWeaponAction::secondary_launch)))
    server_reload_predecessor_deadline_=action_->deadline;
  else {
    for(const auto& prior:pending_fire_) if(prior &&
        (!server_reload_predecessor_deadline_ || prior->deadline>*server_reload_predecessor_deadline_))
      server_reload_predecessor_deadline_=prior->deadline;
    if(pending_launch_ && (!server_reload_predecessor_deadline_ ||
        pending_launch_->deadline>*server_reload_predecessor_deadline_))
      server_reload_predecessor_deadline_=pending_launch_->deadline;
  }
  if((mp5_selected_ || crossbow_selected_) && action_ && bullet_fire(action_->identity.kind))
    retain_fire_predecessor();
  else if(mp5_selected_ && action_ && action_->identity.kind==LocalWeaponAction::secondary_launch)
    pending_launch_=action_;
  else finish_pending(LocalWeaponActionStatus::server_rejected);
  action_.reset();recoil_active_=false;
  if(client_pose_published && server_visual_ && server_visual_->sequence==sequence && server_visual_->body==0U)
    server_visual_->source=LocalWeaponAnimationSource::confirmed_weapon_state;
  else server_visual_=LocalWeaponVisual{sequence,0U,++restart_identity_,now,
      LocalWeaponAnimationSource::confirmed_weapon_state,LocalWeaponActionStatus::retained_server_event};
}
void LocalWeaponPresentationController::retain_fire_predecessor() noexcept {
  auto& previous=pending_fire_[next_pending_fire_++%pending_fire_.size()];
  if(previous && previous->status==LocalWeaponActionStatus::predicted_pending) {
    ++counters_.actions_rejected;if(!previous->crossbow_sniper)++counters_.recoil_rejected;
    provisional_rounds_-=std::min(provisional_rounds_,static_cast<std::size_t>(previous->rounds_reserved));
  }
  previous=action_;
}
bool LocalWeaponPresentationController::reload_predecessor_active(double now) const noexcept {
  if(!mp5_selected_ && !crossbow_selected_) return false;
  if(action_) return action_->identity.kind==LocalWeaponAction::reload &&
      action_->reload_predecessor_deadline && now<=*action_->reload_predecessor_deadline &&
      (action_->status==LocalWeaponActionStatus::predicted_pending || in_reload_==true ||
       (next_client_attack_ && std::isfinite(*next_client_attack_) &&
        *next_client_attack_>std::max(0.0,now-timer_observed_at_)+.001));
  if(in_reload_!=true || !server_visual_ ||
      server_visual_->sequence!=(mp5_selected_ ? 3U : 7U)) return false;
  return (server_reload_predecessor_deadline_ && now<=*server_reload_predecessor_deadline_) ||
    std::any_of(pending_fire_.begin(),pending_fire_.end(),[now](const auto& prior) {
      return prior && now<=prior->deadline;
    }) || (pending_launch_ && now<=pending_launch_->deadline);
}
void LocalWeaponPresentationController::diagnostic_rejection(const LocalWeaponSubmittedCommand& command,std::string_view reason) const noexcept {
  if(!diagnostic_observer_ || !diagnostic_observer_->weapon_diagnostics_enabled() || !weapon_id_ ||
      *weapon_id_==9 || *weapon_id_==10 || !(command.buttons&(1U|(1U<<11)|(1U<<13))))return;
  WeaponActionDiagnosticObservation o;o.identity={generation_,*weapon_id_,model_ ? model_->model_index : 0,
      model_ ? model_->resource_revision : 0,command.sequence,LocalWeaponAction::primary_fire,command.sample_end_seconds,canonical_revision_};
  o.mode=(command.buttons&(1U<<11)) ? WeaponDiagnosticMode::secondary : (command.buttons&1) ? WeaponDiagnosticMode::primary : WeaponDiagnosticMode::reload;
  if(const auto* capability=weapon_diagnostic_capability(*weapon_id_))o.kind=capability->modes[static_cast<std::size_t>(o.mode)];
  o.command=command;o.phase=core::MultiplayerActionPhase::rejected;o.reason=reason;
  o.clip=clip_;if(reserve_)o.reserve=*reserve_;o.reloading=in_reload_;
  o.next_attack=o.mode==WeaponDiagnosticMode::secondary ? next_secondary_attack_ : next_primary_attack_;
  diagnostic_observer_->weapon_action_observed(o);
}
void LocalWeaponPresentationController::diagnostic_action(core::MultiplayerActionPhase phase) const noexcept {
  if(!diagnostic_observer_ || !diagnostic_observer_->weapon_diagnostics_enabled() || !action_)return;
  WeaponActionDiagnosticObservation observation;
  observation.identity=action_->identity;observation.phase=phase;observation.status=action_->status;
  const auto kind=action_->identity.kind;
  observation.mode=kind==LocalWeaponAction::reload ? WeaponDiagnosticMode::reload :
      kind==LocalWeaponAction::secondary_fire || kind==LocalWeaponAction::secondary_launch || action_->glock_secondary ?
      WeaponDiagnosticMode::secondary : WeaponDiagnosticMode::primary;
  if(const auto* capability=weapon_diagnostic_capability(action_->identity.weapon_id))
    observation.kind=capability->modes[static_cast<std::size_t>(observation.mode)];
  if(kind==LocalWeaponAction::prime) {observation.kind=core::MultiplayerActionKind::throw_object;
    if(phase==core::MultiplayerActionPhase::accepted)observation.phase=core::MultiplayerActionPhase::begin;}
  else if(kind==LocalWeaponAction::throw_release || kind==LocalWeaponAction::deploy_charge)
    observation.kind=tripmine_selected_ ? core::MultiplayerActionKind::place : core::MultiplayerActionKind::throw_object;
  else if(kind==LocalWeaponAction::remote_detonate)observation.kind=core::MultiplayerActionKind::detonate;
  if(hand_grenade_selected_ && kind==LocalWeaponAction::throw_release && hand_grenade_.prime_command)
    observation.related_command_sequence=hand_grenade_.prime_command;
  if(action_->crossbow_sniper && bullet_fire(kind))observation.kind=core::MultiplayerActionKind::fire;
  observation.clip=clip_;if(reserve_)observation.reserve=*reserve_;
  observation.reloading=in_reload_;observation.next_attack=observation.mode==WeaponDiagnosticMode::secondary ? next_secondary_attack_ : next_primary_attack_;
  const auto& command=command_history_[action_->identity.command_sequence%command_history_.size()];
  if(command && command->sequence==action_->identity.command_sequence && command->generation==action_->identity.generation) {
    observation.command=command;
    if((kind==LocalWeaponAction::deploy_charge || kind==LocalWeaponAction::remote_detonate) &&
        (command->buttons&(1U<<11)))observation.mode=WeaponDiagnosticMode::secondary;
  }
  // Exact current local visual emitters, not an assertion about server RNG/rays.
  if(shotgun_selected_ && bullet_fire(kind))observation.emitter_count=kind==LocalWeaponAction::secondary_fire ? 8U : 4U;
  diagnostic_observer_->weapon_action_observed(observation);
}
void LocalWeaponPresentationController::finish_pending(LocalWeaponActionStatus status) noexcept {
  if (!action_ || action_->status != LocalWeaponActionStatus::predicted_pending) return;
  if(action_->identity.weapon_id==11U) for(auto& receipt:hornet_receipts_)
    if(receipt && receipt->identity==action_->identity) {
      receipt->status=status;
      if(status!=LocalWeaponActionStatus::server_confirmed && status!=LocalWeaponActionStatus::server_corrected)
        receipt->reserved=false;
    }
  action_->status = status; action_->visual.status = status; counters_.status = status;
  diagnostic_action(status==LocalWeaponActionStatus::server_confirmed || status==LocalWeaponActionStatus::server_corrected ?
      core::MultiplayerActionPhase::server_observed : core::MultiplayerActionPhase::cancel);
  if (status == LocalWeaponActionStatus::server_confirmed || status == LocalWeaponActionStatus::server_corrected) {
    ++counters_.actions_confirmed;
    if (status == LocalWeaponActionStatus::server_corrected) ++counters_.actions_corrected;
    switch (action_->identity.kind) {
    case LocalWeaponAction::primary_fire: ++counters_.primary_fire_confirmed;
      if(!action_->crossbow_sniper) ++counters_.recoil_confirmed; break;
    case LocalWeaponAction::reload: ++counters_.reload_confirmed; break;
    case LocalWeaponAction::melee_swing: ++counters_.melee_swing_confirmed; break;
    case LocalWeaponAction::secondary_launch: ++counters_.secondary_launch_confirmed; ++counters_.recoil_confirmed; break;
    case LocalWeaponAction::secondary_fire: ++counters_.secondary_fire_confirmed; ++counters_.recoil_confirmed; break;
    case LocalWeaponAction::prime: case LocalWeaponAction::throw_release: break;
    }
  } else {
    ++counters_.actions_rejected;
    audio_.cancel();
    if (shotgun_selected_ && status == LocalWeaponActionStatus::server_rejected &&
        bullet_fire(action_->identity.kind)) {
      // A fresh server reload/idle can reject a provisional shot without an
      // ammo decrement. Retire only this action's reservation, not earlier
      // pending shots. Timeout and committed clip consumption already have
      // their own release paths; neither may be charged a second time here.
      provisional_rounds_ -= std::min(provisional_rounds_,
          static_cast<std::size_t>(action_->rounds_reserved));
    }
    if (bullet_fire(action_->identity.kind) ||
        action_->identity.kind == LocalWeaponAction::secondary_launch) {
      ++counters_.recoil_rejected; recoil_active_ = false;
    }
    action_.reset();
  }
}
void LocalWeaponPresentationController::start(LocalWeaponAction kind, std::uint32_t command,
    double at, std::uint32_t seq, std::uint8_t body, double deadline, bool glock_secondary,bool apply_recoil) {
  if (!valid_pose(seq,body)) { counters_.error = LocalWeaponPresentationError::unsupported_pose; return; }
  const bool conventional_reload=kind==LocalWeaponAction::reload && (mp5_selected_ || crossbow_selected_);
  const auto predecessor_deadline=conventional_reload && action_ &&
      (bullet_fire(action_->identity.kind) || (mp5_selected_ && action_->identity.kind==LocalWeaponAction::secondary_launch))
      ? std::optional{action_->deadline} : std::nullopt;
  if((mp5_selected_ || shotgun_selected_ || crossbow_selected_) &&
      (bullet_fire(kind) || kind==LocalWeaponAction::secondary_launch || conventional_reload) && action_ &&
      bullet_fire(action_->identity.kind) &&
      (action_->status==LocalWeaponActionStatus::predicted_pending ||
       (crossbow_selected_ && conventional_reload && !action_->scripted_event_confirmed))) {
    retain_fire_predecessor();
  }
  if(conventional_reload && mp5_selected_ && action_ &&
      action_->identity.kind==LocalWeaponAction::secondary_launch)
    pending_launch_=action_;
  action_ = ActionState{{generation_,*weapon_id_,*model_index_,model_->resource_revision,
      command,kind,at,canonical_revision_},LocalWeaponActionStatus::predicted_pending,
      client_source_ ? client_source_->record_ordinal : model_bound_record_ordinal_,
      clip_,reserve_,deadline,{seq,body,++restart_identity_,at,
      LocalWeaponAnimationSource::provisional_command,LocalWeaponActionStatus::predicted_pending},
      glock_secondary};
  action_->reload_predecessor_deadline=predecessor_deadline;
  if(kind==LocalWeaponAction::secondary_launch) {
    action_->reserve_before=secondary_reserve_;
    ++provisional_grenades_;
  }
  action_->rounds_reserved=shotgun_selected_ && kind==LocalWeaponAction::secondary_fire ? 2U : 1U;
  if((mp5_selected_ || shotgun_selected_) && bullet_fire(kind) && clip_) {
    action_->clip_before=*clip_-static_cast<std::int32_t>(provisional_rounds_);
    provisional_rounds_+=action_->rounds_reserved;
  }
  ++counters_.actions_started; counters_.status = LocalWeaponActionStatus::predicted_pending;
  // Some profiles finalize an identity/mode after start; their owning caller
  // emits once after that finalization, never before a provisional model ID.
  if(kind==LocalWeaponAction::reload || (!crossbow_selected_ && !satchel_selected_ && !tripmine_selected_ && !snark_selected_))
    diagnostic_action(core::MultiplayerActionPhase::accepted);
  audio_.action(action_->identity);
  if (bullet_fire(kind)) {
    if(kind==LocalWeaponAction::secondary_fire) ++counters_.secondary_fire_starts;
    else ++counters_.primary_fire_starts;
    if(apply_recoil) ++counters_.recoil_starts;
    recoil_started_at_ = at; recoil_active_ = apply_recoil && !server_punch_nonzero_; last_primary_fire_at_ = at;
    if (*weapon_id_==2U) glock_fire_cycle_seconds_=glock_secondary ?
        glock_secondary_cycle_seconds : glock_primary_cycle_seconds;
    if(shotgun_selected_) shotgun_fire_cycle_seconds_=kind==LocalWeaponAction::secondary_fire ?
        shotgun_double_cycle_seconds : shotgun_single_cycle_seconds;
    recoil_impulse_=hornet_selected_ ? hornet_visual_pitch(action_->identity) : rpg_selected_ ? -5.0 : python_selected_ ? -10.0 : shotgun_selected_ ? kind==LocalWeaponAction::secondary_fire ? -10.0 : -5.0 : mp5_selected_ ?
        -2.0+static_cast<double>(firearm_variant(action_->identity)%4001U)/1000.0 : -2.0;
  } else if(kind==LocalWeaponAction::secondary_launch) {
    ++counters_.secondary_launch_starts;++counters_.recoil_starts;
    recoil_started_at_=at;recoil_active_=!server_punch_nonzero_;recoil_impulse_=-10.0;
    last_secondary_launch_at_=at;
    launch_idle_not_before_=at+5.0;
  } else if (kind == LocalWeaponAction::reload) ++counters_.reload_starts;
  else if(kind==LocalWeaponAction::melee_swing) { ++counters_.melee_swing_starts; last_melee_swing_at_ = at; crowbar_cycle_ = (crowbar_cycle_ + 1U) % 3U; }
}
void LocalWeaponPresentationController::clear_hand_grenade_state() noexcept {
  hand_grenade_={};hand_grenade_idle_timer_.reset();hand_grenade_ammo_source_.reset();
  hand_grenade_server_primed_=false;hand_grenade_prime_record_=0;hand_grenade_command_time_=0;
  hand_grenade_release_required_=false;
  hand_grenade_last_throw_reserve_.reset();
  hand_grenade_last_reservation_pending_=false;
}
void LocalWeaponPresentationController::submit_hand_grenade(
    const LocalWeaponSubmittedCommand& command,bool ready) {
  const double at=command.sample_end_seconds;
  if(at<hand_grenade_command_time_) {counters_.error=LocalWeaponPresentationError::invalid_time;return;}
  hand_grenade_command_time_=at;
  const bool attack=(command.buttons&goldsrc::kReferenceGoldSrcButtonAttack)!=0;
  const bool secondary=(command.buttons&goldsrc::kReferenceGoldSrcButtonAttack2)!=0;
  if(hand_grenade_release_required_) {
    if(!attack && !secondary) hand_grenade_release_required_=false;
    return;
  }
  auto& grenade=hand_grenade_;
  if(grenade.phase==HandGrenadePhase::throwing && grenade.throw_time && at+1e-8>=*grenade.throw_time+.5) {
    grenade.phase=HandGrenadePhase::post_throw;
    // Server still owns selection/inventory removal. Exhaustion only prevents
    // the local pose from resurrecting an empty grenade while that arrives.
    if(reserve_ && *reserve_<=grenade.reserved_ammo) {
      grenade.phase=HandGrenadePhase::retired;action_.reset();server_visual_.reset();return;
    }
    if(!attack && !secondary) {
      action_.reset();server_visual_=LocalWeaponVisual{7U,0U,++restart_identity_,at,
          LocalWeaponAnimationSource::provisional_command,LocalWeaponActionStatus::predicted_pending};
      grenade.phase=HandGrenadePhase::ready;
    }
  }
  if(grenade.phase==HandGrenadePhase::post_throw && !secondary) {
    if(!attack) {
      action_.reset();server_visual_=LocalWeaponVisual{7U,0U,++restart_identity_,at,
          LocalWeaponAnimationSource::provisional_command,LocalWeaponActionStatus::predicted_pending};
    }
    grenade.phase=HandGrenadePhase::ready;
  }
  if(grenade.phase==HandGrenadePhase::ready && attack && !secondary && ready &&
      clip_==-1 && reserve_ && *reserve_>grenade.reserved_ammo && *reserve_<=hand_grenade_maximum_ammo && in_reload_!=true) {
    grenade.phase=HandGrenadePhase::primed;grenade.prime_time=at;grenade.release_time.reset();
    grenade.throw_time.reset();grenade.frozen_throw.reset();grenade.prime_command=command.sequence;
    grenade.release_command=grenade.throw_command=0;++grenade.prime_count;
    hand_grenade_prime_record_=client_source_ ? client_source_->record_ordinal : model_bound_record_ordinal_;
    hand_grenade_server_primed_=false;
    start(LocalWeaponAction::prime,command.sequence,at,2U,0U,std::numeric_limits<double>::max(),false,false);
    return;
  }
  if(grenade.phase!=HandGrenadePhase::primed && grenade.phase!=HandGrenadePhase::release_pending) return;
  // Stock ItemPostFrame gives SecondaryAttack priority and does not call idle
  // while either attack remains down. R has no no-clip reload meaning.
  if(attack || secondary) return;
  if(!grenade.release_time) {
    grenade.release_time=at;grenade.release_command=command.sequence;
    grenade.phase=HandGrenadePhase::release_pending;
  }
  if(!grenade.prime_time || at+1e-8<*grenade.prime_time+.5) return;
  if(hand_grenade_idle_timer_ && *hand_grenade_idle_timer_>std::max(0.0,at-timer_observed_at_)+.001) return;
  if(!command.shot_context) {counters_.error=LocalWeaponPresentationError::unsupported_pose;return;}
  const auto context=hand_grenade_throw_context(*command.shot_context,at-*grenade.prime_time);
  if(!context) {counters_.error=LocalWeaponPresentationError::unsupported_pose;return;}
  grenade.frozen_throw=context;grenade.throw_time=at;grenade.throw_command=command.sequence;
  hand_grenade_last_throw_reserve_=reserve_;
  hand_grenade_last_reservation_pending_=true;
  grenade.phase=HandGrenadePhase::throwing;++grenade.reserved_ammo;++grenade.throw_count;
  start(LocalWeaponAction::throw_release,command.sequence,at,context->sequence,0U,
      std::numeric_limits<double>::max(),false,false);
}
void LocalWeaponPresentationController::observe_hand_grenade(
    const client::RuntimeClientObservationState& state,bool fresh_client,
    std::optional<std::uint8_t> old_reserve,bool fresh_ammo,
    std::optional<std::uint32_t> changed_client_animation,double now) {
  auto& grenade=hand_grenade_;
  const auto confirm_current_reservation=[&](const client::RuntimeObservationSource& source) {
    // An absolute count can include a pickup and a consumed grenade in one
    // commit. A fresh matching throw occurrence plus that count settles only
    // its reservation; retained AmmoX and duplicate poses cannot settle twice.
    if(!fresh_ammo || !old_reserve || !reserve_ || *reserve_<*old_reserve ||
        !hand_grenade_ammo_source_ || !action_ ||
        action_->identity.kind!=LocalWeaponAction::throw_release ||
        !hand_grenade_last_reservation_pending_ || !grenade.reserved_ammo ||
        hand_grenade_ammo_source_->record_ordinal<source.record_ordinal ||
        source.record_ordinal<=action_->pre_record_ordinal) return;
    --grenade.reserved_ammo;hand_grenade_last_reservation_pending_=false;
    finish_pending(LocalWeaponActionStatus::server_confirmed);
  };
  const auto restore_ready=[&] {
    const bool release_current=(grenade.phase==HandGrenadePhase::throwing ||
        grenade.phase==HandGrenadePhase::post_throw || grenade.phase==HandGrenadePhase::retired) &&
        hand_grenade_last_reservation_pending_;
    finish_pending(LocalWeaponActionStatus::server_rejected);action_.reset();
    grenade.phase=HandGrenadePhase::ready;grenade.prime_time.reset();grenade.release_time.reset();
    if(release_current) {
      grenade.reserved_ammo-=static_cast<std::uint8_t>(grenade.reserved_ammo ? 1U : 0U);
      hand_grenade_last_reservation_pending_=false;
    }
    hand_grenade_server_primed_=false;hand_grenade_release_required_=true;
    ++grenade.corrections;server_visual_.reset();publish_idle(now);
  };
  if(fresh_ammo && old_reserve && reserve_ && *reserve_<*old_reserve && grenade.reserved_ammo) {
    grenade.reserved_ammo-=static_cast<std::uint8_t>(std::min<unsigned>(grenade.reserved_ammo,*old_reserve-*reserve_));
    if(!grenade.reserved_ammo) hand_grenade_last_reservation_pending_=false;
    if(action_ && action_->identity.kind==LocalWeaponAction::throw_release) {
      finish_pending(LocalWeaponActionStatus::server_confirmed);
      action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
    }
  }
  if(grenade.phase==HandGrenadePhase::retired && fresh_ammo && reserve_ && *reserve_>0 &&
      old_reserve && *reserve_>*old_reserve) {
    grenade.phase=HandGrenadePhase::ready;grenade.reserved_ammo=0;
    hand_grenade_last_reservation_pending_=false;publish_idle(now);
  }
  const auto slot=std::find_if(state.weapon_slots.begin(),state.weapon_slots.end(),
      [](const auto& value){return value.weapon_id==12U;});
  if(fresh_client && slot==state.weapon_slots.end()) hand_grenade_idle_timer_.reset();
  if(fresh_client && slot!=state.weapon_slots.end()) {
  hand_grenade_idle_timer_=slot->time_weapon_idle;
  const auto previous_server_start=grenade.server_start_throw;
  grenade.server_start_throw=slot->auxiliary_float2;
  grenade.server_release_throw=slot->auxiliary_float3;
  const bool explicit_start=slot->auxiliary_float2_updated_in_record && slot->auxiliary_float2;
  if(explicit_start && *slot->auxiliary_float2>0) {
    const bool was_primed=grenade.phase==HandGrenadePhase::primed || grenade.phase==HandGrenadePhase::release_pending;
    // A delayed positive prime record can arrive after our throw. Its same
    // prime occurrence must not resurrect a pin-pull or undo the reservation.
    const bool late_prime=grenade.throw_time &&
        (!previous_server_start || *previous_server_start<=0 || previous_server_start==slot->auxiliary_float2);
    if(!was_primed && !late_prime && state.server_time_seconds &&
        *state.server_time_seconds>=*slot->auxiliary_float2) {
      // Absolute server time is mapped through this committed server clock,
      // never treated as the local command clock or a decrement timer.
      const auto elapsed=*state.server_time_seconds-*slot->auxiliary_float2;
      const auto prime=std::max(0.0,now-elapsed);
      grenade.phase=HandGrenadePhase::primed;grenade.prime_time=prime;
      grenade.release_time.reset();grenade.throw_time.reset();grenade.frozen_throw.reset();
      grenade.reserved_ammo=0;grenade.prime_command=grenade.release_command=grenade.throw_command=0;
      start(LocalWeaponAction::prime,0U,prime,2U,0U,std::numeric_limits<double>::max(),false,false);
      ++grenade.corrections;
    } else if(was_primed && (!hand_grenade_server_primed_ ||
        previous_server_start!=slot->auxiliary_float2) &&
        state.server_time_seconds && *state.server_time_seconds>=*slot->auxiliary_float2) {
      // First confirmation can correct the predicted origin too. The current
      // server-clock anchor maps epochs; receipt/transport delay still limits
      // this presentation-only mapping. Never allocate a new occurrence.
      const auto mapped=std::max(0.0,now-(*state.server_time_seconds-*slot->auxiliary_float2));
      if(!grenade.prime_time || std::abs(*grenade.prime_time-mapped)>1e-9) {
        grenade.prime_time=mapped;
        if(action_ && action_->identity.kind==LocalWeaponAction::prime)
          action_->visual.started_at_seconds=mapped;
        ++grenade.corrections;
      }
    }
    if(grenade.phase==HandGrenadePhase::primed || grenade.phase==HandGrenadePhase::release_pending)
      hand_grenade_server_primed_=true;
    if(action_ && action_->identity.kind==LocalWeaponAction::prime) {
      finish_pending(LocalWeaponActionStatus::server_confirmed);
      action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
    }
  } else if(explicit_start && *slot->auxiliary_float2==0 &&
      (grenade.phase==HandGrenadePhase::primed || grenade.phase==HandGrenadePhase::release_pending)) {
    const bool corrected=hand_grenade_server_primed_ ||
        (grenade.prime_time && now-*grenade.prime_time>=.75 &&
         state.receiving_client && state.receiving_client->weapon_animation &&
         idle(12U,*state.receiving_client->weapon_animation));
    if(corrected && client_source_ && client_source_->record_ordinal>hand_grenade_prime_record_) {
      restore_ready();
    }
  }
  if(slot->auxiliary_float3_updated_in_record && slot->auxiliary_float3 &&
      *slot->auxiliary_float3>0 && grenade.prime_time &&
      (grenade.phase==HandGrenadePhase::primed || grenade.phase==HandGrenadePhase::release_pending) &&
      state.server_time_seconds && *state.server_time_seconds>=*slot->auxiliary_float3) {
    if(!grenade.release_time) grenade.release_time=std::max(*grenade.prime_time,
        now-(*state.server_time_seconds-*slot->auxiliary_float3));
    grenade.phase=HandGrenadePhase::release_pending;
  }
  // A fresh real server pose corrects the same local lifecycle; it is not a
  // synthetic firing event. Matching confirmation never restarts animation.
  if(state.receiving_client && state.receiving_client->weapon_animation && action_) {
    const auto sequence=*state.receiving_client->weapon_animation;
    if(sequence==action_->visual.sequence && client_source_ && client_source_->record_ordinal>action_->pre_record_ordinal)
      action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
    if(sequence==action_->visual.sequence && client_source_ &&
        (changed_client_animation==sequence || (explicit_start && *slot->auxiliary_float2==0)))
      confirm_current_reservation(*client_source_);
  }
  if(changed_client_animation && action_ && client_source_ &&
      client_source_->record_ordinal>action_->pre_record_ordinal &&
      valid_pose(*changed_client_animation,action_->visual.body)) {
    const auto sequence=*changed_client_animation;
    if(idle(12U,sequence)) restore_ready();
    else if(sequence!=action_->visual.sequence) {
      action_->visual.sequence=sequence;action_->visual.restart_identity=++restart_identity_;
      action_->visual.started_at_seconds=now;
      action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
      finish_pending(LocalWeaponActionStatus::server_corrected);++grenade.corrections;
    }
  }
  if(changed_client_animation && !action_ && explicit_start && *slot->auxiliary_float2==0 &&
      *changed_client_animation>=3U && *changed_client_animation<=5U &&
      valid_pose(*changed_client_animation,0U)) {
    // Clearing a confirmed prime may precede its local minimum-time command.
    // That clears provisional state, not the fresh authoritative throw pose in
    // this same clientdata record. No firing event/projectile is manufactured.
    if(!server_visual_ || server_visual_->sequence!=*changed_client_animation)
      server_visual_=LocalWeaponVisual{*changed_client_animation,0U,++restart_identity_,now,
          LocalWeaponAnimationSource::confirmed_weapon_state,LocalWeaponActionStatus::retained_server_event};
  }
  if(explicit_start && *slot->auxiliary_float2==0 && grenade.reserved_ammo &&
      (grenade.phase==HandGrenadePhase::throwing || grenade.phase==HandGrenadePhase::retired) &&
      grenade.throw_time && now-*grenade.throw_time>=.75 && hand_grenade_last_throw_reserve_ &&
      reserve_ && *reserve_>=*hand_grenade_last_throw_reserve_ && state.receiving_client &&
      state.receiving_client->weapon_animation && idle(12U,*state.receiving_client->weapon_animation))
    restore_ready(); // Fresh explicit server rejection, never a render-only timeout.
  }
  const auto& hud=state.weapon_hud;
  if(hud.animation_source && hud.animation_sequence && hud.animation_body &&
      hud.animation_source->record_ordinal>=model_bound_record_ordinal_) {
    const SeenEvent event{*hud.animation_source,*hud.animation_sequence,*hud.animation_body};
    const bool seen=std::any_of(event_history_.begin(),event_history_.end(),
        [&](const auto& value){return value && value->source==event.source;});
    if(!seen) {
      event_history_[event_history_next_++%event_history_.size()]=event;
      if(!valid_pose(event.sequence,event.body)) {counters_.error=LocalWeaponPresentationError::unsupported_pose;return;}
      if(idle(12U,event.sequence) && grenade.reserved_ammo && hand_grenade_last_throw_reserve_ &&
          reserve_ && *reserve_>=*hand_grenade_last_throw_reserve_ &&
          grenade.phase==HandGrenadePhase::retired) restore_ready();
      if(action_ && event.source.record_ordinal>action_->pre_record_ordinal) {
        const bool current_throw=action_->identity.kind==LocalWeaponAction::throw_release;
        const bool same=action_->visual.sequence==event.sequence && action_->visual.body==event.body;
        if(!same) {
          action_->visual.sequence=event.sequence;action_->visual.body=event.body;
          action_->visual.restart_identity=++restart_identity_;action_->visual.started_at_seconds=now;
        }
        finish_pending(same ? LocalWeaponActionStatus::server_confirmed : LocalWeaponActionStatus::server_corrected);
        if(same) confirm_current_reservation(event.source);
        action_->visual.source=LocalWeaponAnimationSource::service_event;
        if(idle(12U,event.sequence)) {
          server_visual_=action_->visual;action_.reset();grenade.phase=HandGrenadePhase::ready;
          grenade.prime_time.reset();grenade.release_time.reset();hand_grenade_server_primed_=false;
          hand_grenade_release_required_=true;
          if(current_throw && hand_grenade_last_reservation_pending_ && grenade.reserved_ammo &&
              hand_grenade_last_throw_reserve_ && reserve_ && *reserve_>=*hand_grenade_last_throw_reserve_) {
            --grenade.reserved_ammo;hand_grenade_last_reservation_pending_=false;
          }
        }
      } else if(grenade.phase!=HandGrenadePhase::retired) server_visual_=LocalWeaponVisual{
          event.sequence,event.body,++restart_identity_,now,LocalWeaponAnimationSource::service_event,
          LocalWeaponActionStatus::retained_server_event};
    }
  }
}
void LocalWeaponPresentationController::clear_hornet_state() noexcept {
  hornet_receipts_.fill(std::nullopt);hornet_receipt_next_=0;
  hornet_primary_ready_at_=hornet_secondary_ready_at_=0;
  hornet_reserve_source_.reset();
}
void LocalWeaponPresentationController::expire_hornet_reservations(double now) noexcept {
  for(auto& receipt:hornet_receipts_) if(receipt && now>=receipt->deadline) {
    receipt->reserved=false;
    if(receipt->status==LocalWeaponActionStatus::predicted_pending) {
      if(action_ && action_->identity==receipt->identity) finish_pending(LocalWeaponActionStatus::timed_out);
      else {++counters_.actions_rejected;++counters_.recoil_rejected;receipt->status=LocalWeaponActionStatus::timed_out;}
    }
    // Keep the bounded receipt beyond the visual timeout: a delayed echo is
    // acknowledgement, not another sound/recoil/animation occurrence.
    if(now>receipt->deadline+.75) receipt.reset();
  }
}
void LocalWeaponPresentationController::start_hornet(HornetFireMode mode,
    std::uint32_t command,double at,bool committed) {
  const auto reserved=static_cast<std::size_t>(std::count_if(hornet_receipts_.begin(),hornet_receipts_.end(),
      [](const auto& receipt){return receipt && receipt->reserved;}));
  // A committed-only callback may observe post-shot reserve. It cannot infer
  // that the prior count was one; actual weapon timers remain authoritative.
  const bool final_round=!committed && reserve_ && static_cast<std::size_t>(*reserve_)==reserved+1U;
  start(mode==HornetFireMode::fast ? LocalWeaponAction::secondary_fire : LocalWeaponAction::primary_fire,
      command,at,hornet_shoot_sequence,1U,at+.75);
  if(!action_) return;
  auto& receipt=hornet_receipts_[hornet_receipt_next_++%hornet_receipts_.size()];
  receipt=HornetReceipt{action_->identity,action_->pre_record_ordinal,at+.75,!committed,committed};
  if(mode==HornetFireMode::fast) {
    hornet_primary_ready_at_=hornet_secondary_ready_at_=at+.1+(final_round ? .5 : 0.0);
  } else {
    // Primary changes only the primary timer. Secondary has priority and can
    // run as soon as its independently observed/accepted timer permits it.
    hornet_primary_ready_at_=at+.25+(final_round ? hornet_recharge_interval(maximum_clients_) : 0.0);
  }
  if(committed) {
    action_->scripted_event_confirmed=true;
    action_->visual.source=LocalWeaponAnimationSource::service_event;
    finish_pending(LocalWeaponActionStatus::server_confirmed);
  }
}
void LocalWeaponPresentationController::observe_hornet_ammo(
    std::optional<std::uint8_t> before,bool fresh,double now) {
  if(!hornet_selected_) return;
  expire_hornet_reservations(now);
  // A post-event absolute AmmoX publication includes any coalesced consume +
  // recharge. Its unchanged count is authoritative too; unrelated HUD or
  // retained ammo never releases another not-yet-sampled action reservation.
  if(!fresh || !before || !reserve_ || *reserve_>=*before || *reserve_>hornet_maximum_ammo) {
    release_confirmed_hornet_reservations();return;
  }
  auto consumed=static_cast<unsigned>(*before-*reserve_);
  while(consumed--) {
    HornetReceipt* oldest=nullptr;
    for(auto& receipt:hornet_receipts_) if(receipt && receipt->reserved &&
        canonical_revision_>receipt->identity.canonical_pre_revision &&
        ((hud_source_ && hud_source_->record_ordinal>receipt->pre_record) ||
         (client_source_ && client_source_->record_ordinal>receipt->pre_record)) &&
        (!oldest || receipt->identity.started_at_seconds<oldest->identity.started_at_seconds)) oldest=&*receipt;
    if(!oldest) break;
    oldest->reserved=false;
    if(oldest->status!=LocalWeaponActionStatus::predicted_pending) continue;
    if(action_ && action_->identity==oldest->identity) {
      action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
      finish_pending(LocalWeaponActionStatus::server_confirmed);
    } else {
      oldest->status=LocalWeaponActionStatus::server_confirmed;
      ++counters_.actions_confirmed;++counters_.recoil_confirmed;
      if(oldest->identity.kind==LocalWeaponAction::secondary_fire) ++counters_.secondary_fire_confirmed;
      else ++counters_.primary_fire_confirmed;
    }
  }
  release_confirmed_hornet_reservations();
}
void LocalWeaponPresentationController::release_confirmed_hornet_reservations() noexcept {
  if(!hornet_reserve_source_) return;
  for(auto& receipt:hornet_receipts_) if(receipt && receipt->reserved && receipt->confirmation_source) {
    const auto& confirmed=*receipt->confirmation_source;
    if(hornet_reserve_source_->record_ordinal>confirmed.record_ordinal ||
        (hornet_reserve_source_->record_identity==confirmed.record_identity &&
         hornet_reserve_source_->record_ordinal==confirmed.record_ordinal &&
         hornet_reserve_source_->start_bit_offset>=confirmed.start_bit_offset)) receipt->reserved=false;
  }
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
    auto satchel_models=std::move(satchel_models_);
    // Initial resource configuration may precede the first observation. A
    // later network generation must re-advertise its assigned event index.
    auto secondary_event=secondary_event_index_;
    auto glock_secondary_event=glock_secondary_event_index_;
    auto shotgun_single_event=shotgun_single_event_index_;
    auto shotgun_double_event=shotgun_double_event_index_;
    auto python_event=python_event_index_;
    auto crossbow_event=crossbow_event_index_,crossbow_sniper_event=crossbow_sniper_event_index_;
    auto rpg_event=rpg_event_index_;
    auto hornet_event=generation_==0U ? hornet_event_index_ : std::nullopt;
    auto tripmine_event=generation_==0U ? tripmine_event_index_ : std::nullopt;
    auto snark_event=generation_==0U ? snark_event_index_ : std::nullopt;
    const auto crossbow_sniper=crossbow_sniper_;
    const auto maximum_clients=maximum_clients_;
    if(generation_!=0U) python_event.reset();
    if(generation_!=0U) {crossbow_event.reset();crossbow_sniper_event.reset();}
    if(generation_!=0U) secondary_event.reset();
    if(generation_!=0U) glock_secondary_event.reset();
    if(generation_!=0U) { shotgun_single_event.reset(); shotgun_double_event.reset(); }
    const auto diagnostic_observer=diagnostic_observer_;
    *this = LocalWeaponPresentationController{};
    diagnostic_observer_=diagnostic_observer;
    maximum_clients_=maximum_clients;
    python_event_index_=python_event;
    crossbow_event_index_=crossbow_event;crossbow_sniper_event_index_=crossbow_sniper_event;
    rpg_event_index_=rpg_event;
    hornet_event_index_=hornet_event;
    tripmine_event_index_=tripmine_event;
    snark_event_index_=snark_event;
    crossbow_sniper_=crossbow_sniper;
    audio_ = std::move(audio); audio_.bind(model);
    generation_ = state.generation; last_time_ = now; model_ = std::move(model);
    for(std::size_t i=0;i<satchel_models.size();++i)
      if(satchel_models[i] && satchel_models[i]->generation==generation_) satchel_models_[i]=std::move(satchel_models[i]);
    secondary_event_index_=secondary_event;
    glock_secondary_event_index_=glock_secondary_event;
    shotgun_single_event_index_=shotgun_single_event;
    shotgun_double_event_index_=shotgun_double_event;
  }
  const auto& hud = state.weapon_hud;
  if (life_epoch_ != state.lifecycle.life_epoch || life_deaths_ != state.lifecycle.deaths) {
    server_reload_predecessor_deadline_.reset();
    const bool shotgun_transition=weapon_id_==7U || hud.active_weapon_id==7U;
    audio_.cancel();
    life_epoch_ = state.lifecycle.life_epoch;
    life_deaths_ = state.lifecycle.deaths;
    finish_pending(LocalWeaponActionStatus::server_rejected);
    action_.reset(); server_visual_.reset(); recoil_active_ = false;
    client_animation_sequence_.reset(); client_source_.reset(); hud_source_.reset();
    weapon_id_.reset(); model_index_.reset();
    pending_fire_.fill(std::nullopt);provisional_rounds_=0;
    pending_launch_.reset();provisional_grenades_=0;
    clear_hornet_state();
    clear_hand_grenade_state();
    clear_satchel_state();
    clear_tripmine_state();
    clear_snark_state();
    special_reload_state_.reset();pump_time_.reset();
    if(shotgun_transition) last_primary_fire_at_=-1.0e9;
    shotgun_fire_cycle_seconds_=shotgun_single_cycle_seconds;
    last_secondary_launch_at_=-1.0e9;
    launch_idle_not_before_=0.0;
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
  if(weapon_id_==14U && active==14U && index && std::any_of(satchel_models_.begin(),satchel_models_.end(),
      [&](const auto& variant){return variant && variant->generation==state.generation && variant->model_index==*index;}))
    model_index_=index; // Satchel's server-confirmed model pair shares one lifecycle.
  if (weapon_id_ != active || model_index_ != index) {
    server_reload_predecessor_deadline_.reset();
    const bool shotgun_transition=weapon_id_==7U || active==7U;
    audio_.cancel();
    finish_pending(LocalWeaponActionStatus::server_rejected);
    action_.reset(); server_visual_.reset(); client_source_.reset(); hud_source_.reset();
    clip_.reset(); reserve_.reset(); in_reload_.reset(); next_primary_attack_.reset();
    secondary_reserve_.reset();water_level_.reset();next_secondary_attack_.reset();launch_idle_not_before_=0.0;
    next_client_attack_.reset(); next_reload_.reset();
    special_reload_state_.reset();pump_time_.reset();
    client_animation_sequence_.reset(); recoil_active_ = false; server_punch_nonzero_ = false;
    idle_sequence_ = 0U; idle_body_ = 0U; weapon_id_ = active; model_index_ = index;
    pending_fire_.fill(std::nullopt);provisional_rounds_=0;
    pending_launch_.reset();provisional_grenades_=0;
    clear_hornet_state();
    clear_hand_grenade_state();
    clear_satchel_state();
    clear_tripmine_state();
    clear_snark_state();
    if(shotgun_transition) last_primary_fire_at_=-1.0e9;
    shotgun_fire_cycle_seconds_=shotgun_single_cycle_seconds;
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
  if (canonical_dead_) {
    server_reload_predecessor_deadline_.reset();
    finish_pending(LocalWeaponActionStatus::server_rejected);
    pending_fire_.fill(std::nullopt);provisional_rounds_=0;
    pending_launch_.reset();provisional_grenades_=0;
    clear_hornet_state();
    clear_hand_grenade_state();
    clear_satchel_state();
    clear_tripmine_state();
    clear_snark_state();
    if((active==7U || active==3U || active==6U || active==8U || active==11U || active==12U || active==13U || active==14U || active==15U) && (action_ || server_visual_ || recoil_active_ ||
        special_reload_state_ || pump_time_)) {
      // A committed zero-health observation can precede the lifecycle body's
      // publication. Retire even already-confirmed Shotgun stages immediately;
      // they (and Python's conventional reload) must not keep recoil or reload
      // presentation alive until an epoch counter changes. Other policy stays.
      audio_.cancel();action_.reset();server_visual_.reset();recoil_active_=false;
      special_reload_state_.reset();pump_time_.reset();
    }
  }
  mp5_selected_=active==4U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [&](const auto& type){return type.id==*active && mp5_command(type.command_name);});
  shotgun_selected_=active==7U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [&](const auto& type){return type.id==*active && shotgun_command(type.command_name);});
  python_selected_=active==3U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [&](const auto& type){return type.id==*active && python_command(type.command_name);});
  crossbow_selected_=active==6U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [&](const auto& type){return type.id==*active && crossbow_command(type.command_name) &&
          type.primary_ammo_type>=0;});
  rpg_selected_=active==8U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [](const auto& type){return type.id==8U && rpg_command(type.command_name) && type.primary_ammo_type>=0;});
  hornet_selected_=active==11U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [](const auto& type){return type.id==11U && hornet_command(type.command_name) && type.primary_ammo_type>=0;});
  hand_grenade_selected_=active==12U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [](const auto& type){return type.id==12U && hand_grenade_command(type.command_name) && type.primary_ammo_type>=0;});
  satchel_selected_=active==14U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [](const auto& type){return type.id==14U && satchel_command(type.command_name) && type.primary_ammo_type>=0;});
  tripmine_selected_=active==13U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [](const auto& type){return type.id==13U && tripmine_command(type.command_name) && type.primary_ammo_type>=0 && (type.flags&24U)==24U;});
  snark_selected_=active==15U && std::any_of(hud.catalogue.begin(),hud.catalogue.end(),
      [](const auto& type){return type.id==15U && snark_command(type.command_name) && type.primary_ammo_type>=0 &&
          (type.flags&24U)==24U;});
  if(snark_selected_ && snark_model_rebind_pending_ && state.client_metadata.source &&
      state.client_metadata.freshness==client::RuntimeObservationFreshness::observed_in_record) {
    model_bound_record_ordinal_=std::max(model_bound_record_ordinal_,state.client_metadata.source->record_ordinal);
    snark_model_rebind_pending_=false;
  }
  rpg_state_=rpg_launcher_state(state);
  if(python_selected_) idle_body_=maximum_clients_>1U ? 1U : 0U;
  if (!supported()) { counters_.error = LocalWeaponPresentationError::unsupported_model; return; }
  counters_.error = LocalWeaponPresentationError::none;
  const auto old_clip = clip_; const auto old_reserve = reserve_;
  const auto old_secondary_reserve=secondary_reserve_;
  const auto old_secondary_timer=next_secondary_attack_;
  const auto old_reload = in_reload_; const auto old_timer = next_primary_attack_;
  const auto old_client_attack = next_client_attack_;
  const auto old_next_reload = next_reload_;
  const auto old_special_reload=special_reload_state_;
  bool fresh = false;
  bool fresh_weapon_slot = false;
  bool client_pose_published = false;
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
      fresh_weapon_slot = true;
      clip_ = slot->clip; in_reload_ = slot->in_reload; next_primary_attack_ = slot->next_primary_attack;
      next_reload_ = slot->next_reload;
      next_secondary_attack_=slot->next_secondary_attack;
      special_reload_state_=slot->special_reload_state;
      pump_time_=slot->pump_time;
    } else if(shotgun_selected_) {
      // A fresh record without the slot cannot authorize a special-reload
      // transition using retained numeric state from an older record.
      special_reload_state_.reset();pump_time_.reset();
    }
    if (state.receiving_client) {
      const auto& receiving = *state.receiving_client;
      next_client_attack_ = receiving.next_weapon_attack;
      water_level_=receiving.water_level;
      const auto& punch = receiving.punch_angle;
      server_punch_nonzero_ = punch.complete() && std::isfinite(*punch.x) && std::isfinite(*punch.y) &&
        std::isfinite(*punch.z) && std::hypot(*punch.x,*punch.y,*punch.z) > 0.001;
      // Canonical nonzero punch REPLACES local punch for this action; no resurrection.
      if (server_punch_nonzero_) recoil_active_ = false;
      if (receiving.weapon_animation && receiving.weapon_animation != client_animation_sequence_) {
        if (client_animation_sequence_) changed_client_animation = receiving.weapon_animation;
        client_animation_sequence_ = receiving.weapon_animation;
        const auto seq = *client_animation_sequence_;
        const auto state_body=python_selected_ ? idle_body_ : std::uint8_t{0};
        if (valid_pose(seq,state_body)) {
          if (idle(*active,seq)) idle_sequence_ = seq;
          // State is not a repeat event. Unchanged idle cannot erase a local action.
          const bool retain_observed_reload=fresh_weapon_slot && in_reload_==true &&
              (mp5_selected_ || crossbow_selected_) && server_visual_ &&
              server_visual_->sequence==(mp5_selected_ ? 3U : 7U);
          if (!action_ && !retain_observed_reload && (!server_visual_ || server_visual_->sequence != seq)) {
            server_visual_ = LocalWeaponVisual{seq,state_body,++restart_identity_,now,
              LocalWeaponAnimationSource::clientdata_state,LocalWeaponActionStatus::retained_server_event};
            client_pose_published=true;
          }
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
  if(fresh && type!=hud.catalogue.end() && type->secondary_ammo_type>=0 &&
      static_cast<std::size_t>(type->secondary_ammo_type)<hud.reserve_ammo.size())
    secondary_reserve_=hud.reserve_ammo[static_cast<std::size_t>(type->secondary_ammo_type)];
  bool hornet_ammo_fresh=false;
  if(hornet_selected_ && type!=hud.catalogue.end() && type->primary_ammo_type>=0 &&
      static_cast<std::size_t>(type->primary_ammo_type)<hud.reserve_ammo_sources.size()) {
    const auto& source=hud.reserve_ammo_sources[static_cast<std::size_t>(type->primary_ammo_type)];
    if(source && newer(*source,hornet_reserve_source_)) {hornet_reserve_source_=source;hornet_ammo_fresh=true;}
  }
  observe_hornet_ammo(old_reserve,hornet_ammo_fresh,now);
  if(snark_selected_) {
    bool fresh_ammo=false;
    if(type!=hud.catalogue.end() && type->primary_ammo_type>=0 &&
        static_cast<std::size_t>(type->primary_ammo_type)<hud.reserve_ammo_sources.size()) {
      const auto& source=hud.reserve_ammo_sources[static_cast<std::size_t>(type->primary_ammo_type)];
      if(source && newer(*source,snark_ammo_source_)) {snark_ammo_source_=source;fresh_ammo=true;}
    }
    observe_snark(state,old_reserve,fresh_ammo,changed_client_animation,now);
    if(!server_visual_ && !action_ && !snark_.retired)publish_idle(now);
    return;
  }
  if(tripmine_selected_) {
    bool fresh_ammo=false;
    if(type!=hud.catalogue.end() && type->primary_ammo_type>=0 &&
        static_cast<std::size_t>(type->primary_ammo_type)<hud.reserve_ammo_sources.size()) {
      const auto& source=hud.reserve_ammo_sources[static_cast<std::size_t>(type->primary_ammo_type)];
      if(source && newer(*source,tripmine_ammo_source_)) {tripmine_ammo_source_=source;fresh_ammo=true;}
    }
    observe_tripmine(state,old_reserve,fresh_ammo,now);
    if(!server_visual_ && !action_ && !tripmine_.retired) publish_idle(now);
    return;
  }
  if(satchel_selected_) {
    bool fresh_ammo=false;
    if(type!=hud.catalogue.end() && type->primary_ammo_type>=0 &&
        static_cast<std::size_t>(type->primary_ammo_type)<hud.reserve_ammo_sources.size()) {
      const auto& source=hud.reserve_ammo_sources[static_cast<std::size_t>(type->primary_ammo_type)];
      if(source && newer(*source,satchel_ammo_source_)) {satchel_ammo_source_=source;fresh_ammo=true;}
    }
    observe_satchel(state,fresh_client,old_reserve,fresh_ammo,changed_client_animation,now);
    return;
  }
  if(hand_grenade_selected_) {
    bool fresh_ammo=false;
    if(type!=hud.catalogue.end() && type->primary_ammo_type>=0 &&
        static_cast<std::size_t>(type->primary_ammo_type)<hud.reserve_ammo_sources.size()) {
      const auto& source=hud.reserve_ammo_sources[static_cast<std::size_t>(type->primary_ammo_type)];
      if(source && newer(*source,hand_grenade_ammo_source_)) {hand_grenade_ammo_source_=source;fresh_ammo=true;}
    }
    observe_hand_grenade(state,fresh_client,old_reserve,fresh_ammo,changed_client_animation,now);
    if(!server_visual_ && hand_grenade_.phase!=HandGrenadePhase::retired) publish_idle(now);
    return;
  }
  if(crossbow_selected_) idle_sequence_=clip_.value_or(0)>0 ? 0U : 1U;
  if(rpg_selected_ && clip_) {
    idle_sequence_=*clip_>0 ? 0U : 8U;
    // The retained derived idle may belong to the opposite clip state (e.g.
    // first empty pickup). Preserve exact service poses and ongoing actions;
    // only refresh the incompatible, lower-precedence derived idle.
    if(!action_ && server_visual_ && server_visual_->source==LocalWeaponAnimationSource::clientdata_state &&
        idle(*active,server_visual_->sequence) &&
        (server_visual_->sequence>=8U)!=(*clip_==0)) publish_idle(now);
  }
  if(fresh && old_secondary_reserve && secondary_reserve_ && *secondary_reserve_<*old_secondary_reserve) {
    const auto consumed=static_cast<std::size_t>(*old_secondary_reserve-*secondary_reserve_);
    provisional_grenades_=consumed>=provisional_grenades_ ? 0U : provisional_grenades_-consumed;
    if(pending_launch_ && pending_launch_->reserve_before &&
        canonical_revision_>pending_launch_->identity.canonical_pre_revision &&
        *secondary_reserve_<*pending_launch_->reserve_before) {
      if(pending_launch_->status==LocalWeaponActionStatus::predicted_pending) {
        ++counters_.actions_confirmed;++counters_.secondary_launch_confirmed;++counters_.recoil_confirmed;
      }
      pending_launch_.reset();
    }
  }
  if(fresh_client && pending_launch_ && old_secondary_timer && next_secondary_attack_ &&
      *next_secondary_attack_>*old_secondary_timer+0.01 && client_source_ &&
      client_source_->record_ordinal>pending_launch_->pre_record_ordinal) {
    if(pending_launch_->status==LocalWeaponActionStatus::predicted_pending) {
      ++counters_.actions_confirmed;++counters_.secondary_launch_confirmed;++counters_.recoil_confirmed;
    }
    pending_launch_.reset();
  }
  if((mp5_selected_ || shotgun_selected_ || crossbow_selected_) && fresh && old_clip && clip_ && *clip_<*old_clip) {
    const auto consumed=static_cast<std::size_t>(*old_clip-*clip_);
    provisional_rounds_=consumed>=provisional_rounds_ ? 0U : provisional_rounds_-consumed;
    for(auto& pending:pending_fire_) if(pending && pending->status==LocalWeaponActionStatus::predicted_pending && pending->clip_before &&
        canonical_revision_>pending->identity.canonical_pre_revision &&
        *clip_<=*pending->clip_before-pending->rounds_reserved) {
      ++counters_.actions_confirmed;
      if(pending->identity.kind==LocalWeaponAction::secondary_fire) ++counters_.secondary_fire_confirmed;
      else ++counters_.primary_fire_confirmed;
      if(!pending->crossbow_sniper) ++counters_.recoil_confirmed;
      if(crossbow_selected_ || (mp5_selected_ && !action_ && in_reload_==true &&
          server_visual_ && server_visual_->sequence==3U))
        pending->status=LocalWeaponActionStatus::server_confirmed;
      else pending.reset();
    }
  }
  const bool post_action = action_ && canonical_revision_ > action_->identity.canonical_pre_revision &&
    ((fresh_client && client_source_->record_ordinal > action_->pre_record_ordinal) ||
     (hud_source_ && hud_source_->record_ordinal > action_->pre_record_ordinal));
  if (fresh && post_action) {
    const auto kind = action_->identity.kind;
    if (bullet_fire(kind) && old_clip && clip_ && action_->clip_before &&
        (mp5_selected_ || shotgun_selected_ ? *clip_ <= *action_->clip_before - action_->rounds_reserved :
            *clip_ == *action_->clip_before - 1) && *clip_ < *old_clip) {
      if(!mp5_selected_ && !shotgun_selected_ && !python_selected_ && !crossbow_selected_ && !rpg_selected_ && !hornet_selected_) action_->visual.sequence = action_->glock_secondary ?
          glock_secondary_sequence : *clip_ == 0 ? 4U : 3U;
      action_->visual.source = LocalWeaponAnimationSource::confirmed_weapon_state;
      finish_pending(LocalWeaponActionStatus::server_confirmed);
    } else if(kind==LocalWeaponAction::secondary_launch &&
        ((old_secondary_reserve && secondary_reserve_ && action_->reserve_before &&
          *secondary_reserve_<*old_secondary_reserve && *secondary_reserve_<*action_->reserve_before) ||
         (fresh_client && old_secondary_timer && next_secondary_attack_ &&
          *next_secondary_attack_>*old_secondary_timer+0.01))) {
      action_->visual.source=LocalWeaponAnimationSource::confirmed_weapon_state;
      finish_pending(LocalWeaponActionStatus::server_confirmed);
    } else if (kind == LocalWeaponAction::reload &&
      ((shotgun_selected_ && special_reload_state_ && *special_reload_state_>0 && *special_reload_state_<=2) ||
       (in_reload_ && *in_reload_ && old_reload && !*old_reload) ||
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
  if(shotgun_selected_ && fresh_client)
    observe_shotgun_reload(old_special_reload,old_clip,now);
  if((mp5_selected_ || crossbow_selected_ || rpg_selected_) && fresh_weapon_slot && !canonical_dead_)
    observe_conventional_reload(old_reload,client_pose_published,mp5_selected_ ? 3U : crossbow_selected_ ? 7U : 2U,now);
  const bool inherited_reload_pose=reload_predecessor_active(now);
  // A changed current-record clientdata state has higher precedence than a
  // derived/predicted pose, but is never an automatic repeated restart event.
  if (changed_client_animation && action_ && fresh_client && !inherited_reload_pose &&
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
      else if(inherited_reload_pose &&
          (mp5_selected_ ? event.sequence==2U || (event.sequence>=5U && event.sequence<=7U) :
           crossbow_selected_ && (event.sequence==4U || event.sequence==6U))) {
        // The previous shot's exact pose is consumed once without taking
        // ownership from the newer reload or replaying its audio markers.
        event_history_[event_history_next_++ % event_history_.size()] = event;
      } else {
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
            if (action_->identity.kind == LocalWeaponAction::secondary_launch && event.sequence!=2U)
              recoil_active_=false;
            if (bullet_fire(action_->identity.kind) &&
                (hornet_selected_ ? event.sequence!=hornet_shoot_sequence : rpg_selected_ ? event.sequence!=3U : crossbow_selected_ ? event.sequence!=4U && event.sequence!=6U : python_selected_ ? event.sequence!=2U : shotgun_selected_ ? event.sequence!=1U && event.sequence!=2U :
                 mp5_selected_ ? event.sequence<5U || event.sequence>7U :
                    event.sequence != 3U && event.sequence != 4U)) recoil_active_ = false;
          }
          finish_pending(same ? LocalWeaponActionStatus::server_confirmed : LocalWeaponActionStatus::server_corrected);
          if (!same) { action_->status = LocalWeaponActionStatus::server_corrected; action_->visual.status = action_->status; }
          action_->visual.source = LocalWeaponAnimationSource::service_event;
          if (idle(*active,event.sequence)) { server_visual_ = action_->visual; action_.reset(); }
        } else if((mp5_selected_ || crossbow_selected_ || rpg_selected_) && in_reload_==true &&
            event.sequence==(mp5_selected_ ? 3U : crossbow_selected_ ? 7U : 2U) && event.body==0U &&
            server_visual_ && server_visual_->sequence==event.sequence && server_visual_->body==0U &&
            (server_visual_->source==LocalWeaponAnimationSource::confirmed_weapon_state ||
             server_visual_->source==LocalWeaponAnimationSource::service_event)) {
          // First matching svc confirmation of this observed reload occurrence
          // upgrades provenance without restarting its already visible timeline.
          server_visual_->source=LocalWeaponAnimationSource::service_event;
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
        goldsrc::kReferenceGoldSrcButtonAttack2 |
        goldsrc::kReferenceGoldSrcButtonJump | goldsrc::kReferenceGoldSrcButtonDuck |
        goldsrc::kReferenceGoldSrcButtonUse | goldsrc::kReferenceGoldSrcButtonReload;
    if (!canonical_dead_ && (command.buttons & action_buttons) == 0U)
      life_release_barrier_ = false;
    diagnostic_rejection(command,canonical_dead_ ? "local_dead" : "local_release_required");
    return; // presentation only; immutable committed wire command is unchanged
  }
  const bool reload = (command.buttons & goldsrc::kReferenceGoldSrcButtonReload) != 0U;
  const bool reload_edge = reload && !reload_button_held_; reload_button_held_ = reload;
  if (!supported()) {diagnostic_rejection(command,canonical_dead_ ? "local_dead" : "local_model_or_state_unavailable");return;}
  const auto actions_before=counters_.actions_started;
  const auto at = std::clamp(command.sample_end_seconds,std::max(0.0,now - 0.25),now);
  const bool attack = (command.buttons & goldsrc::kReferenceGoldSrcButtonAttack) != 0U;
  const bool secondary=(command.buttons & goldsrc::kReferenceGoldSrcButtonAttack2)!=0U;
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
  // SDK ItemPostFrame gives eligible attack2/attack precedence, then retries
  // held R each command. Reload itself waits only for the player's timer.
  const bool conventional_reload_request=reload && timer_ready(next_client_attack_) &&
      !(secondary && timer_ready(next_secondary_attack_)) &&
      !(attack && timer_ready(next_primary_attack_));
  const bool reload_can_replace=!pending || (action_ &&
      (bullet_fire(action_->identity.kind) ||
       (mp5_selected_ && action_->identity.kind==LocalWeaponAction::secondary_launch)));
  const auto diagnostic_no_action=[&] {
    if(counters_.actions_started!=actions_before)return;
    const auto mode=secondary ? WeaponDiagnosticMode::secondary : attack ? WeaponDiagnosticMode::primary : WeaponDiagnosticMode::reload;
    const auto* capability=weapon_id_ ? weapon_diagnostic_capability(*weapon_id_) : nullptr;
    // These are observed local gates, not a claim that the server rejected a command.
    if(capability && capability->modes[static_cast<std::size_t>(mode)]==core::MultiplayerActionKind::unknown)
      diagnostic_rejection(command,"local_mode_unsupported");
    else if(in_reload_==true || reload_visual)diagnostic_rejection(command,"local_reload_gate");
    else if(!timer_ready(next_client_attack_) || !timer_ready(secondary ? next_secondary_attack_ : next_primary_attack_))
      diagnostic_rejection(command,"local_attack_timer_gate");
    else if((clip_ && *clip_==0 && !secondary) || (reserve_ && *reserve_==0 && clip_==-1) ||
        (secondary && mp5_selected_ && secondary_reserve_==0))diagnostic_rejection(command,"local_ammo_gate");
    else if(pending)diagnostic_rejection(command,"local_pending_action_gate");
    else diagnostic_rejection(command,"local_action_not_accepted");
  };
  if(snark_selected_) {
    if(command.sample_end_seconds>now || now-command.sample_end_seconds>.25) {counters_.error=LocalWeaponPresentationError::invalid_time;return;}
    submit_snark(command,ready);if(!snark_attempt_count_)diagnostic_no_action();return;
  }
  if(tripmine_selected_) {if(!secondary)submit_tripmine(command,ready);if(!tripmine_attempt_)diagnostic_no_action();return;}
  if(satchel_selected_) {
    if(command.sample_end_seconds>now) {counters_.error=LocalWeaponPresentationError::invalid_time;return;}
    submit_satchel(command,ready,timer_ready(next_secondary_attack_) && timer_ready(next_client_attack_));
  } else if(hand_grenade_selected_) {
    if(command.sample_end_seconds>now) {counters_.error=LocalWeaponPresentationError::invalid_time;return;}
    submit_hand_grenade(command,ready);
  } else if(hornet_selected_) {
    expire_hornet_reservations(now);
    const auto reserved=static_cast<std::size_t>(std::count_if(hornet_receipts_.begin(),hornet_receipts_.end(),
        [](const auto& receipt){return receipt && receipt->reserved;}));
    // The server's biological Reload is not the R-key magazine action. No
    // provisional regeneration, empty auto-switch, or negative reserve here.
    if(clip_!=-1 || !reserve_ || *reserve_>hornet_maximum_ammo || *reserve_<=reserved ||
        (in_reload_ && *in_reload_) || !timer_ready(next_client_attack_)) {diagnostic_no_action();return;}
    if(secondary && timer_ready(next_secondary_attack_) && at+1e-8>=hornet_secondary_ready_at_)
      start_hornet(HornetFireMode::fast,command.sequence,at);
    else if(attack && !secondary && ready && at+1e-8>=hornet_primary_ready_at_)
      start_hornet(HornetFireMode::track,command.sequence,at);
  } else if(shotgun_selected_) {
    if(special_reload_state_ && (*special_reload_state_<0 || *special_reload_state_>2)) {
      counters_.error=LocalWeaponPresentationError::unsupported_pose;
      return; // Unknown negotiated stage cannot authorize a guessed successful action.
    }
    const bool fire_pending=pending && bullet_fire(action_->identity.kind);
    const bool cycle_ready=at-last_primary_fire_at_>=shotgun_fire_cycle_seconds_-1e-8;
    const auto available=clip_ ? *clip_-static_cast<std::int32_t>(provisional_rounds_) : -1;
    // Secondary has the reference ItemPostFrame priority. A one-shell double
    // request cannot silently fall through to a successful primary shot.
    const bool double_ready=secondary && available>=2 &&
        timer_ready(next_secondary_attack_) && timer_ready(next_client_attack_);
    const bool single_ready=attack && !secondary && available>=1 && ready;
    if((double_ready || single_ready) && clip_ && *clip_<=8 && water_level_!=3U &&
        (!in_reload_ || !*in_reload_) && (!pending || fire_pending ||
          action_->identity.kind==LocalWeaponAction::reload) && cycle_ready) {
      // Firing legally interrupts observed special reload; no locally filled
      // clip or hidden insert survives this action transition.
      start(double_ready ? LocalWeaponAction::secondary_fire : LocalWeaponAction::primary_fire,
          command.sequence,at,double_ready ? 2U : 1U,2U,
          at+(double_ready ? 2.0 : 1.25));
    } else if((reload_edge || ((attack || secondary) && available<(secondary ? 2 : 1))) &&
        clip_ && *clip_>=0 && *clip_<8 && reserve_ && *reserve_>0U &&
        (!special_reload_state_ || *special_reload_state_==0) &&
        (!in_reload_ || !*in_reload_) && !pending && !reload_visual && ready && cycle_ready)
      start(LocalWeaponAction::reload,command.sequence,at,5U,0U,at+1.5);
  } else if (rpg_selected_) {
    if(attack && !secondary && clip_==1 && (!in_reload_ || !*in_reload_) &&
        !pending && !reload_visual && ready && at-last_primary_fire_at_>=1.5-1e-8)
      start(LocalWeaponAction::primary_fire,command.sequence,at,3U,1U,at+2.0);
    else if((reload_edge || (attack && clip_==0)) && clip_==0 && reserve_ && *reserve_>0 &&
        (!in_reload_ || !*in_reload_) && !pending && !reload_visual && ready &&
        rpg_state_.can_reload_or_holster())
      start(LocalWeaponAction::reload,command.sequence,at,2U,0U,at+2.0);
  } else if (crossbow_selected_) {
    const auto reserved_shot=action_ && bullet_fire(action_->identity.kind) &&
        action_->clip_before==clip_ ? 1 : 0;
    const auto available=clip_.value_or(-1)-reserved_shot;
    if(attack && !secondary && clip_ && *clip_>0 && *clip_<=5 && reserve_ &&
        (!in_reload_ || !*in_reload_) && !pending && !reload_visual && ready &&
        at-last_primary_fire_at_>=.75-1e-8)
      start_crossbow(command.sequence,at,*clip_-1,*reserve_,crossbow_sniper_);
    else if((conventional_reload_request || (attack && clip_==0 && ready &&
        at-last_primary_fire_at_>=.75-1e-8)) && clip_ && *clip_>=0 && *clip_<=5 && available<5 &&
        reserve_ && *reserve_>0 && (!in_reload_ || !*in_reload_) && reload_can_replace && !reload_visual)
      start(LocalWeaponAction::reload,command.sequence,at,7U,0U,at+4.5);
  } else if (python_selected_) {
    const auto body=static_cast<std::uint8_t>(maximum_clients_>1U ? 1U : 0U);
    if(attack && !secondary && clip_ && *clip_>0 && *clip_<=6 && water_level_!=3U &&
        (!in_reload_ || !*in_reload_) && !pending && !reload_visual && ready &&
        at-last_primary_fire_at_>=0.75-1e-8)
      start(LocalWeaponAction::primary_fire,command.sequence,at,2U,body,at+1.25);
    else if((reload_edge || (attack && clip_==0)) && clip_ && *clip_>=0 && *clip_<6 &&
        reserve_ && *reserve_>0 && (!in_reload_ || !*in_reload_) && !pending &&
        !reload_visual && ready && at-last_primary_fire_at_>=0.75-1e-8)
      start(LocalWeaponAction::reload,command.sequence,at,3U,body,at+2.0);
  } else if (*weapon_id_ == 2U) {
    const bool secondary_ready=secondary && timer_ready(next_secondary_attack_) && timer_ready(next_client_attack_);
    // Preserve SDK secondary priority, shared previous-shot cycle and server
    // timer eligibility without consuming canonical clip/ammo locally.
    if ((secondary_ready || (attack && ready)) && clip_ && *clip_ > 0 && *clip_ <= 17 &&
        (!in_reload_ || !*in_reload_) && !pending && !reload_visual &&
        at - last_primary_fire_at_ >= glock_fire_cycle_seconds_ -
            (glock_fire_cycle_seconds_==glock_secondary_cycle_seconds ? 1e-8 : 0.0))
      start(LocalWeaponAction::primary_fire,command.sequence,at,
          secondary_ready ? glock_secondary_sequence : *clip_ == 1 ? 4U : 3U,
          2U,at + 0.75,secondary_ready);
    else if (reload_edge && clip_ && *clip_ >= 0 && *clip_ < 17 && reserve_ && *reserve_ > 0U &&
        (!in_reload_ || !*in_reload_) && !pending && !reload_visual && ready)
      start(LocalWeaponAction::reload,command.sequence,at,*clip_ == 0 ? 5U : 6U,0U,at + 2.5);
  } else if(mp5_selected_) {
    const bool primary_pending=pending && action_->identity.kind==LocalWeaponAction::primary_fire;
    const bool launch_ready=at-last_secondary_launch_at_>=1.0-1e-8;
    if(secondary && secondary_reserve_ && *secondary_reserve_>provisional_grenades_ &&
        water_level_!=3U && (!in_reload_ || !*in_reload_) && (!pending || primary_pending) && !pending_launch_ &&
        !reload_visual && launch_ready && at-last_primary_fire_at_>=0.1-1e-8 &&
        timer_ready(next_secondary_attack_) && timer_ready(next_client_attack_))
      start(LocalWeaponAction::secondary_launch,command.sequence,at,2U,2U,at+1.5);
    else if(attack && !secondary && launch_ready && clip_ && *clip_>static_cast<std::int32_t>(provisional_rounds_) && *clip_<=50 &&
        (!in_reload_ || !*in_reload_) && (!pending || primary_pending) && !reload_visual &&
        at-last_primary_fire_at_>=0.1-1e-8 && ready) {
      LocalWeaponActionIdentity key;key.generation=generation_;key.command_sequence=command.sequence;
      start(LocalWeaponAction::primary_fire,command.sequence,at,5U+firearm_variant(key)%3U,2U,at+0.75);
    } else if(conventional_reload_request && clip_ && *clip_>=0 && *clip_<=50 &&
        *clip_-static_cast<std::int32_t>(provisional_rounds_)<50 && reserve_ && *reserve_>0 &&
        (!in_reload_ || !*in_reload_) && reload_can_replace && !reload_visual)
      start(LocalWeaponAction::reload,command.sequence,at,3U,0U,at+1.5);
  } else if (*weapon_id_ == 1U && attack && !pending && at - last_melee_swing_at_ >= 0.5 && ready)
    start(LocalWeaponAction::melee_swing,command.sequence,at,misses[crowbar_cycle_],1U,at + 0.8);
  diagnostic_no_action();
}
void LocalWeaponPresentationController::configure_scripted_events(
    std::span<const ScriptedEventBinding> bindings) noexcept {
  secondary_event_index_.reset();
  glock_secondary_event_index_.reset();
  shotgun_single_event_index_.reset();shotgun_double_event_index_.reset();
  python_event_index_.reset();
  crossbow_event_index_.reset();crossbow_sniper_event_index_.reset();
  rpg_event_index_.reset();
  hornet_event_index_.reset();
  tripmine_event_index_.reset();
  snark_event_index_.reset();
  for(const auto& binding:bindings) {
    const auto end=std::find(binding.name.begin(),binding.name.end(),'\0');
    const std::string_view name{binding.name.data(),static_cast<std::size_t>(end-binding.name.begin())};
    const bool glock_secondary=name=="events/glock2.sc";
    const bool shotgun_single=name=="events/shotgun1.sc";
    const bool shotgun_double=name=="events/shotgun2.sc";
    const bool python=name=="events/python.sc";
    const bool crossbow=name=="events/crossbow1.sc", sniper=name=="events/crossbow2.sc";
    const bool rpg=name=="events/rpg.sc";
    const bool hornet=name=="events/firehornet.sc";
    const bool tripmine=name=="events/tripfire.sc";
    const bool snark=name=="events/snarkfire.sc";
    if(name!="events/mp52.sc" && !glock_secondary && !shotgun_single && !shotgun_double && !python && !crossbow && !sniper && !rpg && !hornet && !tripmine && !snark) continue;
    auto& selected=snark ? snark_event_index_ : tripmine ? tripmine_event_index_ : hornet ? hornet_event_index_ : rpg ? rpg_event_index_ : crossbow ? crossbow_event_index_ : sniper ? crossbow_sniper_event_index_ : python ? python_event_index_ : shotgun_single ? shotgun_single_event_index_ : shotgun_double ?
        shotgun_double_event_index_ : glock_secondary ? glock_secondary_event_index_ : secondary_event_index_;
    if(!binding.index || binding.index>1023U || selected) {
      rpg_event_index_.reset();secondary_event_index_.reset();glock_secondary_event_index_.reset();
      hornet_event_index_.reset();
      tripmine_event_index_.reset();
      snark_event_index_.reset();
      python_event_index_.reset();
      crossbow_event_index_.reset();crossbow_sniper_event_index_.reset();
      shotgun_single_event_index_.reset();shotgun_double_event_index_.reset();return;
    }
    // A conflicting assigned index is never a valid semantic binding.
    if(std::count_if(bindings.begin(),bindings.end(),[&](const auto& other) {
        return other.index==binding.index;
      })!=1) {rpg_event_index_.reset();secondary_event_index_.reset();glock_secondary_event_index_.reset();
        hornet_event_index_.reset();
        tripmine_event_index_.reset();
        snark_event_index_.reset();
        python_event_index_.reset();
        crossbow_event_index_.reset();crossbow_sniper_event_index_.reset();
        shotgun_single_event_index_.reset();shotgun_double_event_index_.reset();return;}
    selected=binding.index;
  }
}
void LocalWeaponPresentationController::committed_scripted_event(
    const CommittedScriptedEvent& event,std::optional<std::uint32_t> receiving) noexcept {
  if(snark_event_index_ && event.event_index==*snark_event_index_) {
    committed_snark_event(event,receiving);return;
  }
  if(tripmine_event_index_ && event.event_index==*tripmine_event_index_) {
    committed_tripmine_event(event,receiving);return;
  }
  if(hornet_event_index_ && event.event_index==*hornet_event_index_) {
    committed_hornet_event(event,receiving);return;
  }
  const bool glock_secondary=glock_secondary_event_index_ && event.event_index==*glock_secondary_event_index_;
  const bool mp5_secondary=secondary_event_index_ && event.event_index==*secondary_event_index_;
  const bool shotgun_single=shotgun_single_event_index_ && event.event_index==*shotgun_single_event_index_;
  const bool shotgun_double=shotgun_double_event_index_ && event.event_index==*shotgun_double_event_index_;
  const bool shotgun=shotgun_single || shotgun_double;
  const bool python=python_event_index_ && event.event_index==*python_event_index_;
  const bool crossbow_launch=crossbow_event_index_ && event.event_index==*crossbow_event_index_;
  const bool crossbow_sniper=crossbow_sniper_event_index_ && event.event_index==*crossbow_sniper_event_index_;
  const bool crossbow=crossbow_launch || crossbow_sniper;
  const bool rpg=rpg_event_index_ && event.event_index==*rpg_event_index_;
  if(crossbow && (!event.integer_parameter1 || !event.integer_parameter2 ||
      *event.integer_parameter1<0 || *event.integer_parameter1>5 ||
      *event.integer_parameter2<0 || *event.integer_parameter2>50)) return;
  if(!receiving || event.emitter_entity!=*receiving || (!glock_secondary && !mp5_secondary && !shotgun && !python && !crossbow && !rpg) ||
      event.generation!=generation_ ||
      !event.record || !event.record_ordinal || life_release_barrier_ ||
      event.resolution!=ScriptedEventResolution::ready || !supported() ||
      (rpg ? !rpg_selected_ : crossbow ? !crossbow_selected_ : python ? !python_selected_ : shotgun ? !shotgun_selected_ : glock_secondary ? weapon_id_!=2U : !mp5_selected_) ||
      !finite_time(event.received_at_seconds) ||
      std::abs(last_time_-event.received_at_seconds)>0.25 ||
      (event.record_ordinal && event.record_ordinal<model_bound_record_ordinal_)) return;
  for(const auto& seen:launch_events_) if(seen.record==event.record &&
      seen.cursor==event.message_bit_offset && seen.ordinal==event.entry_ordinal) {
    ++counters_.duplicate_events;return;
  }
  launch_events_[launch_event_next_++%launch_events_.size()]={event.record,event.message_bit_offset,event.entry_ordinal};
  // A local server echo acknowledges the already started command. It cannot
  // restart the animation, repeat recoil/sound or create a projectile.
  const auto relates=[&](const ActionState& action) {
    return ((rpg || python || crossbow) ? !action.scripted_event_confirmed && action.identity.kind==LocalWeaponAction::primary_fire : shotgun ? !action.scripted_event_confirmed &&
        action.identity.kind==(shotgun_double ? LocalWeaponAction::secondary_fire : LocalWeaponAction::primary_fire) :
        glock_secondary ? action.identity.kind==LocalWeaponAction::primary_fire && action.glock_secondary :
        action.identity.kind==LocalWeaponAction::secondary_launch) &&
        event.received_at_seconds+0.001>=action.identity.started_at_seconds &&
        event.received_at_seconds<=action.deadline &&
        (!event.record_ordinal || event.record_ordinal>action.pre_record_ordinal);
  };
  if(action_ && relates(*action_)) {
    if(shotgun || python || crossbow || rpg) action_->scripted_event_confirmed=true;
    if(crossbow) {
      action_->crossbow_sniper=crossbow_sniper;
      if(crossbow_sniper) recoil_active_=false;
      if(const auto role=crossbow_fire_sequence(*event.integer_parameter1,*event.integer_parameter2))
        action_->visual.sequence=*role; // post-shot event ammo wins without an echo restart
    }
    action_->visual.source=LocalWeaponAnimationSource::service_event;
    finish_pending(LocalWeaponActionStatus::server_confirmed);return;
  }
  if(shotgun || crossbow) for(auto& pending:pending_fire_) if(pending && relates(*pending)) {
    if(pending->status==LocalWeaponActionStatus::predicted_pending) {
      ++counters_.actions_confirmed;if(!pending->crossbow_sniper)++counters_.recoil_confirmed;
      if(shotgun_double) ++counters_.secondary_fire_confirmed;else ++counters_.primary_fire_confirmed;
    }
    if(crossbow) {
      pending->status=LocalWeaponActionStatus::server_confirmed;
      pending->scripted_event_confirmed=true;
    } else pending.reset();
    return;
  }
  if(mp5_secondary && pending_launch_ && relates(*pending_launch_)) {
    if(pending_launch_->status==LocalWeaponActionStatus::predicted_pending) {
      ++counters_.actions_confirmed;++counters_.secondary_launch_confirmed;++counters_.recoil_confirmed;
    }
    pending_launch_.reset();return;
  }
  if((crossbow || mp5_secondary) && reload_predecessor_active(event.received_at_seconds))
    return;
  if(python && action_ && action_->identity.kind==LocalWeaponAction::primary_fire &&
      event.received_at_seconds-action_->identity.started_at_seconds<=1.25) return;
  if(crossbow && action_ && action_->scripted_event_confirmed &&
      event.received_at_seconds-action_->identity.started_at_seconds<.75) return;
  if(rpg && event.received_at_seconds-last_primary_fire_at_<1.5) return;
  // Exact committed local event is sufficient presentation evidence even when
  // CLIENT_WEAPONS did not provisionally start this command. Projectile and
  // gameplay authority still remain exclusively server-owned.
  if(action_ && action_->status==LocalWeaponActionStatus::predicted_pending) return;
  if(crossbow) {
    start_crossbow(0U,event.received_at_seconds,*event.integer_parameter1,*event.integer_parameter2,crossbow_sniper);
    if(action_) {action_->scripted_event_confirmed=true;action_->visual.source=LocalWeaponAnimationSource::service_event;
      finish_pending(LocalWeaponActionStatus::server_confirmed);}
    return;
  }
  start((rpg || python) ? LocalWeaponAction::primary_fire : shotgun ? shotgun_double ? LocalWeaponAction::secondary_fire : LocalWeaponAction::primary_fire :
      glock_secondary ? LocalWeaponAction::primary_fire : LocalWeaponAction::secondary_launch,
      0U,event.received_at_seconds,rpg ? 3U : shotgun ? shotgun_double ? 2U : 1U :
      glock_secondary ? glock_secondary_sequence : 2U,rpg ? 1U : python ? static_cast<std::uint8_t>(maximum_clients_>1U) : 2U,
      event.received_at_seconds+(rpg ? 2.0 : shotgun ? shotgun_double ? 2.0 : 1.25 :
          glock_secondary ? 0.75 : 1.5),glock_secondary);
  if(action_) {
    if(shotgun || python || rpg) action_->scripted_event_confirmed=true;
    action_->visual.source=LocalWeaponAnimationSource::service_event;
    finish_pending(LocalWeaponActionStatus::server_confirmed);
  }
}
void LocalWeaponPresentationController::committed_hornet_event(
    const CommittedScriptedEvent& event,std::optional<std::uint32_t> receiving) noexcept {
  if(!receiving || event.emitter_entity!=*receiving || event.generation!=generation_ ||
      event.record_ordinal>std::numeric_limits<std::size_t>::max() ||
      !event.record || !event.record_ordinal || life_release_barrier_ || !hornet_selected_ ||
      event.resolution!=ScriptedEventResolution::ready || !supported() ||
      !finite_time(event.received_at_seconds) || std::abs(last_time_-event.received_at_seconds)>.25 ||
      event.record_ordinal<model_bound_record_ordinal_) return;
  const auto mode=event.integer_parameter1 ? hornet_fire_mode(*event.integer_parameter1) : std::nullopt;
  if(!mode) {counters_.error=LocalWeaponPresentationError::unsupported_event;return;}
  for(const auto& seen:launch_events_) if(seen.record==event.record &&
      seen.cursor==event.message_bit_offset && seen.ordinal==event.entry_ordinal) {
    ++counters_.duplicate_events;return;
  }
  launch_events_[launch_event_next_++%launch_events_.size()]={event.record,event.message_bit_offset,event.entry_ordinal};
  const auto kind=*mode==HornetFireMode::fast ? LocalWeaponAction::secondary_fire : LocalWeaponAction::primary_fire;
  HornetReceipt* oldest=nullptr;
  for(auto& receipt:hornet_receipts_) if(receipt && !receipt->event_confirmed &&
      receipt->identity.kind==kind && event.record_ordinal>receipt->pre_record &&
      event.received_at_seconds+.001>=receipt->identity.started_at_seconds &&
      event.received_at_seconds<=receipt->deadline+.75 &&
      (!oldest || receipt->identity.started_at_seconds<oldest->identity.started_at_seconds)) oldest=&*receipt;
  if(oldest) {
    oldest->event_confirmed=true;
    oldest->confirmation_source=client::RuntimeObservationSource{.record_identity=event.record,
        .record_ordinal=static_cast<std::size_t>(event.record_ordinal),.start_bit_offset=event.message_bit_offset};
    if(action_ && action_->identity==oldest->identity) {
      action_->scripted_event_confirmed=true;action_->visual.source=LocalWeaponAnimationSource::service_event;
      finish_pending(LocalWeaponActionStatus::server_confirmed);
    } else if(oldest->status==LocalWeaponActionStatus::predicted_pending) {
      oldest->status=LocalWeaponActionStatus::server_confirmed;
      ++counters_.actions_confirmed;++counters_.recoil_confirmed;
      if(kind==LocalWeaponAction::secondary_fire) ++counters_.secondary_fire_confirmed;
      else ++counters_.primary_fire_confirmed;
    }
    release_confirmed_hornet_reservations();
    return; // This includes ammo-confirmed and completed visual receipts.
  }
  // A committed-only fire is presentation evidence. It does not spawn a world
  // entity/trail, decrement ammo, choose a target or infer an impact.
  start_hornet(*mode,0U,event.received_at_seconds,true);
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
  server_reload_predecessor_deadline_.reset();
  if(snark_selected_) {
    snark_attempt_count_=0;snark_attempts_={};snark_release_required_=true;
    for(auto& r:snark_receipts_)if(r)r->reserved=false;
    snark_.reserved_ammo=0;
  }
  if(tripmine_selected_) {
    tripmine_attempt_.reset();tripmine_release_required_=true;
    for(auto& r:tripmine_receipts_) if(r)r->reserved=false;
    tripmine_.reserved_ammo=0;
  }
  audio_.cancel();
  finish_pending(LocalWeaponActionStatus::server_rejected); reload_button_held_ = false;
  pending_fire_.fill(std::nullopt);provisional_rounds_=0;
  pending_launch_.reset();provisional_grenades_=0;
  if(satchel_selected_) {
    satchel_.charge_ready=satchel_.server_charge_ready;satchel_.reserved_ammo=0;
    satchel_receipts_.fill(std::nullopt);satchel_.retired=false;satchel_release_required_=true;
    action_.reset();server_visual_.reset();
    if(satchel_.charge_ready) (void)select_satchel_model(*satchel_.charge_ready!=0);
  }
  if(hand_grenade_selected_) {
    if(hand_grenade_.throw_time && hand_grenade_can_holster()) {
      // Focus loss can cancel transient drawing, not the already submitted
      // throw occurrence. Retain its bounded timestamp/reservation so a late
      // positive server prime cannot resurrect it and cause a second throw.
      action_.reset();server_visual_.reset();
    } else if(!hand_grenade_server_primed_ || hand_grenade_can_holster()) {
      clear_hand_grenade_state();action_.reset();server_visual_.reset();
    }
    hand_grenade_release_required_=true;
  }
  // Focus/input cancellation is not a new life/model/network generation.
  // Retain echo tombstones and accepted cooldowns; releasing the mouse must
  // not turn an already audible action's later confirmation into a new shot.
  for(auto& receipt:hornet_receipts_) if(receipt) {
    receipt->reserved=false;
    if(receipt->status==LocalWeaponActionStatus::predicted_pending)
      receipt->status=LocalWeaponActionStatus::server_rejected;
  }
}
std::optional<LocalWeaponActionIdentity>
LocalWeaponPresentationController::current_action_identity() const noexcept {
  return action_ ? std::optional{action_->identity} : std::nullopt;
}
LocalWeaponPresentationSnapshot LocalWeaponPresentationController::sample(double now) noexcept {
  if (!advance_time(now)) return counters_;
  if(hornet_selected_) expire_hornet_reservations(now);
  if(tripmine_selected_) expire_tripmine_receipts(now);
  if(snark_selected_) expire_snark_receipts(now);
  for(auto& pending:pending_fire_) if(pending && now>=pending->deadline) {
    if(pending->status==LocalWeaponActionStatus::predicted_pending) {
      ++counters_.actions_rejected;if(!pending->crossbow_sniper)++counters_.recoil_rejected;
      provisional_rounds_-=std::min(provisional_rounds_,static_cast<std::size_t>(pending->rounds_reserved));
    }
    pending.reset();
  }
  if(pending_launch_ && now>=pending_launch_->deadline) {
    if(pending_launch_->status==LocalWeaponActionStatus::predicted_pending) {
      ++counters_.actions_rejected;++counters_.recoil_rejected;
    }
    pending_launch_.reset();if(provisional_grenades_) --provisional_grenades_;
  }
  if (!hand_grenade_selected_ && !satchel_selected_ && !snark_selected_ && action_ && action_->status == LocalWeaponActionStatus::predicted_pending && now >= action_->deadline) {
    if((mp5_selected_ || shotgun_selected_) && bullet_fire(action_->identity.kind))
      provisional_rounds_-=std::min(provisional_rounds_,static_cast<std::size_t>(action_->rounds_reserved));
    if(action_->identity.kind==LocalWeaponAction::secondary_launch && provisional_grenades_) --provisional_grenades_;
    finish_pending(LocalWeaponActionStatus::timed_out);
  }
  auto result = counters_;
  if (action_) {
    result.action = action_->identity.kind; result.identity = action_->identity;
    result.status = action_->status; result.visual = action_->visual;
  } else result.visual = server_visual_;
  if(hand_grenade_waiting_retirement()) result.visual.reset();
  if(satchel_waiting_retirement()) result.visual.reset();
  if(tripmine_waiting_retirement()) result.visual.reset();
  if(snark_waiting_retirement()) result.visual.reset();
  audio_.update(result,model_,now,!canonical_dead_ && supported());
  if (result.visual && valid_pose(result.visual->sequence,result.visual->body)) {
    const auto& data = model_->sequences[result.visual->sequence];
    const auto frame = std::max(0.0,now - result.visual->started_at_seconds) * data.fps;
    const auto terminal = static_cast<double>(data.frame_count - 1U);
    result.animation_completed = !data.looping && frame >= terminal;
    result.frame_coordinate = data.looping && terminal > 0.0 ? std::fmod(frame,terminal) : std::min(frame,terminal);
    const bool special_reload_pose=shotgun_selected_ && special_reload_state_ &&
        *special_reload_state_>0 && *special_reload_state_<=2 &&
        (result.visual->sequence==3U || result.visual->sequence==5U);
    const bool grenade_hold=hand_grenade_selected_ && hand_grenade_.phase!=HandGrenadePhase::ready;
    const bool satchel_transition=satchel_selected_ && satchel_.charge_ready==2;
    const bool snark_hold=snark_selected_ && snark_recovery_at_.has_value();
    if (result.animation_completed && !idle(*weapon_id_,result.visual->sequence) && !special_reload_pose && !grenade_hold && !satchel_transition && !snark_hold) {
      // The old retained idle may predate this action. Reusing its restart
      // token produced frame zero on this sample, then another frame zero on
      // every subsequent sample while action_ remained at its terminal pose.
      // A completed action crosses to exactly one new idle timeline; a newer
      // server-owned idle event keeps its own occurrence identity instead.
      const bool newer_server_idle = server_visual_ &&
          idle(*weapon_id_,server_visual_->sequence) &&
          server_visual_->started_at_seconds >= result.visual->started_at_seconds;
      if((mp5_selected_ || shotgun_selected_) && action_ && action_->status==LocalWeaponActionStatus::predicted_pending &&
          bullet_fire(action_->identity.kind)) {
        auto& previous=pending_fire_[next_pending_fire_++%pending_fire_.size()];
        if(previous) {++counters_.actions_rejected;++counters_.recoil_rejected;
          provisional_rounds_-=std::min(provisional_rounds_,static_cast<std::size_t>(previous->rounds_reserved));}
        previous=action_;
      }
      if(action_ && action_->status==LocalWeaponActionStatus::predicted_pending &&
          action_->identity.kind==LocalWeaponAction::secondary_launch)
        pending_launch_=action_;
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
    const auto magnitude = std::max(0.0,(20.0+std::abs(recoil_impulse_)) * std::pow(0.99,ticks) * (1.0 - fraction * 0.5) - 20.0);
    result.local_punch_pitch_degrees = std::copysign(magnitude,recoil_impulse_);
    counters_.maximum_visual_recoil = std::max(counters_.maximum_visual_recoil,magnitude);
    result.maximum_visual_recoil = counters_.maximum_visual_recoil;
    if (magnitude == 0.0) recoil_active_ = false;
  }
  return result;
}
} // namespace hlclient::games::halflife

#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/core/log.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>
#include <utility>
namespace hlclient::game_api {
namespace {
using DiagnosticKind=core::MultiplayerActionKind;
using DiagnosticPhase=core::MultiplayerActionPhase;
void diagnostic_reason(core::MultiplayerActionPayload& payload,std::string_view reason) noexcept {
    std::copy_n(reason.begin(),std::min(reason.size(),payload.reason.size()-1),payload.reason.begin());
}
void diagnostic_shot(core::MultiplayerActionPayload& payload,const LocalWeaponSubmittedCommand& command) noexcept {
    payload.command_sample_seconds=command.sample_end_seconds;
    if(!command.shot_context)return;
    const auto& shot=*command.shot_context;
    payload.origin=std::array{shot.eye.x,shot.eye.y,shot.eye.z};
    payload.direction=std::array{shot.direction.x,shot.direction.y,shot.direction.z};
    payload.command_angles_degrees=shot.view_angles_degrees;payload.server_punch_degrees=shot.punch_angles_degrees;
    payload.player_origin=shot.player_origin;payload.player_velocity=shot.player_velocity;payload.player_ducked=shot.player_ducked;
}
}
void GameClientHost::set_diagnostic_sink(core::MultiplayerDiagnosticSink* sink) noexcept {
    if(!sink || diagnostic_sink_!=sink) {
        diagnostic_generations_={};diagnostic_bound_generations_.reset();
    }
    if(diagnostic_sink_==sink)return;
    diagnostic_sink_=sink;diagnostic_state_={};diagnostic_actions_={};diagnostic_action_next_=0;
    diagnostic_objects_={};diagnostic_entity_ordinal_=0;
    diagnostic_sources_={};diagnostic_source_next_=0;
    diagnostic_reject_reason_={};diagnostic_reject_ns_=0;diagnostic_reject_suppressed_=0;
    diagnostic_command_floor_=0;diagnostic_previous_buttons_=0;diagnostic_last_intent_ns_=0;
    module_->set_weapon_diagnostic_observer(sink && active_ ? this : nullptr);
}
void GameClientHost::set_diagnostic_generations(std::function<std::array<std::uint64_t,2>()> source) noexcept {
    diagnostic_generations_=std::move(source);
    diagnostic_bound_generations_.reset();
}
bool GameClientHost::weapon_diagnostics_enabled() const noexcept {
    return active_ && diagnostic_sink_ && diagnostic_sink_->enabled();
}
std::span<const WeaponDiagnosticCapability> GameClientHost::weapon_diagnostic_capabilities() const noexcept {
    const auto entries=module_->weapon_diagnostic_capabilities();
    return entries.size()<=32 ? entries : std::span<const WeaponDiagnosticCapability>{};
}
core::MultiplayerDiagnosticEvent GameClientHost::diagnostic_event() noexcept {
    core::MultiplayerDiagnosticEvent event;
    event.identity.producer=core::MultiplayerDiagnosticProducer::game_host;
    event.identity.producer_event_id=++diagnostic_serial_;
    event.identity.network_generation=diagnostic_identity_.network_generation;
    event.identity.map_generation=diagnostic_identity_.map_generation;
    if(diagnostic_generations_) {
        // Module epochs and session receipt map IDs are separate domains.
        // Never relabel a deferred game fact with an unrelated/current map.
        event.identity.map_generation=0;
        try {
            const auto owner=diagnostic_generations_();
            if(owner[0] && owner[0]==diagnostic_identity_.network_generation && owner[1]) {
                if(!diagnostic_bound_generations_)diagnostic_bound_generations_=owner;
                if(*diagnostic_bound_generations_==owner)event.identity.map_generation=owner[1];
            }
        } catch(...) {} // An unavailable owner is unknown, not the module map epoch.
    }
    if(diagnostic_state_.life)event.identity.local_life=diagnostic_state_.life;
    if(diagnostic_identity_.receiving_entity)event.identity.actor_entity=*diagnostic_identity_.receiving_entity;
    event.local_steady_ns=core::multiplayer_diagnostic_now_ns();
    event.provenance=core::MultiplayerProvenance::runtime_observed;
    return event;
}
void GameClientHost::weapon_action_observed(const WeaponActionDiagnosticObservation& observation) noexcept {
    if(!weapon_diagnostics_enabled() || observation.identity.generation!=diagnostic_identity_.network_generation)return;
    auto event=diagnostic_event();const auto& action=observation.identity;
    if(observation.phase==DiagnosticPhase::rejected) {
        const auto mode=static_cast<std::uint32_t>(observation.mode);
        if(std::string_view{diagnostic_reject_reason_.data()}==observation.reason && diagnostic_reject_weapon_==action.weapon_id && diagnostic_reject_mode_==mode &&
            event.local_steady_ns>=diagnostic_reject_ns_ && event.local_steady_ns-diagnostic_reject_ns_<250'000'000ULL) {
            if(diagnostic_reject_suppressed_<0xffffffffU)++diagnostic_reject_suppressed_;
            return;
        }
        diagnostic_reject_reason_.fill(0);
        std::copy_n(observation.reason.begin(),std::min(observation.reason.size(),diagnostic_reject_reason_.size()-1),diagnostic_reject_reason_.begin());
        diagnostic_reject_weapon_=action.weapon_id;diagnostic_reject_mode_=mode;
        diagnostic_reject_ns_=event.local_steady_ns;
    }
    event.identity.actor_model_revision=action.resource_revision;
    if(action.emitter_entity)event.identity.actor_entity=action.emitter_entity;
    if(action.command_sequence)event.identity.command_id=action.command_sequence;
    if(observation.related_command_sequence) {
        const DiagnosticActionReceipt* related=nullptr;bool ambiguous=false;
        for(const auto& receipt:diagnostic_actions_)if(receipt && receipt->accepted &&
            receipt->identity.generation==action.generation && receipt->identity.weapon_id==action.weapon_id &&
            receipt->identity.command_sequence==*observation.related_command_sequence) {
            if(related){ambiguous=true;break;}related=&*receipt;
        }
        if(related && !ambiguous)event.identity.related_action_id=related->id;
    }
    auto found=std::find_if(diagnostic_actions_.begin(),diagnostic_actions_.end(),[&](const auto& value) {
        return value && value->identity==action;
    });
    const bool accepted=observation.phase==DiagnosticPhase::accepted || observation.phase==DiagnosticPhase::begin;
    if(found==diagnostic_actions_.end() && accepted) {
        auto& receipt=diagnostic_actions_[diagnostic_action_next_++%diagnostic_actions_.size()];
        receipt=DiagnosticActionReceipt{action,++diagnostic_action_serial_,true};
        event.identity.action_id=receipt->id;
    } else if(found!=diagnostic_actions_.end()) {
        if(accepted && (*found)->accepted)return; // same exact action, not a position/time heuristic
        event.identity.action_id=(*found)->id;
    }
    core::MultiplayerActionPayload payload;
    payload.kind=observation.kind;payload.phase=observation.phase;payload.weapon_id=action.weapon_id;
    payload.mode=static_cast<std::uint32_t>(observation.mode);payload.emitter_count=observation.emitter_count;
    payload.clip_before=observation.clip;payload.reserve_before=observation.reserve;
    payload.reloading=observation.reloading;payload.next_attack_seconds=observation.next_attack;
    payload.action_started_seconds=action.started_at_seconds;
    if(observation.command && observation.command->generation==action.generation && observation.command->sequence==action.command_sequence) {
        diagnostic_shot(payload,*observation.command);event.edge=core::MultiplayerEdgeConfidence::exact;
    }
    if(accepted) {
        payload.eligible=true;
        event.knowledge=action.command_sequence ? core::MultiplayerKnowledge::local_action_accepted : core::MultiplayerKnowledge::cosmetic_feedback;
        diagnostic_reason(payload,action.command_sequence ? "local_presentation_action_accepted" : "server_derived_presentation_action");
    } else if(observation.phase==DiagnosticPhase::server_observed) {
        // Existing controller confirmation is a state/event-compatible match,
        // not a server echoed action ID or a damage/hit acknowledgement.
        event.edge=core::MultiplayerEdgeConfidence::candidate;
        event.knowledge=core::MultiplayerKnowledge::server_event_observed;
        diagnostic_reason(payload,"compatible_state_not_exact_action_echo");
    } else if(observation.phase==DiagnosticPhase::rejected) {
        payload.eligible=false;payload.suppressed_repetitions=diagnostic_reject_suppressed_;diagnostic_reject_suppressed_=0;
        event.knowledge=core::MultiplayerKnowledge::input_intent;
        diagnostic_reason(payload,observation.reason.empty() ? "local_action_not_accepted" : observation.reason);
    } else diagnostic_reason(payload,"local_presentation_lifecycle");
    event.payload=payload;(void)diagnostic_sink_->observe(event);
}
void GameClientHost::diagnostic_submit(const LocalWeaponSubmittedCommand& command) noexcept {
    if(!weapon_diagnostics_enabled() || command.generation!=diagnostic_identity_.network_generation ||
        !command.sequence || command.sequence<=diagnostic_command_floor_)return;
    diagnostic_command_floor_=command.sequence;
    constexpr std::uint16_t action_mask=1U|(1U<<11)|(1U<<13)|(1U<<5);
    const auto buttons=static_cast<std::uint16_t>(command.buttons&action_mask);
    const auto previous=diagnostic_previous_buttons_;diagnostic_previous_buttons_=buttons;
    if(!buttons && !previous)return;
    const auto now=core::multiplayer_diagnostic_now_ns();
    // Accepted automatic actions bypass this intent-only suppression entirely.
    if(buttons==previous && now>=diagnostic_last_intent_ns_ && now-diagnostic_last_intent_ns_<250'000'000ULL)return;
    diagnostic_last_intent_ns_=now;
    const auto entries=weapon_diagnostic_capabilities();
    const auto found=std::ranges::find(entries,diagnostic_state_.weapon.value_or(0),&WeaponDiagnosticCapability::weapon_id);
    for(const auto [bit,mode]:std::array<std::pair<std::uint16_t,WeaponDiagnosticMode>,3>{{
        {std::uint16_t{1},WeaponDiagnosticMode::primary},{std::uint16_t{1U<<11},WeaponDiagnosticMode::secondary},{std::uint16_t{1U<<13},WeaponDiagnosticMode::reload}}}) {
        if(!((buttons|previous)&bit))continue;
        auto event=diagnostic_event();event.identity.command_id=command.sequence;
        event.edge=core::MultiplayerEdgeConfidence::exact;event.knowledge=core::MultiplayerKnowledge::input_intent;
        core::MultiplayerActionPayload payload;payload.weapon_id=diagnostic_state_.weapon.value_or(0);
        payload.mode=static_cast<std::uint32_t>(mode);payload.phase=buttons&bit ? DiagnosticPhase::intent : DiagnosticPhase::end;
        if(found!=entries.end())payload.kind=found->modes[static_cast<std::size_t>(mode)];
        payload.clip_before=diagnostic_state_.clip;payload.reserve_before=mode==WeaponDiagnosticMode::secondary && diagnostic_state_.secondary_reserve ?
            diagnostic_state_.secondary_reserve : diagnostic_state_.reserve;
        payload.reloading=diagnostic_state_.reloading;
        payload.next_attack_seconds=mode==WeaponDiagnosticMode::secondary ? diagnostic_state_.secondary_timer : diagnostic_state_.primary_timer;
        if(diagnostic_state_.dead)payload.eligible=false;
        diagnostic_shot(payload,command);
        diagnostic_reason(payload,payload.kind==DiagnosticKind::unknown ? "mode_not_supported" :
            diagnostic_state_.dead ? "dead_input_not_action_acceptance" : "command_intent_not_server_execution");
        event.payload=payload;(void)diagnostic_sink_->observe(event);
    }
    if((buttons|previous)&(1U<<5)) {
        auto event=diagnostic_event();event.identity.command_id=command.sequence;
        event.edge=core::MultiplayerEdgeConfidence::exact;event.knowledge=core::MultiplayerKnowledge::input_intent;
        core::MultiplayerActionPayload p;p.kind=DiagnosticKind::use;p.phase=buttons&(1U<<5) ? DiagnosticPhase::intent : DiagnosticPhase::end;
        diagnostic_shot(p,command);diagnostic_reason(p,"use_command_not_trigger_confirmation");event.payload=p;(void)diagnostic_sink_->observe(event);
    }
}
void GameClientHost::diagnostic_observe(const client::RuntimeClientObservationState& state,double) noexcept {
    if(!weapon_diagnostics_enabled() || state.generation!=diagnostic_identity_.network_generation ||
        state.publication_revision<=diagnostic_state_.revision)return;
    const auto previous=diagnostic_state_;DiagnosticState next;
    next.weapon=state.weapon_hud.active_weapon_id;next.revision=state.publication_revision;
    next.life=state.lifecycle.life_epoch;next.dead=state.lifecycle.dead();next.source=state.client_metadata.source;
    if(state.receiving_client){next.health=state.receiving_client->health;next.fov=state.receiving_client->field_of_view;next.client_timer=state.receiving_client->next_weapon_attack;}
    if(next.weapon) {
        const auto slot=std::ranges::find(state.weapon_slots,std::optional<std::uint32_t>{*next.weapon},&client::RuntimeWeaponSlotObservation::weapon_id);
        if(slot!=state.weapon_slots.end()){next.clip=slot->clip;next.reloading=slot->in_reload;next.primary_timer=slot->next_primary_attack;next.secondary_timer=slot->next_secondary_attack;}
        if(!next.clip && *next.weapon<state.weapon_hud.clips.size())next.clip=state.weapon_hud.clips[*next.weapon];
        const auto type=std::ranges::find(state.weapon_hud.catalogue,*next.weapon,&client::RuntimeWeaponTypeObservation::id);
        if(type!=state.weapon_hud.catalogue.end()) {
            const auto ammo=[&](std::int8_t index)->std::optional<std::int32_t> {
                if(index<0 || static_cast<std::size_t>(index)>=state.weapon_hud.reserve_ammo.size())return {};
                if(const auto v=state.weapon_hud.reserve_ammo[index])return *v;return {};
            };
            next.reserve=ammo(type->primary_ammo_type);next.secondary_reserve=ammo(type->secondary_ammo_type);
        }
    }
    diagnostic_state_=next;
    const auto emit=[&](DiagnosticKind kind,WeaponDiagnosticMode mode,std::uint8_t weapon,std::string_view reason) {
        auto event=diagnostic_event();event.knowledge=core::MultiplayerKnowledge::server_event_observed;
        if(next.source){event.identity.source_record=next.source->record_identity;event.identity.source_record_ordinal=next.source->record_ordinal;}
        event.edge=core::MultiplayerEdgeConfidence::unknown;
        core::MultiplayerActionPayload p;p.kind=kind;p.phase=DiagnosticPhase::server_observed;p.mode=static_cast<std::uint32_t>(mode);p.weapon_id=weapon;
        p.clip_before=previous.clip;p.clip_after=next.clip;p.reserve_before=previous.reserve;p.reserve_after=next.reserve;p.reloading=next.reloading;
        diagnostic_reason(p,reason);event.payload=p;(void)diagnostic_sink_->observe(event);
    };
    if(previous.weapon!=next.weapon) {
        if(previous.weapon)emit(DiagnosticKind::holster,WeaponDiagnosticMode::holster,*previous.weapon,"authoritative_selection_changed");
        if(next.weapon)emit(DiagnosticKind::deploy,WeaponDiagnosticMode::deploy,*next.weapon,"authoritative_selection_changed");
    }
    if(next.weapon && previous.weapon==next.weapon && next.reloading!=previous.reloading && next.reloading)
        emit(DiagnosticKind::reload,WeaponDiagnosticMode::reload,*next.weapon,"authoritative_reload_state_transition");
    if(next.weapon && previous.weapon==next.weapon && next.fov!=previous.fov && next.fov) {
        const auto capabilities=weapon_diagnostic_capabilities();
        const auto capability=std::ranges::find(capabilities,*next.weapon,&WeaponDiagnosticCapability::weapon_id);
        const bool zoom=capability!=capabilities.end() && capability->modes[1]==DiagnosticKind::zoom;
        emit(zoom ? DiagnosticKind::zoom : DiagnosticKind::unknown,WeaponDiagnosticMode::secondary,*next.weapon,"authoritative_fov_transition");
    }
    if(next.reserve!=previous.reserve || next.clip!=previous.clip)emit(DiagnosticKind::unknown,WeaponDiagnosticMode::primary,next.weapon.value_or(0),"authoritative_ammo_not_target_damage");
    if(next.health!=previous.health || next.dead!=previous.dead || next.life!=previous.life) {
        auto event=diagnostic_event();core::MultiplayerLifecyclePayload p;
        p.kind=next.dead!=previous.dead ? next.dead ? core::MultiplayerLifecycleKind::death : core::MultiplayerLifecycleKind::respawn : core::MultiplayerLifecycleKind::health;
        if(previous.health && std::isfinite(*previous.health))p.before=static_cast<std::int32_t>(std::clamp(*previous.health,-1e6,1e6));
        if(next.health && std::isfinite(*next.health))p.after=static_cast<std::int32_t>(std::clamp(*next.health,-1e6,1e6));
        p.alive=!next.dead;event.payload=p;(void)diagnostic_sink_->observe(event);
    }
}
void GameClientHost::diagnostic_audio(const LocalAudioBatch& batch) noexcept {
    if(!weapon_diagnostics_enabled())return;
    for(std::size_t i=0;i<std::min(batch.count,batch.cues.size());++i) {
        const auto& cue=batch.cues[i];auto event=diagnostic_event();
        if(cue.command_sequence)event.identity.command_id=cue.command_sequence;
        event.knowledge=core::MultiplayerKnowledge::cosmetic_feedback;
        core::MultiplayerActionPayload p;p.phase=DiagnosticPhase::feedback;
        p.kind=cue.kind==LocalSoundKind::reload ? DiagnosticKind::reload : cue.kind==LocalSoundKind::deploy ? DiagnosticKind::deploy : DiagnosticKind::unknown;
        p.action_started_seconds=cue.scheduled_seconds;diagnostic_reason(p,"audio_cue_emitted_not_playback_proof");event.payload=p;
        (void)diagnostic_sink_->observe(event);
    }
    for(std::size_t i=0;i<std::min(batch.control_count,batch.controls.size());++i) {
        auto event=diagnostic_event();core::MultiplayerActionPayload p;p.kind=DiagnosticKind::unknown;p.phase=DiagnosticPhase::feedback;
        p.action_started_seconds=batch.controls[i].scheduled_seconds;
        diagnostic_reason(p,batch.controls[i].operation==LocalVoiceControl::Operation::stop ? "persistent_voice_stop" : "persistent_voice_control");
        event.knowledge=core::MultiplayerKnowledge::cosmetic_feedback;event.payload=p;(void)diagnostic_sink_->observe(event);
    }
}
bool GameClientHost::diagnostic_source_once(std::uint8_t kind,std::uint64_t record,std::uint64_t ordinal,
    std::uint64_t bit,std::uint32_t entry) noexcept {
    const DiagnosticSource source{kind,record,ordinal,bit,entry};
    if(std::ranges::any_of(diagnostic_sources_,[&](const auto& s){return s && *s==source;}))return false;
    diagnostic_sources_[diagnostic_source_next_++%diagnostic_sources_.size()]=source;return true;
}
void GameClientHost::diagnostic_entities(std::span<const client::RuntimePacketEntityObservation> entities,
    std::uint32_t maximum_clients,std::uint64_t ordinal) noexcept {
    if(!weapon_diagnostics_enabled() || !ordinal || ordinal<=diagnostic_entity_ordinal_)return;
    diagnostic_entity_ordinal_=ordinal;
    diagnostic_entity_models_.fill(0);
    for(const auto& entity:entities)if(entity.entity_number<diagnostic_entity_models_.size())
        diagnostic_entity_models_[entity.entity_number]=entity.model_index.value_or(0);
    const auto emit=[&](const DiagnosticObject& object,bool visible) {
        auto event=diagnostic_event();event.identity.actor_entity=object.entity;
        event.identity.actor_visual_lifetime=object.lifetime;event.identity.source_record_ordinal=ordinal;
        // An explicit owner is a server entity reference, not a shot association.
        if(object.owner)event.identity.related_actor_entity=*object.owner;
        event.knowledge=core::MultiplayerKnowledge::server_event_observed;
        core::MultiplayerLifecyclePayload p;
        p.kind=visible ? core::MultiplayerLifecycleKind::visibility_enter : core::MultiplayerLifecycleKind::visibility_leave;
        p.object_entity=object.entity;p.object_lifetime=object.lifetime;p.model_index=object.model;
        p.object_profile=static_cast<std::uint32_t>(object.profile);p.visible=visible;p.origin=object.origin;
        if(object.owner)p.owner_entity=*object.owner;
        event.payload=p;(void)diagnostic_sink_->observe(event);
    };
    for(auto& old:diagnostic_objects_)if(old) {
        if(old->entity>=diagnostic_entity_models_.size() || diagnostic_entity_models_[old->entity]!=old->model) {emit(*old,false);old.reset();}
    }
    std::uint32_t omitted=0;
    for(const auto& entity:entities) {
        if(entity.entity_number<=maximum_clients || entity.entity_number>=diagnostic_entity_models_.size() || !entity.model_index)continue;
        auto role=WeaponDiagnosticObjectProfile::unknown;
        if(entity.custom_beam && entity.custom_beam->complete && entity.custom_beam->supported)role=WeaponDiagnosticObjectProfile::beam;
        else {
            const auto resource=std::find_if(diagnostic_object_resources_.begin(),diagnostic_object_resources_.end(),
                [&](const auto& r){return r && r->model==*entity.model_index;});
            if(resource!=diagnostic_object_resources_.end())role=(*resource)->profile;
        }
        if(role==WeaponDiagnosticObjectProfile::unknown)continue;
        auto found=std::find_if(diagnostic_objects_.begin(),diagnostic_objects_.end(),[&](const auto& o){return o && o->entity==entity.entity_number;});
        bool fresh=found==diagnostic_objects_.end();
        if(fresh)found=std::find(diagnostic_objects_.begin(),diagnostic_objects_.end(),std::nullopt);
        if(found==diagnostic_objects_.end()){++omitted;continue;} // fixed visible-object budget
        const auto owner=entity.owner_entity_reference && *entity.owner_entity_reference>0 ? entity.owner_entity_reference : std::nullopt;
        const bool changed=!fresh && ((*found)->owner!=owner || (*found)->profile!=role);
        if(fresh)*found=DiagnosticObject{entity.entity_number,*entity.model_index,role,++diagnostic_object_lifetime_,ordinal,owner,{}};
        auto& object=**found;object.seen_ordinal=ordinal;object.owner=owner;object.profile=role;object.origin.reset();
        if(entity.origin.complete() && std::isfinite(*entity.origin.x) && std::isfinite(*entity.origin.y) && std::isfinite(*entity.origin.z) &&
            std::abs(*entity.origin.x)<=1e8 && std::abs(*entity.origin.y)<=1e8 && std::abs(*entity.origin.z)<=1e8)
            object.origin=std::array{static_cast<float>(*entity.origin.x),static_cast<float>(*entity.origin.y),static_cast<float>(*entity.origin.z)};
        if(fresh || changed)emit(object,true);
    }
    if(omitted) {
        auto event=diagnostic_event();event.identity.source_record_ordinal=ordinal;
        core::MultiplayerLifecyclePayload p;p.omitted_objects=omitted;
        event.payload=p;(void)diagnostic_sink_->observe(event);
    }
}
GameClientHost::ConsolePublicationBatch::ConsolePublicationBatch(GameClientHost* host) noexcept {
    if(host && !host->console_publication_deferred_) {
        host_=host;serial_=host->reset_serial();host->console_publication_deferred_=true;
    }
}
GameClientHost::ConsolePublicationBatch::~ConsolePublicationBatch() {
    if(host_ && serial_==host_->reset_serial())host_->finish_console_publication(accepted_);
}
void GameClientHost::finish_console_publication(bool accepted) noexcept {
    auto events=std::move(deferred_console_text_);const auto dropped=deferred_console_dropped_;
    deferred_console_text_.clear();deferred_console_bytes_=deferred_console_dropped_=0;
    console_publication_deferred_=false;
    if(!accepted || !active_)return;
    for(const auto& event:events) {
        if(event.origin==GameRecordState::ConsoleText::Origin::game_event)core::log_game_event(event.text);
        else core::log_server_text(event.text);
    }
    if(dropped)core::log(core::LogLevel::warning,"[console] source text publication budget exceeded");
}
GameClientHost::GameClientHost(std::unique_ptr<IGameClientModule> module)
    : module_(std::move(module)) {
    if (!module_) throw std::invalid_argument{"Game module is required"};
}
GameClientHost::~GameClientHost() { teardown(); }
std::span<const std::string_view> GameClientHost::effect_sprite_preparation() const noexcept {
    return active_ ? module_->effect_sprite_preparation() : std::span<const std::string_view>{};
}
void GameClientHost::reset(GameSessionIdentity identity) noexcept {
    finish_console_publication(false);
    console_text_high_water_.reset();
    temporary_delivery_={};
    follow_events_={};follow_generation_=identity.network_generation;
    ++audio_session_;
    // RuntimeReplaySession resets the game generation after the live loop has
    // established its steady-clock origin. That origin belongs to this host
    // lifetime, not to the network/map generation.
    diagnostic_identity_=identity;diagnostic_state_={};diagnostic_actions_={};diagnostic_action_next_=0;
    diagnostic_bound_generations_.reset();
    diagnostic_objects_={};diagnostic_object_resources_={};diagnostic_entity_ordinal_=0;
    diagnostic_sources_={};diagnostic_source_next_=0;
    diagnostic_reject_reason_={};diagnostic_reject_ns_=0;diagnostic_reject_suppressed_=0;
    diagnostic_command_floor_=0;diagnostic_previous_buttons_=0;diagnostic_last_intent_ns_=0;
    module_->reset(identity); active_ = true;
    module_->set_weapon_diagnostic_observer(diagnostic_sink_ ? this : nullptr);
}
void GameClientHost::teardown() noexcept {
    diagnostic_generations_={};diagnostic_bound_generations_.reset();
    finish_console_publication(false);
    console_text_high_water_.reset();
    follow_events_={};follow_generation_=0;
    ++audio_session_;
    movement_audio_time_origin_.reset();
    if (active_) {module_->set_weapon_diagnostic_observer(nullptr);module_->teardown(); active_ = false;}
}
const GameMovementPolicy& GameClientHost::movement_policy() const noexcept {
    return module_->movement_policy();
}
ConsoleVisualTheme GameClientHost::console_theme() const noexcept {return module_->console_theme();}
std::string_view GameClientHost::text_message_preparation() const noexcept {
    return active_?module_->text_message_preparation():std::string_view{};
}
bool GameClientHost::bind_text_messages(const TextMessageCatalogue& catalogue) {
    if(!active_ || catalogue.size()>kMaximumTextMessageEntries)return false;
    std::size_t bytes{};std::set<std::string_view> keys;
    for(const auto& entry:catalogue) {
        if(entry.key.empty() || entry.key.size()>63U || !keys.insert(entry.key).second || entry.text.size()>4096U ||
            entry.text.find('\0')!=entry.text.npos ||
            (entry.destination && (*entry.destination<TextDestination::notify || *entry.destination>TextDestination::center)) ||
            std::any_of(entry.key.begin(),entry.key.end(),[](unsigned char c){return !((c>='a'&&c<='z')||
                (c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-');}))return false;
        const auto size=entry.key.size()+entry.text.size();if(size>kMaximumTextMessageBytes-bytes)return false;bytes+=size;
    }
    module_->bind_text_messages(catalogue);return true;
}
gameplay_input::GameplayButtonMask GameClientHost::edge_triggered_buttons(
    const client::RuntimeClientObservationState& state) const noexcept {
    return active_ ? module_->edge_triggered_buttons(state) & movement_policy().live_buttons : 0U;
}
GameRecordResult GameClientHost::stage_record(const GameRecordInput& input) const {
    if (!active_) return {{}, std::string{"Game session is torn down"}};
    auto result = module_->stage_record(input);
    if (result.message_failure) {
        const auto& failure = *result.message_failure;
        if (failure.message_name.size() > 63U || failure.actual_body_size > 65'536U ||
            (failure.body_byte_offset && *failure.body_byte_offset >= failure.actual_body_size) ||
            std::none_of(input.messages.begin(), input.messages.end(),
                [&](const auto& message) {
                    return message.source == failure.source &&
                        message.registration_id == failure.registration_id &&
                        message.body.size() == failure.actual_body_size;
                }))
            return {{}, std::string{"Game failure metadata violates its input boundary"}};
    }
    if (!result) return result;
    if(result.state->console_text.size()>32U || result.state->console_text_dropped>input.messages.size())
        return {{},std::string{"Game console text event capacity exceeded"}};
    if(result.state->display_text.size()>32U)return {{},std::string{"Game display text event capacity exceeded"}};
    std::size_t display_bytes{};
    for(const auto& text:result.state->display_text) {
        if(text.text.size()>4096U-display_bytes || text.text.find('\0')!=text.text.npos ||
            !text.source.record_identity || text.source.end_bit_offset<=text.source.start_bit_offset ||
            text.destination<TextDestination::notify || text.destination>TextDestination::center ||
            std::none_of(input.messages.begin(),input.messages.end(),[&](const auto& m){return m.source==text.source;}))
            return {{},std::string{"Game display text has invalid bounds or provenance"}};
        display_bytes+=text.text.size();
    }
    std::size_t console_bytes{};
    for(const auto& text:result.state->console_text) {
        if(text.text.size()>4096U-console_bytes || text.text.find('\0')!=text.text.npos ||
            text.origin>GameRecordState::ConsoleText::Origin::game_event ||
            !text.source.record_identity || text.source.end_bit_offset<=text.source.start_bit_offset ||
            std::none_of(input.messages.begin(),input.messages.end(),[&](const auto& message){return message.source==text.source;}))
            return {{},std::string{"Game console text has invalid bounds or provenance"}};
        console_bytes+=text.text.size();
    }
    if((!result.state->screen_fades.empty() || !result.state->screen_shakes.empty()) &&
        result.state->screen_effect_seconds != input.presentation_seconds)
        return {{},std::string{"Game screen effect clock differs from the owning record"}};
    if(result.state->screen_fades.size()>32)
        return {{},std::string{"Game screen fade capacity exceeded"}};
    for(const auto& fade:result.state->screen_fades)if(
        !std::isfinite(fade.duration_seconds) || fade.duration_seconds<0 || fade.duration_seconds>=16 ||
        !std::isfinite(fade.hold_seconds) || fade.hold_seconds<0 || fade.hold_seconds>=16 ||
        std::any_of(fade.rgba.begin(),fade.rgba.end(),[](float v){return !std::isfinite(v) || v<0 || v>1;}) ||
        std::none_of(input.messages.begin(),input.messages.end(),[&](const auto& m){return m.source==fade.source;}))
        return {{},std::string{"Game screen fade has invalid bounds or provenance"}};
    if(result.state->screen_shakes.size()>32)return {{},std::string{"Game visual event capacity exceeded"}};
    for(const auto& shake:result.state->screen_shakes)if(!std::isfinite(shake.amplitude) || shake.amplitude<0 || shake.amplitude>=16 ||
        !std::isfinite(shake.duration_seconds) || shake.duration_seconds<0 || shake.duration_seconds>=16 ||
        !std::isfinite(shake.frequency_hz) || shake.frequency_hz<0 || shake.frequency_hz>=256 ||
        std::none_of(input.messages.begin(),input.messages.end(),[&](const auto& message){return message.source==shake.source;}))
        return {{},std::string{"Game visual event has invalid bounds or provenance"}};
    if (result.state->multiplayer.maximum_clients == 0U ||
        result.state->multiplayer.maximum_clients > kMaximumHudPlayers)
        return {{}, std::string{"Game display directory exceeds its bounds"}};
    for (const auto& player : result.state->multiplayer.players)
        if (player.name.size() > kMaximumPlayerDisplayBytes || player.team.size() > 31U ||
            player.appearance.model.size()>32U || player.appearance.top_color.value_or(0)>360U ||
            player.appearance.bottom_color.value_or(0)>360U ||
            player.ping_ms.value_or(0U)>4095U || player.packet_loss.value_or(0U)>127U ||
            player.ping_ms.has_value()!=player.packet_loss.has_value())
            return {{}, std::string{"Game display text exceeds its bounds"}};
    for (const auto& team : result.state->multiplayer.teams)
        if (team.name.size() > kMaximumPlayerDisplayBytes)
            return {{}, std::string{"Game team display text exceeds its bounds"}};
    for (const auto& notice : result.state->multiplayer.notices) if (notice &&
        (notice->killer_name.size() > kMaximumPlayerDisplayBytes ||
         notice->victim_name.size() > kMaximumPlayerDisplayBytes || notice->weapon.size() > 63U ||
         (notice->created_seconds && (!std::isfinite(*notice->created_seconds) || *notice->created_seconds < 0.0))))
        return {{}, std::string{"Game notice exceeds its bounds"}};
    if (result.state->pickups.size() > 64U)
        return {{}, std::string{"Game pickup event capacity exceeded"}};
    for (const auto& pickup : result.state->pickups) {
        if (pickup.kind > GameRecordState::Pickup::Kind::item ||
            pickup.item_token.size() > 63U ||
            pickup.source.record_identity == 0U ||
            pickup.source.end_bit_offset <= pickup.source.start_bit_offset ||
            std::none_of(input.messages.begin(), input.messages.end(),
                [&](const auto& message) { return message.source == pickup.source; }))
            return {{}, std::string{"Game pickup event has invalid bounds or provenance"}};
    }
    return result;
}
void GameClientHost::commit_record(GameRecordState&& state) noexcept {
    if(!active_)return;
    if(weapon_diagnostics_enabled())for(const auto& life:state.life_events) {
        if(life.kind!=client::RuntimeLifeEventKind::damage || !diagnostic_source_once(4,life.source.record_identity,
            life.source.record_ordinal,life.source.start_bit_offset,0))continue;
        auto event=diagnostic_event();event.identity.source_record=life.source.record_identity;
        event.identity.source_record_ordinal=life.source.record_ordinal;event.identity.source_message_bit_offset=life.source.start_bit_offset;
        event.knowledge=core::MultiplayerKnowledge::server_damage_observed;
        // This is incoming Damage for the receiving player. It proves neither
        // the attacker nor any local/remote shot, and creates no such edge.
        core::MultiplayerLifecyclePayload p;p.kind=core::MultiplayerLifecycleKind::damage;
        if(state.lifecycle.last_damage_source==life.source) {
            const auto health=[](std::optional<double> value)->std::optional<std::int32_t> {
                if(!value || !std::isfinite(*value))return {};
                return static_cast<std::int32_t>(std::clamp(*value,-1e6,1e6));
            };
            p.before=health(state.lifecycle.health_before_damage);p.after=health(state.lifecycle.health_after_damage);
        }
        if(std::ranges::all_of(life.damage_origin,[](double v){return std::isfinite(v) && std::abs(v)<=1e8;}))
            p.origin=std::array{static_cast<float>(life.damage_origin[0]),static_cast<float>(life.damage_origin[1]),static_cast<float>(life.damage_origin[2])};
        event.payload=p;(void)diagnostic_sink_->observe(event);
    }
    if(weapon_diagnostics_enabled()) for(const auto& pickup:state.pickups) {
        auto event=diagnostic_event();event.identity.source_record=pickup.source.record_identity;
        event.identity.source_record_ordinal=pickup.source.record_ordinal;
        event.identity.source_message_bit_offset=pickup.source.start_bit_offset;
        event.edge=core::MultiplayerEdgeConfidence::exact;event.knowledge=core::MultiplayerKnowledge::server_event_observed;
        core::MultiplayerLifecyclePayload p;p.kind=core::MultiplayerLifecycleKind::pickup;
        if(pickup.amount)p.after=*pickup.amount;event.payload=p;(void)diagnostic_sink_->observe(event);
    }
    auto text=std::move(state.console_text);
    const auto dropped=std::exchange(state.console_text_dropped,0U);
    module_->commit_record(std::move(state));
    for(auto& event:text) {
        if(console_text_high_water_ && (event.source.record_ordinal<console_text_high_water_->record_ordinal ||
            (event.source.record_ordinal==console_text_high_water_->record_ordinal &&
             event.source.end_bit_offset<=console_text_high_water_->end_bit_offset)))continue;
        console_text_high_water_=event.source;
        if(console_publication_deferred_) {
            if(deferred_console_text_.size()>=128U || event.text.size()>65536U-deferred_console_bytes_)++deferred_console_dropped_;
            else try {deferred_console_bytes_+=event.text.size();deferred_console_text_.push_back(std::move(event));}
                catch(...) {++deferred_console_dropped_;}
        } else if(event.origin==GameRecordState::ConsoleText::Origin::game_event)core::log_game_event(event.text);
        else core::log_server_text(event.text);
    }
    if(dropped && console_publication_deferred_)deferred_console_dropped_+=dropped;
    else if(dropped)try {
        core::log(core::LogLevel::warning,"[server] bounded text event budget dropped "+std::to_string(dropped)+" messages");
    }catch(...){}
}
ScreenEffectsFrame GameClientHost::screen_effects(double now) const noexcept {
    if(!active_ || !std::isfinite(now) || now<0)return {};
    auto frame=module_->screen_effects(now);
    if(!std::isfinite(frame.roll_degrees) || std::abs(frame.roll_degrees)>16 ||
        std::any_of(frame.translation.begin(),frame.translation.end(),[](float v){return !std::isfinite(v) || std::abs(v)>16;}) ||
        std::any_of(frame.overlay_rgba.begin(),frame.overlay_rgba.end(),[](float v){return !std::isfinite(v) || v<0 || v>1;}))return {};
    return frame;
}
void GameClientHost::set_hud_context(HudPresentationContext context) noexcept {
    if (active_) module_->set_hud_context(context);
}
std::span<const HudManifestRequest> GameClientHost::hud_resource_preparation() const noexcept {
    if (!active_) return {};
    const auto requests = module_->hud_resource_preparation();
    return requests.size() <= kMaximumHudResourceDescriptors ? requests : std::span<const HudManifestRequest>{};
}
bool GameClientHost::bind_hud_resources(const HudResourceSet& resources) {
    if (!active_ || resources.descriptors.size() > kMaximumHudResourceDescriptors) return false;
    std::size_t count{};
    for (const auto& descriptor : resources.descriptors) {
        if (descriptor.name.size() > 128U || descriptor.source_sha256.size() > 64U ||
            descriptor.status > HudResourceStatus::submitted ||
            descriptor.entries.size() > kMaximumHudResourceEntries - count) return false;
        count += descriptor.entries.size();
        for (const auto& entry : descriptor.entries) {
            if (entry.role.size() > 63U || entry.sprite_name.size() > 128U || entry.asset_sha256.size() > 64U ||
                entry.status > HudResourceStatus::submitted) return false;
            if (entry.status == HudResourceStatus::ready &&
                (!entry.resource_id || !entry.resource_revision || !entry.source.width || !entry.source.height ||
                 entry.source.x > entry.frame_width || entry.source.width > entry.frame_width - entry.source.x ||
                 entry.source.y > entry.frame_height || entry.source.height > entry.frame_height - entry.source.y)) return false;
        }
    }
    module_->bind_hud_resources(resources);
    return true;
}
HudMenuResult GameClientHost::hud_menu_input(const client::RuntimeClientObservationState& state,
    const HudMenuInput& input) noexcept {
    if (!active_ || !std::isfinite(input.seconds) || input.seconds < 0.0 ||
        input.cycle_direction < -1 || input.cycle_direction > 1 ||
        (input.group_digit && (*input.group_digit < 1U || *input.group_digit > 5U))) return {};
    auto result = module_->hud_menu_input(state,input);
    try {
        if (result.selection && !module_->inventory_selection(state,*result.selection)) result.selection.reset();
    } catch (...) { result.selection.reset(); }
    return result;
}
std::span<const LocalSoundReference> GameClientHost::hud_sound_preparation() const noexcept {
    if (!active_) return {};
    const auto resources = module_->hud_sound_preparation();
    return resources.size() <= 8U ? resources : std::span<const LocalSoundReference>{};
}
void GameClientHost::hud_selection_result(std::uint8_t weapon,bool queued,double seconds) noexcept {
    if(active_ && weapon && std::isfinite(seconds) && seconds>=0.0)
        module_->hud_selection_result(weapon,queued,seconds);
}
std::optional<double> GameClientHost::presentation_seconds(double seconds) const noexcept {
    if (!movement_audio_time_origin_ || !std::isfinite(seconds)) return {};
    const auto relative = seconds - *movement_audio_time_origin_;
    return relative >= 0.0 && std::isfinite(relative) ? std::optional<double>{relative} : std::nullopt;
}
void GameClientHost::bind_model(std::optional<LocalWeaponModelMetadata> model) {
    if (active_) module_->bind_model(std::move(model));
}
std::span<const std::string_view> GameClientHost::local_model_preparation() const noexcept {
    if(!active_) return {};
    const auto names=module_->local_model_preparation();
    return names.size()<=8U ? names : std::span<const std::string_view>{};
}
bool GameClientHost::bind_model_variants(std::span<const LocalWeaponModelMetadata> models) {
    if(!active_ || models.size()>8U) return false;
    for(std::size_t i=0;i<models.size();++i) {
        const auto& model=models[i];
        if(!model.generation || !model.model_index || !model.resource_revision ||
            model.resource_name.size()>128U || !model.resource_name.starts_with("models/") ||
            !model.resource_name.ends_with(".mdl") || model.resource_name.find("..")!=std::string::npos ||
            model.resource_name.find('\\')!=std::string::npos) return false;
        for(std::size_t j=0;j<i;++j)
            if(models[j].model_index==model.model_index || models[j].resource_name==model.resource_name ||
                models[j].generation!=model.generation) return false;
    }
    module_->bind_model_variants(models);return true;
}
void GameClientHost::observe(const client::RuntimeClientObservationState& state, double time) {
    if (active_) {diagnostic_observe(state,time);module_->observe(state, time);}
}
void GameClientHost::submit(const LocalWeaponSubmittedCommand& command, double time) {
    if (active_) {diagnostic_submit(command);module_->submit(command, time);}
}
void GameClientHost::prepare_presentation(const IStaticEffectTrace& query, double time) noexcept {
    if(active_) module_->prepare_presentation(query,time);
}
std::optional<assets::AssetVector3> GameClientHost::command_movement_effect(const LocalWeaponSubmittedCommand& c) noexcept {
    if(active_)diagnostic_submit(c);
    return active_ ? module_->command_movement_effect(c) : std::nullopt;
}
CommandMovementEffectPhase GameClientHost::command_movement_effect_phase() const noexcept {
    return active_ ? module_->command_movement_effect_phase()
                   : CommandMovementEffectPhase::before_movement;
}
BeamEffectsFrame GameClientHost::beam_effects(const IStaticEffectTrace& trace,double now,std::optional<assets::AssetVector3> muzzle) noexcept {
    if(active_) return module_->beam_effects(trace,now,muzzle);return {};
}
std::optional<EntityTrailProfile> GameClientHost::entity_trail_profile(
    std::string_view model,std::string_view sprite) const noexcept {
    return active_ ? module_->entity_trail_profile(model,sprite) : std::nullopt;
}
BeamRibbonGlowProfile GameClientHost::beam_ribbon_glow_profile(std::string_view sprite) const noexcept {
    const auto profile=active_ ? module_->beam_ribbon_glow_profile(sprite) : BeamRibbonGlowProfile{};
    return valid_beam_ribbon_glow(profile) ? profile : BeamRibbonGlowProfile{};
}
BeamSceneLightProfile GameClientHost::beam_scene_light_profile(std::string_view sprite,bool sine_strand) const noexcept {
    const auto profile=active_ ? module_->beam_scene_light_profile(sprite,sine_strand) : BeamSceneLightProfile{};
    return valid_beam_scene_light(profile) ? profile : BeamSceneLightProfile{};
}
void GameClientHost::cancel_uncommitted() noexcept {
    if (active_) module_->cancel_uncommitted();
}
LocalWeaponPresentationSnapshot GameClientHost::sample(double time) noexcept {
    auto result=active_ ? module_->sample(time) : LocalWeaponPresentationSnapshot{};
    // Sampling is not a new action: accepted actions are captured at their
    // controller boundary, including multiple commands between render frames.
    return result;
}
LocalAudioBatch GameClientHost::drain_audio() noexcept {
    auto batch=active_ ? module_->drain_audio() : LocalAudioBatch{};
    batch.statistics.movement_observations=movement_audio_observations_;
    batch.statistics.movement_clock_dropped=movement_audio_clock_dropped_;
    batch.statistics.movement_invalid_time=movement_audio_invalid_time_;
    diagnostic_audio(batch);
    batch.session=audio_session_; return batch;
}
RemotePlayerPresentationPolicy GameClientHost::remote_player_policy() const noexcept {
    return active_ ? module_->remote_player_policy() : RemotePlayerPresentationPolicy{};
}
PlayerAppearanceSelection GameClientHost::player_appearance(std::uint32_t slot,std::string_view base) const {
    return active_ ? module_->player_appearance(slot,base) : PlayerAppearanceSelection{};
}
std::vector<PlayerPaletteRange> GameClientHost::player_palette(
    const PlayerAppearanceFields& fields,const assets::SkeletalModelAssetData& model) const {
    return active_ ? module_->player_palette(fields,model) : std::vector<PlayerPaletteRange>{};
}
bool GameClientHost::player_model_compatible(const assets::SkeletalModelAssetData& model) const noexcept {
    return active_ && module_->player_model_compatible(model);
}
void GameClientHost::configure_scripted_events(std::span<const ScriptedEventBinding> bindings) noexcept {
    if(active_) module_->configure_scripted_events(bindings);
}
void GameClientHost::committed_scripted_event(const CommittedScriptedEvent& event) noexcept {
    if(!active_) return;
    if(weapon_diagnostics_enabled() && event.generation==diagnostic_identity_.network_generation &&
        diagnostic_source_once(1,event.record,event.record_ordinal,event.message_bit_offset,event.entry_ordinal)) {
        auto observed=diagnostic_event();observed.identity.source_record=event.record;
        observed.identity.source_record_ordinal=event.record_ordinal;
        observed.identity.source_message_bit_offset=event.message_bit_offset;
        observed.identity.source_entry_ordinal=event.entry_ordinal;
        observed.identity.source_event=event.event_index;
        observed.identity.actor_entity=event.emitter_entity;
        observed.knowledge=core::MultiplayerKnowledge::server_event_observed;
        core::MultiplayerActionPayload p;p.phase=DiagnosticPhase::server_observed;
        p.source_kind=1;p.source_received_steady_seconds=event.received_at_seconds;
        p.origin=std::array{event.origin.x,event.origin.y,event.origin.z};
        // Generic event fparams are NOT universally spread (e.g. Gauss energy).
        diagnostic_reason(p,"committed_script_event_not_target_damage");observed.payload=p;
        (void)diagnostic_sink_->observe(observed);
    }
    auto relative=event;
    if(!movement_audio_time_origin_) relative.resolution=ScriptedEventResolution::missing_clock;
    else relative.received_at_seconds-=*movement_audio_time_origin_;
    if(!std::isfinite(relative.received_at_seconds) || relative.received_at_seconds<0)
        relative.resolution=ScriptedEventResolution::missing_clock;
    module_->committed_scripted_event(relative);
}
RemoteWeaponEffectsBatch GameClientHost::drain_remote_effects(double now) noexcept {
    // Direct returns preserve guaranteed value elision for the fixed batch.
    // The conditional expression creates two large temporaries in Win32 Debug.
    if (active_) return module_->drain_remote_effects(now);
    return {};
}
void GameClientHost::committed_temporary_effect(const client::CommittedTemporaryEffect& event) noexcept {
    const auto index=static_cast<std::size_t>(event.kind);
    if(index>=temporary_delivery_.size())return;
    if(weapon_diagnostics_enabled() && event.generation==diagnostic_identity_.network_generation &&
        diagnostic_source_once(2,event.record,event.record_ordinal,event.message_bit_offset,event.entry_ordinal)) {
        auto observed=diagnostic_event();observed.identity.source_record=event.record;
        observed.identity.source_record_ordinal=event.record_ordinal;
        observed.identity.source_message_bit_offset=event.message_bit_offset;observed.identity.source_entry_ordinal=event.entry_ordinal;
        observed.identity.source_event=index;observed.identity.actor_entity.reset();
        observed.knowledge=core::MultiplayerKnowledge::server_event_observed;
        core::MultiplayerActionPayload p;p.phase=DiagnosticPhase::server_observed;p.source_kind=2;
        p.source_received_steady_seconds=event.received_at_seconds;
        if(event.kind==client::TemporaryEffectKind::explosion)p.source_flags=event.flags;
        if(event.kind==client::TemporaryEffectKind::explosion || event.kind==client::TemporaryEffectKind::smoke ||
            event.kind==client::TemporaryEffectKind::bubbles || event.kind==client::TemporaryEffectKind::bubble_trail ||
            event.kind==client::TemporaryEffectKind::beam_follow)p.source_resource_slot=event.sprite_index;
        if(event.kind==client::TemporaryEffectKind::explosion || event.kind==client::TemporaryEffectKind::smoke ||
            event.kind==client::TemporaryEffectKind::sparks || event.kind==client::TemporaryEffectKind::blood_sprite ||
            event.kind==client::TemporaryEffectKind::decal)p.origin=std::array{event.origin.x,event.origin.y,event.origin.z};
        diagnostic_reason(p,"committed_temp_effect_not_target_damage");observed.payload=p;(void)diagnostic_sink_->observe(observed);
    }
    auto& delivery=temporary_delivery_[index];++delivery.received;
    if(!active_ || !movement_audio_time_origin_) {++delivery.clock_dropped;return;}
    auto relative=event;relative.received_at_seconds-=*movement_audio_time_origin_;
    if(!std::isfinite(relative.received_at_seconds) || relative.received_at_seconds<0) {++delivery.clock_dropped;return;}
    if(relative.kind==client::TemporaryEffectKind::beam_follow) {
        if(relative.generation!=follow_generation_) return;
        if(follow_events_.count<follow_events_.events.size())
            follow_events_.events[follow_events_.count++]=relative;
        else ++follow_events_.dropped;
        return;
    }
    module_->committed_temporary_effect(relative);++delivery.forwarded;
}
void GameClientHost::bind_effect_resource(const EffectResourceBinding& binding) noexcept {
    if(active_ && binding.name.size()<=255) {
        module_->bind_effect_resource(binding);
        if(binding.kind==EffectResourceBinding::Kind::model) {
            const auto role=module_->weapon_diagnostic_object_profile(binding.name);
            auto slot=std::find_if(diagnostic_object_resources_.begin(),diagnostic_object_resources_.end(),
                [&](const auto& r){return r && r->model==binding.slot;});
            if(role==WeaponDiagnosticObjectProfile::unknown) {if(slot!=diagnostic_object_resources_.end())slot->reset();}
            else {
                if(slot==diagnostic_object_resources_.end())slot=std::find(diagnostic_object_resources_.begin(),diagnostic_object_resources_.end(),std::nullopt);
                if(slot!=diagnostic_object_resources_.end())*slot=DiagnosticObjectResource{binding.slot,role};
            }
        }
    }
}
std::string_view GameClientHost::effect_palette_preparation() const noexcept {
    return active_ ? module_->effect_palette_preparation() : std::string_view{};
}
std::span<const DecalAssetRequest> GameClientHost::surface_decal_preparation() const noexcept {
    if(!active_)return {};
    const auto requests=module_->surface_decal_preparation();
    return requests.size()<=kMaximumPreparedDecalAssets ? requests : std::span<const DecalAssetRequest>{};
}
void GameClientHost::bind_effect_palette(const EffectPaletteMetadata& palette) noexcept {
    if(active_ && palette.status<=EffectPaletteStatus::decode_failed)module_->bind_effect_palette(palette);
}
void GameClientHost::configure_effect_policy(const EffectPresentationPolicy& policy) noexcept {
    if(active_)module_->configure_effect_policy(policy);
}
std::string_view GameClientHost::temporary_effect_source_label(TemporaryEffectIdentity::Source source) const noexcept {
    if(!active_)return {};
    const auto label=module_->temporary_effect_source_label(source);
    if(label.size()>64 || !std::ranges::all_of(label,[](char c){return (c>='a'&&c<='z') || c=='_';}))return {};
    return label;
}
void GameClientHost::committed_sound(const CommittedSoundObservation& event) noexcept {
    if(weapon_diagnostics_enabled() && event.generation==diagnostic_identity_.network_generation &&
        diagnostic_source_once(3,event.record,event.record_ordinal,event.message_bit_offset,event.entry_ordinal)) {
        auto observed=diagnostic_event();observed.identity.source_record=event.record;
        observed.identity.source_record_ordinal=event.record_ordinal;
        observed.identity.source_message_bit_offset=event.message_bit_offset;observed.identity.source_entry_ordinal=event.entry_ordinal;
        observed.identity.source_event=event.sample_slot;observed.identity.actor_entity=event.entity;
        observed.knowledge=core::MultiplayerKnowledge::server_event_observed;
        core::MultiplayerActionPayload p;p.phase=DiagnosticPhase::server_observed;p.source_kind=3;
        p.source_received_steady_seconds=event.received_at_seconds;p.source_resource_slot=event.sample_slot;
        p.source_channel=event.channel;p.source_flags=event.flags;p.origin=std::array{event.origin.x,event.origin.y,event.origin.z};
        diagnostic_reason(p,"committed_sound_not_target_damage");observed.payload=p;(void)diagnostic_sink_->observe(observed);
    }
    if(!active_ || !movement_audio_time_origin_)return;
    auto relative=event;relative.received_at_seconds-=*movement_audio_time_origin_;
    if(!std::isfinite(relative.received_at_seconds) || relative.received_at_seconds<0)return;
    module_->committed_sound(relative);
}
GameClientHost::FollowEvents GameClientHost::drain_follow_events() noexcept {
    auto result=follow_events_;follow_events_.count=0;follow_events_.dropped=0;return result;
}
TemporaryEffectsBatch GameClientHost::drain_temporary_effects(double now) noexcept {
    return active_ ? module_->drain_temporary_effects(now) : TemporaryEffectsBatch{};
}
std::span<const LocalSoundReference> GameClientHost::temporary_sound_preparation() const noexcept {
    return active_ ? module_->temporary_sound_preparation() : std::span<const LocalSoundReference>{};
}
RemoteWeaponSocketPlan GameClientHost::remote_weapon_socket_plan(const RemoteWeaponModelContext& context) noexcept {
    return active_ ? module_->remote_weapon_socket_plan(context) : RemoteWeaponSocketPlan{};
}
void GameClientHost::remote_weapon_poses(std::span<const RemoteWeaponPoseSample> poses) noexcept {
    if(active_) module_->remote_weapon_poses(poses.first(std::min(poses.size(),std::size_t{32})));
}
void GameClientHost::remote_entities(std::span<const client::RuntimePacketEntityObservation> entities,
    std::uint32_t maximum_clients,std::uint64_t ordinal) noexcept {
    if(active_ && entities.size()<=4096U && maximum_clients<=kMaximumRemotePlayers) {
        diagnostic_entities(entities,maximum_clients,ordinal);
        module_->remote_entities(entities,maximum_clients,ordinal);
    }
}
WorldEntityPresentationIntent GameClientHost::world_entity(const WorldEntityPresentationContext& context) const noexcept {
    return active_ ? module_->world_entity(context) : WorldEntityPresentationIntent{};
}
void GameClientHost::remote_slot_boundary(std::uint32_t entity) noexcept {
    if(active_ && entity && entity<=kMaximumRemotePlayers) module_->remote_slot_boundary(entity);
}
LocalAudioBatch GameClientHost::drain_remote_audio() noexcept {
    auto batch=active_ ? module_->drain_remote_audio() : LocalAudioBatch{};
    batch.session=audio_session_;
    diagnostic_audio(batch);
    return batch;
}
void GameClientHost::remote_world_impact(const LocalWeaponActionIdentity& action,
    assets::AssetVector3 point,double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    if(active_) module_->remote_world_impact(action,point,time,std::move(surface));
}
void GameClientHost::remote_shell_contact(const LocalShellContact& contact) noexcept {
    if(active_) module_->remote_shell_contact(contact);
}
RemotePlayerPresentationIntent GameClientHost::remote_player(
    const RemotePlayerPresentationContext& input) noexcept {
    return active_ ? module_->remote_player(input) : RemotePlayerPresentationIntent{};
}
LocalVisualFrame GameClientHost::local_visuals(const LocalVisualContext& context) noexcept {
    return active_ ? module_->local_visuals(context) : LocalVisualFrame{};
}
std::optional<LocalImpactAssetProfile> GameClientHost::local_impact_assets() const noexcept {
    return active_ ? module_->local_impact_assets() : std::nullopt;
}
std::optional<LocalCrowbarImpactAssetProfile> GameClientHost::local_crowbar_impact_assets() const noexcept {
    return active_ ? module_->local_crowbar_impact_assets() : std::nullopt;
}
void GameClientHost::accepted_world_impact(const LocalWeaponActionIdentity& action,
    assets::AssetVector3 point, double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    if (active_) module_->accepted_world_impact(action, point, time,std::move(surface));
}
bool GameClientHost::resolved_world_impact(const LocalWeaponActionIdentity& action,
    LocalWorldImpactOutcome outcome, std::optional<assets::AssetVector3> point,
    double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    const auto result=active_ && module_->resolved_world_impact(action, outcome, point, time,std::move(surface));
    if(weapon_diagnostics_enabled() && action.generation==diagnostic_identity_.network_generation) {
        auto event=diagnostic_event();event.identity.command_id=action.command_sequence ? std::optional<std::uint64_t>{action.command_sequence} : std::nullopt;
        const auto receipt=std::find_if(diagnostic_actions_.begin(),diagnostic_actions_.end(),[&](const auto& value){
            if(!value)return false;auto parent=action;parent.sub_identity=0;return value->identity==parent;
        });
        if(receipt!=diagnostic_actions_.end()){event.identity.action_id=(*receipt)->id;event.edge=core::MultiplayerEdgeConfidence::exact;}
        event.knowledge=core::MultiplayerKnowledge::local_proxy;
        core::MultiplayerGeometryPayload p;p.kind=core::MultiplayerGeometryKind::world_blocker;
        if(point)p.endpoint=std::array{point->x,point->y,point->z};
        if(outcome==LocalWorldImpactOutcome::static_world_hit)p.intersects=true;
        else if(outcome==LocalWorldImpactOutcome::miss)p.intersects=false;
        if(outcome==LocalWorldImpactOutcome::start_solid)p.startsolid=true;
        event.payload=p;(void)diagnostic_sink_->observe(event);
    }
    return result;
}
std::optional<LocalWorldImpactDiagnostic>
GameClientHost::last_world_impact_diagnostic() const noexcept {
    return active_ ? module_->last_world_impact_diagnostic() : std::nullopt;
}
void GameClientHost::shell_contact(const LocalShellContact& contact) noexcept {
    if (active_) module_->shell_contact(contact);
}
void GameClientHost::configure_movement_materials(std::string_view text) noexcept {
    if (active_) module_->configure_movement_materials(text);
}
std::span<const LocalSoundReference> GameClientHost::movement_sound_preparation() const noexcept {
    return active_ ? module_->movement_sound_preparation() : std::span<const LocalSoundReference>{};
}
void GameClientHost::rewind_movement_audio(std::uint32_t boundary) noexcept {
    if (active_) module_->rewind_movement_audio(boundary);
}
void GameClientHost::observe_movement_audio(
    const MovementAudioObservation& observation, bool replay) noexcept {
    if (!active_) return;
    ++movement_audio_observations_;
    if (!movement_audio_time_origin_) {++movement_audio_clock_dropped_; return;}
    auto relative=observation;
    relative.scheduled_seconds-=*movement_audio_time_origin_;
    if (std::isfinite(relative.scheduled_seconds) && relative.scheduled_seconds>=0)
        module_->observe_movement_audio(relative,replay);
    else ++movement_audio_invalid_time_;
}
void GameClientHost::set_movement_audio_time_origin(double seconds) noexcept {
    if (std::isfinite(seconds) && seconds>=0) movement_audio_time_origin_=seconds;
}
ViewmodelIntent GameClientHost::viewmodel(const client::RuntimeClientObservationState& state,
    std::optional<LocalWeaponModelMetadata> model, double time,
    std::optional<LocalWeaponVisual> visual) {
    return active_ ? module_->viewmodel(state,std::move(model),time,visual) : ViewmodelIntent{};
}
HudState GameClientHost::hud(const client::RuntimeClientObservationState& state, double time) {
    auto hud = active_ ? module_->hud(state,time) : HudState{};
    if (!valid_hud_draw_commands(hud.draw)) {hud.draw = {}; hud.draw_rejected = true;}
    return hud;
}
CameraIntent GameClientHost::camera(const client::RuntimeClientObservationState& state, double punch) const noexcept {
    return active_ ? module_->camera(state,punch) : CameraIntent{};
}
void GameClientHost::observe_action_evidence(const client::RuntimeClientObservationState& state,
    const std::optional<GameActionTraffic>& traffic) noexcept {
    if (active_) module_->observe_action_evidence(state,traffic);
}
GameActionEvidenceSnapshot GameClientHost::action_evidence() const noexcept {
    return active_ ? module_->action_evidence() : GameActionEvidenceSnapshot{};
}
std::optional<GameCommandRequest> GameClientHost::inventory_selection(
    const client::RuntimeClientObservationState& state, std::uint8_t id) const {
    return active_ ? module_->inventory_selection(state,id) : std::nullopt;
}
std::optional<std::uint8_t> GameClientHost::select_group(
    const client::RuntimeClientObservationState& state, std::uint8_t group,
    std::optional<std::uint8_t> current) const {
    return active_ ? module_->select_group(state,group,current) : std::nullopt;
}
std::optional<std::uint8_t> GameClientHost::cycle_inventory(
    const client::RuntimeClientObservationState& state,
    std::optional<std::uint8_t> current, int direction) const {
    return active_ ? module_->cycle_inventory(state,current,direction) : std::nullopt;
}
GameCommandRequest GameClientHost::self_kill_request() const {
    return active_ ? module_->self_kill_request() : GameCommandRequest{};
}
std::optional<std::uint8_t> GameClientHost::scenario_inventory_target(
    const client::RuntimeClientObservationState& state, std::size_t phase) const {
    return active_ ? module_->scenario_inventory_target(state,phase) : std::nullopt;
}
GameScenarioDirective GameClientHost::scenario_command(GameScenario mode,
    const client::RuntimeClientObservationState* state, std::size_t phase, double time, double phase_time) noexcept {
    return active_ ? module_->scenario_command(mode,state,phase,time,phase_time) : GameScenarioDirective{};
}
DamageRespawnScriptSnapshot GameClientHost::damage_respawn_snapshot() const noexcept {
    return module_->damage_respawn_snapshot();
}
}

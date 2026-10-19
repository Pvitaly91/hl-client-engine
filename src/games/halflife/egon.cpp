#include <hlclient/games/halflife/egon.hpp>
#include <algorithm>
#include <cmath>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
namespace hlclient::games::halflife {
namespace {
namespace api=game_api;using V=assets::AssetVector3;
bool finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
api::LocalSoundReference sound(std::string_view n){api::LocalSoundReference r;r.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;std::copy(n.begin(),n.end(),r.sample.begin());return r;}
std::string_view name(const api::ScriptedEventBinding& b){return {b.name.data(),static_cast<std::size_t>(std::find(b.name.begin(),b.name.end(),'\0')-b.name.begin())};}
}
bool egon_sequences(const api::LocalWeaponModelMetadata& m) noexcept {
    if(m.resource_name!="models/v_egon.mdl" || m.sequences.size()!=11 || !m.supported_bodies[1])return false;
    constexpr std::array<std::string_view,11> labels{"idle1","fidget1","altfireon","altfirecycle","altfireoff","fire1","fire2","fire3","fire4","draw","holster"};
    for(std::size_t i=0;i<labels.size();++i)if(m.sequences[i].label!=labels[i] || (i>=5 && i<=8 && !m.sequences[i].looping))return false;
    return std::ranges::all_of(m.sequences,[](const auto& s){return !s.label.empty() && s.frame_count && std::isfinite(s.fps) && s.fps>0;});
}
std::span<const api::LocalSoundReference> EgonPresentation::resources() noexcept {
    static const std::array sounds{sound("weapons/egon_windup2.wav"),sound("weapons/egon_run3.wav"),sound("weapons/egon_off1.wav")};return sounds;
}
void EgonPresentation::reset(api::GameSessionIdentity id) noexcept {*this=EgonPresentation{};identity_=id;}
void EgonPresentation::configure(std::span<const api::ScriptedEventBinding> bindings) noexcept {
    fire_event_=stop_event_=0;bool fire_conflict=false,stop_conflict=false;
    for(const auto& b:bindings)if(b.index && b.index<=1023) {
        if(name(b)=="events/egon_fire.sc"){fire_conflict|=fire_event_ && fire_event_!=b.index;fire_event_=b.index;}
        if(name(b)=="events/egon_stop.sc"){stop_conflict|=stop_event_ && stop_event_!=b.index;stop_event_=b.index;}
    }
    for(const auto& b:bindings) {
        if(b.index==fire_event_ && name(b)!="events/egon_fire.sc")fire_event_=0;
        if(b.index==stop_event_ && name(b)!="events/egon_stop.sc")stop_event_=0;
    }
    if(fire_conflict)fire_event_=0;if(stop_conflict)stop_event_=0;
    if(fire_event_==stop_event_)fire_event_=stop_event_=0;
}
void EgonPresentation::bind(const std::optional<api::LocalWeaponModelMetadata>& model) {
    if(model_==model)return;cancel(now_);model_=model;selected_=eligible_=false;snapshot_={};ready_at_=cooldown_=0;
    animation_source_.reset();animation_floor_pending_=true;
}
void EgonPresentation::control(std::uint32_t i,api::LocalVoiceControl::Operation op,double at) noexcept {
    if(i>=emitters_.size() || !emitters_[i].voice)return;
    if(op==api::LocalVoiceControl::Operation::source)
        for(std::size_t n=0;n<control_count_;++n)if(controls_[n].identity==emitters_[i].voice && controls_[n].operation==op) {
            controls_[n].world_origin=emitters_[i].origin;controls_[n].scheduled_seconds=at;return;
        }
    if(control_count_==controls_.size()) {
        const auto replace=std::find_if(controls_.begin(),controls_.end(),[](const auto& c){return c.operation==api::LocalVoiceControl::Operation::source;});
        if(op==api::LocalVoiceControl::Operation::source || replace==controls_.end()){++capacity_;return;}
        std::move(replace+1,controls_.end(),replace);--control_count_;
    }
    const auto& e=emitters_[i];auto& c=controls_[control_count_++];c={};
    c.identity=e.voice;c.operation=op;c.scheduled_seconds=at;c.reference=resources()[e.running ? 1 : 0];
    c.repeat_sample=e.running;c.volume=e.narrow ? .9F : .98F;c.pitch=e.narrow ? 1 : 1.25F;
    c.world_origin=e.origin;c.attenuation=e.origin ? .8F : 0;
    if(op==api::LocalVoiceControl::Operation::start)++starts_;
    if(op==api::LocalVoiceControl::Operation::replace)++replacements_;
    if(op==api::LocalVoiceControl::Operation::stop)++stops_;
}
void EgonPresentation::start(std::uint32_t i,double at,bool narrow,std::optional<V> origin) noexcept {
    auto& e=emitters_[i];if(e.active)return;
    e={ (std::uint64_t{1}<<44)|(++voice_lifetime_<<6)|(i+1U),at,true,false,narrow,origin};
    control(i,api::LocalVoiceControl::Operation::start,at);
}
void EgonPresentation::stop(std::uint32_t i,double at,bool noise) noexcept {
    auto& e=emitters_[i];if(!e.active)return;
    if(i==0 && snapshot_.identity && diagnostic_observer_ && diagnostic_observer_->weapon_diagnostics_enabled()) {
        api::WeaponActionDiagnosticObservation o;o.identity=*snapshot_.identity;o.kind=core::MultiplayerActionKind::beam;
        o.phase=core::MultiplayerActionPhase::end;diagnostic_observer_->weapon_action_observed(o);
    }
    control(i,api::LocalVoiceControl::Operation::stop,at);
    if(noise && cue_count_<cues_.size()) {
        auto& c=cues_[cue_count_++];c={};c.reference=resources()[2];c.scheduled_seconds=at;
        c.volume=.98F;c.kind=api::LocalSoundKind::fire;c.world_origin=e.origin;c.attenuation=e.origin ? .8F : 0;
    }
    e.active=false;e.voice=0;
    if(i==0){animate(0,at);cooldown_=at+.5;confirmed_=false;server_pair_=false;}
}
void EgonPresentation::animate(std::uint32_t seq,double at) noexcept {
    snapshot_.visual=api::LocalWeaponVisual{seq,1,++restart_,at,api::LocalWeaponAnimationSource::provisional_command,snapshot_.status};
}
void EgonPresentation::cancel(double at) noexcept {stop(0,std::max(at,now_),true);aim_.reset();}
void EgonPresentation::observe(const client::RuntimeClientObservationState& s,double at) noexcept {
    if(s.generation!=identity_.network_generation || !std::isfinite(at) || at<0 || s.publication_revision<revision_)return;
    now_=std::max(now_,at);const auto previous=selected_;selected_=eligible_=false;
    const auto type=std::ranges::find(s.weapon_hud.catalogue,10U,&client::RuntimeWeaponTypeObservation::id);
    if(!model_ || !egon_sequences(*model_) || s.weapon_hud.active_weapon_id!=10 || type==s.weapon_hud.catalogue.end() ||
        type->command_name!="weapon_egon" || !s.receiving_client || s.receiving_client->viewmodel_index!=model_->model_index) {
        if(previous)cancel(at);return;
    }
    selected_=true;const auto& c=*s.receiving_client;
    if(animation_floor_pending_ && s.client_metadata.source){animation_floor_=s.client_metadata.source->record_ordinal;animation_floor_pending_=false;}
    if(life_!=s.lifecycle.life_epoch){cancel(at);snapshot_={};life_=s.lifecycle.life_epoch;}
    if(s.lifecycle.dead() || !c.health || *c.health<=0 || !c.water_level || *c.water_level>=3 ||
        !c.owned_weapon_bits || !(*c.owned_weapon_bits&(1U<<10)) || !c.next_weapon_attack ||
        type->primary_ammo_type<0 || static_cast<std::size_t>(type->primary_ammo_type)>=s.weapon_hud.reserve_ammo.size()) {cancel(at);return;}
    const auto ammo=s.weapon_hud.reserve_ammo[static_cast<std::size_t>(type->primary_ammo_type)];
    if(!ammo || !*ammo || *ammo>100){cancel(at);return;}
    const auto w=std::ranges::find(s.weapon_slots,std::optional<std::uint32_t>{10},&client::RuntimeWeaponSlotObservation::weapon_id);
    if(w==s.weapon_slots.end() || !w->next_primary_attack || s.client_metadata.freshness==client::RuntimeObservationFreshness::unavailable){cancel(at);return;}
    if(s.publication_revision!=revision_ && s.client_metadata.freshness==client::RuntimeObservationFreshness::observed_in_record) {
        const auto remaining=std::max({0.,*c.next_weapon_attack,*w->next_primary_attack});
        // Explicit zero/elapsed server timers mean ready, not "wait until this
        // render frame". Due quantized commands can precede the RX drain clock.
        ready_at_=remaining>0 ? at+remaining : 0;
        if(w->auxiliary_integer3) {
            if(*w->auxiliary_integer3==1 && emitters_[0].active) {
                if(!confirmed_)++snapshot_.actions_confirmed;confirmed_=true;snapshot_.status=api::LocalWeaponActionStatus::server_confirmed;
            } else if(*w->auxiliary_integer3==0 && confirmed_)stop(0,at,true);
            else if(*w->auxiliary_integer3<0 || *w->auxiliary_integer3>1){cancel(at);return;}
        }
    }
    revision_=s.publication_revision;eligible_=true;
    if(!snapshot_.visual)animate(9,at);
    if(!animation_floor_pending_ && s.weapon_hud.animation_source && s.weapon_hud.animation_source->record_ordinal>=animation_floor_ &&
        s.weapon_hud.animation_source!=animation_source_ && s.weapon_hud.animation_sequence &&
        *s.weapon_hud.animation_sequence<model_->sequences.size()) {
        animation_source_=s.weapon_hud.animation_source;const auto seq=*s.weapon_hud.animation_sequence;
        if(seq==10){stop(0,at,true);animate(seq,at);}
        else if(!emitters_[0].active || (seq>=5 && seq<=8))animate(seq,at);
        if(snapshot_.visual)snapshot_.visual->source=api::LocalWeaponAnimationSource::service_event;
    }
}
void EgonPresentation::command(const api::LocalWeaponSubmittedCommand& c) noexcept {
    if(c.generation!=identity_.network_generation || !c.sequence || c.sequence<=command_floor_ ||
        !std::isfinite(c.sample_end_seconds) || c.sample_end_seconds<0 || c.sample_end_seconds<last_command_seconds_)return;
    command_floor_=c.sequence;last_command_seconds_=c.sample_end_seconds;
    now_=std::max(now_,c.sample_end_seconds);const auto at=c.sample_end_seconds;
    // RMB is not an Egon mode. No reload action and no local uranium burn.
    const auto rejected=[&](std::string_view reason) {
        if(!selected_ || !diagnostic_observer_ || !diagnostic_observer_->weapon_diagnostics_enabled())return;
        api::WeaponActionDiagnosticObservation o;
        o.identity={identity_.network_generation,10,model_ ? model_->model_index : 0,model_ ? model_->resource_revision : 0,
            c.sequence,api::LocalWeaponAction::primary_fire,at,revision_};
        o.command=c;o.mode=(c.buttons&1) ? api::WeaponDiagnosticMode::primary : (c.buttons&(1U<<11)) ? api::WeaponDiagnosticMode::secondary : api::WeaponDiagnosticMode::reload;
        o.kind=o.mode==api::WeaponDiagnosticMode::primary ? core::MultiplayerActionKind::beam : core::MultiplayerActionKind::unknown;o.phase=core::MultiplayerActionPhase::rejected;
        o.reason=reason;o.next_attack=ready_at_;diagnostic_observer_->weapon_action_observed(o);
    };
    if(!(c.buttons&1)){if(c.buttons&((1U<<11)|(1U<<13)))rejected("local_mode_unsupported");stop(0,at,true);require_release_=false;return;}
    if(require_release_ || !selected_ || !eligible_ || at<cooldown_ || !c.shot_context || !finite(c.shot_context->eye) || !finite(c.shot_context->direction)) {
        rejected(require_release_ ? "local_release_required" : at<cooldown_ ? "local_attack_timer_gate" : !c.shot_context ? "local_command_pose_unavailable" : "local_model_or_state_unavailable");return;
    }
    if(!emitters_[0].active && at<ready_at_)rejected("local_attack_timer_gate");
    if(!emitters_[0].active && at>=ready_at_) {
        start(0,at,false,{});fire_started_=at;confirmed_=false;
        snapshot_.status=api::LocalWeaponActionStatus::predicted_pending;++snapshot_.actions_started;
        snapshot_.action=api::LocalWeaponAction::primary_fire;
        snapshot_.identity=api::LocalWeaponActionIdentity{identity_.network_generation,10,model_->model_index,model_->resource_revision,
            c.sequence,api::LocalWeaponAction::primary_fire,at,revision_};
        if(diagnostic_observer_ && diagnostic_observer_->weapon_diagnostics_enabled()) {
            api::WeaponActionDiagnosticObservation o;o.identity=*snapshot_.identity;o.command=c;
            o.kind=core::MultiplayerActionKind::beam;o.phase=core::MultiplayerActionPhase::begin;
            diagnostic_observer_->weapon_action_observed(o);
        }
        fire_sequence_=5+(c.sequence+1)%4;animate(fire_sequence_,at);
    }
    if(emitters_[0].active)aim_ = api::LocalVisualContext{c.shot_context->eye,c.shot_context->direction,c.shot_context->right,c.shot_context->up,{},at};
}
void EgonPresentation::aim(const api::LocalVisualContext& c) noexcept {
    if(selected_ && emitters_[0].active && finite(c.eye) && finite(c.forward) && std::isfinite(c.now_seconds))aim_=c;
}
api::LocalWeaponPresentationSnapshot EgonPresentation::sample(double at) noexcept {
    if(!std::isfinite(at) || at<now_)at=now_;else now_=at;
    if(emitters_[0].active && !confirmed_ && at-fire_started_>.75) {
        stop(0,at,true);require_release_=true;snapshot_.status=api::LocalWeaponActionStatus::timed_out;++snapshot_.actions_rejected;
    }
    // Only the receiving player predicts the timed transition. Remote run is
    // established by the committed egon_fire event, never a guessed timer.
    if(emitters_[0].active && !emitters_[0].running && at>=emitters_[0].started+2) {
        emitters_[0].running=true;control(0,api::LocalVoiceControl::Operation::replace,emitters_[0].started+2);
    }
    if(snapshot_.visual && model_) {
        auto& v=*snapshot_.visual;const auto& seq=model_->sequences[v.sequence];
        auto age=std::max(0.,at-v.started_at_seconds);const auto duration=seq.frame_count/seq.fps;
        if(emitters_[0].active && duration>0 && seq.looping) {
            age=std::fmod(age,duration);
        } else if(age>=duration && v.sequence!=0){animate(0,at);age=0;}
        snapshot_.frame_coordinate=std::min(age*seq.fps,static_cast<double>(seq.frame_count-1));
        snapshot_.visual->status=snapshot_.status;
    }
    return snapshot_;
}
void EgonPresentation::event(const api::CommittedScriptedEvent& e) noexcept {
    if(!handles(e.event_index) || e.generation!=identity_.network_generation || !e.emitter_entity || e.emitter_entity>32 ||
        !e.record || !e.record_ordinal || !std::isfinite(e.received_at_seconds) || e.received_at_seconds<0 || e.resolution!=api::ScriptedEventResolution::ready)return;
    const std::array key{e.record_ordinal,std::uint64_t(e.message_bit_offset),std::uint64_t(e.entry_ordinal)};
    if((event_floor_ && key<=*event_floor_) || std::ranges::find(seen_,key)!=seen_.end()){++snapshot_.duplicate_events;return;}
    auto& evicted=seen_[next_seen_++%seen_.size()];if(evicted[0] && (!event_floor_ || evicted>*event_floor_))event_floor_=evicted;evicted=key;
    const auto local=identity_.receiving_entity==e.emitter_entity;const auto i=local ? 0 : e.emitter_entity;
    if(!local && e.record_ordinal<=slot_floor_[i-1])return;
    const auto at=e.received_at_seconds;
    if(e.event_index==stop_event_){if(e.integer_parameter1)stop(i,at,*e.integer_parameter1!=0);return;}
    if(!local && slot_known_[i-1] && (!slot_visible_[i-1] || slot_weapon_[i-1]==0U ||
        (e.weapon_model_index && slot_weapon_[i-1] && e.weapon_model_index!=slot_weapon_[i-1])))return;
    if(!e.integer_parameter1 || !e.integer_parameter2 || !e.boolean_parameter1 ||
        *e.integer_parameter1<0 || *e.integer_parameter1>1 || *e.integer_parameter2<0 || *e.integer_parameter2>1)return;
    if(local){if(emitters_[0].active){confirmed_=true;snapshot_.status=api::LocalWeaponActionStatus::server_confirmed;
        if(!*e.boolean_parameter1){animate(5+static_cast<std::uint32_t>((e.record_ordinal+e.entry_ordinal)%4),at);snapshot_.visual->source=api::LocalWeaponAnimationSource::service_event;}}
        return;}
    if(!finite(e.origin))return;
    if(*e.boolean_parameter1)start(i,at,*e.integer_parameter2==0,e.origin);
    else {
        if(!emitters_[i].active)start(i,at,*e.integer_parameter2==0,e.origin);
        if(!emitters_[i].running){emitters_[i].running=true;control(i,api::LocalVoiceControl::Operation::replace,at);}
    }
}
void EgonPresentation::poses(std::span<const api::RemoteWeaponPoseSample> poses) noexcept {
    for(const auto& p:poses)if(p.entity && p.entity<=32 && p.generation==identity_.network_generation && emitters_[p.entity].active) {
        if(p.semantic_known && p.semantic_key!=10)stop(p.entity,now_,false);
        else if(p.semantic_match && p.semantic_key==10){emitters_[p.entity].origin=p.muzzle.origin;control(p.entity,api::LocalVoiceControl::Operation::source,now_);}
    }
}
void EgonPresentation::entities(std::span<const client::RuntimePacketEntityObservation> entities,std::uint64_t ordinal) noexcept {
    if(!ordinal || ordinal<=entity_ordinal_)return;entity_ordinal_=ordinal;server_pair_=false;
    unsigned local_beams=0;std::optional<std::uint32_t> sprite;
    for(const auto& e:entities)if(e.custom_beam && e.custom_beam->supported && e.custom_beam->type==client::RuntimeCustomBeamObservation::Type::entity_point &&
        identity_.receiving_entity==e.custom_beam->end_reference.entity && e.custom_beam->end_reference.attachment==1 && !(e.custom_beam->flags&0xe0)) {
            if(!sprite)sprite=e.custom_beam->sprite_model_index;if(sprite==e.custom_beam->sprite_model_index)++local_beams;
        }
    server_pair_=local_beams>=2;
    if(server_pair_ && emitters_[0].active)confirmed_=true;
    for(unsigned i=1;i<=32;++i)if(identity_.receiving_entity!=i) {
        const auto p=std::ranges::find(entities,i,&client::RuntimePacketEntityObservation::entity_number);
        slot_known_[i-1]=true;slot_visible_[i-1]=p!=entities.end() && !(p->effects.value_or(0)&128);
        slot_weapon_[i-1]=p==entities.end() ? std::nullopt : p->weapon_model_index;
        if(!slot_visible_[i-1] || slot_weapon_[i-1]==0U){stop(i,now_,false);slot_floor_[i-1]=ordinal;}
    }
}
void EgonPresentation::slot_boundary(std::uint32_t e) noexcept {
    if(!e || e>32)return;stop(e,now_,false);slot_floor_[e-1]=entity_ordinal_;slot_known_[e-1]=true;slot_visible_[e-1]=false;slot_weapon_[e-1].reset();
}
void EgonPresentation::append_audio(api::LocalAudioBatch& b) noexcept {
    for(const auto& r:resources())if(b.prepare_count<b.prepare.size())b.prepare[b.prepare_count++]=r;
    for(std::size_t i=0;i<control_count_;++i)if(b.control_count<b.controls.size())b.controls[b.control_count++]=controls_[i];else ++capacity_;
    for(std::size_t i=0;i<cue_count_;++i)if(b.count<b.cues.size()){auto c=cues_[i];c.scope=b.scope;b.cues[b.count++]=c;}else ++capacity_;
    control_count_=cue_count_=0;
}
void EgonPresentation::append_frame(const api::IStaticEffectTrace& query,double at,std::optional<V> muzzle,api::BeamEffectsFrame& out) noexcept {
    out.shots+=snapshot_.actions_started;out.capacity+=capacity_;out.server_handoff=server_pair_ && selected_ && emitters_[0].active;
    out.active_emitters=static_cast<std::uint32_t>(std::ranges::count_if(emitters_,[](const auto& e){return e.active;}));
    out.audio_start_requests=starts_;out.audio_replace_requests=replacements_;out.audio_stop_requests=stops_;
    if(!emitters_[0].active || server_pair_ || !aim_ || !snapshot_.identity || !muzzle || !finite(*muzzle))return;
    const auto a=*aim_;const auto n=std::hypot(a.forward.x,a.forward.y,a.forward.z);if(n<.001F || !std::isfinite(n))return;
    const V end{a.eye.x+a.forward.x*2048/n,a.eye.y+a.forward.y*2048/n,a.eye.z+a.forward.z*2048/n};
    const auto hit=query.trace(a.eye,end);if(!hit.valid || hit.start_solid || hit.all_solid || hit.unsupported)return;
    for(unsigned i=0;i<2 && out.count<out.sprites.size();++i){auto& b=out.sprites[out.count++];b.action=*snapshot_.identity;b.action.sub_identity=i+1;
        constexpr std::string_view sprite="sprites/xbeam1.spr";std::copy(sprite.begin(),sprite.end(),b.virtual_sprite.begin());
        b.start=*muzzle;b.end=hit.hit ? hit.point : end;b.width=i ? 5.F : 3.5F;b.opacity=.7F;b.color={.5F,.5F,1};
        b.starts=fire_started_;b.expires=at+.1;b.persistent=true;b.noise_amplitude=i ? .8F : 2.F;b.sine_noise=!i;
        b.scroll_cycles_per_second=i ? .25F : .55F;b.phase_seconds=at;b.ribbon_glow=egon_beam_glow;
        if(!i)b.scene_light=egon_beam_light;}
}
api::RemoteWeaponSocketPlan EgonPresentation::socket_plan(const api::RemoteWeaponModelContext& c) noexcept {
    if(c.virtual_name!="models/p_egon.mdl")return {};
    if(socket_.semantic_key==10 && socket_resource_==c.resource_id && socket_revision_==c.revision)return socket_;
    socket_resource_=c.resource_id;socket_revision_=c.revision;socket_={};socket_.semantic_key=10;socket_.semantic_match=true;
    // Current owner attachments still have priority in the generic resolver.
    if(!c.model.attachments.empty()) {socket_.source=api::RemoteSocketSource::explicit_model_attachment;socket_.muzzle_attachment=socket_.shell_attachment=0;return socket_;}
    constexpr std::array<std::string_view,10> bones{"Bip01","Bip01 Pelvis","Bip01 Spine","Bip01 Spine1","Bip01 Spine2","Bip01 Spine3","Bip01 Neck","Bip01 R Arm","Bip01 R Arm1","Bip01 R Arm2"};
    if(c.model.bones.size()!=bones.size())return socket_;
    for(std::size_t i=0;i<bones.size();++i)if(c.model.bones[i].name!=bones[i])return socket_;
    const auto near=[](float a,float b){return std::isfinite(a) && std::abs(a-b)<.02F;};
    V lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};
    for(const auto& sub:c.model.submodels)for(const auto& v:sub.vertices){const auto p=v.source_position;if(!finite(p))return socket_;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    if(!near(lo.x,-12.03F)||!near(lo.y,-14.99F)||!near(lo.z,-12.22F)||!near(hi.x,29.07F)||!near(hi.y,5.89F)||!near(hi.z,10.08F))return socket_;
    // Inspected original p_egon reference front (+X). Derive its source-bone
    // centroid once; the host evaluates it in the current merged player pose.
    try {
        goldsrc::studio::StudioPoseInput input;const auto pose=goldsrc::studio::StudioPoseEvaluator{}.evaluate({"egon-reference",c.revision},c.model,input);
        if(!pose)return socket_;float front=-1e9F;V center{};unsigned count=0;
        const auto world_x=[&](const auto& v){const auto& b=pose.pose->world_bones()[v.position_bone_index].transform.values;const auto p=v.source_position;return b[0]*p.x+b[1]*p.y+b[2]*p.z+b[3];};
        for(const auto& sub:c.model.submodels)for(const auto& v:sub.vertices)front=std::max(front,world_x(v));
        for(const auto& sub:c.model.submodels)for(const auto& v:sub.vertices)if(world_x(v)>=front-.25F){if(v.position_bone_index!=9)return socket_;center.x+=v.source_position.x;center.y+=v.source_position.y;center.z+=v.source_position.z;++count;}
        if(!count)return socket_;const auto n=static_cast<float>(count);center={center.x/n,center.y/n,center.z/n};
        socket_.source=api::RemoteSocketSource::validated_pmodel_socket;socket_.muzzle={9,center,{1,0,0},{0,-1,0},{0,0,1}};socket_.shell=socket_.muzzle;
    } catch(...) {return socket_;}
    return socket_;
}
}

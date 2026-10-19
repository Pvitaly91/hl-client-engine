#include <hlclient/games/halflife/gauss.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace hlclient::games::halflife {
namespace {
namespace api=game_api;using V=assets::AssetVector3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};} V mul(V a,float b){return {a.x*b,a.y*b,a.z*b};}
V sub(V a,V b){return add(a,mul(b,-1));}float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool finite(V a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
std::optional<V> unit(V a){const auto n=std::hypot(a.x,a.y,a.z);if(!finite(a)||n<.001F)return {};return mul(a,1/n);}
api::LocalSoundReference sound(std::string_view name){api::LocalSoundReference r;r.source=api::SoundReferenceSource::pinned_halflife_client_sound_profile;std::copy(name.begin(),name.end(),r.sample.begin());return r;}
std::string_view token(const api::ScriptedEventBinding& b){const auto n=std::find(b.name.begin(),b.name.end(),'\0');return {b.name.data(),static_cast<std::size_t>(n-b.name.begin())};}
}
bool gauss_sequences(const api::LocalWeaponModelMetadata& m) noexcept {
    // Pinned SDK roles + inspected model labels, not arbitrary sequence indices.
    constexpr std::array<std::string_view,9> names{"idle","idle2","fidget","spinup","spin","fire","fire2","holster","draw"};
    if(m.resource_name!="models/v_gauss.mdl" || m.sequences.size()!=names.size() || !m.supported_bodies[2])return false;
    for(std::size_t i=0;i<names.size();++i)if(m.sequences[i].label!=names[i] || !m.sequences[i].frame_count ||
        !std::isfinite(m.sequences[i].fps)||m.sequences[i].fps<=0)return false;return true;
}
std::span<const api::LocalSoundReference> GaussPresentation::resources() noexcept {
    static const std::array sounds{sound("ambience/pulsemachine.wav"),sound("weapons/gauss2.wav"),
        sound("weapons/357_cock1.wav"),sound("weapons/electro4.wav"),sound("weapons/electro6.wav")};return sounds;
}
void GaussPresentation::reset(api::GameSessionIdentity identity) noexcept { *this=GaussPresentation{};identity_=identity; }
void GaussPresentation::configure(std::span<const api::ScriptedEventBinding> bindings) noexcept {
    fire_event_=spin_event_=0;bool fire_conflict=false,spin_conflict=false;
    for(const auto& b:bindings) {
        if(!b.index||b.index>1023)continue;
        const auto name=token(b);
        if(name=="events/gauss.sc") {fire_conflict|=fire_event_!=0 && fire_event_!=b.index;fire_event_=b.index;}
        if(name=="events/gaussspin.sc") {spin_conflict|=spin_event_!=0 && spin_event_!=b.index;spin_event_=b.index;}
    }
    if(fire_conflict)fire_event_=0;if(spin_conflict)spin_event_=0;
    if(fire_event_==spin_event_)fire_event_=spin_event_=0;
    for(const auto& b:bindings) {
        if(b.index==fire_event_ && token(b)!="events/gauss.sc")fire_event_=0;
        if(b.index==spin_event_ && token(b)!="events/gaussspin.sc")spin_event_=0;
    }
}
void GaussPresentation::bind(const std::optional<api::LocalWeaponModelMetadata>& model) {
    if(model_==model)return;cancel(now_);model_=model;selected_=eligible_=false;
    reservations_=0;snapshot_={};cooldown_=idle_at_=completed_secondary_echo_until_=0;
}
void GaussPresentation::voice(api::LocalVoiceControl::Operation operation,std::uint32_t emitter,float pitch,double now,std::optional<V> origin) noexcept {
    if(emitter>=voice_id_.size())return;
    if(operation==api::LocalVoiceControl::Operation::start)voice_id_[emitter]=(++voice_lifetime_<<6)|(emitter+1U);
    if(!voice_id_[emitter])return;
    if(control_count_==controls_.size()) {++capacity_;return;}
    auto& c=controls_[control_count_++];c={};c.operation=operation;c.identity=voice_id_[emitter];c.serial=++serial_;
    c.reference=resources()[0];c.pitch=pitch;c.world_origin=origin;c.scheduled_seconds=now;
    if(operation==api::LocalVoiceControl::Operation::stop)voice_id_[emitter]=0;
}
void GaussPresentation::cue(std::string_view name,double now,float volume,float pitch,std::optional<V> origin) noexcept {
    if(cue_count_==cues_.size()) {++capacity_;return;}auto& c=cues_[cue_count_++];c={};
    c.reference=sound(name);c.scheduled_seconds=now;c.volume=volume;c.pitch=pitch;c.channel=api::LocalSoundChannel::automatic;
    c.kind=api::LocalSoundKind::fire;c.world_origin=origin;c.attenuation=origin ? .8F : 0;
}
void GaussPresentation::cancel(double now) noexcept {
    if(diagnostic_charge_ && diagnostic_observer_ && diagnostic_observer_->weapon_diagnostics_enabled()) {
        api::WeaponActionDiagnosticObservation o;o.identity=*diagnostic_charge_;
        o.mode=api::WeaponDiagnosticMode::secondary;o.kind=core::MultiplayerActionKind::charge;
        o.phase=core::MultiplayerActionPhase::end;diagnostic_observer_->weapon_action_observed(o);
    }
    diagnostic_charge_.reset();
    if(charge_phase_) {
        voice(api::LocalVoiceControl::Operation::stop,0,1,std::max(now,now_));
        snapshot_.visual.reset(); // no provisional spin remains after cancellation
    }
    charge_phase_=0;charging_seen_=false;charge_command_owned_=false;idle_at_=0;
}
void GaussPresentation::observe(const client::RuntimeClientObservationState& s,double now) noexcept {
    if(s.generation!=identity_.network_generation || !std::isfinite(now) || now<0 || s.publication_revision<revision_)return;
    now_=std::max(now_,now);const auto previous=selected_;selected_=false;eligible_=false;
    const auto type=std::ranges::find(s.weapon_hud.catalogue,9U,&client::RuntimeWeaponTypeObservation::id);
    const auto weapon=std::ranges::find(s.weapon_slots,std::optional<std::uint32_t>{9},&client::RuntimeWeaponSlotObservation::weapon_id);
    if(!model_ || !gauss_sequences(*model_) || s.weapon_hud.active_weapon_id!=9 ||
        type==s.weapon_hud.catalogue.end() || type->command_name!="weapon_gauss" || type->primary_ammo_type<0 ||
        static_cast<std::size_t>(type->primary_ammo_type)>=s.weapon_hud.reserve_ammo.size() ||
        !s.receiving_client || s.receiving_client->viewmodel_index!=model_->model_index) {if(previous) {cancel(now);reservations_=0;snapshot_={};completed_secondary_echo_until_=0;}return;}
    selected_=true;const auto& c=*s.receiving_client;
    if(life_!=s.lifecycle.life_epoch || s.lifecycle.dead() || c.health.value_or(0)<=0) {cancel(now);reservations_=0;snapshot_={};cooldown_=completed_secondary_echo_until_=0;}
    life_=s.lifecycle.life_epoch;
    if(!c.health || *c.health<=0 || s.lifecycle.dead() || !c.owned_weapon_bits || !(*c.owned_weapon_bits&(1U<<9)) ||
        !c.water_level || *c.water_level>3 || !c.next_weapon_attack ||
        s.client_metadata.freshness==client::RuntimeObservationFreshness::unavailable ||
        weapon==s.weapon_slots.end() || !weapon->auxiliary_integer2 || *weapon->auxiliary_integer2<0 || *weapon->auxiliary_integer2>2 ||
        !weapon->next_primary_attack || !weapon->next_secondary_attack || !c.auxiliary_float2 || !c.auxiliary_float3) {cancel(now);return;}
    const auto ammo=s.weapon_hud.reserve_ammo[static_cast<std::size_t>(type->primary_ammo_type)];
    if(!ammo || *ammo>100) {cancel(now);return;}
    if(s.publication_revision!=revision_) {
        const auto consumed=std::max(0,ammo_-static_cast<int>(*ammo));
        if(revision_ && ammo_<*ammo) {/* pickup does not erase pending consumption */}
        else if(revision_ && ammo_>*ammo)reservations_=std::max(0,reservations_-(ammo_-*ammo));
        ammo_=*ammo;revision_=s.publication_revision;observed_at_=now;
        // Keep the existing bounded completed-charge echo protection separate
        // from attack readiness and the latest visual. A new local charge or
        // primary may already have started when the preceding phase arrives.
        // Receipt time cannot prove which immutable command a phase describes.
        const bool completed_secondary_echo=now<completed_secondary_echo_until_;
        if(s.client_metadata.freshness==client::RuntimeObservationFreshness::observed_in_record) {
            attack_ready_at_=now+std::max({0.0,*c.next_weapon_attack,*weapon->next_primary_attack,*weapon->next_secondary_attack});
            if(*weapon->auxiliary_integer2>0 && !completed_secondary_echo) {
                charging_seen_=true;
                const auto full=multiplayer_ ? 1.5 : 4.0;
                // Only fresh negotiated weapon/client timers reconcile the
                // provisional clock. Retained records never advance deadlines.
                // The clamped post-full timer cannot recover overcharge age.
                const auto remaining=*c.auxiliary_float3;
                if(multiplayer_known_ && std::isfinite(remaining) && remaining>=-.0011 && remaining<=full) {
                    if(!charge_phase_) {
                        charge_command_owned_=false;
                        charge_phase_=static_cast<unsigned>(*weapon->auxiliary_integer2);
                        charge_started_=now-full+std::max(0.0,remaining);
                        idle_at_=std::max(now,charge_started_+.5);
                        animate(charge_phase_==1 ? 3 : 4,now);
                        voice(api::LocalVoiceControl::Operation::start,0,1.1F,now);
                    } else if(remaining>0) {
                        const auto reconciled_start=now-full+remaining;
                        charge_started_=charge_command_owned_ ? std::min(charge_started_,reconciled_start) : reconciled_start;
                    }
                    if(*weapon->auxiliary_integer2==2 && charge_phase_==1) {charge_phase_=2;animate(4,now);}
                    const auto burn=*c.auxiliary_float2;
                    if(std::isfinite(burn) && burn>=-.0011 && burn<=1000) {
                        const auto reconciled_burn=burn>=999 ? 1000 : now+std::max(0.0,burn);
                        // Receipt time is not the source command clock. An
                        // accepted-command charge must not postpone a due burn
                        // (and its exhaustion discharge) behind a delayed echo.
                        if(reconciled_burn>=999 || !charge_command_owned_)
                            burn_at_=reconciled_burn;
                        else if(burn_at_<999)
                            burn_at_=std::min(burn_at_,reconciled_burn);
                    }
                    if(remaining<=0)burn_at_=1000;
                }
            }
            else if(*weapon->auxiliary_integer2==0 && charging_seen_ && charge_phase_)cancel(now);
        }
        // Exact neutral fuser2 means remaining ammo-burn timer; 1000 is the
        // server full-charge sentinel. fuser3 is the remaining full-charge timer.
        if(!completed_secondary_echo && charging_seen_ && charge_phase_ && *c.auxiliary_float2>=999)burn_at_=1000;
        if(consumed && snapshot_.identity && !charge_phase_ && *weapon->auxiliary_integer2==0 &&
            snapshot_.status==api::LocalWeaponActionStatus::predicted_pending) {
            snapshot_.status=api::LocalWeaponActionStatus::server_confirmed;++snapshot_.actions_confirmed;
            if(snapshot_.visual)snapshot_.visual->status=snapshot_.status;
        }
    }
    water_=*c.water_level;
    if(water_==3 && charge_phase_) {cue("weapons/electro4.wav",now,1,1);cancel(now);animate(0,now);cooldown_=now+.5;}
    eligible_=multiplayer_known_ && std::isfinite(attack_ready_at_) && std::isfinite(*c.auxiliary_float2) && std::isfinite(*c.auxiliary_float3);
}
void GaussPresentation::animate(std::uint32_t seq,double now,api::LocalWeaponActionStatus status) noexcept {
    snapshot_.visual=api::LocalWeaponVisual{seq,2,++restart_,now,api::LocalWeaponAnimationSource::provisional_command,status};snapshot_.status=status;
}
void GaussPresentation::queue(Shot shot) noexcept {
    auto slot=std::find_if(shots_.begin(),shots_.end(),[](const auto& s){return !s;});
    if(slot==shots_.end()) {++capacity_;return;} *slot=shot;++shot_count_;
}
std::optional<V> GaussPresentation::fire(const api::LocalWeaponSubmittedCommand& c,Discharge reason,float energy) noexcept {
    if(!c.shot_context || !unit(c.shot_context->direction) || !finite(c.shot_context->eye)) {++rejected_;cancel(c.sample_end_seconds);return {};}
    const auto now=c.sample_end_seconds;const bool primary=reason==Discharge::primary;
    const auto charge_command=diagnostic_charge_ ? std::optional{diagnostic_charge_->command_sequence} : std::nullopt;
    cancel(now);
    Identity id{identity_.network_generation,9,model_->model_index,model_->resource_revision,c.sequence,
        primary ? api::LocalWeaponAction::primary_fire : api::LocalWeaponAction::secondary_fire,now,revision_};
    snapshot_.identity=id;snapshot_.action=id.kind;++snapshot_.actions_started;
    if(diagnostic_observer_ && diagnostic_observer_->weapon_diagnostics_enabled()) {
        api::WeaponActionDiagnosticObservation o;o.identity=id;o.command=c;
        o.mode=primary ? api::WeaponDiagnosticMode::primary : api::WeaponDiagnosticMode::secondary;
        o.kind=core::MultiplayerActionKind::beam;o.reserve=ammo_;o.next_attack=attack_ready_at_;
        o.related_command_sequence=charge_command;
        diagnostic_observer_->weapon_action_observed(o);
    }
    if(primary)++snapshot_.primary_fire_starts;else ++snapshot_.secondary_fire_starts;
    ++snapshot_.recoil_starts;animate(6,now);
    // SDK WeaponIdle release sets an idle deadline, not an attack cooldown.
    // Only SecondaryAttack's forced exhaustion owns the one-second gate.
    idle_at_=now+(reason==Discharge::released ? 2.0 : 1.0);
    cooldown_=now+(primary ? .2 : reason==Discharge::exhausted ? 1.0 : 0.0);
    if(!primary)completed_secondary_echo_until_=now+1.0;
    queue({id,c.shot_context->eye,*unit(c.shot_context->direction),energy,primary,now});
    cue("weapons/gauss2.wav",now,.5F+energy/400,static_cast<float>(85+c.sequence%32)/100);
    if(primary)return {};const auto impulse=mul(*unit(c.shot_context->direction),-energy*5);
    return multiplayer_ ? impulse : V{impulse.x,impulse.y,0};
}
std::optional<V> GaussPresentation::command(const api::LocalWeaponSubmittedCommand& c) noexcept {
    if(!selected_ || c.generation!=identity_.network_generation || !std::isfinite(c.sample_end_seconds) || c.sample_end_seconds<0 ||
        c.sequence<=command_floor_) {if(selected_ && c.sequence<=command_floor_)++snapshot_.duplicate_submissions;return {};}
    command_floor_=c.sequence;now_=std::max(now_,c.sample_end_seconds);const auto at=c.sample_end_seconds;
    const bool held=(c.buttons&(1U<<11))!=0;const bool primary=(c.buttons&1)!=0;
    const auto rejected=[&](std::string_view reason) {
        if(!(held||primary) || !diagnostic_observer_ || !diagnostic_observer_->weapon_diagnostics_enabled())return;
        api::WeaponActionDiagnosticObservation o;
        o.identity={identity_.network_generation,9,model_ ? model_->model_index : 0,model_ ? model_->resource_revision : 0,
            c.sequence,held ? api::LocalWeaponAction::secondary_fire : api::LocalWeaponAction::primary_fire,at,revision_};
        o.command=c;o.mode=held ? api::WeaponDiagnosticMode::secondary : api::WeaponDiagnosticMode::primary;
        o.kind=held ? core::MultiplayerActionKind::charge : core::MultiplayerActionKind::beam;
        o.phase=core::MultiplayerActionPhase::rejected;o.reason=reason;o.reserve=ammo_;o.next_attack=attack_ready_at_;
        diagnostic_observer_->weapon_action_observed(o);
    };
    if(!eligible_) {rejected("local_model_or_state_unavailable");return {};}
    if(!charge_phase_ && (at<cooldown_ || at<attack_ready_at_)){rejected("local_attack_timer_gate");return {};}
    if(water_==3) {rejected("local_underwater_gate");cancel(at);if(held||primary) {cue("weapons/357_cock1.wav",at,.8F,1);cooldown_=at+(held ? .5 : .15);}return {};}
    const auto available=std::max(0,ammo_-reservations_);
    if(primary && !held) {
        if(at<cooldown_){rejected("local_attack_timer_gate");return {};}
        if(available<2) {rejected("local_ammo_gate");cue("weapons/357_cock1.wav",at,.8F,1);cooldown_=at+.5;return {};}
        reservations_+=2;return fire(c,Discharge::primary,20);
    }
    if(held && !charge_phase_) {
        if(available==0) {rejected("local_ammo_gate");cue("weapons/357_cock1.wav",at,.8F,1);cooldown_=at+.5;return {};}
        ++reservations_;charge_phase_=1;charging_seen_=false;charge_command_owned_=true;charge_started_=burn_at_=at;idle_at_=at+.5;
        if(diagnostic_observer_ && diagnostic_observer_->weapon_diagnostics_enabled()) {
            diagnostic_charge_=Identity{identity_.network_generation,9,model_->model_index,model_->resource_revision,
                c.sequence,api::LocalWeaponAction::secondary_fire,at,revision_};
            api::WeaponActionDiagnosticObservation o;o.identity=*diagnostic_charge_;o.command=c;o.reserve=ammo_;
            o.mode=api::WeaponDiagnosticMode::secondary;o.kind=core::MultiplayerActionKind::charge;
            o.phase=core::MultiplayerActionPhase::begin;diagnostic_observer_->weapon_action_observed(o);
        }
        animate(3,at);voice(api::LocalVoiceControl::Operation::start,0,1.1F,at);return {};
    }
    if(!charge_phase_)return {};
    const auto elapsed=std::max(0.0,at-charge_started_);const double full=multiplayer_ ? 1.5 : 4;
    if(elapsed>10) {cue("weapons/electro4.wav",at,1,1);cue("weapons/electro6.wav",at,1,1);cancel(at);animate(0,at);cooldown_=at+1;return {};}
    // WeaponIdle cannot discharge before its initial spin-up idle deadline.
    if(!held && at>=idle_at_)return fire(c,Discharge::released,static_cast<float>(200*std::min(1.0,elapsed/full)));
    if(charge_phase_==1 && at>idle_at_) {charge_phase_=2;animate(4,at);return {};}
    if(charge_phase_==2 && held) {
        if(burn_at_<1000 && at>=burn_at_) {++reservations_;burn_at_=at+(multiplayer_ ? .1 : .3);}
        if(ammo_-reservations_<=0)return fire(c,Discharge::exhausted,static_cast<float>(200*std::min(1.0,elapsed/full)));
        if(elapsed>=full)burn_at_=1000;
        voice(api::LocalVoiceControl::Operation::change,0,static_cast<float>(std::min(250.0,100+elapsed*150/full))/100,at);
    }
    return {};
}
api::LocalWeaponPresentationSnapshot GaussPresentation::sample(double now) noexcept {
    if(!selected_)return {};
    now_=std::max(now_,now);
    if(idle_at_>0 && !charge_phase_ && now>=idle_at_) {animate(0,idle_at_);idle_at_=0;}
    auto result=snapshot_;
    if(result.visual && model_) {
        const auto age=std::max(0.0,now-result.visual->started_at_seconds);
        result.frame_coordinate=age*model_->sequences[result.visual->sequence].fps;
    }
    if(result.identity)result.local_punch_pitch_degrees=-2*std::max(0.0,1-(now-result.identity->started_at_seconds)/.2);
    result.maximum_visual_recoil=2;return result;
}
api::LocalVisualFrame GaussPresentation::visuals(double now) const noexcept {
    api::LocalVisualFrame out;
    if(!selected_ || !snapshot_.identity || !snapshot_.visual || !std::isfinite(now))return out;
    const auto& action=*snapshot_.identity;
    if(now<action.started_at_seconds || now>=action.started_at_seconds+.06)return out;
    // EV_FireGauss EF_MUZZLEFLASH on the first local segment; the imported
    // v_gauss attachment zero is evaluated by the existing camera-local pass.
    api::LocalMuzzleFlash flash;flash.action=action;flash.model_index=action.model_index;
    flash.sequence=6;flash.attachment_index=0;flash.starts_at_seconds=action.started_at_seconds;
    flash.ends_at_seconds=action.started_at_seconds+.06;out.flash=flash;
    api::LocalMuzzleLight light;light.action=action;light.starts_at_seconds=flash.starts_at_seconds;
    light.ends_at_seconds=flash.ends_at_seconds;out.light=light;return out;
}
void GaussPresentation::append_audio(api::LocalAudioBatch& batch) noexcept {
    if(selected_ || fire_event_ || spin_event_)for(const auto& r:resources())if(batch.prepare_count<batch.prepare.size())batch.prepare[batch.prepare_count++]=r;
    for(std::size_t i=0;i<cue_count_ && batch.count<batch.cues.size();++i) {auto c=cues_[i];c.scope=batch.scope;batch.cues[batch.count++]=c;}cue_count_=0;
    for(std::size_t i=0;i<control_count_ && batch.control_count<batch.controls.size();++i)batch.controls[batch.control_count++]=controls_[i];control_count_=0;
}
api::RemoteWeaponSocketPlan GaussPresentation::socket_plan(const api::RemoteWeaponModelContext& c) noexcept {
    if(c.virtual_name!="models/p_gauss.mdl")return {};
    if(socket_resource_==c.resource_id && socket_revision_==c.revision)return socket_;
    socket_resource_=c.resource_id;socket_revision_=c.revision;socket_={};socket_.semantic_match=true;socket_.semantic_key=9;
    constexpr std::array<std::string_view,12> names{"Bip01","Bip01 Pelvis","Bip01 Spine","Bip01 Spine1",
        "Bip01 Spine2","Bip01 Spine3","Bip01 Neck","Bip01 R Arm","Bip01 R Arm1","Bip01 R Arm2","Bip01 R Hand","Box02"};
    if(c.model.bones.size()!=names.size())return socket_;
    for(std::size_t i=0;i<names.size();++i)if(c.model.bones[i].name!=names[i] || c.model.bones[i].parent_index!=static_cast<int>(i)-1)return socket_;
    const auto near=[](float a,float b){return std::isfinite(a)&&std::abs(a-b)<.02F;};
    const auto& bone=c.model.bones[11];
    if(!near(bone.default_translation.x,8.02966F)||!near(bone.default_translation.y,.950843F)||!near(bone.default_translation.z,4.03907F)||
        !near(bone.default_rotation_radians.x,1.54315F)||!near(bone.default_rotation_radians.y,.002126F)||!near(bone.default_rotation_radians.z,-3.04524F))return socket_;
    V lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F},end{};unsigned count=0;
    for(const auto& submodel:c.model.submodels)for(const auto& vertex:submodel.vertices) {
        if(vertex.position_bone_index!=11 || !finite(vertex.source_position))return socket_;
        const auto p=vertex.source_position;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    if(!near(lo.x,-20.43F)||!near(lo.y,-4.88F)||!near(lo.z,-3.1F)||!near(hi.x,15.84F)||!near(hi.y,3.96F)||!near(hi.z,3.53F))return socket_;
    // Inspected -X barrel-end ring, evaluated in the existing merged Box02
    // pose. This is a documented compatible socket, not a stock attachment.
    for(const auto& submodel:c.model.submodels)for(const auto& vertex:submodel.vertices)
        if(near(vertex.source_position.x,lo.x)) {end=add(end,vertex.source_position);++count;}
    if(!count)return socket_;end=mul(end,1.0F/static_cast<float>(count));end.x-=.1F;
    socket_.source=api::RemoteSocketSource::validated_pmodel_socket;
    socket_.muzzle={11,end,{-1,0,0},{0,0,-1},{0,1,0}};socket_.shell=socket_.muzzle;return socket_;
}
void GaussPresentation::poses(std::span<const api::RemoteWeaponPoseSample> poses) noexcept {
    poses_.fill(std::nullopt);for(const auto& p:poses)if(p.entity && p.entity<=32 && p.generation==identity_.network_generation) {
        poses_[p.entity-1]=p;
        if(remote_charging_[p.entity-1] && p.semantic_known && p.semantic_key!=9) {
            voice(api::LocalVoiceControl::Operation::stop,p.entity,1,now_);remote_charging_[p.entity-1]=false;
        } else if(remote_charging_[p.entity-1] && p.semantic_key==9)
            voice(api::LocalVoiceControl::Operation::source,p.entity,1,now_,p.muzzle.origin);
    }
}
void GaussPresentation::slot_boundary(std::uint32_t entity) noexcept {
    if(!entity||entity>32)return;voice(api::LocalVoiceControl::Operation::stop,entity,1,now_);remote_charging_[entity-1]=false;poses_[entity-1].reset();remote_floor_[entity-1]=~std::uint64_t{};
}
void GaussPresentation::entities(std::span<const client::RuntimePacketEntityObservation> entities,std::uint64_t ordinal) noexcept {
    if(!ordinal || ordinal<=entity_ordinal_)return;entity_ordinal_=ordinal;
    for(unsigned i=0;i<32;++i)if(remote_charging_[i] && std::ranges::find(entities,i+1U,&client::RuntimePacketEntityObservation::entity_number)==entities.end()) {
        voice(api::LocalVoiceControl::Operation::stop,i+1U,1,now_);remote_charging_[i]=false;remote_floor_[i]=ordinal;
    }
    for(const auto& e:entities)if(e.entity_number && e.entity_number<=32) {
        auto& floor=remote_floor_[e.entity_number-1];if(floor==~std::uint64_t{})floor=ordinal;
        if(e.weapon_model_index.value_or(0)==0 || (e.effects.value_or(0)&128)) {
            if(remote_charging_[e.entity_number-1])voice(api::LocalVoiceControl::Operation::stop,e.entity_number,1,now_);
            remote_charging_[e.entity_number-1]=false;floor=ordinal;
        }
    }
}
void GaussPresentation::event(const api::CommittedScriptedEvent& e) noexcept {
    if(e.generation!=identity_.network_generation || !e.emitter_entity || e.emitter_entity>32 || !e.record || !e.record_ordinal ||
        (e.resolution!=api::ScriptedEventResolution::ready && !(e.resolution==api::ScriptedEventResolution::unsupported_delay && e.fire_delay_ticks<=10)) ||
        !std::isfinite(e.received_at_seconds) || e.received_at_seconds<0 ||
        (e.event_index!=fire_event_ && e.event_index!=spin_event_))return;
    const std::array key{e.record_ordinal,std::uint64_t(e.message_bit_offset),std::uint64_t(e.entry_ordinal)};
    if((event_floor_ && key<=*event_floor_) || std::ranges::find(seen_,key)!=seen_.end()) {++snapshot_.duplicate_events;return;}
    auto& evicted=seen_[next_seen_++%seen_.size()];if(evicted[0] && (!event_floor_ || evicted>*event_floor_))event_floor_=evicted;evicted=key;
    const bool local=identity_.receiving_entity==e.emitter_entity;const auto at=e.received_at_seconds+e.fire_delay_ticks*.01;
    if(!local && e.record_ordinal<=remote_floor_[e.emitter_entity-1])return;
    // STOP flag is processed before magnitude/trace/weapon/direction validation.
    if(e.event_index==fire_event_ && e.boolean_parameter2.value_or(false)) {
        // Local commands/observations already retire their exact voice. This
        // reliable echo may belong to an older charge, not the current voice.
        if(local)return;
        voice(api::LocalVoiceControl::Operation::stop,e.emitter_entity,1,at);
        remote_charging_[e.emitter_entity-1]=false;return;
    }
    if(e.event_index==spin_event_) {
        if(!e.integer_parameter1 || *e.integer_parameter1<1 || *e.integer_parameter1>250 || !e.boolean_parameter1)return;
        if(local)return; // local exact command timeline owns prediction, echoes don't restart it
        if(!finite(e.origin))return;
        const auto change=*e.boolean_parameter1;
        if((change && !remote_charging_[e.emitter_entity-1]) || (!change && remote_charging_[e.emitter_entity-1]))return;
        voice(change ? api::LocalVoiceControl::Operation::change : api::LocalVoiceControl::Operation::start,e.emitter_entity,
            static_cast<float>(*e.integer_parameter1)/100,at,e.origin);remote_charging_[e.emitter_entity-1]=true;remote_update_at_[e.emitter_entity-1]=at;return;
    }
    if(!e.float_parameter1 || !std::isfinite(*e.float_parameter1) || *e.float_parameter1<0 || *e.float_parameter1>200 || !e.boolean_parameter1 || !e.boolean_parameter2)return;
    const auto energy=*e.float_parameter1;const auto primary=*e.boolean_parameter1;
    // A new spin retains the previous fire identity for its visual tail, but
    // an old fire echo cannot confirm that new, not-yet-discharged animation.
    if(local && !charge_phase_ && snapshot_.identity && at-snapshot_.identity->started_at_seconds>=-.05 && at-snapshot_.identity->started_at_seconds<=.5 &&
        ((snapshot_.identity->kind==api::LocalWeaponAction::primary_fire)==primary)) {
        if(snapshot_.status!=api::LocalWeaponActionStatus::server_confirmed)++snapshot_.actions_confirmed;
        snapshot_.status=api::LocalWeaponActionStatus::server_confirmed;
        if(snapshot_.visual)snapshot_.visual->status=snapshot_.status;return;
    }
    if(local)return;
    voice(api::LocalVoiceControl::Operation::stop,e.emitter_entity,1,at);remote_charging_[e.emitter_entity-1]=false;
    // SDK Gauss explicitly publishes pev->angles; packet fallback carries
    // the same HL model pitch. angles_from_entity describes wire provenance,
    // not a different aiming-angle convention for this weapon.
    const auto pitch=-3.0*std::remainder(double(e.angles.x),360.0);
    const auto p=pitch*std::numbers::pi/180,y=double(e.angles.y)*std::numbers::pi/180;
    const V direction{static_cast<float>(std::cos(p)*std::cos(y)),static_cast<float>(std::cos(p)*std::sin(y)),static_cast<float>(-std::sin(p))};
    if(!finite(e.origin)||!unit(direction))return;
    V origin=e.origin;origin.z+=e.ducking ? 12 : 28;
    Identity id{identity_.network_generation,9,e.weapon_model_index.value_or(0),1,static_cast<std::uint32_t>(e.record_ordinal),
        primary ? api::LocalWeaponAction::primary_fire : api::LocalWeaponAction::secondary_fire,at,e.record,e.emitter_entity};
    queue({id,origin,direction,energy,primary,at});cue("weapons/gauss2.wav",at,.5F+energy/400,.85F+static_cast<float>(e.record%32)*.01F,e.origin);
}
void GaussPresentation::sprite(const Shot& s,V a,V b,bool ribbon,float scale,float opacity,double lifetime,std::optional<V> velocity,std::optional<V> normal,
    std::optional<api::EffectSurfaceAttachment> attachment) noexcept {
    auto slot=std::find_if(sprites_.begin(),sprites_.end(),[&](const auto& v){return !v || v->expires<=now_;});
    if(slot==sprites_.end()) {++capacity_;return;}api::BeamSpriteFrame f;f.action=s.action;f.start=a;f.end=b;f.ribbon=ribbon;
    const std::string_view name=ribbon ? "sprites/smoke.spr" : "sprites/hotglow.spr";std::copy(name.begin(),name.end(),f.virtual_sprite.begin());
    f.scale=scale;f.opacity=std::clamp(opacity,0.0F,1.0F);f.width=s.primary ? 1 : 2.5F;f.starts=s.at;f.expires=s.at+std::clamp(lifetime,.01,6.0);
    if(ribbon) {
        f.width=(s.primary ? 1.75F : 3.5F)*(s.action.emitter_entity ? 1.7F : 1.0F);
        // Local-compatible visual profile: the existing rounded MP5 solid
        // streak shape, three times its 48-unit length, without new physics.
        f.geometry=api::WorldLineGeometry::solid_3d;f.travel_speed_units_per_second=12000;
        f.motion_starts=now_;
        f.segment_length_units=144;f.head_taper_fraction=.90F;f.taper_end_diameter_fraction=.20F;
        f.emission_gain=3;f.opacity=1;f.head_glow={2.5F,64,1};
        f.color=s.primary ? std::array<float,3>{1,1,0} : std::array<float,3>{1,1,1};
    }
    if(velocity)f.velocity=*velocity;
    if(!ribbon && !velocity && normal) {
        f.surface_normal=unit(*normal);
        if(f.surface_normal)f.surface_attachment=attachment;
    }
    *slot=f;
}
api::BeamEffectsFrame GaussPresentation::frame(const api::IStaticEffectTrace& query,double now,std::optional<V> muzzle) noexcept {
    api::BeamEffectsFrame out;if(!std::isfinite(now)||now<0)return out;now_=std::max(now_,now);
    for(unsigned i=0;i<32;++i)if(remote_charging_[i] && now-remote_update_at_[i]>1) {
        voice(api::LocalVoiceControl::Operation::stop,i+1U,1,now);remote_charging_[i]=false;
    }
    for(auto& pending:shots_)if(pending) {
        auto s=*pending;if(now<s.at)continue;pending.reset();if(now-s.at>.25) {++rejected_;continue;}
        bool punched=false;V start=s.origin,dir=s.direction;float energy=s.energy;
        for(unsigned i=0;i<10 && energy>10;++i) {
            const auto end=add(start,mul(dir,8192));const auto hit=query.trace(start,end);
            if(!hit.valid || hit.start_solid || hit.all_solid || hit.unsupported) {++rejected_;break;}
            V visual_start=start;
            if(i==0) {
                if(!s.action.emitter_entity && muzzle && selected_ && model_ &&
                    model_->model_index==s.action.model_index)visual_start=*muzzle;
                else if(s.action.emitter_entity && s.action.emitter_entity<=32) {
                    const auto& pose=poses_[s.action.emitter_entity-1];
                    if(pose && pose->semantic_key==9 && pose->source!=api::RemoteSocketSource::unavailable &&
                        std::abs(pose->presented_at_seconds-s.at)<.25)visual_start=pose->muzzle.origin;
                }
            }
            sprite(s,visual_start,hit.point,true,1,s.primary ? 128.0F/255 : energy/255,.15);
            if(!hit.hit)break;const auto normal=unit(hit.normal);if(!normal)break;
            const auto n=std::clamp(-dot(*normal,dir),0.0F,1.0F);
            const auto balls=[&](V point,unsigned count,double lifetime,float speed) {
                for(unsigned k=0;k<std::min(count,60U);++k) {
                    const float angle=static_cast<float>((s.action.command_sequence*31+k*137+i*17)%360)*std::numbers::pi_v<float>/180;
                    const float z=static_cast<float>(int(k%5)-2)*.3F;
                    const V v{std::cos(angle)*speed,std::sin(angle)*speed,z*speed};sprite(s,point,point,false,.1F+(k%2)*.1F,1,lifetime,v);
                }
            };
            if(n<.5F) {
                const auto reflected=unit(add(dir,mul(*normal,2*n)));if(!reflected)break;
                sprite(s,hit.point,hit.point,false,.2F,energy*n/255,energy*n*.05,{},*normal,hit.surface_attachment);balls(hit.point,3,.1,100);
                dir=*reflected;start=add(hit.point,mul(dir,8));energy*=1-(n==0 ? .1F : n);++reflections_;continue;
            }
            const auto decal=[&](V point,V outward,unsigned sub_identity) {
                if(out.impact_count==out.impacts.size()) {++capacity_;return;}
                auto& impact=out.impacts[out.impact_count++];impact.action=s.action;impact.action.sub_identity=sub_identity;
                impact.origin=add(point,mul(outward,2));impact.direction=mul(outward,-1);impact.maximum_distance_units=4;
                impact.expires_at_seconds=now+10;
            };
            decal(hit.point,*normal,i*2+1);sprite(s,hit.point,hit.point,false,1,energy/255,6,{},*normal,hit.surface_attachment);
            if(s.primary) {sprite(s,hit.point,hit.point,false,.2F,200.0F/255,.3,{},*normal,hit.surface_attachment);balls(hit.point,8,.6,100);break;}
            if(punched)break;punched=true;
            const auto beyond=query.trace(add(hit.point,mul(dir,8)),end);
            if(!beyond.valid || beyond.all_solid || beyond.unsupported)break;
            const auto back=query.trace(beyond.point,hit.point);
            if(!back.valid||back.start_solid||back.all_solid||!back.hit||back.unsupported)break;
            const auto thickness=std::hypot(back.point.x-hit.point.x,back.point.y-hit.point.y,back.point.z-hit.point.z);
            if(thickness>=energy)break;energy-=std::max(1.0F,thickness);
            balls(hit.point,3,.1,100);decal(back.point,back.normal,i*2+2);
            sprite(s,back.point,back.point,false,.1F,energy/255,6,{},back.normal,back.surface_attachment);balls(back.point,static_cast<unsigned>(energy*.3F),.1,200);
            start=add(back.point,dir);++penetrations_;
        }
    }
    for(auto& slot:sprites_)if(slot) {
        if(slot->expires<=now) {slot.reset();continue;}if(slot->starts>now)continue;
        auto f=*slot;const auto age=static_cast<float>(now-f.starts);
        f.start=add(f.start,mul(f.velocity,age));f.end=add(f.end,mul(f.velocity,age));
        if(!f.ribbon)f.opacity*=static_cast<float>((f.expires-now)/(f.expires-f.starts));
        out.sprites[out.count++]=f;
    }
    out.shots=shot_count_;out.reflections=reflections_;out.penetrations=penetrations_;out.rejected=rejected_;out.capacity=capacity_;return out;
}
}

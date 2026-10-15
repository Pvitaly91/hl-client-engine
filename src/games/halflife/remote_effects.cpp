#include <hlclient/games/halflife/remote_effects.hpp>
#include <hlclient/games/halflife/weapon_visuals.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/crossbow_profile.hpp>
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::games::halflife {
namespace {
using V=assets::AssetVector3;
using namespace game_api;
bool finite(V v) noexcept { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool socket_basis(const assets::EvaluatedModelSocket& socket) noexcept {
    const auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    return finite(socket.origin)&&finite(socket.forward)&&finite(socket.right)&&finite(socket.up)&&
        std::abs(dot(socket.forward,socket.forward)-1)<0.01F&&
        std::abs(dot(socket.right,socket.right)-1)<0.01F&&
        std::abs(dot(socket.up,socket.up)-1)<0.01F&&
        std::abs(dot(socket.forward,socket.right))<0.01F&&
        std::abs(dot(socket.forward,socket.up))<0.01F&&std::abs(dot(socket.right,socket.up))<0.01F;
}
V add(V a,V b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V mul(V a,float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
std::string_view name(const ScriptedEventBinding& b) noexcept {
    const auto end=std::find(b.name.begin(),b.name.end(),'\0');
    return {b.name.data(),static_cast<std::size_t>(end-b.name.begin())};
}
LocalSoundReference sound(std::string_view token) noexcept {
    LocalSoundReference result;
    result.source=SoundReferenceSource::pinned_halflife_client_sound_profile;
    if(token.size()<result.sample.size()) std::copy(token.begin(),token.end(),result.sample.begin());
    return result;
}
bool glock(std::string_view token) noexcept {
    return token=="events/glock1.sc" || token=="events/glock2.sc";
}
bool shotgun(std::string_view token) noexcept {
    return token=="events/shotgun1.sc" || token=="events/shotgun2.sc";
}
}
void RemoteWeaponEffects::reset(std::uint64_t generation,std::optional<std::uint32_t> receiving,std::uint64_t map_generation) noexcept {
    generation_=generation; receiving_=receiving; binding_count_=pending_count_=0;
    map_generation_=map_generation ? map_generation : generation;
    serial_=action_sequence_=0; seen_={}; next_seen_=next_action_=0;
    actions_.fill(std::nullopt); audio_={}; stats_={};
    poses_={};pose_count_=0;socket_profiles_={};next_socket_profile_=0;
    visual_lives_={};entity_ordinal_=0;
}
void RemoteWeaponEffects::entities(std::span<const client::RuntimePacketEntityObservation> entities,
    std::uint32_t maximum_clients,std::uint64_t ordinal) noexcept {
    if(!ordinal || ordinal<=entity_ordinal_ || maximum_clients>visual_lives_.size()) return;
    entity_ordinal_=ordinal;
    for(std::size_t i=0;i<visual_lives_.size();++i) {
        auto& life=visual_lives_[i];
        const auto entity=std::ranges::find(entities,static_cast<std::uint32_t>(i+1U),
            &client::RuntimePacketEntityObservation::entity_number);
        const bool present=i<maximum_clients && entity!=entities.end() && entity->player_movement_schema;
        const bool inactive=present && (entity->weapon_model_index==0U ||
            (entity->effects.value_or(0U)&128U)!=0U);
        const auto model=present ? entity->model_index : std::nullopt;
        if(!life.observed || present!=life.present || model!=life.model || inactive!=life.inactive) {
            ++life.epoch; life.floor=ordinal;
            life.tracer_count=0;
        }
        life.observed=true;life.present=present;life.inactive=inactive;life.model=model;
    }
    // Owned pending events only. Already materialized shells/decals/voices have
    // independent world lifetimes and are deliberately not cancelled here.
}
void RemoteWeaponEffects::slot_boundary(std::uint32_t entity) noexcept {
    if(!entity || entity>visual_lives_.size()) return;
    auto& life=visual_lives_[entity-1U];++life.epoch;
    life.tracer_count=0;
    life.observed=true;life.present=false;life.floor=entity_ordinal_;life.model.reset();
}
RemoteWeaponSocketPlan RemoteWeaponEffects::socket_plan(const RemoteWeaponModelContext& context) noexcept {
    RemoteWeaponSocketPlan plan;
    const bool mp5=context.virtual_name=="models/p_9mmAR.mdl";
    const bool buckshot=context.virtual_name=="models/p_shotgun.mdl";
    const bool python=context.virtual_name=="models/p_357.mdl";
    if(context.virtual_name!="models/p_9mmhandgun.mdl" && !mp5 && !buckshot && !python) return plan;
    for(const auto& cached:socket_profiles_) if(cached.used && cached.resource_id==context.resource_id &&
        cached.revision==context.revision && cached.plan.semantic_key==(python ? 3U : buckshot ? 7U : mp5 ? 4U : 2U)) return cached.plan;
    plan.semantic_match=true;
    plan.semantic_key=python ? 3U : buckshot ? 7U : mp5 ? 4U : 2U;
    // Evidence-bound to the inspected p-model, not the session numeric slot.
    // One-time validation per immutable resource revision; no bone/vertex scan per frame.
    const auto validate=[&]() {
        const auto& model=context.model;
        const std::array<std::string_view,12> names{"Bip01","Bip01 Pelvis","Bip01 Spine",
            "Bip01 Spine1","Bip01 Spine2","Bip01 Spine3","Bip01 Neck","Bip01 R Arm",
            "Bip01 R Arm1","Bip01 R Arm2","Bip01 R Hand",python ? "Cylinder01" : buckshot ? "Box01" : mp5 ? "Line02" : "sides01"};
        if(model.bones.size()!=names.size()) return false;
        for(std::size_t i=0;i<names.size();++i)
            if(model.bones[i].name!=names[i] || model.bones[i].parent_index!=static_cast<std::int32_t>(i)-1) return false;
        const auto near=[](float a,float b){return std::isfinite(a)&&std::abs(a-b)<0.02F;};
        const auto& bone=model.bones[11];
        if(python ? !near(bone.default_translation.x,5.854939F)||!near(bone.default_translation.y,0.808541F)||
            !near(bone.default_translation.z,2.847965F)||!near(bone.default_rotation_radians.x,-1.564202F)||
            !near(bone.default_rotation_radians.y,-3.113941F)||!near(bone.default_rotation_radians.z,-1.474388F) :
            buckshot ? !near(bone.default_translation.x,12.295609F)||!near(bone.default_translation.y,1.386845F)||
            !near(bone.default_translation.z,1.667124F)||!near(bone.default_rotation_radians.x,1.598449F)||
            !near(bone.default_rotation_radians.y,-0.010846F)||!near(bone.default_rotation_radians.z,0.096108F) :
            mp5 ? !near(bone.default_translation.x,10.5165F)||!near(bone.default_translation.y,0.478351F)||
            !near(bone.default_translation.z,1.89628F)||!near(bone.default_rotation_radians.x,1.54306F)||
            !near(bone.default_rotation_radians.y,0.080641F)||!near(bone.default_rotation_radians.z,-3.04742F) :
            !near(bone.default_translation.x,5.7819F)||!near(bone.default_translation.y,0.735678F)||
            !near(bone.default_translation.z,1.5869F)||!near(bone.default_rotation_radians.x,0.027741F)||
            !near(bone.default_rotation_radians.y,-0.080641F)||!near(bone.default_rotation_radians.z,0.094173F)) return false;
        V lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};std::size_t vertices=0;
        for(const auto& sub:model.submodels) for(const auto& vertex:sub.vertices) {
            if(vertex.position_bone_index!=11U || !finite(vertex.source_position)) return false;
            const auto p=vertex.source_position;++vertices;
            lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
            hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
        }
        if(!vertices || (python ? !near(lo.x,-1.06F)||!near(lo.y,-4.49F)||!near(lo.z,-3.67F)||
            !near(hi.x,1.05F)||!near(hi.y,1.56F)||!near(hi.z,8.06F) :
            buckshot ? !near(lo.x,-12.26F)||!near(lo.y,-4.01F)||!near(lo.z,-1.57F)||
            !near(hi.x,12.26F)||!near(hi.y,4.01F)||!near(hi.z,1.57F) :
            mp5 ? !near(lo.x,-7.61F)||!near(lo.y,-6.11F)||!near(lo.z,-0.53F)||
            !near(hi.x,12.38F)||!near(hi.y,5.2F)||!near(hi.z,1.6F) :
            !near(lo.x,-4.12F)||!near(lo.y,-0.79F)||!near(lo.z,-2.65F)||
            !near(hi.x,4.12F)||!near(hi.y,0.79F)||!near(hi.z,2.65F))) return false;
        // The actual asset has no attachments. Named explicit sockets are only
        // accepted for a validated compatible revision, never guessed by index.
        for(std::size_t i=0;i<model.attachments.size();++i) {
            const auto& a=model.attachments[i];
            if(a.bone_index!=11U || !finite(a.local_origin)) continue;
            if(a.name=="muzzle") {if(plan.muzzle_attachment) return false;plan.muzzle_attachment=static_cast<std::uint32_t>(i);}
            if(a.name=="shell") {if(plan.shell_attachment) return false;plan.shell_attachment=static_cast<std::uint32_t>(i);}
        }
        plan.muzzle={11U,{4.20F,0.0F,1.865F}};
        plan.shell={11U,{1.0F,-0.85F,1.8F}};
        if(mp5) {
            // Inspected Line02 has a different orientation, not Glock offsets.
            plan.muzzle={11U,{-7.70F,4.65F,0.55F},{-1,0,0},{0,0,-1},{0,1,0}};
            plan.shell={11U,{0.0F,4.25F,-0.60F},{-1,0,0},{0,0,-1},{0,1,0}};
        }
        if(buckshot) {
            // Read-only p_shotgun evidence: shothole.bmp has its four muzzle
            // vertices at +X=12.26, centered at Y=2.77,Z=0 on Box01. The shell
            // region is a model-specific local-compatible receiver profile,
            // not a stock attachment or the Glock/MP5 orientation.
            plan.muzzle={11U,{12.35F,2.77F,0.0F},{1,0,0},{0,0,-1},{0,1,0}};
            plan.shell={11U,{2.0F,2.77F,-1.65F},{1,0,0},{0,0,-1},{0,1,0}};
        }
        if(python) {
            // Inspected Cylinder01 barrel-end ring: Z=8.06, X centered at 0,
            // Y=0.03. Local-compatible muzzle just beyond it, not MP5 offsets.
            plan.muzzle={11U,{0.0F,0.03F,8.10F},{0,0,1},{1,0,0},{0,-1,0}};
            plan.shell=plan.muzzle; // neutral pose contract only; never emits brass
        }
        plan.source=plan.muzzle_attachment && plan.shell_attachment ?
            RemoteSocketSource::explicit_model_attachment : RemoteSocketSource::validated_pmodel_socket;
        return true;
    };
    if(!validate()) {plan.source=RemoteSocketSource::unavailable;plan.muzzle_attachment.reset();plan.shell_attachment.reset();}
    socket_profiles_[next_socket_profile_++%socket_profiles_.size()]={true,context.resource_id,context.revision,plan};
    return plan;
}
void RemoteWeaponEffects::poses(std::span<const RemoteWeaponPoseSample> samples) noexcept {
    pose_count_=std::min(samples.size(),poses_.size());
    std::copy_n(samples.begin(),pose_count_,poses_.begin());
}
void RemoteWeaponEffects::configure(std::span<const ScriptedEventBinding> bindings) noexcept {
    binding_count_=0;
    for(const auto& binding:bindings) {
        const auto token=name(binding);
        if(!glock(token) && !shotgun(token) && token!="events/firehornet.sc" && token!="events/rpg.sc" && token!="events/python.sc" && token!="events/crossbow1.sc" && token!="events/crossbow2.sc" && token!="events/crowbar.sc" && token!="events/mp5.sc" && token!="events/mp52.sc") continue;
        if(binding.index==0 || binding.index>1023 ||
            binding_count_==bindings_.size()) {++stats_.invalid; continue;}
        bool duplicate=false;
        for(std::size_t i=0;i<binding_count_;++i)
            if(bindings_[i].index==binding.index) duplicate=true;
        if(duplicate) {++stats_.invalid; binding_count_=0; return;}
        bindings_[binding_count_++]=binding;
    }
    // Same approved loader used by local cues. Readiness does not wait for a
    // particular local viewmodel; the listener may be holding a crowbar.
    audio_.prepare[0]=sound("weapons/pl_gun3.wav");
    audio_.prepare[1]=sound("weapons/cbar_miss1.wav");
    audio_.prepare[2]=sound("player/pl_shell1.wav");
    audio_.prepare_count=3;
    if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[](const auto& b){return name(b)=="events/rpg.sc";})) {
        audio_.prepare[audio_.prepare_count++]=sound("weapons/rocketfire1.wav");
    }
    if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[](const auto& b){return name(b)=="events/mp5.sc";})) {
        audio_.prepare[audio_.prepare_count++]=sound("weapons/hks1.wav");
        audio_.prepare[audio_.prepare_count++]=sound("weapons/hks2.wav");
    }
    if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[](const auto& b){return name(b)=="events/mp52.sc";})) {
        audio_.prepare[audio_.prepare_count++]=sound("weapons/glauncher.wav");
        audio_.prepare[audio_.prepare_count++]=sound("weapons/glauncher2.wav");
    }
    for(const auto& [event_token,sample]:std::array{
        std::pair{"events/shotgun1.sc","weapons/sbarrel1.wav"},
        std::pair{"events/shotgun2.sc","weapons/dbarrel1.wav"}}) {
        if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[&](const auto& b){return name(b)==event_token;}))
            audio_.prepare[audio_.prepare_count++]=sound(sample);
    }
    if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[](const auto& b){return shotgun(name(b));}))
        audio_.prepare[audio_.prepare_count++]=sound("weapons/sshell1.wav");
    if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[](const auto& b){return name(b)=="events/python.sc";})) {
        audio_.prepare[audio_.prepare_count++]=sound("weapons/357_shot1.wav");
        audio_.prepare[audio_.prepare_count++]=sound("weapons/357_shot2.wav");
    }
    if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[](const auto& b){return name(b)=="events/crossbow1.sc" || name(b)=="events/crossbow2.sc";}))
        for(const auto token:{"weapons/xbow_fire1.wav","weapons/xbow_reload1.wav","weapons/xbow_hit1.wav",
            "weapons/xbow_hitbod1.wav","weapons/xbow_hitbod2.wav"})
            if(audio_.prepare_count<audio_.prepare.size()) audio_.prepare[audio_.prepare_count++]=crossbow_sound(token);
    if(std::any_of(bindings_.begin(),bindings_.begin()+binding_count_,[](const auto& b){return name(b)=="events/firehornet.sc";}))
        for(const auto token:{"agrunt/ag_fire1.wav","agrunt/ag_fire2.wav","agrunt/ag_fire3.wav"})
            if(const auto sample=hornet_sound(token);sample && audio_.prepare_count<audio_.prepare.size())
                audio_.prepare[audio_.prepare_count++]=*sample;
}
void RemoteWeaponEffects::commit(const CommittedScriptedEvent& event) noexcept {
    ++stats_.received;
    if(!generation_ || event.generation!=generation_ || !event.record ||
        !std::isfinite(event.received_at_seconds) || event.received_at_seconds<0) {
        ++stats_.invalid; return;
    }
    for(const auto& seen:seen_) if(seen.record==event.record &&
        seen.cursor==event.message_bit_offset && seen.ordinal==event.entry_ordinal) {
        ++stats_.duplicates; return;
    }
    seen_[next_seen_]={event.record,event.message_bit_offset,event.entry_ordinal};
    next_seen_=(next_seen_+1U)%seen_.size();
    if(event.resolution!=ScriptedEventResolution::ready) {++stats_.unresolved; return;}
    if(!receiving_ || event.emitter_entity==0 || event.emitter_entity>32U ||
        !finite(event.origin)||!finite(event.angles)||!finite(event.velocity)||
        !std::isfinite(event.spread_x)||!std::isfinite(event.spread_y)||
        std::abs(event.spread_x)>1 || std::abs(event.spread_y)>1) {
        ++stats_.invalid; return;
    }
    if(event.emitter_entity==*receiving_) {++stats_.local_echo; return;}
    if(pending_count_==pending_.size()) {++stats_.capacity; return;}
    std::uint64_t lifetime=0;
    for(std::size_t i=0;i<pose_count_;++i) if(poses_[i].entity==event.emitter_entity &&
        poses_[i].generation==event.generation) lifetime=poses_[i].entity_lifetime;
    const auto epoch=event.emitter_entity<=visual_lives_.size() ?
        visual_lives_[event.emitter_entity-1U].epoch : 0U;
    pending_[pending_count_++]={event,lifetime,epoch};
}
void RemoteWeaponEffects::cue(LocalSoundReference sample,LocalSoundKind kind,V origin,
    double at,float volume,const LocalWeaponActionIdentity& action,std::uint32_t ordinal,float pitch) noexcept {
    if(audio_.count==audio_.cues.size()) {++stats_.capacity; return;}
    LocalSoundCue result;
    result.reference=sample; result.scope=1; result.serial=++serial_;
    result.scheduled_seconds=at; result.volume=volume; result.kind=kind;result.pitch=pitch;
    result.world_origin=origin; result.attenuation=0.8F;
    // Automatic per-event voices never replace the listener's local weapon or
    // another emitter. Mixer capacity, not an unbounded per-entity channel map.
    result.channel=LocalSoundChannel::automatic;
    result.command_sequence=action.command_sequence; result.marker_ordinal=ordinal;
    audio_.cues[audio_.count++]=result;
}
RemoteWeaponEffectsBatch RemoteWeaponEffects::drain(double now) noexcept {
    RemoteWeaponEffectsBatch result;
    if(!std::isfinite(now)||now<0) {result.statistics=stats_; return result;}
    for(std::size_t i=0;i<pending_count_;++i) {
        const auto& event=pending_[i].event;
        if(event.emitter_entity && event.emitter_entity<=visual_lives_.size()) {
            const auto& life=visual_lives_[event.emitter_entity-1U];
            if(life.observed && (!life.present || life.inactive ||
                (pending_[i].visual_epoch && pending_[i].visual_epoch!=life.epoch) ||
                (event.record_ordinal && event.record_ordinal<life.floor))) {
                ++stats_.late;continue;
            }
        }
        if(event.received_at_seconds>now+0.001 || now-event.received_at_seconds>0.25) {
            ++stats_.late; continue;
        }
        std::string_view token;
        for(std::size_t j=0;j<binding_count_;++j)
            if(bindings_[j].index==event.event_index) token=name(bindings_[j]);
        const bool mp5=token=="events/mp5.sc";
        const bool secondary=token=="events/mp52.sc";
        const bool buckshot=shotgun(token);
        const bool double_shot=token=="events/shotgun2.sc";
        const bool python=token=="events/python.sc";
        const bool crossbow=token=="events/crossbow1.sc" || token=="events/crossbow2.sc";
        const bool sniper=token=="events/crossbow2.sc";
        const bool rpg=token=="events/rpg.sc";
        const bool hornet=token=="events/firehornet.sc";
        std::optional<HornetFireMode> hornet_mode;
        if(hornet) {
            if(!event.integer_parameter1) {++stats_.invalid;continue;}
            hornet_mode=hornet_fire_mode(*event.integer_parameter1);
            if(!hornet_mode) {++stats_.unsupported;continue;}
            if(event.fire_delay_ticks) {++stats_.unresolved;continue;}
        }
        if(crossbow && (!event.integer_parameter1 || !event.integer_parameter2 ||
            *event.integer_parameter1<0 || *event.integer_parameter1>5 ||
            *event.integer_parameter2<0 || *event.integer_parameter2>50)) {++stats_.invalid;continue;}
        const bool firearm=glock(token) || mp5 || buckshot || python || crossbow || rpg;
        if(!firearm && !secondary && !hornet && token!="events/crowbar.sc") {++stats_.unsupported; continue;}
        if(action_sequence_==std::numeric_limits<std::uint32_t>::max()) {++stats_.capacity; continue;}
        LocalWeaponActionIdentity action;
        action.generation=generation_; action.weapon_id=hornet ? 11U : rpg ? 8U : crossbow ? 6U : python ? 3U : buckshot ? 7U : (mp5 || secondary) ? 4U : firearm ? 2U : 1U;
        action.resource_revision=1; action.command_sequence=++action_sequence_;
        action.kind=hornet ? (*hornet_mode==HornetFireMode::fast ? LocalWeaponAction::secondary_fire : LocalWeaponAction::primary_fire) :
            secondary ? LocalWeaponAction::secondary_launch :
            double_shot ? LocalWeaponAction::secondary_fire :
            firearm ? LocalWeaponAction::primary_fire : LocalWeaponAction::melee_swing;
        action.started_at_seconds=event.received_at_seconds;
        action.canonical_pre_revision=event.record; action.emitter_entity=event.emitter_entity;
        ++stats_.accepted;
        if(hornet) {
            // Pinned EV_HornetGunFire shares launch presentation in both modes.
            // Only the local emitter owns HGUN_SHOOT/punch; the remote player's
            // sequence and all hornet models/trajectories/trails remain server
            // observations. No firearm effects, socket guess or projectile spawn.
            if(const auto sample=hornet_sound(hornet_launch_sound(action)))
                cue(*sample,LocalSoundKind::fire,event.origin,event.received_at_seconds,1.0F,action,0U,1.0F);
            ++stats_.fire;continue;
        }
        if(secondary) {
            // mp52 is only a launch cue. The server's ordinary packet entity
            // owns the projectile; this path creates no primary-fire effects.
            cue(sound(mp5_launch_sound(action)),LocalSoundKind::secondary_launch,event.origin,
                event.received_at_seconds,1.0F,action,0U,
                static_cast<float>(94U+firearm_variant(action)%16U)/100.0F);
            ++stats_.secondary_launch; continue;
        }
        if(!firearm) {
            // Pinned EV_Crowbar emits swing here. Hit sounds remain server
            // svc_sound; duplicating that trace would fabricate/duplicate hits.
            cue(sound("weapons/cbar_miss1.wav"),LocalSoundKind::swing,event.origin,
                event.received_at_seconds,1.0F,action);
            ++stats_.swing; continue;
        }
        ++stats_.fire;
        if(rpg) {
            // Match the local user-selected launch mix; the rocket motor is svc_sound.
            cue(sound("weapons/rocketfire1.wav"),LocalSoundKind::fire,event.origin,event.received_at_seconds,.9F,action,0,1);
            continue;
        }
        constexpr float rad=3.14159265358979323846F/180.0F;
        // The pinned HL player Studio profile stores compressed model pitch.
        // Restore view pitch only for that inherited player angle source;
        // explicit event aiming angles are already uncompressed.
        const float pitch_degrees=event.angles_from_entity ?
            -3.0F*std::remainder(event.angles.x,360.0F) : event.angles.x;
        const float pitch=pitch_degrees*rad,yaw=event.angles.y*rad,roll=event.angles.z*rad;
        const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
        const float cr=std::cos(roll),sr=std::sin(roll);
        const V forward{cp*cy,cp*sy,-sp};
        const V right{-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp};
        const V up{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
        const V eye=add(event.origin,{0,0,event.ducking ? 12.0F : 28.0F});
        if(crossbow) {
            cue(crossbow_sound("weapons/xbow_fire1.wav"),LocalSoundKind::fire,event.origin,
                event.received_at_seconds,1,action,0,crossbow_pitch(action));
            cue(crossbow_sound("weapons/xbow_reload1.wav"),LocalSoundKind::fire,event.origin,
                event.received_at_seconds,crossbow_mechanical_volume(action),action,1,crossbow_pitch(action));
            if(sniper) {
                RemoteWeaponEffect effect;effect.action=action;
                effect.impact=LocalWorldImpactRequest{action,eye,forward,8192,4,event.received_at_seconds+.25,0};
                effect.impact->suppress_decal=true;
                actions_[next_action_]=Action{action,1,0,false,0,{},forward};
                next_action_=(next_action_+1U)%actions_.size();
                result.effects[result.count++]=effect;
            }
            continue; // no local flying bolt, flash, light, brass, decal or explosion timer
        }
        const V muzzle=add(add(eye,mul(forward,buckshot ? 32.0F : 20.0F)),mul(up,-4));
        RemoteWeaponEffect effect;
        effect.action=action; effect.muzzle_origin=muzzle;
        // World muzzle/light uses a documented local-compatible attachment
        // offset; no camera-local viewmodel or local player's recoil is touched.
        effect.flash=RemoteMuzzleFlash{muzzle,3,{1.0F,0.72F,0.28F,0.85F},
            event.received_at_seconds,event.received_at_seconds+0.075};
        effect.light=LocalMuzzleLight{action,python ? 128.0F : 96.0F,python ? 1.0F : 0.8F,{1.0F,0.62F,0.28F},
            event.received_at_seconds,event.received_at_seconds+0.075};
        const auto variant=action.command_sequence ^ event.emitter_entity ^
            static_cast<std::uint32_t>(event.record);
        effect.flash->sprite=muzzle_sprite_profile(buckshot ? double_shot ? 41U : 31U :
            python ? 31U : mp5 ? 20U : 11U,variant);
        LocalShellEjection shell;
        shell.action=action;
        const auto shell_name=buckshot ? std::string_view{"models/shotgunshell.mdl"} : std::string_view{"models/shell.mdl"};
        std::copy(shell_name.begin(),shell_name.end(),shell.model_name.begin());
        shell.origin=add(add(add(eye,mul(forward,buckshot ? 32.0F : 20.0F)),mul(up,-12)),mul(right,buckshot ? 6.0F : 4.0F));
        shell.velocity=add(add(add(event.velocity,mul(forward,25)),
            mul(right,50+static_cast<float>(variant%21))),mul(up,100+static_cast<float>((variant>>8)%51)));
        shell.yaw_degrees=event.angles.y;
        shell.starts_at_seconds=event.received_at_seconds;
        shell.expires_at_seconds=event.received_at_seconds+2.5;
        effect.shell=shell;
        auto shell_right=right,shell_up=up;
        // Freeze sockets once; trace direction/eye, audio, action identity and
        // shell lifetime are deliberately independent and remain unchanged.
        effect.pose.event_age=now-event.received_at_seconds;
        const RemoteWeaponPoseSample* selected=nullptr;
        for(std::size_t at=0;at<pose_count_;++at) if(poses_[at].entity==event.emitter_entity) {
            if(selected) {selected=nullptr;break;} // ambiguous numeric input fails closed
            selected=&poses_[at];
        }
        if(selected) {
            const auto& p=*selected;auto& d=effect.pose;
            d.model_slot=p.model_slot;d.lifetime=p.entity_lifetime;d.resource_id=p.resource_id;
            d.frame_identity=p.frame_identity;d.muzzle_bone=p.muzzle_bone;d.shell_bone=p.shell_bone;
            d.server_sequence=p.server_sequence;d.effects=p.effects;d.pose_age=now-p.observed_at_seconds;
            if(p.generation!=event.generation || p.map_generation!=map_generation_)
                d.status=RemotePoseResolution::generation_mismatch;
            else if(pending_[i].lifetime && p.entity_lifetime!=pending_[i].lifetime)
                d.status=RemotePoseResolution::lifetime_mismatch;
            else if(!p.current_model_slot || *p.current_model_slot!=p.model_slot ||
                (event.weapon_model_index && *event.weapon_model_index!=p.model_slot) ||
                (p.semantic_known && (!p.semantic_match ||
                    (p.semantic_key && p.semantic_key!=action.weapon_id)))) d.status=RemotePoseResolution::weapon_mismatch;
            else if(!std::isfinite(d.pose_age) || d.pose_age< -0.001 || d.pose_age>0.25 ||
                !std::isfinite(p.presented_at_seconds) || p.presented_at_seconds>now+0.001 ||
                (event.record_ordinal ? p.source_ordinal>event.record_ordinal : p.source_record!=event.record))
                d.status=RemotePoseResolution::stale;
            else if(p.source==RemoteSocketSource::unavailable) d.status=RemotePoseResolution::missing;
            else if(!p.entity_lifetime || !p.source_record || !p.frame_identity || !p.resource_id ||
                !p.semantic_known || !p.semantic_match ||
                (p.source!=RemoteSocketSource::explicit_model_attachment &&
                 p.source!=RemoteSocketSource::validated_pmodel_socket) ||
                !socket_basis(p.muzzle)||!socket_basis(p.shell)) d.status=RemotePoseResolution::invalid_socket;
            else {
                d.status=RemotePoseResolution::resolved;d.source=p.source;
                effect.muzzle_origin=p.muzzle.origin;effect.flash->origin=p.muzzle.origin;
                if(effect.shell) effect.shell->origin=p.shell.origin;
                if(effect.shell) effect.shell->velocity=add(add(add(event.velocity,mul(p.shell.forward,25)),
                    mul(p.shell.right,50+static_cast<float>(variant%21))),mul(p.shell.up,100+static_cast<float>((variant>>8)%51)));
                if(effect.shell) effect.shell->yaw_degrees=std::atan2(p.shell.forward.y,p.shell.forward.x)/rad;
                shell_right=p.shell.right;shell_up=p.shell.up;
            }
            if(d.status==RemotePoseResolution::weapon_mismatch || d.status==RemotePoseResolution::generation_mismatch ||
                d.status==RemotePoseResolution::lifetime_mismatch) {
                effect.flash.reset();effect.light.reset();effect.shell.reset();d.source=RemoteSocketSource::unavailable;
            }
        }
        if(python) effect.shell.reset();
        auto direction=add(add(forward,mul(right,event.spread_x)),mul(up,event.spread_y));
        const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
        if(std::isfinite(length)&&length>0) direction=mul(direction,1.0F/length);
        effect.impact=LocalWorldImpactRequest{action,eye,direction,8192,4,
            event.received_at_seconds+0.25,0.0,bullet_impact_particles()};
        const auto pellet_count=static_cast<std::uint8_t>(buckshot ? double_shot ? 8U : 4U : 1U);
        if(buckshot) {
            // The event resource, not the receiving client's buttons or its
            // camera, chooses single/double. One frozen basis feeds all owned
            // traces; each pellet has a deterministic disk-distributed sample.
            for(std::uint32_t pellet=0;pellet<pellet_count;++pellet) {
                auto request=*effect.impact;
                request.action.sub_identity=pellet+1U;
                request.direction=shotgun_pellet_direction(action,pellet,forward,right,up,double_shot);
                request.maximum_distance_units=2048.0F;
                if(pellet==0) effect.impact=request;
                else effect.extra_impacts[effect.extra_impact_count++]=request;
            }
            if(double_shot && effect.shell) {
                auto second=*effect.shell;
                second.action.sub_identity=1U;
                // Same validated ejection region, separate deterministic
                // flight/lifetime identity. A duplicate event emits neither.
                second.velocity=add(second.velocity,add(mul(shell_right,7.0F),mul(shell_up,11.0F)));
                effect.extra_shells[effect.extra_shell_count++]=second;
            }
        }
        if(mp5 && (visual_lives_[event.emitter_entity-1U].tracer_count++%2U)==0U) {
            effect.impact->tracer=LocalWorldImpactRequest::Tracer{effect.muzzle_origin};
            effect.impact->tracer->width_units=1.30F;
            effect.impact->tracer->start_offset_units=mp5_tracer_start_offset(action);
            effect.impact->tracer->lifetime_seconds=0.1014;
            effect.impact->tracer->travel_speed_units_per_second=12000;
            effect.impact->tracer->segment_length_units=48;
            // Rounded front occupies 10%; the remaining 90% tapers rearward.
            effect.impact->tracer->head_taper_fraction=0.90F;
            effect.impact->tracer->taper_end_diameter_fraction=0.20F;
            effect.impact->tracer->head_glow={1.25F,48.0F,0.35F};
            effect.impact->tracer->geometry=WorldLineGeometry::solid_3d;
            effect.impact->suppress_surface_effects=true;
            effect.impact->particles.reset();
        }
        result.effects[result.count++]=effect;
        actions_[next_action_]=Action{action,pellet_count,static_cast<std::uint8_t>(python ? 0U : double_shot ? 2U : 1U),buckshot};
        next_action_=(next_action_+1U)%actions_.size();
        cue(sound(buckshot ? double_shot ? "weapons/dbarrel1.wav" : "weapons/sbarrel1.wav" :
            python ? python_sound(action) : mp5 ? mp5_sound(action) : "weapons/pl_gun3.wav"),LocalSoundKind::fire,event.origin,
            event.received_at_seconds,python ? python_volume(action) : buckshot ? shotgun_fire_volume(action) : mp5 ? 1.0F : 0.96F,action,0U,
            buckshot ? shotgun_fire_pitch(action) :
            mp5 ? static_cast<float>(94U+(variant%16U))/100.0F : 1.0F);
    }
    pending_count_=0;
    result.statistics=stats_;
    return result;
}
LocalAudioBatch RemoteWeaponEffects::audio() noexcept {
    auto result=audio_; result.scope=1;
    audio_.count=audio_.prepare_count=0;
    return result;
}
std::optional<V> RemoteWeaponEffects::impact(const LocalWeaponActionIdentity& action,V point,double now,
    std::optional<LocalWorldSurfaceHit> surface,const HalfLifeMaterials& materials) noexcept {
    if(action.generation!=generation_ || !action.emitter_entity || !finite(point) ||
        !std::isfinite(now) || now<action.started_at_seconds || now-action.started_at_seconds>0.25) return {};
    auto parent=action;parent.sub_identity=0U;
    auto found=std::find_if(actions_.begin(),actions_.end(),[&](const auto& a){return a && a->identity==parent;});
    if(found==actions_.end()) return {};
    const auto& owner=**found;
    if(owner.buckshot ? action.sub_identity==0U || action.sub_identity>owner.pellet_count : action.sub_identity!=0U) return {};
    const auto ordinal=owner.buckshot ? action.sub_identity-1U : 0U;
    const auto mask=static_cast<std::uint16_t>(1U<<ordinal);
    if(owner.impacts&mask) return {};
    (*found)->impacts|=mask; ++stats_.impacts;
    if(owner.crossbow_direction) {
        cue(crossbow_sound("weapons/xbow_hit1.wav"),LocalSoundKind::impact,point,now,1,action,2);
        return owner.crossbow_direction;
    }
    const auto texture=surface ? std::string_view{surface->texture_name.data(),
        static_cast<std::size_t>(std::find(surface->texture_name.begin(),surface->texture_name.end(),'\0')-
            surface->texture_name.begin())} : std::string_view{};
    if(!owner.buckshot) {
        const auto chosen=material_impact_sound(materials.lookup(texture).kind,action,false);
        cue(chosen.reference,LocalSoundKind::impact,point,now,chosen.volume,action);
    }
    if(const auto ricochet=bullet_ricochet_sound(action))
        cue(*ricochet,LocalSoundKind::impact,point,now,1.0F,action,1);
    return {};
}
void RemoteWeaponEffects::contact(const LocalShellContact& contact) noexcept {
    if(contact.action.generation!=generation_ || !contact.action.emitter_entity ||
        contact.ordinal==0 || contact.ordinal>3 || !finite(contact.point) ||
        !std::isfinite(contact.inward_normal_speed) ||
        contact.inward_normal_speed<(contact.ordinal==1 ? 25.0F : 40.0F) ||
        !std::isfinite(contact.at_seconds) || contact.at_seconds<contact.action.started_at_seconds ||
        contact.at_seconds-contact.action.started_at_seconds>2.5) return;
    auto parent=contact.action;parent.sub_identity=0U;
    auto found=std::find_if(actions_.begin(),actions_.end(),[&](const auto& a){
        return a && a->identity==parent;
    });
    const auto mask=static_cast<std::uint8_t>(1U<<contact.ordinal);
    if(found==actions_.end() || contact.action.sub_identity>1U ||
        contact.action.sub_identity>=(*found)->shell_count || ((*found)->contacts[contact.action.sub_identity]&mask)) return;
    (*found)->contacts[contact.action.sub_identity]|=mask; ++stats_.shell_contacts;
    // The pinned SDK specifies a distinct shotshell bounce category but the
    // engine callback's exact sample is opaque. This inspected, approved shell
    // sample is the explicit local-compatible Shotgun contact profile.
    cue(sound((*found)->buckshot ? "weapons/sshell1.wav" : "player/pl_shell1.wav"),LocalSoundKind::shell_contact,contact.point,
        contact.at_seconds,contact.ordinal==1 ? 0.7F : contact.ordinal==2 ? 0.45F : 0.3F,
        contact.action,contact.ordinal);
}
} // namespace hlclient::games::halflife

#include <hlclient/games/halflife/remote_effects.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::games::halflife {
namespace {
using V=assets::AssetVector3;
using namespace game_api;
bool finite(V v) noexcept { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
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
}
void RemoteWeaponEffects::reset(std::uint64_t generation,std::optional<std::uint32_t> receiving) noexcept {
    generation_=generation; receiving_=receiving; binding_count_=pending_count_=0;
    serial_=action_sequence_=0; seen_={}; next_seen_=next_action_=0;
    actions_.fill(std::nullopt); audio_={}; stats_={};
}
void RemoteWeaponEffects::configure(std::span<const ScriptedEventBinding> bindings) noexcept {
    binding_count_=0;
    for(const auto& binding:bindings) {
        const auto token=name(binding);
        if(!glock(token) && token!="events/crowbar.sc") continue;
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
    if(!receiving_ || event.emitter_entity==0 || event.emitter_entity>255U ||
        !finite(event.origin)||!finite(event.angles)||!finite(event.velocity)||
        !std::isfinite(event.spread_x)||!std::isfinite(event.spread_y)||
        std::abs(event.spread_x)>1 || std::abs(event.spread_y)>1) {
        ++stats_.invalid; return;
    }
    if(event.emitter_entity==*receiving_) {++stats_.local_echo; return;}
    if(pending_count_==pending_.size()) {++stats_.capacity; return;}
    pending_[pending_count_++]=event;
}
void RemoteWeaponEffects::cue(LocalSoundReference sample,LocalSoundKind kind,V origin,
    double at,float volume,const LocalWeaponActionIdentity& action,std::uint32_t ordinal) noexcept {
    if(audio_.count==audio_.cues.size()) {++stats_.capacity; return;}
    LocalSoundCue result;
    result.reference=sample; result.scope=1; result.serial=++serial_;
    result.scheduled_seconds=at; result.volume=volume; result.kind=kind;
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
        const auto& event=pending_[i];
        if(event.received_at_seconds>now+0.001 || now-event.received_at_seconds>0.25) {
            ++stats_.late; continue;
        }
        std::string_view token;
        for(std::size_t j=0;j<binding_count_;++j)
            if(bindings_[j].index==event.event_index) token=name(bindings_[j]);
        const bool firearm=glock(token);
        if(!firearm && token!="events/crowbar.sc") {++stats_.unsupported; continue;}
        if(action_sequence_==std::numeric_limits<std::uint32_t>::max()) {++stats_.capacity; continue;}
        LocalWeaponActionIdentity action;
        action.generation=generation_; action.weapon_id=firearm ? 2U : 1U;
        action.resource_revision=1; action.command_sequence=++action_sequence_;
        action.kind=firearm ? LocalWeaponAction::primary_fire : LocalWeaponAction::melee_swing;
        action.started_at_seconds=event.received_at_seconds;
        action.canonical_pre_revision=event.record; action.emitter_entity=event.emitter_entity;
        ++stats_.accepted;
        if(!firearm) {
            // Pinned EV_Crowbar emits swing here. Hit sounds remain server
            // svc_sound; duplicating that trace would fabricate/duplicate hits.
            cue(sound("weapons/cbar_miss1.wav"),LocalSoundKind::swing,event.origin,
                event.received_at_seconds,1.0F,action);
            ++stats_.swing; continue;
        }
        ++stats_.fire;
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
        const V muzzle=add(add(eye,mul(forward,20)),mul(up,-4));
        RemoteWeaponEffect effect;
        effect.action=action; effect.muzzle_origin=muzzle;
        // World muzzle/light uses a documented local-compatible attachment
        // offset; no camera-local viewmodel or local player's recoil is touched.
        effect.flash=RemoteMuzzleFlash{muzzle,3,{1.0F,0.72F,0.28F,0.85F},
            event.received_at_seconds,event.received_at_seconds+0.075};
        effect.light=LocalMuzzleLight{action,96,0.8F,{1.0F,0.62F,0.28F},
            event.received_at_seconds,event.received_at_seconds+0.075};
        const auto variant=action.command_sequence ^ event.emitter_entity ^
            static_cast<std::uint32_t>(event.record);
        LocalShellEjection shell;
        shell.action=action;
        constexpr char shell_name[]="models/shell.mdl";
        std::copy_n(shell_name,sizeof(shell_name),shell.model_name.begin());
        shell.origin=add(add(add(eye,mul(forward,20)),mul(up,-12)),mul(right,4));
        shell.velocity=add(add(add(event.velocity,mul(forward,25)),
            mul(right,50+static_cast<float>(variant%21))),mul(up,100+static_cast<float>((variant>>8)%51)));
        shell.yaw_degrees=event.angles.y;
        shell.starts_at_seconds=event.received_at_seconds;
        shell.expires_at_seconds=event.received_at_seconds+2.5;
        effect.shell=shell;
        auto direction=add(add(forward,mul(right,event.spread_x)),mul(up,event.spread_y));
        const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
        if(std::isfinite(length)&&length>0) direction=mul(direction,1.0F/length);
        effect.impact=LocalWorldImpactRequest{action,eye,direction,8192,4,
            event.received_at_seconds+0.25};
        result.effects[result.count++]=effect;
        actions_[next_action_]=Action{action}; next_action_=(next_action_+1U)%actions_.size();
        cue(sound("weapons/pl_gun3.wav"),LocalSoundKind::fire,event.origin,
            event.received_at_seconds,0.96F,action);
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
void RemoteWeaponEffects::impact(const LocalWeaponActionIdentity& action,V point,double now,
    std::optional<LocalWorldSurfaceHit> surface,const HalfLifeMaterials& materials) noexcept {
    if(action.generation!=generation_ || !action.emitter_entity || !finite(point) ||
        !std::isfinite(now) || now<action.started_at_seconds || now-action.started_at_seconds>0.25) return;
    auto found=std::find_if(actions_.begin(),actions_.end(),[&](const auto& a){return a && a->identity==action;});
    if(found==actions_.end() || (*found)->impact) return;
    (*found)->impact=true; ++stats_.impacts;
    const auto texture=surface ? std::string_view{surface->texture_name.data(),
        static_cast<std::size_t>(std::find(surface->texture_name.begin(),surface->texture_name.end(),'\0')-
            surface->texture_name.begin())} : std::string_view{};
    const auto chosen=material_impact_sound(materials.lookup(texture).kind,action,false);
    cue(chosen.reference,LocalSoundKind::impact,point,now,chosen.volume,action);
    if(const auto ricochet=glock_ricochet_sound(action))
        cue(*ricochet,LocalSoundKind::impact,point,now,1.0F,action,1);
}
void RemoteWeaponEffects::contact(const LocalShellContact& contact) noexcept {
    if(contact.action.generation!=generation_ || !contact.action.emitter_entity ||
        contact.ordinal==0 || contact.ordinal>3 || !finite(contact.point) ||
        !std::isfinite(contact.inward_normal_speed) ||
        contact.inward_normal_speed<(contact.ordinal==1 ? 25.0F : 40.0F) ||
        !std::isfinite(contact.at_seconds) || contact.at_seconds<contact.action.started_at_seconds ||
        contact.at_seconds-contact.action.started_at_seconds>2.5) return;
    auto found=std::find_if(actions_.begin(),actions_.end(),[&](const auto& a){
        return a && a->identity==contact.action;
    });
    const auto mask=static_cast<std::uint8_t>(1U<<contact.ordinal);
    if(found==actions_.end() || ((*found)->contacts&mask)) return;
    (*found)->contacts|=mask; ++stats_.shell_contacts;
    cue(sound("player/pl_shell1.wav"),LocalSoundKind::shell_contact,contact.point,
        contact.at_seconds,contact.ordinal==1 ? 0.7F : contact.ordinal==2 ? 0.45F : 0.3F,
        contact.action,contact.ordinal);
}
} // namespace hlclient::games::halflife

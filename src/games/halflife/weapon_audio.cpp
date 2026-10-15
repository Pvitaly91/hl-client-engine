#include <hlclient/games/halflife/weapon_audio.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/games/halflife/crossbow_profile.hpp>
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <hlclient/assets/animation_markers.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::games::halflife {
using namespace game_api;
namespace {
LocalSoundReference reference(std::string_view name,SoundReferenceSource source) noexcept {
    LocalSoundReference r; r.source=source;
    if(name.size()<r.sample.size()) std::copy(name.begin(),name.end(),r.sample.begin());
    return r;
}
std::optional<LocalSoundReference> marker(const assets::ModelSequenceEvent& e) noexcept {
    if(e.event_number!=5004 || e.options.empty() || e.options.size()>63) return {};
    LocalSoundReference r;
    for(std::size_t i=0;i<e.options.size();++i) {
        const auto c=static_cast<char>(e.options[i]);
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='/'||c=='.')) return {};
        r.sample[i]=c;
    }
    const auto name=r.name();
    if((!name.starts_with("weapons/") && !name.starts_with("items/")) || !name.ends_with(".wav") || name.find("..")!=name.npos ||
       name.find("//")!=name.npos) return {};
    return r;
}
bool supported(const LocalWeaponModelMetadata& m) noexcept {
    return m.resource_name=="models/v_9mmhandgun.mdl" || m.resource_name=="models/v_crowbar.mdl" || mp5_sequences(m) || shotgun_sequences(m) || python_sequences(m) || crossbow_sequences(m) || rpg_sequences(m) || hornet_sequences(m);
}
bool audible_sequence(const LocalWeaponModelMetadata& m,std::uint32_t seq) noexcept {
    if(hornet_sequences(m)) return false; // launch event only; no reload or invented model sounds.
    if(firearm_model(m.resource_name)==Firearm::rpg) return false;
    if(firearm_model(m.resource_name)==Firearm::crossbow) return false; // actual MDL has no audio markers; reload is server svc_sound.
    // Insert and both pump sounds are server svc_sound, not MDL audio markers.
    if(firearm_model(m.resource_name)==Firearm::shotgun) return seq==6;
    if(firearm_model(m.resource_name)==Firearm::python) return seq==3 || seq==5;
    return firearm_model(m.resource_name)==Firearm::mp5 ? seq==3 || seq==4 :
        m.resource_name=="models/v_9mmhandgun.mdl" ? seq==5 || seq==6 || seq==7 : seq==1;
}
}
void WeaponAudio::cancel() noexcept {
    ++batch_.scope; batch_.statistics.cancelled+=batch_.count; batch_.count=0;
    cancelled_restart_=restart_; armed_=false;
}
void WeaponAudio::bind(const std::optional<LocalWeaponModelMetadata>& m) {
    cancel(); restart_=cancelled_restart_=action_command_=0; previous_=-1; armed_=true;
    consumed_count_=0;
    batch_.prepare_count=0;
    if(!m || !supported(*m)) return;
    if(hornet_sequences(*m)) {
        for(const auto token:{"agrunt/ag_fire1.wav","agrunt/ag_fire2.wav","agrunt/ag_fire3.wav"})
            batch_.prepare[batch_.prepare_count++]=*hornet_sound(token);
    } else if(rpg_sequences(*m)) {
        batch_.prepare[batch_.prepare_count++]=reference("weapons/rocketfire1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
    } else if(crossbow_sequences(*m)) {
        for(const auto token:{"weapons/xbow_fire1.wav","weapons/xbow_reload1.wav",
            "weapons/xbow_hit1.wav","weapons/xbow_hitbod1.wav","weapons/xbow_hitbod2.wav"})
            batch_.prepare[batch_.prepare_count++]=crossbow_sound(token);
    } else if(python_sequences(*m)) {
        for(const auto token:{"weapons/357_shot1.wav","weapons/357_shot2.wav",
            "weapons/357_reload1.wav","weapons/357_cock1.wav"})
            batch_.prepare[batch_.prepare_count++]=reference(token,SoundReferenceSource::pinned_halflife_client_sound_profile);
    } else if(firearm_model(m->resource_name)==Firearm::shotgun) {
        batch_.prepare[batch_.prepare_count++]=reference("weapons/sbarrel1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
        batch_.prepare[batch_.prepare_count++]=reference("weapons/dbarrel1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
        batch_.prepare[batch_.prepare_count++]=reference("weapons/sshell1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
    } else if(firearm_model(m->resource_name)==Firearm::mp5) {
        batch_.prepare[batch_.prepare_count++]=reference("weapons/hks1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
        batch_.prepare[batch_.prepare_count++]=reference("weapons/hks2.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
        batch_.prepare[batch_.prepare_count++]=reference("weapons/glauncher.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
        batch_.prepare[batch_.prepare_count++]=reference("weapons/glauncher2.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
    } else batch_.prepare[batch_.prepare_count++]=reference(m->resource_name=="models/v_9mmhandgun.mdl" ?
        "weapons/pl_gun3.wav" : "weapons/cbar_miss1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile);
    for(std::size_t seq=0;seq<m->sequences.size();++seq) if(audible_sequence(*m,static_cast<std::uint32_t>(seq)))
        for(const auto& e:m->sequences[seq].events) if(auto r=marker(e)) {
            if(std::find(batch_.prepare.begin(),batch_.prepare.begin()+batch_.prepare_count,*r)!=batch_.prepare.begin()+batch_.prepare_count) continue;
            if(batch_.prepare_count<batch_.prepare.size()) batch_.prepare[batch_.prepare_count++]=*r;
        }
}
void WeaponAudio::emit(LocalSoundReference r,LocalSoundKind kind,double time,float volume,float pitch) noexcept {
    if(batch_.count==batch_.cues.size()) {++batch_.statistics.late; return;}
    batch_.cues[batch_.count++]={r,batch_.scope,++serial_,time,volume,pitch,kind};
    switch(kind) {case LocalSoundKind::fire:++batch_.statistics.fire;break;
    case LocalSoundKind::reload:++batch_.statistics.reload;break;
    case LocalSoundKind::deploy:++batch_.statistics.deploy;break;
    case LocalSoundKind::swing:++batch_.statistics.swing;break;
    case LocalSoundKind::secondary_launch:++batch_.statistics.secondary_launch;break;
    default:++batch_.statistics.invalid;break;}
}
void WeaponAudio::action(const LocalWeaponActionIdentity& a) noexcept {
    ++batch_.statistics.actions; armed_=true;
    const auto before=batch_.count;
    // Deterministic per-action variation in the SDK's range, not stock RNG parity.
    const auto variant=(a.command_sequence*2654435761U)^static_cast<std::uint32_t>(a.generation);
    if(a.weapon_id==11U && (a.kind==LocalWeaponAction::primary_fire || a.kind==LocalWeaponAction::secondary_fire)) {
        emit(*hornet_sound(hornet_launch_sound(a)),
            LocalSoundKind::fire,a.started_at_seconds,1.0F,1.0F);
        if(batch_.count>before) {
            batch_.cues[batch_.count-1].command_sequence=a.command_sequence;
            batch_.cues[batch_.count-1].channel=LocalSoundChannel::weapon;
        }
        return;
    }
    if(a.weapon_id==8U && a.kind==LocalWeaponAction::primary_fire) {
        // User-selected mix: retain the original RPG launch sample without
        // EV_FireRpg's shared MP5-grenade CHAN_ITEM overlay. Motor stays server-owned.
        emit(reference("weapons/rocketfire1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile),LocalSoundKind::fire,a.started_at_seconds,.9F,1);
        for(std::size_t i=before;i<batch_.count;++i) {
            batch_.cues[i].command_sequence=a.command_sequence;
            batch_.cues[i].channel=LocalSoundChannel::automatic;
            batch_.cues[i].marker_ordinal=static_cast<std::uint32_t>(i-before);
        }
        return;
    }
    if(a.weapon_id==6U && a.kind==LocalWeaponAction::primary_fire) {
        emit(crossbow_sound("weapons/xbow_fire1.wav"),LocalSoundKind::fire,a.started_at_seconds,1.0F,crossbow_pitch(a));
        emit(crossbow_sound("weapons/xbow_reload1.wav"),LocalSoundKind::fire,a.started_at_seconds,crossbow_mechanical_volume(a),crossbow_pitch(a));
        for(std::size_t i=before;i<batch_.count;++i) {
            batch_.cues[i].command_sequence=a.command_sequence;
            batch_.cues[i].channel=LocalSoundChannel::automatic;
            batch_.cues[i].marker_ordinal=static_cast<std::uint32_t>(i-before);
        }
        return;
    }
    if(a.weapon_id==3U && a.kind==LocalWeaponAction::primary_fire)
        emit(reference(python_sound(a),SoundReferenceSource::pinned_halflife_client_sound_profile),
            LocalSoundKind::fire,a.started_at_seconds,python_volume(a),1.0F);
    else if(a.weapon_id==7U && (a.kind==LocalWeaponAction::primary_fire || a.kind==LocalWeaponAction::secondary_fire))
        emit(reference(a.kind==LocalWeaponAction::secondary_fire ? "weapons/dbarrel1.wav" : "weapons/sbarrel1.wav",
            SoundReferenceSource::pinned_halflife_client_sound_profile),LocalSoundKind::fire,
            a.started_at_seconds,shotgun_fire_volume(a),shotgun_fire_pitch(a));
    else if(a.kind==LocalWeaponAction::primary_fire)
        emit(reference(a.weapon_id==4U ? mp5_sound(a) : "weapons/pl_gun3.wav",SoundReferenceSource::pinned_halflife_client_sound_profile),
            LocalSoundKind::fire,a.started_at_seconds,a.weapon_id==4U ? 1.0F : 0.92F+static_cast<float>(variant%81)/1000.0F,
            static_cast<float>(a.weapon_id==4U ? 94+firearm_variant(a)%16U : 98+variant%4)/100.0F);
    else if(a.kind==LocalWeaponAction::secondary_launch && a.weapon_id==4U)
        emit(reference(mp5_launch_sound(a),SoundReferenceSource::pinned_halflife_client_sound_profile),
            LocalSoundKind::secondary_launch,a.started_at_seconds,1.0F,
            static_cast<float>(94U+firearm_variant(a)%16U)/100.0F);
    else if(a.kind==LocalWeaponAction::melee_swing)
        emit(reference("weapons/cbar_miss1.wav",SoundReferenceSource::pinned_halflife_client_sound_profile),
            LocalSoundKind::swing,a.started_at_seconds);
    if(batch_.count>before) {
        batch_.cues[batch_.count-1].command_sequence=a.command_sequence;
        batch_.cues[batch_.count-1].channel=a.weapon_id==4U ? LocalSoundChannel::automatic : LocalSoundChannel::weapon;
    }
}
void WeaponAudio::update(const LocalWeaponPresentationSnapshot& s,
    const std::optional<LocalWeaponModelMetadata>& m,double now,bool enabled) noexcept {
    if(!std::isfinite(now)||now<last_clock_) return;
    last_clock_=now;
    if(!enabled) {if(armed_) cancel(); return;}
    if(!m || !supported(*m) || !s.visual || s.visual->sequence>=m->sequences.size()) return;
    const auto& v=*s.visual;
    if(!armed_ && v.restart_identity==cancelled_restart_) return;
    const auto command=s.identity ? s.identity->command_sequence : 0;
    const auto elapsed=std::max(0.0,now-v.started_at_seconds);
    if(v.restart_identity!=restart_) {
        const bool correction=command && command==action_command_;
        const bool crowbar_hit=correction && s.action &&
            *s.action==LocalWeaponAction::melee_swing &&
            ((sequence_==4U && v.sequence==3U) ||
             (sequence_==5U && v.sequence==6U) ||
             (sequence_==7U && v.sequence==8U));
        // The local line trace refines this accepted swing's pose, not its
        // already emitted swing voice. Server corrections retain old policy.
        if(correction && !crowbar_hit) {++batch_.scope; batch_.count=0; ++batch_.statistics.timeline_corrections;}
        else consumed_count_=0;
        restart_=v.restart_identity; sequence_=v.sequence; start_=v.started_at_seconds;
        previous_=correction ? std::max(previous_,elapsed) : -1.0; action_command_=command; armed_=true;
    }
    if(!audible_sequence(*m,sequence_)) {previous_=elapsed; return;}
    const auto& seq=m->sequences[sequence_];
    const auto crossed=assets::animation_markers(seq.events,seq.fps,seq.frame_count,seq.looping,previous_,elapsed,5004);
    previous_=std::max(previous_,elapsed); batch_.statistics.late+=crossed.late+crossed.overflow;
    for(std::size_t i=0;i<crossed.count;++i) {
        const auto& occurrence=crossed.values[i]; const auto& e=seq.events[occurrence.ordinal];
        if(e.event_number!=5004) continue;
        // Supported reload variants share marker occurrence ownership within
        // one B1 action. A correction may change sequence/timing, but cannot
        // replay an ordinal/loop that this same action already consumed.
        if(std::any_of(consumed_.begin(),consumed_.begin()+consumed_count_,[&](const auto& seen){
            return seen.ordinal==occurrence.ordinal && seen.loop==occurrence.loop;
        })) {++batch_.statistics.duplicates; continue;}
        if(consumed_count_==consumed_.size()) {++batch_.statistics.late; continue;}
        consumed_[consumed_count_++]={occurrence.ordinal,occurrence.loop};
        ++batch_.statistics.markers;
        if(auto r=marker(e)) {
            const auto before=batch_.count;
            emit(*r,(firearm_model(m->resource_name)==Firearm::python ? sequence_==3 : firearm_model(m->resource_name)==Firearm::mp5 ? sequence_==3 : sequence_==5 || sequence_==6)
                ? LocalSoundKind::reload : LocalSoundKind::deploy,start_+occurrence.seconds);
            if(batch_.count>before) {
                auto& cue=batch_.cues[batch_.count-1]; cue.command_sequence=command;
                cue.sequence=sequence_; cue.restart=restart_;
                cue.marker_ordinal=static_cast<std::uint32_t>(occurrence.ordinal); cue.loop_iteration=occurrence.loop;
            }
        }
        else ++batch_.statistics.invalid;
    }
}
LocalAudioBatch WeaponAudio::drain() noexcept { auto out=batch_; batch_.count=0; return out; }
}

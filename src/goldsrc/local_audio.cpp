#include <hlclient/goldsrc/local_audio.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace hlclient::goldsrc {
namespace {
bool movement_cue(game_api::LocalSoundKind kind) noexcept {
    return kind==game_api::LocalSoundKind::footstep ||
        kind==game_api::LocalSoundKind::ladder ||
        kind==game_api::LocalSoundKind::landing ||
        kind==game_api::LocalSoundKind::fall_pain;
}
bool impact_cue(game_api::LocalSoundKind kind) noexcept {
    return kind==game_api::LocalSoundKind::impact;
}
bool shell_cue(game_api::LocalSoundKind kind) noexcept {
    return kind==game_api::LocalSoundKind::shell_contact;
}
}
void TemporaryAudio::reset(std::uint64_t generation) noexcept {
    generation_=generation;seen_count_=0;next_seen_=0;evicted_floor_.reset();serial_=0;++session_;
    game_api::LocalAudioBatch empty;empty.session=session_;empty.scope=1;
    delivery_.update(empty,nullptr,0.0,false);
    audio::Command stop;stop.operation=audio::Operation::stop_group;
    stop.voice=(std::uint64_t{1}<<63)|(std::uint64_t{1}<<61);
    stop.voice_group_mask=std::uint64_t{7}<<61;
    (void)output_.submit(stop);
}
void TemporaryAudio::update(const game_api::TemporaryEffectsBatch& input,SoundAssets* assets,
    double now,bool audible) noexcept {
    if(!std::isfinite(now)||now<0) return;
    game_api::LocalAudioBatch batch;batch.session=session_;batch.scope=1;
    for(std::size_t i=0;i<std::min(input.count,input.effects.size());++i) {
        const auto& effect=input.effects[i];if(!effect.sound) continue;
        if(!generation_ || effect.identity.generation!=generation_) {++stats_.stale;continue;}
        const auto& sound=*effect.sound;
        if(!effect.identity.record || !effect.record_ordinal || !std::isfinite(effect.starts_at_seconds) ||
           effect.starts_at_seconds<0 || effect.starts_at_seconds>now+0.001 ||
           !std::isfinite(effect.origin.x)||!std::isfinite(effect.origin.y)||!std::isfinite(effect.origin.z)||
           !std::isfinite(sound.volume)||sound.volume<0||sound.volume>1 ||
           !std::isfinite(sound.pitch)||sound.pitch<=0||sound.pitch>4 ||
           !std::isfinite(sound.attenuation)||sound.attenuation<0||sound.attenuation>4 ||
           !valid_local_sound_reference(sound.reference)) {++stats_.invalid;continue;}
        if(std::find(seen_.begin(),seen_.begin()+seen_count_,effect.identity)!=seen_.begin()+seen_count_) {
            ++stats_.duplicate;continue;
        }
        const auto key=[](std::uint64_t ordinal,const auto& identity) {
            return std::array<std::uint64_t,3>{ordinal,identity.message_bit_offset,identity.entry_ordinal};
        };
        // Committed record ordinals, not arbitrary record IDs, are monotonic. Retain exact recent
        // identities and a stale floor, not an unlimited-session event cap.
        if(evicted_floor_ && key(effect.record_ordinal,effect.identity)<=*evicted_floor_) {++stats_.stale;continue;}
        if(serial_==std::numeric_limits<std::uint64_t>::max()) {
            ++stats_.capacity;continue;
        }
        if(seen_count_==seen_.size()) {
            const auto evicted=key(seen_ordinals_[next_seen_],seen_[next_seen_]);
            if(!evicted_floor_ || evicted>*evicted_floor_) evicted_floor_=evicted;
        } else ++seen_count_;
        seen_[next_seen_]=effect.identity;seen_ordinals_[next_seen_]=effect.record_ordinal;
        next_seen_=(next_seen_+1)%seen_.size();
        auto& cue=batch.cues[batch.count++];cue.reference=sound.reference;
        cue.scope=1;cue.serial=++serial_;cue.scheduled_seconds=effect.starts_at_seconds;
        cue.volume=sound.volume;cue.pitch=sound.pitch;
        cue.kind=game_api::LocalSoundKind::world_effect;
        cue.channel=game_api::LocalSoundChannel::automatic;
        cue.world_origin=effect.origin;cue.attenuation=sound.attenuation;
        ++stats_.accepted;
    }
    delivery_.update(batch,assets,now,audible);
}
bool valid_local_sound_reference(const game_api::LocalSoundReference& r) noexcept {
    if(r.source!=game_api::SoundReferenceSource::validated_local_model_event &&
       r.source!=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile &&
       r.source!=game_api::SoundReferenceSource::module_profile_dependency) return false;
    const auto name=r.name();
    const auto separator=name.find('/');
    const bool module_profile=r.source==game_api::SoundReferenceSource::module_profile_dependency &&
        separator!=name.npos && separator>0 && name.size()>separator+5 &&
        name.find('/',separator+1)==name.npos && !name.starts_with("sound/") &&
        name.find('.')==name.size()-4;
    if(r.source==game_api::SoundReferenceSource::module_profile_dependency && !module_profile) return false;
    const bool player_profile=r.source==game_api::SoundReferenceSource::pinned_halflife_client_sound_profile &&
        name.starts_with("player/");
    const bool debris_profile=r.source==game_api::SoundReferenceSource::pinned_halflife_client_sound_profile &&
        name.starts_with("debris/");
    const bool ambience_profile=r.source==game_api::SoundReferenceSource::pinned_halflife_client_sound_profile && name.starts_with("ambience/");
    if(name.size()>63 || (!name.starts_with("weapons/") && !name.starts_with("items/") &&
        !player_profile && !debris_profile && !ambience_profile && !module_profile) || !name.ends_with(".wav") ||
        name.find("..")!=name.npos || name.find("//")!=name.npos) return false;
    return std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z') ||
        (c>='0'&&c<='9') || c=='_' || c=='/' || c=='.';});
}
void LocalAudio::update(const game_api::LocalAudioBatch& batch,SoundAssets* assets,double now,bool audible) noexcept {
    if(!std::isfinite(now)||now<0) return;
    if(batch.session<session_ || (batch.session==session_ && batch.scope<scope_)) {
        stats_.cancelled+=std::min(batch.count,batch.cues.size()); return;
    }
    if(scope_!=batch.scope || session_!=batch.session) {
        for(auto& p:pending_) if(p &&
            (session_!=batch.session || (!impact_cue(p->kind) && !shell_cue(p->kind) && !movement_cue(p->kind)))) {
            p.reset(); ++stats_.cancelled;
        }
        scope_=batch.scope;
        if(session_!=batch.session) {
            if(session_) {
                audio::Command stop;stop.operation=audio::Operation::stop_group;stop.voice=voice_namespace_;
                stop.voice_group_mask=std::uint64_t{7}<<61;(void)output_.submit(stop);
            }
            for(auto& v:persistent_) if(v) {
                audio::Command stop;stop.operation=audio::Operation::stop;
                stop.voice=voice_namespace_|(std::uint64_t{1}<<60)|v->control.identity;
                (void)output_.submit(stop);v.reset();
            }
            scheduled_controls_.fill(std::nullopt);
            session_=batch.session;high_serial_=control_serial_=0;
        }
    }
    if(movement_epoch_!=batch.movement_epoch) {
        for(auto& p:pending_) if(p && movement_cue(p->kind)) {p.reset(); ++stats_.cancelled;}
        for(const auto channel:{2U,3U}) {
            audio::Command stop; stop.operation=audio::Operation::stop;
            stop.voice=voice_namespace_|channel; (void)output_.submit(stop);
        }
        movement_epoch_=batch.movement_epoch;
    }
    if(assets) for(std::size_t i=0;i<std::min(batch.prepare_count,batch.prepare.size());++i)
        if(valid_local_sound_reference(batch.prepare[i])) (void)assets->request_local(batch.prepare[i]);
    for(std::size_t i=0;i<std::min(batch.control_count,batch.controls.size());++i) {
        const auto& c=batch.controls[i];
        if(c.serial<=control_serial_) {++stats_.duplicates;continue;} control_serial_=c.serial;
        if(!c.identity || c.identity>0xffffffffffffULL || c.operation>game_api::LocalVoiceControl::Operation::replace ||
            !std::isfinite(c.scheduled_seconds) || c.scheduled_seconds<0 || c.scheduled_seconds>now+.101 ||
            !std::isfinite(c.volume) || c.volume<0 || c.volume>1 || !std::isfinite(c.pitch) || c.pitch<=0 || c.pitch>4 ||
            !std::isfinite(c.attenuation) || c.attenuation<0 || c.attenuation>4 ||
            (c.world_origin && (!std::isfinite(c.world_origin->x)||!std::isfinite(c.world_origin->y)||!std::isfinite(c.world_origin->z)))) {++stats_.limits;continue;}
        auto free=std::find_if(scheduled_controls_.begin(),scheduled_controls_.end(),[](const auto& v){return !v;});
        if(free==scheduled_controls_.end()) {++stats_.limits;continue;} *free=c;
    }
    std::sort(scheduled_controls_.begin(),scheduled_controls_.end(),[](const auto& a,const auto& b) {
        return a && (!b || std::pair{a->scheduled_seconds,a->serial}<std::pair{b->scheduled_seconds,b->serial});
    });
    for(auto& scheduled:scheduled_controls_) if(scheduled && scheduled->scheduled_seconds<=now) {
        const auto c=*scheduled;scheduled.reset();
        auto slot=std::find_if(persistent_.begin(),persistent_.end(),[&](const auto& v){return v && v->control.identity==c.identity;});
        audio::Command command;command.voice=voice_namespace_|(std::uint64_t{1}<<60)|c.identity;
        if(c.operation==game_api::LocalVoiceControl::Operation::stop) {
            if(slot!=persistent_.end()) {command.operation=audio::Operation::stop;(void)output_.submit(command);slot->reset();} continue;
        }
        if(c.operation==game_api::LocalVoiceControl::Operation::start || c.operation==game_api::LocalVoiceControl::Operation::replace) {
            if((!audible && !c.repeat_sample) || !valid_local_sound_reference(c.reference) || now-c.scheduled_seconds>.25) {++stats_.cancelled;continue;}
            if(slot!=persistent_.end() && c.operation==game_api::LocalVoiceControl::Operation::start) {++stats_.duplicates;continue;}
            if(slot!=persistent_.end()) {command.operation=audio::Operation::stop;(void)output_.submit(command);}
            else slot=std::find_if(persistent_.begin(),persistent_.end(),[](const auto& v){return !v;});
            if(slot==persistent_.end()) {++stats_.limits;continue;} *slot=PersistentVoice{c,false,!audible};
        } else if(slot!=persistent_.end()) {
            auto& v=**slot;
            if(c.operation==game_api::LocalVoiceControl::Operation::change) {v.control.volume=c.volume;v.control.pitch=c.pitch;}
            v.control.world_origin=c.world_origin;
            if(v.started) {
                command.operation=audio::Operation::change;command.volume=v.control.volume;command.pitch=v.control.pitch;(void)output_.submit(command);
                command.operation=audio::Operation::source;command.origin=c.world_origin.value_or(assets::AssetVector3{});
                command.local=!c.world_origin;(void)output_.submit(command);
            }
        }
    }
    for(auto& slot:persistent_) if(slot) {
        auto& v=*slot;audio::Command c;c.voice=voice_namespace_|(std::uint64_t{1}<<60)|v.control.identity;
        if(!audible) {
            if(v.started){c.operation=audio::Operation::stop;(void)output_.submit(c);v.started=false;}
            ++stats_.muted;
            if(v.control.repeat_sample){v.muted=true;continue;}
            slot.reset();continue;
        }
        // Resume current continuous intent, never a stale finite startup/cue.
        // Stop/session reset while muted still destroys this owning intent.
        if(v.muted){v.control.scheduled_seconds=now;v.muted=false;}
        if(v.started || now<v.control.scheduled_seconds) continue;
        if(now-v.control.scheduled_seconds>.25) {slot.reset();++stats_.late;continue;}
        if(!assets) continue;
        const auto resource=assets->request_local(v.control.reference);
        if(resource.status==SoundAssetStatus::pending) {++stats_.resource_pending;continue;}
        if(resource.status!=SoundAssetStatus::ready || !resource.asset) {
            if(resource.status==SoundAssetStatus::missing)++stats_.missing;
            else if(resource.status==SoundAssetStatus::not_authorized)++stats_.resource_not_authorized;
            else if(resource.status==SoundAssetStatus::open_failed)++stats_.resource_open_failed;
            else if(resource.status==SoundAssetStatus::decode_failed)++stats_.resource_decode_failed;
            else ++stats_.limits;
            slot.reset();continue;
        }
        c.asset=resource.asset;c.local=!v.control.world_origin;c.origin=v.control.world_origin.value_or(assets::AssetVector3{});
        c.volume=v.control.volume;c.pitch=v.control.pitch;c.attenuation=v.control.attenuation;
        c.loop_enabled=c.repeat_sample=v.control.repeat_sample;c.presentation_voice=true;c.scheduled_seconds=v.control.scheduled_seconds;
        if(output_.submit(c)) {v.started=true;++stats_.submitted;} else {slot.reset();++stats_.output_queue_rejected;}
    }
    for(std::size_t i=0;i<std::min(batch.count,batch.cues.size());++i) {
        const auto& cue=batch.cues[i];
        if(cue.serial<=high_serial_) {++stats_.duplicates; continue;}
        high_serial_=cue.serial;
        if(cue.scope!=scope_) {++stats_.cancelled; continue;}
        if(!audible) {++stats_.muted; if(movement_cue(cue.kind)) ++stats_.movement_muted;
            if(impact_cue(cue.kind)) ++stats_.impact_muted;
            if(shell_cue(cue.kind)) ++stats_.shell_muted; continue;}
        if(!valid_local_sound_reference(cue.reference) || !std::isfinite(cue.scheduled_seconds) ||
           cue.scheduled_seconds>now+0.101) {++stats_.limits;
            if(impact_cue(cue.kind)) ++stats_.impact_rejected;
            if(shell_cue(cue.kind)) ++stats_.shell_rejected; continue;}
        auto free=std::find_if(pending_.begin(),pending_.end(),[](const auto& p){return !p;});
        if(free==pending_.end()) {++stats_.limits;
            if(impact_cue(cue.kind)) ++stats_.impact_rejected;
            if(shell_cue(cue.kind)) ++stats_.shell_rejected; continue;}
        *free=cue;
    }
    // Preserve this owning outbox's exact serial order during asynchronous
    // readiness. Wait only until the existing 250 ms per-cue deadline; do not
    // infer duplicates or suppress cues based on another sample's time.
    std::sort(pending_.begin(),pending_.end(),[](const auto& a,const auto& b) {
        return a && (!b || a->serial<b->serial);
    });
    for(auto& p:pending_) if(p) {
        if(now<p->scheduled_seconds)continue;
        if(!audible) {if(movement_cue(p->kind)) ++stats_.movement_muted;
            if(impact_cue(p->kind)) ++stats_.impact_muted;
            if(shell_cue(p->kind)) ++stats_.shell_muted; p.reset(); ++stats_.muted; continue;}
        if(now-p->scheduled_seconds>0.25) {if(movement_cue(p->kind)) ++stats_.movement_late;
            if(impact_cue(p->kind)) ++stats_.impact_late;
            if(shell_cue(p->kind)) ++stats_.shell_late; p.reset(); ++stats_.late; continue;}
        if(!assets) continue;
        const auto asset=assets->request_local(p->reference);
        if(asset.status==SoundAssetStatus::pending) {++stats_.resource_pending; continue;}
        if(asset.status!=SoundAssetStatus::ready || !asset.asset) {
            if(asset.status==SoundAssetStatus::not_authorized) ++stats_.resource_not_authorized;
            if(asset.status==SoundAssetStatus::open_failed) ++stats_.resource_open_failed;
            if(asset.status==SoundAssetStatus::decode_failed) ++stats_.resource_decode_failed;
            if(asset.status==SoundAssetStatus::missing) {
                ++stats_.missing;
                if(movement_cue(p->kind)) ++stats_.movement_missing;
                if(impact_cue(p->kind)) ++stats_.impact_missing;
                if(shell_cue(p->kind)) ++stats_.shell_missing;
            }
            else {++stats_.limits;
                if(impact_cue(p->kind)) ++stats_.impact_rejected;
                if(shell_cue(p->kind)) ++stats_.shell_rejected;
            } // unsupported PCM/reference/capacity, not a missing file
            p.reset(); continue;
        }
        audio::Command c;
        // Body and voice occupy their own local channel identities; neither
        // replaces server entity sounds or the E2 weapon channel.
        const auto channel=p->channel;
        c.voice=voice_namespace_|
            (channel==game_api::LocalSoundChannel::weapon ? 1U :
             channel==game_api::LocalSoundChannel::body ? 2U :
             channel==game_api::LocalSoundChannel::voice ? 3U : 4U+next_voice_++);
        c.asset=asset.asset;
        c.local=!p->world_origin.has_value(); c.loop_enabled=false;
        c.origin=p->world_origin.value_or(assets::AssetVector3{});
        c.attenuation=p->world_origin ? p->attenuation : 0.0F;
        c.presentation_voice=true;
        c.volume=p->volume; c.pitch=p->pitch; c.scheduled_seconds=p->scheduled_seconds;
        // Named local channel replaces only its own previous voice. Automatic
        // markers are independent, never replacing CHAN_ITEM/world/entity voices.
        if(output_.submit(c)) {
            ++stats_.submitted;
            if(movement_cue(p->kind)) ++stats_.movement_submitted;
            if(impact_cue(p->kind)) ++stats_.impact_submitted;
            if(shell_cue(p->kind)) ++stats_.shell_submitted;
        } else {++stats_.limits; ++stats_.output_queue_rejected;
            if(impact_cue(p->kind)) ++stats_.impact_rejected;
            if(shell_cue(p->kind)) ++stats_.shell_rejected;
        }
        p.reset();
    }
}
}

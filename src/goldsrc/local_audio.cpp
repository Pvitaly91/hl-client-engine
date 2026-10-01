#include <hlclient/goldsrc/local_audio.hpp>
#include <algorithm>
#include <cmath>
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
bool valid_local_sound_reference(const game_api::LocalSoundReference& r) noexcept {
    if(r.source!=game_api::SoundReferenceSource::validated_local_model_event &&
       r.source!=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile) return false;
    const auto name=r.name();
    const bool player_profile=r.source==game_api::SoundReferenceSource::pinned_halflife_client_sound_profile &&
        name.starts_with("player/");
    const bool debris_profile=r.source==game_api::SoundReferenceSource::pinned_halflife_client_sound_profile &&
        name.starts_with("debris/");
    if(name.size()>63 || (!name.starts_with("weapons/") && !name.starts_with("items/") &&
        !player_profile && !debris_profile) || !name.ends_with(".wav") ||
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
        if(session_!=batch.session) {session_=batch.session;high_serial_=0;}
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
    for(std::size_t i=0;i<std::min(batch.count,batch.cues.size());++i) {
        const auto& cue=batch.cues[i];
        if(cue.serial<=high_serial_) {++stats_.duplicates; continue;}
        high_serial_=cue.serial;
        if(cue.scope!=scope_) {++stats_.cancelled; continue;}
        if(!audible) {++stats_.muted; if(movement_cue(cue.kind)) ++stats_.movement_muted;
            if(impact_cue(cue.kind)) ++stats_.impact_muted;
            if(shell_cue(cue.kind)) ++stats_.shell_muted; continue;}
        if(!valid_local_sound_reference(cue.reference) || !std::isfinite(cue.scheduled_seconds) ||
           cue.scheduled_seconds>now+0.001) {++stats_.limits;
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

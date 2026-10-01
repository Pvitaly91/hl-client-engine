#include <hlclient/goldsrc/server_audio.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::goldsrc {
void CommittedSoundQueue::reset_generation(std::uint64_t generation) noexcept {
    reset(); generation_=generation;
    CommittedSound marker; marker.generation=generation; marker.opcode=RuntimeControlOpcode::svc_nop;
    events_[0]=marker; size_=1;
}
void CommittedSoundQueue::publish(std::uint64_t record,std::size_t ordinal,const RuntimeControlEvent& event,SoundTime now) noexcept {
    const auto* sound=std::get_if<RuntimeControlSound>(&event.body);
    if(const auto* fixed=std::get_if<RuntimeControlExactFixedBody>(&event.body);fixed && fixed->sound) sound=&*fixed->sound;
    const auto* user=std::get_if<RuntimeControlUserInfoUpdate>(&event.body);
    if(!sound && !user) return;
    const auto generation=event.provenance.source_generation;
    const auto cursor=event.provenance.end_cursor.absolute_bit_offset();
    if(generation_ && generation<generation_) {++duplicates; return;}
    if(generation_!=generation) {reset(); generation_=generation;}
    if(ordinal<high_ordinal_ || (ordinal==high_ordinal_ && cursor<=high_cursor_)) {++duplicates; return;}
    high_ordinal_=ordinal; high_cursor_=cursor;
    if(user) {} // Safe slot boundary only; never retain private identity data.
    else if(sound->field_mask&32U) ++stops;
    else if(sound->field_mask&192U) ++changes;
    else if(event.opcode==RuntimeControlOpcode::svc_spawnstaticsound) ++statics;
    else ++starts;
    if(size_==events_.size()) {++dropped; overflow_=true; return;}
    auto routed=sound ? *sound : RuntimeControlSound{};
    if(user) routed.entity_reference=static_cast<std::uint16_t>(user->client_index+1U);
    events_[(head_+size_++)%events_.size()]={generation,record,ordinal,cursor,event.provenance.message_ordinal,event.opcode,routed,now};
}
bool CommittedSoundQueue::pop(CommittedSound& event) noexcept {
    if(!size_) return false;
    event=events_[head_]; head_=(head_+1)%events_.size(); --size_; return true;
}
void ServerAudio::reset(std::uint64_t generation) noexcept {
    (void)output_.submit(audio::Command{audio::Operation::reset});
    for(auto& slot:slots_) slot={};
    generation_=generation; high_ordinal_=high_cursor_=0;
}
void ServerAudio::stop(Slot& s) noexcept {
    if(s.playing) {auto c=s.command; c.operation=audio::Operation::stop; (void)output_.submit(c);}
    s={}; ++stats_.stopped;
}
void ServerAudio::consume(const CommittedSound& event,SoundTime now) noexcept {
    if(event.generation<generation_) return;
    if(event.opcode==RuntimeControlOpcode::svc_nop && event.record==0) {reset(event.generation); return;}
    if(event.generation!=generation_) reset(event.generation);
    if(event.ordinal<high_ordinal_ || (event.ordinal==high_ordinal_ && event.cursor<=high_cursor_)) return;
    high_ordinal_=event.ordinal; high_cursor_=event.cursor;
    const auto& sound=event.sound;
    if(event.opcode==RuntimeControlOpcode::svc_updateuserinfo) {
        // A committed slot update is a conservative attachment boundary, not
        // proof of disconnect. Do not transfer an old loop to a new occupant.
        for(auto& s:slots_) if(s.used && s.event.sound.entity_reference==sound.entity_reference) {
            s.attachment_valid=false;
            s.command.origin={s.event.sound.origin[0],s.event.sound.origin[1],s.event.sound.origin[2]};
            ++stats_.attachment_invalidated;
        }
        return;
    }
    if(!std::all_of(sound.origin.begin(),sound.origin.end(),[](float v){return std::isfinite(v);})) {
        ++stats_.invalid_origin; error("invalid_origin"); return;
    }
    if(sound.field_mask&16U || sound.channel==7 || sound.entity_reference>8191) {
        ++stats_.unsupported; if(sound.field_mask&16U) ++stats_.sentences;
        error(sound.field_mask&16U ? "unsupported_sentence" : "unsupported_source"); return;
    }
    const bool stopping=(sound.field_mask&32U)!=0, changing=(sound.field_mask&192U)!=0;
    const bool static_voice=sound.channel==6;
    const auto volume=static_cast<float>(sound.volume.value_or(255))/255.0F;
    const auto pitch=static_cast<float>(sound.pitch.value_or(100))/100.0F;
    if(!stopping && pitch<=0) {++stats_.unsupported; error("invalid_pitch"); return;}
    bool matched=false;
    for(auto& s:slots_) if(s.used && s.event.sound.entity_reference==sound.entity_reference && s.event.sound.channel==sound.channel) {
        const bool sample=s.event.sound.sound_reference==sound.sound_reference;
        if(stopping && (sample || event.opcode==RuntimeControlOpcode::svc_stopsound)) {stop(s); matched=true; break;}
        else if(changing && sample) {
            if(sound.field_mask&64U) s.command.volume=volume;
            if(sound.field_mask&128U) {
                if(s.playing && s.expires!=SoundTime::max())
                    s.expires=now+std::chrono::duration_cast<SoundTime::duration>((s.expires-now)*static_cast<double>(s.command.pitch/pitch));
                s.command.pitch=pitch;
            }
            if(s.playing) {auto c=s.command; c.operation=audio::Operation::change; (void)output_.submit(c);}
            ++stats_.updated; matched=true; break;
        } else if(!stopping && !changing && sound.channel!=0 && (!static_voice || sample)) stop(s);
    }
    if(stopping || changing) {if(!matched) {++stats_.no_voice; if(changing) error("change_without_voice");} return;}
    // CHAN_AUTO has independent voices; ambient channels may host distinct
    // samples for one entity. Dynamic named channels replace that entity only.
    if(static_voice && std::count_if(slots_.begin(),slots_.end(),[](const auto& s){return s.used && s.command.static_voice;})>=audio::maximum_static_voices) {++stats_.limits; return;}
    for(auto& s:slots_) if(!s.used) {
        s.used=true; s.event=event; s.seen=now;
        s.command={audio::Operation::start,++next_voice_,{},
            {sound.origin[0],sound.origin[1],sound.origin[2]},volume,
            static_cast<float>(sound.attenuation.value_or(64))/64.0F,pitch,
            static_voice,sound.entity_reference!=0 && sound.entity_reference==receiving_entity_,sound.channel!=5};
        s.expires=event.received+std::chrono::seconds{10}; // bounded pending; loops not immortal before load
        return;
    }
    ++stats_.limits;
}
void ServerAudio::update(SoundAssets* assets,SoundTime now) noexcept {
    for(auto& s:slots_) if(s.used) {
        if(now>=s.expires) {stop(s); ++stats_.expired; continue;}
        if(s.playing || !assets) continue;
        const auto loaded=assets->request(s.event.sound.sound_reference);
        if(loaded.status==SoundAssetStatus::pending) {if(!s.pending_reported) {++stats_.pending; s.pending_reported=true;} continue;}
        if(loaded.status!=SoundAssetStatus::ready || !loaded.asset) {
            if(loaded.status==SoundAssetStatus::limit) {++stats_.limits; error("asset_budget");}
            else if(loaded.status==SoundAssetStatus::unsupported) {++stats_.unsupported; ++stats_.formats; error("unsupported_asset");}
            else if(loaded.status==SoundAssetStatus::not_authorized) {++stats_.not_authorized; error("resource_not_authorized");}
            else if(loaded.status==SoundAssetStatus::open_failed) {++stats_.open_failed; error("resource_open_failed");}
            else if(loaded.status==SoundAssetStatus::decode_failed) {++stats_.decode_failed; error("resource_decode_failed");}
            else {++stats_.missing; error("resource_missing");}
            s={}; continue;
        }
        const bool loop=loaded.asset->loop.has_value() && s.command.loop_enabled;
        if(!loop && now-s.event.received>std::chrono::milliseconds{250}) {s={}; ++stats_.expired; continue;}
        s.command.asset=loaded.asset;
        if(!output_.submit(s.command)) {s={}; ++stats_.voice_rejected; error("output_queue_rejected"); continue;}
        s.playing=true; ++stats_.started; ++stats_.loads;
        s.attached=loop && s.attachment_valid;
        // Generous one-shot retention, independent of render/audio clocks.
        // Mixer is authoritative for exact PCM completion; slot is bookkeeping.
        const auto seconds=static_cast<double>(loaded.asset->interleaved_samples.size())/
            loaded.asset->channel_count/loaded.asset->sample_rate/s.command.pitch;
        s.expires=loop ? SoundTime::max() : now+std::chrono::duration_cast<SoundTime::duration>(std::chrono::duration<double>{seconds})+std::chrono::milliseconds{50};
    }
}
void ServerAudio::present(std::span<const SoundSource> sources,audio::Listener listener,SoundTime now,std::uint32_t receiving) noexcept {
    receiving_entity_=receiving; output_.set_listener(listener);
    for(auto& s:slots_) if(s.used && s.event.sound.entity_reference) {
        const auto found=std::find_if(sources.begin(),sources.end(),[&](const auto& p){return p.entity==s.event.sound.entity_reference;});
        if(s.attached && s.attachment_valid) {
            if(found!=sources.end() && std::isfinite(found->position.x) && std::isfinite(found->position.y) && std::isfinite(found->position.z)) {s.command.origin=found->position; s.seen=now;}
            else if(now-s.seen>std::chrono::seconds{2}) s.command.origin={s.event.sound.origin[0],s.event.sound.origin[1],s.event.sound.origin[2]};
        }
        s.command.local=s.event.sound.entity_reference==receiving;
        if(s.playing) {auto c=s.command; c.operation=audio::Operation::source; (void)output_.submit(c);}
    }
}
}

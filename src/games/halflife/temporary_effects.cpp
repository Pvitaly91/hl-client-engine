#include <hlclient/games/halflife/temporary_effects.hpp>
#include <hlclient/games/halflife/blood_decal_profile.hpp>
#include <hlclient/games/halflife/crossbow_profile.hpp>
#include <hlclient/games/halflife/blood_profile.hpp>
#include <algorithm>
#include <cmath>
#include <string_view>
namespace hlclient::games::halflife {
namespace {
constexpr game_api::LocalSoundReference explosion_reference(std::string_view name) noexcept {
    game_api::LocalSoundReference reference;
    reference.source=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    for(std::size_t i=0;i<name.size();++i) reference.sample[i]=name[i];
    return reference;
}
// Actual approved Half-Life explosion PCM family, not the separate server
// weapons/debris*.wav aftermath. Pinned SDK verifies TE_EXPLOSION client sound
// and NOSOUND but does not expose the engine's sample RNG/attenuation. Stable
// variant selection and gain1/pitch1/ATTN_NORM0.8 are local-compatible policy,
// not a claim of stock-binary parity.
constexpr std::array explosion_sounds{
    explosion_reference("weapons/explode3.wav"),
    explosion_reference("weapons/explode4.wav"),
    explosion_reference("weapons/explode5.wav")};
const game_api::LocalSoundReference& explosion_sound(const game_api::TemporaryEffectIdentity& identity) noexcept {
    auto hash=identity.record ^ (identity.generation*0x9e3779b97f4a7c15ULL) ^
        (static_cast<std::uint64_t>(identity.message_bit_offset)<<32U) ^ identity.entry_ordinal;
    hash^=hash>>30U;hash*=0xbf58476d1ce4e5b9ULL;
    hash^=hash>>27U;hash*=0x94d049bb133111ebULL;hash^=hash>>31U;
    return explosion_sounds[hash%explosion_sounds.size()];
}
void blood_sprites(game_api::TemporaryEffectIntent& intent,std::uint16_t spray,
    std::uint16_t drops,std::uint8_t size,const std::array<float,3>& tint) noexcept {
    const auto profile=blood_sprite_profile(size);
    // Blood-specific auxiliary-informed scale, distinct from the smoke byte
    // tenths rule. Original current-frame geometry is scaled exactly once.
    // Timing/ballistics remain local-compatible E27.1 values, not SDK facts.
    intent.sprite=game_api::TemporarySpriteIntent{spray,profile.spray_scale,0,1,20,
        game_api::TemporarySpriteBlend::alpha_blended,.5,tint,0,false};
    game_api::TemporarySpriteBurstIntent burst;
    burst.sprite={drops,profile.drop_scale,0,1,0,game_api::TemporarySpriteBlend::alpha_blended,.9,tint,800,false};
    burst.sprite.animation=game_api::TemporarySpriteAnimation::fixed;
    burst.count=profile.drop_count; // Separate bounded density, never size==count.
    burst.minimum_velocity={-64,-64,96};burst.maximum_velocity={64,64,192};
    burst.minimum_lifetime_seconds=.6;burst.maximum_lifetime_seconds=.9;
    burst.seed=static_cast<std::uint32_t>(intent.identity.record ^
        (intent.identity.generation*0x9e3779b97f4a7c15ULL) ^ intent.identity.message_bit_offset);
    intent.sprite_burst=burst;
}
}
std::span<const game_api::LocalSoundReference> TemporaryEffects::sound_preparation() const noexcept {
    return explosion_sounds;
}
void TemporaryEffects::reset(std::uint64_t generation) noexcept {
    generation_=generation;seen_.fill({});next_seen_=0;pending_={};
    decal_assets_by_index_.fill(-1);
    seen_hits_.fill({});next_hit_=0;hit_serial_=0;
    snark_model_=pop_sample_=spray_model_=blood_model_=0;
    snarks_={};entity_ordinal_=0;pops_={};next_pop_=0;
    palette_={};blood_enabled_.reset();
}
void TemporaryEffects::bind_palette(const game_api::EffectPaletteMetadata& palette) noexcept {
    palette_=palette;
}
void TemporaryEffects::bind_resource(const game_api::EffectResourceBinding& resource) noexcept {
    if(resource.kind==game_api::EffectResourceBinding::Kind::decal) {
        if(resource.slot<decal_assets_by_index_.size()) {
            const auto slot=blood_decal_asset_slot(resource.name);
            decal_assets_by_index_[resource.slot]=slot ? static_cast<std::int8_t>(*slot) : std::int8_t{-2};
        }
        return;
    }
    if(resource.kind==game_api::EffectResourceBinding::Kind::model && resource.slot>0 && resource.slot<=1023) {
        if(resource.name=="models/w_squeak.mdl")snark_model_=resource.slot;
        if(resource.name=="sprites/bloodspray.spr")spray_model_=resource.slot;
        if(resource.name=="sprites/blood.spr")blood_model_=resource.slot;
    } else if(resource.kind==game_api::EffectResourceBinding::Kind::sound && resource.slot<512 &&
        resource.name=="squeek/sqk_blast1.wav")pop_sample_=resource.slot;
}
void TemporaryEffects::entities(std::span<const client::RuntimePacketEntityObservation> entities,
    std::uint32_t maximum_clients,std::uint64_t ordinal) noexcept {
    if(!ordinal || ordinal<=entity_ordinal_)return;
    entity_ordinal_=ordinal;
    for(auto& source:snarks_) {
        if(!source.entity)continue;
        const auto found=std::ranges::find(entities,source.entity,&client::RuntimePacketEntityObservation::entity_number);
        if(found!=entities.end() && found->model_index.value_or(0)!=0 &&
            (!found->ordinary_visual_schema || found->player_movement_schema || found->model_index!=snark_model_)) {
            source={};continue; // Observed reuse cannot inherit the old creature.
        }
        if(found==entities.end() || found->model_index.value_or(0)==0) {
            if(!source.retired)source.retired=ordinal;
        }
    }
    if(!snark_model_)return;
    for(const auto& entity:entities) {
        if(entity.entity_number<=maximum_clients || entity.entity_number>8191 || !entity.ordinary_visual_schema ||
            entity.player_movement_schema || entity.model_index!=snark_model_)continue;
        auto slot=std::ranges::find(snarks_,entity.entity_number,&SnarkSource::entity);
        if(slot!=snarks_.end() && !slot->retired)continue;
        if(slot==snarks_.end())slot=std::ranges::find_if(snarks_,[&](const auto& s){return !s.entity || (s.retired && s.retired<ordinal);});
        if(slot==snarks_.end()) {++pending_.statistics.pop_capacity;continue;}
        *slot={entity.entity_number,ordinal,0,false};
    }
}
bool TemporaryEffects::correlate_pop(assets::AssetVector3 origin,double time,bool sound) noexcept {
    // TE has no emitter ID. One-to-one, tightly bounded cosmetic correlation,
    // not death/ownership evidence. Distinct colocated sounds still get a pop.
    for(auto& receipt:pops_) {
        const auto dx=origin.x-receipt.origin.x,dy=origin.y-receipt.origin.y,dz=origin.z-receipt.origin.z;
        if(receipt.used && !receipt.matched && receipt.sound!=sound &&
            std::abs(time-receipt.time)<=.25 && dx*dx+dy*dy+dz*dz<=4) {
            receipt.matched=true;++pending_.statistics.pop_te_correlated;return true;
        }
    }
    pops_[next_pop_++%pops_.size()]={origin,time,true,sound,false};return false;
}
void TemporaryEffects::sound(const game_api::CommittedSoundObservation& event) noexcept {
    if(!pop_sample_ || event.sample_slot!=pop_sample_)return;
    auto& stats=pending_.statistics;++stats.pop_sound_received;
    if(blood_enabled_==false) {++stats.pop_policy_suppressed;return;}
    if(!generation_ || event.generation!=generation_ || !event.record || !event.record_ordinal ||
        event.channel!=3 || (event.flags&~15U) || !std::isfinite(event.received_at_seconds) ||
        event.received_at_seconds<0 || !std::isfinite(event.origin.x) ||
        !std::isfinite(event.origin.y) || !std::isfinite(event.origin.z)) {++stats.pop_invalid;return;}
    const game_api::TemporaryEffectIdentity identity{event.generation,event.record,event.message_bit_offset,
        event.entry_ordinal,game_api::TemporaryEffectIdentity::Source::server_sound};
    if(std::ranges::any_of(seen_,[&](const auto& entry){return entry && *entry==identity;})) {++stats.pop_duplicate;return;}
    seen_[next_seen_++%seen_.size()]=identity;
    auto source=std::ranges::find(snarks_,event.entity,&SnarkSource::entity);
    if(!event.entity || source==snarks_.end() || event.record_ordinal<source->born ||
        event.record_ordinal<entity_ordinal_ || (source->retired && source->retired!=event.record_ordinal)) {
        ++stats.pop_entity_rejected;return;
    }
    if(source->popped) {++stats.pop_duplicate;return;}
    source->popped=true; // Exactly once per observed creature, never replay a missed visual later.
    if(!spray_model_ || !blood_model_) {++stats.pop_resource_missing;return;}
    if(palette_.status!=game_api::EffectPaletteStatus::ready) {++stats.blood_palette_unavailable;return;}
    if(pending_.count==pending_.effects.size()) {++stats.pop_capacity;return;}
    if(correlate_pop(event.origin,event.received_at_seconds,true))return;
    game_api::TemporaryEffectIntent intent{identity,event.origin,event.received_at_seconds};
    intent.record_ordinal=event.record_ordinal;
    intent.diagnostic_tag=2;
    const auto& rgb=palette_.colors[195];
    blood_sprites(intent,spray_model_,blood_model_,16,
        {rgb[0]/255.F,rgb[1]/255.F,rgb[2]/255.F});
    pending_.effects[pending_.count++]=intent;++stats.pop_sound_accepted;
    // Original svc_sound remains the sole audio event. No fabricated TE,
    // extra voice, damage, removal, fireball, decal or explosion light.
}
void TemporaryEffects::commit(const client::CommittedTemporaryEffect& event) noexcept {
    auto& stats=pending_.statistics;++stats.received;
    const bool blood=event.kind==client::TemporaryEffectKind::blood_sprite;
    if(blood)++stats.blood_received;
    if(!generation_ || event.generation!=generation_) {++stats.stale;if(blood)++stats.blood_stale;return;}
    if(!event.record || !std::isfinite(event.received_at_seconds) || event.received_at_seconds<0 ||
        !std::isfinite(event.origin.x) || !std::isfinite(event.origin.y) || !std::isfinite(event.origin.z)) {
        ++stats.invalid;if(blood)++stats.blood_invalid;return;
    }
    const game_api::TemporaryEffectIdentity identity{event.generation,event.record,event.message_bit_offset,event.entry_ordinal};
    if(std::ranges::any_of(seen_,[&](const auto& seen){return seen && *seen==identity;})) {++stats.duplicate;if(blood)++stats.blood_duplicate;return;}
    seen_[next_seen_++%seen_.size()]=identity;
    if(event.kind==client::TemporaryEffectKind::decal) {
        ++stats.decal_received;
        if(event.decal_index<0 || static_cast<std::size_t>(event.decal_index)>=decal_assets_by_index_.size() ||
            decal_assets_by_index_[static_cast<std::size_t>(event.decal_index)]==-1) {
            ++stats.decal_index_unresolved;return;
        }
        const auto slot=decal_assets_by_index_[static_cast<std::size_t>(event.decal_index)];
        if(slot<0) {++stats.decal_asset_unsupported;return;}
        if(event.decal_entity<0 || event.decal_entity>8191 ||
            (event.decal_model_reference && (*event.decal_model_reference<=0 || *event.decal_model_reference>1023))) {
            ++stats.decal_target_invalid;return;
        }
        if(pending_.count==pending_.effects.size()) {++stats.decal_capacity;return;}
        game_api::TemporaryEffectIntent intent{identity,event.origin,event.received_at_seconds};
        intent.record_ordinal=event.record_ordinal;
        intent.surface_decal=game_api::TemporarySurfaceDecalIntent{static_cast<std::uint8_t>(slot),
            static_cast<std::uint16_t>(event.decal_index),static_cast<std::uint32_t>(event.decal_entity),event.decal_model_reference,
            event.decal_target_pose};
        pending_.effects[pending_.count++]=intent;++stats.accepted;++stats.decal_accepted;return;
    }
    if(event.kind==client::TemporaryEffectKind::blood_sprite) {
        // Pinned util.h supplies the palette tokens, not RGB. Both real TE
        // colors use approved host_basepal and the same field-driven profile;
        // yellow alone does not establish a Snark emitter/death identity.
        const bool yellow=event.palette_color==195U, red=event.palette_color==247U;
        if(!yellow && !red) {++stats.unsupported_palette;++stats.blood_palette;return;}
        if(!event.blood_scale || std::ranges::any_of(event.blood_sprite_indices,
            [](auto slot){return slot<=0 || slot>1023;})) {++stats.invalid;++stats.blood_invalid;return;}
        if(pending_.count==pending_.effects.size()) {++stats.capacity;++stats.blood_capacity;return;}
        if(palette_.status!=game_api::EffectPaletteStatus::ready) {++stats.blood_palette_unavailable;return;}
        if(yellow && event.blood_sprite_indices[0]==spray_model_ && event.blood_sprite_indices[1]==blood_model_ &&
            (event.blood_scale==8 || event.blood_scale==16) && correlate_pop(event.origin,event.received_at_seconds,false))return;
        game_api::TemporaryEffectIntent intent{identity,event.origin,event.received_at_seconds};
        intent.record_ordinal=event.record_ordinal;
        // Red vs large/small yellow wire diagnostic groups do not establish
        // an emitter or Snark ownership. Tags remain opaque to mechanisms;
        // they do not select assets, change delivery, or create an effect.
        intent.diagnostic_tag=red ? 1U : event.blood_scale>=8U ? 2U : 3U;
        const auto& rgb=palette_.colors[event.palette_color];
        blood_sprites(intent,static_cast<std::uint16_t>(event.blood_sprite_indices[0]),
            static_cast<std::uint16_t>(event.blood_sprite_indices[1]),event.blood_scale,
            {rgb[0]/255.F,rgb[1]/255.F,rgb[2]/255.F});
        // Blood alone is neither explosion light/fireball nor sound/damage.
        pending_.effects[pending_.count++]=intent;++stats.accepted;++stats.blood_sprites;return;
    }
    if(event.kind!=client::TemporaryEffectKind::explosion && event.kind!=client::TemporaryEffectKind::smoke &&
        event.kind!=client::TemporaryEffectKind::sparks) {
        ++stats.deferred_kind;return; // Exact water grammar remains safe, visuals explicitly deferred.
    }
    // SDK const.h verifies all four low bits. Unknown extensions are consumed
    // by framing but rejected as a presentation component set, never guessed.
    if(event.kind==client::TemporaryEffectKind::explosion && (event.flags & ~15U)) {
        ++stats.unsupported_flags;return;
    }
    game_api::TemporaryEffectIntent intent{identity,event.origin,event.received_at_seconds};
    intent.record_ordinal=event.record_ordinal;
    if(event.kind==client::TemporaryEffectKind::sparks) {
        if(pending_.count==pending_.effects.size()) {++stats.capacity;return;}
        intent.particles=crossbow_sparks();
        pending_.effects[pending_.count++]=intent;++stats.accepted;++stats.sparks;return;
    }
    const bool explosion=event.kind==client::TemporaryEffectKind::explosion;
    if(event.sprite_index && event.scale_tenths && event.framerate) {
        intent.sprite=game_api::TemporarySpriteIntent{event.sprite_index,event.scale_tenths*0.1F,
            explosion ? 8.0F : 30.0F,explosion ? 1.0F : 0.65F,event.framerate,
            explosion ? ((event.flags&1U) ? game_api::TemporarySpriteBlend::opaque : game_api::TemporarySpriteBlend::additive)
                : game_api::TemporarySpriteBlend::alpha_blended,4.0};
    }
    if(explosion) {
        ++stats.explosion;
        if(!(event.flags&2U)) intent.light=game_api::TemporaryLightIntent{192.0F,1.1F,{1.0F,0.55F,0.2F},0.35};
        if(!(event.flags&8U)) {
            game_api::ImpactParticleProfile profile;
            profile.count=16;profile.lifetime_seconds=0.65F;
            profile.minimum_speed_units=96;profile.maximum_speed_units=240;
            profile.gravity_units=120;profile.radius_units=4;
            profile.color={1.0F,0.56F,0.15F,0.8F};profile.emissive=true;
            profile.radial_distribution=true;profile.flicker=true;
            intent.particles=profile;
        }
        if(!(event.flags&4U))
            intent.sound=game_api::TemporarySoundIntent{explosion_sound(identity),1.0F,1.0F,0.8F};
        else ++stats.main_audio_suppressed;
    } else ++stats.smoke;
    if(pending_.count==pending_.effects.size()) {++stats.capacity;return;}
    if(intent.sound) ++stats.main_audio_requested;
    pending_.effects[pending_.count++]=intent;++stats.accepted;
}
void TemporaryEffects::sniper_world_hit(const game_api::LocalWeaponActionIdentity& action,
    assets::AssetVector3 point,assets::AssetVector3 direction,double now) noexcept {
    const auto finite=[](assets::AssetVector3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    const auto length=std::hypot(direction.x,direction.y,direction.z);
    if(!generation_ || action.generation!=generation_ || action.weapon_id!=6 ||
        action.kind!=game_api::LocalWeaponAction::primary_fire || !finite(point) || !finite(direction) ||
        !std::isfinite(now) || now<action.started_at_seconds || now-action.started_at_seconds>.25 ||
        length<.99F || length>1.01F) return;
    if(std::ranges::any_of(seen_hits_,[&](const auto& seen){return seen && *seen==action;})) return;
    seen_hits_[next_hit_++%seen_hits_.size()]=action;
    auto& stats=pending_.statistics;
    if(pending_.count==pending_.effects.size()) {++stats.capacity;return;}
    game_api::TemporaryEffectIntent intent;
    intent.identity={generation_,++hit_serial_,action.command_sequence,action.emitter_entity,
        game_api::TemporaryEffectIdentity::Source::game_action};
    intent.starts_at_seconds=now;intent.origin=point;intent.particles=crossbow_sparks();
    game_api::TemporaryStudioIntent model;
    constexpr std::string_view name="models/crossbow_bolt.mdl";
    std::copy(name.begin(),name.end(),model.model_name.begin());
    constexpr float degrees=180.0F/3.14159265358979323846F;
    model.angles_degrees={std::atan2(direction.z,std::hypot(direction.x,direction.y))*degrees,
        std::atan2(direction.y,direction.x)*degrees,0};
    model.lifetime_seconds=5;intent.model=model;
    intent.origin={point.x-direction.x*10,point.y-direction.y*10,point.z-direction.z*10};
    // Sparks originate at contact, not at the pulled-out model anchor. Publish
    // independent neutral components with distinct provenance in the same batch.
    intent.particles.reset();
    pending_.effects[pending_.count++]=intent;++stats.accepted;++stats.stuck_models;
    if(pending_.count==pending_.effects.size()) {++stats.capacity;return;}
    intent.identity.entry_ordinal+=32U;intent.origin=point;intent.model.reset();intent.particles=crossbow_sparks();
    pending_.effects[pending_.count++]=intent;++stats.accepted;++stats.sparks;
}
game_api::TemporaryEffectsBatch TemporaryEffects::drain(double now) noexcept {
    auto result=pending_;pending_.count=0;
    if(!std::isfinite(now) || now<0) {
        for(std::size_t i=0;i<result.count;++i)if(result.effects[i].diagnostic_tag>=1 && result.effects[i].diagnostic_tag<=3)++pending_.statistics.blood_delivery_stale;
        result.count=0;result.statistics=pending_.statistics;return result;
    }
    std::size_t count{};
    for(std::size_t i=0;i<result.count;++i) {
        const auto age=now-result.effects[i].starts_at_seconds;
        if(result.effects[i].surface_decal) {
            // Persistence begins on publication, not on a sprite/HUD clock.
            // A bounded readiness stall is allowed; an older skipped placement
            // is never resurrected later or retimed into another generation.
            if(age>2.0) {++pending_.statistics.decal_stale;continue;}
            result.effects[count++]=result.effects[i];continue;
        }
        // Subtracting the steady-clock origin can put the same instant a few
        // ulps ahead of the frame clock. Accept only numerical noise, not an
        // actually future event, and publish a nonnegative presentation age.
        if(age < -0.000001 || age>0.25) {++pending_.statistics.stale;if(result.effects[i].diagnostic_tag>=1 && result.effects[i].diagnostic_tag<=3)++pending_.statistics.blood_delivery_stale;continue;}
        if(age<0) result.effects[i].starts_at_seconds=now;
        result.effects[count++]=result.effects[i];
    }
    result.count=count;result.statistics=pending_.statistics;return result;
}
}

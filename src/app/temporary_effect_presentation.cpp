#include <hlclient/app/temporary_effect_presentation.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::app {
namespace {
bool valid_sprite(const game_api::TemporarySpriteIntent& p) noexcept {
    return p.model_slot && p.animation<=game_api::TemporarySpriteAnimation::fixed &&
        (p.animation==game_api::TemporarySpriteAnimation::fixed || p.frames_per_second) &&
        std::isfinite(p.scale) && p.scale>0 && p.scale<=25.5F &&
        std::isfinite(p.vertical_speed_units) && std::abs(p.vertical_speed_units)<=128 &&
        std::isfinite(p.gravity_units) && p.gravity_units>=0 && p.gravity_units<=1024 &&
        std::isfinite(p.opacity) && p.opacity>=0 && p.opacity<=1 &&
        std::ranges::all_of(p.tint,[](float v){return std::isfinite(v) && v>=0 && v<=1;}) &&
        std::isfinite(p.maximum_lifetime_seconds) && p.maximum_lifetime_seconds>0 &&
        p.maximum_lifetime_seconds<=4 && p.blend<=game_api::TemporarySpriteBlend::alpha_blended;
}
bool bounded_velocity(assets::AssetVector3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
        std::abs(v.x)<=512 && std::abs(v.y)<=512 && std::abs(v.z)<=512;
}
}
void TemporaryEffectPresentation::reset(std::uint64_t generation) noexcept {
    generation_=generation;now_=0;seen_.fill({});next_seen_=next_sprite_=next_light_=0;
    active_sprites_.fill({});active_lights_.fill({});sprites_={};particles_.reset();stats_={};
    tag_stats_={};
    active_models_.fill({});models_={};next_model_=0;
    active_drops_.fill({});next_drop_=0;
}
void TemporaryEffectPresentation::consume(const game_api::TemporaryEffectsBatch& batch,double now) noexcept {
    if(!std::isfinite(now) || now<0) return;
    now_=std::max(now_,now);
    for(std::size_t i=0;i<std::min(batch.count,batch.effects.size());++i) {
        const auto& effect=batch.effects[i];
        // Persistent surface placement has a separate map lifetime in the
        // existing decal owner. It must not consume a transient clock/slot.
        if(effect.surface_decal && !effect.sprite && !effect.secondary_sprite &&
            !effect.sprite_burst && !effect.model && !effect.light && !effect.particles)continue;
        auto& tag=tag_stats_[effect.diagnostic_tag<tag_stats_.size() ? effect.diagnostic_tag : 0U];
        if(!generation_ || effect.identity.generation!=generation_ || effect.starts_at_seconds>now_ ||
            now_-effect.starts_at_seconds>0.25) {++stats_.stale;++tag.stale;continue;}
        if(!effect.identity.record || !std::isfinite(effect.starts_at_seconds) ||
            !std::isfinite(effect.origin.x) || !std::isfinite(effect.origin.y) || !std::isfinite(effect.origin.z)) {
            ++stats_.invalid;++tag.invalid;continue;
        }
        if(std::ranges::any_of(seen_,[&](const auto& seen){return seen && *seen==effect.identity;})) {
            ++stats_.duplicate;++tag.duplicate;continue;
        }
        seen_[next_seen_++%seen_.size()]=effect.identity;++stats_.accepted;++tag.accepted;
        if(effect.model) {
            const auto& m=*effect.model;
            const auto end=std::find(m.model_name.begin(),m.model_name.end(),'\0');
            const std::string_view name{m.model_name.data(),static_cast<std::size_t>(end-m.model_name.begin())};
            if(end==m.model_name.end() || !name.starts_with("models/") || !name.ends_with(".mdl") ||
                name.find("..")!=name.npos || name.find("//")!=name.npos ||
                !std::ranges::all_of(name,[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='.';}) ||
                !std::isfinite(m.lifetime_seconds) || m.lifetime_seconds<=0 || m.lifetime_seconds>5 ||
                !std::isfinite(m.angles_degrees.x) || !std::isfinite(m.angles_degrees.y) || !std::isfinite(m.angles_degrees.z))
                ++stats_.invalid;
            else {
                auto& slot=active_models_[next_model_++%active_models_.size()];
                if(slot) ++stats_.model_evictions;
                slot=effect;
            }
        }
        const auto add_sprite=[&](const game_api::TemporarySpriteIntent& p) {
            if(!valid_sprite(p)) {++stats_.invalid;++tag.invalid;}
            else {
                auto& slot=active_sprites_[next_sprite_++%active_sprites_.size()];
                if(slot) {++stats_.sprite_evictions;
                    ++tag_stats_[slot->diagnostic_tag<tag_stats_.size() ? slot->diagnostic_tag : 0U].sprite_evictions;}
                slot=effect;
                slot->sprite=p;
                slot->secondary_sprite.reset();
                slot->sprite_burst.reset();
            }
        };
        if(effect.sprite) add_sprite(*effect.sprite);
        if(effect.secondary_sprite) add_sprite(*effect.secondary_sprite);
        if(effect.sprite_burst) {
            const auto& burst=*effect.sprite_burst;
            const auto& low=burst.minimum_velocity;
            const auto& high=burst.maximum_velocity;
            if(!valid_sprite(burst.sprite) || !burst.count || burst.count>16 ||
                !bounded_velocity(low) || !bounded_velocity(high) ||
                low.x>high.x || low.y>high.y || low.z>high.z ||
                !std::isfinite(burst.minimum_lifetime_seconds) ||
                !std::isfinite(burst.maximum_lifetime_seconds) ||
                burst.minimum_lifetime_seconds<=0 ||
                burst.maximum_lifetime_seconds<burst.minimum_lifetime_seconds ||
                burst.maximum_lifetime_seconds>4) {++stats_.invalid;++tag.invalid;}
            else {
                // The module supplies the normalized effect seed. In particular,
                // corroborating sources can present identical geometry without
                // treating their distinct committed provenance as the same record.
                std::uint64_t seed=static_cast<std::uint64_t>(burst.seed)+0x9e3779b97f4a7c15ULL;
                const auto random_bits=[&]() {
                    seed^=seed>>12;seed^=seed<<25;seed^=seed>>27;
                    return static_cast<std::uint32_t>((seed*2685821657736338717ULL)>>32U);
                };
                const auto random=[&]() {return static_cast<double>(random_bits())/4294967295.0;};
                for(std::uint8_t child=0;child<burst.count;++child) {
                    const assets::AssetVector3 velocity{
                        low.x+static_cast<float>((high.x-low.x)*random()),
                        low.y+static_cast<float>((high.y-low.y)*random()),
                        low.z+static_cast<float>((high.z-low.z)*random())};
                    auto sprite=burst.sprite;
                    sprite.maximum_lifetime_seconds=burst.minimum_lifetime_seconds+
                        (burst.maximum_lifetime_seconds-burst.minimum_lifetime_seconds)*random();
                    sprite.frame_choice=random_bits();
                    auto& slot=active_drops_[next_drop_++%active_drops_.size()];
                    if(slot) {
                        ++stats_.drop_evictions;
                        ++tag_stats_[slot->frame.diagnostic_tag<tag_stats_.size() ? slot->frame.diagnostic_tag : 0U].drop_evictions;
                    }
                    slot=SpriteDrop{{effect.identity,sprite,effect.origin,0,effect.diagnostic_tag,velocity},
                        effect.starts_at_seconds};
                    ++stats_.drops_created;++tag.drops_created;
                }
            }
        }
        if(effect.light) {
            const auto& p=*effect.light;
            const renderer::RenderPointLight light{effect.origin,p.radius_units,p.intensity,p.color};
            if(!renderer::valid_render_point_light(light) || !std::isfinite(p.lifetime_seconds) ||
                p.lifetime_seconds<=0 || p.lifetime_seconds>1) ++stats_.invalid;
            else {
                auto& slot=active_lights_[next_light_++%active_lights_.size()];
                if(slot) ++stats_.light_evictions;
                slot=effect;
            }
        }
        if(effect.particles) {
            // Adapt exact provenance to the existing pool's opaque causal key.
            // This does not create a submitted weapon action, ammo or projectile.
            game_api::LocalWeaponActionIdentity key;
            key.generation=effect.identity.generation;key.resource_revision=effect.identity.record;
            key.command_sequence=effect.identity.message_bit_offset;
            key.canonical_pre_revision=effect.identity.entry_ordinal;
            key.started_at_seconds=effect.starts_at_seconds;
            key.sub_identity=static_cast<std::uint32_t>(effect.identity.source);
            if(particles_.spawn(key,effect.origin,{0,0,1},*effect.particles,now_)==renderer::ParticleBurstStatus::spawned)
                ++stats_.particle_submissions;
        }
    }
    update_models();
}
void TemporaryEffectPresentation::update_models() noexcept {
    std::size_t count{};
    for(auto& effect:active_models_) if(effect) {
        const auto age=now_-effect->starts_at_seconds;
        if(age>=effect->model->lifetime_seconds) {effect.reset();continue;}
        model_buffer_[count++]={effect->identity,*effect->model,effect->origin,age};
    }
    models_={model_buffer_.data(),count};
}
void TemporaryEffectPresentation::present(renderer::RenderScene& scene,double now) noexcept {
    if(!std::isfinite(now) || now<0) return;
    now_=std::max(now_,now);
    update_models();
    std::size_t count{};
    for(auto& effect:active_sprites_) if(effect) {
        const auto age=now_-effect->starts_at_seconds;
        if(age>=effect->sprite->maximum_lifetime_seconds) {effect.reset();continue;}
        sprite_buffer_[count++]={effect->identity,*effect->sprite,effect->origin,age,effect->diagnostic_tag};
    }
    for(auto& drop:active_drops_) if(drop) {
        const auto age=now_-drop->starts_at_seconds;
        if(age>=drop->frame.sprite.maximum_lifetime_seconds) {drop.reset();continue;}
        drop->frame.age_seconds=age;
        sprite_buffer_[count++]=drop->frame;
    }
    sprites_={sprite_buffer_.data(),count};
    for(auto& effect:active_lights_) if(effect) {
        const auto& p=*effect->light;
        const auto age=now_-effect->starts_at_seconds;
        if(age>=p.lifetime_seconds) {effect.reset();continue;}
        if(scene.world_effect_light_count>=scene.world_effect_lights.size()) continue;
        scene.world_effect_lights[scene.world_effect_light_count++]={effect->origin,p.radius_units,
            p.intensity*static_cast<float>(1.0-age/p.lifetime_seconds),p.color};++stats_.light_submissions;
    }
    particles_.update(now_);
    for(const auto& particle:particles_.particles()) {
        if(scene.world_particle_count==scene.world_particles.size()) break;
        scene.world_particles[scene.world_particle_count++]=particle;
    }
}
}

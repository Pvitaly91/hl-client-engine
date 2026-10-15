#include <hlclient/app/remote_effect_presentation.hpp>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace hlclient::app {
void RemoteEffectPresentation::reset() noexcept {
    active_.fill({});seen_.fill({});next_=next_seen_=0;
}
void RemoteEffectPresentation::consume(const game_api::RemoteWeaponEffectsBatch& batch,
    game_api::GameClientHost& host, renderer::TransientVisuals& shells,
    WorldImpactPresentation& impacts, std::shared_ptr<const collision::CollisionWorldPackage> world,
    const world_render::WorldRenderPackage* render_world,
    std::shared_ptr<const goldsrc::collision::BrushCollisionScene> blockers,
    bool shell_ready,bool decal_ready,double now,const RuntimeReplayLocalAssets* resources) noexcept {
    if(!std::isfinite(now)) return;
    for(std::size_t i=0;i<std::min(batch.count,batch.effects.size());++i) {
        const auto& effect=batch.effects[i];
        if(effect.action.emitter_entity==0 || now-effect.action.started_at_seconds>0.25 ||
            effect.action.started_at_seconds>now) continue;
        if(std::any_of(seen_.begin(),seen_.end(),[&](const auto& action) {
            return action && *action==effect.action;
        })) continue;
        seen_[next_seen_++%seen_.size()]=effect.action;
        ++stats_.consumed;
        if(effect.flash || effect.light) active_[next_++%active_.size()]=
            ActiveVisual{effect.muzzle_origin,effect.flash,effect.light};
        const auto spawn_shell=[&](const game_api::LocalShellEjection& cue) {
            const auto end=std::find(cue.model_name.begin(),cue.model_name.end(),'\0');
            const std::string_view name{cue.model_name.data(),static_cast<std::size_t>(end-cue.model_name.begin())};
            const bool ready=resources ? end!=cue.model_name.end() && resources->has_shell_model(name) : shell_ready;
            if(ready) {if(shells.spawn(cue,now)) ++stats_.shells;}
            else ++stats_.shell_unavailable;
        };
        if(effect.shell) spawn_shell(*effect.shell);
        for(std::size_t extra=0;extra<std::min(effect.extra_shell_count,effect.extra_shells.size());++extra)
            spawn_shell(effect.extra_shells[extra]);
        const auto submit_impact=[&](const game_api::LocalWorldImpactRequest& request) {
            const auto brush_context=resources ? resources->brush_impact_context() : WorldImpactPresentation::BrushImpactContext{};
            const auto hit=impacts.submit(request,world,render_world,blockers,decal_ready,now,&brush_context);
            if(hit.status==WorldImpactStatus::hit && hit.point && !request.suppress_surface_effects) {
                ++stats_.impact_hits;
                host.remote_world_impact(request.action,*hit.point,now,hit.surface);
            } else ++stats_.impact_rejected;
        };
        if(effect.impact) submit_impact(*effect.impact);
        for(std::size_t extra=0;extra<std::min(effect.extra_impact_count,effect.extra_impacts.size());++extra)
            submit_impact(effect.extra_impacts[extra]);
    }
}
void RemoteEffectPresentation::present(renderer::RenderScene& scene,double now,RuntimeReplayLocalAssets* resources) noexcept {
    if(!std::isfinite(now)) return;
    float nearest=std::numeric_limits<float>::max();
    std::optional<renderer::RenderPointLight> light;
    std::array<RuntimeReplayLocalAssets::MuzzleSpriteFrame,32> sprites{};
    std::size_t sprite_count{};
    for(auto& item:active_) {
        if(!item) continue;
        const auto& effect=*item;
        const bool flash=effect.flash && now>=effect.flash->starts_at_seconds && now<effect.flash->ends_at_seconds;
        const bool lit=effect.light && now>=effect.light->starts_at_seconds && now<effect.light->ends_at_seconds;
        if(!flash && !lit) {item.reset();continue;}
        if(flash && effect.flash->sprite && resources) {
            sprites[sprite_count++]={effect.flash->origin,*effect.flash->sprite};
        } else if(flash && scene.world_flash_count<scene.world_flashes.size()) {
            scene.world_flashes[scene.world_flash_count++]={effect.flash->origin,
                effect.flash->radius_units,effect.flash->color};
            ++stats_.flash_submissions;
        }
        if(lit) {
            const auto p=effect.muzzle_origin, c=scene.camera.position;
            const float d=(p.x-c.x)*(p.x-c.x)+(p.y-c.y)*(p.y-c.y)+(p.z-c.z)*(p.z-c.z);
            if(d<nearest) {
                nearest=d;
                light=renderer::RenderPointLight{p,effect.light->radius_units,
                    effect.light->intensity,effect.light->color};
            }
        }
    }
    // The primary world-light slot retains local muzzle priority; otherwise
    // use the closest remote muzzle. Secondary effect lights have a separate batch.
    if(!scene.transient_world_light && light) {scene.transient_world_light=light;++stats_.light_submissions;}
    // Missing/rejected optional original sprites do not manufacture a second
    // flash, delay an expired action, or suppress its independent light/audio.
    if(resources && sprite_count) {
        try {
            scene.world_muzzle_entities=resources->materialize_muzzle_sprites({sprites.data(),sprite_count},now);
            if(scene.world_muzzle_entities)stats_.flash_submissions+=scene.world_muzzle_entities->frame->sprite_instances().size();
        } catch(...) {scene.world_muzzle_entities.reset();}
    }
}
}

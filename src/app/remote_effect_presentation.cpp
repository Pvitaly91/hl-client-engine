#include <hlclient/app/remote_effect_presentation.hpp>
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
    bool shell_ready,bool decal_ready,double now) noexcept {
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
        if(effect.flash || effect.light) active_[next_++%active_.size()]=effect;
        if(effect.shell) {
            if(shell_ready) {if(shells.spawn(*effect.shell,now)) ++stats_.shells;}
            else ++stats_.shell_unavailable;
        }
        if(effect.impact) {
            const auto hit=impacts.submit(*effect.impact,world,render_world,blockers,decal_ready,now);
            if(hit.status==WorldImpactStatus::hit && hit.point) {
                ++stats_.impact_hits;
                host.remote_world_impact(effect.action,*hit.point,now,hit.surface);
            } else ++stats_.impact_rejected;
        }
    }
}
void RemoteEffectPresentation::present(renderer::RenderScene& scene,double now) noexcept {
    if(!std::isfinite(now)) return;
    float nearest=std::numeric_limits<float>::max();
    std::optional<renderer::RenderPointLight> light;
    for(auto& item:active_) {
        if(!item) continue;
        const auto& effect=*item;
        const bool flash=effect.flash && now>=effect.flash->starts_at_seconds && now<effect.flash->ends_at_seconds;
        const bool lit=effect.light && now>=effect.light->starts_at_seconds && now<effect.light->ends_at_seconds;
        if(!flash && !lit) {item.reset();continue;}
        if(flash && scene.world_flash_count<scene.world_flashes.size()) {
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
    // Existing renderer supports one point light. The local first-person shot
    // retains priority; otherwise use the closest active world light.
    if(!scene.transient_world_light && light) {scene.transient_world_light=light;++stats_.light_submissions;}
}
}

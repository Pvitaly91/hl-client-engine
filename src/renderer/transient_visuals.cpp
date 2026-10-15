#include <hlclient/renderer/transient_visuals.hpp>
#include <algorithm>
#include <cmath>
#include <bit>

namespace hlclient::renderer {
namespace {
using V=assets::AssetVector3;
V add(V a,V b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,float k) noexcept {return {a.x*k,a.y*k,a.z*k};}
float dot(V a,V b) noexcept {return a.x*b.x+a.y*b.y+a.z*b.z;}
bool finite(V a) noexcept {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
}
void ImpactParticlePool::reset() noexcept {
    bursts_.fill(std::nullopt);seen_.fill(std::nullopt);visible_={};
    next_burst_=next_seen_=0U;generation_=0U;now_=0;stats_={};
}
ParticleBurstStatus ImpactParticlePool::spawn(const game_api::LocalWeaponActionIdentity& action,
    V point,V normal,const game_api::ImpactParticleProfile& p,double now) noexcept {
    const auto reject=[&] {++stats_.invalid;return ParticleBurstStatus::invalid;};
    const float nl=std::sqrt(dot(normal,normal));
    if(!action.generation || !std::isfinite(action.started_at_seconds) || !std::isfinite(now) ||
        !finite(point)||!finite(normal)||nl<0.99F||nl>1.01F || !p.count||p.count>16U ||
        !std::isfinite(p.lifetime_seconds)||p.lifetime_seconds<=0.0F||p.lifetime_seconds>(p.radial_distribution ? 1.0F : 0.5F) ||
        !std::isfinite(p.minimum_speed_units)||!std::isfinite(p.maximum_speed_units)||
        p.minimum_speed_units<0.0F||p.maximum_speed_units<p.minimum_speed_units||p.maximum_speed_units>(p.radial_distribution ? 256.0F : 128.0F) ||
        !std::isfinite(p.gravity_units)||p.gravity_units<0.0F||p.gravity_units>800.0F ||
        !std::isfinite(p.radius_units)||p.radius_units<=0.0F||p.radius_units>(p.radial_distribution ? 8.0F : 2.0F) ||
        std::ranges::any_of(p.color,[](float v){return !std::isfinite(v)||v<0.0F||v>1.0F;}) ||
        !std::isfinite(p.spread_scale)||p.spread_scale<0||p.spread_scale>2 ||
        !game_api::valid_transient_glow(p.contact_glow) ||
        !std::isfinite(p.contact_glow_seconds)||p.contact_glow_seconds<0 ||
        p.contact_glow_seconds>p.lifetime_seconds ||
        ((p.contact_glow.radius_units>0 || p.contact_glow.light_radius_units>0) && p.contact_glow_seconds<=0)) return reject();
    if(now<action.started_at_seconds || now-action.started_at_seconds>0.25) {
        ++stats_.expired;return ParticleBurstStatus::expired;
    }
    if(generation_ && action.generation!=generation_) {++stats_.stale;return ParticleBurstStatus::stale_generation;}
    for(const auto& value:seen_) if(value && *value==action) {++stats_.duplicate;return ParticleBurstStatus::duplicate;}
    generation_=action.generation;
    // Stable causal seed, never wall clock, render frame or shared random state.
    std::uint64_t seed=14695981039346656037ULL;
    for(const auto v:{action.generation,action.resource_revision,std::uint64_t(action.command_sequence),
        std::uint64_t(action.emitter_entity),std::uint64_t(action.model_index),
        action.canonical_pre_revision,std::bit_cast<std::uint64_t>(action.started_at_seconds)}) {
        seed^=v;seed*=1099511628211ULL;
    }
    if(action.sub_identity) {seed^=action.sub_identity;seed*=1099511628211ULL;}
    const auto random=[&]() {seed^=seed>>12;seed^=seed<<25;seed^=seed>>27;
        return float((seed*2685821657736338717ULL)>>40)/float(0xFFFFFFU);};
    const V ref=std::abs(normal.z)>0.9F ? V{0,1,0}:V{0,0,1};
    V tangent{ref.y*normal.z-ref.z*normal.y,ref.z*normal.x-ref.x*normal.z,ref.x*normal.y-ref.y*normal.x};
    tangent=mul(tangent,1.0F/std::sqrt(dot(tangent,tangent)));
    const V bitangent{normal.y*tangent.z-normal.z*tangent.y,normal.z*tangent.x-normal.x*tangent.z,normal.x*tangent.y-normal.y*tangent.x};
    Burst burst{action,p,add(point,mul(normal,0.125F)),{},now};
    for(std::size_t i=0;i<p.count;++i) {
        V direction=add(mul(normal,0.5F+random()*0.5F),
            add(mul(tangent,(random()*1.5F-0.75F)*p.spread_scale),mul(bitangent,(random()*1.5F-0.75F)*p.spread_scale)));
        if(p.radial_distribution) {
            const float z=random()*2.0F-1.0F, angle=random()*6.28318530718F;
            const float radial=std::sqrt(std::max(0.0F,1.0F-z*z));
            direction={radial*std::cos(angle),radial*std::sin(angle),z};
        }
        direction=mul(direction,1.0F/std::sqrt(dot(direction,direction)));
        burst.velocities[i]=mul(direction,p.minimum_speed_units+random()*(p.maximum_speed_units-p.minimum_speed_units));
    }
    if(bursts_[next_burst_]) ++stats_.evicted;
    bursts_[next_burst_]=burst;next_burst_=(next_burst_+1U)%bursts_.size();
    seen_[next_seen_]=action;next_seen_=(next_seen_+1U)%seen_.size();++stats_.spawned;
    return ParticleBurstStatus::spawned;
}
void ImpactParticlePool::update(double now) noexcept {
    if(!std::isfinite(now)) return;
    now_=std::max(now_,now);
    std::size_t count=0U;
    for(auto& value:bursts_) if(value) {
        const double age=now_-value->start;
        if(age<0.0 || age>=value->profile.lifetime_seconds) {value.reset();++stats_.expired;continue;}
        for(std::size_t i=0;i<value->profile.count;++i) {
            if(count==buffer_.size()) {++stats_.output_limited;break;}
            auto position=add(value->origin,mul(value->velocities[i],static_cast<float>(age)));
            position.z-=static_cast<float>(0.5*value->profile.gravity_units*age*age);
            auto color=value->profile.color;color[3]*=static_cast<float>(1.0-age/value->profile.lifetime_seconds);
            if(value->profile.flicker) {
                const float flicker=0.65F+0.35F*std::abs(std::sin(static_cast<float>(age)*47.0F+static_cast<float>(i)*2.17F));
                for(std::size_t channel=0;channel<3;++channel) color[channel]*=flicker;
            }
            buffer_[count++]={position,value->profile.radius_units,color,value->profile.emissive};
        }
    }
    visible_={buffer_.data(),count};
}
void ImpactParticlePool::append_glows(RenderScene& scene) const noexcept {
    for(const auto& burst:bursts_) if(burst) {
        const auto& p=burst->profile;
        const auto age=now_-burst->start;
        if(age<0 || p.contact_glow_seconds<=0 || age>=p.contact_glow_seconds) continue;
        const auto fade=static_cast<float>(1.0-age/p.contact_glow_seconds);
        auto color=p.color;color[3]*=fade;
        if(p.contact_glow.radius_units>0 && scene.world_flash_count<scene.world_flashes.size())
            scene.world_flashes[scene.world_flash_count++]={burst->origin,p.contact_glow.radius_units,color};
        if(p.contact_glow.light_radius_units>0 && scene.world_effect_light_count<scene.world_effect_lights.size())
            scene.world_effect_lights[scene.world_effect_light_count++]={burst->origin,
                p.contact_glow.light_radius_units,p.contact_glow.light_intensity*fade,{color[0],color[1],color[2]}};
    }
}
void TransientVisuals::reset() noexcept {
    entries_.fill(std::nullopt); visible_={}; contacts_={}; stats_={}; world_.reset();
}
void TransientVisuals::set_collision_world(std::shared_ptr<const collision::CollisionWorldPackage> world) noexcept {
    world_=std::move(world);
}
bool TransientVisuals::spawn(const game_api::LocalShellEjection& cue,double now) noexcept {
    if (!std::isfinite(now)||!std::isfinite(cue.starts_at_seconds)||
        !std::isfinite(cue.expires_at_seconds)||!finite(cue.origin)||!finite(cue.velocity)||
        !std::isfinite(cue.yaw_degrees)||cue.expires_at_seconds<=now||
        now-cue.starts_at_seconds>0.25) {++stats_.shells_dropped;return false;}
    for(const auto& item:entries_) if(item && item->action==cue.action) {
        ++stats_.duplicate_spawns; return false;
    }
    const auto slot=std::find_if(entries_.begin(),entries_.end(),[](const auto& item){return !item;});
    if(slot==entries_.end()) {++stats_.shells_dropped;return false;}
    *slot=Entry{{cue.origin,cue.velocity,cue.yaw_degrees,cue.starts_at_seconds,
        cue.expires_at_seconds,false,cue.model_name},cue.action,cue.starts_at_seconds};
    ++stats_.shells_created;
    return true;
}
void TransientVisuals::update(double now) noexcept {
    if(!std::isfinite(now)) return;
    std::size_t count=0, contact_count=0;
    contacts_={};
    const collision::CollisionWorldQuery query{world_};
    for(auto& item:entries_) {
        if(!item) continue;
        auto& shell=item->shell;
        if(now>=shell.expires_at_seconds || now<item->simulated_at ||
            now-item->simulated_at>0.25) {item.reset();++stats_.shells_expired;continue;}
        constexpr double step=1.0/120.0;
        const auto steps=std::min(30,static_cast<int>(std::floor((now-item->simulated_at)/step+1e-8)));
        for(int n=0;n<steps && !shell.at_rest;++n) {
            V next=add(shell.position,mul(shell.velocity,static_cast<float>(step)));
            next.z-=static_cast<float>(0.5*800.0*step*step);
            shell.velocity.z-=static_cast<float>(800.0*step);
            if(world_) {
                collision::CollisionTraceRequest request;
                request.start=shell.position;request.end=next;
                const auto trace=query.trace_line(request,scratch_);
                if(!trace) {++stats_.trace_failures;item.reset();break;}
                if(trace.result->start_solid || trace.result->all_solid) {
                    shell.at_rest=true;shell.velocity={};break;
                }
                // A BSP entry exactly at the segment endpoint can report
                // fraction 1 with a valid collision plane. The plane, not
                // fraction < 1, is the trace API's hit predicate.
                if(trace.result->collision_plane) {
                    ++stats_.collision_contacts;
                    const auto normal=trace.result->collision_plane->normal;
                    const auto inward=dot(shell.velocity,normal);
                    const auto contact_at=item->simulated_at+(n+1)*step;
                    // Multiple sweeps against the same plane are one physical
                    // contact, not a new bounce or a new sound opportunity.
                    if(inward<0.0F && (item->last_contact_at<0.0 ||
                        contact_at-item->last_contact_at>=0.045)) {
                        item->last_contact_at=contact_at;
                        ++item->contact_ordinal;
                        if(contact_count<contact_buffer_.size()) {
                            contact_buffer_[contact_count++]={item->action,item->contact_ordinal,
                                trace.result->end_position,-inward,contact_at};
                            ++stats_.contact_events;
                        } else ++stats_.contact_events_dropped;
                    } else ++stats_.contact_repeat_suppressed;
                    shell.position=add(trace.result->end_position,mul(normal,0.125F));
                    shell.velocity=mul(sub(shell.velocity,mul(normal,1.28F*inward)),0.62F);
                    if(dot(shell.velocity,shell.velocity)<144.0F) {shell.velocity={};shell.at_rest=true;}
                    continue;
                }
            }
            shell.position=next;
            if(!finite(shell.position)||!finite(shell.velocity)) {item.reset();++stats_.trace_failures;break;}
        }
        if(!item) continue;
        item->simulated_at+=steps*step;
        buffer_[count++]=shell;
    }
    visible_={buffer_.data(),count};contacts_={contact_buffer_.data(),contact_count};stats_.active=count;
}
TransientVisualStatistics TransientVisuals::statistics() const noexcept {return stats_;}
} // namespace hlclient::renderer

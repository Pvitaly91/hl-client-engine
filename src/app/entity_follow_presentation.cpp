#include <hlclient/app/entity_follow_presentation.hpp>
#include <hlclient/entity_render/entity_render_types.hpp>
#include <algorithm>
#include <cmath>
namespace hlclient::app {
namespace {
using V=assets::AssetVector3;
bool finite(V v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float distance(V a,V b) {return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
V origin(const client::RuntimePacketEntityObservation& e) {
    return {static_cast<float>(*e.origin.x),static_cast<float>(*e.origin.y),static_cast<float>(*e.origin.z)};
}
bool valid(const game_api::EntityTrailProfile& p) {
    return std::isfinite(p.spacing_units) && p.spacing_units>=2 && p.spacing_units<=64 &&
        std::isfinite(p.initial_diameter) && p.initial_diameter>0 &&
        std::isfinite(p.final_diameter) && p.final_diameter>=p.initial_diameter && p.final_diameter<=64 &&
        std::isfinite(p.lifetime_seconds) && p.lifetime_seconds>=.1F && p.lifetime_seconds<=4 &&
        std::isfinite(p.rise_units_per_second) && p.rise_units_per_second>=0 && p.rise_units_per_second<=32 &&
        std::isfinite(p.head_glow_radius) && p.head_glow_radius>=0 && p.head_glow_radius<=8 &&
        std::isfinite(p.head_axis_offset) && std::abs(p.head_axis_offset)<=64 &&
        std::isfinite(p.smoke_opacity) && p.smoke_opacity>=0 && p.smoke_opacity<=1 &&
        std::ranges::find(p.head_sprite,'\0')!=p.head_sprite.end() &&
        std::ranges::all_of(p.head_glow_color,[](float v){return std::isfinite(v)&&v>=0&&v<=1;});
}
}
void EntityFollowPresentation::reset(std::uint64_t generation) noexcept {
    generation_=generation;next_life_=ordinal_=next_seen_=segment_count_=head_count_=0;
    lives_.clear();
    // Do not materialize the complete bounded trail pool as a stack temporary.
    // This reset also runs below the normal Win32 live application's frame.
    for(auto& trail:trails_) trail=Trail{};
    seen_={};high_={};stats_={};
}
std::uint64_t EntityFollowPresentation::lifetime(std::uint32_t entity) const noexcept {
    const auto at=lives_.find(entity);return at==lives_.end() ? 0 : at->second.id;
}
std::uint64_t EntityFollowPresentation::lifetime(std::uint32_t entity,std::uint64_t event_record_ordinal) const noexcept {
    const auto at=lives_.find(entity);
    return !event_record_ordinal || at==lives_.end() || at->second.born_record_ordinal>event_record_ordinal ? 0 : at->second.id;
}
void EntityFollowPresentation::observe(const client::RuntimeClientObservationState& s) {
    if(s.generation!=generation_) reset(s.generation);
    if(!s.entity_metadata.source || s.entity_metadata.source->record_ordinal<=ordinal_ ||
        s.entity_metadata.freshness!=client::RuntimeObservationFreshness::observed_in_record || s.packet_entities.size()>4096) return;
    std::map<std::uint32_t,Life> next;
    auto next_id=next_life_;
    for(const auto& e:s.packet_entities) if(e.entity_number && e.model_index && *e.model_index) {
        const auto old=lives_.find(e.entity_number);
        next.emplace(e.entity_number,old!=lives_.end() && old->second.model==e.model_index ?
            old->second : Life{++next_id,e.model_index,s.entity_metadata.source->record_ordinal});
    }
    lives_=std::move(next);next_life_=next_id;ordinal_=s.entity_metadata.source->record_ordinal;
    for(auto& t:trails_) if(t.used && t.attached && lifetime(t.entity)!=t.lifetime) {
        t.attached=false;++stats_.detached;
    }
}
void EntityFollowPresentation::consume(const client::CommittedTemporaryEffect& e,double now,
    std::optional<game_api::EntityTrailProfile> profile,std::uint16_t head_sprite_slot) noexcept {
    ++stats_.received;
    const auto entity=static_cast<std::uint32_t>(e.entity_attachment&0xfffU);
    if(e.kind!=client::TemporaryEffectKind::beam_follow || e.generation!=generation_ ||
        !e.record || !e.record_ordinal || !std::isfinite(now) || !std::isfinite(e.received_at_seconds) ||
        e.received_at_seconds<0 || e.received_at_seconds>now+.001 || now-e.received_at_seconds>.25 ||
        !entity || (e.entity_attachment>>12)!=0 || !e.sprite_index || e.sprite_index>1023 ||
        !e.life_tenths || !e.width_tenths || !e.brightness || head_sprite_slot>1023 ||
        (profile && !valid(*profile))) {++stats_.invalid;return;}
    const std::array<std::uint64_t,3> key{e.record_ordinal,e.message_bit_offset,e.entry_ordinal};
    if(key<=high_ || std::ranges::find(seen_,key)!=seen_.end()) {++stats_.duplicate;return;}
    high_=key;seen_[next_seen_++%seen_.size()]=key;
    const auto id=lifetime(entity);
    if(!id) {++stats_.invalid;return;} // no guessed occupant or late reattachment
    for(const auto& t:trails_) if(t.used && t.attached && t.entity==entity && t.lifetime==id) {
        ++stats_.duplicate;return;
    }
    const auto free=std::ranges::find(trails_,false,&Trail::used);
    if(free==trails_.end()) {++stats_.capacity;return;}
    *free={};free->used=free->attached=true;free->entity=entity;free->lifetime=id;free->event=e;
    free->profile=profile;
    free->head_sprite_slot=profile ? head_sprite_slot : 0;
    ++stats_.accepted;
}
void EntityFollowPresentation::present(std::span<const client::RuntimePacketEntityObservation> entities,
    renderer::RenderScene& scene,double now) noexcept {
    segment_count_=head_count_=0;
    if(!std::isfinite(now) || now<0 || entities.size()>4096) return;
    // EF_LIGHT is a generic bit. No local ignition time or game-specific ID.
    for(const auto& e:entities) if((e.effects.value_or(0)&64U) && !(e.effects.value_or(0)&128U) &&
        e.origin.complete() && lifetime(e.entity_number) && scene.world_effect_light_count<scene.world_effect_lights.size()) {
        renderer::RenderPointLight light{origin(e),160,1,{1,.65F,.3F}};
        if(renderer::valid_render_point_light(light)) {
            scene.world_effect_lights[scene.world_effect_light_count++]=light;++stats_.light_submitted;
        }
    }
    for(auto& t:trails_) if(t.used) {
        const double life=std::min(t.event.life_tenths*.1,
            t.profile ? static_cast<double>(t.profile->lifetime_seconds) : 25.5);
        std::size_t expired=0;while(expired<t.count && now-t.points[expired].time>=life) ++expired;
        if(expired) {std::move(t.points.begin()+expired,t.points.begin()+t.count,t.points.begin());t.count-=expired;}
        if(t.attached && lifetime(t.entity)!=t.lifetime) {t.attached=false;++stats_.detached;}
        const auto entity=std::ranges::find(entities,t.entity,&client::RuntimePacketEntityObservation::entity_number);
        if(t.attached && entity!=entities.end() && entity->origin.complete() && !(entity->effects.value_or(0)&128U) && now>t.sampled_at) {
            const auto p=origin(*entity);
            if(finite(p)) {
              if(t.profile) {
                const auto append=[&](V at,double time) {
                    if(t.count==t.points.size()) {std::move(t.points.begin()+1,t.points.end(),t.points.begin());--t.count;}
                    // Fixed seed per lifetime/sample; never camera or shared random state.
                    auto seed=++t.sample_ordinal*1664525U+t.lifetime*1013904223U+t.entity;
                    const auto jitter=[&]() {seed=seed*1664525U+1013904223U;return (float((seed>>16)&255U)/255-.5F)*1.2F;};
                    at.x+=jitter();at.y+=jitter();at.z+=jitter();
                    t.points[t.count++]={at,time};++stats_.sampled;
                };
                const auto length=t.anchored ? distance(p,t.last_origin) : 0;
                if((entity->effects.value_or(0)&32U) || (t.anchored &&
                    (length>512 || (length>.1F && now-t.sampled_at>.25)))) {
                    t.count=0;t.anchored=false;t.spacing_carry=0;++stats_.discontinuity;
                } else if(!t.anchored) {
                    append(p,now);t.anchored=true;t.spacing_carry=0;
                } else if(length>.0001F) {
                    const auto spacing=t.profile->spacing_units;
                    for(float along=spacing-t.spacing_carry;along<=length;along+=spacing) {
                        const auto k=along/length;
                        append({t.last_origin.x+(p.x-t.last_origin.x)*k,
                            t.last_origin.y+(p.y-t.last_origin.y)*k,t.last_origin.z+(p.z-t.last_origin.z)*k},
                            t.sampled_at+(now-t.sampled_at)*k);
                    }
                    t.spacing_carry=std::fmod(t.spacing_carry+length,spacing);
                }
                t.last_origin=p;
              } else {
                if(t.count && (distance(p,t.points[t.count-1].origin)>512 || (entity->effects.value_or(0)&32U))) {
                    t.count=0;++stats_.discontinuity;
                }
                if(!t.count || (now-t.points[t.count-1].time>=.02 && distance(p,t.points[t.count-1].origin)>.1F)) {
                    if(t.count==64) {std::move(t.points.begin()+1,t.points.begin()+t.count,t.points.begin());--t.count;}
                    t.points[t.count++]={p,now};++stats_.sampled;
                }
              }
            } else if(t.profile) {t.anchored=false;t.spacing_carry=0;}
            t.sampled_at=now;
        } else if(t.profile && (entity==entities.end() || !entity->origin.complete() || (entity->effects.value_or(0)&128U))) {
            // Returning from visibility absence must not draw a guessed bridge.
            t.anchored=false;t.spacing_carry=0;
        }
        if(!t.attached && !t.count) {t={};continue;}
        if(t.profile) {
            for(std::size_t i=0;i<t.count && segment_count_<segments_.size();++i) {
                const auto age=static_cast<float>(std::clamp(now-t.points[i].time,0.0,life));
                const auto fraction=age/static_cast<float>(life);if(fraction>=1) continue;
                auto position=t.points[i].origin;position.z+=t.profile->rise_units_per_second*age;
                const auto width=t.profile->initial_diameter+(t.profile->final_diameter-t.profile->initial_diameter)*fraction;
                const auto opacity=(1-fraction)*t.profile->smoke_opacity*t.event.brightness/255.0F;
                segments_[segment_count_++]={position,position,t.event.sprite_index,width,opacity,
                    {t.event.color[0]/255.0F,t.event.color[1]/255.0F,t.event.color[2]/255.0F},true,fraction};
            }
            if(t.attached && entity!=entities.end() && entity->origin.complete() && entity->angles.complete() &&
                (entity->effects.value_or(0)&64U) && !(entity->effects.value_or(0)&128U) &&
                finite(origin(*entity)) && t.profile->head_glow_radius>0 && t.head_sprite_slot && head_count_<heads_.size()) {
                // Exactly the ordinary Studio entity transform, including its
                // source pitch sign. Never infer direction from camera/velocity.
                const entity_render::EntityRenderTransform transform{origin(*entity),
                    {static_cast<float>(*entity->angles.z),-static_cast<float>(*entity->angles.x),
                     static_cast<float>(*entity->angles.y)},1};
                if(entity_render::finite_entity_render_transform(transform)) {
                    const auto m=entity_render::entity_render_model_matrix(transform);
                    const auto offset=t.profile->head_axis_offset;
                    const V p{m[12]+m[0]*offset,m[13]+m[1]*offset,m[14]+m[2]*offset};
                    const auto& color=t.profile->head_glow_color;
                    heads_[head_count_++]={p,p,t.head_sprite_slot,t.profile->head_glow_radius*2,color[3],
                        {color[0],color[1],color[2]},true,0};
                }
            }
            continue;
        }
        for(std::size_t i=1;i<t.count && segment_count_<segments_.size();++i) {
            const auto fade=static_cast<float>(std::clamp(1-(now-t.points[i-1].time)/life,0.0,1.0));
            segments_[segment_count_++]={t.points[i-1].origin,t.points[i].origin,t.event.sprite_index,
                t.event.width_tenths*.1F,fade*t.event.brightness/255.0F,
                {t.event.color[0]/255.0F,t.event.color[1]/255.0F,t.event.color[2]/255.0F}};
        }
        // Retained history deliberately samples at 20 ms; the visible end must
        // nevertheless follow the current presented entity on faster frames.
        // This transient tip adds no history point, physics or event delivery.
        if(t.attached && t.count && now>=t.sampled_at && entity!=entities.end() &&
            entity->model_index && *entity->model_index && entity->origin.complete() &&
            !(entity->effects.value_or(0)&(128U|32U)) && segment_count_<segments_.size()) {
            const auto p=origin(*entity);const auto& previous=t.points[t.count-1];
            const auto length=distance(p,previous.origin);
            if(finite(p) && length>.0001F && length<=512) {
                const auto fade=static_cast<float>(std::clamp(1-(now-previous.time)/life,0.0,1.0));
                segments_[segment_count_++]={previous.origin,p,t.event.sprite_index,
                    t.event.width_tenths*.1F,fade*t.event.brightness/255.0F,
                    {t.event.color[0]/255.0F,t.event.color[1]/255.0F,t.event.color[2]/255.0F}};
            }
        }
    }
}
}

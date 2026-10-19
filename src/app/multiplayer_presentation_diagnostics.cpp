#include <hlclient/app/multiplayer_presentation_diagnostics.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace hlclient::app {
namespace {
using V=std::array<float,3>; using M=std::array<float,16>;
V vec(assets::AssetVector3 v) noexcept { return {v.x,v.y,v.z}; }
assets::AssetVector3 asset(V v) noexcept { return {v[0],v[1],v[2]}; }
std::optional<V> unit(V v) noexcept {
    double n{};for(float x:v){if(!std::isfinite(x))return {};n+=double(x)*x;}
    if(n<1e-12)return {};n=std::sqrt(n);for(auto&x:v)x=static_cast<float>(x/n);return v;
}
M multiply(const M&a,const M&b) noexcept {
    M out{};for(std::size_t c=0;c<4;++c)for(std::size_t r=0;r<4;++r)
        for(std::size_t k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];return out;
}
}
std::optional<double> diagnostic_ray_box(const V&o,const V&d,const M&m,const V&lo,const V&hi) noexcept {
    for(float x:m)if(!std::isfinite(x))return {};
    // Inverse of affine 3x3: unlike transpose this also supports uniform scale.
    const double a=m[0],b=m[4],c=m[8],e=m[1],f=m[5],g=m[9],i=m[2],j=m[6],k=m[10];
    const double det=a*(f*k-g*j)-b*(e*k-g*i)+c*(e*j-f*i);
    if(!std::isfinite(det)||std::abs(det)<1e-10)return {};
    const std::array<std::array<double,3>,3> inv{{{(f*k-g*j)/det,(c*j-b*k)/det,(b*g-c*f)/det},
        {(g*i-e*k)/det,(a*k-c*i)/det,(c*e-a*g)/det},
        {(e*j-f*i)/det,(b*i-a*j)/det,(a*f-b*e)/det}}};
    double near=0,far=8192;
    for(std::size_t axis=0;axis<3;++axis) {
        if(!std::isfinite(o[axis])||!std::isfinite(d[axis])||!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>hi[axis])return {};
        double p{},v{};for(std::size_t n=0;n<3;++n){p+=inv[axis][n]*(o[n]-m[12+n]);v+=inv[axis][n]*d[n];}
        if(std::abs(v)<1e-10){if(p<lo[axis]||p>hi[axis])return std::numeric_limits<double>::infinity();continue;}
        double x=(lo[axis]-p)/v,y=(hi[axis]-p)/v;if(x>y)std::swap(x,y);
        near=std::max(near,x);far=std::min(far,y);if(near>far)return std::numeric_limits<double>::infinity();
    }
    return near;
}
void MultiplayerPresentationDiagnostics::invalidate() noexcept { entities_.reset();completed_ns_=0;frame_=0; }
void MultiplayerPresentationDiagnostics::completed_frame(const renderer::RenderScene& scene,
    std::uint64_t frame,std::uint64_t network,std::uint64_t map,std::uint64_t ns,
    std::uint32_t width,std::uint32_t height) noexcept {
    if(!enabled())return;
    const auto camera=renderer::apply_screen_effects(scene.camera,scene.screen_effects);
    if(!camera){invalidate();return;}
    camera_=*camera;entities_.reset();
    // A large scene is unavailable, not permission to retain an unbounded
    // previous pose snapshot. Imported immutable assets are shared, not copied.
    if(scene.dynamic_entities && scene.dynamic_entities->frame &&
       scene.dynamic_entities->frame->studio_instances().size()<=128U &&
       scene.dynamic_entities->frame->studio_poses().size()<=128U &&
       scene.dynamic_entities->frame->statistics().total_bone_matrix_count<=8192U)
        entities_=scene.dynamic_entities;
    frame_=frame;network_=network;map_=map;
    completed_ns_=ns;width_=width;height_=height;
    // Ordinary Studio actors (including non-player entities) also have an
    // observable pose lane while nobody fires. A scene sample time is not an
    // entity animation/rewind clock; absent per-entity clocks remain absent.
    if(entities_ && entities_->frame && entities_->package) {
        std::size_t count{};
        for(const auto& instance:entities_->frame->studio_instances()) {
            if(instance.subinstance_ordinal)continue;
            if(++count>32)continue;
            core::MultiplayerDiagnosticEvent event;
            event.identity.producer=core::MultiplayerDiagnosticProducer::presentation;
            event.identity.producer_event_id=++event_id_;event.identity.network_generation=network;
            event.identity.map_generation=map;event.identity.actor_entity=instance.entity_number;
            event.identity.render_frame=frame;event.local_steady_ns=ns;
            event.provenance=core::MultiplayerProvenance::runtime_observed;
            event.knowledge=core::MultiplayerKnowledge::local_proxy;
            event.edge=core::MultiplayerEdgeConfidence::exact;
            if(instance.studio_asset_index<entities_->package->studio_assets().size())
                if(const auto& asset=entities_->package->studio_assets()[instance.studio_asset_index])
                    event.identity.actor_model_revision=asset->source_identity().revision;
            core::MultiplayerPresentationPayload p;p.phase=core::MultiplayerPresentationPhase::pose;
            p.origin=vec(instance.transform.origin);p.angles=vec(instance.transform.rotation_degrees);
            p.sample_seconds=entities_->frame->interpolation().sample_time_seconds;
            if(entities_->frame->interpolation().profile==entity_render::EntityRenderInterpolationProfile::public_runtime_server_seconds_v1) {
                event.server_seconds=p.sample_seconds;event.server_time_domain=core::MultiplayerTimeDomain::server_simulation_seconds;
            }
            p.visible=instance.visibility_status==entity_render::RuntimeEntityVisibilityStatus::visible;
            event.payload=p;(void)sink_.observe(event);
        }
        const auto omitted=count>32?static_cast<std::uint32_t>(count-32):0U;
        if(omitted!=last_pose_omissions_) {
            core::MultiplayerDiagnosticEvent event;event.identity.producer=core::MultiplayerDiagnosticProducer::presentation;
            event.identity.producer_event_id=++event_id_;event.identity.network_generation=network;event.identity.map_generation=map;
            event.identity.render_frame=frame;event.local_steady_ns=ns;event.provenance=core::MultiplayerProvenance::runtime_observed;
            core::MultiplayerLifecyclePayload p;p.omitted_objects=omitted;event.payload=p;(void)sink_.observe(event);
            last_pose_omissions_=omitted;
        }
    }
}
bool MultiplayerPresentationDiagnostics::observe(const core::MultiplayerDiagnosticEvent& event) noexcept {
    if(!enabled())return false;
    const bool recorded=sink_.observe(event);
    if(const auto* action=std::get_if<core::MultiplayerActionPayload>(&event.payload);
        recorded && action && (action->phase==core::MultiplayerActionPhase::accepted || action->phase==core::MultiplayerActionPhase::begin))
        query_action(event,*action);
    return recorded;
}
void MultiplayerPresentationDiagnostics::query_action(const core::MultiplayerDiagnosticEvent& source,
    const core::MultiplayerActionPayload& action) noexcept {
    using namespace core;
    if(!action.origin||!action.direction || !completed_ns_ || source.local_steady_ns<completed_ns_ ||
        source.identity.network_generation!=network_ || source.identity.map_generation!=map_)return;
    for(const auto value:*action.origin)if(!std::isfinite(value))return;
    for(const auto value:vec(camera_.position))if(!std::isfinite(value))return;
    const auto command_direction=unit(*action.direction);if(!command_direction)return;
    const auto view_direction=unit({camera_.target.x-camera_.position.x,camera_.target.y-camera_.position.y,camera_.target.z-camera_.position.z});
    if(!view_direction)return;
    const double age_ms=double(source.local_steady_ns-completed_ns_)/1e6;
    const auto query=++query_id_;
    for(std::size_t ray=0;ray<2;++ray) {
        MultiplayerDiagnosticEvent out;out.identity=source.identity;out.identity.producer=MultiplayerDiagnosticProducer::presentation;
        out.identity.producer_event_id=++event_id_;out.identity.source_query_id=query;out.identity.render_frame=frame_;
        out.local_steady_ns=multiplayer_diagnostic_now_ns();out.edge=MultiplayerEdgeConfidence::exact;
        out.provenance=MultiplayerProvenance::derived_estimate;out.knowledge=MultiplayerKnowledge::local_proxy;
        MultiplayerGeometryPayload p;p.kind=ray==0?MultiplayerGeometryKind::last_presented_view:MultiplayerGeometryKind::command_query;
        p.origin=ray==0?vec(camera_.position):*action.origin;p.direction=ray==0?*view_direction:*command_direction;
        if(ray==0){p.camera_up=vec(camera_.up);p.vertical_fov_radians=camera_.vertical_field_of_view_radians;p.near_plane=camera_.near_plane;p.far_plane=camera_.far_plane;}
        p.proxy_age_ms=age_ms;p.viewport_width=width_;p.viewport_height=height_;p.framebuffer_width=width_;p.framebuffer_height=height_;
        // Stale/generation-invalid proxies remain unknown, never misses.
        if(age_ms<=250 && entities_ && entities_->frame && entities_->package) {
            const auto& package=*entities_->package;const auto& frame=*entities_->frame;
            // Resource bindings are not scene/pose revisions. The exact owning
            // rendered-frame ID stays above; unavailable revisions stay null.
            out.identity.scene_revision.reset();out.identity.pose_revision.reset();
            double nearest=8192;std::size_t inspected{},boxes{},source_probes{};bool exhausted{},incomplete{},queried_box{};
            for(const auto& inst:frame.studio_instances()) {
                if(inst.subinstance_ordinal || inst.visibility_status!=entity_render::RuntimeEntityVisibilityStatus::visible)continue;
                if(++inspected>32){exhausted=true;break;}
                if(inst.studio_asset_index>=package.studio_assets().size()||inst.pose_index>=frame.studio_poses().size()){incomplete=true;continue;}
                const auto& render_asset=package.studio_assets()[inst.studio_asset_index];if(!render_asset){incomplete=true;continue;}
                const auto identity=render_asset->source_identity();const assets::ModelAsset* model{};
                if(package.asset_library())for(const auto& record:package.asset_library()->records()) {
                    if(++source_probes>4096){exhausted=true;break;}
                    if(record.resource_id()==identity.resource_id && record.resource_revision()==identity.revision){model=record.model_asset().get();break;}
                }
                if(!model && !exhausted)for(const auto& record:package.presentation_studio_sources()) {
                    if(++source_probes>4096){exhausted=true;break;}
                    if(record.identity==identity){model=record.model.get();break;}
                }
                if(exhausted)break;
                if(!model||!model->skeletal_data||model->skeletal_data->hitboxes.empty()){incomplete=true;continue;}
                const auto& pose=frame.studio_poses()[inst.pose_index];const auto transform=entity_render::entity_render_model_matrix(inst.transform);
                for(const auto& box:model->skeletal_data->hitboxes) {
                    if(++boxes>1024){exhausted=true;break;}
                    if(box.bone_index>=pose.bone_matrices.size()){incomplete=true;continue;}
                    const auto hit=diagnostic_ray_box(*p.origin,*p.direction,multiply(transform,pose.bone_matrices[box.bone_index]),vec(box.local_bounds.minimum),vec(box.local_bounds.maximum));
                    if(!hit){incomplete=true;continue;}queried_box=true;
                    if(*hit<nearest){nearest=*hit;p.proxy_actor=inst.entity_number;p.hitbox_group=static_cast<std::uint32_t>(std::max(0,box.group));p.proxy_model_revision=identity.revision;}
                }
                if(exhausted)break;
            }
            p.query_budget_exhausted=exhausted;
            p.query_incomplete=incomplete||exhausted;
            if(p.proxy_actor || (queried_box && !incomplete && !exhausted)) {
                p.intersects=p.proxy_actor.has_value();p.exact_model_pose=true;
                if(p.proxy_actor)p.fraction=nearest/8192;
            }
            const V end{(*p.origin)[0]+(*p.direction)[0]*8192,(*p.origin)[1]+(*p.direction)[1]*8192,(*p.origin)[2]+(*p.direction)[2]*8192};
            p.endpoint=end;
            if(world_query_) {
                // This read-only world query observes current world/brush
                // presentation, not a reconstructed last-swap/server rewind.
                p.world_query_steady_ns=multiplayer_diagnostic_now_ns();
                const auto trace=world_query_->trace(asset(*p.origin),asset(end));
                if(trace.valid && !trace.unsupported && std::isfinite(trace.point.x) &&
                    std::isfinite(trace.point.y) && std::isfinite(trace.point.z)) {
                    p.startsolid=trace.start_solid;p.allsolid=trace.all_solid;
                    if(trace.hit) {
                        const auto point=vec(trace.point);double distance{};for(std::size_t axis=0;axis<3;++axis){const double v=point[axis]-(*p.origin)[axis];distance+=v*v;}
                        p.world_blocked=std::sqrt(distance)+.05<nearest;
                        if(trace.surface_attachment)p.blocker_entity=trace.surface_attachment->entity;
                    } else p.world_blocked=false;
                }
            }
        }
        out.payload=p;(void)sink_.observe(out);
        if(p.exact_model_pose) {
            // Same shown MDL pose queried above, NOT a reconstruction of a
            // server rewind pose or proof of server collision/damage.
            out.identity.producer_event_id=++event_id_;out.provenance=MultiplayerProvenance::source_model;
            p.kind=MultiplayerGeometryKind::source_model_query;out.payload=p;(void)sink_.observe(out);
        }
    }
}
}

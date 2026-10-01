#include <hlclient/app/world_impact_presentation.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace hlclient::app {
namespace {
using V=assets::AssetVector3;
struct P { double x{},y{}; };
V add(V a,V b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,double s) noexcept {return {static_cast<float>(a.x*s),static_cast<float>(a.y*s),static_cast<float>(a.z*s)};}
double dot(V a,V b) noexcept {return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
V cross(V a,V b) noexcept {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double length(V a) noexcept {return std::sqrt(dot(a,a));}
bool finite(V a) noexcept {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
double edge(P a,P b,P p) noexcept {return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
P intersect(P a,P b,P e0,P e1) noexcept {
    const auto da=edge(e0,e1,a),db=edge(e0,e1,b);
    const auto den=da-db;
    const auto t=std::abs(den)>1e-12 ? std::clamp(da/den,0.0,1.0) : 0.0;
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
}
std::vector<P> clip_triangle(std::vector<P> polygon,std::array<P,3> tri) {
    const auto orientation=edge(tri[0],tri[1],tri[2])>=0 ? 1.0 : -1.0;
    for(std::size_t k=0;k<3 && !polygon.empty();++k) {
        std::vector<P> next;next.reserve(8);
        const auto e0=tri[k],e1=tri[(k+1)%3];
        auto previous=polygon.back();
        auto previous_inside=orientation*edge(e0,e1,previous)>=-1e-7;
        for(const auto current:polygon) {
            const auto inside=orientation*edge(e0,e1,current)>=-1e-7;
            if(inside!=previous_inside) next.push_back(intersect(previous,current,e0,e1));
            if(inside) next.push_back(current);
            previous=current;previous_inside=inside;
        }
        polygon=std::move(next);
    }
    return polygon;
}
bool contains(V point,V a,V b,V c,V normal) noexcept {
    const auto n=cross(sub(b,a),sub(c,a));
    if(dot(n,normal)<=0 || length(n)<1e-7) return false;
    if(std::abs(dot(sub(point,a),normal))>0.25) return false;
    constexpr double tolerance=-0.08;
    return dot(cross(sub(b,a),sub(point,a)),n)>=tolerance*dot(n,n) &&
        dot(cross(sub(c,b),sub(point,b)),n)>=tolerance*dot(n,n) &&
        dot(cross(sub(a,c),sub(point,c)),n)>=tolerance*dot(n,n);
}
bool allowed_surface(const world_render::WorldRenderPackage& package,
    const world_render::WorldRenderSurfaceRange& range) noexcept {
    const auto& world=package.textured_world().world;
    if(range.source_world_surface_index>=world.surfaces.size() ||
        range.render_material_index>=package.materials().size() ||
        range.alpha_mode!=assets::WorldTextureAlphaMode::opaque ||
        world.surfaces[range.source_world_surface_index].special_surface ||
        package.materials()[range.render_material_index].special_surface) return false;
    const auto index=world.surfaces[range.source_world_surface_index].material_index;
    if(index>=world.materials.size() || !world.materials[index].texture_name) return false;
    const auto& name=*world.materials[index].texture_name;
    return !name.empty() && !name.starts_with("sky") && name.front()!='!';
}
} // namespace

void WorldImpactPresentation::reset() noexcept {
    for(auto& entry:entries_) entry.reset();
    next_entry_=0; ++revision_; dirty_=true; cached_.reset();
}
std::size_t WorldImpactPresentation::active() const noexcept {
    return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),
        [](const auto& e){return e.has_value();}));
}
std::size_t WorldImpactPresentation::pending() const noexcept {
    return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),
        [](const auto& e){return e && !e->published;}));
}
WorldImpactResult WorldImpactPresentation::submit(
    const game_api::LocalWorldImpactRequest& request,
    std::shared_ptr<const collision::CollisionWorldPackage> world,
    const world_render::WorldRenderPackage* package,
    std::shared_ptr<const goldsrc::collision::BrushCollisionScene> brushes,
    bool decal_resource_ready,double now) noexcept {
    try {
    const auto distance=length(request.direction);
    if(!finite(request.origin)||!finite(request.direction)||!std::isfinite(now)||
        !std::isfinite(request.expires_at_seconds)||!std::isfinite(request.maximum_distance_units)||
        !std::isfinite(request.decal_half_size_units)||
        !std::isfinite(request.decal_delay_seconds)||distance<1e-6 ||
        request.maximum_distance_units<=0 || request.maximum_distance_units>8192 ||
        request.decal_half_size_units<=0 || request.decal_half_size_units>16 ||
        request.decal_delay_seconds<0 || request.decal_delay_seconds>1.0)
        return {WorldImpactStatus::invalid_request,{}};
    if(now>request.expires_at_seconds || now<request.action.started_at_seconds)
        return {WorldImpactStatus::expired,{}};
    if(!world||!package) return {WorldImpactStatus::collision_unavailable,{}};
    const auto direction=mul(request.direction,1.0/distance);
    const auto end=add(request.origin,mul(direction,request.maximum_distance_units));
    if(!finite(end)) return {WorldImpactStatus::invalid_request,{}};
    collision::CollisionTraceResult hit;
    if(brushes) {
        goldsrc::collision::BrushCollisionSceneTraceRequest trace;
        trace.start=request.origin;trace.end=end;
        const auto result=goldsrc::collision::BrushCollisionSceneQuery{std::move(brushes)}
            .trace_hull(trace,scratch_);
        if(!result) return {WorldImpactStatus::trace_failed,{}};
        if(result.result->scene_hit &&
            result.result->scene_hit->kind!=collision::CollisionTraceHitKind::world)
            return {WorldImpactStatus::unsupported_blocker,{}};
        hit=result.result->trace;
    } else {
        collision::CollisionTraceRequest trace;
        trace.start=request.origin;trace.end=end;
        const auto result=collision::CollisionWorldQuery{std::move(world)}
            .trace_line(trace,scratch_);
        if(!result) return {WorldImpactStatus::trace_failed,{}};
        hit=*result.result;
    }
    if(hit.start_solid||hit.all_solid) return {WorldImpactStatus::start_solid,{}};
    if(!hit.collision_plane || !hit.hit) return {WorldImpactStatus::miss,{}};
    if(hit.hit->kind!=collision::CollisionTraceHitKind::world || hit.in_liquid)
        return {WorldImpactStatus::unsupported_blocker,{}};
    const auto point=hit.end_position;
    const auto normal=hit.collision_plane->normal;
    if(!finite(point)||!finite(normal)||length(normal)<0.99||length(normal)>1.01)
        return {WorldImpactStatus::trace_failed,{}};
    const auto vertices=package->vertices();
    const auto indices=package->indices();
    const auto ranges=package->surface_ranges();
    std::optional<std::size_t> selected;
    for(std::size_t i=0;i<ranges.size();++i) {
        const auto& range=ranges[i];
        if(point.x<range.bounds.minimum.x-0.25F||point.x>range.bounds.maximum.x+0.25F||
            point.y<range.bounds.minimum.y-0.25F||point.y>range.bounds.maximum.y+0.25F||
            point.z<range.bounds.minimum.z-0.25F||point.z>range.bounds.maximum.z+0.25F||
            range.first_index>indices.size()||range.index_count>indices.size()-range.first_index) continue;
        for(std::size_t j=range.first_index;j+2<static_cast<std::size_t>(range.first_index)+range.index_count;j+=3) {
            if(indices[j]>=vertices.size()||indices[j+1]>=vertices.size()||indices[j+2]>=vertices.size()) continue;
            if(!contains(point,vertices[indices[j]].position,vertices[indices[j+1]].position,
                vertices[indices[j+2]].position,normal)) continue;
            if(selected && *selected!=i) return {WorldImpactStatus::surface_ambiguous,{}};
            selected=i;
        }
    }
    if(!selected) return {WorldImpactStatus::surface_unmapped,{}};
    const auto& range=ranges[*selected];
    if(!allowed_surface(*package,range)) return {WorldImpactStatus::surface_unsupported,{}};
    const auto& source=package->textured_world().world;
    const auto material_index=source.surfaces[range.source_world_surface_index].material_index;
    const auto& texture=*source.materials[material_index].texture_name;
    if(range.source_world_surface_index>std::numeric_limits<std::uint32_t>::max() ||
        texture.size()>=game_api::LocalWorldSurfaceHit{}.texture_name.size())
        return {WorldImpactStatus::surface_unsupported,{}};
    game_api::LocalWorldSurfaceHit surface;
    surface.source_surface_index=static_cast<std::uint32_t>(range.source_world_surface_index);
    surface.source_material_index=material_index;
    std::copy(texture.begin(),texture.end(),surface.texture_name.begin());
    if(!decal_resource_ready) return {WorldImpactStatus::hit,point,surface};
    const auto ref=std::abs(normal.z)>0.9F ? V{0,1,0}:V{0,0,1};
    auto tangent=cross(ref,normal);
    tangent=mul(tangent,1.0/length(tangent));
    const auto bitangent=cross(normal,tangent);
    // Project onto the uniquely identified render plane, not the BSP hull's
    // offset collision plane. Polygon offset handles depth bias at draw time.
    const auto a0=vertices[indices[range.first_index]].position;
    const auto anchor=sub(point,mul(normal,dot(sub(point,a0),normal)));
    const double half=request.decal_half_size_units;
    std::vector<renderer::RenderDecalVertex> geometry;
    geometry.reserve(96);
    const auto project=[&](V p)->P {
        const auto d=sub(p,anchor);return {dot(d,tangent),dot(d,bitangent)};
    };
    for(std::size_t j=range.first_index;j+2<static_cast<std::size_t>(range.first_index)+range.index_count;j+=3) {
        if(indices[j]>=vertices.size()||indices[j+1]>=vertices.size()||indices[j+2]>=vertices.size()) continue;
        const auto a=vertices[indices[j]].position,b=vertices[indices[j+1]].position,
            c=vertices[indices[j+2]].position;
        if(dot(cross(sub(b,a),sub(c,a)),normal)<=0) continue;
        auto polygon=clip_triangle({{-half,-half},{half,-half},{half,half},{-half,half}},
            {project(a),project(b),project(c)});
        if(polygon.size()<3) continue;
        const auto triangle_vertices=(polygon.size()-2)*3;
        if(geometry.size()+triangle_vertices>96)
            return {WorldImpactStatus::geometry_limit,{}};
        const auto emit=[&](P p) {
            renderer::RenderDecalVertex v;
            v.position=add(add(anchor,mul(tangent,p.x)),mul(bitangent,p.y));
            v.uv={static_cast<float>(0.5+p.x/(2*half)),static_cast<float>(0.5+p.y/(2*half))};
            geometry.push_back(v);
        };
        for(std::size_t k=1;k+1<polygon.size();++k) {
            emit(polygon[0]);emit(polygon[k]);emit(polygon[k+1]);
        }
    }
    if(geometry.empty()) return {WorldImpactStatus::surface_unmapped,{}};
    entries_[next_entry_]=Entry{request.action,std::move(geometry),now+20.0,
        now+request.decal_delay_seconds,request.decal_delay_seconds==0.0};
    next_entry_=(next_entry_+1)%entries_.size();
    ++revision_;dirty_=true;
    return {WorldImpactStatus::hit,point,surface};
    } catch (...) { return {WorldImpactStatus::trace_failed,{}}; }
}
void WorldImpactPresentation::update(double now) noexcept {
    if(!std::isfinite(now)) return;
    bool changed=false;
    for(auto& entry:entries_) if(entry) {
        if(now>=entry->expires_at) {entry.reset();changed=true;}
        else if(!entry->published && now>=entry->publish_at) {
            entry->published=true;changed=true;
        }
    }
    if(changed) {++revision_;dirty_=true;}
}
std::optional<renderer::RenderWorldDecals> WorldImpactPresentation::frame(
    std::shared_ptr<const assets::WorldTextureAsset> texture,
    game_api::LocalDecalMaterialMode mode) {
    if(!texture) return {};
    if(dirty_) {
        auto vertices=std::make_shared<std::vector<renderer::RenderDecalVertex>>();
        vertices->reserve(64U*96U);
        for(const auto& entry:entries_) if(entry && entry->published)
            vertices->insert(vertices->end(),entry->vertices.begin(),entry->vertices.end());
        cached_=std::move(vertices);dirty_=false;
    }
    if(!cached_||cached_->empty()) return {};
    return renderer::RenderWorldDecals{std::move(texture),cached_,revision_,mode};
}
} // namespace hlclient::app

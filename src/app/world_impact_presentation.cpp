#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
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
bool rigid_matrix(const renderer::RenderMatrix4& m) noexcept {
    if(!renderer::is_finite(m) || std::abs(m.values[3])>1e-6F ||
        std::abs(m.values[7])>1e-6F || std::abs(m.values[11])>1e-6F ||
        std::abs(m.values[15]-1.0F)>1e-6F) return false;
    const V x{m.values[0],m.values[1],m.values[2]},
        y{m.values[4],m.values[5],m.values[6]}, z{m.values[8],m.values[9],m.values[10]};
    return std::abs(dot(x,x)-1)<1e-4 && std::abs(dot(y,y)-1)<1e-4 &&
        std::abs(dot(z,z)-1)<1e-4 && std::abs(dot(x,y))<1e-4 &&
        std::abs(dot(x,z))<1e-4 && std::abs(dot(y,z))<1e-4 &&
        dot(cross(x,y),z)>.9999;
}
V inverse_rigid_point(const renderer::RenderMatrix4& m,V p) noexcept {
    const auto d=sub(p,{m.values[12],m.values[13],m.values[14]});
    return {static_cast<float>(dot(d,{m.values[0],m.values[1],m.values[2]})),
        static_cast<float>(dot(d,{m.values[4],m.values[5],m.values[6]})),
        static_cast<float>(dot(d,{m.values[8],m.values[9],m.values[10]}))};
}
std::optional<std::array<double,3>> barycentric(V p,V a,V b,V c) noexcept {
    const auto ab=sub(b,a),ac=sub(c,a),ap=sub(p,a);
    const auto aa=dot(ab,ab),bb=dot(ac,ac),d=dot(ab,ac),
        determinant=aa*bb-d*d;
    if(!std::isfinite(determinant)||determinant<=1e-12) return {};
    const auto u=(dot(ap,ab)*bb-dot(ap,ac)*d)/determinant;
    const auto v=(dot(ap,ac)*aa-dot(ap,ab)*d)/determinant;
    return std::array<double,3>{1-u-v,u,v};
}
std::optional<std::array<float,3>> surface_light(
    const world_render::WorldRenderPackage& package,
    const world_render::WorldRenderSurfaceRange& range,
    const world_render::WorldRenderVertex& a,
    const world_render::WorldRenderVertex& b,
    const world_render::WorldRenderVertex& c,V p) noexcept {
    // Match the world pass's selected static source layer, at this exact
    // triangle's interpolated atlas coordinate, not a camera/downward probe.
    if(range.lightmap_mode==world_render::WorldRenderLightmapMode::unlit_white)
        return std::array<float,3>{1,1,1};
    const auto* binding=package.lightmaps().binding_for_surface(range.source_world_surface_index);
    if(!binding || binding->status!=assets::WorldSurfaceLightmapBindingStatus::resolved ||
        !binding->atlas_page_index || *binding->atlas_page_index>=package.lightmaps().pages().size() ||
        binding->selected_static_source_style_slot>=assets::kWorldLightmapStyleSlotCount) return {};
    const auto& page=package.lightmaps().pages()[*binding->atlas_page_index];
    const auto& image=page.style_slot_images[binding->selected_static_source_style_slot];
    if(!image.width || !image.height || image.rgba_pixels.size()!=std::size_t(image.width)*image.height*4U)
        return {};
    const auto weights=barycentric(p,a.position,b.position,c.position);
    if(!weights) return {};
    const double s=(*weights)[0]*a.lightmap_atlas_coordinate.x+
        (*weights)[1]*b.lightmap_atlas_coordinate.x+(*weights)[2]*c.lightmap_atlas_coordinate.x;
    const double t=(*weights)[0]*a.lightmap_atlas_coordinate.y+
        (*weights)[1]*b.lightmap_atlas_coordinate.y+(*weights)[2]*c.lightmap_atlas_coordinate.y;
    if(!std::isfinite(s)||!std::isfinite(t))return {};
    const auto x=std::clamp(s*image.width-.5,0.0,double(image.width-1U));
    const auto y=std::clamp(t*image.height-.5,0.0,double(image.height-1U));
    const auto x0=static_cast<std::uint32_t>(std::floor(x)), y0=static_cast<std::uint32_t>(std::floor(y));
    const auto x1=std::min(x0+1U,image.width-1U), y1=std::min(y0+1U,image.height-1U);
    const auto fx=x-x0,fy=y-y0;
    const auto sample=[&](std::uint32_t sx,std::uint32_t sy,std::size_t channel) {
        return double(std::to_integer<std::uint8_t>(image.rgba_pixels[(std::size_t(sy)*image.width+sx)*4U+channel]))/255.0;
    };
    std::array<float,3> rgb{};
    for(std::size_t channel=0;channel<rgb.size();++channel)
        rgb[channel]=static_cast<float>((sample(x0,y0,channel)*(1-fx)+sample(x1,y0,channel)*fx)*(1-fy)+
            (sample(x0,y1,channel)*(1-fx)+sample(x1,y1,channel)*fx)*fy);
    return rgb;
}
} // namespace

void WorldImpactPresentation::reset() noexcept {
    for(auto& entry:entries_) entry.reset();
    next_entry_=0; ++revision_; dirty_=true; cached_.reset();
    seen_brush_actions_.fill(std::nullopt);next_seen_brush_=0;
    server_generation_=server_resource_generation_=next_seen_server_=0;
    seen_server_.fill(std::nullopt);server_stale_floor_={};
    particles_.reset();
    particle_diagnostics_.fill(std::nullopt);
    lines_.fill(std::nullopt);seen_lines_.fill(std::nullopt);next_line_=next_seen_line_=0;line_now_=0;
    debug_frozen_line_.reset();
    line_serial_=debug_frozen_serial_=0;
}
std::size_t WorldImpactPresentation::server_active() const noexcept {
    return static_cast<std::size_t>(std::count_if(entries_.begin(),entries_.end(),
        [](const auto& e){return e && e->server.has_value();}));
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
    bool decal_resource_ready,double now,const BrushImpactContext* brush_context) noexcept {
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
    // A normal approved brush context requires an authoritative scene even
    // when it contains zero blockers. Unavailable reconstruction is NOT an
    // empty scene: never cast through a door at a farther world surface.
    if(brush_context && brush_context->library) {
        if(!brushes)return {WorldImpactStatus::collision_unavailable,{}};
        if(server_generation_ && request.action.generation!=server_generation_)
            return {WorldImpactStatus::unsupported_blocker,{}};
    }
    const auto direction=mul(request.direction,1.0/distance);
    const auto end=add(request.origin,mul(direction,request.maximum_distance_units));
    if(!finite(end)) return {WorldImpactStatus::invalid_request,{}};
    collision::CollisionTraceResult hit;
    const goldsrc::collision::BrushCollisionSceneInstance* brush_instance=nullptr;
    if(brushes) {
        goldsrc::collision::BrushCollisionSceneTraceRequest trace;
        trace.start=request.origin;trace.end=end;
        const auto result=goldsrc::collision::BrushCollisionSceneQuery{brushes}
            .trace_hull(trace,scratch_);
        if(!result) return {WorldImpactStatus::trace_failed,{}};
        if(result.result->scene_hit &&
            result.result->scene_hit->kind!=collision::CollisionTraceHitKind::world) {
            const auto& identity=result.result->scene_hit->brush_instance;
            if(identity) {
                const auto found=std::ranges::find(brushes->instances(),*identity,
                    &goldsrc::collision::BrushCollisionSceneInstance::identity);
                if(found!=brushes->instances().end())brush_instance=&*found;
            }
            if(!brush_instance)return {WorldImpactStatus::unsupported_blocker,{}};
        }
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
    if(request.tracer) {
        const auto& line=*request.tracer;
        line_now_=std::max(line_now_,now);
        const bool motion_valid=(line.travel_speed_units_per_second==0 && line.segment_length_units==0) ||
            (std::isfinite(line.travel_speed_units_per_second) && line.travel_speed_units_per_second>0 &&
             line.travel_speed_units_per_second<=100000 && std::isfinite(line.segment_length_units) &&
             line.segment_length_units>0 && line.segment_length_units<=1024);
        const auto endpoint=hit.hit ? hit.end_position : end;
        if(motion_valid && finite(line.origin) && finite(endpoint) && std::isfinite(line.width_units) &&
            line.width_units>0 && line.width_units<=4 && std::isfinite(line.lifetime_seconds) &&
            line.lifetime_seconds>0 && line.lifetime_seconds<=0.25 &&
            std::isfinite(line.head_taper_fraction) && line.head_taper_fraction>=0 && line.head_taper_fraction<=1 &&
            std::isfinite(line.taper_end_diameter_fraction) && line.taper_end_diameter_fraction>=0 && line.taper_end_diameter_fraction<=1 &&
            game_api::valid_transient_glow(line.head_glow) &&
            game_api::valid_world_line_geometry(line.geometry) &&
            std::isfinite(line.start_offset_units) && line.start_offset_units>=0 && line.start_offset_units<=1024 &&
            std::all_of(line.color.begin(),line.color.end(),[](float c){return std::isfinite(c)&&c>=0&&c<=1;}) &&
            now<request.action.started_at_seconds+line.lifetime_seconds &&
            !std::any_of(seen_lines_.begin(),seen_lines_.end(),[&](const auto& a){return a && *a==request.action;})) {
            seen_lines_[next_seen_line_++%seen_lines_.size()]=request.action;
            lines_[next_line_++%lines_.size()]=LineEntry{request.action,
                {line.origin,endpoint,line.width_units,line.color,line.head_taper_fraction,line.geometry,
                    line.taper_end_diameter_fraction},request.action.started_at_seconds+line.lifetime_seconds,
                line.follow_local_muzzle,line.travel_speed_units_per_second,line.segment_length_units,
                line.follow_local_muzzle ? std::nullopt : std::optional{line.origin},now,line.head_glow,line.start_offset_units,++line_serial_};
        }
    }
    if(!hit.collision_plane || !hit.hit) return {WorldImpactStatus::miss,{}};
    if(request.suppress_surface_effects) return {WorldImpactStatus::hit,hit.end_position,{}};
    if((hit.hit->kind!=collision::CollisionTraceHitKind::world && !brush_instance) || hit.in_liquid)
        return {WorldImpactStatus::unsupported_blocker,{}};
    const auto world_point=hit.end_position,world_normal=hit.collision_plane->normal;
    auto point=world_point,normal=world_normal;
    if(!finite(point)||!finite(normal)||length(normal)<0.99||length(normal)>1.01)
        return {WorldImpactStatus::trace_failed,{}};
    std::optional<ServerDecalTarget> target;
    std::span<const std::uint32_t> brush_surfaces;
    if(brush_instance) {
        if(!brush_context || !brush_context->library || !brush_context->library->render_package() ||
            !brush_instance->identity.source_entity_index || brush_context->lifetimes.size()>4096U)
            return {WorldImpactStatus::unsupported_blocker,{}};
        const auto entity=*brush_instance->identity.source_entity_index;
        const auto source_model=brush_instance->identity.source_model_index;
        const auto life=std::ranges::find_if(brush_context->lifetimes,[&](const auto& value) {
            return value.entity_number==entity && value.source_model_index==source_model &&
                value.lifetime!=0U && value.model_slot!=0U;
        });
        const auto model=std::ranges::find(brush_context->library->models(),source_model,
            &world_scene_render::BrushSubmodelRenderModel::source_model_index);
        const auto& pose=brush_instance->transform;
        const auto transform=goldsrc::brush_models::make_brush_submodel_transform(
            pose.translation,pose.rotation_degrees,pose.source_model_origin);
        if(life==brush_context->lifetimes.end() || model==brush_context->library->models().end() ||
            !transform || model->render_surface_indices().empty())
            return {WorldImpactStatus::unsupported_blocker,{}};
        package=brush_context->library->render_package().get();
        brush_surfaces=model->render_surface_indices();
        target=ServerDecalTarget{entity,life->model_slot,source_model,life->lifetime,transform.transform->model_matrix};
        point=goldsrc::brush_models::brush_rigid_world_to_local_point(pose,point);
        normal=goldsrc::brush_models::brush_rigid_world_to_local_vector(pose,normal);
    }
    const auto vertices=package->vertices();
    const auto indices=package->indices();
    const auto ranges=package->surface_ranges();
    std::optional<std::size_t> selected;
    for(std::size_t i=0;i<ranges.size();++i) {
        if(target && std::ranges::find(brush_surfaces,i)==brush_surfaces.end())continue;
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
    if(target) {
        if(std::ranges::any_of(seen_brush_actions_,[&](const auto& action) {
            return action && *action==request.action;
        }))return {WorldImpactStatus::hit,world_point,surface};
        seen_brush_actions_[next_seen_brush_++%seen_brush_actions_.size()]=request.action;
    }
    if(request.particles) {
        const auto status=particles_.spawn(request.action,world_point,world_normal,*request.particles,now);
        const auto serial=++particle_serial_;
        particle_diagnostics_[(serial-1U)%particle_diagnostics_.size()]=WorldImpactParticleDiagnostic{
            serial,request.action,world_point,surface.source_surface_index,request.particles->count,status};
    }
    if(!decal_resource_ready || request.suppress_decal || (target && !brush_context->texture))
        return {WorldImpactStatus::hit,world_point,surface};
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
    Entry entry{request.action,std::move(geometry),now+20.0,
        now+request.decal_delay_seconds,request.decal_delay_seconds==0.0};
    if(target) {
        auto retained=std::make_shared<const std::vector<renderer::RenderDecalVertex>>(std::move(entry.vertices));
        entry.brush=Entry::ServerPlacement{{},brush_context->texture,brush_context->material_mode,
            std::move(target),std::move(retained),revision_+1U};
    }
    entries_[next_entry_]=std::move(entry);
    next_entry_=(next_entry_+1)%entries_.size();
    ++revision_;dirty_=true;
    return {WorldImpactStatus::hit,world_point,surface};
    } catch (...) { return {WorldImpactStatus::trace_failed,{}}; }
}
void WorldImpactPresentation::bind_server_resources(
    const std::uint64_t generation,const std::uint64_t resource_generation) noexcept {
    if(generation==server_generation_ && resource_generation==server_resource_generation_)return;
    bool changed=false;
    for(auto& entry:entries_)if(entry && (entry->server || entry->brush)) {entry.reset();changed=true;}
    seen_brush_actions_.fill(std::nullopt);next_seen_brush_=0;
    server_generation_=generation;server_resource_generation_=resource_generation;
    seen_server_.fill(std::nullopt);next_seen_server_=0;server_stale_floor_={};
    if(changed){++revision_;dirty_=true;}
}
ServerDecalResult WorldImpactPresentation::submit_server(
    const ServerDecalRequest& request,const world_render::WorldRenderPackage* package,
    const std::span<const std::uint32_t> allowed_surface_indices,
    std::optional<ServerDecalTarget> target) noexcept {
    try {
    if(!request.identity.generation || !request.identity.record || !request.record_ordinal ||
        !request.resource_generation || !finite(request.position) || request.target_entity>8191U ||
        request.identity.source!=game_api::TemporaryEffectIdentity::Source::server_record)
        return {ServerDecalStatus::invalid_request};
    if(!server_generation_ && !server_resource_generation_)
        bind_server_resources(request.identity.generation,request.resource_generation);
    if(request.identity.generation!=server_generation_ ||
        request.resource_generation!=server_resource_generation_) return {ServerDecalStatus::stale};
    const std::array<std::uint64_t,3> order{request.record_ordinal,
        request.identity.message_bit_offset,request.identity.entry_ordinal};
    if(order<=server_stale_floor_ || std::any_of(seen_server_.begin(),seen_server_.end(),[&](const auto& receipt) {
        return receipt && receipt->identity==request.identity;
    })) return {ServerDecalStatus::duplicate};
    if(!package) return {ServerDecalStatus::target_unavailable};
    if(request.target_entity) {
        if(!target || target->entity_number!=request.target_entity || !target->model_slot ||
            !target->source_model_index || !target->lifetime || !rigid_matrix(target->model_transform) ||
            allowed_surface_indices.empty())return {ServerDecalStatus::target_unavailable};
    } else if(target || !allowed_surface_indices.empty())return {ServerDecalStatus::invalid_request};
    const auto& texture=request.texture;
    if(!texture || (texture->alpha_mode!=assets::WorldTextureAlphaMode::masked_index_255 &&
        texture->alpha_mode!=assets::WorldTextureAlphaMode::straight_alpha) ||
        !texture->width || !texture->height || texture->width>256U || texture->height>256U ||
        texture->mip_levels[0].width!=texture->width || texture->mip_levels[0].height!=texture->height ||
        texture->mip_levels[0].rgba_pixels.size()!=std::size_t(texture->width)*texture->height*4U ||
        (request.material_mode!=game_api::LocalDecalMaterialMode::straight_alpha &&
         request.material_mode!=game_api::LocalDecalMaterialMode::white_neutral_modulate &&
         request.material_mode!=game_api::LocalDecalMaterialMode::lit_straight_alpha))
        return {ServerDecalStatus::material_rejected};
    const auto position=target ? inverse_rigid_point(target->model_transform,request.position) : request.position;
    if(!finite(position))return {ServerDecalStatus::invalid_request};
    const auto vertices=package->vertices();const auto indices=package->indices();const auto ranges=package->surface_ranges();
    if(allowed_surface_indices.size()>65535U || std::any_of(allowed_surface_indices.begin(),allowed_surface_indices.end(),
        [&](std::uint32_t i){return i>=ranges.size();})) return {ServerDecalStatus::target_unavailable};
    const auto eligible=[&](std::size_t i) {
        return !target || std::find(allowed_surface_indices.begin(),allowed_surface_indices.end(),i)!=allowed_surface_indices.end();
    };
    struct Selected {std::size_t range{};V normal{},anchor{};double distance{};};
    std::optional<Selected> selected;bool ambiguous=false;
    // Each wire coordinate is quantized to eighths; .25 bounds the combined
    // position error. Find a plane at the received point, not a new shot hit.
    constexpr double plane_tolerance=.25, equal_distance_tolerance=1e-5;
    for(std::size_t i=0;i<ranges.size();++i) {
        if(!eligible(i))continue;
        const auto& range=ranges[i];
        if(position.x<range.bounds.minimum.x-plane_tolerance || position.x>range.bounds.maximum.x+plane_tolerance ||
            position.y<range.bounds.minimum.y-plane_tolerance || position.y>range.bounds.maximum.y+plane_tolerance ||
            position.z<range.bounds.minimum.z-plane_tolerance || position.z>range.bounds.maximum.z+plane_tolerance ||
            range.first_index>indices.size() || range.index_count>indices.size()-range.first_index)continue;
        for(std::size_t j=range.first_index;j+2U<std::size_t(range.first_index)+range.index_count;j+=3U) {
            if(indices[j]>=vertices.size() || indices[j+1]>=vertices.size() || indices[j+2]>=vertices.size())continue;
            const auto a=vertices[indices[j]].position,b=vertices[indices[j+1]].position,c=vertices[indices[j+2]].position;
            const auto unscaled=cross(sub(b,a),sub(c,a));const auto nlength=length(unscaled);
            if(nlength<1e-7)continue;
            const auto normal=mul(unscaled,1/nlength);const auto signed_distance=dot(sub(position,a),normal);
            if(std::abs(signed_distance)>plane_tolerance)continue;
            const auto anchor=sub(position,mul(normal,signed_distance));const auto weights=barycentric(anchor,a,b,c);
            if(!weights || std::any_of(weights->begin(),weights->end(),[](double v){return v< -1e-5 || v>1.00001;}))continue;
            const auto distance=std::abs(signed_distance);
            if(!selected || distance<selected->distance-equal_distance_tolerance) {
                selected=Selected{i,normal,anchor,distance};ambiguous=false;
            } else if(std::abs(distance-selected->distance)<=equal_distance_tolerance &&
                (dot(normal,selected->normal)<.9999 || std::abs(dot(sub(anchor,selected->anchor),selected->normal))>1e-4))
                ambiguous=true;
        }
    }
    if(!selected)return {ServerDecalStatus::surface_unmapped};
    if(ambiguous)return {ServerDecalStatus::surface_ambiguous};
    if(!allowed_surface(*package,ranges[selected->range]))return {ServerDecalStatus::surface_unsupported};
    const auto normal=selected->normal,anchor=selected->anchor;
    const auto ref=std::abs(normal.z)>.9F ? V{0,1,0} : V{0,0,1};
    auto tangent=cross(ref,normal);tangent=mul(tangent,1/length(tangent));const auto bitangent=cross(normal,tangent);
    // Compatible unit-size profile: the immutable asset's FULL dimensions
    // supply the world footprint. Width and height remain independent.
    const double half_width=texture->width*.5,half_height=texture->height*.5;
    std::vector<renderer::RenderDecalVertex> geometry;geometry.reserve(96U);
    const auto project=[&](V p)->P {const auto d=sub(p,anchor);return {dot(d,tangent),dot(d,bitangent)};};
    for(std::size_t i=0;i<ranges.size();++i) {
        if(!eligible(i) || !allowed_surface(*package,ranges[i]))continue;
        const auto& range=ranges[i];
        if(range.first_index>indices.size() || range.index_count>indices.size()-range.first_index)continue;
        for(std::size_t j=range.first_index;j+2U<std::size_t(range.first_index)+range.index_count;j+=3U) {
            if(indices[j]>=vertices.size() || indices[j+1]>=vertices.size() || indices[j+2]>=vertices.size())continue;
            const auto& a=vertices[indices[j]];const auto& b=vertices[indices[j+1]];const auto& c=vertices[indices[j+2]];
            const auto n=cross(sub(b.position,a.position),sub(c.position,a.position));const auto nlength=length(n);
            if(nlength<1e-7 || dot(mul(n,1/nlength),normal)<.9999 ||
                std::abs(dot(sub(a.position,anchor),normal))>1e-4 ||
                std::abs(dot(sub(b.position,anchor),normal))>1e-4 ||
                std::abs(dot(sub(c.position,anchor),normal))>1e-4)continue;
            const auto polygon=clip_triangle({{-half_width,-half_height},{half_width,-half_height},
                {half_width,half_height},{-half_width,half_height}},
                {project(a.position),project(b.position),project(c.position)});
            if(polygon.size()<3U)continue;
            if(geometry.size()+(polygon.size()-2U)*3U>96U)return {ServerDecalStatus::geometry_limit};
            const auto emit=[&](P p)->bool {
                renderer::RenderDecalVertex v;
                v.position=add(add(anchor,mul(tangent,p.x)),mul(bitangent,p.y));
                v.uv={static_cast<float>(.5+p.x/(2*half_width)),static_cast<float>(.5+p.y/(2*half_height))};
                const auto light=surface_light(*package,range,a,b,c,v.position);
                if(!light)return false;
                v.surface_light_rgb=*light;geometry.push_back(v);return true;
            };
            for(std::size_t k=1;k+1U<polygon.size();++k)
                if(!emit(polygon[0]) || !emit(polygon[k]) || !emit(polygon[k+1U]))
                    return {ServerDecalStatus::lighting_unavailable};
        }
    }
    if(geometry.empty())return {ServerDecalStatus::surface_unmapped};
    const auto count=geometry.size();
    auto retained=std::make_shared<const std::vector<renderer::RenderDecalVertex>>(std::move(geometry));
    Entry entry;entry.expires_at=std::numeric_limits<double>::infinity();entry.published=true;
    entry.server=Entry::ServerPlacement{request.identity,texture,request.material_mode,
        std::move(target),std::move(retained),revision_+1U};
    const bool evicted=entries_[next_entry_].has_value();
    entries_[next_entry_]=std::move(entry);next_entry_=(next_entry_+1U)%entries_.size();
    auto& receipt=seen_server_[next_seen_server_++%seen_server_.size()];
    if(receipt)server_stale_floor_=std::max(server_stale_floor_,receipt->order);
    receipt=ServerReceipt{request.identity,order};
    ++revision_;dirty_=true;
    return {ServerDecalStatus::submitted,static_cast<std::uint32_t>(ranges[selected->range].source_world_surface_index),count,evicted};
    } catch (...) {return {ServerDecalStatus::geometry_limit};}
}
void WorldImpactPresentation::retire_server_targets(
    const std::span<const ServerDecalTargetLifetime> live) noexcept {
    if(live.size()>4096U)return;
    bool changed=false;
    for(auto& entry:entries_)if(entry && (entry->server || entry->brush)) {
        const auto& placement=entry->server ? *entry->server : *entry->brush;
        if(!placement.target)continue;
        const auto& target=*placement.target;
        if(std::none_of(live.begin(),live.end(),[&](const auto& current) {
            return current.entity_number==target.entity_number && current.lifetime==target.lifetime;
        })) {entry.reset();changed=true;}
    }
    if(changed){++revision_;dirty_=true;}
}
std::vector<renderer::RenderWorldDecals> WorldImpactPresentation::frame_batches(
    const world_scene_render::RuntimeBrushRenderFrame* frame) const {
    std::vector<renderer::RenderWorldDecals> result;result.reserve(64U);
    for(const auto& entry:entries_)if(entry && entry->published && (entry->server || entry->brush)) {
        const auto& placement=entry->server ? *entry->server : *entry->brush;
        renderer::RenderWorldDecals batch{placement.texture,placement.geometry,placement.geometry_revision,placement.material_mode};
        if(placement.target) {
            if(!frame || frame->generation!=(entry->server ? server_generation_ : entry->action.generation))continue;
            const auto& target=*placement.target;
            const auto current=std::find_if(frame->instances.begin(),frame->instances.end(),[&](const auto& i) {
                return i.entity_number==target.entity_number && i.model_slot==target.model_slot &&
                    i.source_model_index==target.source_model_index;
            });
            if(current==frame->instances.end() || !rigid_matrix(current->model_transform))continue;
            batch.model_transform=current->model_transform;
        }
        result.push_back(std::move(batch));
    }
    return result;
}
void WorldImpactPresentation::update(double now) noexcept {
    if(!std::isfinite(now)) return;
    line_now_=std::max(line_now_,now);
    particles_.update(now);
    for(auto& line:lines_) if(line && now>=line->expires_at) line.reset();
    bool changed=false;
    for(auto& entry:entries_) if(entry) {
        if(now>=entry->expires_at) {entry.reset();changed=true;}
        else if(!entry->published && now>=entry->publish_at) {
            entry->published=true;changed=true;
        }
    }
    if(changed) {++revision_;dirty_=true;}
}
void WorldImpactPresentation::append_lines(renderer::RenderScene& scene,std::optional<LocalMuzzleAnchor> anchor) noexcept {
    for(auto& line:lines_) if(line && line_now_<line->expires_at && scene.world_line_count<scene.world_lines.size()) {
        auto presented=line->line;
        const bool freeze=debug_freeze_tracer_ && line->speed>0 &&
            presented.geometry==game_api::WorldLineGeometry::solid_3d;
        if(line->follow_local_muzzle && (line->speed==0 || !line->launch_origin)) {
            if(!anchor || !finite(anchor->point) || anchor->generation!=line->action.generation ||
                anchor->model_index!=line->action.model_index) continue;
            presented.start=anchor->point;
            if(line->speed>0) {line->launch_origin=anchor->point;line->flight_started_at=line_now_;}
        }
        if(line->speed>0 || line->start_offset_units>0) {
            if(line->speed>0 && !line->launch_origin) continue;
            const auto start=line->speed>0 ? *line->launch_origin : presented.start;
            const auto delta=sub(line->line.end,start);const auto distance=length(delta);
            if(!std::isfinite(distance) || distance<0.001) continue;
            const auto elapsed=freeze ? 0.0 : std::max(0.0,line_now_-line->flight_started_at);
            // Presentation-only offset, measured from the captured muzzle,
            // never from the trace eye or from last frame's advanced tail.
            const auto back=static_cast<double>(line->start_offset_units)+line->speed*elapsed;
            const auto front=line->speed>0 ? std::min(distance,back+line->segment_length) : distance;
            if(back>=front) continue; // close hit or tail reached the frozen collision point
            const auto direction=mul(delta,1.0/distance);
            presented.start=add(start,mul(direction,back));
            presented.end=add(start,mul(direction,front));
            if(line->speed>0 && !freeze) {
                const auto duration=line->expires_at-line->action.started_at_seconds;
                presented.color[3]*=static_cast<float>(std::clamp((line->expires_at-line_now_)/duration,0.0,1.0));
            }
        }
        if(freeze) {
            // Pool wrap or a delayed muzzle anchor must not replace a newer shot.
            if(line->serial>debug_frozen_serial_) {
                debug_frozen_line_=presented; // same clipped length/profile, original opacity
                debug_frozen_serial_=line->serial;
            }
            line.reset(); // consumed once; render/update/replay cannot recapture or move it
            continue; // inspect geometry without a permanent head billboard/light halo
        }
        scene.world_lines[scene.world_line_count++]=presented;
        if(line->head_glow.radius_units>0 && scene.world_flash_count<scene.world_flashes.size()) {
            auto color=presented.color;color[3]*=0.45F;
            scene.world_flashes[scene.world_flash_count++]={presented.end,line->head_glow.radius_units,color};
        }
        if(line->head_glow.light_radius_units>0 && scene.world_effect_light_count<scene.world_effect_lights.size())
            scene.world_effect_lights[scene.world_effect_light_count++]={presented.end,line->head_glow.light_radius_units,
                line->head_glow.light_intensity*presented.color[3],{presented.color[0],presented.color[1],presented.color[2]}};
    }
    if(debug_frozen_line_ && scene.world_line_count<scene.world_lines.size())
        scene.world_lines[scene.world_line_count++]=*debug_frozen_line_;
}
std::optional<renderer::RenderWorldDecals> WorldImpactPresentation::frame(
    std::shared_ptr<const assets::WorldTextureAsset> texture,
    game_api::LocalDecalMaterialMode mode) {
    if(!texture) return {};
    if(dirty_) {
        auto vertices=std::make_shared<std::vector<renderer::RenderDecalVertex>>();
        vertices->reserve(64U*96U);
        for(const auto& entry:entries_) if(entry && entry->published && !entry->server && !entry->brush)
            vertices->insert(vertices->end(),entry->vertices.begin(),entry->vertices.end());
        cached_=std::move(vertices);dirty_=false;
    }
    if(!cached_||cached_->empty()) return {};
    return renderer::RenderWorldDecals{std::move(texture),cached_,revision_,mode};
}
} // namespace hlclient::app

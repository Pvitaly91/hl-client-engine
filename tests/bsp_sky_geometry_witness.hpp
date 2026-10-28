#pragma once
#ifdef HLCLIENT_BSP_CANDIDATE
// Narrow opt-in forensic witness; every readback and reconstruction runs only
// after the fixture's measured loop. It never filters or reorders geometry.
#include "bsp_world_causal_trace.hpp"
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>
#include <cmath>
#include <limits>
#include <set>

namespace hlclient::tests::bsp_sky_geometry_witness {
namespace trace = bsp_world_causal_trace;
using Key=std::array<std::uint32_t,3>;
using GeometryKey=std::array<std::uint32_t,9>;
using Range=std::pair<std::uint32_t,std::uint32_t>;
enum class Relation {outside,intersecting,inside};
inline const char* name(Relation relation) {
    return relation==Relation::outside?"outside":relation==Relation::inside?"inside":"intersecting";
}
// Intentionally mirrors classify_prepared_world_bounds in opengl_renderer.cpp,
// including its 1-unit + 64-float-epsilon magnitude margin. Public classify()
// uses another tolerance and is not an exact legacy admission witness.
inline Relation classify(const world_visibility::WorldViewFrustum& frustum,const assets::WorldBounds& bounds) {
    bool inside=true;
    for(const auto& plane:frustum.planes()) {
        const auto px=plane.normal.x>=0?bounds.maximum.x:bounds.minimum.x;
        const auto py=plane.normal.y>=0?bounds.maximum.y:bounds.minimum.y;
        const auto pz=plane.normal.z>=0?bounds.maximum.z:bounds.minimum.z;
        const auto nx=plane.normal.x>=0?bounds.minimum.x:bounds.maximum.x;
        const auto ny=plane.normal.y>=0?bounds.minimum.y:bounds.maximum.y;
        const auto nz=plane.normal.z>=0?bounds.minimum.z:bounds.maximum.z;
        const auto distance=[&](double x,double y,double z) {
            return static_cast<double>(plane.normal.x)*x+static_cast<double>(plane.normal.y)*y+
                static_cast<double>(plane.normal.z)*z+static_cast<double>(plane.signed_offset);
        };
        const auto positive=distance(px,py,pz),negative=distance(nx,ny,nz);
        const auto magnitude=std::abs(static_cast<double>(plane.normal.x)*px)+
            std::abs(static_cast<double>(plane.normal.y)*py)+std::abs(static_cast<double>(plane.normal.z)*pz)+
            std::abs(static_cast<double>(plane.signed_offset));
        const auto tolerance=1.0+64.0*static_cast<double>(std::numeric_limits<float>::epsilon())*magnitude;
        if(!std::isfinite(positive)||!std::isfinite(negative)||!std::isfinite(tolerance))return Relation::intersecting;
        if(positive < -tolerance)return Relation::outside;
        if(negative < tolerance)inside=false;
    }
    return inside?Relation::inside:Relation::intersecting;
}
inline void vector(assets::AssetVector3 v){std::cout<<'['<<v.x<<','<<v.y<<','<<v.z<<']';}
inline void bounds(const assets::WorldBounds& b){std::cout<<'[';vector(b.minimum);std::cout<<',';vector(b.maximum);std::cout<<']';}
template<class T>inline void list(const T& values){std::cout<<'[';bool first=true;for(const auto& v:values){if(!first)std::cout<<',';first=false;std::cout<<v;}std::cout<<']';}
template<class T>inline void optional(const std::optional<T>& value){if(value)std::cout<<*value;else std::cout<<"null";}
inline std::uint64_t vertices_signature(const world_render::WorldRenderPackage& world) {
    std::uint64_t hash=14695981039346656037ULL;
    for(const auto& v:world.vertices())for(const float f:{v.position.x,v.position.y,v.position.z,
        v.normal.x,v.normal.y,v.normal.z,v.base_texture_coordinate.x,v.base_texture_coordinate.y,
        v.lightmap_atlas_coordinate.x,v.lightmap_atlas_coordinate.y})trace::hash_word(hash,std::bit_cast<std::uint32_t>(f));
    return hash;
}
inline std::uint64_t indices_signature(std::span<const std::uint32_t> indices) {
    std::uint64_t hash=14695981039346656037ULL;for(const auto index:indices)trace::hash_word(hash,index);return hash;
}
inline assets::WorldBounds indexed_bounds(const world_render::WorldRenderPackage& world,
    const world_render::WorldRenderSurfaceRange& surface) {
    REQUIRE(surface.index_count>0U);
    const auto first=world.vertices()[world.indices()[surface.first_index]].position;
    assets::WorldBounds result{first,first};
    for(const auto index:world.indices().subspan(surface.first_index,surface.index_count)) {
        const auto p=world.vertices()[index].position;
        result.minimum.x=std::min(result.minimum.x,p.x);result.minimum.y=std::min(result.minimum.y,p.y);result.minimum.z=std::min(result.minimum.z,p.z);
        result.maximum.x=std::max(result.maximum.x,p.x);result.maximum.y=std::max(result.maximum.y,p.y);result.maximum.z=std::max(result.maximum.z,p.z);
    }
    return result;
}
inline GeometryKey geometry_key(const world_render::WorldRenderPackage& world,const Key& key) {
    GeometryKey result{};
    for(std::size_t i=0;i<3U;++i){const auto p=world.vertices()[key[i]].position;
        result[i*3U]=std::bit_cast<std::uint32_t>(p.x);result[i*3U+1U]=std::bit_cast<std::uint32_t>(p.y);result[i*3U+2U]=std::bit_cast<std::uint32_t>(p.z);}
    // Same winding, cyclic rotations equivalent. Reversed winding is not silently merged.
    const auto original=result;
    for(std::size_t rotation=1U;rotation<3U;++rotation){GeometryKey rotated{};
        for(std::size_t i=0;i<9U;++i)rotated[i]=original[(i+rotation*3U)%9U];
        if(rotated<result)result=rotated;}
    return result;
}
struct Catalog {
    std::map<Key,std::vector<std::size_t>> sources;
    std::set<std::size_t> sky_surfaces;
    explicit Catalog(const world_render::WorldRenderPackage& world) {
        const auto indices=world.indices();
        for(std::size_t n=0;n<world.surface_ranges().size();++n){const auto& s=world.surface_ranges()[n];
            if(world.materials()[s.render_material_index].sky_surface)sky_surfaces.insert(n);
            for(std::size_t i=s.first_index;i<s.first_index+s.index_count;i+=3U)
                sources[{indices[i],indices[i+1U],indices[i+2U]}].push_back(n);}
    }
    bool sky(const Key& key) const {
        const auto found=sources.find(key);REQUIRE(found!=sources.end());REQUIRE_FALSE(found->second.empty());
        const bool result=sky_surfaces.contains(found->second.front());
        for(const auto source:found->second)REQUIRE(sky_surfaces.contains(source)==result);
        return result;
    }
};
struct Stream {
    std::vector<std::uint32_t> sky_indices;
    std::map<Key,std::size_t> multiplicities;
    std::map<GeometryKey,std::size_t> geometric_multiplicities;
    std::set<std::size_t> sources;
    std::size_t ambiguous_source_triangles{},sky_draws{},sky_ranges{};
};
inline Stream stream(const trace::Trace& recorded,GLint world_vao,const Catalog& catalog,
    const world_render::WorldRenderPackage& world) {
    Stream result;
    for(const auto& draw:recorded.draws)if(draw.vao==world_vao){bool any=false,all=true;
        for(std::size_t i=0;i<draw.indices.size();i+=3U){const Key key{draw.indices[i],draw.indices[i+1U],draw.indices[i+2U]};
            const bool sky=catalog.sky(key);any|=sky;all&=sky;if(!sky)continue;
            result.sky_indices.insert(result.sky_indices.end(),key.begin(),key.end());
            ++result.multiplicities[key];++result.geometric_multiplicities[geometry_key(world,key)];
            const auto& sources=catalog.sources.at(key);result.ambiguous_source_triangles+=sources.size()>1U;
            result.sources.insert(sources.begin(),sources.end());}
        REQUIRE((!any||all));if(any){++result.sky_draws;result.sky_ranges+=draw.index_ranges.size();}}
    return result;
}
struct LegacyBatch {std::string reason;Relation relation{Relation::intersecting};std::vector<Range> ranges;std::size_t visited_blocks{};};
struct LegacyPrediction {std::vector<LegacyBatch> batches;std::vector<std::size_t> probes;bool bypass{};std::size_t outside_probes{};};
inline LegacyPrediction predict_legacy(const world_render::WorldRenderPackage& world,
    const world_visibility::WorldViewFrustum& frustum) {
    LegacyPrediction out;const auto batches=world.draw_batches();const auto prepared=world.prepared_draw_batches();
    const auto blocks=world.prepared_draw_blocks();const bool ready=prepared.size()==batches.size()&&!blocks.empty();
    if(ready){const auto probes=std::min<std::size_t>(16U,blocks.size());
        for(std::size_t p=0;p<probes;++p){const auto b=p*blocks.size()/probes;out.probes.push_back(b);
            if(classify(frustum,blocks[b].bounds)==Relation::outside&&++out.outside_probes>=2U)break;}
        out.bypass=out.outside_probes<2U;}
    for(std::size_t b=0;b<batches.size();++b){const auto& batch=batches[b];LegacyBatch row;
        const auto full=[&](const char* reason){row.reason=reason;row.ranges={{batch.first_index,batch.index_count}};};
        if(!ready)full("prepared_unavailable_full_batch");
        else if(out.bypass)full("insufficient_probe_rejections_full_batch");
        else {const auto& p=prepared[b];row.relation=classify(frustum,p.bounds);
            if(p.block_count==0U||p.first_block>blocks.size()||p.block_count>blocks.size()-p.first_block)full("invalid_prepared_batch_full_batch");
            else if(row.relation==Relation::outside)row.reason="batch_bounds_outside_rejected";
            else if(row.relation==Relation::inside)full("batch_bounds_inside_full_batch");
            else {bool in_run=false;std::size_t runs{};
                for(const auto& block:blocks.subspan(p.first_block,p.block_count)){++row.visited_blocks;
                    const bool selected=classify(frustum,block.bounds)!=Relation::outside;
                    if(selected&&!in_run){if(++runs>4U)break;row.ranges.emplace_back(block.first_index,0U);}
                    if(selected)row.ranges.back().second=block.first_index+block.index_count-row.ranges.back().first;
                    in_run=selected;}
                if(runs>4U)full("more_than_four_runs_full_batch");else row.reason="selected_block_runs";}}
        out.batches.push_back(std::move(row));}
    return out;
}
inline void run(renderer::opengl::OpenGlRenderer& gpu,const renderer::RenderScene& full,
    renderer::RenderExtent extent,std::string_view view,bool capture,std::uint64_t& frame_id,
    renderer::opengl::OpenGlStaticWorldSelectionPolicy restore_selection,
    renderer::opengl::OpenGlStaticWorldOrderPolicy restore_order) {
    using Selection=renderer::opengl::OpenGlStaticWorldSelectionPolicy;
    using Order=renderer::opengl::OpenGlStaticWorldOrderPolicy;
    trace::RestorePolicies restore{gpu,restore_selection,restore_order};
    const auto old_precision=std::cout.precision();std::cout<<std::setprecision(std::numeric_limits<float>::max_digits10);
    // Match the existing causal trace boundary exactly, preserving every
    // authoritative shading/effect field instead of rebuilding a partial scene.
    auto scene=full;scene.dynamic_entities.reset();scene.first_person_entities.reset();
    REQUIRE(scene.static_world);REQUIRE(scene.static_world->scene_package);REQUIRE_FALSE(scene.static_world->visible_draw_list);
    REQUIRE(full.requested_msaa_samples==0U);
    const auto owner=scene.static_world->scene_package->world_package();const auto& world=*owner;
    REQUIRE(world.prepared_draw_batches().size()==world.draw_batches().size());
    const auto identity=world.resource_identity();const auto vertex_hash=vertices_signature(world),index_hash=indices_signature(world.indices());
    auto final_camera=scene.camera;
    if(scene.screen_effects.roll_degrees!=0.0||std::any_of(scene.screen_effects.translation.begin(),scene.screen_effects.translation.end(),[](float f){return f!=0;})){
        const auto adjusted=renderer::apply_screen_effects(scene.camera,scene.screen_effects);REQUIRE(adjusted);final_camera=*adjusted;}
    const auto matrix=renderer::camera_view_projection(final_camera,extent);REQUIRE(matrix);
    const auto frustum_result=world_visibility::WorldViewFrustum::from_view_projection(*matrix.matrix);REQUIRE(frustum_result);
    const auto& frustum=*frustum_result.frustum;const Catalog catalog{world};const auto prediction=predict_legacy(world,frustum);
    gpu.set_static_world_order_policy(Order::original);gpu.set_static_world_selection_policy(Selection::legacy_prepared_frustum);
    trace::Trace a;gpu.render_with_gpu_timing(scene,extent,frame_id++,true);a.finish();
    const auto receipt_a=gpu.statistics().last_static_world_frame;REQUIRE(receipt_a);
    const auto color_a=gpu.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_a);
    const auto depth_a=trace::read_depth(a.depth_target,extent);while(gpu.poll_gpu_timing()){}
    gpu.set_static_world_selection_policy(Selection::prepared_bsp);
    trace::Trace c;gpu.render_with_gpu_timing(scene,extent,frame_id++,true);c.finish();
    const auto receipt_c=gpu.statistics().last_static_world_frame;REQUIRE(receipt_c);
    const auto color_c=gpu.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_c);
    const auto depth_c=trace::read_depth(c.depth_target,extent);while(gpu.poll_gpu_timing()){}
    REQUIRE(receipt_c->path==renderer::opengl::OpenGlStaticWorldPath::prepared_bsp);
    REQUIRE(receipt_a->resource_id==identity.resource_id);REQUIRE(receipt_c->resource_id==identity.resource_id);
    REQUIRE(receipt_a->resource_revision==identity.revision);REQUIRE(receipt_c->resource_revision==identity.revision);
    REQUIRE(scene.static_world->scene_package->world_package().get()==owner.get());
    REQUIRE(world.resource_identity()==identity);REQUIRE(vertices_signature(world)==vertex_hash);
    REQUIRE(indices_signature(world.indices())==index_hash);REQUIRE_FALSE(a.draws.empty());REQUIRE_FALSE(c.draws.empty());
    const auto vao=a.draws.front().vao;REQUIRE(c.draws.front().vao==vao);
    const auto sa=stream(a,vao,catalog,world),sc=stream(c,vao,catalog,world);
    const auto prefix=[&](const char* tag){std::cout<<'['<<tag<<"] {\"view\":"<<std::quoted(view)
        <<",\"remote_capture\":"<<(capture?"true":"false")<<",\"resource_id\":\""<<identity.resource_id
        <<"\",\"revision\":\""<<identity.revision<<'"';};
    prefix("bsp-sky-geometry-owner");std::cout<<",\"schema\":1,\"scope\":\"untimed_world_only_original_order_A_legacy_C_cached_bsp\""
        <<",\"same_owner\":true,\"vertices\":"<<world.vertices().size()<<",\"indices\":"<<world.indices().size()
        <<",\"vertex_float_word_fnv64\":\""<<vertex_hash<<"\",\"index_word_fnv64\":\""<<index_hash<<'"'
        <<",\"width\":"<<extent.width<<",\"height\":"<<extent.height<<",\"camera_position\":";vector(final_camera.position);
    std::cout<<",\"camera_target\":";vector(final_camera.target);std::cout<<",\"camera_up\":";vector(final_camera.up);
    std::cout<<",\"fov\":"<<final_camera.vertical_field_of_view_radians<<",\"near\":"<<final_camera.near_plane<<",\"far\":"<<final_camera.far_plane;
    std::cout<<",\"view_projection\":";list(matrix.matrix->values);std::cout<<",\"frustum_planes\":[";
    bool first=true;for(const auto& plane:frustum.planes()){if(!first)std::cout<<',';first=false;std::cout<<'['<<plane.normal.x<<','<<plane.normal.y<<','<<plane.normal.z<<','<<plane.signed_offset<<']';}
    std::cout<<"],\"indexed_identity\":\"ordered_original_world_vertex_index_triple\",\"geometric_identity\":\"position_float_bits_cyclic_same_winding\"}\n";
    const auto point=world_spatial::WorldSpatialQuery::locate_point(scene.static_world->scene_package->spatial_package(),final_camera.position);
    prefix("bsp-sky-geometry-context");std::cout<<",\"camera_leaf\":";if(point.result)std::cout<<point.result->leaf_index;else std::cout<<"null";
    std::cout<<",\"legacy_probe_outside\":"<<prediction.outside_probes<<",\"legacy_all_batch_bypass\":"<<(prediction.bypass?"true":"false")<<",\"probed_block_ids\":";list(prediction.probes);
    std::cout<<",\"probed_blocks\":[";first=true;for(const auto id:prediction.probes){if(!first)std::cout<<',';first=false;
        const auto& block=world.prepared_draw_blocks()[id];std::cout<<"{\"block\":"<<id<<",\"bounds\":";bounds(block.bounds);
        std::cout<<",\"relation\":"<<std::quoted(name(classify(frustum,block.bounds)))<<'}';}std::cout<<']';
    std::cout<<",\"C_selected_surfaces\":";optional(receipt_c->selected_surfaces);
    std::cout<<",\"C_pvs_fallback_code\":";optional(receipt_c->pvs_fallback_code);
    std::cout<<",\"C_pvs_cache_hits\":";optional(receipt_c->pvs_cache_hits);
    std::cout<<",\"C_pvs_rebuilds\":";optional(receipt_c->pvs_rebuilds);
    std::cout<<",\"C_command_cache_hits\":";optional(receipt_c->command_cache_hits);
    std::cout<<",\"C_visited_nodes\":";optional(receipt_c->bsp_visited_nodes);
    std::cout<<",\"C_pvs_rejected_nodes\":";optional(receipt_c->bsp_pvs_rejected_nodes);
    std::cout<<",\"C_frustum_rejected_nodes\":";optional(receipt_c->bsp_frustum_rejected_nodes);std::cout<<"}\n";
    std::vector<std::uint32_t> predicted_indices;std::size_t outside_sky{};
    for(std::size_t b=0;b<world.draw_batches().size();++b){const auto& batch=world.draw_batches()[b];
        if(!world.materials()[batch.render_material_index].sky_surface)continue;
        const auto& predicted=prediction.batches[b];const auto& prepared=world.prepared_draw_batches()[b];
        prefix("bsp-sky-geometry-legacy-batch");std::cout<<",\"batch\":"<<b<<",\"material\":"<<batch.render_material_index
            <<",\"first_index\":"<<batch.first_index<<",\"index_count\":"<<batch.index_count<<",\"first_block\":"<<prepared.first_block
            <<",\"block_count\":"<<prepared.block_count<<",\"bounds\":";bounds(prepared.bounds);
        std::cout<<",\"frustum_relation\":"<<std::quoted(name(predicted.relation))<<",\"admission\":"<<std::quoted(predicted.reason)
            <<",\"scan_visited_blocks\":"<<predicted.visited_blocks<<",\"predicted_ranges\":[";first=true;
        for(const auto& [start,count]:predicted.ranges){if(!first)std::cout<<',';first=false;std::cout<<'['<<start<<','<<count<<']';
            predicted_indices.insert(predicted_indices.end(),world.indices().begin()+start,world.indices().begin()+start+count);}
        std::cout<<"]}\n";
        for(std::size_t j=0;j<prepared.block_count;++j){const auto id=prepared.first_block+j;const auto& block=world.prepared_draw_blocks()[id];
            prefix("bsp-sky-geometry-block");std::cout<<",\"batch\":"<<b<<",\"block\":"<<id<<",\"first_index\":"<<block.first_index
                <<",\"index_count\":"<<block.index_count<<",\"source_surface_count\":"<<unsigned(block.source_surface_count)<<",\"bounds\":";bounds(block.bounds);
            std::cout<<",\"conservative_frustum_relation\":"<<std::quoted(name(classify(frustum,block.bounds)))<<"}\n";}}
    for(const auto n:catalog.sky_surfaces){const auto& source=world.surface_ranges()[n];
        const auto exact_bounds=indexed_bounds(world,source);
        const bool same_bounds=source.bounds.minimum.x==exact_bounds.minimum.x&&source.bounds.minimum.y==exact_bounds.minimum.y&&
            source.bounds.minimum.z==exact_bounds.minimum.z&&source.bounds.maximum.x==exact_bounds.maximum.x&&
            source.bounds.maximum.y==exact_bounds.maximum.y&&source.bounds.maximum.z==exact_bounds.maximum.z;
        const auto relation=classify(frustum,exact_bounds);outside_sky+=relation==Relation::outside;
        prefix("bsp-sky-geometry-source");std::cout<<",\"render_surface\":"<<n<<",\"source\":"<<source.source_world_surface_index
            <<",\"source_ordinal\":";optional(world.textured_world().world.surfaces[source.source_world_surface_index].source_surface_ordinal);
        std::cout<<",\"batch\":"<<source.source_batch_index<<",\"material\":"<<source.render_material_index
            <<",\"first_index\":"<<source.first_index<<",\"index_count\":"<<source.index_count<<",\"bounds\":";bounds(source.bounds);
        std::cout<<",\"indexed_bounds\":";bounds(exact_bounds);std::cout<<",\"source_bounds_equal_indexed\":"<<(same_bounds?"true":"false");
        std::cout<<",\"conservative_frustum_relation\":"<<std::quoted(name(relation))<<",\"conservative_frustum_outside\":"<<(relation==Relation::outside?"true":"false")
            <<",\"submitted_in_A\":"<<(sa.sources.contains(n)?"true":"false")<<",\"submitted_in_C\":"<<(sc.sources.contains(n)?"true":"false")
            <<",\"indices\":";list(world.indices().subspan(source.first_index,source.index_count));
        std::set<std::uint32_t> vertices;for(const auto index:world.indices().subspan(source.first_index,source.index_count))vertices.insert(index);
        std::cout<<",\"vertices\":[";first=true;for(const auto index:vertices){if(!first)std::cout<<',';first=false;const auto& v=world.vertices()[index];
            std::cout<<'['<<index<<','<<v.position.x<<','<<v.position.y<<','<<v.position.z<<','<<v.normal.x<<','<<v.normal.y<<','<<v.normal.z
                <<','<<v.base_texture_coordinate.x<<','<<v.base_texture_coordinate.y<<','<<v.lightmap_atlas_coordinate.x<<','<<v.lightmap_atlas_coordinate.y<<']';}
        std::cout<<"]}\n";}
    const auto print_draws=[&](const trace::Trace& recorded,const char* policy){
        for(std::size_t d=0;d<recorded.draws.size();++d){const auto& draw=recorded.draws[d];
            if(draw.vao!=vao||draw.indices.empty()||!catalog.sky({draw.indices[0],draw.indices[1],draw.indices[2]}))continue;
            prefix("bsp-sky-geometry-draw");std::cout<<",\"policy\":"<<std::quoted(policy)<<",\"draw\":"<<d<<",\"vao\":"<<draw.vao<<",\"ebo\":"<<draw.ebo
                <<",\"program\":"<<draw.program<<",\"textures\":";list(draw.textures);
            std::cout<<",\"uniform_signature\":\""<<draw.uniforms<<"\",\"raster_signature\":\""<<draw.raster_state<<"\",\"samples_passed\":"<<draw.samples_passed<<",\"ranges\":[";
            bool comma=false;for(const auto& range:draw.index_ranges){if(comma)std::cout<<',';comma=true;std::cout<<"{\"byte_offset\":"<<range.byte_offset
                <<",\"index_count\":"<<range.count<<",\"stream_first\":"<<range.stream_first<<'}';}
            std::cout<<"],\"indices\":";list(draw.indices);std::cout<<"}\n";}};
    print_draws(a,"A");print_draws(c,"C");
    for(const auto& [key,sources]:catalog.sources)if(catalog.sky(key)){
        const auto ia=sa.multiplicities.find(key),ic=sc.multiplicities.find(key);
        prefix("bsp-sky-geometry-triangle");std::cout<<",\"indices\":";list(key);std::cout<<",\"render_surface_candidates\":";list(sources);
        std::cout<<",\"A_submissions\":"<<(ia==sa.multiplicities.end()?0U:ia->second)<<",\"C_submissions\":"<<(ic==sc.multiplicities.end()?0U:ic->second)<<"}\n";}
    const auto print_stream=[&](const Stream& s,const char* policy){prefix("bsp-sky-geometry-stream");
        std::cout<<",\"policy\":"<<std::quoted(policy)<<",\"source_surfaces\":"<<s.sources.size()<<",\"actual_submissions\":"<<s.sky_indices.size()/3U
            <<",\"unique_indexed_triangles\":"<<s.multiplicities.size()<<",\"repeated_indexed_submissions\":"<<s.sky_indices.size()/3U-s.multiplicities.size()
            <<",\"unique_geometric_triangles\":"<<s.geometric_multiplicities.size()<<",\"repeated_geometric_submissions\":"<<s.sky_indices.size()/3U-s.geometric_multiplicities.size()
            <<",\"ambiguous_source_triangles\":"<<s.ambiguous_source_triangles<<",\"draws\":"<<s.sky_draws<<",\"ranges\":"<<s.sky_ranges<<"}\n";};
    print_stream(sa,"A");print_stream(sc,"C");
    std::size_t common{},only_a{},only_c{},common_sources{},only_a_sources{},only_c_sources{},color_diff{},depth_diff{};
    for(const auto& [key,n]:sa.multiplicities){(void)n;if(sc.multiplicities.contains(key))++common;else ++only_a;}
    for(const auto& [key,n]:sc.multiplicities){(void)n;if(!sa.multiplicities.contains(key))++only_c;}
    for(const auto n:sa.sources){if(sc.sources.contains(n))++common_sources;else ++only_a_sources;}
    for(const auto n:sc.sources)if(!sa.sources.contains(n))++only_c_sources;
    REQUIRE(color_a.rgba8.size()==color_c.rgba8.size());REQUIRE(depth_a.size()==depth_c.size());
    for(std::size_t p=0;p<depth_a.size();++p){color_diff+=!std::equal(color_a.rgba8.begin()+p*4U,color_a.rgba8.begin()+p*4U+4U,color_c.rgba8.begin()+p*4U);
        depth_diff+=std::bit_cast<std::uint32_t>(depth_a[p])!=std::bit_cast<std::uint32_t>(depth_c[p]);}
    prefix("bsp-sky-geometry-proof");std::cout<<",\"common_unique_indexed_triangles\":"<<common<<",\"A_only_unique_indexed_triangles\":"<<only_a<<",\"C_only_unique_indexed_triangles\":"<<only_c
        <<",\"common_source_surfaces\":"<<common_sources<<",\"A_only_source_surfaces\":"<<only_a_sources<<",\"C_only_source_surfaces\":"<<only_c_sources
        <<",\"all_sky_sources\":"<<catalog.sky_surfaces.size()<<",\"all_sky_conservative_frustum_outside\":"<<outside_sky
        <<",\"predicted_legacy_sky_stream_exact\":"<<(predicted_indices==sa.sky_indices?"true":"false")
        <<",\"color_diff\":"<<color_diff<<",\"depth_diff\":"<<depth_diff<<",\"timed\":false}\n";
    CHECK(predicted_indices==sa.sky_indices);CHECK(color_diff==0U);CHECK(depth_diff==0U);CHECK(glGetError()==GL_NO_ERROR);
    std::cout.precision(old_precision);
}

// Separate, bounded C/E control: never reruns the preserved A/C admission dump
// or the rejected sky-order experiment. Raw sky draws remain independently
// joinable to the immutable source tuples emitted by the admission witness.
inline void compare_candidate(renderer::opengl::OpenGlRenderer& gpu,const renderer::RenderScene& full,
    renderer::RenderExtent extent,std::string_view view,bool capture,
    renderer::opengl::OpenGlStaticWorldSelectionPolicy restore_selection,
    renderer::opengl::OpenGlStaticWorldOrderPolicy restore_order) {
    using Selection=renderer::opengl::OpenGlStaticWorldSelectionPolicy;
    using Order=renderer::opengl::OpenGlStaticWorldOrderPolicy;
    trace::RestorePolicies restore{gpu,restore_selection,restore_order};
    auto scene=full;scene.dynamic_entities.reset();scene.first_person_entities.reset();
    REQUIRE(scene.static_world);REQUIRE(scene.static_world->scene_package);
    REQUIRE_FALSE(scene.static_world->visible_draw_list);REQUIRE(scene.requested_msaa_samples==0U);
    const auto owner=scene.static_world->scene_package->world_package();const auto& world=*owner;
    const auto identity=world.resource_identity();const auto vertex_hash=vertices_signature(world),index_hash=indices_signature(world.indices());
    const Catalog catalog{world};
    gpu.set_static_world_order_policy(Order::original);gpu.set_static_world_selection_policy(Selection::prepared_bsp);
    trace::Trace c;gpu.render(scene,extent);c.finish();
    const auto color_c=gpu.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_c);
    const auto depth_c=trace::read_depth(c.depth_target,extent);
    gpu.set_static_world_selection_policy(Selection::prepared_bsp_sky_frustum);
    trace::Trace e;gpu.render(scene,extent);e.finish();
    const auto color_e=gpu.observe_framebuffer(extent,scene.clear_color,true);REQUIRE(color_e);
    const auto depth_e=trace::read_depth(e.depth_target,extent);
    REQUIRE_FALSE(c.draws.empty());REQUIRE_FALSE(e.draws.empty());
    const auto vao=c.draws.front().vao;REQUIRE(e.draws.front().vao==vao);
    REQUIRE(scene.static_world->scene_package->world_package().get()==owner.get());
    REQUIRE(world.resource_identity()==identity);REQUIRE(vertices_signature(world)==vertex_hash);
    REQUIRE(indices_signature(world.indices())==index_hash);
    const auto group=[&](const trace::Trace& recorded,bool sky) {
        std::vector<trace::Triangle> result;
        for(const auto& triangle:trace::triangles(recorded,vao))
            if(catalog.sky(triangle.index)==sky)result.push_back(triangle);
        return result;
    };
    const auto ordinary_c=group(c,false),ordinary_e=group(e,false);
    const auto sky_c=group(c,true),sky_e=group(e,true);
    const bool ordinary_exact=ordinary_c==ordinary_e;
    std::size_t cursor{},matched{};
    for(const auto& triangle:sky_e){while(cursor<sky_c.size()&&!(sky_c[cursor]==triangle))++cursor;
        if(cursor==sky_c.size())break;++matched;++cursor;}
    const bool retained_sky_exact=matched==sky_e.size();
    const auto prefix=[&](const char* tag){std::cout<<'['<<tag<<"] {\"view\":"<<std::quoted(view)
        <<",\"remote_capture\":"<<(capture?"true":"false")<<",\"resource_id\":\""<<identity.resource_id
        <<"\",\"revision\":\""<<identity.revision<<'"';};
    const auto print_summary=[&](const trace::Trace& recorded,const char* policy) {
        std::size_t sky_draws{},ordinary_draws{},sky_triangles{},ordinary_triangles{},ranges{},program_changes{},texture_changes{},uniform_changes{},raster_changes{};
        std::uint64_t sky_samples{},ordinary_samples{};const trace::Draw* previous=nullptr;
        for(std::size_t d=0;d<recorded.draws.size();++d){const auto& draw=recorded.draws[d];
            if(draw.vao!=vao||draw.indices.empty())continue;
            const bool sky=catalog.sky({draw.indices[0],draw.indices[1],draw.indices[2]});
            for(std::size_t i=0;i<draw.indices.size();i+=3U)REQUIRE(catalog.sky({draw.indices[i],draw.indices[i+1U],draw.indices[i+2U]})==sky);
            if(sky){++sky_draws;sky_triangles+=draw.indices.size()/3U;sky_samples+=draw.samples_passed;}
            else {++ordinary_draws;ordinary_triangles+=draw.indices.size()/3U;ordinary_samples+=draw.samples_passed;}
            ranges+=draw.index_ranges.size();
            if(previous){program_changes+=draw.program!=previous->program;texture_changes+=draw.textures!=previous->textures;
                uniform_changes+=draw.uniforms!=previous->uniforms;raster_changes+=draw.raster_state!=previous->raster_state;}
            previous=&draw;
            if(!sky)continue;
            prefix("bsp-sky-geometry-draw");std::cout<<",\"policy\":"<<std::quoted(policy)<<",\"draw\":"<<d<<",\"vao\":"<<draw.vao<<",\"ebo\":"<<draw.ebo
                <<",\"program\":"<<draw.program<<",\"textures\":";list(draw.textures);
            std::cout<<",\"uniform_signature\":\""<<draw.uniforms<<"\",\"raster_signature\":\""<<draw.raster_state<<"\",\"samples_passed\":"<<draw.samples_passed<<",\"ranges\":[";
            bool first=true;for(const auto& range:draw.index_ranges){if(!first)std::cout<<',';first=false;std::cout<<"{\"byte_offset\":"<<range.byte_offset
                <<",\"index_count\":"<<range.count<<",\"stream_first\":"<<range.stream_first<<'}';}
            std::cout<<"],\"indices\":";list(draw.indices);std::cout<<"}\n";
        }
        prefix("bsp-sky-selection-summary");std::cout<<",\"policy\":"<<std::quoted(policy)<<",\"order\":\"original\",\"sky_draws\":"<<sky_draws
            <<",\"ordinary_draws\":"<<ordinary_draws<<",\"sky_triangles\":"<<sky_triangles<<",\"ordinary_triangles\":"<<ordinary_triangles
            <<",\"static_ranges\":"<<ranges<<",\"sky_samples_passed\":"<<sky_samples<<",\"ordinary_samples_passed\":"<<ordinary_samples
            <<",\"program_transitions\":"<<program_changes<<",\"texture_transitions\":"<<texture_changes
            <<",\"uniform_transitions\":"<<uniform_changes<<",\"raster_transitions\":"<<raster_changes
            <<",\"scope\":\"untimed_world_only_samples_not_shader_invocations\"}\n";
    };
    print_summary(c,"C");print_summary(e,"E");
    REQUIRE(color_c.rgba8.size()==color_e.rgba8.size());REQUIRE(depth_c.size()==depth_e.size());
    std::size_t color_diff{},depth_diff{};
    for(std::size_t p=0;p<depth_c.size();++p){color_diff+=!std::equal(color_c.rgba8.begin()+p*4U,color_c.rgba8.begin()+p*4U+4U,color_e.rgba8.begin()+p*4U);
        depth_diff+=std::bit_cast<std::uint32_t>(depth_c[p])!=std::bit_cast<std::uint32_t>(depth_e[p]);}
    prefix("bsp-sky-selection-comparison");std::cout<<",\"first\":\"C\",\"second\":\"E\",\"order\":\"original\",\"same_owner\":true"
        <<",\"vertex_float_word_fnv64\":\""<<vertex_hash<<"\",\"index_word_fnv64\":\""<<index_hash<<'"'
        <<",\"ordinary_geometry_order_and_state_exact\":"<<(ordinary_exact?"true":"false")
        <<",\"retained_sky_subsequence_and_state_exact\":"<<(retained_sky_exact?"true":"false")
        <<",\"C_sky_triangles\":"<<sky_c.size()<<",\"E_sky_triangles\":"<<sky_e.size()<<",\"matched_sky_triangles\":"<<matched
        <<",\"color_diff\":"<<color_diff<<",\"depth_diff\":"<<depth_diff<<",\"boundary\":\"completed_world_before_entities\",\"timed\":false}\n";
    CHECK(ordinary_exact);CHECK(retained_sky_exact);CHECK(color_diff==0U);CHECK(depth_diff==0U);CHECK(glGetError()==GL_NO_ERROR);
}
}
#endif

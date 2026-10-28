#pragma once

// Opt-in forensic controls for the rejected 71b97 visibility helper. All GL
// interception and readback below happens in a separate, untimed world-only
// render; no mutable access to production geometry or immutable draw lists.
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>
#include <glad/gl.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::tests::pvs_negative_diagnostics {

struct SurfaceDrawFilter {
    inline static SurfaceDrawFilter* active{};
    PFNGLDRAWELEMENTSPROC saved{};
    std::vector<const world_render::WorldRenderSurfaceRange*> ordered;
    std::span<const std::uint8_t> keep;
    std::size_t drawn_surfaces{}, skipped_surfaces{};
    bool unexpected_range{};

    static void GLAD_API_PTR draw(GLenum mode, GLsizei count, GLenum type,
        const void* indices)
    {
        auto& self=*active;
        const auto offset=reinterpret_cast<std::uintptr_t>(indices);
        if(mode!=GL_TRIANGLES||type!=GL_UNSIGNED_INT||count<=0||offset%4U!=0U) {
            self.unexpected_range=true; self.saved(mode,count,type,indices); return;
        }
        const auto first=offset/4U;
        const auto end=first+static_cast<std::size_t>(count);
        auto it=std::lower_bound(self.ordered.begin(),self.ordered.end(),first,
            [](const auto* surface,std::size_t value){return surface->first_index<value;});
        auto cursor=first;
        for(;it!=self.ordered.end()&&(*it)->first_index<end;++it) {
            const auto& surface=**it;
            if(surface.first_index!=cursor||surface.index_count>end-cursor||
                surface.source_world_surface_index>=self.keep.size()) {
                self.unexpected_range=true; return;
            }
            if(self.keep[surface.source_world_surface_index]) {
                self.saved(mode,static_cast<GLsizei>(surface.index_count),type,
                    reinterpret_cast<const void*>(std::uintptr_t{surface.first_index}*4U));
                ++self.drawn_surfaces;
            } else ++self.skipped_surfaces;
            cursor+=surface.index_count;
        }
        self.unexpected_range|=cursor!=end;
    }
    SurfaceDrawFilter(const world_render::WorldRenderPackage& package,
        std::span<const std::uint8_t> selection):keep{selection}
    {
        REQUIRE(active==nullptr);
        for(const auto& surface:package.surface_ranges())ordered.push_back(&surface);
        std::sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->first_index<b->first_index;});
        saved=glad_glDrawElements;active=this;glad_glDrawElements=&draw;
    }
    ~SurfaceDrawFilter(){glad_glDrawElements=saved;active=nullptr;}
};

inline std::vector<float> read_default_depth(renderer::RenderExtent size)
{
    GLint target{},pack{},alignment{},row{},x{},y{};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&target);REQUIRE(target==0);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);
    glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&x);glGetIntegerv(GL_PACK_SKIP_ROWS,&y);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
    std::vector<float> depth(static_cast<std::size_t>(size.width)*size.height);
    glReadPixels(0,0,size.width,size.height,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
    const auto error=glGetError();
    glBindBuffer(GL_PIXEL_PACK_BUFFER,static_cast<GLuint>(pack));glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH,row);glPixelStorei(GL_PACK_SKIP_PIXELS,x);glPixelStorei(GL_PACK_SKIP_ROWS,y);
    REQUIRE(error==GL_NO_ERROR);return depth;
}

inline void diagnose(renderer::opengl::OpenGlRenderer& renderer,
    const renderer::RenderScene& original,renderer::RenderExtent size,
    std::string_view view,bool capture,
    const world_visibility::WorldVisibleDrawList& rejected,
    std::span<const std::uint8_t> baseline_color,std::span<const float> baseline_depth,
    std::span<const std::uint8_t> rejected_color,std::span<const float> rejected_depth)
{
    REQUIRE(original.static_world);REQUIRE(original.static_world->scene_package);
    REQUIRE_FALSE(original.static_world->visible_draw_list);
    const auto& map=*original.static_world->scene_package;
    const auto& package=*map.world_package();
    const auto count=package.surface_ranges().size();
    std::vector<std::uint8_t> all(count,1U),old(count,0U),sky(count,0U);
    for(const auto& command:rejected.commands()) {
        REQUIRE(command.object_kind==world_visibility::WorldVisibleObjectKind::world_surface);
        REQUIRE(command.source_surface_index<count);old[command.source_surface_index]=1U;
    }
    std::vector<world_visibility::WorldVisibilityBrushInstanceInput> brushes;
    for(const auto& b:map.brush_instances())brushes.push_back({b.source_instance_index,b.transformed_bounds,
        b.touched_leaf_indices,b.support_status==world_scene_render::BrushSubmodelRenderSupportStatus::supported_static_opaque&&b.source_model_index.has_value()});
    const auto resolve=[&](world_visibility::WorldVisibilityMode mode,const renderer::RenderCamera& camera) {
        const world_visibility::WorldVisibilityResolveInput input{&map.spatial_package(),map.world_surfaces(),brushes,camera,size,
            mode,world_visibility::WorldPvsFallbackPolicy::frustum_only,1,map.visibility_scene_identity(),false};
        auto result=world_visibility::WorldVisibilityResolver{}.resolve(input);REQUIRE(result);REQUIRE(result.visibility);
        std::cout<<"[pvs-negative-context] {\"view\":"<<std::quoted(view)
            <<",\"mode\":"<<std::quoted(world_visibility::to_string(mode))
            <<",\"eye\":["<<camera.position.x<<','<<camera.position.y<<','<<camera.position.z<<']'
            <<",\"leaf\":"<<result.visibility->camera_leaf_index().value_or(0U)
            <<",\"fallback\":"<<std::quoted(world_visibility::to_string(result.visibility->fallback_reason()))
            <<",\"surfaces\":"<<result.visibility->visible_world_surface_indices().size()<<"}\n";
        std::vector<std::uint8_t> mask(count,0U);
        for(auto surface:result.visibility->visible_world_surface_indices()){REQUIRE(surface<count);mask[surface]=1U;}
        return mask;
    };
    std::cout<<"[pvs-negative-surfaces] {\"view\":"<<std::quoted(view)<<",\"omitted_sky_source_ids\":[";
    bool comma=false;
    for(const auto& surface:package.surface_ranges())if(package.materials()[surface.render_material_index].sky_surface) {
        sky[surface.source_world_surface_index]=1U;
        if(!old[surface.source_world_surface_index]){if(comma)std::cout<<',';comma=true;std::cout<<surface.source_world_surface_index;}
    }
    std::cout<<"]}\n";
    const auto compare=[&](std::string_view label,const std::vector<std::uint8_t>& keep) {
        std::size_t drawn{},skipped{};
        {SurfaceDrawFilter filter{package,keep};renderer.render(original,size);
            REQUIRE_FALSE(filter.unexpected_range);drawn=filter.drawn_surfaces;skipped=filter.skipped_surfaces;}
        const auto color=renderer.observe_framebuffer(size,original.clear_color,true);REQUIRE(color);
        const auto depth=read_default_depth(size);REQUIRE(color.rgba8.size()==baseline_color.size());
        std::size_t color_diff{},depth_diff{},old_color_diff{},old_depth_diff{},holes{},extra{};
        std::uint32_t max_ulp{};
        for(std::size_t p=0;p<depth.size();++p) {
            const auto rgba=p*4U;
            color_diff+=!std::equal(color.rgba8.begin()+rgba,color.rgba8.begin()+rgba+4U,baseline_color.begin()+rgba);
            old_color_diff+=!std::equal(color.rgba8.begin()+rgba,color.rgba8.begin()+rgba+4U,rejected_color.begin()+rgba);
            const auto a=std::bit_cast<std::uint32_t>(depth[p]),b=std::bit_cast<std::uint32_t>(baseline_depth[p]);
            depth_diff+=a!=b;old_depth_diff+=a!=std::bit_cast<std::uint32_t>(rejected_depth[p]);
            max_ulp=std::max(max_ulp,a>b?a-b:b-a);holes+=depth[p]>=1.F&&baseline_depth[p]<1.F;
            extra+=depth[p]<1.F&&baseline_depth[p]>=1.F;
        }
        std::cout<<"[pvs-negative-cause] {\"view\":"<<std::quoted(view)<<",\"remote_capture\":"<<(capture?"true":"false")
            <<",\"control\":"<<std::quoted(label)<<",\"color_vs_baseline\":"<<color_diff<<",\"depth_vs_baseline\":"<<depth_diff
            <<",\"color_vs_rejected\":"<<old_color_diff<<",\"depth_vs_rejected\":"<<old_depth_diff
            <<",\"missing_depth\":"<<holes<<",\"extra_depth\":"<<extra<<",\"max_depth_ulp\":"<<max_ulp
            <<",\"drawn_surfaces\":"<<drawn<<",\"skipped_surfaces\":"<<skipped<<"}\n";
    };
    compare("split_all_original_order",all);
    compare("old_subset_original_order",old);
    auto old_and_sky=old;for(std::size_t i=0;i<count;++i)old_and_sky[i]|=sky[i];
    compare("old_subset_plus_all_sky",old_and_sky);
    compare("frustum_only_original_order",resolve(world_visibility::WorldVisibilityMode::frustum_only,original.camera));
    compare("pvs_only_original_order",resolve(world_visibility::WorldVisibilityMode::pvs_only,original.camera));
    auto nearby=old;
    for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1}) {
        auto camera=original.camera;const auto d=camera.near_plane;
        camera.position.x+=static_cast<float>(x)*d;camera.position.y+=static_cast<float>(y)*d;camera.position.z+=static_cast<float>(z)*d;
        camera.target.x+=static_cast<float>(x)*d;camera.target.y+=static_cast<float>(y)*d;camera.target.z+=static_cast<float>(z)*d;
        const auto mask=resolve(world_visibility::WorldVisibilityMode::pvs_only,camera);
        for(std::size_t i=0;i<count;++i)nearby[i]|=mask[i];
    }
    compare("near_plane_cube_pvs_union_original_order",nearby);
    renderer.render(original,size); // Restore exact untimed baseline state.
    REQUIRE(glGetError()==GL_NO_ERROR);
}
} // namespace hlclient::tests::pvs_negative_diagnostics

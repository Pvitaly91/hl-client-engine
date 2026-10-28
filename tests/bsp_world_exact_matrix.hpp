#pragma once
#ifdef HLCLIENT_BSP_CANDIDATE
#include "pvs_negative_control_diagnostics.hpp"
#include "bsp_world_causal_trace.hpp"
#include <chrono>
#include <cmath>
#include <thread>

namespace hlclient::tests::bsp_world_exact_matrix {
inline void compare(renderer::opengl::OpenGlRenderer& renderer,
    const renderer::RenderScene& full_scene,renderer::RenderExtent extent,
    std::string_view view,bool capture,std::uint64_t& frame_id,
    renderer::opengl::OpenGlStaticWorldSelectionPolicy restore_selection,
    renderer::opengl::OpenGlStaticWorldOrderPolicy restore_order,bool sky_order_pairs=false,
    bool sky_selection_pairs=false)
{
    using Policy=renderer::opengl::OpenGlStaticWorldSelectionPolicy;
    using Order=renderer::opengl::OpenGlStaticWorldOrderPolicy;
    REQUIRE_FALSE((sky_order_pairs&&sky_selection_pairs));
    bsp_world_causal_trace::RestorePolicies restore{renderer,restore_selection,restore_order};
    renderer::RenderScene base;
    base.camera=full_scene.camera;base.static_world=full_scene.static_world;
    base.skybox=full_scene.skybox;base.screen_effects=full_scene.screen_effects;
    base.clear_color=full_scene.clear_color;base.image_adjustments=full_scene.image_adjustments;
    base.world_effect_lights=full_scene.world_effect_lights;
    base.world_effect_light_count=full_scene.world_effect_light_count;
    base.transient_world_light=full_scene.transient_world_light;
    REQUIRE(base.static_world);REQUIRE(base.static_world->scene_package);
    base.static_world->visible_draw_list.reset();base.static_world->visibility_summary.reset();
    const auto map=base.static_world->scene_package;
    struct Case {std::string_view label;renderer::RenderScene scene;renderer::RenderExtent size;bool reset{};bool expect_invalid_pvs{};};
    std::vector<Case> cases;
    const auto add=[&](std::string_view label){cases.push_back({label,base,extent,false});};
    add("base");
    add("same_leaf_yaw45");
    auto& yaw=cases.back().scene.camera;
    const float fx=yaw.target.x-yaw.position.x,fy=yaw.target.y-yaw.position.y;
    constexpr float diagonal=.7071067811865475F;
    yaw.target.x=yaw.position.x+(fx-fy)*diagonal;yaw.target.y=yaw.position.y+(fx+fy)*diagonal;
    add("same_leaf_pitch_up");cases.back().scene.camera.target.z+=.35F;
    add("narrow_fov");cases.back().scene.camera.vertical_field_of_view_radians=.65F;
    add("wide_fov");cases.back().scene.camera.vertical_field_of_view_radians=1.8F;
    add("aspect_4_3");cases.back().size={1440,1080};
    add("aspect_21_9");cases.back().size={1920,823};
    add("final_shake_roll");cases.back().scene.screen_effects.translation={.5F,-.25F,1.F};
    cases.back().scene.screen_effects.roll_degrees=15.;
    add("near_plane_half_unit");cases.back().scene.camera.near_plane=.5F;
    add("boundary_down");cases.back().scene.camera.position.z-=.125F;cases.back().scene.camera.target.z-=.125F;
    add("boundary_up");cases.back().scene.camera.position.z+=.125F;cases.back().scene.camera.target.z+=.125F;
    add("backface_enabled");cases.back().scene.static_world->cull_mode=renderer::RenderCullMode::back;
    add("spatial_package_absent");cases.back().scene.static_world->scene_package.reset();
    cases.back().scene.static_world->runtime_brushes.reset();
    // Retain the scene and all geometry/brushes while invalidating ONLY PVS.
    // Unlike spatial_package_absent, these must execute the new BSP path
    // with its conservative invalid/absent-PVS fallback, not bypass it.
    for(const bool malformed:{false,true}) {
        const auto& original=map->spatial_package();
        const auto& original_pvs=original.pvs_table();
        world_spatial::WorldPvsTable pvs;
        if(malformed) {
            REQUIRE(original_pvs.unique_row_count()>0U);
            std::vector<std::vector<std::byte>> rows;
            for(std::uint32_t i=0U;i<original_pvs.unique_row_count();++i) {
                const auto row=original_pvs.row(i);REQUIRE(row);
                rows.emplace_back(row->begin(),row->end());
            }
            rows.front().clear();
            const auto leaf_rows=original_pvs.leaf_row_indices();
            pvs=world_spatial::WorldPvsTable{original_pvs.row_byte_count(),
                original_pvs.visible_leaf_count(),std::move(rows),
                {leaf_rows.begin(),leaf_rows.end()},original_pvs.all_visible_row_index()};
        }
        world_spatial::WorldSpatialPackage spatial{
            {original.planes().begin(),original.planes().end()},
            {original.nodes().begin(),original.nodes().end()},
            {original.leaves().begin(),original.leaves().end()},std::move(pvs),
            original.world_model(),original.statistics(),original.compatibility_profile(),original.evidence_profile()};
        auto fallback_scene=world_scene_render::WorldSceneRenderPackageBuilder{}.build(
            map->world_package(),std::move(spatial),map->brush_library(),
            {map->brush_instances().begin(),map->brush_instances().end()});
        REQUIRE(fallback_scene);REQUIRE(fallback_scene.package);
        auto replacement=std::make_shared<const world_scene_render::WorldSceneRenderPackage>(
            std::move(*fallback_scene.package));
        add(malformed?"malformed_pvs_same_geometry":"absent_pvs_same_geometry");
        auto& test=cases.back();test.expect_invalid_pvs=true;
        test.scene.static_world->scene_package=replacement;
        if(test.scene.static_world->runtime_brushes) {
            auto runtime=std::make_shared<world_scene_render::RuntimeBrushRenderFrame>(
                *test.scene.static_world->runtime_brushes);
            runtime->scene_identity=replacement->resource_identity();
            test.scene.static_world->runtime_brushes=std::move(runtime);
        }
    }
    auto rebuilt=world_scene_render::WorldSceneRenderPackageBuilder{}.build(
        map->world_package(),map->spatial_package(),map->brush_library(),
        std::vector<world_scene_render::BrushSubmodelRenderInstance>{map->brush_instances().begin(),map->brush_instances().end()});
    REQUIRE(rebuilt);REQUIRE(rebuilt.package);
    auto other=std::make_shared<const world_scene_render::WorldSceneRenderPackage>(std::move(*rebuilt.package));
    add("replacement_scene_same_geometry");cases.back().scene.static_world->scene_package=other;
    cases.back().scene.static_world->runtime_brushes.reset();
    add("reuse_original_scene");
    if(!map->brush_library().models().empty()) {
        const auto& model=map->brush_library().models().front();
        const auto& bounds=model.local_bounds();
        const auto& c=base.camera;
        const auto forward=renderer::normalize({c.target.x-c.position.x,c.target.y-c.position.y,c.target.z-c.position.z});
        REQUIRE(forward);
        for(unsigned step=0;step<2U;++step) {
            auto brushes=std::make_shared<world_scene_render::RuntimeBrushRenderFrame>();
            brushes->scene_identity=map->resource_identity();brushes->generation=1;brushes->revision=step+1U;
            world_scene_render::RuntimeBrushRenderInstance instance;
            instance.entity_number=100;instance.model_slot=10;instance.source_model_index=model.source_model_index();
            const assets::AssetVector3 translation{
                c.position.x+forward->x*160.F-(bounds.minimum.x+bounds.maximum.x)*.5F,
                c.position.y+forward->y*160.F-(bounds.minimum.y+bounds.maximum.y)*.5F,
                c.position.z+forward->z*160.F-(bounds.minimum.z+bounds.maximum.z)*.5F+static_cast<float>(step)*24.F};
            instance.model_transform.values[12]=translation.x;instance.model_transform.values[13]=translation.y;
            instance.model_transform.values[14]=translation.z;
            instance.transformed_bounds=bounds;
            instance.transformed_bounds.minimum.x+=translation.x;instance.transformed_bounds.maximum.x+=translation.x;
            instance.transformed_bounds.minimum.y+=translation.y;instance.transformed_bounds.maximum.y+=translation.y;
            instance.transformed_bounds.minimum.z+=translation.z;instance.transformed_bounds.maximum.z+=translation.z;
            brushes->instances.push_back(instance);REQUIRE(world_scene_render::valid_runtime_brush_frame(*brushes,*map));
            add(step==0U?"brush_at_view":"brush_moved");cases.back().scene.static_world->runtime_brushes=brushes;
        }
    }
    add("reset_reconnect_resources");cases.back().reset=true;
    if(sky_order_pairs||sky_selection_pairs)cases.push_back({"full_frame",full_scene,extent,false,false});
    std::size_t bsp_frames{};
    for(const auto& test:cases) {
      for(unsigned pair=0;pair<((sky_order_pairs||sky_selection_pairs)?2U:1U);++pair) {
        const auto first_policy=pair==1U?Policy::prepared_bsp:Policy::legacy_prepared_frustum;
        const auto second_policy=sky_selection_pairs?Policy::prepared_bsp_sky_frustum:
            sky_order_pairs?first_policy:Policy::prepared_bsp;
        CAPTURE(view,capture,test.label);
        if(test.reset)renderer.reset_session_resources();
        renderer.set_static_world_selection_policy(first_policy);
        renderer.set_static_world_order_policy(Order::original);
        GLint first_depth_target{};
        {bsp_world_causal_trace::DepthTargetCapture target;
          renderer.render_with_gpu_timing(test.scene,test.size,frame_id++,true);first_depth_target=target.target;}
        auto legacy=renderer.statistics().last_static_world_frame;
        // The retained no-scene-package renderer has no static selector
        // receipt; that is unavailable instrumentation, not a render failure.
        if(!test.scene.static_world->scene_package) {CHECK_FALSE(legacy);legacy.emplace();}
        REQUIRE(legacy);
        const auto a=renderer.observe_framebuffer(test.size,test.scene.clear_color,true);REQUIRE(a);
        const auto da=bsp_world_causal_trace::read_depth(first_depth_target,test.size);
        renderer.set_static_world_selection_policy(second_policy);
        renderer.set_static_world_order_policy(sky_order_pairs?Order::defer_safe_sky:Order::original);
        GLint second_depth_target{};
        {bsp_world_causal_trace::DepthTargetCapture target;
          renderer.render_with_gpu_timing(test.scene,test.size,frame_id++,true);second_depth_target=target.target;}
        auto candidate=renderer.statistics().last_static_world_frame;
        if(!test.scene.static_world->scene_package) {CHECK_FALSE(candidate);candidate.emplace();}
        REQUIRE(candidate);
        if(test.expect_invalid_pvs&&second_policy!=Policy::legacy_prepared_frustum) {
            CHECK(candidate->path==renderer::opengl::OpenGlStaticWorldPath::prepared_bsp);
            CHECK(candidate->fallback==renderer::opengl::OpenGlStaticWorldFallback::pvs_conservative);
            CHECK(candidate->pvs_fallback_code==2U);
        }
        const auto b=renderer.observe_framebuffer(test.size,test.scene.clear_color,true);REQUIRE(b);
        const auto db=bsp_world_causal_trace::read_depth(second_depth_target,test.size);
        REQUIRE(a.rgba8.size()==b.rgba8.size());REQUIRE(da.size()==db.size());
        std::size_t color_diff{},depth_diff{};
        for(std::size_t p=0;p<da.size();++p) {
            color_diff+=!std::equal(a.rgba8.begin()+p*4U,a.rgba8.begin()+p*4U+4U,b.rgba8.begin()+p*4U);
            depth_diff+=std::bit_cast<std::uint32_t>(da[p])!=std::bit_cast<std::uint32_t>(db[p]);
        }
        bsp_frames+=candidate->path==renderer::opengl::OpenGlStaticWorldPath::prepared_bsp;
        if(sky_order_pairs) {
            CHECK(legacy->submitted_triangles==candidate->submitted_triangles);
            CHECK(legacy->selected_surfaces==candidate->selected_surfaces);
        }
        std::cout<<(sky_selection_pairs?"[bsp-sky-selection-exact] ":sky_order_pairs?"[bsp-sky-order-exact] ":"[bsp-world-exact] ")
            <<"{\"view\":"<<std::quoted(view)<<",\"remote_capture\":"<<(capture?"true":"false")
            <<",\"selection\":"<<std::quoted(sky_selection_pairs?(pair==0U?"A_to_E":"C_to_E"):
                sky_order_pairs?(first_policy==Policy::prepared_bsp?"bsp":"legacy"):"legacy_to_bsp")
            <<",\"first_order\":\"original\",\"second_order\":"<<std::quoted(sky_order_pairs?"deferred":"original")
            <<",\"boundary\":"<<std::quoted(test.label=="full_frame"?"completed_frame_including_viewmodel_depth_reset":"completed_world_only_frame")
            <<",\"depth_target\":\"owned_scene_target\",\"timed\":false"
            <<",\"case\":"<<std::quoted(test.label)<<",\"color_diff\":"<<color_diff<<",\"depth_diff\":"<<depth_diff
            <<",\"width\":"<<test.size.width<<",\"height\":"<<test.size.height
            <<",\"legacy_path\":"<<static_cast<unsigned>(legacy->path)<<",\"candidate_path\":"<<static_cast<unsigned>(candidate->path)
            <<",\"candidate_fallback\":"<<static_cast<unsigned>(candidate->fallback)
            <<",\"pvs_fallback_code\":"<<(candidate->pvs_fallback_code?std::to_string(*candidate->pvs_fallback_code):"null")
            <<",\"legacy_triangles\":"<<legacy->submitted_triangles.value_or(0U)
            <<",\"candidate_triangles\":"<<candidate->submitted_triangles.value_or(0U)
            <<",\"first_triangles\":"<<(legacy->submitted_triangles?std::to_string(*legacy->submitted_triangles):"null")
            <<",\"second_triangles\":"<<(candidate->submitted_triangles?std::to_string(*candidate->submitted_triangles):"null")
            <<",\"candidate_surfaces\":"<<candidate->submitted_surfaces.value_or(0U)
            <<",\"pvs_cache_hits\":"<<candidate->pvs_cache_hits.value_or(0U)
            <<",\"pvs_rebuilds\":"<<candidate->pvs_rebuilds.value_or(0U)<<"}\n";
        CHECK(color_diff==0U);CHECK(depth_diff==0U);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(renderer.gpu_timing_statistics().pending&&std::chrono::steady_clock::now()<deadline) {
            while(renderer.poll_gpu_timing()){}
            if(renderer.gpu_timing_statistics().pending)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(renderer.gpu_timing_statistics().pending==0U);
      }
    }
    CHECK(bsp_frames>0U);
    REQUIRE(glGetError()==GL_NO_ERROR);
}
} // namespace hlclient::tests::bsp_world_exact_matrix
#endif

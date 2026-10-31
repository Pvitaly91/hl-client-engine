#pragma once
// Included inside the fixture's private namespace, after the unchanged legacy
// correctness implementation. Uses the SAME byte-exact sample extractor.
namespace eligibility_regressions {
namespace ws=hlclient::world_scene_render;
namespace spatial=hlclient::world_spatial;
namespace visibility=hlclient::world_visibility;

std::shared_ptr<const ws::WorldSceneRenderPackage> map_for(
    const std::shared_ptr<const wr::WorldRenderPackage>& world,
    const std::shared_ptr<const wr::WorldRenderPackage>& brush,
    std::vector<ws::BrushSubmodelRenderModel> models) {
    const a::WorldBounds bounds{{-100000,-100000,-100000},{100000,100000,100000}};
    spatial::WorldSpatialNode node;node.children={spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,2},
        spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,1}};node.bounds=bounds;
    std::vector<spatial::WorldSpatialLeaf> leaves(3);
    for(std::uint32_t i=0;i<3;++i) {
        auto& leaf=leaves[i];leaf.source_leaf_index=i;leaf.bounds=bounds;leaf.surface_membership.source_leaf_index=i;
        leaf.solid_or_special=i==0;if(!i)continue;leaf.pvs_row_index=0;leaf.pvs_bit_addressable=true;
        leaf.surface_membership.source_marksurface_count=static_cast<std::uint32_t>(world->surface_ranges().size());
        for(std::uint32_t surface=0;surface<world->surface_ranges().size();++surface)
            leaf.surface_membership.world_surface_indices.push_back(surface);
    }
    spatial::WorldSpatialPackage spatial_package{{spatial::WorldSpatialPlane{{1,0,0},.25F,0}},{node},std::move(leaves),
        spatial::WorldPvsTable{1,2,{{std::byte{3}}},{std::nullopt,0,0},0},{0,2,bounds},
        {1,1,3,2*world->surface_ranges().size(),2*world->surface_ranges().size(),1,1},
        spatial::WorldSpatialCompatibilityProfile::goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records};
    auto built=ws::WorldSceneRenderPackageBuilder{}.build(world,std::move(spatial_package),
        ws::BrushSubmodelRenderLibrary{brush,std::move(models)},{});
    need(bool(built),"Regression map rejected");
    return std::make_shared<const ws::WorldSceneRenderPackage>(std::move(*built.package));
}
void select_world(r::RenderScene& scene,visibility::WorldVisibilityMode mode,r::RenderExtent extent) {
    auto& world=*scene.static_world;const auto& map=*world.scene_package;
    std::vector<visibility::WorldVisibleBrushModelInput> models;
    const auto& library=map.brush_library();
    if(library.render_package())for(const auto& model:library.models())models.push_back({model.source_model_index(),
        library.render_package()->indices().size(),library.render_package()->materials().size(),model.surfaces()});
    const visibility::WorldVisibilityResolveInput input{&map.spatial_package(),map.world_surfaces(),{},
        scene.camera,extent,mode,visibility::WorldPvsFallbackPolicy::frustum_only,1,map.visibility_scene_identity(),false};
    auto resolved=visibility::WorldVisibilityResolver{}.resolve(input);need(bool(resolved),"Regression visibility rejected");
    const auto& selected=*resolved.visibility;
    auto built=visibility::WorldVisibleDrawListBuilder{}.build({&selected,map.world_surfaces(),
        world.package->indices().size(),world.package->materials().size(),models,{}});
    need(bool(built),"Regression draw list rejected");
    world.visible_draw_list=std::make_shared<const visibility::WorldVisibleDrawList>(std::move(*built.draw_list));
    const auto identity=selected.scene_identity();const auto signature=selected.result_signature();
    world.visibility_summary=r::RenderStaticWorldVisibilitySummary{selected.revision(),identity.resource_id,identity.revision,
        identity.visibility_input_signature,identity.draw_input_signature,signature.first,signature.second,
        selected.statistics().visible_world_surface_count,selected.statistics().visible_brush_instance_count};
}
void add_models(r::RenderScene& scene,bool second_model,bool far_transform,bool culled_instance) {
    auto brush=package({Quad{-.3F,.3F,-.3F,.3F,0},Quad{-.3F,.3F,-.3F,.3F,30}});
    std::vector<ws::BrushSubmodelRenderModel> models;
    models.emplace_back(1,a::WorldBounds{{-.3F,0,-.3F},{.3F,0,.3F}},std::vector<std::uint32_t>{0});
    models.emplace_back(2,a::WorldBounds{{-.3F,30,-.3F},{.3F,30,.3F}},std::vector<std::uint32_t>{1});
    auto map=map_for(scene.static_world->package,brush,std::move(models));
    auto frame=std::make_shared<ws::RuntimeBrushRenderFrame>();frame->scene_identity=map->resource_identity();frame->generation=frame->revision=1;
    ws::RuntimeBrushRenderInstance first;first.entity_number=100;first.model_slot=10;first.source_model_index=1;
    first.model_transform.values[12]=-.35F;first.model_transform.values[13]=-.25F;
    first.transformed_bounds={{-.65F,-.25F,-.3F},{-.05F,-.25F,.3F}};frame->instances.push_back(first);
    if(second_model || culled_instance) {
        ws::RuntimeBrushRenderInstance next;next.entity_number=101;next.model_slot=11;next.source_model_index=2;
        const float y=far_transform?17.95F:culled_instance?200.0F:.15F;
        next.model_transform.values[12]=.35F;next.model_transform.values[13]=y-30.0F;
        next.transformed_bounds={{.05F,y,-.3F},{.65F,y,.3F}};
        if(far_transform) {
            // Rotate the ACTUAL model plane across the far plane, not merely
            // its conservative bounds. A plane wholly at/beyond far is culled
            // by the real instance selector and cannot test submitted work.
            constexpr float c=.7071067811865476F;
            next.model_transform.values[5]=c;next.model_transform.values[6]=c;
            next.model_transform.values[9]=-c;next.model_transform.values[10]=c;
            next.model_transform.values[13]=y-30.0F*c;next.model_transform.values[14]=-30.0F*c;
            next.transformed_bounds={{.05F,y-.3F*c,-.3F*c},{.65F,y+.3F*c,.3F*c}};
        }
        frame->instances.push_back(next);
    }
    need(ws::valid_runtime_brush_frame(*frame,*map),"Regression runtime frame rejected");
    scene.static_world->scene_package=std::move(map);scene.static_world->runtime_brushes=std::move(frame);
}
r::RenderScene scene(std::string_view name,unsigned samples) {
    auto result=scene_for("open",samples,true);
    if(name=="unsubmitted_far_range" || name=="submitted_far_range") {
        result.static_world->package=package({Quad{-.5F,.5F,-.5F,.5F,0},Quad{-.5F,.5F,-.5F,.5F,200}});
        result.static_world->scene_package=map_for(result.static_world->package,{},{});
        select_world(result,name=="unsubmitted_far_range"?visibility::WorldVisibilityMode::frustum_only:visibility::WorldVisibilityMode::all,{96,96});
        need(result.static_world->visible_draw_list->commands().size()==(name=="unsubmitted_far_range"?1U:2U),"Incorrect selected range inventory");
    } else if(name=="partial_far_clip") {
        result.camera.position={-2,-2,0};result.camera.target={0,0,0};result.camera.far_plane=10;
        result.static_world->package=package({Quad{0,16,-.5F,.5F,0}});
    } else if(name=="union_aabb_only") {
        result.camera.position={-2,-2,0};result.camera.target={0,0,0};
        result.static_world->package=package({Quad{-.5F,.5F,-.5F,.5F,0},Quad{19.5F,20.5F,-.5F,.5F,-20},Quad{-20.5F,-19.5F,-.5F,.5F,20}});
        const auto vp=r::camera_view_projection(result.camera,{96,96});need(bool(vp),"Union camera rejected");
        need(!gl::sky_depth_guard(result.static_world->package->bounds(),*vp.matrix,24),"Union AABB must reproduce old false rejection");
        for(const auto& range:result.static_world->package->surface_ranges())need(gl::sky_depth_guard(range.bounds,*vp.matrix,24),"Each actual surface must be safe");
    } else if(name=="brush_model_isolation")add_models(result,false,false,false);
    else if(name=="brush_model_transforms")add_models(result,true,false,false);
    else if(name=="brush_far_transform")add_models(result,true,true,false);
    else if(name=="brush_culled_instance")add_models(result,false,false,true);
    else if(name=="fragmented_full_batch") {
        std::vector<Quad> quads;
        // Each immutable block contains16source surfaces. Six disjoint visible
        // runs force accepted renderer's >4-run full-batch fallback. Hidden
        // blocks include actually submitted far-plane geometry after fallback.
        for(unsigned block=0;block<11;++block)for(unsigned surface=0;surface<16;++surface) {
            const bool hidden=(block%2)==1;const float x=hidden?100.0F:0.0F;
            const float y=hidden?18.0F:0.0F;
            quads.push_back(Quad{x-.4F,x+.4F,-.4F,.4F,y});
        }
        result.static_world->package=package(quads,true);
        need(result.static_world->package->draw_batches().size()==1 && result.static_world->package->prepared_draw_blocks().size()==11,"Fragmented fixture did not produce11blocks in1batch");
        result.static_world->scene_package=map_for(result.static_world->package,{},{});
    }
    return result;
}

// This is an explicit, test-only state injection at the actual pre-clear
// boundary. Real GL setters are used; no API return values or guard results
// are spoofed. Capture restores its own observer state, as in legacy cases.
struct ClearStateInjection {
    inline static ClearStateInjection* active{};
    PFNGLCLEARDEPTHPROC original=glad_glClearDepth;std::string_view kind;bool fired{};
    explicit ClearStateInjection(std::string_view value):kind(value){need(!active,"Nested injection");active=this;glad_glClearDepth=&call;}
    ~ClearStateInjection(){glad_glClearDepth=original;active=nullptr;}
    static void GLAD_API_PTR call(GLdouble value) {
        auto& self=*active;self.original(value);if(self.fired)return;self.fired=true;
        if(self.kind=="unsafe_clear_mask")glDepthMask(GL_FALSE);
        else if(self.kind=="unsafe_clear_scissor"){glEnable(GL_SCISSOR_TEST);glScissor(11,7,40,55);}
        else if(self.kind=="unsafe_clear_color")glColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_TRUE);
    }
};
constexpr std::array names{"core_polygon_query","polygon_line","inherited_always","inherited_greater","unsafe_clear_mask","unsafe_clear_scissor","unsafe_clear_color",
    "unsubmitted_far_range","submitted_far_range","partial_far_clip","union_aabb_only","brush_model_isolation","brush_model_transforms",
    "brush_far_transform","brush_culled_instance","fragmented_full_batch","camera_fov_extent_change","resource_replacement"};
void run(const Options& o,gl::OpenGlRenderer& gpu) {
    Extractor extractor;std::ofstream manifest(o.output/L"regressions.json");manifest<<std::setprecision(17);
    manifest<<"{\"schema\":\"hlc.sky.eligibility-regressions.v1\",\"status\":\"ARTIFACTS_REQUIRE_EXACT_AB_COMPARISON\",";hardware(manifest,gpu);
    manifest<<",\"boundaries\":\"same legacy extractor; per-sample color/depth before viewmodel clear and after scene plus final default color\",\"cases\":[";
    bool comma{};
    for(const unsigned samples:std::array<unsigned,2>{0,4})for(const auto* name:names) {
        const std::string_view n=name;int w=96,h=96;auto s=scene(n,samples);
        const auto dir=o.output/(std::string(name)+"-msaa"+std::to_string(samples));std::filesystem::create_directory(dir);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDepthRange(0,1);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glDepthFunc(GL_LEQUAL);glDisable(GL_SCISSOR_TEST);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        gpu.render(s,{w,h});
        if(n=="camera_fov_extent_change") {s.camera.position={.15F,-2.3F,.1F};s.camera.target={.1F,0,0};s.camera.vertical_field_of_view_radians=1.4F;w=127;h=91;}
        if(n=="resource_replacement")s.static_world->package=package({Quad{-.7F,.7F,-.2F,.2F,0,false,false,137}});
        if(n=="inherited_always")glDepthFunc(GL_ALWAYS);
        if(n=="inherited_greater")glDepthFunc(GL_GREATER);
        if(n=="polygon_line")glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
        // Observe the actual driver ABI outside renderer timing. No wrapper
        // changes the query result. In core contexts NVIDIA may write one
        // polygon mode while Intel writes the redundant second value too.
        // The zero-init query reproduces the old guard's interpretation;
        // a sentinel query independently identifies untouched storage.
        constexpr GLint polygon_sentinel=-123456789;
        GLint polygon_zero[2]{},polygon_probe[2]{polygon_sentinel,polygon_sentinel},profile{};
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
        glGetIntegerv(GL_POLYGON_MODE,polygon_zero);const auto polygon_zero_error=glGetError();
        glGetIntegerv(GL_POLYGON_MODE,polygon_probe);const auto polygon_probe_error=glGetError();
        need((profile&GL_CONTEXT_CORE_PROFILE_BIT)!=0 && polygon_zero_error==GL_NO_ERROR && polygon_probe_error==GL_NO_ERROR,"Polygon witness query invalid");
        need(polygon_probe[0]==(n=="polygon_line"?GL_LINE:GL_FILL),"Unexpected effective polygon state");
        hlc_background_sky_reset_stats();
        const auto before=gpu.statistics();const auto ebefore=gpu.entity_statistics();Samples world;GLuint source{};
        sky_fixture::Calls calls;GLint inherited_function{};glGetIntegerv(GL_DEPTH_FUNC,&inherited_function);
        {ClearStateInjection inject(n);sky_fixture::CallCounter counter;counter.tracking=true;
            ClearHook hook(extractor,w,h,samples);gpu.render(s,{w,h});counter.tracking=false;calls=counter.count;
            if(hook.error)std::rethrow_exception(hook.error);
            need(hook.world_count==1 && hook.world.has_value(),"Regression missing pre-viewmodel boundary");
            source=hook.source;world=std::move(*hook.world);save_samples(dir,"world-before-viewmodel",world);}
        const auto final=extractor.capture(source,w,h,samples);save_samples(dir,"final-scene",final);
        const auto observed=gpu.observe_framebuffer({w,h},s.clear_color,true);need(bool(observed),"Regression final default missing");binary(dir/L"final-default.rgba8.bin",observed.rgba8);
        need(gpu.scene_target_statistics().actual_samples==samples,"Regression sample fallback");
        const auto c=hlc_background_sky_stats();need(c.frames==1,"Regression frame counter wrong");
        const bool unsafe_state=n.starts_with("unsafe_clear_") || n.starts_with("inherited_") || n=="polygon_line";
        const bool unsafe_far=n=="submitted_far_range" || n=="partial_far_clip" || n=="brush_far_transform" || n=="fragmented_full_batch";
        const bool cold_resource=n=="resource_replacement";
        if(hlc_background_sky_policy()==BackgroundSkyPolicy::depth) {
            if(unsafe_state) {
                need(c.late_frames==0 && (c.fallback_bits&8),"Retained GL-state guard did not fallback");
                const auto expected=n=="unsafe_clear_mask"?SkyStateFailure::depth_write_mask:
                    n=="unsafe_clear_scissor"?SkyStateFailure::enabled_capability:
                    n=="unsafe_clear_color"?SkyStateFailure::color_write_mask:
                    n=="polygon_line"?SkyStateFailure::polygon_mode:SkyStateFailure::inherited_depth_function;
                need(hlc_background_sky_diagnostics().state_failures[static_cast<std::size_t>(expected)]==1,"Wrong specific GL-state fallback");
            }
            else if(unsafe_far)need(c.late_frames==0 && (c.fallback_bits&16),"Actually submitted far geometry did not fallback");
            else if(cold_resource)need(c.late_frames==0 && (c.fallback_bits&2),"Unprepared replacement did not fallback");
            else need(c.late_frames==1 && c.early_frames==0,"Newly eligible regression did not execute late path");
        } else need(c.original_frames==1 && c.bounds_tested==0 && c.gl_state_queries==0,"Original regression entered candidate guard");
        const auto work=gpu.statistics();
        if(n=="fragmented_full_batch")need(work.prepared_world_full_batch_fallback_count-before.prepared_world_full_batch_fallback_count==1 &&
            work.triangle_count-before.triangle_count==352,"Fragmented fullbatch did not submit every176quad");
        if(n=="brush_culled_instance")need(work.runtime_brush_culled_count-before.runtime_brush_culled_count==1 &&
            work.runtime_brush_submitted_count-before.runtime_brush_submitted_count==1,"Runtime culling control missed actual selector");
        if(n=="brush_far_transform")need(work.runtime_brush_culled_count-before.runtime_brush_culled_count==0 &&
            work.runtime_brush_submitted_count-before.runtime_brush_submitted_count==2,"Far-straddling brush was not actually submitted");
        GLint final_function{},final_polygon[2]{};GLboolean final_depth_write{},final_color_write[4]{};GLdouble final_range[2]{};
        glGetIntegerv(GL_DEPTH_FUNC,&final_function);glGetIntegerv(GL_POLYGON_MODE,final_polygon);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&final_depth_write);glGetBooleanv(GL_COLOR_WRITEMASK,final_color_write);glGetDoublev(GL_DEPTH_RANGE,final_range);
        const bool final_scissor=glIsEnabled(GL_SCISSOR_TEST)!=0;
        gl_ok("Regression left GL errors");
        if(comma)manifest<<',';comma=true;
        manifest<<"{\"name\":"<<json(n)<<",\"width\":"<<w<<",\"height\":"<<h<<",\"actual_samples\":"<<samples
            <<",\"inherited_depth_function\":"<<inherited_function<<",\"polygon_query_witness\":{\"profile_mask\":"<<profile
            <<",\"zero_initialized_result\":["<<polygon_zero[0]<<','<<polygon_zero[1]<<"],\"sentinel\":"<<polygon_sentinel
            <<",\"sentinel_initialized_result\":["<<polygon_probe[0]<<','<<polygon_probe[1]<<"],\"second_slot_written\":"<<(polygon_probe[1]!=polygon_sentinel?"true":"false")
            <<",\"zero_query_error\":"<<polygon_zero_error<<",\"sentinel_query_error\":"<<polygon_probe_error<<"},\"candidate\":";stats_json(manifest,c);
        manifest<<",\"world_samples\":";sample_summary(manifest,world);manifest<<",\"final_samples\":";sample_summary(manifest,final);
        manifest<<",\"actual_draw_deltas\":{\"world_draws\":"<<work.draw_call_count-before.draw_call_count<<",\"triangles\":"<<work.triangle_count-before.triangle_count
            <<",\"brush_draws\":"<<work.brush_draw_call_count-before.brush_draw_call_count<<",\"brush_instances\":"<<work.runtime_brush_submitted_count-before.runtime_brush_submitted_count
            <<",\"brush_culled\":"<<work.runtime_brush_culled_count-before.runtime_brush_culled_count<<",\"full_batch_fallbacks\":"<<work.prepared_world_full_batch_fallback_count-before.prepared_world_full_batch_fallback_count
            <<",\"studio_draws\":"<<gpu.entity_statistics().studio_draw_count-ebefore.studio_draw_count<<"},\"terminal_gl_state\":{\"depth_func\":"<<final_function
            <<",\"depth_write\":"<<static_cast<unsigned>(final_depth_write)<<",\"depth_range\":["<<final_range[0]<<','<<final_range[1]
            <<"],\"polygon_mode\":["<<final_polygon[0]<<','<<final_polygon[1]<<"],\"color_write\":["<<static_cast<unsigned>(final_color_write[0])<<','<<static_cast<unsigned>(final_color_write[1])
            <<','<<static_cast<unsigned>(final_color_write[2])<<','<<static_cast<unsigned>(final_color_write[3])<<"],\"scissor\":"<<(final_scissor?"true":"false")
            <<"},\"render_gl_calls_excluding_readback\":";sky_fixture::json(manifest,calls);manifest<<'}';
    }
    manifest<<"]}";need(bool(manifest),"Regression manifest write failed");
}
} // namespace eligibility_regressions

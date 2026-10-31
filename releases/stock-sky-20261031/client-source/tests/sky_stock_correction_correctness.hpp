#pragma once
// Focused implementation regression, NOT a replacement for the retained Valve
// BSP/gameplay front/behind/side acceptance. Historical distance2 tests above
// remain unchanged and are never relabelled as Valve's depth oracle.
namespace sky_stock_correction {
namespace ws=hlclient::world_scene_render;
namespace visibility=hlclient::world_visibility;
using Draw=sky_disable_diagnostic::Draw;
using Color=sky_disable_diagnostic::Color;
bool candidate(){return hlc_background_sky_policy()==BackgroundSkyPolicy::world_sky_non_occluding;}
std::shared_ptr<const wr::WorldRenderPackage> profiled(
    const std::shared_ptr<const wr::WorldRenderPackage>& original,bool known=true) {
    auto input=original->textured_world();
    input.world.source_profile=known?a::WorldGeometrySourceProfile::goldsrc_bsp_v30:a::WorldGeometrySourceProfile::unspecified;
    input.world.statistics.source_version=known?30:0;
    input.world.statistics.source_model_count=known?1:0;
    input.world.statistics.source_face_count=known?input.world.surfaces.size():0;
    input.world.statistics.world_model_source_face_count=known?input.world.surfaces.size():0;
    input.world.source_model_bounds=input.world.bounds;
    auto built=wr::WorldRenderPackageBuilder{}.build(std::move(input),original->lightmaps());
    need(bool(built),"Focused source-profile fixture rejected");
    return std::make_shared<const wr::WorldRenderPackage>(std::move(*built.package));
}
void add_brushes(r::RenderScene& scene,bool sky_material) {
    auto brush=profiled(package({Quad{-.6F,.6F,-.6F,.6F,0,false,sky_material,210}}));
    auto map=sky_advanced::brush_scene(scene.static_world->package,brush);
    auto frame=std::make_shared<ws::RuntimeBrushRenderFrame>();
    frame->scene_identity=map->resource_identity();frame->generation=frame->revision=1;
    for(unsigned i=0;i<2;++i) {
        const float x=i==0?-.45F:.45F,y=i==0?-.25F:.25F;
        ws::RuntimeBrushRenderInstance instance;instance.entity_number=100+i;instance.model_slot=10;instance.source_model_index=1;
        instance.model_transform.values[12]=x;instance.model_transform.values[13]=y;
        instance.transformed_bounds={{x-.6F,y,-.6F},{x+.6F,y,.6F}};frame->instances.push_back(instance);
    }
    need(ws::valid_runtime_brush_frame(*frame,*map),"Focused native brush frame rejected");
    scene.static_world->scene_package=std::move(map);scene.static_world->runtime_brushes=std::move(frame);
}
struct Definition {const char* name;bool coordinates{},side{},no_static_sky{},unknown_profile{},missing_cube{},brush_sky{};};
const std::vector<Definition>& definitions(){static const std::vector<Definition> value{
    {"main_coordinate_motif",true},{"same_behind_side_coordinate_motif",true,true},
    {"ordinary_opaque_no_sky",false,false,true},{"open_background_gap"},
    {"opaque_room"},{"narrow_aperture"},{"wall_sky_boundary"},{"masked_edges"},
    {"alternating_material_ranges"},{"no_sky_resource",false,false,false,false,true},
    {"unknown_source_conservative",false,false,false,true},
    {"scene_package_full_ranges"},{"scene_package_selected_ranges"},{"scene_package_visible_commands"},
    {"runtime_brushes"},{"runtime_brush_sky_conservative",false,false,false,false,false,true},
    {"decals"},{"effects"},{"viewmodel"},{"translucent_submission_only"},
    {"resource_replacement"},{"roll_resize"},{"special_non_sky_water",false,false,true}};return value;}
r::RenderScene scene(const Definition& definition,unsigned samples,bool omit_static_sky) {
    const std::string_view name=definition.name;
    auto value=scene_for(name=="effects"?"effects":name=="translucent_submission_only"?"translucent_studio":"open",samples,true);
    std::vector<Quad> geometry;
    if(definition.coordinates) {
        value.camera.position=definition.side?a::AssetVector3{640,0,128}:a::AssetVector3{0,0,128};
        value.camera.target=definition.side?a::AssetVector3{64,448,128}:a::AssetVector3{0,1,128};
        value.camera.up={0,0,1};value.camera.near_plane=4;value.camera.far_plane=4096;
        value.camera.vertical_field_of_view_radians=1.2870022176F;
        // Test-generated faces at the retained map's useful scale. The actual
        // func_wall BSP acceptance remains a separate normal-gameplay run.
        geometry={{-192,192,64,320,256,false,true,224},
            {-80,-16,96,160,128,false,false,48},{32,96,96,160,416,false,false,201},
            {-768,768,64,384,768,false,false,85}};
    } else if(name=="opaque_room") {
        geometry={{-8,8,-8,8,-.4F,false,false,85},{-1,1,-1,1,0,false,true,224}};
    } else if(name=="ordinary_opaque_no_sky") {
        geometry={{-.8F,.8F,-.8F,.8F,.3F,false,false,201},{-.5F,.5F,-.5F,.5F,0,false,false,48}};
    } else if(name=="narrow_aperture") {
        geometry={{-1.8F,1.8F,-1.4F,1.4F,.2F,false,true,224},
            {-2,-.043F,-2,2,-.1F,false,false,85},{.039F,2,-2,2,-.1F,false,false,85},
            {-.15F,.15F,-.9F,.9F,.5F,false,false,201}};
    } else if(name=="wall_sky_boundary") {
        geometry={{-.9F,.9F,-.9F,.9F,.3F,false,true,224},
            {-.71F,.031F,-.91F,.813F,0,false,false,48},{.031F,.8F,-.91F,.813F,.7F,false,false,201}};
    } else if(name=="masked_edges") {
        geometry={{-1.3F,1.3F,-1.3F,1.3F,.25F,false,true,224},
            {-1.127F,.783F,-.793F,.917F,0,true,false,180}};
    } else if(name=="alternating_material_ranges") {
        geometry={{-.9F,-.4F,-.9F,.9F,.25F,false,true,224},
            {-.8F,-.1F,-.8F,.8F,0,false,false,48},
            {.1F,.9F,-.9F,.9F,.25F,false,true,224},{.1F,.8F,-.8F,.8F,.5F,false,false,201}};
    } else {
        geometry={{-1.127F,.783F,-.793F,.917F,.25F,false,true,224},
            {-.5F,.5F,-.5F,.5F,0,false,false,90}};
    }
    if(name=="scene_package_selected_ranges") {
        // The frozen package builder uses sixteen source faces per prepared
        // block: fill one in-view SKY block and TWO wholly offscreen blocks.
        // Production deliberately bypasses detailed selection below two
        // outside probes; one outside block cannot exercise selected ranges.
        for(unsigned i=0;i<15;++i)geometry.push_back(geometry.front());
        for(unsigned i=0;i<32;++i)geometry.push_back(Quad{100,101,-.2F,.2F,.25F,false,true,224});
    }
    if(definition.no_static_sky || omit_static_sky)
        geometry.erase(std::remove_if(geometry.begin(),geometry.end(),[](const Quad& quad){return quad.sky;}),geometry.end());
    value.static_world=r::RenderStaticWorld{profiled(package(geometry),!definition.unknown_profile),r::RenderCullMode::none,r::RenderBaselineLightStylePolicy::source_slot_zero};
    if(name=="scene_package_selected_ranges" && !omit_static_sky) {
        // One legitimate SKY material batch spans an in-view and an offscreen
        // SKY surface, exercising prepared block selection instead of merely
        // the easy all-batches/standalone branch. Material data stays retained.
        auto source=value.static_world->package->textured_world();
        for(auto& item:source.world.surfaces)
            if(source.world.materials[item.material_index].texture_name=="sky")item.material_index=source.world.surfaces.front().material_index;
        // The package contract requires EVERY source material to be referenced.
        // Remapping thirty-two SKY faces to material0 must also compact the
        // redundant source-material/texture bindings, not retain 31 unused ones.
        auto textures=a::WorldTextureSet::create(
            {source.textures.textures()[0],source.textures.textures()[1]},
            {source.textures.bindings()[0],source.textures.bindings()[1]}, {},2);
        need(bool(textures),"Selected-range compact texture bindings rejected");
        source.world.materials.resize(2);source.world.statistics.material_count=2;
        a::TexturedWorldAsset compact{std::move(source.world),std::move(*textures.texture_set)};
        auto grouped=wr::WorldRenderPackageBuilder{}.build(std::move(compact),value.static_world->package->lightmaps());
        if(!grouped)throw std::runtime_error(std::string("Selected-range grouped SKY fixture rejected: ")+
            (grouped.error?grouped.error->context:"no structured error"));
        value.static_world->package=std::make_shared<const wr::WorldRenderPackage>(std::move(*grouped.package));
    }
    if(name.starts_with("scene_package_")) {
        value.static_world->scene_package=eligibility_regressions::map_for(value.static_world->package,nullptr,{});
        if(name=="scene_package_visible_commands")
            eligibility_regressions::select_world(value,visibility::WorldVisibilityMode::pvs_and_frustum,{96,96});
    }
    if(definition.missing_cube)value.skybox.reset();
    if(name=="runtime_brushes" || definition.brush_sky)add_brushes(value,definition.brush_sky);
    if(name=="decals")sky_advanced::add_decals(value);
    if(name=="special_non_sky_water")value.static_world->package=profiled(sky_advanced::water_package());
    if(name=="resource_replacement")value.skybox=sky(2);
    if(name=="roll_resize") {value.camera.vertical_field_of_view_radians=1.4F;value.screen_effects.translation={.07F,-.04F,.05F};value.screen_effects.roll_degrees=13;}
    return value;
}
struct Capture {
    Samples world,final;std::vector<std::uint8_t> resolved;SkyCandidateStats stats{};
    std::vector<Draw> order;sky_fixture::Calls calls{};
    unsigned fullscreen{},finite_textured{},non_sky{};
    std::uint64_t world_draws{},brush_draws{},brush_instances{},studio_draws{},decal_batches{};
    std::uint64_t selected_blocks{},rejected_blocks{},all_batch_bypasses{},visible_commands{};
};
Capture capture(gl::OpenGlRenderer& gpu,Extractor& extractor,const r::RenderScene& input,int width,int height,bool prime) {
    sky_disable_diagnostic::neutral();
    if(prime)gpu.render(input,{width,height});
    hlc_background_sky_reset_stats();
    const auto before=gpu.statistics();const auto ebefore=gpu.entity_statistics();const auto dbefore=gpu.decal_statistics();
    Capture result;GLuint source{};
    {sky_fixture::CallCounter counter;counter.tracking=true;
        sky_disable_diagnostic::DrawObserver draws(counter.tracking,false);ClearHook hook(extractor,width,height,input.requested_msaa_samples);
        gpu.render(input,{width,height});counter.tracking=false;
        if(hook.error)std::rethrow_exception(hook.error);
        need(hook.world_count==1 && hook.world.has_value(),"Focused pre-viewmodel boundary missing");
        source=hook.source;result.world=std::move(*hook.world);result.calls=counter.count;
        result.order=std::move(draws.order);result.fullscreen=draws.fullscreen;result.finite_textured=draws.finite_textured;result.non_sky=draws.non_sky;
    }
    result.stats=hlc_background_sky_stats();
    result.final=extractor.capture(source,width,height,input.requested_msaa_samples);
    const auto observed=gpu.observe_framebuffer({width,height},input.clear_color,true);need(bool(observed),"Focused resolved framebuffer unavailable");result.resolved=observed.rgba8;
    need(gpu.scene_target_statistics().actual_samples==input.requested_msaa_samples,"Focused MSAA quality changed");
    result.world_draws=gpu.statistics().draw_call_count-before.draw_call_count;
    result.brush_draws=gpu.statistics().brush_draw_call_count-before.brush_draw_call_count;
    result.brush_instances=gpu.statistics().runtime_brush_submitted_count-before.runtime_brush_submitted_count;
    result.studio_draws=gpu.entity_statistics().studio_draw_count-ebefore.studio_draw_count;
    result.decal_batches=gpu.decal_statistics().rendered_batch_count-dbefore.rendered_batch_count;
    result.selected_blocks=gpu.statistics().prepared_world_selected_block_count-before.prepared_world_selected_block_count;
    result.rejected_blocks=gpu.statistics().prepared_world_rejected_block_count-before.prepared_world_rejected_block_count;
    result.all_batch_bypasses=gpu.statistics().prepared_world_all_batch_bypass_frame_count-before.prepared_world_all_batch_bypass_frame_count;
    result.visible_commands=gpu.statistics().rendered_command_count-before.rendered_command_count;
    need(result.stats.frames==1 && result.stats.early_frames==1 && result.stats.late_frames==0 && result.stats.bounds_tested==0 && result.stats.gl_state_queries==0 && result.stats.fallback_bits==0,"Focused mode unexpectedly used late/fallback guard");
    const auto& cost=hlc_background_sky_diagnostics();need(cost.guard_ns==0 && cost.prepare_ns==0 && cost.range_tests==0 && cost.prepared_frames==0,"Focused mode unexpectedly prepared a depth guard");
    need(result.fullscreen==result.stats.fullscreen_sky_draws,"Observed background draws disagree with counters");
    gl_ok("Focused capture left GL errors");return result;
}
std::size_t color_count(const Samples& samples,Color color) {
    std::size_t count{};for(std::size_t i=0;i<samples.color.size()/4;++i)if(sky_disable_diagnostic::is_color(samples,i,color))++count;return count;
}
void exact_samples(const Samples& a,const Samples& b,const char* why) {
    need(a.samples==b.samples && a.depth_available==b.depth_available && a.color==b.color && a.depth_bits==b.depth_bits,why);
}
void save(const Path& directory,const Capture& value) {
    std::filesystem::create_directory(directory);save_samples(directory,"world-before-viewmodel",value.world);
    save_samples(directory,"final-scene",value.final);binary(directory/L"final-default.rgba8.bin",value.resolved);
}
void capture_json(std::ostream& out,const Capture& value) {
    out<<"{\"stats\":";stats_json(out,value.stats);
    out<<",\"static_world_sky_skipped_draws\":"<<value.stats.static_world_sky_skipped_draws
       <<",\"static_world_sky_skipped_frames\":"<<value.stats.static_world_sky_skipped_frames
       <<",\"unsupported_brush_sky_draws\":"<<value.stats.unsupported_brush_sky_draws
       <<",\"static_world_sky_unsupported_draws\":"<<value.stats.static_world_sky_unsupported_draws
       <<",\"static_world_sky_missing_background_frames\":"<<value.stats.static_world_sky_missing_background_frames
       <<",\"observed_fullscreen_draws\":"<<value.fullscreen
       <<",\"actual_work\":{\"world_and_decal_draws\":"<<value.world_draws<<",\"brush_draws\":"<<value.brush_draws
       <<",\"brush_instances\":"<<value.brush_instances<<",\"studio_draws\":"<<value.studio_draws<<",\"decal_batches\":"<<value.decal_batches
       <<",\"selected_blocks\":"<<value.selected_blocks<<",\"rejected_blocks\":"<<value.rejected_blocks
       <<",\"all_batch_bypasses\":"<<value.all_batch_bypasses<<",\"visible_commands\":"<<value.visible_commands<<"},\"order\":";
    sky_disable_diagnostic::order_json(out,value.order);out<<",\"calls_excluding_readback\":";sky_fixture::json(out,value.calls);out<<'}';
}
void run(const Options& options,gl::OpenGlRenderer& gpu) {
    need(candidate() || hlc_background_sky_policy()==BackgroundSkyPolicy::original,"Focused stock correction entry accepts only original or world_sky_non_occluding");
    Extractor extractor;std::ofstream out(options.output/L"stock-sky-correctness.json");out<<std::setprecision(17);
    out<<"{\"schema\":\"hlc.stock-sky.focused-correctness.v1\",\"status\":\"FOCUSED_FIXTURE_NOT_VALVE_GAMEPLAY_ACCEPTANCE\",\"policy\":"<<json(background_sky_policy_name(hlc_background_sky_policy()))<<',';hardware(out,gpu);
    out<<",\"reference\":\"paired scene without static SKY draws; selected clear preserved exactly; reference per-sample depth is NOT Valve depth\",\"historical_distance2_oracles\":\"unchanged and original-only\",\"cases\":[";
    bool comma{};
    for(const unsigned samples:{0U,4U})for(const auto& definition:definitions()) {
        const std::string_view name=definition.name;
        const int width=definition.coordinates?128:name=="roll_resize"?127:96,height=definition.coordinates?72:name=="roll_resize"?95:96;
        auto input=scene(definition,samples,false);const auto directory=options.output/(std::string(name)+"-aa"+std::to_string(samples));
        if(name=="resource_replacement") {
            auto previous=scene(definition,samples,false);previous.skybox=sky(1);gpu.render(previous,{width,height});
        }
        const auto actual=capture(gpu,extractor,input,width,height,name!="resource_replacement");save(directory,actual);
        const auto expected_fullscreen=definition.missing_cube?0U:1U;
        need(actual.stats.fullscreen_sky_draws==expected_fullscreen && actual.stats.fullscreen_sky_skipped_frames==0,"Early background changed under focused policy");
        const bool supported=!definition.no_static_sky && !definition.unknown_profile;
        if(candidate() && supported) {
            need(actual.stats.static_world_sky_skipped_draws>0 && actual.stats.static_world_sky_skipped_frames==1,"Supported static SKY omission not exercised");
            // No static finite replacement, depth-only surrogate or fullscreen
            // removal is permitted. Runtime-brush SKY stays conservative.
            if(!definition.brush_sky)need(actual.stats.finite_sky_textured_draws==0 && actual.stats.finite_sky_constant_draws==0,"Supported static SKY still emitted a finite draw");
        } else need(actual.stats.static_world_sky_skipped_draws==0,"Unsupported/no-SKY/original package was skipped");
        if(definition.unknown_profile)need(actual.stats.finite_sky_textured_draws>0 && (!candidate() || actual.stats.static_world_sky_unsupported_draws>0),"Unknown profile was not conservative");
        if(definition.brush_sky)need(actual.brush_draws==2 && (!candidate() || actual.stats.unsupported_brush_sky_draws==2),"Inline-brush SKY path was altered or not witnessed");
        if(name=="runtime_brushes")need(actual.brush_draws==2 && actual.brush_instances==2,"Normal runtime brush work missing");
        if(name=="decals")need(actual.decal_batches==1,"Real decal pass missing");
        need(actual.studio_draws==(name=="translucent_submission_only"?3U:1U),"Studio/viewmodel submission changed");
        if(definition.coordinates) {
            need(color_count(actual.world,{48,96,40})>0,"Coordinate foreground witness missing");
            const auto behind=color_count(actual.world,{201,96,40});
            need((candidate() || definition.side)?behind>0:behind==0,"Coordinate behind visibility contract failed");
            if(candidate() && !definition.side)need(sky_disable_diagnostic::is_color(actual.world,static_cast<std::size_t>(height/2)*width+width/2,{85,96,40}),"Coordinate main backwall witness missing");
        }
        if(name=="ordinary_opaque_no_sky")need(sky_disable_diagnostic::is_color(actual.world,static_cast<std::size_t>(height/2)*width+width/2,{48,96,40}),"Ordinary foreground failed to occlude ordinary background");
        if(name=="scene_package_visible_commands")need(actual.visible_commands>0,"Named visible-command branch not exercised");
        if(name=="scene_package_selected_ranges")need(actual.selected_blocks>0 && actual.rejected_blocks>0,"Named selected-range branch not exercised");
        if(name=="scene_package_full_ranges")need(actual.all_batch_bypasses>0,"Named full-range batch bypass not exercised");
        auto reference=scene(definition,samples,candidate() && !definition.unknown_profile);
        // Removing diagnostic SKY geometry also removes its flat-background
        // metadata from the test package. Preserve the actual selected clear,
        // so the oracle checks draw omission, not an unintended clear change.
        const auto& clear=actual.stats.last_frame_clear_color;reference.clear_color={clear[0],clear[1],clear[2],clear[3]};
        const auto oracle=capture(gpu,extractor,reference,width,height,true);save(directory/L"no-static-sky-oracle",oracle);
        exact_samples(actual.world,oracle.world,"Paired world color/depth differs from exact static-draw omission oracle");
        exact_samples(actual.final,oracle.final,"Paired final scene color/depth differs from omission oracle");
        need(actual.resolved==oracle.resolved,"Paired resolved output differs from omission oracle");
        need(actual.brush_draws==oracle.brush_draws && actual.brush_instances==oracle.brush_instances && actual.studio_draws==oracle.studio_draws && actual.decal_batches==oracle.decal_batches,"Unrelated render pass work differs from paired oracle");
        if(name=="masked_edges" && samples==4) {
            const auto pixels=static_cast<std::size_t>(width)*height;bool partial{};
            for(unsigned n=1;n<actual.world.samples;++n)if(!std::equal(actual.world.depth_bits.begin(),actual.world.depth_bits.begin()+static_cast<std::ptrdiff_t>(pixels),actual.world.depth_bits.begin()+static_cast<std::ptrdiff_t>(pixels*n)))partial=true;
            need(partial,"Masked regression did not exercise partial MSAA coverage");
        }
        if(candidate() && name=="open_background_gap") {
            const auto p=std::size_t{0};need(actual.world.depth_bits[p]==std::bit_cast<std::uint32_t>(1.0F),"Open gap not uncovered");
            std::array<std::uint8_t,3> clear_rgb{};for(unsigned c=0;c<3;++c)clear_rgb[c]=static_cast<std::uint8_t>(std::lround(clear[c]*255));
            need(!std::equal(clear_rgb.begin(),clear_rgb.end(),actual.world.color.begin()+static_cast<std::ptrdiff_t>(p*4)),"Available background gap is clear instead of textured sky");
        }
        if(comma)out<<',';comma=true;
        out<<"{\"name\":"<<json(name)<<",\"width\":"<<width<<",\"height\":"<<height<<",\"actual_samples\":"<<samples
           <<",\"synthetic_source_profile\":"<<json(definition.unknown_profile?"unspecified":"explicit_goldsrc_bsp_v30_test_generated_NOT_imported_stock_BSP")
           <<",\"paired_reference_omits_static_sky\":"<<((candidate() && !definition.unknown_profile)?"true":"false")
           <<",\"static_sky_source_ranges\":[";
        bool range_comma{};
        const auto range=[&](std::uint32_t first,std::uint32_t count,std::size_t material) {
            if(!input.static_world->package->materials()[material].sky_surface)return;
            if(range_comma)out<<',';range_comma=true;
            out<<"{\"mode\":"<<GL_TRIANGLES<<",\"type\":"<<GL_UNSIGNED_INT<<",\"count\":"<<count<<",\"offset\":"<<static_cast<std::uint64_t>(first)*sizeof(std::uint32_t)<<'}';
        };
        if(input.static_world->visible_draw_list) {
            for(const auto& command:input.static_world->visible_draw_list->commands())
                if(command.object_kind==visibility::WorldVisibleObjectKind::world_surface)range(command.first_index,command.index_count,command.render_material_index);
        } else {
            // Surface ranges describe the precise selected-range boundaries;
            // a full batch may combine them, so emit that boundary as well.
            for(const auto& item:input.static_world->package->surface_ranges())range(item.first_index,item.index_count,item.render_material_index);
            for(const auto& item:input.static_world->package->draw_batches())range(item.first_index,item.index_count,item.render_material_index);
            const auto batches=input.static_world->package->draw_batches();
            const auto prepared=input.static_world->package->prepared_draw_batches();
            const auto blocks=input.static_world->package->prepared_draw_blocks();
            for(std::size_t b=0;b<prepared.size();++b)for(std::size_t k=0;k<prepared[b].block_count;++k) {
                const auto& item=blocks[prepared[b].first_block+k];range(item.first_index,item.index_count,batches[b].render_material_index);
            }
        }
        out<<"],\"exact_world_color_depth\":true,\"exact_final_color_depth\":true,\"exact_resolved\":true,\"actual\":";
        capture_json(out,actual);out<<",\"paired_oracle\":";capture_json(out,oracle);out<<'}';
    }
    out<<"],\"limitations\":[\"Actual retained Valve BSP main/side acceptance is a separate required normal-gameplay control\",\"Studio/translucent submission stability is not established Valve-behind-SKY parity\",\"No timing or performance acceptance in this readback suite\"],\"gl_error\":0}";
    need(bool(out),"Focused stock correction manifest write failed");
}
} // namespace sky_stock_correction

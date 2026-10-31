#pragma once
// Included after the owning fixture's Options/Extractor definitions. The owning
// TU includes the existing native asset/parser/hash/visibility headers globally.
// One immutable real map, no actors/network replay, no production code changes.
namespace sky_crossfire_control {
namespace gs=hlclient::goldsrc;
namespace app=hlclient::app;
namespace vis=hlclient::world_visibility;
namespace spatial=hlclient::world_spatial;
constexpr r::RenderExtent extent{1920,1080};
constexpr std::uintmax_t maximum_map_bytes=64U*1024U*1024U;

inline std::string file_hash(const Path& path) {
    const auto size=std::filesystem::file_size(path);
    need(size>0 && size<=maximum_map_bytes,"Crossfire input exceeds bounded read");
    std::ifstream input(path,std::ios::binary);need(bool(input),"Cannot open read-only Crossfire input");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    need(bool(input),"Short read of Crossfire input");
    const auto hash=hlclient::hash::sha256(bytes);need(hash.has_value(),"Crossfire SHA256 failed");
    return hlclient::hash::sha256_hex(*hash);
}
inline gs::ServerInfoState server_info() {
    // Project-authored inert metadata, NOT a captured server or replay. Real BSP
    // bytes below are resolved independently by the normal approved provider.
    std::vector<std::byte> body;
    const auto u32=[&](std::uint32_t value){for(unsigned n=0;n<4;++n)body.push_back(static_cast<std::byte>((value>>(n*8U))&255U));};
    const auto text=[&](std::string_view value){for(const char c:value)body.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));body.push_back(std::byte{});};
    u32(48);u32(1);u32(0);for(unsigned n=0;n<16;++n)body.push_back(std::byte{});
    body.push_back(std::byte{2});body.push_back(std::byte{});body.push_back(std::byte{1});
    text("valve");text("Offline static Crossfire control");text("maps/crossfire.bsp");text("inert-metadata");body.push_back(std::byte{});
    auto parsed=gs::ServerInfoParser{}.parse(body);
    need(bool(parsed)&&parsed.state.has_value(),"Inert Crossfire server metadata rejected");return std::move(*parsed.state);
}
inline gs::ResourceListState resource_list() {
    const std::array entries{resource_list_test_fixture::EntrySpec{2,"maps/crossfire.bsp",9,0,0}};
    const auto literal=resource_list_test_fixture::make_message(entries);
    auto parsed=gs::ResourceListParser{}.parse(literal.bytes,0,literal.bit_length);
    need(bool(parsed)&&parsed.state.has_value(),"Inert Crossfire resource metadata rejected");return std::move(*parsed.state);
}
struct View {a::AssetVector3 eye;double yaw{},pitch{};};
inline View selected_view(std::string_view name) {
    // Frozen actual-Crossfire camera controls, not a historical match replay.
    // Every selected eye is additionally gated by the imported spatial package.
    if(name=="room")return {{-983.0078125F,712.1748657226562F,-1472.F},-36.000854,-.4};
    need(name=="open","Crossfire view must be room or open");
    // Established courtyard view, qualified by the retained actual B6 image
    // and scene receipt; still checked against this imported BSP and pixels.
    return {{32.F,320.F,-1791.96875F},121.997680664,0.};
}
inline r::RenderCamera camera(View view) {
    constexpr double rad=.01745329251994329577;
    const auto y=view.yaw*rad,p=view.pitch*rad;
    return {view.eye,{view.eye.x+static_cast<float>(std::cos(y)*std::cos(p)),view.eye.y+static_cast<float>(std::sin(y)*std::cos(p)),view.eye.z+static_cast<float>(std::sin(p))},
        {0,0,1},static_cast<float>(2*std::atan(std::tan(90*rad*.5)/(4./3.))),.1F,4096.F};
}
struct Prepared {
    std::unique_ptr<app::RuntimeReplayLocalAssets> assets;
    r::RenderScene scene;
    std::shared_ptr<const vis::WorldVisibilitySet> visibility;
    std::shared_ptr<const vis::WorldVisibleDrawList> draws;
    Path root,map;
    std::string map_sha;
    std::array<std::string,6> sky_sha;
    std::uintmax_t map_bytes{};
    std::filesystem::file_time_type map_time;
    spatial::WorldPointLeafResult leaf;
};
inline Prepared prepare(const Options& options) {
    Prepared value;value.root=L"D:/DEV/HLCLIENT-RESEARCH/Half-Life";
    value.map=value.root/L"valve/maps/crossfire.bsp";
    need(std::filesystem::is_regular_file(value.map),"Approved real Crossfire map is absent");
    value.map_bytes=std::filesystem::file_size(value.map);value.map_time=std::filesystem::last_write_time(value.map);value.map_sha=file_hash(value.map);
    constexpr std::array faces{"bk","dn","ft","lf","rt","up"};
    for(std::size_t i=0;i<faces.size();++i)value.sky_sha[i]=file_hash(value.root/(std::string("valve/gfx/env/desert")+faces[i]+".tga"));
    // Zero advertised size means unavailable metadata; the real approved-root
    // file is still parsed/validated by the unchanged normal resource provider.
    auto made=app::RuntimeReplayLocalAssets::create(resource_list(),server_info(),value.root,"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client,{},{},"desert");
    need(bool(made.projection),"Normal provider could not load approved real Crossfire");
    value.assets=std::move(made.projection);const auto map=value.assets->surface_scene();
    need(bool(map)&&bool(map->world_package())&&bool(value.assets->skybox()),"Crossfire world/cubemap readiness incomplete");
    value.scene.camera=camera(selected_view(options.scene));value.scene.requested_msaa_samples=options.msaa;
    value.scene.clear_color={.08F,.065F,.05F,1};value.scene.skybox=value.assets->skybox();
    value.scene.static_world=r::RenderStaticWorld{map->world_package(),r::RenderCullMode::none,r::RenderBaselineLightStylePolicy::source_slot_zero,map};
    const auto located=spatial::WorldSpatialQuery::locate_point(map->spatial_package(),value.scene.camera.position);
    need(bool(located)&&located.result.has_value()&&located.result->leaf_index!=0&&!located.result->solid_or_special,"Crossfire camera is in SOLID/special or invalid leaf");value.leaf=*located.result;
    // Use existing immutable visibility APIs, not the rejected test-only 71b97
    // cache. Adapters preserve the package's complete identity, while actual
    // runtime/static brush drawing is explicitly OFF in this map-only control.
    std::vector<vis::WorldVisibilityBrushInstanceInput> instances;
    std::vector<vis::WorldVisibleBrushModelInput> models;
    std::vector<vis::WorldVisibleBrushInstanceDrawInput> brush_draws;
    for(const auto& instance:map->brush_instances()) {
        const bool supported=instance.support_status==hlclient::world_scene_render::BrushSubmodelRenderSupportStatus::supported_static_opaque && instance.source_model_index.has_value();
        instances.push_back({instance.source_instance_index,instance.transformed_bounds,instance.touched_leaf_indices,supported});
        if(supported)brush_draws.push_back({instance.source_instance_index,*instance.source_model_index,instance.model_transform});
    }
    const auto& library=map->brush_library();
    if(library.render_package())for(const auto& model:library.models())models.push_back({model.source_model_index(),library.render_package()->indices().size(),library.render_package()->materials().size(),model.surfaces()});
    const vis::WorldVisibilityResolveInput input{&map->spatial_package(),map->world_surfaces(),instances,value.scene.camera,extent,
        vis::WorldVisibilityMode::pvs_and_frustum,vis::WorldPvsFallbackPolicy::frustum_only,1,map->visibility_scene_identity(),false};
    auto resolved=vis::WorldVisibilityResolver{}.resolve(input);need(bool(resolved)&&resolved.visibility.has_value(),"Crossfire PVS/frustum resolve failed");
    value.visibility=std::make_shared<const vis::WorldVisibilitySet>(std::move(*resolved.visibility));
    auto built=vis::WorldVisibleDrawListBuilder{}.build({value.visibility.get(),map->world_surfaces(),map->world_package()->indices().size(),map->world_package()->materials().size(),models,brush_draws});
    need(bool(built)&&built.draw_list.has_value(),"Crossfire visible draw list failed");
    value.draws=std::make_shared<const vis::WorldVisibleDrawList>(std::move(*built.draw_list));
    value.scene.static_world->visible_draw_list=value.draws;
    const auto id=value.visibility->scene_identity();const auto sig=value.visibility->result_signature();
    value.scene.static_world->visibility_summary=r::RenderStaticWorldVisibilitySummary{value.visibility->revision(),id.resource_id,id.revision,id.visibility_input_signature,id.draw_input_signature,sig.first,sig.second,value.visibility->statistics().visible_world_surface_count,0};
    need(value.draws->statistics().brush_command_count==0,"Crossfire control must not draw static/runtime brush entities");
    need(file_hash(value.map)==value.map_sha,"Crossfire map changed during normal provider load");return value;
}
inline void counters(std::ostream& out,const SkyCandidateStats& s) {
    out<<"{\"frames\":"<<s.frames<<",\"early_frames\":"<<s.early_frames<<",\"late_frames\":"<<s.late_frames
        <<",\"fullscreen_sky_draws\":"<<s.fullscreen_sky_draws<<",\"finite_sky_textured_draws\":"<<s.finite_sky_textured_draws
        <<",\"non_sky_world_draws\":"<<s.non_sky_world_draws<<",\"last_frame_world_draws\":"<<s.last_frame_world_draws
        <<",\"world_draw_order_hash\":"<<json(std::to_string(s.world_draw_order_hash))
        <<",\"static_world_sky_skipped_draws\":"<<s.static_world_sky_skipped_draws
        <<",\"static_world_sky_skipped_frames\":"<<s.static_world_sky_skipped_frames
        <<",\"unsupported_brush_sky_draws\":"<<s.unsupported_brush_sky_draws
        <<",\"static_world_sky_unsupported_draws\":"<<s.static_world_sky_unsupported_draws<<'}';
}
inline void metadata(std::ostream& out,const Prepared& p,const Options& o,gl::OpenGlRenderer& gpu) {
    hardware(out,gpu);const auto& c=p.scene.camera;const auto& world=*p.scene.static_world->package;
    out<<",\"policy\":"<<json(background_sky_policy_name(hlc_background_sky_policy()))<<",\"scene\":"<<json(o.scene)
        <<",\"scene_provenance\":\"actual_read_only_Crossfire_BSP_static_model0_with_existing_PVS_and_cubemap; no_actors_or_network_replay_NOT_gameplay_FPS\""
        <<",\"map_virtual_name\":\"maps/crossfire.bsp\",\"map_sha256\":"<<json(p.map_sha)<<",\"map_bytes\":"<<p.map_bytes
        <<",\"sky_face_sha256\":[";for(std::size_t i=0;i<p.sky_sha.size();++i){if(i)out<<',';out<<json(p.sky_sha[i]);}
    out<<"],\"width\":1920,\"height\":1080,\"requested_samples\":"<<o.msaa<<",\"actual_samples\":"<<gpu.scene_target_statistics().actual_samples
        <<",\"camera\":{\"eye\":["<<c.position.x<<','<<c.position.y<<','<<c.position.z<<"],\"target\":["<<c.target.x<<','<<c.target.y<<','<<c.target.z
        <<"],\"up\":[0,0,1],\"vertical_fov_radians\":"<<c.vertical_field_of_view_radians<<",\"near\":"<<c.near_plane<<",\"far\":"<<c.far_plane
        <<",\"leaf\":"<<p.leaf.leaf_index<<",\"contents\":"<<p.leaf.contents<<"},\"pvs_applied\":"<<json(vis::to_string(p.visibility->applied_mode()))
        <<",\"pvs_fallback\":"<<json(vis::to_string(p.visibility->fallback_reason()))<<",\"immutable_selection_scope\":\"prepared_once_before_timing_same_fixed_camera; selection_and_asset_load_not_in_CPU_render_scope\""
        <<",\"resource_id\":"<<json(std::to_string(p.scene.static_world->scene_package->resource_id()))<<",\"resource_revision\":"<<json(std::to_string(p.scene.static_world->scene_package->resource_revision()))
        <<",\"source_geometry\":{\"vertices\":"<<world.vertices().size()<<",\"indices\":"<<world.indices().size()<<",\"surfaces\":"<<world.surface_ranges().size()<<",\"batches\":"<<world.draw_batches().size()<<",\"materials\":"<<world.materials().size()
        <<",\"selected_world_commands\":"<<p.draws->statistics().world_command_count<<",\"selected_brush_commands\":0}"
        <<",\"runtime_text\":\"off\",\"active_application_counters\":null,\"active_application_counter_semantics\":\"offline_renderer_control_not_normal_application_gameplay_loop; no_active_gameplay_claim\"";
}
inline void verify_unchanged(const Prepared& p) {need(std::filesystem::file_size(p.map)==p.map_bytes&&std::filesystem::last_write_time(p.map)==p.map_time&&file_hash(p.map)==p.map_sha,"Read-only Crossfire input changed");}
inline void capture(const Options& o,Prepared& p,gl::OpenGlRenderer& gpu) {
    // Existing exact extractor, all readbacks deliberately outside timing runs.
    Extractor extractor;sky_disable_diagnostic::neutral();gpu.render(p.scene,extent);hlc_background_sky_reset_stats();
    GLuint source{};
    {ClearHook hook(extractor,extent.width,extent.height,o.msaa);gpu.render(p.scene,extent);if(hook.error)std::rethrow_exception(hook.error);
        // No viewmodel is submitted in this static-only control, so the normal
        // renderer has no later viewmodel depth-clear boundary to intercept.
        // The initial scene-clear FBO remains valid after render/resolve.
        need(hook.world_count==0&&!hook.world.has_value(),"Unexpected viewmodel/depth-clear pass in static Crossfire control");source=hook.source;}
    const auto world=extractor.capture(source,extent.width,extent.height,o.msaa);
    const auto final=world; // Same exact boundary: there are no later populated passes.
    const auto resolved=gpu.observe_framebuffer(extent,p.scene.clear_color,true);need(bool(resolved),"Crossfire resolved readback failed");
    save_samples(o.output,"world-before-viewmodel",world);save_samples(o.output,"final-scene",final);binary(o.output/L"final-default.rgba8.bin",resolved.rgba8);
    std::ofstream out(o.output/L"crossfire-preflight.json");out<<std::setprecision(17)<<"{\"schema\":\"hlc.stock-sky-correction.crossfire-preflight.v1\",";metadata(out,p,o,gpu);
    out<<",\"world_samples\":";sample_summary(out,world);out<<",\"final_samples\":";sample_summary(out,final);out<<",\"candidate\":";counters(out,hlc_background_sky_stats());
    out<<",\"selected_commands\":[";bool comma=false;for(const auto& command:p.draws->commands()){if(comma)out<<',';comma=true;out<<'['<<command.first_index<<','<<command.index_count<<','<<command.render_material_index<<','<<command.source_surface_index<<']';}
    out<<"],\"world_final_boundary_semantics\":\"same_after_render_scene_FBO; no_viewmodel_or_later_populated_passes\",\"comparison\":\"paired_original_vs_candidate_external_exact_full_color_depth; no_large_sky_mask; report_any_difference_footprint\",\"timed\":false}";
    need(bool(out),"Crossfire preflight report failed");verify_unchanged(p);gl_ok("Crossfire preflight GL error");
}
struct Row {std::uint64_t frame{};double cpu_us{},interval_us{},swap_us{};SkyCandidateStats sky{};gl::OpenGlWorldRendererStatistics work{};};
inline void metric(std::ostream& out,std::vector<double> values) {
    if(values.empty()){out<<"null";return;}double sum=0;for(double v:values)sum+=v;std::sort(values.begin(),values.end());
    const auto nearest=[&](double q){const auto index=static_cast<std::size_t>(std::ceil(q*values.size()))-1;return values[std::min(index,values.size()-1)];};
    out<<"{\"n\":"<<values.size()<<",\"mean\":"<<sum/values.size()<<",\"median\":"<<nearest(.5)<<",\"p95\":"<<nearest(.95)<<",\"p99\":"<<nearest(.99)<<",\"max\":"<<values.back()<<",\"quantile_method\":\"exact_nearest_rank_all_rows\"}";
}
inline void run(const Options& o,hlclient::platform::SdlWindow& window,gl::OpenGlRenderer& gpu) {
    need(hlc_background_sky_policy()==BackgroundSkyPolicy::original||hlc_background_sky_policy()==BackgroundSkyPolicy::world_sky_non_occluding,"Crossfire control supports only original/correction");
    const auto initial_pixels=window.pixel_extent();
    need(initial_pixels.width==1920&&initial_pixels.height==1080,"Crossfire preflight/timing requires a real1920x1080framebuffer, not a cropped synthetic correctness window");
    auto p=prepare(o);if(o.crossfire_correctness){capture(o,p,gpu);return;}
    need(std::uint64_t(o.warm)+o.frames<=720,"Crossfire GPU query set exceeds720frames");
    hlclient::platform::WindowEvent startup;while(window.poll_event(startup))need(startup.type!=hlclient::platform::WindowEventType::quit_requested,"Crossfire control cancelled");
    auto* native=SDL_GL_GetCurrentWindow();need(native!=nullptr,"Crossfire context has no current native window");const auto flags=SDL_GetWindowFlags(native);
    need(!(flags&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED)),"Crossfire timing window hidden/minimized");
    const auto pixels=window.pixel_extent();const auto video=window.video_state();
    need(pixels.width==1920&&pixels.height==1080&&video.actual_mode==hlclient::core::VideoWindowMode::borderless&&video.actual_swap_interval&&*video.actual_swap_interval==0,"Crossfire actual extent/borderless/VSync mismatch");
    sky_disable_diagnostic::neutral();hlc_background_sky_reset_stats();gpu.render(p.scene,extent);
    need(gpu.scene_target_statistics().actual_samples==o.msaa,"Crossfire cold-frame MSAA fallback");
    const auto cold=hlc_background_sky_stats();need(cold.frames==1&&cold.fullscreen_sky_draws==1&&cold.last_frame_world_draws>0&&cold.last_frame_world_draws<=65536,"Crossfire cold draw contract invalid");
    const auto expected=static_cast<unsigned>(cold.last_frame_world_draws);
    std::vector<Row> rows;rows.reserve(o.frames);std::optional<Clock::time_point> previous;
    sky_fixture::GpuQueries queries(o.warm+o.frames,o.msaa,true,expected,true);
    for(std::uint64_t frame=1;frame<=std::uint64_t(o.warm)+o.frames;++frame) {
        hlclient::platform::WindowEvent event;while(window.poll_event(event)){need(event.type!=hlclient::platform::WindowEventType::quit_requested&&event.type!=hlclient::platform::WindowEventType::native_event_limit_exceeded,"Crossfire timing cancelled/event overflow");if(event.type==hlclient::platform::WindowEventType::resized)need(event.extent.width==1920&&event.extent.height==1080,"Crossfire timing resized");}
        if(frame==std::uint64_t(o.warm)+1)hlc_background_sky_reset_stats();
        const auto begin=Clock::now();queries.begin(frame);gpu.render(p.scene,extent);queries.end();const auto rendered=Clock::now();window.swap_buffers();const auto completed=Clock::now();
        if(frame>o.warm)rows.push_back({frame,us(rendered-begin),previous?us(completed-*previous):0.0,us(completed-rendered),hlc_background_sky_stats(),gpu.statistics()});
        previous=completed;
        need(gpu.scene_target_statistics().actual_samples==o.msaa,"Crossfire timed MSAA fallback");
    }
    // One post-window drain only; no availability/results/readbacks/sleep/fence
    // in the measured loop, and no hidden glFinish anywhere in this control.
    std::map<std::uint64_t,gl::OpenGlGpuFrameTiming> timings;for(const auto& t:queries.drain())timings.emplace(t.frame_number,t);
    std::vector<double> cpu,interval,gpu_total;cpu.reserve(rows.size());interval.reserve(rows.size());gpu_total.reserve(rows.size());std::size_t invalid{};
    std::ofstream raw(o.output/L"samples.jsonl");raw<<std::setprecision(17);
    constexpr std::array<double,5> thresholds{10000.,1000000./60.,1000000./30.,50000.,100000.};std::array<std::uint64_t,5> long_cpu{},long_interval{},long_gpu{};
    for(const auto& row:rows) {
        cpu.push_back(row.cpu_us);interval.push_back(row.interval_us);const auto found=timings.find(row.frame);
        const bool ready=found!=timings.end()&&found->second.status==gl::OpenGlGpuTimingStatus::ready;
        if(ready)gpu_total.push_back(found->second.elapsed_ms);else ++invalid;
        for(std::size_t i=0;i<thresholds.size();++i){if(row.cpu_us>thresholds[i])++long_cpu[i];if(row.interval_us>thresholds[i])++long_interval[i];if(ready&&found->second.elapsed_ms*1000.>thresholds[i])++long_gpu[i];}
        raw<<"{\"frame\":"<<row.frame<<",\"cpu_render_us\":"<<row.cpu_us<<",\"swap_completion_interval_us\":"<<row.interval_us<<",\"swap_call_us\":"<<row.swap_us<<",\"gpu_total_ms\":";
        if(ready)raw<<found->second.elapsed_ms;else raw<<"null";raw<<",\"candidate_cumulative\":";counters(raw,row.sky);
        raw<<",\"world_draws_cumulative\":"<<row.work.draw_call_count<<",\"triangles_cumulative\":"<<row.work.triangle_count<<",\"program_binds_cumulative\":"<<row.work.program_bind_count<<",\"vao_binds_cumulative\":"<<row.work.vertex_array_bind_count<<"}\n";
    }
    std::ofstream out(o.output/L"benchmark.json");out<<std::setprecision(17)<<"{\"schema\":\"hlc.stock-sky-correction.crossfire-benchmark.v1\",";metadata(out,p,o,gpu);
    out<<",\"visible_window\":true,\"actual_swap_interval\":0,\"warm_frames\":"<<o.warm<<",\"measured_frames\":"<<rows.size()<<",\"expected_static_world_draws\":"<<expected<<",\"expected_fullscreen_background_draws\":1,\"cpu_render_us\":";metric(out,cpu);
    out<<",\"swap_completion_interval_us\":";metric(out,interval);out<<",\"gpu_total_ms\":";metric(out,gpu_total);
    out<<",\"corresponding_frequency_hz\":";double mean=0;for(double v:interval)mean+=v;if(!interval.empty()&&mean>0)out<<1000000.*interval.size()/mean;else out<<"null";
    out<<",\"gpu_missing_or_invalid_measured\":"<<invalid<<",\"candidate\":";counters(out,hlc_background_sky_stats());
    const auto& q=queries.statistics();out<<",\"gpu_query_statistics\":{\"submitted\":"<<q.submitted<<",\"completed\":"<<q.completed<<",\"invalid\":"<<q.invalid_results<<",\"timeout\":"<<q.timeout_results<<",\"pending\":"<<q.pending<<",\"gl_error\":"<<q.query_result_gl_error<<"},\"long_frames\":{\"threshold_us\":[";
    for(std::size_t i=0;i<thresholds.size();++i){if(i)out<<',';out<<thresholds[i];}out<<"],\"cpu_render\":[";for(std::size_t i=0;i<thresholds.size();++i){if(i)out<<',';out<<long_cpu[i];}out<<"],\"full_interval\":[";for(std::size_t i=0;i<thresholds.size();++i){if(i)out<<',';out<<long_interval[i];}out<<"],\"gpu_total\":[";for(std::size_t i=0;i<thresholds.size();++i){if(i)out<<',';out<<long_gpu[i];}
    out<<"]},\"cpu_scope\":\"complete gpu.render plus identical test query begin/end; no asset/selection/event/swap inclusion\",\"interval_scope\":\"consecutive swap completion including event/render/swap/row bookkeeping; not scanout or gameplay FPS\",\"gpu_scope\":\"complete render GL_TIME_ELAPSED includes clear/resolve and timer hooks, excludes SDL swap\",\"software_limiter\":\"none_in_this_offline_static_control\",\"blocking_work_in_timed_loop\":false,\"readback_in_timed_loop\":false,\"per_frame_text_IO\":false,\"CPU_GPU_present_added\":false,\"acceptance\":\"SINGLE_LEG_NOT_DECIDED\"}";
    need(bool(raw)&&bool(out),"Crossfire timing outputs failed");verify_unchanged(p);gl_ok("Crossfire timing GL error");
}
} // namespace sky_crossfire_control

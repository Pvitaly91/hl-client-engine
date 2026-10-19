#if HLCLIENT_HAS_GAME_HALFLIFE
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include "gauss_world_visibility_control.hpp"
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/presentation.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/goldsrc/user_info_update.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>
#include "entity_render/entity_opengl_test_support.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "user_info_test_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <thread>

namespace {
namespace app=hlclient::app;
namespace cl=hlclient::client;
namespace api=hlclient::game_api;
namespace rr=hlclient::renderer;
namespace vv=hlclient::world_visibility;
namespace f=hlclient::tests;
using Clock=std::chrono::steady_clock;
constexpr rr::RenderExtent extent{1920,1080};
constexpr hlclient::assets::AssetVector3 marker_eye{-983.0078125F,712.1748657226562F,-1472.F};
struct View {const char* label;hlclient::assets::AssetVector3 eye;double yaw;};
cl::RenderCameraState camera(const View& v) {
  // camera_final_angles uses the runtime's pitch-up convention. Use the same
  // production basis and default HFOV-at-4:3 policy as LiveVisualControl.
  const auto forward=hlclient::gameplay_camera::forward_from_yaw_pitch(v.yaw,-.4);
  REQUIRE(forward);
  constexpr double radians=.01745329251994329577;
  return {v.eye,{v.eye.x+forward->x,v.eye.y+forward->y,v.eye.z+forward->z},
      {0,0,1},static_cast<float>(2*std::atan(std::tan(90*radians*.5)/(4./3.))),.1F,4096.F};
}
cl::RuntimeClientObservationState observed() {
  cl::RuntimeClientObservationState s;s.generation=s.publication_revision=1;
  s.server_time_seconds=46.141073;s.server_time_metadata.generation=s.client_metadata.generation=1;
  s.entity_metadata={1,cl::RuntimeObservationFreshness::observed_in_record,
      cl::RuntimeObservationCompleteness::complete_reconstruction,cl::RuntimeObservationSource{1,1,42,0,8}};
  s.server_time_metadata=s.entity_metadata;
  for(unsigned i=0;i<8+12;++i) {
    cl::RuntimePacketEntityObservation e;e.entity_number=i<8?i+2:i+32;
    e.ordinary_visual_schema=true;e.player_movement_schema=i<8;e.model_index=i<8?7U:(i%2?15U:16U);
    e.origin={double(marker_eye.x)+80.+double(i%5)*48.,double(marker_eye.y)-96.+double(i/5)*64.,-1500.};
    e.angles={0,180,0};e.sequence=0;e.frame=32.;e.body=0;e.skin=0;e.effects=0;e.frame_rate=0;
    e.animation_time_seconds=46.1;e.controllers.fill(127);e.blending.fill(127);
    if(i<8){e.gait_sequence=0;e.weapon_model_index=22;}
    s.packet_entities.push_back(e);
  }
  s.canonical_state_hash=cl::runtime_observation_canonical_hash(s);return s;
}
hlclient::goldsrc::ServerInfoState server_info() {
  std::vector<std::byte> b;f::readiness_fixture::append_u32_le(b,48);
  f::readiness_fixture::append_u32_le(b,1);f::readiness_fixture::append_u32_le(b,2);
  b.insert(b.end(),16,std::byte{});b.push_back(std::byte{16});b.push_back(std::byte{});b.push_back(std::byte{1});
  for(auto name:{"valve","71b97 synthetic offline scene","maps/crossfire.bsp","owned"})
    f::readiness_fixture::append_string(b,name);
  b.push_back(std::byte{});auto result=hlclient::goldsrc::ServerInfoParser{}.parse(b);REQUIRE(result);
  return std::move(*result.state);
}
void colors(api::GameClientHost& host,const cl::RuntimeClientObservationState& s) {
  for(unsigned entity=2;entity<=9;++entity) {
    const auto info="\\topcolor\\"+std::to_string(entity*23)+"\\bottomcolor\\"+std::to_string(255-entity*19);
    const auto bytes=hlclient::test::user_info_fixture::make_message(static_cast<std::uint8_t>(entity-1),
        static_cast<std::int32_t>(entity+40),info);
    const auto decoded=hlclient::goldsrc::UserInfoUpdateParser{}.parse(bytes);REQUIRE(decoded);
    api::PlayerDisplayUpdate update{entity,true,true,"Synthetic",decoded.state->player_appearance()};
    api::GameMessageView message{api::GameMessageKind::player_display,{},{},
        {entity,entity,entity,0,512},{},update};
    auto staged=host.stage_record({entity==2?nullptr:&s,s,std::span{&message,1U},1,16});REQUIRE(staged);
    host.commit_record(std::move(*staged.state));
  }
}
// Sidecar only: records existing PVS/frustum visibility without changing the
// normal production scene's visibility publication or inventing server brushes.
auto sidecar(const hlclient::world_scene_render::WorldSceneRenderPackage& map,const rr::RenderCamera& c) {
  std::vector<vv::WorldVisibilityBrushInstanceInput> brushes;
  for(const auto& b:map.brush_instances())brushes.push_back({b.source_instance_index,b.transformed_bounds,
      b.touched_leaf_indices,b.support_status==hlclient::world_scene_render::BrushSubmodelRenderSupportStatus::supported_static_opaque && b.source_model_index.has_value()});
  vv::WorldVisibilityResolveInput input{&map.spatial_package(),map.world_surfaces(),brushes,c,extent,
      vv::WorldVisibilityMode::pvs_and_frustum,vv::WorldPvsFallbackPolicy::frustum_only,1,map.visibility_scene_identity(),false};
  auto result=vv::WorldVisibilityResolver{}.resolve(input);INFO((result.error?result.error->message:"ready"));REQUIRE(result);
  return std::move(*result.visibility);
}
void quantile(const char* view,bool capture,const char* scope,std::vector<double> values) {
  if(values.empty()) {std::cout<<"[71b97-perf] {\"view\":\""<<view<<"\",\"remote_capture\":"<<(capture?"true":"false")<<",\"scope\":\""<<scope<<"\",\"status\":\"unavailable\"}\n";return;}
  std::sort(values.begin(),values.end());
  const auto q=[&](double p){return values[static_cast<std::size_t>(std::ceil(p*double(values.size())))-1];};
  std::cout<<"[71b97-perf] {\"view\":\""<<view<<"\",\"remote_capture\":"<<(capture?"true":"false")
      <<",\"scope\":\""<<scope<<"\",\"unit\":\"ms\",\"percentile\":\"nearest_rank\",\"n\":"<<values.size()
      <<",\"p50\":"<<q(.5)<<",\"p95\":"<<q(.95)<<",\"p99\":"<<q(.99)<<",\"max\":"<<values.back()<<"}\n";
}
}

TEST_CASE("71b97 opt-in actual Crossfire matched cameras expose production preparation and GPU costs",
    "[gauss-71b97-camera][actual-assets][opengl][benchmark]") {
  std::filesystem::path root;
  bool selected_visibility=true;
#ifdef _WIN32
  wchar_t* value=nullptr;std::size_t count{};
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0&&value)root=value;
  std::free(value);
  char* mode=nullptr;std::size_t mode_count{};
  if(_dupenv_s(&mode,&mode_count,"HLCLIENT_71B97_VISIBILITY")==0&&mode) {
    REQUIRE((std::string_view{mode}=="all"||std::string_view{mode}=="selected"));
    selected_visibility=std::string_view{mode}=="selected";
  }
  std::free(mode);
#endif
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("Exact approved read-only local asset root opt-in required");
  constexpr std::array names{"maps/crossfire.bsp","models/player.mdl","models/w_gauss.mdl",
      "models/w_battery.mdl","models/v_9mmhandgun.mdl","models/p_9mmhandgun.mdl","models/playert.mdl"};
  std::array<std::filesystem::file_time_type,names.size()> modified{};
  for(std::size_t i=0;i<names.size();++i){REQUIRE(std::filesystem::is_regular_file(root/"valve"/names[i]));modified[i]=std::filesystem::last_write_time(root/"valve"/names[i]);}
  auto gl=f::entity_opengl_fixture::try_context(extent.width,extent.height);
  if(!gl||!f::entity_opengl_fixture::capable_context())SKIP("OpenGL3.3 actual context unavailable");
  gl->initialize_renderer();auto& renderer=gl->renderer();
  std::cout<<std::setprecision(std::numeric_limits<double>::max_digits10);
  const auto& hardware=renderer.information();
  std::cout<<"[71b97-hardware] {\"vendor\":"<<std::quoted(hardware.vendor)
      <<",\"device\":"<<std::quoted(hardware.device)<<",\"version\":"<<std::quoted(hardware.version)<<"}\n";
  std::cout<<"[71b97-fixture] {\"fixture_version\":2,\"phase\":\"fixed_server_time_and_animation\",\"visibility\":\""<<(selected_visibility?"selected":"all")
      <<"\",\"hidden_context\":true,\"swaps\":false,\"historical_replay\":false,\"width\":1920,\"height\":1080,\"requested_msaa\":8} \n";
  const auto resources=f::readiness_fixture::parse_resource_list({{2,names[0],9,0,0},{2,names[1],7,0,0},
      {2,names[2],15,0,0},{2,names[3],16,0,0},{2,names[4],21,0,0},{2,names[5],22,0,0}});
  auto made=app::RuntimeReplayLocalAssets::create(resources,server_info(),root,"valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client,{},{},"desert");
  INFO((made.error?made.error->context:"ready"));REQUIRE(made.projection);
  auto& assets=*made.projection;cl::ClientWorldState world;auto s=observed();
  REQUIRE(cl::valid_runtime_observation(s));
  const auto initialized=assets.reset_generation(s,world);
  INFO((initialized.error?initialized.error->context:"reset ready"));REQUIRE(initialized);
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});colors(host,s);
  const hlclient::assets::ModelAsset* base_player=nullptr;
  for(const auto& record:world.entity_scene()->asset_library()->records())
    if(record.manifest_model_slot()==7&&record.model_asset())base_player=record.model_asset().get();
  REQUIRE(base_player);REQUIRE(base_player->skeletal_data);
  std::size_t expected_appearance_owners{};
  for(unsigned entity=2;entity<=9;++entity) {
    const auto appearance=host.player_appearance(entity,"models/player.mdl");
    REQUIRE(appearance.model_name.empty());
    REQUIRE(appearance.fields.top_color==entity*23);
    REQUIRE(appearance.fields.bottom_color==255-entity*19);
    // Original player.mdl/playert.mdl may have no DM_Base texture at all.
    // Preserve those exact textures instead of substituting a remappable model.
    if(!host.player_palette(appearance.fields,*base_player->skeletal_data).empty())++expected_appearance_owners;
  }
  const auto model=assets.presentation_model(1,21);REQUIRE(model);
  auto local=s;local.receiving_client.emplace();local.receiving_client->health=100;local.receiving_client->viewmodel_index=21;
  local.client_metadata=local.server_time_metadata;
  local.weapon_hud.active_weapon_id=2;
  local.canonical_state_hash=cl::runtime_observation_canonical_hash(local);
  REQUIRE(cl::valid_runtime_observation(local));
  hlclient::games::halflife::HalfLifePresentation presentation;
  const auto held=presentation.viewmodel(local,model,46.141073,{});REQUIRE(held.status==api::ViewmodelStatus::ready);
  std::vector<View> views{{"f8",marker_eye,-36.000854},{"f8_yaw90",marker_eye,53.999146},
      {"f8_yaw180",marker_eye,143.999146},{"approach_128",{-1086.56F,787.41F,-1472},-36.000854},
      {"adjacent_128",{-907.77F,815.73F,-1472},-36.000854}};
  views.push_back({"f8_yaw_sweep",marker_eye,-36.000854});
  // A read-only map-derived sloped-surface control; it is explicitly NOT
  // identified as the user's Shotgun ramp without a recorded camera position.
  const auto& map=*assets.surface_scene();
  for(const auto& vertex:map.world_package()->vertices())if(vertex.normal.z>.35F&&vertex.normal.z<.95F) {
    views.push_back({"sloped_surface_not_verified_shotgun",{vertex.position.x,vertex.position.y,vertex.position.z+40},-36.000854});break;
  }
  std::uint64_t next_frame=1;
  constexpr unsigned warm=48,samples=192;
  for(const auto& view:views)for(bool capture:{false,true}) {
    world.set_camera(camera(view));
    f::RuntimeWorldVisibility selector;
    std::array<std::vector<double>,5> cpu;
    std::array<std::vector<double>,7> gpu;
    std::map<std::uint64_t,bool> accepted;
    rr::RenderScene scene;
    const auto collect=[&]{while(auto timing=renderer.poll_gpu_timing()) {
      const auto found=accepted.find(timing->frame_number);
      if(found==accepted.end())continue;
      if(found->second&&timing->status==rr::opengl::OpenGlGpuTimingStatus::ready) {
        gpu[0].push_back(timing->elapsed_ms);
        for(std::size_t pass=0;pass<6;++pass)if(timing->valid_pass_mask&(1U<<pass))gpu[pass+1].push_back(timing->pass_elapsed_ms[pass]);
      }
      accepted.erase(found);
    }};
    std::uint64_t world_draws{},world_triangles{},studio_draws{},sprite_draws{},pose_updates{},base_binds{},lightmap_binds{},model_binds{};
    std::uint64_t world_uploads{},studio_uploads{},sprite_uploads{},program_binds{},vao_binds{},sky_binds{},transform_uploads{},camera_uploads{},light_uploads{};
    double pixel_yaw=view.yaw;
    unsigned slot_wait_timeouts{};
    for(unsigned i=0;i<warm+samples;++i) {
      // Test-only bounded back-pressure outside CPU measurement prevents the
      // eight-slot asynchronous query ring from biasing GPU sample selection.
      // This is deliberately not part of the normal nonblocking render loop.
      const auto slot_deadline=Clock::now()+std::chrono::seconds(5);
      while(renderer.gpu_timing_statistics().pending>=6&&Clock::now()<slot_deadline) {
        collect();std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if(renderer.gpu_timing_statistics().pending>=6)++slot_wait_timeouts;
      auto current_view=view;
      if(std::string_view{view.label}=="f8_yaw_sweep")current_view.yaw+=static_cast<double>(i)*.05;
      pixel_yaw=current_view.yaw;
      world.set_camera(camera(current_view));
      const auto begin=Clock::now();
      const auto result=assets.present_entities(host,100,1,world,extent,capture);
      const auto presented=Clock::now();
      scene=cl::build_render_scene(world);scene.skybox=assets.skybox();
      scene.first_person_entities=assets.materialize_viewmodel(held,view.eye);
      scene.requested_msaa_samples=8;scene.image_adjustments={.1F,1.04F,1.1F};
      const auto prepared=Clock::now();
      bool visibility_ok=true;
      if(selected_visibility)visibility_ok=selector.apply(scene,extent);
      const auto culled=Clock::now();
      const auto ws=renderer.statistics();const auto es=renderer.entity_statistics();
      const auto id=next_frame++;accepted.emplace(id,i>=warm);
      const auto timing_submitted=renderer.gpu_timing_statistics().submitted;
      const auto render_begin=Clock::now();
      renderer.render_with_gpu_timing(scene,extent,id);
      if(renderer.gpu_timing_statistics().submitted==timing_submitted)accepted.erase(id);
      const auto submitted=Clock::now();collect();
      // Test-only driver flush. No glFinish/query-result wait in renderer or
      // normal gameplay. No swap: hidden fixture measures submissions/GPU, not VSync/scanout.
      glFlush();
      const auto ended=Clock::now();
      if(!result){INFO((result.error?result.error->context:"presentation failed without error"));REQUIRE(result);}
      REQUIRE(result);REQUIRE(scene.first_person_entities);REQUIRE(visibility_ok);
      if(i>=warm) {
        cpu[0].push_back(std::chrono::duration<double,std::milli>(presented-begin).count());
        cpu[1].push_back(std::chrono::duration<double,std::milli>(prepared-presented).count());
        cpu[2].push_back(std::chrono::duration<double,std::milli>(culled-prepared).count());
        cpu[3].push_back(std::chrono::duration<double,std::milli>(submitted-render_begin).count());
        cpu[4].push_back(std::chrono::duration<double,std::milli>(ended-begin).count());
        const auto& w=renderer.statistics();const auto& e=renderer.entity_statistics();
        world_draws+=w.draw_call_count-ws.draw_call_count;world_triangles+=w.triangle_count-ws.triangle_count;
        base_binds+=w.base_texture_bind_count-ws.base_texture_bind_count;lightmap_binds+=w.lightmap_bind_count-ws.lightmap_bind_count;
        studio_draws+=e.studio_draw_count-es.studio_draw_count;sprite_draws+=e.sprite_draw_count-es.sprite_draw_count;
        pose_updates+=e.pose_ubo_update_count-es.pose_ubo_update_count;model_binds+=e.model_texture_bind_count-es.model_texture_bind_count;
        world_uploads+=w.upload_count-ws.upload_count;studio_uploads+=e.studio_asset_upload_count-es.studio_asset_upload_count;
        sprite_uploads+=e.sprite_asset_upload_count-es.sprite_asset_upload_count;
        program_binds+=w.program_bind_count-ws.program_bind_count;vao_binds+=w.vertex_array_bind_count-ws.vertex_array_bind_count;
        sky_binds+=w.sky_texture_bind_count-ws.sky_texture_bind_count;transform_uploads+=w.transform_upload_count-ws.transform_upload_count;
        camera_uploads+=w.camera_upload_count-ws.camera_upload_count;light_uploads+=w.light_upload_count-ws.light_upload_count;
      }
    }
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(!accepted.empty()&&Clock::now()<deadline){collect();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(glGetError()==GL_NO_ERROR);
    const auto pixels=renderer.observe_framebuffer(extent,scene.clear_color,false);REQUIRE(pixels);
    const auto visibility=sidecar(map,scene.camera);
    const auto& v=visibility.statistics();const auto& target=renderer.scene_target_statistics();
    const auto& timing=renderer.gpu_timing_statistics();const auto& summary=assets.summary();
    REQUIRE(world.entity_scene()->presentation_studio_sources().size()==expected_appearance_owners);
    std::uint64_t studio_triangles{},studio_vertices{},studio_bones{};
    const auto geometry=[&](const rr::RenderDynamicEntities& entities) {
      for(const auto& instance:entities.frame->studio_instances()) {
        if(instance.visibility_status!=hlclient::entity_render::RuntimeEntityVisibilityStatus::visible)continue;
        const auto& asset=*entities.package->studio_assets()[instance.studio_asset_index];
        const auto submodels=asset.select_submodels(instance.body_value);REQUIRE(submodels);
        studio_bones+=entities.frame->studio_poses()[instance.pose_index].bone_matrices.size();
        for(const auto index:submodels.submodel_indices) {
          const auto& submodel=asset.submodels()[index];studio_vertices+=submodel.vertex_count;
          studio_triangles+=submodel.index_count/3U;
        }
      }
    };
    if(scene.dynamic_entities)geometry(*scene.dynamic_entities);geometry(*scene.first_person_entities);
    std::size_t mesh_bytes{};
    for(const auto& rec:world.entity_scene()->asset_library()->records())if(rec.manifest_model_slot()==7&&rec.model_asset())
      mesh_bytes=rec.model_asset()->vertices.size()*sizeof(hlclient::assets::ModelVertex)+rec.model_asset()->indices.size()*sizeof(std::uint32_t);
    std::cout<<"[71b97-scene] {\"view\":\""<<view.label<<"\",\"remote_capture\":"<<(capture?"true":"false")
      <<",\"fixture_version\":2,\"phase\":\"fixed_server_time_and_animation\",\"server_time\":46.141073,\"animation_frame\":32"
      <<",\"visibility\":\""<<(selected_visibility?"selected":"all")<<'"'
      <<",\"eye\":["<<view.eye.x<<','<<view.eye.y<<','<<view.eye.z<<"],\"yaw\":"<<view.yaw
      <<",\"pixel_eye\":["<<scene.camera.position.x<<','<<scene.camera.position.y<<','<<scene.camera.position.z<<"],\"pixel_yaw\":"<<pixel_yaw
      <<",\"pixel_target\":["<<scene.camera.target.x<<','<<scene.camera.target.y<<','<<scene.camera.target.z<<"]"
      <<",\"pitch\":-0.4,\"vertical_fov_radians\":"<<scene.camera.vertical_field_of_view_radians
      <<",\"near_plane\":"<<scene.camera.near_plane<<",\"far_plane\":"<<scene.camera.far_plane
      <<",\"historical_replay\":false,\"synthetic_players\":8,\"synthetic_pickups\":12,\"effects\":0,\"decals\":0,\"lights\":0"
      <<",\"leaf\":"<<visibility.camera_leaf_index().value_or(0)<<",\"pvs_candidates\":"<<v.pvs_candidate_world_surface_count
      <<",\"frustum_visible\":"<<v.frustum_visible_world_surface_count<<",\"sidecar_visible_surfaces\":"<<v.visible_world_surface_count
      <<",\"actual_visible_list\":"<<(scene.static_world->visible_draw_list?"true":"false")
      <<",\"world_draws\":"<<world_draws<<",\"world_triangles\":"<<world_triangles<<",\"studio_draws\":"<<studio_draws
      <<",\"sprite_draws\":"<<sprite_draws<<",\"pose_updates\":"<<pose_updates<<",\"base_binds\":"<<base_binds
      <<",\"lightmap_binds\":"<<lightmap_binds<<",\"model_binds\":"<<model_binds<<",\"frames\":"<<samples
      <<",\"world_uploads\":"<<world_uploads<<",\"studio_uploads\":"<<studio_uploads<<",\"sprite_uploads\":"<<sprite_uploads
      <<",\"program_binds\":"<<program_binds<<",\"vao_binds\":"<<vao_binds<<",\"sky_binds\":"<<sky_binds
      <<",\"transform_uploads\":"<<transform_uploads<<",\"camera_uploads\":"<<camera_uploads<<",\"light_uploads\":"<<light_uploads
      <<",\"final_visible_studio_triangles\":"<<studio_triangles<<",\"final_visible_studio_vertices\":"<<studio_vertices
      <<",\"final_visible_studio_bones\":"<<studio_bones
      <<",\"ordinary_pose_evaluations\":"<<summary.ordinary_pose_evaluations<<",\"ordinary_pose_reuses\":"<<summary.ordinary_pose_reuses
      <<",\"base_model_mesh_value_bytes\":"<<mesh_bytes<<",\"appearance_owners\":"<<world.entity_scene()->presentation_studio_sources().size()
      <<",\"color_only_players\":8,\"expected_appearance_owners\":"<<expected_appearance_owners
      <<",\"palette_path\":\""<<(expected_appearance_owners?"remapped_variants":"no_remappable_palette")<<'"'
      <<",\"pixel_signature\":\""<<pixels.color_signature<<"\",\"msaa\":"<<target.actual_samples<<",\"target_allocations\":"<<target.storage_allocations
      <<",\"width\":"<<target.scene_extent.width<<",\"height\":"<<target.scene_extent.height
      <<",\"resolve_width\":"<<target.resolve_extent.width<<",\"resolve_height\":"<<target.resolve_extent.height
      <<",\"hud_width\":"<<target.hud_extent.width<<",\"hud_height\":"<<target.hud_extent.height
      <<",\"selection_resolves\":"<<selector.statistics().resolves<<",\"selection_cache_hits\":"<<selector.statistics().cache_hits
      <<",\"gpu_pending\":"<<timing.pending<<",\"gpu_capacity_drops\":"<<timing.capacity_drops<<",\"gpu_unavailable\":"<<timing.unavailable
      <<",\"unobserved_frame_ids\":"<<accepted.size()<<",\"gpu_sample_count\":"<<gpu[0].size()<<",\"test_slot_wait_timeouts\":"<<slot_wait_timeouts<<"}\n";
    constexpr std::array cpu_names{"studio_prepare","viewmodel_scene_prepare","world_visibility","render_submit","owned_subset"};
    constexpr std::array gpu_names{"gpu_total","gpu_world","gpu_studio_viewmodel","gpu_effects_decals","gpu_msaa_resolve","gpu_image_adjustments","gpu_hud_console"};
    for(std::size_t i=0;i<cpu.size();++i)quantile(view.label,capture,cpu_names[i],std::move(cpu[i]));
    for(std::size_t i=0;i<gpu.size();++i)quantile(view.label,capture,gpu_names[i],std::move(gpu[i]));
  }
  for(std::size_t i=0;i<names.size();++i)CHECK(std::filesystem::last_write_time(root/"valve"/names[i])==modified[i]);
  host.teardown();gl->release_renderer();
}
#endif

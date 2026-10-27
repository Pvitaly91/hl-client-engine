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
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
struct View {const char* label;hlclient::assets::AssetVector3 eye;double yaw;double pitch{-0.4};};
cl::RenderCameraState camera(const View& v) {
  // camera_final_angles uses the runtime's pitch-up convention. Use the same
  // production basis and default HFOV-at-4:3 policy as LiveVisualControl.
  const auto forward=hlclient::gameplay_camera::forward_from_yaw_pitch(v.yaw,v.pitch);
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
std::string optional_environment(const char* name) {
#ifdef _WIN32
  char* value=nullptr;std::size_t count{};
  if(_dupenv_s(&value,&count,name)!=0)return {};
  const std::string result=value?value:"";std::free(value);return result;
#else
  const char* value=std::getenv(name);return value?value:"";
#endif
}
struct RawWorldSample {
  std::array<double,5> cpu{};
  std::optional<double> gpu_total;
  std::array<std::optional<double>,6> gpu_pass{};
  std::array<std::optional<double>,4> world_subpass{};
  std::uint8_t pass_mask{},world_subpass_mask{};
  std::uint64_t light_input_signature{};
};

// Synthetic controlled presentation lights. These are not reconstructed from
// the historical run (which did not retain the owning light inputs).
void apply_dynamic_profile(rr::RenderScene& scene,const View& view,
    const std::string_view profile,const unsigned frame) {
  scene.transient_world_light.reset();scene.world_effect_light_count=0;
  auto state=profile;
  if(profile=="transition") {
    constexpr std::array states{"none","one","far","many","moved","expired"};
    state=states[(frame/16U)%states.size()];
  }
  if(state.empty()||state=="none"||state=="expired")return;
  const auto forward=hlclient::gameplay_camera::forward_from_yaw_pitch(view.yaw,view.pitch);
  REQUIRE(forward);
  const auto light_origin=hlclient::assets::AssetVector3{view.eye.x+forward->x*96.F,
      view.eye.y+forward->y*96.F,view.eye.z+forward->z*96.F};
  const rr::RenderPointLight one{light_origin,192.F,.65F*.7F,{.5F,.5F,1.F}};
  if(state=="transient") {scene.transient_world_light=one;return;}
  scene.world_effect_lights[0]=one;scene.world_effect_light_count=1;
  if(state=="far")scene.world_effect_lights[0].center={100000.F,100000.F,100000.F};
  else if(state=="moved")scene.world_effect_lights[0].center.x+=96.F;
  else if(state=="many") {
    scene.world_effect_light_count=scene.world_effect_lights.size();
    for(std::size_t i=0;i<scene.world_effect_lights.size();++i) {
      auto light=one;light.center.x+=static_cast<float>(i%4U)*96.F-144.F;
      light.center.y+=static_cast<float>(i/4U)*128.F-64.F;
      light.radius_units=128.F+static_cast<float>(i)*16.F;
      light.color={.25F+static_cast<float>(i%3U)*.2F,.5F,1.F};
      scene.world_effect_lights[i]=light;
    }
  }
}
std::uint64_t light_signature(const rr::RenderScene& scene) {
  std::uint64_t hash=14695981039346656037ULL;
  const auto word=[&](const std::uint32_t bits) {
    for(unsigned shift=0;shift<32;shift+=8){hash^=(bits>>shift)&255U;hash*=1099511628211ULL;}
  };
  const auto light=[&](const rr::RenderPointLight& value) {
    for(const float x:{value.center.x,value.center.y,value.center.z,value.radius_units,
        value.intensity,value.color[0],value.color[1],value.color[2]})word(std::bit_cast<std::uint32_t>(x));
  };
  word(scene.transient_world_light?1U:0U);if(scene.transient_world_light)light(*scene.transient_world_light);
  word(static_cast<std::uint32_t>(scene.effect_lights().size()));
  for(const auto& value:scene.effect_lights())light(value);
  return hash;
}

// Test-only interception during a separate, untimed world-only render. Captures
// the depth-owning target before resolve; no renderer or gameplay instrumentation.
struct OfflineDepthTarget {
  inline static PFNGLCLEARPROC saved{};
  inline static GLint target{};
  static void GLAD_API_PTR clear(const GLbitfield mask) {
    if((mask&(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))==(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT))
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);
    saved(mask);
  }
  OfflineDepthTarget(){saved=glad_glClear;target=0;glad_glClear=&clear;}
  ~OfflineDepthTarget(){glad_glClear=saved;}
};
std::uint64_t offline_depth_signature(const GLint source,const rr::RenderExtent size) {
  GLint old_read{},old_draw{},old_rb{},old_pack{},old_alignment{},old_row{},old_x{},old_y{};
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
  glGetIntegerv(GL_RENDERBUFFER_BINDING,&old_rb);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&old_pack);
  glGetIntegerv(GL_PACK_ALIGNMENT,&old_alignment);glGetIntegerv(GL_PACK_ROW_LENGTH,&old_row);
  glGetIntegerv(GL_PACK_SKIP_PIXELS,&old_x);glGetIntegerv(GL_PACK_SKIP_ROWS,&old_y);
  GLuint fbo{},depth{};
  if(source!=0) {
    glGenFramebuffers(1,&fbo);glGenRenderbuffers(1,&depth);
    glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,size.width,size.height);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
    glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(source));
    glBlitFramebuffer(0,0,size.width,size.height,0,0,size.width,size.height,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
  } else glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
  glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
  glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
  std::vector<float> values(static_cast<std::size_t>(size.width)*size.height);
  glReadPixels(0,0,size.width,size.height,GL_DEPTH_COMPONENT,GL_FLOAT,values.data());
  const auto error=glGetError();
  glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(old_read));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(old_draw));
  glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(old_rb));
  glBindBuffer(GL_PIXEL_PACK_BUFFER,static_cast<GLuint>(old_pack));glPixelStorei(GL_PACK_ALIGNMENT,old_alignment);
  glPixelStorei(GL_PACK_ROW_LENGTH,old_row);glPixelStorei(GL_PACK_SKIP_PIXELS,old_x);glPixelStorei(GL_PACK_SKIP_ROWS,old_y);
  if(depth)glDeleteRenderbuffers(1,&depth);if(fbo)glDeleteFramebuffers(1,&fbo);
  REQUIRE(error==GL_NO_ERROR);REQUIRE(glGetError()==GL_NO_ERROR);
  REQUIRE(std::any_of(values.begin(),values.end(),[](float v){return v<1.F;}));
  std::uint64_t hash=14695981039346656037ULL;
  for(const auto value:values)for(unsigned shift=0;shift<32;shift+=8) {
    hash^=(std::bit_cast<std::uint32_t>(value)>>shift)&255U;hash*=1099511628211ULL;
  }
  return hash;
}
template<class Timing>
void collect_world_subpasses(const Timing& timing,RawWorldSample* raw,
    std::array<std::vector<double>,4>& quantiles) {
  if constexpr(requires(const Timing& value) {
      value.valid_world_subpass_mask;value.world_subpass_elapsed_ms;}) {
    for(std::size_t pass=0;pass<quantiles.size();++pass)
      if(timing.valid_world_subpass_mask&(1U<<pass)) {
        quantiles[pass].push_back(timing.world_subpass_elapsed_ms[pass]);
        if(raw)raw->world_subpass[pass]=timing.world_subpass_elapsed_ms[pass];
      }
    if(raw)raw->world_subpass_mask=timing.valid_world_subpass_mask;
  }
}
template<class Statistics>
std::optional<std::uint64_t> material_uniform_delta(const Statistics& before,
    const Statistics& after) {
  if constexpr(requires(const Statistics& value){value.material_uniform_upload_count;})
    return after.material_uniform_upload_count-before.material_uniform_upload_count;
  else
    return std::nullopt;
}
template<class Target,class Image>
void color_input_receipt(const Target& target,const Image& image) {
  std::cout<<"[254e-color-input] {\"color_copies\":"<<image.framebuffer_color_copies
    <<",\"resolved_texture_reads\":"<<image.resolved_texture_reads
    <<",\"copy_storage_reallocations\":"<<image.storage_reallocations;
  if constexpr(requires{target.single_sample_frames;target.output_depth_blits;
      target.retained_attachment_bytes;target.peak_attachment_bytes;image.single_sample_texture_reads;}) {
    std::cout<<",\"single_sample_texture_reads\":"<<image.single_sample_texture_reads
      <<",\"single_sample_frames\":"<<target.single_sample_frames
      <<",\"output_depth_blits\":"<<target.output_depth_blits
      <<",\"retained_attachment_bytes\":"<<target.retained_attachment_bytes
      <<",\"peak_attachment_bytes\":"<<target.peak_attachment_bytes;
  }
  std::cout<<"}\n";
}
}

TEST_CASE("71b97 opt-in actual Crossfire matched cameras expose production preparation and GPU costs",
    "[gauss-71b97-camera][actual-assets][opengl][benchmark]") {
  std::filesystem::path root;
  bool selected_visibility=true;
  const auto raw_setting=optional_environment("HLCLIENT_22AD_RAW_W");
  REQUIRE((raw_setting.empty()||raw_setting=="1"));
  const bool emit_raw=raw_setting=="1";
  const auto old_comparison=optional_environment("HLCLIENT_51_OLD_VISIBILITY_DIFF");
  REQUIRE((old_comparison.empty()||old_comparison=="1"));
  const bool compare_old_visibility=old_comparison=="1";
  const auto msaa_setting=optional_environment("HLCLIENT_22AD_MSAA");
  REQUIRE((msaa_setting.empty()||msaa_setting=="0"||msaa_setting=="2"||
      msaa_setting=="4"||msaa_setting=="8"));
  const unsigned requested_msaa=msaa_setting.empty()?8U:
      static_cast<unsigned>(std::stoul(msaa_setting));
  const auto selected_view=optional_environment("HLCLIENT_22AD_VIEW");
  const auto dynamic_profile=optional_environment("HLCLIENT_9DE_LIGHT_PROFILE");
  constexpr std::array allowed_profiles{"","none","one","far","many","moved","expired","transient","transition"};
  REQUIRE(std::find(allowed_profiles.begin(),allowed_profiles.end(),dynamic_profile)!=allowed_profiles.end());
  const bool dynamic_fixture=!dynamic_profile.empty();
  const auto timing_mode=optional_environment("HLCLIENT_9DE_TIMING");
  REQUIRE((timing_mode.empty()||timing_mode=="on"||timing_mode=="off"));
  const bool timed=timing_mode!="off";
  const auto neutral_setting=optional_environment("HLCLIENT_0C29_NEUTRAL_IMAGE");
  REQUIRE((neutral_setting.empty()||neutral_setting=="1"));
  const auto capture_mode=optional_environment("HLCLIENT_9DE_CAPTURE");
  REQUIRE((capture_mode.empty()||capture_mode=="local"||capture_mode=="remote"));
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
      <<"\",\"hidden_context\":true,\"swaps\":false,\"historical_replay\":false,\"width\":1920,\"height\":1080,\"requested_msaa\":"<<requested_msaa<<"} \n";
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
  // Camera-only controls from the retained MSAA8 run. Actors, effects,
  // lighting, and animation remain this fixed synthetic fixture, not a
  // reconstruction of the historical live frame.
  views.push_back({"f093_frame3538_camera_fixed_fixture",
      {-713.9987183F,498.9496765F,-1472.F},-80.50122,-3.238726});
  views.push_back({"f093_f8_marker_camera_fixed_fixture",
      {-739.8359375F,669.09375F,-1472.F},-172.80122,-14.0});
  // movement_frame.camera_final_angles records {presented_pitch, presented_yaw,
  // roll}, not {yaw, pitch, roll}. The production camera basis uses positive
  // pitch for +Z (up), so these negative pitches look downward. Only the
  // camera is retained from the MSAA8 run; all actors, lights, effects,
  // animation and FOV remain this fixture's fixed synthetic inputs.
  views.push_back({"msaa8_frame31834_A_camera_fixed_fixture",
      {-815.96875F,484.1993713378906F,-1472.F},68.39793,-1.342709});
  views.push_back({"msaa8_frame39014_B_camera_fixed_fixture",
      {-890.2877807617188F,597.84228515625F,-1472.F},-27.864029,-15.853333});
  views.push_back({"9de_primary_fixed",{-952.3203125F,1120.1875F,-1472.F},-6.3167515,-10.773334});
  views.push_back({"9de_secondary_fixed",{-952.3203125F,1120.1875F,-1472.F},-40.81675,-10.773334});
  views.push_back({"9de_primary_sweep",{-952.3203125F,1120.1875F,-1472.F},-6.3167515,-10.773334});
  // Only the recorded camera is retained. Actors/materials/lights/time below
  // remain the explicit fixed fixture, not a replay of the Windows10 match.
  views.push_back({"50b5_frame2875_camera_fixed_fixture",
      {-898.7626342773438F,54.29488754272461F,-1471.9969482421875F},18.2,-4.5});
  views.push_back({"50b5_frame2908_camera_fixed_fixture",
      {-917.7095947265625F,224.34974670410156F,-1471.982666015625F},27.0,-3.2});
  // Only position and pitch-up/yaw are a historical seed. Fixed fixture time,
  // actors and explicit light profiles below are NOT a reconstruction of 12854.
  views.push_back({"254e_frame12854_camera_fixed_fixture",
      {569.95849609375F,458.0836486816406F,-1791.98046875F},108.69914,-3.5});
  // A read-only map-derived sloped-surface control; it is explicitly NOT
  // identified as the user's Shotgun ramp without a recorded camera position.
  const auto& map=*assets.surface_scene();
  for(const auto& vertex:map.world_package()->vertices())if(vertex.normal.z>.35F&&vertex.normal.z<.95F) {
    views.push_back({"sloped_surface_not_verified_shotgun",{vertex.position.x,vertex.position.y,vertex.position.z+40},-36.000854});break;
  }
  std::uint64_t next_frame=1;
  constexpr unsigned warm=48,samples=192;
  std::size_t matched_views{};
  for(const auto& view:views) {
    if(!selected_view.empty()&&selected_view!=view.label)continue;
    ++matched_views;
  for(bool capture:{false,true}) {
    if((capture_mode=="local"&&capture)||(capture_mode=="remote"&&!capture))continue;
    world.set_camera(camera(view));
    f::RuntimeWorldVisibility selector;
    std::array<std::vector<double>,5> cpu;
    std::array<std::vector<double>,7> gpu;
    std::array<std::vector<double>,4> world_gpu;
    std::vector<RawWorldSample> raw_samples(emit_raw?samples:0U);
    const auto first_sample_frame=next_frame+warm;
    std::map<std::uint64_t,bool> accepted;
    rr::RenderScene scene;
    std::uint64_t invalid_timing_samples{};
    const auto collect=[&]{while(auto timing=renderer.poll_gpu_timing()) {
      const auto found=accepted.find(timing->frame_number);
      if(found==accepted.end())continue;
      if(found->second&&timing->status==rr::opengl::OpenGlGpuTimingStatus::ready) {
        gpu[0].push_back(timing->elapsed_ms);
        for(std::size_t pass=0;pass<6;++pass)if(timing->valid_pass_mask&(1U<<pass))gpu[pass+1].push_back(timing->pass_elapsed_ms[pass]);
        RawWorldSample* raw=nullptr;
        if(emit_raw&&timing->frame_number>=first_sample_frame &&
            timing->frame_number-first_sample_frame<raw_samples.size()) {
          raw=&raw_samples[static_cast<std::size_t>(timing->frame_number-first_sample_frame)];
          raw->gpu_total=timing->elapsed_ms;
          raw->pass_mask=timing->valid_pass_mask;
          for(std::size_t pass=0;pass<raw->gpu_pass.size();++pass)
            if(timing->valid_pass_mask&(1U<<pass))
              raw->gpu_pass[pass]=timing->pass_elapsed_ms[pass];
        }
        collect_world_subpasses(*timing,raw,world_gpu);
      }
      else if(found->second)++invalid_timing_samples;
      accepted.erase(found);
    }};
    std::uint64_t world_draws{},world_triangles{},studio_draws{},sprite_draws{},pose_updates{},base_binds{},lightmap_binds{},model_binds{};
    std::uint64_t world_uploads{},studio_uploads{},sprite_uploads{},program_binds{},vao_binds{},sky_binds{},transform_uploads{},camera_uploads{},light_uploads{},material_uniform_uploads{};
    std::uint64_t prepared_selected_blocks{},prepared_rejected_blocks{},prepared_batch_fallbacks{};
    std::uint64_t prepared_probed_blocks{},prepared_all_batch_bypass_frames{};
    double pixel_yaw=view.yaw;
    unsigned slot_wait_timeouts{};
    double cold_render_ms{};
    std::uint64_t omitted_timing_samples{},light_state_changes{},previous_lights{};
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
      if(std::string_view{view.label}=="f8_yaw_sweep"||std::string_view{view.label}=="9de_primary_sweep")
        current_view.yaw+=static_cast<double>(i)*.05;
      pixel_yaw=current_view.yaw;
      world.set_camera(camera(current_view));
      const auto begin=Clock::now();
      const auto result=assets.present_entities(host,100,1,world,extent,capture);
      const auto presented=Clock::now();
      scene=cl::build_render_scene(world);scene.skybox=assets.skybox();
      scene.first_person_entities=assets.materialize_viewmodel(held,view.eye);
      scene.requested_msaa_samples=requested_msaa;scene.image_adjustments=neutral_setting=="1"
          ?hlclient::core::ImageAdjustments{}:hlclient::core::ImageAdjustments{.1F,1.04F,1.1F};
      apply_dynamic_profile(scene,view,dynamic_profile,i);
      const auto light_inputs=light_signature(scene);
      const auto prepared=Clock::now();
      bool visibility_ok=true;
      if(selected_visibility)visibility_ok=selector.apply(scene,extent);
      const auto culled=Clock::now();
      const auto ws=renderer.statistics();const auto es=renderer.entity_statistics();
      const auto id=next_frame++;if(timed)accepted.emplace(id,i>=warm);
      const auto timing_submitted=renderer.gpu_timing_statistics().submitted;
      const auto render_begin=Clock::now();
      if(timed)renderer.render_with_gpu_timing(scene,extent,id);else renderer.render(scene,extent);
      if(i==0U&&renderer.scene_target_statistics().actual_samples!=requested_msaa)
        SKIP("Requested MSAA mode is unavailable in the actual OpenGL context");
      if(timed&&renderer.gpu_timing_statistics().submitted==timing_submitted) {
        accepted.erase(id);if(i>=warm)++omitted_timing_samples;
      }
      const auto submitted=Clock::now();collect();
      if(i==0U)cold_render_ms=std::chrono::duration<double,std::milli>(submitted-render_begin).count();
      // Test-only driver flush. No glFinish/query-result wait in renderer or
      // normal gameplay. No swap: hidden fixture measures submissions/GPU, not VSync/scanout.
      glFlush();
      const auto ended=Clock::now();
      if(!result){INFO((result.error?result.error->context:"presentation failed without error"));REQUIRE(result);}
      REQUIRE(result);REQUIRE(scene.first_person_entities);REQUIRE(visibility_ok);
      if(i>=warm) {
        const std::array durations{
          std::chrono::duration<double,std::milli>(presented-begin).count(),
          std::chrono::duration<double,std::milli>(prepared-presented).count(),
          std::chrono::duration<double,std::milli>(culled-prepared).count(),
          std::chrono::duration<double,std::milli>(submitted-render_begin).count(),
          std::chrono::duration<double,std::milli>(ended-begin).count()};
        for(std::size_t stage=0;stage<cpu.size();++stage)cpu[stage].push_back(durations[stage]);
        if(emit_raw){raw_samples[i-warm].cpu=durations;raw_samples[i-warm].light_input_signature=light_inputs;}
        if(i==warm||previous_lights!=light_inputs)++light_state_changes;
        previous_lights=light_inputs;
        const auto& w=renderer.statistics();const auto& e=renderer.entity_statistics();
        world_draws+=w.draw_call_count-ws.draw_call_count;world_triangles+=w.triangle_count-ws.triangle_count;
        prepared_selected_blocks+=w.prepared_world_selected_block_count-ws.prepared_world_selected_block_count;
        prepared_rejected_blocks+=w.prepared_world_rejected_block_count-ws.prepared_world_rejected_block_count;
        prepared_batch_fallbacks+=w.prepared_world_full_batch_fallback_count-ws.prepared_world_full_batch_fallback_count;
        prepared_probed_blocks+=w.prepared_world_probed_block_count-ws.prepared_world_probed_block_count;
        prepared_all_batch_bypass_frames+=w.prepared_world_all_batch_bypass_frame_count-ws.prepared_world_all_batch_bypass_frame_count;
        base_binds+=w.base_texture_bind_count-ws.base_texture_bind_count;lightmap_binds+=w.lightmap_bind_count-ws.lightmap_bind_count;
        studio_draws+=e.studio_draw_count-es.studio_draw_count;sprite_draws+=e.sprite_draw_count-es.sprite_draw_count;
        pose_updates+=e.pose_ubo_update_count-es.pose_ubo_update_count;model_binds+=e.model_texture_bind_count-es.model_texture_bind_count;
        world_uploads+=w.upload_count-ws.upload_count;studio_uploads+=e.studio_asset_upload_count-es.studio_asset_upload_count;
        sprite_uploads+=e.sprite_asset_upload_count-es.sprite_asset_upload_count;
        program_binds+=w.program_bind_count-ws.program_bind_count;vao_binds+=w.vertex_array_bind_count-ws.vertex_array_bind_count;
        sky_binds+=w.sky_texture_bind_count-ws.sky_texture_bind_count;transform_uploads+=w.transform_upload_count-ws.transform_upload_count;
        camera_uploads+=w.camera_upload_count-ws.camera_upload_count;light_uploads+=w.light_upload_count-ws.light_upload_count;
        if(const auto uniform_count=material_uniform_delta(ws,w))material_uniform_uploads+=*uniform_count;
      }
    }
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(!accepted.empty()&&Clock::now()<deadline){collect();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(glGetError()==GL_NO_ERROR);
    const auto pixels=renderer.observe_framebuffer(extent,scene.clear_color,false);REQUIRE(pixels);
    if(dynamic_fixture) {
      // A separate render avoids first-person depth reset. Use the same ordered
      // world inputs and camera; no swaps/readback occur in the timed loop.
      const std::vector<std::string_view> proof_states=dynamic_profile=="transition"
          ?std::vector<std::string_view>{"none","one","far","many","moved","expired","one","none"}
          :std::vector<std::string_view>{dynamic_profile};
      std::size_t proof_index{};
      for(const auto state:proof_states) {
        auto proof=scene;proof.dynamic_entities.reset();proof.first_person_entities.reset();
        apply_dynamic_profile(proof,view,state,0U);
        GLint depth_target{};
        {OfflineDepthTarget capture_depth;renderer.render(proof,extent);depth_target=OfflineDepthTarget::target;}
        const auto proof_color=renderer.observe_framebuffer(extent,proof.clear_color,false);REQUIRE(proof_color);
        const auto proof_depth=offline_depth_signature(depth_target,extent);
        std::cout<<"[9de-equivalence] {\"view\":"<<std::quoted(view.label)
            <<",\"profile\":"<<std::quoted(dynamic_profile)<<",\"proof_state\":"<<std::quoted(state)
            <<",\"proof_index\":"<<proof_index++<<",\"requested_msaa\":"<<requested_msaa
            <<",\"remote_capture\":"<<(capture?"true":"false")<<",\"color_signature\":\""<<proof_color.color_signature
            <<"\",\"depth_signature\":\""<<proof_depth<<"\",\"depth_scope\":\"world_only_owned_depth_resolved_nearest\""
            <<",\"light_input_signature\":\""<<light_signature(proof)<<"\",\"transient_light\":";
        const auto print_light=[](const rr::RenderPointLight& light) {
          std::cout<<"{\"center\":["<<light.center.x<<','<<light.center.y<<','<<light.center.z
              <<"],\"radius\":"<<light.radius_units<<",\"intensity\":"<<light.intensity
              <<",\"color\":["<<light.color[0]<<','<<light.color[1]<<','<<light.color[2]<<"]}";
        };
        if(proof.transient_world_light)print_light(*proof.transient_world_light);else std::cout<<"null";
        std::cout<<",\"effect_lights\":[";
        for(std::size_t n=0;n<proof.effect_lights().size();++n){if(n)std::cout<<',';print_light(proof.effect_lights()[n]);}
        std::cout<<"]}\n";
      }
    }
    // The first-person pass clears depth, so the completed full frame cannot
    // prove world depth correctness. Draw the same static-world camera alone
    // after all timed samples, and hash its default depth only in Off mode.
    // MSAA resolves color only; default depth there would be false evidence.
    std::optional<std::uint64_t> depth_signature;
    std::optional<std::size_t> depth_nonclear_pixels;
    std::optional<std::array<float,2>> depth_range;
    if(requested_msaa==0U) {
      rr::RenderScene world_only;
      world_only.clear_color=scene.clear_color;
      world_only.camera=scene.camera;
      world_only.static_world=scene.static_world;
      world_only.skybox=scene.skybox;
      renderer.render(world_only,extent);
      const auto baseline_world_color=compare_old_visibility&&!selected_visibility
          ?renderer.observe_framebuffer(extent,scene.clear_color,true)
          :hlclient::renderer::opengl::OpenGlFramebufferObservation{};
      GLint read_fbo{};
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);
      if(read_fbo==0) {
        std::vector<float> depth(static_cast<std::size_t>(extent.width)*extent.height);
        GLint old_pack{},old_alignment{},old_row_length{},old_skip_pixels{},old_skip_rows{};
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&old_pack);
        glGetIntegerv(GL_PACK_ALIGNMENT,&old_alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH,&old_row_length);
        glGetIntegerv(GL_PACK_SKIP_PIXELS,&old_skip_pixels);
        glGetIntegerv(GL_PACK_SKIP_ROWS,&old_skip_rows);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        glPixelStorei(GL_PACK_SKIP_ROWS,0);
        glReadPixels(0,0,extent.width,extent.height,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
        const auto read_error=glGetError();
        glPixelStorei(GL_PACK_ALIGNMENT,old_alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH,old_row_length);
        glPixelStorei(GL_PACK_SKIP_PIXELS,old_skip_pixels);
        glPixelStorei(GL_PACK_SKIP_ROWS,old_skip_rows);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,static_cast<GLuint>(old_pack));
        REQUIRE(read_error==GL_NO_ERROR);
        REQUIRE(glGetError()==GL_NO_ERROR);
        std::uint64_t hash=14695981039346656037ULL;
        for(const float depth_value:depth) {
          const auto bits=std::bit_cast<std::uint32_t>(depth_value);
          for(unsigned shift=0;shift<32;shift+=8) {
            hash^=(bits>>shift)&0xffU;hash*=1099511628211ULL;
          }
        }
        depth_signature=hash;
        depth_nonclear_pixels=static_cast<std::size_t>(std::count_if(
            depth.begin(),depth.end(),[](const float value){return value<1.0F;}));
        const auto [minimum,maximum]=std::minmax_element(depth.begin(),depth.end());
        depth_range=std::array{*minimum,*maximum};
        if(compare_old_visibility&&!selected_visibility) {
          REQUIRE(baseline_world_color);
          auto selected_scene=scene;
          f::RuntimeWorldVisibility negative_selector;
          REQUIRE(negative_selector.apply(selected_scene,extent));
          world_only.static_world=selected_scene.static_world;
          renderer.render(world_only,extent);
          const auto selected_world_color=renderer.observe_framebuffer(
              extent,scene.clear_color,true);
          REQUIRE(selected_world_color);
          REQUIRE(selected_world_color.rgba8.size()==baseline_world_color.rgba8.size());
          std::vector<float> selected_depth(depth.size());
          glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);
          REQUIRE(read_fbo==0);
          glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
          glPixelStorei(GL_PACK_ALIGNMENT,1);
          glPixelStorei(GL_PACK_ROW_LENGTH,0);
          glPixelStorei(GL_PACK_SKIP_PIXELS,0);
          glPixelStorei(GL_PACK_SKIP_ROWS,0);
          glReadPixels(0,0,extent.width,extent.height,GL_DEPTH_COMPONENT,
              GL_FLOAT,selected_depth.data());
          REQUIRE(glGetError()==GL_NO_ERROR);
          glPixelStorei(GL_PACK_ALIGNMENT,old_alignment);
          glPixelStorei(GL_PACK_ROW_LENGTH,old_row_length);
          glPixelStorei(GL_PACK_SKIP_PIXELS,old_skip_pixels);
          glPixelStorei(GL_PACK_SKIP_ROWS,old_skip_rows);
          glBindBuffer(GL_PIXEL_PACK_BUFFER,static_cast<GLuint>(old_pack));
          REQUIRE(glGetError()==GL_NO_ERROR);
          std::size_t changed_color{},changed_depth{},same_depth_changed_color{};
          std::size_t missing_depth{},extra_depth{};
          int min_x=extent.width,min_y=extent.height,max_x=-1,max_y=-1;
          for(std::size_t pixel=0;pixel<depth.size();++pixel) {
            const auto rgba=pixel*4U;
            const bool color_changed=!std::equal(
                baseline_world_color.rgba8.begin()+rgba,
                baseline_world_color.rgba8.begin()+rgba+4U,
                selected_world_color.rgba8.begin()+rgba);
            const bool depth_changed=std::bit_cast<std::uint32_t>(depth[pixel])!=
                std::bit_cast<std::uint32_t>(selected_depth[pixel]);
            changed_color+=color_changed;changed_depth+=depth_changed;
            same_depth_changed_color+=color_changed&&!depth_changed;
            missing_depth+=depth[pixel]<1.0F&&selected_depth[pixel]>=1.0F;
            extra_depth+=depth[pixel]>=1.0F&&selected_depth[pixel]<1.0F;
            if(color_changed||depth_changed) {
              const int x=static_cast<int>(pixel%static_cast<std::size_t>(extent.width));
              const int y=static_cast<int>(pixel/static_cast<std::size_t>(extent.width));
              min_x=std::min(min_x,x);min_y=std::min(min_y,y);
              max_x=std::max(max_x,x);max_y=std::max(max_y,y);
            }
          }
          std::cout<<"[51-g1-negative-diff] {\"view\":"<<std::quoted(view.label)
            <<",\"remote_capture\":"<<(capture?"true":"false")
            <<",\"scope\":\"world_only_rgba8_and_default_depth_off\""
            <<",\"changed_color_pixels\":"<<changed_color
            <<",\"changed_depth_pixels\":"<<changed_depth
            <<",\"same_depth_changed_color_pixels\":"<<same_depth_changed_color
            <<",\"missing_depth_pixels\":"<<missing_depth
            <<",\"extra_depth_pixels\":"<<extra_depth
            <<",\"diff_bounds_bottom_origin\":["<<min_x<<','<<min_y<<','<<max_x<<','<<max_y<<"]}\n";
        }
      }
    }
    const auto depth_text=depth_signature?"\""+std::to_string(*depth_signature)+"\"":"null";
    const auto visibility=sidecar(map,scene.camera);
    const auto& v=visibility.statistics();const auto& target=renderer.scene_target_statistics();
    const auto& timing=renderer.gpu_timing_statistics();const auto& summary=assets.summary();
    color_input_receipt(target,renderer.image_adjustment_statistics());
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
      <<",\"pitch\":"<<view.pitch<<",\"vertical_fov_radians\":"<<scene.camera.vertical_field_of_view_radians
      <<",\"near_plane\":"<<scene.camera.near_plane<<",\"far_plane\":"<<scene.camera.far_plane
      <<",\"historical_replay\":false,\"synthetic_players\":8,\"synthetic_pickups\":12,\"effects\":0,\"decals\":0,\"lights\":"
      <<scene.effect_lights().size()+(scene.transient_world_light?1U:0U)
      <<",\"light_profile\":"<<std::quoted(dynamic_profile)<<",\"timing_enabled\":"<<(timed?"true":"false")
      <<",\"cold_render_cpu_ms\":"<<cold_render_ms<<",\"light_state_changes\":"<<light_state_changes
      <<",\"timing_omitted_samples\":"<<omitted_timing_samples
      <<",\"timing_invalid_samples\":"<<invalid_timing_samples
      <<",\"leaf\":"<<visibility.camera_leaf_index().value_or(0)<<",\"pvs_candidates\":"<<v.pvs_candidate_world_surface_count
      <<",\"frustum_visible\":"<<v.frustum_visible_world_surface_count<<",\"sidecar_visible_surfaces\":"<<v.visible_world_surface_count
      <<",\"actual_visible_list\":"<<(scene.static_world->visible_draw_list?"true":"false")
      <<",\"world_draws\":"<<world_draws<<",\"world_triangles\":"<<world_triangles<<",\"studio_draws\":"<<studio_draws
      <<",\"prepared_draw_blocks\":"<<map.world_package()->statistics().prepared_draw_block_count
      <<",\"prepared_draw_bytes\":"<<map.world_package()->statistics().prepared_draw_byte_count
      <<",\"prepared_selected_blocks\":"<<prepared_selected_blocks
      <<",\"prepared_rejected_blocks\":"<<prepared_rejected_blocks
      <<",\"prepared_batch_fallbacks\":"<<prepared_batch_fallbacks
      <<",\"prepared_probed_blocks\":"<<prepared_probed_blocks
      <<",\"prepared_all_batch_bypass_frames\":"<<prepared_all_batch_bypass_frames
      <<",\"sprite_draws\":"<<sprite_draws<<",\"pose_updates\":"<<pose_updates<<",\"base_binds\":"<<base_binds
      <<",\"lightmap_binds\":"<<lightmap_binds<<",\"model_binds\":"<<model_binds<<",\"frames\":"<<samples
      <<",\"world_uploads\":"<<world_uploads<<",\"studio_uploads\":"<<studio_uploads<<",\"sprite_uploads\":"<<sprite_uploads
      <<",\"program_binds\":"<<program_binds<<",\"vao_binds\":"<<vao_binds<<",\"sky_binds\":"<<sky_binds
      <<",\"transform_uploads\":"<<transform_uploads<<",\"camera_uploads\":"<<camera_uploads<<",\"light_uploads\":"<<light_uploads
      <<",\"material_uniform_uploads\":";
    if(material_uniform_delta(renderer.statistics(),renderer.statistics()))
      std::cout<<material_uniform_uploads;
    else std::cout<<"null";
    std::cout
      <<",\"final_visible_studio_triangles\":"<<studio_triangles<<",\"final_visible_studio_vertices\":"<<studio_vertices
      <<",\"final_visible_studio_bones\":"<<studio_bones
      <<",\"ordinary_pose_evaluations\":"<<summary.ordinary_pose_evaluations<<",\"ordinary_pose_reuses\":"<<summary.ordinary_pose_reuses
      <<",\"base_model_mesh_value_bytes\":"<<mesh_bytes<<",\"appearance_owners\":"<<world.entity_scene()->presentation_studio_sources().size()
      <<",\"color_only_players\":8,\"expected_appearance_owners\":"<<expected_appearance_owners
      <<",\"palette_path\":\""<<(expected_appearance_owners?"remapped_variants":"no_remappable_palette")<<'"'
      <<",\"pixel_signature\":\""<<pixels.color_signature<<"\",\"depth_signature\":"<<depth_text
      <<",\"depth_signature_scope\":\""<<(depth_signature?"world_only_default_depth_off":"unavailable_msaa_or_default_depth")<<"\""
      <<",\"depth_nonclear_pixels\":";
    if(depth_nonclear_pixels)std::cout<<*depth_nonclear_pixels;else std::cout<<"null";
    std::cout<<",\"depth_range\":";
    if(depth_range)std::cout<<'['<<(*depth_range)[0]<<','<<(*depth_range)[1]<<']';else std::cout<<"null";
    std::cout
      <<",\"msaa\":"<<target.actual_samples<<",\"target_allocations\":"<<target.storage_allocations
      <<",\"resolved_frames\":"<<target.resolved_frames
      <<",\"output_color_blits\":"<<target.output_color_blits
      <<",\"direct_adjustment_frames\":"<<target.direct_adjustment_frames
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
    constexpr std::array world_names{"gpu_world_target_setup_clear","gpu_world_sky",
        "gpu_world_static_world","gpu_world_runtime_brushes"};
    for(std::size_t i=0;i<world_gpu.size();++i)
      quantile(view.label,capture,world_names[i],std::move(world_gpu[i]));
    if(emit_raw)for(std::size_t sample=0;sample<raw_samples.size();++sample) {
      const auto& raw=raw_samples[sample];
      const auto print_optional=[](const std::optional<double>& value) {
        if(value)std::cout<<*value;else std::cout<<"null";
      };
      std::cout<<"[22ad-world-raw] {\"view\":"<<std::quoted(view.label)
        <<",\"remote_capture\":"<<(capture?"true":"false")
        <<",\"requested_msaa\":"<<requested_msaa
        <<",\"light_profile\":"<<std::quoted(dynamic_profile)
        <<",\"light_input_signature\":\""<<raw.light_input_signature<<'"'
        <<",\"source_frame\":"<<first_sample_frame+sample
        <<",\"cpu_ms\":[";
      for(std::size_t stage=0;stage<raw.cpu.size();++stage) {
        if(stage)std::cout<<',';std::cout<<raw.cpu[stage];
      }
      std::cout<<"],\"gpu_total_ms\":";print_optional(raw.gpu_total);
      std::cout<<",\"gpu_pass_ms\":[";
      for(std::size_t pass=0;pass<raw.gpu_pass.size();++pass) {
        if(pass)std::cout<<',';print_optional(raw.gpu_pass[pass]);
      }
      std::cout<<"],\"world_subpass_ms\":[";
      for(std::size_t pass=0;pass<raw.world_subpass.size();++pass) {
        if(pass)std::cout<<',';print_optional(raw.world_subpass[pass]);
      }
      std::cout<<"],\"valid_pass_mask\":"<<unsigned(raw.pass_mask)
        <<",\"valid_world_subpass_mask\":"<<unsigned(raw.world_subpass_mask)<<"}\n";
    }
  }
  }
  REQUIRE((selected_view.empty()||matched_views==1U));
  for(std::size_t i=0;i<names.size();++i)CHECK(std::filesystem::last_write_time(root/"valve"/names[i])==modified[i]);
  host.teardown();gl->release_renderer();
}
#endif

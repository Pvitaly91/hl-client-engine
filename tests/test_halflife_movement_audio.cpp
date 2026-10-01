#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/server_audio.hpp>
#include <hlclient/goldsrc/surface_texture_query.hpp>
#include <hlclient/audio/output.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include "world_render_test_fixture.hpp"
#include "literal_movement_bsp_fixture.hpp"
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/goldsrc/reference_prediction_seed.hpp>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include "local_resource_readiness_test_fixture.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <memory>
#include <string_view>
#include <vector>

namespace {
namespace api=hlclient::game_api;
namespace sound=hlclient::goldsrc;
namespace audio=hlclient::audio;

api::MovementAudioObservation step(unsigned sequence, float horizontal=250.0F,
    std::string_view texture="CONCRETE") {
  api::MovementAudioObservation o;
  o.generation=1; o.life_epoch=1; o.command_sequence=sequence;
  o.command_milliseconds=20; o.scheduled_seconds=sequence*.02;
  o.before_mode=o.after_mode=api::MovementAudioMode::ground;
  o.before_grounded=o.after_grounded=true;
  o.horizontal_speed=o.total_speed=horizontal;
  o.dry_context=true; o.movevars_footsteps=true;
  o.surface_status=api::MovementSurfaceStatus::found;
  o.texture_name=texture;
  return o;
}
struct Host {
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  Host() {
    host.reset({1,1,{1}});
    host.set_movement_audio_time_origin(0);
    host.configure_movement_materials(
        "C CONCRETE\nM METAL\nD DIRT\nG GRATE\nT TILE\nV VENT\n");
  }
  api::LocalAudioBatch observe(const api::MovementAudioObservation& o,
      bool replay=false) {
    host.observe_movement_audio(o,replay);
    return host.drain_audio();
  }
};
struct Loader final : sound::SoundAssets {
  sound::SoundAssetStatus status{sound::SoundAssetStatus::ready};
  std::shared_ptr<hlclient::assets::AudioAsset> pcm=std::make_shared<hlclient::assets::AudioAsset>();
  Loader() {
    pcm->sample_rate=48000; pcm->channel_count=1;
    pcm->interleaved_samples.assign(4800,.125F);
  }
  sound::SoundAssetResult request(std::uint16_t) noexcept override {
    return {sound::SoundAssetStatus::ready,pcm};
  }
  sound::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {
    return {status,pcm};
  }
};
struct Sink final : audio::Output {
  audio::OfflineOutput offline;
  std::vector<audio::Command> starts;
  bool submit(const audio::Command& command) noexcept override {
    if (command.operation==audio::Operation::start) starts.push_back(command);
    return offline.submit(command);
  }
  void set_listener(audio::Listener listener) noexcept override {
    offline.set_listener(listener);
  }
};

hlclient::world_spatial::WorldSpatialPackage spatial_fixture(float max_x=16) {
  namespace spatial=hlclient::world_spatial;
  const hlclient::assets::WorldBounds bounds{{0,0,-1},{max_x,16,32}};
  spatial::WorldSpatialNode node;
  node.plane_index=0;
  node.children={spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,1},
      spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,1}};
  node.bounds=bounds;
  spatial::WorldSpatialLeaf solid;
  solid.source_leaf_index=0; solid.bounds=bounds; solid.solid_or_special=true;
  solid.surface_membership.source_leaf_index=0;
  spatial::WorldSpatialLeaf visible;
  visible.source_leaf_index=1; visible.bounds=bounds; visible.pvs_row_index=0;
  visible.pvs_bit_addressable=true;
  visible.surface_membership.source_leaf_index=1;
  visible.surface_membership.source_marksurface_count=1;
  visible.surface_membership.world_surface_indices={0};
  return {{spatial::WorldSpatialPlane{{1,0,0},0,0}}, {node}, {solid,visible},
      spatial::WorldPvsTable{1,1,{{std::byte{1}}},{std::nullopt,0},0},
      spatial::WorldSpatialModelMetadata{0,1,bounds},
      spatial::WorldSpatialStatistics{1,1,2,1,1,1,1},
      spatial::WorldSpatialCompatibilityProfile::goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
      spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records};
}
hlclient::movement::LocalPlayerMovementState ground_state(float x,
    hlclient::movement::PlayerMovementHitIdentity hit) {
  hlclient::movement::LocalPlayerMovementStateCreateInfo info;
  info.origin={x,8,36}; info.mode=hlclient::movement::PlayerMovementMode::walking;
  info.ground.grounded=info.ground.walkable=true;
  info.ground.hit=hit; info.ground.plane.normal={0,0,1};
  info.ground.contact_position=info.origin;
  auto created=hlclient::movement::LocalPlayerMovementState::create(info);
  REQUIRE(created);
  return *created.state;
}
// Independently authored point hull, not reconstructed from player clip planes.
std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
point_floors(float slope=0,float right_height=0) {
  namespace c=hlclient::collision;
  const auto length=std::sqrt(1+slope*slope);
  const hlclient::assets::AssetVector3 normal{-slope/length,0,1/length};
  const c::CollisionContents empty{{-1},c::CollisionContentsCategory::empty};
  const c::CollisionContents solid{{-2},c::CollisionContentsCategory::solid};
  c::CollisionModel model; model.source_model_index=0;
  for(std::size_t i=0;i<c::kCollisionHullCount;++i) {
    const auto ordinal=*c::collision_hull_ordinal(i);
    model.hulls[i]={ordinal,c::CollisionHullTreeDomain::node_leaf,
        {c::CollisionHullRootKind::terminal,0,empty},*c::standard_collision_hull_profile(ordinal)};
  }
  model.hulls[0].root={c::CollisionHullRootKind::node,0,{}};
  std::vector<c::CollisionNode> nodes(3);
  nodes[0]={0,{{{c::CollisionNodeChildKind::node,2},{c::CollisionNodeChildKind::node,1}}}};
  nodes[1]={1,{{{c::CollisionNodeChildKind::leaf,0},{c::CollisionNodeChildKind::leaf,1}}}};
  nodes[2]={2,{{{c::CollisionNodeChildKind::leaf,0},{c::CollisionNodeChildKind::leaf,1}}}};
  return std::make_shared<const c::CollisionWorldPackage>(
      std::vector<c::CollisionPlane>{{{1,0,0},16,0,0},{normal,0,1,3},
          {normal,right_height/length,2,3}},std::move(nodes),
      std::vector<c::CollisionLeaf>{{0,empty},{1,solid}},
      std::vector<c::CollisionClipnode>{},std::vector<c::CollisionModel>{model});
}
}

TEST_CASE("E3 HL1 dry material families use command cadence through the game host",
    "[movement-audio][game-module]") {
  for (const auto [texture,prefix]:{
      std::pair{"CONCRETE","player/pl_step"},
      {"METAL","player/pl_metal"},
      {"DIRT","player/pl_dirt"},
      {"GRATE","player/pl_grate"},
      {"TILE","player/pl_tile"},
      {"VENT","player/pl_duct"}}) {
    Host h;
    const auto first=h.observe(step(1,250,texture));
    REQUIRE(first.count==1);
    CHECK(first.cues[0].kind==api::LocalSoundKind::footstep);
    CHECK(first.cues[0].reference.name().starts_with(prefix));
    CHECK(first.cues[0].reference.source==
        api::SoundReferenceSource::pinned_halflife_client_sound_profile);
    const float expected=std::string_view{texture}=="DIRT" ? .55F :
        std::string_view{texture}=="VENT" ? .7F : .5F;
    CHECK(first.cues[0].volume==Catch::Approx(expected).margin(.001F));
    CHECK(first.cues[0].scheduled_seconds==Catch::Approx(.02));
    for (unsigned n=2;n<16;++n) CHECK(h.observe(step(n,250,texture)).count==0);
    const auto second=h.observe(step(16,250,texture));
    REQUIRE(second.count==1);
    CHECK(second.cues[0].reference.name().starts_with(prefix));
    CHECK(second.cues[0].command_sequence==16);
    CHECK(second.cues[0].serial!=first.cues[0].serial);
  }
}

TEST_CASE("E3 material changes preserve phase and normalized texture lookup",
    "[movement-audio]") {
  Host h;
  CHECK(h.observe(step(1,250,"+0CONCRETE")).count==1);
  for(unsigned n=2;n<16;++n) CHECK(h.observe(step(n,250,"METAL")).count==0);
  const auto metal=h.observe(step(16,250,"METAL"));
  REQUIRE(metal.count==1);
  CHECK(metal.cues[0].reference.name().starts_with("player/pl_metal"));
  Host fallback;
  const auto unknown=fallback.observe(step(1,250,"UNKNOWN"));
  REQUIRE(unknown.count==1);
  CHECK(unknown.cues[0].reference.name().starts_with("player/pl_step"));
  auto invalid=step(2); invalid.surface_status=api::MovementSurfaceStatus::invalid_support;
  CHECK(fallback.observe(invalid).count==0);
}

TEST_CASE("E3 quiet movement still advances local timer without camera or lift cues",
    "[movement-audio]") {
  Host h;
  auto slow=step(1,100); CHECK(h.observe(slow).count==0);
  auto muted=step(2,250); muted.movevars_footsteps.reset();
  CHECK(h.observe(muted).count==0);
  for(unsigned n=3;n<=20;++n) {
    auto stopped=step(n,0);
    CHECK(h.observe(stopped).count==0);
  }
  // The first low-speed decision used the 400 ms walking interval; neither
  // a held key nor a moving support advances another audible occurrence.
  CHECK(h.observe(step(21,250)).count==1);
  Host wall;
  for(unsigned n=1;n<=30;++n) CHECK(wall.observe(step(n,0)).count==0);
  auto crouched=step(31,240); crouched.ducked=true;
  const auto duck=wall.observe(crouched);
  REQUIRE(duck.count==1);
  CHECK(duck.cues[0].volume==Catch::Approx(.175F).margin(.001F));
}

TEST_CASE("E3 ladder movement and landing are life-local and replay-silent",
    "[movement-audio]") {
  Host h;
  auto ladder=step(1,0); ladder.before_mode=ladder.after_mode=api::MovementAudioMode::ladder;
  ladder.before_grounded=ladder.after_grounded=false; ladder.total_speed=100;
  ladder.surface_status=api::MovementSurfaceStatus::geometry_unavailable;
  ladder.texture_name={};
  const auto upward=h.observe(ladder);
  REQUIRE(upward.count==1);
  CHECK(upward.cues[0].kind==api::LocalSoundKind::ladder);
  CHECK(upward.cues[0].reference.name().starts_with("player/pl_ladder"));
  for(unsigned n=2;n<19;++n) {
    ladder.command_sequence=n; ladder.scheduled_seconds=n*.02;
    ladder.total_speed=0;
    CHECK(h.observe(ladder).count==0);
  }
  ladder.command_sequence=19; ladder.scheduled_seconds=.38; ladder.total_speed=100;
  CHECK(h.observe(ladder).count==1); // descending uses the same HL1 ladder family
  auto airborne=step(20,0);
  airborne.before_mode=airborne.after_mode=api::MovementAudioMode::airborne;
  airborne.before_grounded=airborne.after_grounded=false;
  CHECK(h.observe(airborne).count==0);
  auto contact=step(21,250);
  contact.before_mode=api::MovementAudioMode::airborne;
  contact.before_grounded=false; contact.before_vertical_velocity=-400;
  const auto landing=h.observe(contact);
  REQUIRE(landing.count>=1);
  CHECK(std::any_of(landing.cues.begin(),landing.cues.begin()+landing.count,
      [](const auto& cue){return cue.kind==api::LocalSoundKind::landing &&
          cue.volume==Catch::Approx(.85F).margin(.001F); }));
  CHECK(h.observe(step(22,250)).count==0);
  h.host.rewind_movement_audio(20);
  CHECK(h.observe(contact,true).count==0);
  CHECK(h.observe(step(22,250),true).count==0);
  CHECK(h.observe(step(23,250)).count==0);
  auto hard=step(24,250); hard.before_mode=api::MovementAudioMode::airborne;
  hard.before_grounded=false; hard.before_vertical_velocity=-600;
  const auto hard_batch=h.observe(hard);
  CHECK(std::any_of(hard_batch.cues.begin(),hard_batch.cues.begin()+hard_batch.count,
      [](const auto& cue){return cue.kind==api::LocalSoundKind::fall_pain;}));
  h.host.reset({1,1,{1}});
  h.host.set_movement_audio_time_origin(0);
  h.host.configure_movement_materials("C CONCRETE\n");
  CHECK(h.observe(step(1,0)).count==0);
}

TEST_CASE("E3 local body PCM and E1 server audio use independent voices",
    "[movement-audio][audio]") {
  Host h; Loader loader; Sink sink;
  sink.set_listener({{}, {0,-1,0},1,false});
  sound::LocalAudio local(sink); sound::ServerAudio server(sink);
  const auto batch=h.observe(step(1));
  REQUIRE(batch.count==1);
  local.update(batch,&loader,.02,true);
  CHECK(local.statistics().movement_submitted==1);
  sound::CommittedSound event;
  event.generation=1; event.record=1; event.ordinal=1; event.cursor=1;
  event.opcode=sound::RuntimeControlOpcode::svc_sound;
  event.sound.sound_reference=3; event.sound.channel=3;
  event.sound.entity_reference=2; event.sound.volume=255;
  server.consume(event,{}); server.update(&loader,{});
  REQUIRE(sink.starts.size()==2);
  CHECK(sink.starts[0].voice!=sink.starts[1].voice);
  std::array<float,64> pcm{}; sink.offline.mixer.render(pcm);
  CHECK(pcm[0]>0);
  local.update(batch,&loader,.02,true);
  CHECK(sink.starts.size()==2);
  CHECK(local.statistics().duplicates==1);
  Host focused; Sink muted_sink; sound::LocalAudio muted(muted_sink);
  const auto missed=focused.observe(step(1));
  muted.update(missed,&loader,.02,false);
  muted.update(missed,&loader,.04,true);
  CHECK(muted_sink.starts.empty());
  CHECK(muted.statistics().movement_muted==1);
}

TEST_CASE("E3 runtime generation reset retains the live audio clock origin",
    "[movement-audio][game-module]") {
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.set_movement_audio_time_origin(100.0);
  // RuntimeReplaySession creates/refreshes a generation after the application
  // has established the clock. Clearing it here silently drops all steps.
  host.reset({1,1,{1}});
  host.configure_movement_materials("C CONCRETE\n");
  auto observed=step(1);
  observed.scheduled_seconds=100.02;
  host.observe_movement_audio(observed,false);
  const auto first=host.drain_audio();
  REQUIRE(first.count==1);
  CHECK(first.cues[0].scheduled_seconds==Catch::Approx(.02));
  CHECK(first.statistics.movement_observations==1);
  CHECK(first.statistics.movement_clock_dropped==0);
  host.reset({2,2,{1}});
  observed.generation=2; observed.life_epoch=2;
  observed.scheduled_seconds=100.04;
  host.observe_movement_audio(observed,false);
  const auto second=host.drain_audio();
  REQUIRE(second.count==1);
  CHECK(second.cues[0].scheduled_seconds==Catch::Approx(.04));
  CHECK(second.statistics.movement_observations==2);
  CHECK(second.statistics.movement_clock_dropped==0);
  host.teardown();
  CHECK(host.drain_audio().count==0);
  api::GameClientHost unarmed{hlclient::games::halflife::make_half_life_client_module()};
  unarmed.reset({1,1,{1}});
  unarmed.observe_movement_audio(step(1),false);
  const auto dropped=unarmed.drain_audio();
  CHECK(dropped.count==0);
  CHECK(dropped.statistics.movement_clock_dropped==1);
}

TEST_CASE("E3 sample warm-up restarts when approved assets arrive after early drains",
    "[movement-audio][audio]") {
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,{1}});
  const auto early=host.drain_audio();
  REQUIRE(early.prepare_count==6);
  CHECK(early.prepare[0].name()=="player/pl_step1.wav");
  host.configure_movement_materials("C CONCRETE\n");
  const auto ready=host.drain_audio();
  REQUIRE(ready.prepare_count==6);
  CHECK(ready.prepare[0].name()=="player/pl_step1.wav");
}

TEST_CASE("E3 presentation texture trace is constrained to supporting world or brush model",
    "[movement-audio][surface-query]") {
  namespace fixture=hlclient::tests::world_render_fixture;
  namespace render=hlclient::world_render;
  namespace scene=hlclient::world_scene_render;
  namespace collision=hlclient::goldsrc::collision;
  auto world_build=fixture::make_package(); REQUIRE(world_build);
  auto world=std::make_shared<const render::WorldRenderPackage>(std::move(*world_build.package));
  auto brush_asset=fixture::make_textured_world();
  brush_asset.world.materials[0].texture_name="METAL";
  auto brush_build=render::WorldRenderPackageBuilder{}.build(
      std::move(brush_asset),fixture::make_lightmap_set({}));
  REQUIRE(brush_build);
  auto brush=std::make_shared<const render::WorldRenderPackage>(std::move(*brush_build.package));
  std::vector<scene::BrushSubmodelRenderModel> models;
  models.emplace_back(1,brush->bounds(),std::vector<std::uint32_t>{0});
  scene::BrushSubmodelRenderLibrary library{brush,std::move(models)};
  auto built=scene::WorldSceneRenderPackageBuilder{}.build(world,spatial_fixture(),std::move(library));
  REQUIRE(built);
  auto package=std::make_shared<const scene::WorldSceneRenderPackage>(std::move(*built.package));
  hlclient::goldsrc::SurfaceTextureQuery query(package,point_floors());
  auto supported_world=ground_state(8,{hlclient::movement::PlayerMovementHitKind::world,0,{},{}});
  auto from_world=query.at_support(supported_world,nullptr);
  CHECK(from_world.status==hlclient::goldsrc::SurfaceTextureResult::Status::found);
  CHECK(from_world.texture_name=="STONE");
  auto transform=hlclient::goldsrc::brush_models::make_brush_rigid_transform({32,0,0},{});
  REQUIRE(transform);
  collision::BrushCollisionSceneInstance instance;
  instance.identity={7,1,42}; instance.transform=*transform.transform;
  instance.role=collision::BrushCollisionRole::solid;
  collision::BrushCollisionScene brush_scene({}, {instance},
      collision::BrushCollisionRoleProviderProfile::explicit_synthetic_brush_solidity_v1);
  auto supported_brush=ground_state(40,{hlclient::movement::PlayerMovementHitKind::brush_entity,1,7,42});
  const auto from_brush=query.at_support(supported_brush,&brush_scene);
  CHECK(from_brush.status==hlclient::goldsrc::SurfaceTextureResult::Status::found);
  CHECK(from_brush.texture_name=="METAL");
  auto wrong_identity=ground_state(40,{hlclient::movement::PlayerMovementHitKind::brush_entity,1,8,42});
  CHECK(query.at_support(wrong_identity,&brush_scene).status==
      hlclient::goldsrc::SurfaceTextureResult::Status::invalid_support);
}

namespace {
constexpr std::string_view e8_table=
    "C CONCRETE\nM METAL\nD DIRT\nV VENT\nG GRATE\nT TILE\nS SLOSH\n"
    "W WOOD\nP COMPUTER\nY GLASS\nF FLESH\nN SNOW\n";
api::MovementAudioObservation spatial_step(unsigned command,float speed=320,
    std::string_view texture="CONCRETE") {
  auto o=step(command,speed,texture);
  o.world_origin=hlclient::assets::AssetVector3{8,8,0};
  o.source_surface_index=41; o.source_material_index=0;
  o.source_texture_index=0; o.source_model_index=0;
  return o;
}
std::shared_ptr<const hlclient::world_scene_render::WorldSceneRenderPackage>
two_floors(float slope=0,float right_height=0) {
  namespace fixture=hlclient::tests::world_render_fixture;
  namespace assets=hlclient::assets;
  auto world=fixture::make_world(2);
  world.bounds.maximum.x=32;
  for(std::size_t i=4;i<8;++i) world.vertices[i].position.x-=16;
  for(std::size_t i=0;i<8;++i) world.vertices[i].position.z=
      slope*world.vertices[i].position.x+(i>=4 ? right_height:0);
  world.bounds.maximum.z=32*slope+right_height;
  world.surfaces[0].bounds.maximum.z=16*slope;
  world.surfaces[1].bounds={{16,0,16*slope+right_height},{32,16,32*slope+right_height}};
  world.surfaces[0].source_surface_ordinal=41;
  world.surfaces[1].source_surface_ordinal=87;
  world.materials[0].texture_name="CONCRETE";
  auto material=world.materials[0]; material.texture_name="METAL"; material.source_texture_index=1;
  world.materials.push_back(material); world.surfaces[1].material_index=1;
  world.statistics.material_count=2;
  std::vector<assets::WorldTextureAsset> textures{fixture::make_texture(false),fixture::make_texture(false)};
  textures[0].name="CONCRETE"; textures[1].name="METAL"; textures[1].source_bsp_texture_index=1;
  std::vector<assets::WorldMaterialTextureBinding> bindings(2);
  for(std::size_t i=0;i<2;++i) {
    bindings[i].material_index=i; bindings[i].texture_asset_index=i;
    bindings[i].source_bsp_texture_index=static_cast<std::uint32_t>(i);
    bindings[i].status=assets::WorldMaterialTextureBindingStatus::resolved_embedded;
  }
  auto texture_set=assets::WorldTextureSet::create(std::move(textures),std::move(bindings),{},2);
  REQUIRE(texture_set);
  std::vector<assets::WorldSurfaceLightmapBinding> lightmaps(2);
  for(std::size_t i=0;i<2;++i) {
    lightmaps[i].surface_index=i;
    lightmaps[i].status=assets::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap;
    lightmaps[i].sample_width=lightmaps[i].sample_height=2;
  }
  auto lightmap_set=assets::WorldLightmapSet::create({},std::move(lightmaps),2); REQUIRE(lightmap_set);
  auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
      {std::move(world),std::move(*texture_set.texture_set)},std::move(*lightmap_set.lightmap_set));
  REQUIRE(built);
  auto package=std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*built.package));
  auto scene=hlclient::world_scene_render::WorldSceneRenderPackageBuilder{}.build(package,spatial_fixture(32));
  REQUIRE(scene);
  return std::make_shared<const hlclient::world_scene_render::WorldSceneRenderPackage>(std::move(*scene.package));
}
hlclient::movement::LocalPlayerMovementState floor_support(float x,float slope=0,float height=0) {
  namespace m=hlclient::movement;
  m::LocalPlayerMovementStateCreateInfo info;
  info.origin={x,8,slope*x+std::abs(slope)*16+36+height};
  info.mode=m::PlayerMovementMode::walking;
  info.ground.grounded=info.ground.walkable=true;
  info.ground.hit=m::PlayerMovementHitIdentity{m::PlayerMovementHitKind::world,0,{},{}};
  const auto length=std::sqrt(1+slope*slope);
  info.ground.plane={{-slope/length,0,1/length},(36+16*std::abs(slope)+height)/length,999};
  info.ground.contact_position=info.origin;
  auto created=m::LocalPlayerMovementState::create(info); REQUIRE(created);
  return *created.state;
}
}

TEST_CASE("E8 one owning E7 table selects seven steps and explicit impact-only fallbacks",
    "[e8][movement-audio][game-module]") {
  for(const auto [texture,category,prefix]:{
      std::tuple{"CONCRETE","concrete","player/pl_step"}, {"METAL","metal","player/pl_metal"},
      {"DIRT","dirt","player/pl_dirt"}, {"VENT","vent","player/pl_duct"},
      {"GRATE","grate","player/pl_grate"}, {"TILE","tile","player/pl_tile"},
      {"SLOSH","slosh","player/pl_slosh"}, {"WOOD","concrete","player/pl_step"},
      {"GLASS","concrete","player/pl_step"}, {"COMPUTER","concrete","player/pl_step"},
      {"FLESH","concrete","player/pl_step"}, {"SNOW","concrete","player/pl_step"},
      {"UNKNOWN","concrete","player/pl_step"}}) {
    CAPTURE(texture); Host h; std::string owning_input{e8_table};
    h.host.configure_movement_materials(owning_input); owning_input.assign("overwritten");
    const auto b=h.observe(spatial_step(1,320,texture)); REQUIRE(b.count==1);
    CHECK(b.cues[0].reference.name().starts_with(prefix)); REQUIRE(b.movement_diagnostic);
    const auto& d=*b.movement_diagnostic;
    CHECK(std::string_view{d.step_category.data()}==category);
    CHECK(d.surface_index==41); CHECK(d.texture_index==0); CHECK(d.ordinal==1);
    CHECK(d.left); CHECK(d.cadence_ms==300); CHECK(std::string_view{d.decision.data()}=="selected");
    CHECK(b.cues[0].world_origin->x==8); CHECK(b.cues[0].attenuation==Catch::Approx(.8));
    CHECK(std::string_view{d.classification.data()}==
        (std::string_view{texture}=="UNKNOWN" ? "unknown_concrete_fallback":"table_match"));
  }
  Host normalized; normalized.host.configure_movement_materials(e8_table);
  const auto b=normalized.observe(spatial_step(1,320,"+0{mEtAl")); REQUIRE(b.count==1);
  CHECK(b.cues[0].reference.name().starts_with("player/pl_metal"));
  CHECK(std::string_view{b.movement_diagnostic->texture_key.data()}=="METAL");
}

TEST_CASE("E8 walk run duck and confirmed multiplayer quiet policy retain command cadence",
    "[e8][movement-audio]") {
  for(const auto [speed,duck,cadence,gain,audible]:{
      std::tuple{150.F,false,400U,.2F,false}, {320.F,false,300U,.5F,true},
      {70.F,true,500U,.07F,false}, {240.F,true,400U,.175F,true}}) {
    Host h; auto o=spatial_step(1,speed); o.ducked=duck;
    const auto first=h.observe(o); REQUIRE(first.movement_diagnostic);
    CHECK(first.count==(audible ? 1U:0U));
    CHECK(first.movement_diagnostic->cadence_ms==cadence);
    CHECK(first.movement_diagnostic->volume==Catch::Approx(gain));
    for(unsigned i=2;i<=cadence/20;++i) {
      o.command_sequence=i; o.scheduled_seconds=i*.02;
      const auto b=h.observe(o); CHECK(b.count==0); CHECK(b.movement_diagnostic->ordinal==1);
    }
    o.command_sequence=1+cadence/20; o.scheduled_seconds=o.command_sequence*.02;
    const auto second=h.observe(o); CHECK(second.count==(audible ? 1U:0U));
    CHECK(second.movement_diagnostic->ordinal==2); CHECK_FALSE(second.movement_diagnostic->left);
  }
  for(const std::optional<bool> flag:std::array<std::optional<bool>,3>{{{},false,true}}) {
    Host h; auto o=spatial_step(1); o.movevars_footsteps=flag;
    const auto b=h.observe(o); CHECK(b.count==(flag==true ? 1U:0U));
    CHECK(std::string_view{b.movement_diagnostic->decision.data()}==
        (!flag ? "movevars_missing":!*flag ? "movevars_disabled":"selected"));
  }
}

TEST_CASE("E8 stopped wall turn jump wet and airborne contexts cannot invent footsteps",
    "[e8][movement-audio]") {
  Host h;
  for(unsigned i=1;i<40;++i) CHECK(h.observe(spatial_step(i,0)).count==0);
  auto jump=spatial_step(40); jump.jump_edge=true; CHECK(h.observe(jump).count==0);
  auto air=spatial_step(41); air.before_grounded=air.after_grounded=false;
  air.before_mode=air.after_mode=api::MovementAudioMode::airborne;
  for(unsigned i=41;i<55;++i) {air.command_sequence=i; air.scheduled_seconds=i*.02; CHECK(h.observe(air).count==0);}
  auto wet=spatial_step(55); wet.dry_context=false; CHECK(h.observe(wet).count==0);
  auto unavailable=spatial_step(56); unavailable.surface_status=api::MovementSurfaceStatus::source_texture_unavailable;
  CHECK(h.observe(unavailable).count==0);
  CHECK(h.observe(spatial_step(57)).count==1);
  for(unsigned i=58;i<72;++i) CHECK(h.observe(spatial_step(i,0)).count==0);
  CHECK(h.observe(spatial_step(72)).count==1); // Stop/restart is not held-key cadence.
}

TEST_CASE("E8 movement diagnostics own mode and log suppression transitions without tick spam",
    "[e8][movement-audio]") {
  Host h;
  const auto stopped=h.observe(spatial_step(1,0)); REQUIRE(stopped.movement_diagnostic);
  CHECK(stopped.count==0); const auto& first=*stopped.movement_diagnostic;
  CHECK(first.grounded); CHECK(std::string_view{first.movement_mode.data()}=="ground");
  CHECK(std::string_view{first.speed_band.data()}=="stationary");
  CHECK(std::string_view{first.decision.data()}=="stationary");
  CHECK(h.observe(spatial_step(2,0)).movement_diagnostic->serial==first.serial);
  auto air=spatial_step(3); air.after_mode=api::MovementAudioMode::airborne;
  air.after_grounded=false;
  const auto airborne=h.observe(air); REQUIRE(airborne.movement_diagnostic);
  CHECK_FALSE(airborne.movement_diagnostic->grounded);
  CHECK(std::string_view{airborne.movement_diagnostic->decision.data()}=="airborne");
  air.command_sequence=4;
  CHECK(h.observe(air).movement_diagnostic->serial==airborne.movement_diagnostic->serial);
  const auto resumed=h.observe(spatial_step(5)); REQUIRE(resumed.count==1);
  CHECK(std::string_view{resumed.movement_diagnostic->speed_band.data()}=="run");
  const auto duplicate=h.observe(spatial_step(5)); CHECK(duplicate.count==0);
  REQUIRE(duplicate.movement_diagnostic);
  CHECK(std::string_view{duplicate.movement_diagnostic->decision.data()}=="replay_duplicate");
  CHECK(h.observe(spatial_step(5)).movement_diagnostic->serial==duplicate.movement_diagnostic->serial);
}

TEST_CASE("E8 actual landing owns contact command and delays the next regular step",
    "[e8][movement-audio]") {
  for(float fall:{-100.F,-400.F,-600.F}) {
    Host h; auto contact=spatial_step(1);
    contact.before_grounded=false; contact.before_mode=api::MovementAudioMode::airborne;
    contact.before_vertical_velocity=fall;
    const auto b=h.observe(contact);
    CHECK(b.count==(fall== -100.F ? 0U:fall== -400.F ? 1U:2U));
    CHECK(std::none_of(b.cues.begin(),b.cues.begin()+b.count,[](const auto& c){return c.kind==api::LocalSoundKind::footstep;}));
    for(unsigned i=2;i<16;++i) CHECK(h.observe(spatial_step(i)).count==0);
    const auto next=h.observe(spatial_step(16)); REQUIRE(next.count==1);
    CHECK(next.cues[0].kind==api::LocalSoundKind::footstep);
  }
}

TEST_CASE("E8 replay rewinds only timer never heard side ordinal or duplicate voices",
    "[e8][movement-audio][prediction]") {
  Host h; api::LocalAudioBatch last;
  for(unsigned i=1;i<=61;++i) last=h.observe(spatial_step(i));
  REQUIRE(last.movement_diagnostic); CHECK(last.movement_diagnostic->ordinal==5);
  h.host.rewind_movement_audio(0);
  for(unsigned i=1;i<=61;++i) {
    const auto b=h.observe(spatial_step(i),true); CHECK(b.count==0);
    if(b.movement_diagnostic) CHECK(b.movement_diagnostic->ordinal==5);
  }
  CHECK(h.observe(spatial_step(61)).count==0);
  for(unsigned i=62;i<76;++i) CHECK(h.observe(spatial_step(i)).count==0);
  const auto next=h.observe(spatial_step(76)); REQUIRE(next.count==1);
  CHECK(next.movement_diagnostic->ordinal==6); CHECK_FALSE(next.movement_diagnostic->left);
  for(unsigned i=77;i<220;++i) (void)h.observe(spatial_step(i));
  h.host.rewind_movement_audio(1); // exhausted 128-command history
  for(unsigned i=2;i<220;++i) CHECK(h.observe(spatial_step(i),true).count==0);
  CHECK(h.host.drain_audio().statistics.movement_history_gaps>=1);
}

TEST_CASE("E8 rendering slices and drain frequency leave deterministic side and tile variants",
    "[e8][movement-audio]") {
  using Identity=std::tuple<unsigned,unsigned,std::string>;
  std::vector<Identity> baseline;
  for(unsigned drain_every:{1U,2U,5U,12U}) {
    Host h; h.host.configure_movement_materials(e8_table);
    std::vector<Identity> observed; bool tile5=false;
    for(unsigned command=1;command<=1501;++command) {
      h.host.observe_movement_audio(spatial_step(command,320,"TILE"),false);
      if(command%drain_every && command!=1501) continue;
      const auto b=h.host.drain_audio();
      for(std::size_t i=0;i<b.count;++i) {
        const auto& c=b.cues[i]; observed.emplace_back(c.command_sequence,c.marker_ordinal,c.reference.name());
        tile5|=c.reference.name()=="player/pl_tile5.wav";
        const auto n=c.reference.name()[14];
        if(n!='5') CHECK((n==(c.marker_ordinal%2 ? '2':'1') || n==(c.marker_ordinal%2 ? '4':'3')));
      }
    }
    CHECK(tile5); REQUIRE(observed.size()==101);
    if(baseline.empty()) baseline=observed; else CHECK(observed==baseline);
  }
}

TEST_CASE("E8 life map network reset and teardown cancel stale movement without HL fallback",
    "[e8][movement-audio][game-module]") {
  Host h; const auto first=h.observe(spatial_step(1)); REQUIRE(first.count==1);
  auto life=spatial_step(2); life.life_epoch=2;
  const auto fresh=h.observe(life); REQUIRE(fresh.count==1);
  CHECK(fresh.movement_epoch!=first.movement_epoch); CHECK(fresh.movement_diagnostic->ordinal==1);
  CHECK(h.observe(spatial_step(3)).count==0); // old life is rejected, not a new reset
  h.host.reset({2,2,{1}}); h.host.configure_movement_materials(e8_table);
  life=spatial_step(1); life.generation=2;
  const auto map=h.observe(life); REQUIRE(map.count==1); CHECK(map.movement_diagnostic->ordinal==1);
  h.host.teardown(); CHECK(h.host.movement_sound_preparation().empty());
  h.host.observe_movement_audio(spatial_step(4),false); CHECK(h.host.drain_audio().count==0);
}

TEST_CASE("E8 supporting face identity selects seam side slope step and rejects ambiguous edge",
    "[e8][surface-query]") {
  for(const auto [slope,height]:{std::pair{0.F,0.F},{.5F,0.F},{0.F,12.F}}) {
    sound::SurfaceTextureQuery query(two_floors(slope,height),point_floors(slope,height));
    for(const float x:{.01F,8.F,15.9F,16.1F,24.F,31.9F}) {
      const auto result=query.at_support(floor_support(x,slope,x>16 ? height:0),nullptr);
      REQUIRE(result.status==sound::SurfaceTextureResult::Status::found);
      CHECK(result.texture_name==(x<16 ? "CONCRETE":"METAL"));
      CHECK(result.source_surface_index==(x<16 ? 41U:87U)); // never collision plane 999
      CHECK(result.source_material_index==(x<16 ? 0U:1U));
      REQUIRE(result.world_origin); CHECK(result.world_origin->z==Catch::Approx(slope*x+(x>16 ? height:0)).margin(.001));
      Host h; auto o=spatial_step(1,320,result.texture_name);
      o.source_surface_index=result.source_surface_index; o.world_origin=result.world_origin;
      const auto b=h.observe(o); REQUIRE(b.count==1);
      CHECK(b.cues[0].reference.name().starts_with(x<16 ? "player/pl_step":"player/pl_metal"));
    }
  }
  sound::SurfaceTextureQuery query(two_floors(),point_floors());
  CHECK(query.at_support(floor_support(16),nullptr).status==sound::SurfaceTextureResult::Status::ambiguous_support);
  CHECK(query.at_support(floor_support(40),nullptr).status==sound::SurfaceTextureResult::Status::geometry_unavailable);
}

TEST_CASE("E8 material transition uses current support at the next cadence not a prior hit",
    "[e8][surface-query][movement-audio]") {
  sound::SurfaceTextureQuery query(two_floors(),point_floors()); Host h;
  for(unsigned i=1;i<=16;++i) {
    const auto result=query.at_support(floor_support(i<8 ? 8.F:24.F),nullptr);
    auto o=spatial_step(i,320,result.texture_name); o.source_surface_index=result.source_surface_index;
    const auto b=h.observe(o);
    if(i==1) {REQUIRE(b.count==1); CHECK(b.cues[0].reference.name().starts_with("player/pl_step"));}
    else if(i==16) {REQUIRE(b.count==1); CHECK(b.cues[0].reference.name().starts_with("player/pl_metal")); CHECK(b.movement_diagnostic->surface_index==87);}
    else CHECK(b.count==0);
  }
}
TEST_CASE("E8 production collision ground from literal BSP constrains real slope texture trace",
    "[e8][surface-query][movement][prediction]") {
  for(float slope:{0.F,.125F,.5F,.9F}) {
    const auto parsed=sound::bsp::GoldSrcBspParser::parse(
        hlclient::tests::literal_movement_bsp::make_slope_bsp_v30(slope)); REQUIRE(parsed);
    const auto collision=sound::collision::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);
    REQUIRE(collision);
    sound::movement::WorldOnlyMovementCollision provider{collision.package};
    sound::movement::GoldSrcLocalMovementScratch scratch;
    sound::ReferencePredictionSeed seed; seed.generation=1; seed.record_identity=1;
    seed.command_boundary=*sound::GoldSrcUserCmdSequence::create(1);
    seed.origin={8,8,36+16*slope+8*slope+.001}; seed.velocity={0,0,0}; seed.view_offset={0,0,28};
    seed.base_velocity={0,0,0}; seed.flags=1U<<9U; seed.move_type=3; seed.use_hull=0;
    seed.gravity_multiplier=seed.friction_multiplier=1;
    const auto ground=sound::derive_reference_prediction_ground(seed,{},provider,scratch);
    INFO(sound::to_string(ground.status));
    REQUIRE(ground.state); REQUIRE(ground.state->ground_state().grounded());
    sound::SurfaceTextureQuery query(two_floors(slope),collision.package);
    const auto surface=query.at_support(*ground.state,nullptr);
    REQUIRE(surface.status==sound::SurfaceTextureResult::Status::found);
    CHECK(surface.source_surface_index==41); CHECK(surface.texture_name=="CONCRETE");
    CHECK(surface.world_origin->z==Catch::Approx(8*slope).margin(.001));
  }
}

TEST_CASE("E8 spatial first-step PCM isolates volume origin panning and distance attenuation",
    "[e8][movement-audio][audio]") {
  for(const auto [texture,gain]:{std::pair{"CONCRETE",.5F},{"METAL",.5F},{"TILE",.5F},
      {"DIRT",.55F},{"VENT",.7F},{"SLOSH",.5F}}) {
    Host h; h.host.configure_movement_materials(e8_table); Loader loader; Sink sink;
    sink.set_listener({{8,8,64},{1,0,0},1,false}); sound::LocalAudio local(sink);
    const auto batch=h.observe(spatial_step(1,320,texture)); local.update(batch,&loader,.02,true);
    REQUIRE(sink.starts.size()==1); CHECK_FALSE(sink.starts[0].local);
    CHECK(sink.starts[0].origin.z==0); CHECK(sink.starts[0].volume==Catch::Approx(gain));
    std::array<float,32> pcm{}; sink.offline.mixer.render(pcm);
    const auto expected=.125F*gain*(1-64.F*.8F/1000.F);
    for(float value:pcm) {CHECK(std::isfinite(value)); CHECK(value==Catch::Approx(expected));}
    local.update(batch,&loader,.04,true); CHECK(sink.starts.size()==1);
  }
  std::array<float,3> powers{};
  for(std::size_t i=0;i<3;++i) {
    Host h; Loader loader; Sink sink; sound::LocalAudio local(sink);
    sink.set_listener({{}, {1,0,0},1,false}); auto o=spatial_step(1);
    o.world_origin=hlclient::assets::AssetVector3{static_cast<float>(i)*500,0,0};
    local.update(h.observe(o),&loader,.02,true);
    std::array<float,32> pcm{}; sink.offline.mixer.render(pcm);
    for(float value:pcm) powers[i]+=value*value;
    if(i) CHECK(pcm[1]>pcm[0]); // real source direction, not fake foot-side pan
  }
  CHECK(powers[1]>powers[2]); CHECK(powers[0]>0); CHECK(powers[2]>0);
}

TEST_CASE("E8 all bounded preparation tokens exist before the first normal step",
    "[e8][movement-audio][audio]") {
  Host h; const auto tokens=h.host.movement_sound_preparation(); REQUIRE(tokens.size()==34);
  std::set<std::string> names;
  for(const auto& token:tokens) {
    CHECK(sound::valid_local_sound_reference(token)); names.emplace(token.name());
  }
  CHECK(names.size()==34); CHECK(names.contains("player/pl_slosh4.wav"));
  CHECK(names.contains("player/pl_tile5.wav")); CHECK(names.contains("player/pl_ladder4.wav"));
  const auto first=h.observe(spatial_step(1)); REQUIRE(first.count==1);
  CHECK(names.contains(std::string{first.cues[0].reference.name()}));
  // Absent/malformed table fallback is distinct from absent supporting geometry.
  for(const auto text:{std::string_view{},std::string_view{"invalid"}}) {
    h.host.reset({1,2,{1}}); h.host.configure_movement_materials(text);
    const auto b=h.observe(spatial_step(1)); REQUIRE(b.count==1);
    CHECK(std::string_view{b.movement_diagnostic->classification.data()}==
        (text.empty() ? "missing_table_concrete_fallback":"malformed_table_concrete_fallback"));
  }
}

TEST_CASE("E8 pending missing decode rejected muted and stale steps do not become delayed bursts",
    "[e8][movement-audio][audio]") {
  for(const auto status:{sound::SoundAssetStatus::pending,sound::SoundAssetStatus::missing,
      sound::SoundAssetStatus::not_authorized,sound::SoundAssetStatus::open_failed,
      sound::SoundAssetStatus::decode_failed,sound::SoundAssetStatus::limit}) {
    Host h; Loader loader; loader.status=status; Sink sink; sound::LocalAudio local(sink);
    const auto b=h.observe(spatial_step(1)); local.update(b,&loader,.02,true);
    CHECK(sink.starts.empty());
    auto drained=b; drained.count=0;
    local.update(drained,&loader,.4,true);
    loader.status=sound::SoundAssetStatus::ready;
    local.update(drained,&loader,.5,true); CHECK(sink.starts.empty());
    if(status==sound::SoundAssetStatus::pending) CHECK(local.statistics().movement_late==1);
    if(status==sound::SoundAssetStatus::missing) CHECK(local.statistics().movement_missing==1);
  }
  Host h; Loader loader; Sink sink; sound::LocalAudio local(sink);
  const auto b=h.observe(spatial_step(1)); local.update(b,&loader,.02,false);
  local.update(b,&loader,.04,true); CHECK(sink.starts.empty());
  CHECK(local.statistics().movement_muted==1);
}

TEST_CASE("E8 cadence outbox and mixer voices are independently bounded",
    "[e8][movement-audio][audio]") {
  Host h;
  for(unsigned i=1;i<=600;++i) h.host.observe_movement_audio(spatial_step(i),false);
  const auto b=h.host.drain_audio(); CHECK(b.count==32); CHECK(b.statistics.movement_outbox_limit==8);
  Loader loader; Sink sink; sound::LocalAudio local(sink);
  auto batch=b; batch.count=1; batch.cues[0].scheduled_seconds=.02;
  sink.set_listener({{}, {1,0,0},1,false});
  for(unsigned i=1;i<=hlclient::audio::maximum_voices;++i) {
    hlclient::audio::Command voice; voice.voice=i; voice.asset=loader.pcm;
    CHECK(sink.submit(voice));
  }
  local.update(batch,&loader,.02,true);
  std::array<float,8> pcm{}; sink.offline.mixer.render(pcm);
  CHECK(sink.offline.mixer.statistics().rejected==1);
  CHECK(sink.offline.mixer.active()==hlclient::audio::maximum_voices);
}

TEST_CASE("E8 ground body weapon impact and server voices coexist in production mixer",
    "[e8][movement-audio][audio]") {
  Host h; Loader loader; Sink sink; sound::LocalAudio local(sink); sound::ServerAudio server(sink);
  sink.set_listener({{8,8,64},{1,0,0},1,false}); auto batch=h.observe(spatial_step(1));
  REQUIRE(batch.count==1);
  for(const auto [kind,channel,name]:{
      std::tuple{api::LocalSoundKind::fire,api::LocalSoundChannel::weapon,"weapons/pl_gun3.wav"},
      {api::LocalSoundKind::impact,api::LocalSoundChannel::automatic,"player/pl_step1.wav"},
      {api::LocalSoundKind::shell_contact,api::LocalSoundChannel::automatic,"player/pl_shell1.wav"}}) {
    auto cue=batch.cues[0]; cue.kind=kind; cue.channel=channel;
    cue.serial+=batch.count; cue.reference.sample.fill(0);
    std::copy_n(name,std::char_traits<char>::length(name),cue.reference.sample.begin());
    batch.cues[batch.count++]=cue;
  }
  local.update(batch,&loader,.02,true);
  sound::CommittedSound event; event.generation=1; event.record=1; event.ordinal=1; event.cursor=1;
  event.opcode=sound::RuntimeControlOpcode::svc_sound; event.sound.sound_reference=3;
  event.sound.channel=3; event.sound.entity_reference=2; event.sound.volume=255;
  server.consume(event,{}); server.update(&loader,{});
  REQUIRE(sink.starts.size()==5); std::set<std::uint64_t> voices;
  for(const auto& start:sink.starts) voices.insert(start.voice);
  CHECK(voices.size()==5); std::array<float,64> pcm{}; sink.offline.mixer.render(pcm);
  CHECK(sink.offline.mixer.statistics().started==5); CHECK(pcm[0]>0);
}

TEST_CASE("E8 audible acceleration onset does not inherit a muted full step interval",
    "[e8][movement-audio][e8-fix]") {
  for(float direction:{-1.F,1.F}) {
    Host h;
    const auto quiet=h.observe(spatial_step(1,std::abs(direction*80.F)));
    REQUIRE(quiet.count==0);
    const auto audible=h.observe(spatial_step(2,std::abs(direction*270.F)));
    REQUIRE(audible.count==1);
    CHECK(audible.movement_diagnostic->ordinal==2);
    for(unsigned i=3;i<17;++i) CHECK(h.observe(spatial_step(i,270)).count==0);
    REQUIRE(h.observe(spatial_step(17,270)).count==1);
    CHECK(h.observe(spatial_step(17,270)).count==0);
  }
}

TEST_CASE("E8 quiet onset checkpoints preserve cadence replay and server mute policy",
    "[e8][e8-fix][movement-audio][prediction]") {
  Host h;
  REQUIRE(h.observe(spatial_step(1,80)).count==0);
  REQUIRE(h.observe(spatial_step(2,270)).count==1);
  h.host.rewind_movement_audio(0);
  CHECK(h.observe(spatial_step(1,80),true).count==0);
  CHECK(h.observe(spatial_step(2,270),true).count==0);
  // A quiet speed excursion after a heard step cannot bypass its cadence.
  CHECK(h.observe(spatial_step(3,80)).count==0);
  for(unsigned i=4;i<17;++i) CHECK(h.observe(spatial_step(i,270)).count==0);
  const auto next=h.observe(spatial_step(17,270)); REQUIRE(next.count==1);
  CHECK(next.movement_diagnostic->ordinal==3);
  CHECK(h.observe(spatial_step(17,270)).count==0);
  for(const auto permission:{std::optional<bool>{},std::optional<bool>{false}}) {
    Host muted;
    auto quiet=spatial_step(1,80); quiet.movevars_footsteps=permission;
    CHECK(muted.observe(quiet).count==0);
    for(unsigned i=2;i<21;++i) {
      auto running=spatial_step(i,270); running.movevars_footsteps=permission;
      CHECK(muted.observe(running).count==0);
    }
  }
}

TEST_CASE("E8 independent compiled player planes cannot silence point-hull ramp materials",
    "[e8][e8-fix][surface-query][movement-audio]") {
  for(const float slope:{.5F,.9F}) {
    sound::SurfaceTextureQuery query(two_floors(slope),point_floors(slope));
    for(const float direction:{-1.F,1.F}) {
      Host h;
      for(unsigned command=1;command<=31;++command) {
        const float x=direction>0 ? 2+command*.35F : 14-command*.35F;
        const auto authored=floor_support(x,slope);
        hlclient::movement::LocalPlayerMovementStateCreateInfo info;
        const auto& ground=authored.ground_state();
        // A independently compiled player plane, deliberately unlike analytic
        // box expansion by the original-map observed 6.7 plane units.
        info.origin=authored.origin(); info.origin.z-=6.7F/ground.plane().normal.z;
        info.mode=hlclient::movement::PlayerMovementMode::walking;
        info.ground.grounded=info.ground.walkable=true; info.ground.hit=ground.hit();
        info.ground.plane=ground.plane(); info.ground.plane.distance-=6.7;
        info.ground.contact_position=info.origin;
        const auto state=hlclient::movement::LocalPlayerMovementState::create(info); REQUIRE(state);
        const auto surface=query.at_support(*state.state,nullptr);
        REQUIRE(surface.status==sound::SurfaceTextureResult::Status::found);
        CHECK(surface.source_surface_index==41);
        REQUIRE(surface.world_origin);
        CHECK(surface.world_origin->z==Catch::Approx(x*slope).margin(.001));
        auto observation=spatial_step(command,270,surface.texture_name);
        observation.world_origin=surface.world_origin;
        const auto batch=h.observe(observation);
        CHECK(batch.count==((command-1)%15==0 ? 1U:0U));
      }
    }
  }
  const auto scene=two_floors(.5F);
  sound::SurfaceTextureQuery missing(scene);
  CHECK(missing.at_support(floor_support(8,.5F),nullptr).status==
      sound::SurfaceTextureResult::Status::geometry_unavailable);
  sound::SurfaceTextureQuery incompatible(scene,point_floors());
  CHECK(incompatible.at_support(floor_support(8,.5F),nullptr).status==
      sound::SurfaceTextureResult::Status::invalid_support);
}

TEST_CASE("E8 opt in Crossfire ramp support resolves original compiled hulls read only",
    "[e8-fix][surface-query][installed-ramp]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value=nullptr; std::size_t count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
  std::free(value);
#else
  if(const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT")) root=value;
#endif
  if(root.empty()) SKIP("No opt-in read-only local game root");
  namespace ready=hlclient::tests::readiness_fixture;
  const auto created=hlclient::app::RuntimeReplayLocalAssets::create(
      ready::parse_resource_list({{2U,"maps/crossfire.bsp",9U,0U,0U}}),
      ready::parse_server_info("maps/crossfire.bsp"),root,"valve",
      hlclient::app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  const auto scene=created.projection->surface_scene(); REQUIRE(scene);
  const auto package=scene->world_package(); REQUIRE(package);
  sound::SurfaceTextureQuery query(scene,created.projection->collision_world_package());
  sound::movement::WorldOnlyMovementCollision collision(created.projection->collision_world_package());
  sound::movement::GoldSrcLocalMovementScratch scratch;
  unsigned checked=0,matched=0;
  for(const auto& range:package->surface_ranges()) {
    if(range.index_count<3) continue;
    const auto vertices=package->vertices(); const auto indices=package->indices();
    const auto normal=vertices[indices[range.first_index]].normal;
    if(normal.z<.75F || normal.z>.99F) continue;
    hlclient::assets::AssetVector3 center{};
    const auto vertex_count=static_cast<float>(range.index_count);
    for(std::size_t i=0;i<range.index_count;++i) {
      const auto p=vertices[indices[range.first_index+i]].position;
      center.x+=p.x/vertex_count; center.y+=p.y/vertex_count; center.z+=p.z/vertex_count;
    }
    const float lift=36.F+16.F*(std::abs(normal.x)+std::abs(normal.y))/normal.z;
    const auto trace=collision.trace_hull({center.x,center.y,center.z+lift+8},
        {center.x,center.y,center.z+lift-16},hlclient::movement::PlayerMovementHull::standing,scratch.collision);
    if(!trace || trace.result->start_solid || !trace.result->collision_plane || !trace.result->hit) continue;
    const auto n=trace.result->collision_plane->normal;
    if(n.x*normal.x+n.y*normal.y+n.z*normal.z<.9999F) continue;
    sound::ReferencePredictionSeed seed; seed.generation=1; seed.record_identity=1;
    seed.command_boundary=*sound::GoldSrcUserCmdSequence::create(1);
    const auto p=trace.result->end_position;
    seed.origin={p.x,p.y,p.z}; seed.velocity={0,0,0}; seed.view_offset={0,0,28};
    seed.base_velocity={0,0,0}; seed.flags=1U<<9U; seed.move_type=3;
    seed.gravity_multiplier=seed.friction_multiplier=1;
    const auto grounded=sound::derive_reference_prediction_ground(seed,{},collision,scratch);
    if(!grounded.state || !grounded.state->ground_state().grounded()) continue;
    ++checked;
    const auto result=query.at_support(*grounded.state,nullptr);
    matched+=result.status==sound::SurfaceTextureResult::Status::found;
    const double authored=normal.x*center.x+normal.y*center.y+normal.z*center.z;
    const double inferred=trace.result->collision_plane->distance-
        16*(std::abs(n.x)+std::abs(n.y))-36*n.z;
    if(checked<=16) std::cout << "offline_ramp_control surface=" << range.source_world_surface_index
        << " normal=" << n.x << ',' << n.y << ',' << n.z
        << " plane_offset_error=" << inferred-authored
        << " found=" << (result.status==sound::SurfaceTextureResult::Status::found) << '\n';
  }
  std::cout << "offline_ramp_control checked=" << checked << " matched=" << matched << '\n';
  REQUIRE(checked>0);
  CHECK(matched==checked);
}

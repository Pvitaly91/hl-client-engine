#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/renderer/render_camera_math.hpp>

#include "entity_render/entity_opengl_test_support.hpp"
#include "entity_visual/entity_visual_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "world_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>

namespace {
namespace app = hlclient::app;
namespace client = hlclient::client;
namespace fixture = hlclient::tests;
namespace readiness = fixture::readiness_fixture;

client::RuntimeClientObservationState clipping_observation() {
  client::RuntimeClientObservationState state;
  state.generation=1U;state.publication_revision=1U;
  state.server_time_metadata.generation=1U;
  state.client_metadata.generation=1U;
  state.entity_metadata={1U,client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{1U,1U,1U,0U,1U}};
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(client::valid_runtime_observation(state));
  return state;
}

struct ClippingContext {
  fixture::ScopedLocalResourceTestRoot root;
  std::vector<std::byte> map;
  ClippingContext() {
    fixture::SyntheticBspBuilder builder;
    const std::string entities="{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
    const auto bytes=std::as_bytes(std::span{entities});
    builder.lump(fixture::SyntheticBspLumpId::entities).assign(bytes.begin(),bytes.end());
    map=builder.build();
    fixture::SyntheticWad3Entry texture;
    texture.name="TEST_QUAD";
    texture.payload=fixture::synthetic_goldsrc_miptex("TEST_QUAD",64U,64U);
    root.write("valve","test.wad",fixture::synthetic_wad3({texture}).bytes);
    root.write("valve","maps/test_map.bsp",map);
  }
};

TEST_CASE("First-person near clipping removes camera-origin geometry but retains ordinary Studio geometry",
          "[renderer][first-person][opengl][actual-context][viewmodel-clipping]") {
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  ClippingContext context;
  auto near_model=fixture::literal_minimal_goldsrc_studio_v10();
  // An independent owned triangle in the same camera-origin hazard range.
  // Disable the unrelated fixture's +2-unit animation translation.
  fixture::studio_write_u16le(near_model,fixture::kSyntheticStudioAnimationOffset,0U);
  for(std::size_t i=0;i<3U;++i) {
    fixture::studio_write_vector3(near_model,fixture::kSyntheticStudioVerticesOffset+12U*i,
        0.4F,i==0U ? -0.5F : i==1U ? 0.5F : 0.0F,i==2U ? 1.0F : 0.22F);
    fixture::studio_write_vector3(near_model,fixture::kSyntheticStudioNormalsOffset+12U*i,-1,0,0);
  }
  auto visible_model=near_model;
  for(std::size_t i=0;i<3U;++i)
    fixture::studio_write_f32le(visible_model,fixture::kSyntheticStudioVerticesOffset+12U*i,12.0F);
  context.root.write("valve","models/v_near_camera.mdl",near_model);
  context.root.write("valve","models/v_visible_geometry.mdl",visible_model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/v_near_camera.mdl",21U,static_cast<std::uint32_t>(near_model.size()),0U},
      {2U,"models/v_visible_geometry.mdl",22U,static_cast<std::uint32_t>(visible_model.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  const auto state=clipping_observation();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  hlclient::game_api::ViewmodelIntent intent;
  intent.status=hlclient::game_api::ViewmodelStatus::ready;
  intent.generation=state.generation;intent.publication_revision=1U;intent.model_index=21U;
  const auto near_frame=created.projection->materialize_viewmodel(intent);REQUIRE(near_frame);
  intent.model_index=22U;
  const auto visible_frame=created.projection->materialize_viewmodel(intent);REQUIRE(visible_frame);
  auto world_result=fixture::world_render_fixture::make_package();REQUIRE(world_result);
  auto background=std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*world_result.package));
  for(const auto extent:{hlclient::renderer::RenderExtent{320,240},hlclient::renderer::RenderExtent{640,360}}) {
    CAPTURE(extent.width,extent.height);
    for(const bool with_world:{false,true}) {
      CAPTURE(with_world);
      hlclient::renderer::RenderScene scene;
      if(with_world) scene.static_world=hlclient::renderer::RenderStaticWorld{background};
      const auto draw=[&] {
        gl->renderer().render(scene,extent);CHECK(glGetError()==GL_NO_ERROR);
        return gl->renderer().observe_framebuffer(extent,scene.clear_color,true);
      };
      const auto baseline=draw();REQUIRE(baseline);
      const auto saved_camera=scene.camera;
      scene.first_person_entities=near_frame;
      const auto clipped=draw();REQUIRE(clipped);
      CHECK(std::ranges::equal(clipped.rgba8,baseline.rgba8));
      CHECK(scene.camera==saved_camera);
      scene.first_person_entities=visible_frame;
      const auto visible=draw();REQUIRE(visible);
      CHECK_FALSE(std::ranges::equal(visible.rgba8,baseline.rgba8));
      const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
      scene.camera.position={100,-200,80};scene.camera.target={101,-199,81};
      if(!with_world) CHECK(std::ranges::equal(draw().rgba8,visible.rgba8));
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
      scene.camera=saved_camera;scene.first_person_entities.reset();
      CHECK(std::ranges::equal(draw().rgba8,baseline.rgba8));
    }
  }
  gl->release_renderer();
}

TEST_CASE("E17 optional approved Shotgun root-shell near clipping retains insert-shell GPU pixels",
          "[e17][installed-shotgun][viewmodel-clipping][first-person][opengl]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value=nullptr;std::size_t count=0U;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
  std::free(value);
#else
  if(const auto* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT")) root=value;
#endif
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("No exact opt-in approved research root");
  const auto model_path=root/"valve/models/v_shotgun.mdl";
  if(!std::filesystem::is_regular_file(model_path)) SKIP("Approved Shotgun model absent");
  const auto original_time=std::filesystem::last_write_time(model_path);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/crossfire.bsp",9U,0U,0U},{2U,"models/v_shotgun.mdl",21U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),
      root,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : "ready"));REQUIRE(created.projection);
  const auto state=clipping_observation();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  const auto model=created.projection->studio_model_data(21U);REQUIRE(model);
  REQUIRE(model->bones.size()==32U);REQUIRE(model->sequences.size()==10U);
  constexpr std::array<std::uint32_t,10U> frames{21U,21U,21U,10U,11U,9U,13U,11U,21U,61U};
  for(std::size_t i=0U;i<frames.size();++i) REQUIRE(model->sequences[i].frame_count==frames[i]);
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();hlclient::renderer::RenderScene scene;
  const auto draw=[&] {
    gl->renderer().render(scene,{640,480});CHECK(glGetError()==GL_NO_ERROR);
    return gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);
  };
  const auto baseline=draw();REQUIRE(baseline);
  // Isolate this component: firing can legitimately lift the weapon into
  // the top region, and an inserted shell can be occluded by hand/receiver.
  const auto top_changes=[&](const auto& pixels) {
    REQUIRE(pixels.size()==baseline.rgba8.size());
    std::size_t changed=0U;
    for(std::size_t y=420U;y<480U;++y) for(std::size_t x=0U;x<640U;++x) {
      const auto at=(y*640U+x)*4U;
      changed+=pixels[at]!=baseline.rgba8[at] || pixels[at+1U]!=baseline.rgba8[at+1U] ||
          pixels[at+2U]!=baseline.rgba8[at+2U];
    }
    return changed;
  };
  std::uint64_t isolated_revision=0U;
  const auto isolate=[&](const hlclient::renderer::RenderDynamicEntities& original,bool shell_only) {
    hlclient::entity_render::EntityRenderFrameBuildInput input;
    input.resource_id=0xE17C1U;input.resource_revision=++isolated_revision;
    input.interpolation=original.frame->interpolation();
    input.studio_poses.assign(original.frame->studio_poses().begin(),original.frame->studio_poses().end());
    input.studio_instances.assign(original.frame->studio_instances().begin(),original.frame->studio_instances().end());
    REQUIRE(input.studio_poses.size()==1U);
    REQUIRE(input.studio_poses[0U].bone_matrices.size()==32U);
    for(std::size_t bone=0U;bone<32U;++bone)
      if(shell_only ? bone!=31U : bone==31U) input.studio_poses[0U].bone_matrices[bone][12U]=-100.0F;
    auto built=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*original.package,std::move(input));
    REQUIRE(built);
    auto result=original;
    result.frame=std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*built.frame));
    return result;
  };
  hlclient::game_api::ViewmodelIntent intent;
  intent.status=hlclient::game_api::ViewmodelStatus::ready;
  intent.generation=state.generation;intent.publication_revision=1U;
  intent.model_index=21U;intent.body=2U;
  std::size_t non_reload_frames=0U;
  for(const std::uint32_t sequence:{0U,1U,2U,4U,5U,6U,7U,8U,9U}) {
    intent.sequence=sequence;
    for(std::uint32_t frame=0U;frame<frames[sequence];++frame) {
      CAPTURE(sequence,frame);intent.frame_coordinate=frame;
      const auto original=created.projection->materialize_viewmodel(intent);REQUIRE(original);
      scene.first_person_entities=original;
      const auto full=draw();REQUIRE(full);
      if(sequence==0U) CHECK(top_changes(full.rgba8)==0U);
      scene.first_person_entities=isolate(*original,false);
      const auto excluded=draw();REQUIRE(excluded);
      // Stronger than a blank top band: this root shell contributes no pixels
      // anywhere, even while the legitimately animated gun reaches the top.
      CHECK(std::ranges::equal(full.rgba8,excluded.rgba8));
      ++non_reload_frames;
    }
  }
  const auto reload_shell=std::ranges::find_if(model->submodels,[](const auto& part){return part.name=="shell";});
  REQUIRE(reload_shell!=model->submodels.end());REQUIRE_FALSE(reload_shell->vertices.empty());
  std::size_t visible_reload_frames=0U,unoccluded_reload_frames=0U;
  intent.sequence=3U;
  for(std::uint32_t frame=0U;frame<frames[3U];++frame) {
    CAPTURE(frame);intent.frame_coordinate=frame;
    const auto original=created.projection->materialize_viewmodel(intent);REQUIRE(original);
    const auto& matrix=original->frame->studio_poses()[0U].bone_matrices[31U];
    for(const auto& vertex:reload_shell->vertices) {
      REQUIRE(vertex.position_bone_index==31U);
      const auto p=vertex.source_position;
      const auto forward=matrix[0U]*p.x+matrix[4U]*p.y+matrix[8U]*p.z+matrix[12U];
      CHECK(forward>1.0F); // Every actual insert-shell vertex survives near clipping.
    }
    scene.first_person_entities=original;const auto full=draw();REQUIRE(full);
    scene.first_person_entities=isolate(*original,false);const auto excluded=draw();REQUIRE(excluded);
    unoccluded_reload_frames+=!std::ranges::equal(full.rgba8,excluded.rgba8);
    scene.first_person_entities=isolate(*original,true);const auto shell=draw();REQUIRE(shell);
    visible_reload_frames+=shell.non_clear_pixel_count>0U;
  }
  CHECK(visible_reload_frames>0U);
  std::cout << "offline_shotgun_clipping_control non_reload_frames=" << non_reload_frames
      << " root_shell_contribution=zero idle_top_band=clear insert_vertices=unclipped"
      << " isolated_visible_reload_frames=" << visible_reload_frames
      << " unoccluded_full_reload_frames=" << unoccluded_reload_frames << "\n";
  scene.first_person_entities.reset();

  gl->release_renderer();
  CHECK(std::filesystem::last_write_time(model_path)==original_time);
}
} // namespace

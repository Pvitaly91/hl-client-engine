#if HLCLIENT_HAS_GAME_HALFLIFE
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/games/halflife/presentation.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include "entity_render/entity_opengl_test_support.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
namespace api=hlclient::game_api;
namespace r=hlclient::renderer;
namespace app=hlclient::app;
namespace f=hlclient::tests;
struct Weapon { std::uint8_t id; const char* name; };
constexpr std::array weapons{
    Weapon{1,"models/v_crowbar.mdl"},Weapon{2,"models/v_9mmhandgun.mdl"},
    Weapon{3,"models/v_357.mdl"},Weapon{4,"models/v_9mmAR.mdl"},
    Weapon{6,"models/v_crossbow.mdl"},Weapon{7,"models/v_shotgun.mdl"},
    Weapon{8,"models/v_rpg.mdl"},Weapon{9,"models/v_gauss.mdl"},
    Weapon{10,"models/v_egon.mdl"},Weapon{11,"models/v_hgun.mdl"},
    Weapon{12,"models/v_grenade.mdl"},Weapon{13,"models/v_tripmine.mdl"},
    Weapon{14,"models/v_satchel.mdl"},Weapon{14,"models/v_satchel_radio.mdl"},
    Weapon{15,"models/v_squeak.mdl"}};
auto state() {
  hlclient::client::RuntimeClientObservationState s;
  s.generation=1;s.publication_revision=1;
  s.server_time_metadata.generation=1;s.client_metadata.generation=1;
  s.entity_metadata={1,hlclient::client::RuntimeObservationFreshness::observed_in_record,
      hlclient::client::RuntimeObservationCompleteness::complete_reconstruction,
      hlclient::client::RuntimeObservationSource{1,1,1,0,1}};
  s.canonical_state_hash=hlclient::client::runtime_observation_canonical_hash(s);
  return s;
}
}

TEST_CASE("HL all weapon models select the same source-unit held origin and widescreen projection",
          "[stock-viewmodel][game-module][no-stock]") {
  for(const auto& weapon:weapons) {
    CAPTURE(weapon.id,weapon.name);
    hlclient::games::halflife::HalfLifePresentation presentation;
    auto observed=state();observed.receiving_client.emplace();
    observed.receiving_client->health=100;observed.receiving_client->viewmodel_index=21;
    observed.weapon_hud.active_weapon_id=weapon.id;
    api::LocalWeaponModelMetadata model;
    model.generation=1;model.model_index=21;model.resource_name=weapon.name;
    model.sequences.push_back({30,21,true,{},{}});
    model.supported_bodies[0]=model.selectable_bodies[0]=true;
    const auto intent=presentation.viewmodel(observed,model,0,{});
    REQUIRE(intent.status==api::ViewmodelStatus::ready);
    CHECK(intent.camera_local_origin==std::array<float,3>{0,0,-1});
    const auto camera=presentation.camera(observed,0);
    CHECK(camera.horizontal_field_of_view_degrees==90.);
    CHECK(camera.field_of_view_reference_aspect_ratio==4./3.);
    CHECK(camera.mouse_sensitivity_scale==1.);
    observed.lifecycle.state=hlclient::client::LocalPlayerLifeState::dead;
    CHECK(presentation.viewmodel(observed,model,.1,{}).status==api::ViewmodelStatus::hidden);
  }
}

TEST_CASE("Installed HL full weapon suite uses unscaled meshes and source-sized muzzle sprites on GPU",
          "[stock-viewmodel][stock-viewmodel-assets][first-person][actual-context]") {
  std::filesystem::path root;
  std::filesystem::path evidence;
#ifdef _WIN32
  wchar_t* value=nullptr;std::size_t count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;
  std::free(value);
  value=nullptr;count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_VIEWMODEL_EVIDENCE_DIR")==0 && value)evidence=value;
  std::free(value);
#endif
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("No exact opt-in approved research root");
  auto gl=f::entity_opengl_fixture::try_context(640,480);
  if(!gl || !f::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  const std::array<std::string,2> sprites{"sprites/muzzleflash1.spr","sprites/muzzleflash2.spr"};
  const auto draw=[&](const r::RenderScene& scene,r::RenderExtent extent) {
    gl->renderer().render(scene,extent);CHECK(glGetError()==GL_NO_ERROR);
    auto pixels=gl->renderer().observe_framebuffer(extent,scene.clear_color,true);REQUIRE(pixels);
    return pixels;
  };
  const auto save=[&](std::string_view name,const auto& pixels,r::RenderExtent extent) {
    if(evidence.empty())return;
    REQUIRE(std::filesystem::is_directory(evidence));
    std::ofstream out(evidence/(std::string{name}+".ppm"),std::ios::binary);REQUIRE(out);
    out<<"P6\n"<<extent.width<<' '<<extent.height<<"\n255\n";
    for(int y=extent.height-1;y>=0;--y)for(int x=0;x<extent.width;++x) {
      const auto i=(static_cast<std::size_t>(y)*static_cast<std::size_t>(extent.width)+static_cast<std::size_t>(x))*4U;
      out.write(reinterpret_cast<const char*>(pixels.rgba8.data()+i),3);
    }
    REQUIRE(out.good());
  };
  for(const auto& weapon:weapons) {
    CAPTURE(weapon.name);
    const auto path=root/"valve"/weapon.name;
    REQUIRE(std::filesystem::is_regular_file(path));
    const auto before=std::filesystem::last_write_time(path);
    const auto resources=f::readiness_fixture::parse_resource_list({
        {2,"maps/crossfire.bsp",9,0,0},{2,weapon.name,21,0,0}});
    auto created=app::RuntimeReplayLocalAssets::create(resources,
        f::readiness_fixture::parse_server_info("maps/crossfire.bsp"),root,"valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client,{},sprites);
    INFO((created.error ? created.error->context : "ready"));REQUIRE(created.projection);
    auto observed=state();hlclient::client::ClientWorldState world;
    REQUIRE(created.projection->reset_generation(observed,world));
    auto model=created.projection->presentation_model(1,21);REQUIRE(model);
    observed.receiving_client.emplace();observed.receiving_client->viewmodel_index=21;
    observed.receiving_client->health=100;observed.weapon_hud.active_weapon_id=weapon.id;
    hlclient::games::halflife::HalfLifePresentation presentation;
    auto intent=presentation.viewmodel(observed,model,0,{});
    REQUIRE(intent.status==api::ViewmodelStatus::ready);
    const auto materialized=created.projection->materialize_viewmodel(intent);REQUIRE(materialized);
    const auto& instance=materialized->frame->studio_instances()[0];
    CHECK(instance.transform.uniform_scale==1.F);CHECK(instance.transform.origin.z==-1.F);
    for(const auto extent:{r::RenderExtent{640,480},r::RenderExtent{640,400},r::RenderExtent{640,360}}) {
      CAPTURE(extent.width,extent.height);
      r::RenderScene scene;scene.camera={{0,0,0},{1,0,0},{0,0,1}};
      scene.camera.vertical_field_of_view_radians=static_cast<float>(2*std::atan(.75));
      const auto blank=draw(scene,extent);scene.first_person_entities=materialized;
      const auto held=draw(scene,extent);
      CHECK_FALSE(std::ranges::equal(blank.rgba8,held.rgba8));
      if(extent.height==360) {
        save(std::filesystem::path{weapon.name}.stem().string()+"-corrected",held,extent);
        if(weapon.id==2) {
          auto old_intent=intent;old_intent.camera_local_origin={};
          auto old=scene;old.first_person_entities=created.projection->materialize_viewmodel(old_intent);
          old.camera.vertical_field_of_view_radians=static_cast<float>(2*std::atan(9./16.));
          save("v_9mmhandgun-before",draw(old,extent),extent);
        }
      }
      const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
      scene.camera.position={100,200,300};scene.camera.target={101,201,301};
      CHECK(std::ranges::equal(held.rgba8,draw(scene,extent).rgba8));
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    }
    CHECK(std::filesystem::last_write_time(path)==before);
  }
  // At equal forward distance, 16:9 corrected muzzle diameter is 75% of the
  // previous horizontal-90 projection. Keep .1/.2/.3/.4 original source scales.
  const auto resources=f::readiness_fixture::parse_resource_list({{2,"maps/crossfire.bsp",9,0,0}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,
      f::readiness_fixture::parse_server_info("maps/crossfire.bsp"),root,"valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client,{},sprites);
  REQUIRE(created.projection);
  for(const unsigned marker:{11U,20U,31U,41U}) {
    CAPTURE(marker);
    const auto profile=hlclient::games::halflife::muzzle_sprite_profile(marker,0);REQUIRE(profile);
    CHECK(profile->scale==Catch::Approx(static_cast<float>(marker/10U)*.1F));
    app::RuntimeReplayLocalAssets::MuzzleSpriteFrame request{{24,0,0},*profile};
    r::RenderScene scene;scene.camera={{0,0,0},{1,0,0},{0,0,1}};
    scene.camera.vertical_field_of_view_radians=static_cast<float>(2*std::atan(.75));
    const auto blank=draw(scene,{640,360});
    scene.world_muzzle_entities=created.projection->materialize_muzzle_sprites(std::span{&request,1U},0);
    REQUIRE(scene.world_muzzle_entities);
    const auto pixels=draw(scene,{640,360});CHECK_FALSE(std::ranges::equal(pixels.rgba8,blank.rgba8));
    save("muzzle-"+std::to_string(marker)+"-corrected",pixels,{640,360});
    const auto projection=r::camera_view_projection(scene.camera,{640,360});REQUIRE(projection);
    const auto p=r::transform(*projection.matrix,{24,0,profile->scale*32,1});
    CHECK(p.y/p.w==Catch::Approx(profile->scale*32.F/(24.F*.75F)));
    auto old_camera=scene.camera;
    old_camera.vertical_field_of_view_radians=static_cast<float>(2*std::atan(9./16.));
    const auto old_projection=r::camera_view_projection(old_camera,{640,360});REQUIRE(old_projection);
    const auto old_p=r::transform(*old_projection.matrix,{24,0,profile->scale*32,1});
    CHECK((p.y/p.w)/(old_p.y/old_p.w)==Catch::Approx(.75F));
    std::cout<<"stock_muzzle marker="<<marker<<" source_scale="<<profile->scale
        <<" corrected_vertical_fov="<<scene.camera.vertical_field_of_view_radians<<'\n';
  }
  gl->release_renderer();
}
#endif

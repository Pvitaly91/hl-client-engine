#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/app/movement_diagnostics.hpp>
#if HLCLIENT_HAS_GAME_HALFLIFE
#include <hlclient/games/halflife/presentation.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/games/halflife/firearm_profile.hpp>
#endif
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/app/temporary_effect_presentation.hpp>
#include <hlclient/app/remote_effect_presentation.hpp>
#include <hlclient/audio/mixer.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/renderer/transient_visuals.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <thread>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/reference_client_move.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include "entity_visual/entity_visual_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "goldsrc_sprite_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "collision_brush_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include "player_origin_test_fixture.hpp"
#include "user_info_test_fixture.hpp"
#include <hlclient/goldsrc/user_info_update.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace {
namespace app = hlclient::app;
namespace client = hlclient::client;
namespace visual = hlclient::entity_visual;
namespace fixture = hlclient::tests;
namespace readiness = fixture::readiness_fixture;

client::RuntimeClientObservationState observation();

struct LocalContext {
  fixture::ScopedLocalResourceTestRoot root;
  std::vector<std::byte> map;
  std::vector<std::byte> model = fixture::literal_minimal_goldsrc_studio_v10();
  LocalContext(bool brushes = false, std::string_view brush_name = "BRUSH_ONLY", bool alternate = false,
               std::optional<std::uint8_t> world_light = {}, std::string_view wad_names = "test.wad") {
    fixture::SyntheticBspBuilder builder;
    const std::string text = "{\n\"classname\" \"worldspawn\"\n\"wad\" \"" +
        std::string{wad_names} + "\"\n}\n";
    const auto bytes = std::as_bytes(std::span{text});
    builder.lump(fixture::SyntheticBspLumpId::entities)
        .assign(bytes.begin(), bytes.end());
    if (brushes) {
      std::array faces{fixture::SyntheticBspFace{}, fixture::SyntheticBspFace{}, fixture::SyntheticBspFace{}};
      faces[1].texinfo_index = 1;
      faces[2].texinfo_index = 1;
      faces[1].light_styles = faces[2].light_styles = {0U,255U,255U,255U};
      faces[1].light_offset = faces[2].light_offset = 0;
      if (world_light) {
        faces[0].light_styles = {0U,255U,255U,255U};
        faces[0].light_offset = 0;
      }
      std::array models{fixture::SyntheticBspModel{}, fixture::SyntheticBspModel{}, fixture::SyntheticBspModel{}};
      models[1].first_face = 1; models[2].first_face = 2;
      models[1].visibility_leaf_count = models[2].visibility_leaf_count = 0;
      std::array texinfo{fixture::SyntheticBspTexinfo{}, fixture::SyntheticBspTexinfo{}};
      texinfo[1].miptex_index = 1;
      std::vector<std::optional<fixture::SyntheticBspMipTexture>> textures{
          fixture::synthetic_external_texture("TEST_QUAD"), fixture::synthetic_external_texture(brush_name)};
      if (alternate) textures.push_back(fixture::synthetic_external_texture("+ASWITCH"));
      builder.set_faces(faces).set_models(models).set_texinfo(texinfo).set_texture_directory(textures);
      builder.lump(fixture::SyntheticBspLumpId::lighting).assign(5U*5U*3U,std::byte{200});
    }
    if (world_light) {
      if (!brushes) {
        fixture::SyntheticBspFace face;
        face.light_styles = {0U,255U,255U,255U};
        face.light_offset = 0;
        builder.set_faces(std::span{&face, 1U});
      }
      builder.lump(fixture::SyntheticBspLumpId::lighting).assign(5U*5U*3U,
          static_cast<std::byte>(*world_light));
    }
    map = builder.build();
    fixture::SyntheticWad3Entry texture;
    texture.name="TEST_QUAD";
    texture.payload=fixture::synthetic_goldsrc_miptex("TEST_QUAD",64U,64U);
    fixture::SyntheticWad3Entry brush_texture;
    brush_texture.name = brush_name;
    brush_texture.payload = fixture::synthetic_goldsrc_miptex(brush_name,64U,64U);
    if (brush_name.starts_with('{')) {
      std::size_t offset = 40U;
      for (std::size_t level = 0; level < 4U; ++level) {
        const auto width = 64U >> level;
        for (std::size_t y = 0; y < width; ++y)
          for (std::size_t x = 0; x < width; ++x)
            brush_texture.payload[offset++] = x < width / 2U ? std::byte{255} : std::byte{50};
      }
    }
    std::vector<fixture::SyntheticWad3Entry> wad{std::move(texture),std::move(brush_texture)};
    if (alternate) {
      fixture::SyntheticWad3Entry alt;
      alt.name = "+ASWITCH"; alt.payload = fixture::synthetic_goldsrc_miptex(alt.name,64U,64U,100U);
      wad.push_back(std::move(alt));
    }
    root.write("valve","test.wad",fixture::synthetic_wad3(wad).bytes);
    root.write("valve", "maps/test_map.bsp", map);
    root.write("valve", "models/shared.mdl", model);
    root.write("valve", "sound/shared.wav", "sound");
  }
  auto resources(const bool mismatch = false,
                 std::string_view brush_reference = "*1") const {
    return readiness::parse_resource_list({
        {0U, "shared.wav", 7U, 5U, 0U},
        {2U, "maps/test_map.bsp", 9U, static_cast<std::uint32_t>(map.size()),
         0U},
        {2U, "models/shared.mdl", 7U,
         static_cast<std::uint32_t>(model.size()) + (mismatch ? 1U : 0U), 0U},
        {2U, "models/shared.mdl", 15U, static_cast<std::uint32_t>(model.size()),
         0U},
        {2U, "models/missing.mdl", 20U, 4U, 0U},
        {2U, std::string{brush_reference}, 27U, 0U, 0U},
    });
  }
  auto create(const bool mismatch = false) const {
    return app::RuntimeReplayLocalAssets::create(
        resources(mismatch), readiness::parse_server_info("maps/test_map.bsp"),
        root.path(), "valve");
  }
};

// Independent test oracle for CRC_MapFile: unlike a whole-file CRC, the raw
// accumulator visits BSP lumps 1..14 in directory order and excludes entities.
std::uint32_t fixture_map_crc(const std::span<const std::byte> map) {
  REQUIRE(map.size() >= fixture::kSyntheticBspHeaderSize);
  std::uint32_t crc = 0xffff'ffffU;
  for (std::size_t lump = 1U; lump < fixture::kSyntheticBspLumpCount; ++lump) {
    const auto offset = fixture::synthetic_read_u32le(map, 4U + lump * 8U);
    const auto length = fixture::synthetic_read_u32le(map, 8U + lump * 8U);
    REQUIRE(offset <= map.size());
    REQUIRE(length <= map.size() - offset);
    for (const auto byte : map.subspan(offset, length)) {
      crc ^= std::to_integer<std::uint8_t>(byte);
      for (unsigned bit = 0U; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb8'8320U : 0U);
    }
  }
  return crc; // GoldSrc's map checksum is not finalized with XOR FFFFFFFF.
}

void fixture_replace_map_entities(std::vector<std::byte>& map,
                                  const std::string_view entities) {
  const auto descriptor = fixture::synthetic_lump_descriptor_offset(
      fixture::SyntheticBspLumpId::entities);
  fixture::synthetic_write_u32le(map, descriptor,
      static_cast<std::uint32_t>(map.size()));
  fixture::synthetic_write_u32le(map, descriptor + 4U,
      static_cast<std::uint32_t>(entities.size()));
  const auto bytes = std::as_bytes(std::span{entities.data(), entities.size()});
  map.insert(map.end(), bytes.begin(), bytes.end());
}

hlclient::goldsrc::ServerInfoState fixture_server_map_crc(
    const std::uint32_t crc, const std::uint8_t slot = 0U,
    const std::uint8_t maximum_clients = 8U) {
  // Independently encode the four-byte COM_Munge3 value from serverinfo. This
  // fixture deliberately does not call the production decoder to create input.
  const std::uint32_t key = 255U - slot;
  const auto value = crc ^ ~key;
  const auto swapped = ((value & 0xffU) << 24U) |
      ((value & 0xff00U) << 8U) | ((value >> 8U) & 0xff00U) |
      ((value >> 24U) & 0xffU);
  const auto wire_crc = swapped ^ 0xffbf'a7a5U ^ key;
  std::vector<std::byte> body;
  readiness::append_u32_le(body, 48U);
  readiness::append_u32_le(body, 0x1234'5678U);
  readiness::append_u32_le(body, wire_crc);
  body.insert(body.end(), 16U, std::byte{0});
  body.push_back(static_cast<std::byte>(maximum_clients));
  body.push_back(static_cast<std::byte>(slot));
  body.push_back(static_cast<std::byte>(maximum_clients > 1U ? 1U : 0U));
  readiness::append_string(body, "valve");
  readiness::append_string(body, "Local resource CRC fixture");
  readiness::append_string(body, "maps/test_map.bsp");
  readiness::append_string(body, "cursor-only");
  body.push_back(std::byte{0});
  const auto parsed = hlclient::goldsrc::ServerInfoParser{}.parse(body);
  INFO((parsed.error ? parsed.error->context : std::string{}));
  REQUIRE(parsed);
  REQUIRE(parsed.state);
  REQUIRE(parsed.state->world_map_crc() == crc);
  return *parsed.state;
}

auto create_live_resource_fixture(const LocalContext& context,
                                 const hlclient::goldsrc::ResourceListState& resources,
                                 const hlclient::goldsrc::ServerInfoState& server) {
  return app::RuntimeReplayLocalAssets::create(resources, server,
      context.root.path(), "valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client,
      {}, {}, {}, {}, app::ReplayLocalResourcePolicy::live_standard_hl);
}

namespace {
std::vector<std::byte> muzzle_fixture_sprite() {
  namespace sf=fixture::sprite_fixture;
  auto bytes=sf::literal_header_and_palette(2,1,2,0,64,64);
  for(std::size_t i=0;i<256*3;++i)bytes[sf::kPaletteOffset+i]=std::byte{0};
  bytes[sf::kPaletteOffset+3]=std::byte{240};bytes[sf::kPaletteOffset+4]=std::byte{128};
  bytes[sf::kPaletteOffset+5]=std::byte{32};
  for(unsigned frame=0;frame<2;++frame) {
    sf::FrameSpec spec{-32,32,64,64,std::vector<std::byte>(64U*64U)};
    for(std::size_t y=20;y<44;++y)for(std::size_t x=20;x<44;++x)
      if(frame==0 || x<32)spec.pixels[y*64+x]=std::byte{1};
    sf::append_single_entry(bytes,spec);
  }
  return bytes;
}
app::ReplayLocalAssetsCreateResult create_muzzle_fixture(LocalContext& context) {
  context.root.write("valve","sprites/fixture_muzzle.spr",muzzle_fixture_sprite());
  const std::array<std::string,1> dependencies{"sprites/fixture_muzzle.spr"};
  return app::RuntimeReplayLocalAssets::create(context.resources(),readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::offline_spectator,{},dependencies);
}
app::RuntimeReplayLocalAssets::MuzzleSpriteFrame muzzle_request(hlclient::assets::AssetVector3 origin={0,0,0}) {
  app::RuntimeReplayLocalAssets::MuzzleSpriteFrame value;value.origin=origin;
  constexpr std::string_view name="sprites/fixture_muzzle.spr";
  std::copy(name.begin(),name.end(),value.sprite.virtual_name.begin());value.sprite.scale=.1F;
  return value;
}
}

TEST_CASE("Approved muzzle sprites preload exact source pixels with bounded optional dependencies",
          "[muzzle-sprites][local-capture-assets][no-stock]") {
  LocalContext context;auto created=create_muzzle_fixture(context);REQUIRE(created.projection);
  auto& resources=*created.projection;
  CHECK(resources.muzzle_sprite_statistics().ready==1);
  auto request=muzzle_request({8,0,0});
  const auto first=resources.materialize_muzzle_sprites(std::span{&request,1U},0);REQUIRE(first);
  REQUIRE(first->frame->sprite_instances().size()==1);
  const auto& instance=first->frame->sprite_instances()[0];
  CHECK(instance.transform.origin.x==8);CHECK(instance.transform.uniform_scale==Catch::Approx(.1F));
  CHECK(instance.blend_mode==hlclient::entity_render::SpriteBlendMode::additive);
  CHECK(instance.selected_frame_index==0);
  const auto& sprite=*first->package->sprite_assets()[instance.sprite_asset_index];
  REQUIRE(sprite.frames().size()==2);CHECK(sprite.frames()[0].geometry.source_origin.x==-32);
  const auto& pixels=sprite.frames()[0].rgba8;
  CHECK(pixels[0]==std::byte{0});CHECK(pixels[1]==std::byte{0});CHECK(pixels[3]==std::byte{255});
  const auto at=(32U*64U+32U)*4U;
  CHECK(pixels[at]==std::byte{240});CHECK(pixels[at+1]==std::byte{128});CHECK(pixels[at+2]==std::byte{32});
  CHECK(first->package->presentation_sprite_sources().size()==1);
  // Immutable prepared pixels do not reopen even if source disappears/changes.
  context.root.write("valve","sprites/fixture_muzzle.spr","invalid now");
  request.sprite.frame_seed=1;
  const auto next=resources.materialize_muzzle_sprites(std::span{&request,1U},.01);REQUIRE(next);
  CHECK(next->package==first->package);CHECK(next->frame->sprite_instances()[0].selected_frame_index==1);
  request.sprite.scale=std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(resources.materialize_muzzle_sprites(std::span{&request,1U},.01));
  CHECK_FALSE(resources.materialize_muzzle_sprites({},.01));
  const std::array<std::string,4> invalid{"../bad.spr","sprites/missing.spr","sprites/fixture_muzzle.spr","sprites/nested/a.spr"};
  auto failed=app::RuntimeReplayLocalAssets::create(context.resources(),readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::offline_spectator,{},invalid);
  REQUIRE(failed.projection);CHECK(failed.projection->muzzle_sprite_statistics().ready==0);
  CHECK(failed.projection->muzzle_sprite_statistics().missing==1);
  CHECK(failed.projection->muzzle_sprite_statistics().rejected==3);
  request=muzzle_request();CHECK_FALSE(failed.projection->materialize_muzzle_sprites(std::span{&request,1U},0));
  // Supplemental sources cannot bypass exact ownership validation.
  hlclient::entity_render::EntitySceneRenderPackageCreateInfo package;
  package.asset_library=first->package->asset_library();package.asset_library_identity=first->package->asset_library_identity();
  package.resource_id=first->package->resource_id();
  package.studio_assets.assign(first->package->studio_assets().begin(),first->package->studio_assets().end());
  package.sprite_assets.assign(first->package->sprite_assets().begin(),first->package->sprite_assets().end());
  CHECK_FALSE(hlclient::entity_render::EntitySceneRenderPackageBuilder{}.build(package));
  package.presentation_sprite_sources.assign(first->package->presentation_sprite_sources().begin(),first->package->presentation_sprite_sources().end());
  CHECK(hlclient::entity_render::EntitySceneRenderPackageBuilder{}.build(package));
  package.presentation_sprite_sources.push_back(package.presentation_sprite_sources.front());
  CHECK_FALSE(hlclient::entity_render::EntitySceneRenderPackageBuilder{}.build(package));
}

TEST_CASE("Original muzzle sprite GPU passes preserve black neutrality local projection world depth and frames",
          "[muzzle-sprites][local-capture-assets][actual-context]") {
  LocalContext context;auto created=create_muzzle_fixture(context);REQUIRE(created.projection);
  auto gl=fixture::entity_opengl_fixture::try_context();
  if(!gl || !fixture::entity_opengl_fixture::capable_context())SKIP("OpenGL 3.3 Core unavailable");
  gl->initialize_renderer();
  hlclient::renderer::RenderScene baseline;baseline.clear_color={.05F,.1F,.15F,1};
  baseline.camera={{0,0,0},{1,0,0},{0,0,1}};
  const auto draw=[&](const auto& scene) {
    gl->renderer().render(scene,{96,96});CHECK(glGetError()==GL_NO_ERROR);
    return fixture::entity_opengl_fixture::framebuffer();
  };
  const auto original=draw(baseline);auto request=muzzle_request({8,0,0});
  auto scene=baseline;scene.world_muzzle_entities=created.projection->materialize_muzzle_sprites(std::span{&request,1U},0);
  REQUIRE(scene.world_muzzle_entities);const auto visible=draw(scene);CHECK(visible!=original);
  const auto uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;
  CHECK(draw(scene)==visible);CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==uploads);
  const auto center=(48U*96U+48U)*4U;
  CHECK(std::to_integer<unsigned>(visible[center])>std::to_integer<unsigned>(original[center]));
  // Black source edges and outside pixels preserve underlying surface exactly.
  for(const auto pixel:{0U,(48U*96U+24U)*4U})for(unsigned c=0;c<3;++c)CHECK(visible[pixel+c]==original[pixel+c]);
  request.sprite.frame_seed=1;scene.world_muzzle_entities=created.projection->materialize_muzzle_sprites(std::span{&request,1U},.01);
  CHECK(draw(scene)!=visible);CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==uploads);
  namespace wf=fixture::world_render_fixture;
  auto wall=wf::make_world();
  for(auto& vertex:wall.vertices) {vertex.position={4,vertex.position.x-8,vertex.position.y-8};vertex.normal={-1,0,0};}
  wall.bounds={{4,-8,-8},{4,8,8}};wall.surfaces[0].bounds=wall.bounds;
  auto wall_package=hlclient::world_render::WorldRenderPackageBuilder{}.build(
      {std::move(wall),wf::make_texture_set(false)},wf::make_lightmap_set({}));REQUIRE(wall_package);
  auto blocked=baseline;blocked.static_world=hlclient::renderer::RenderStaticWorld{
      std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*wall_package.package))};
  const auto opaque=draw(blocked);blocked.world_muzzle_entities=scene.world_muzzle_entities;
  CHECK(draw(blocked)==opaque); // original sprite behind real world depth
  // A camera-local pass uses the existing first-person projection despite
  // arbitrary world camera origin/orientation, and doesn't become a HUD overlay.
  auto state=observation();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  hlclient::game_api::ViewmodelIntent intent;intent.status=hlclient::game_api::ViewmodelStatus::ready;
  intent.generation=1;intent.publication_revision=1;intent.model_index=7;
  scene=baseline;scene.camera={{100,100,100},{100,101,100},{0,0,1}};
  scene.first_person_entities=created.projection->materialize_viewmodel(intent);REQUIRE(scene.first_person_entities);
  const auto local_base=draw(scene);request=muzzle_request({8,0,4});
  scene.first_person_muzzle_entities=created.projection->materialize_muzzle_sprites(std::span{&request,1U},.02);
  REQUIRE(scene.first_person_muzzle_entities);const auto local_flash=draw(scene);CHECK(local_flash!=local_base);
  scene.camera={{-100,-100,-100},{-99,-100,-100},{0,0,1}};CHECK(draw(scene)==local_flash);
  scene.first_person_muzzle_entities.reset();CHECK(draw(scene)==local_base);
  GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);CHECK(depth==GL_TRUE);
  CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);
}

#if HLCLIENT_HAS_GAME_HALFLIFE
TEST_CASE("Original muzzle profile covers eligible firearms and remote owner retains dedup expiry lights",
          "[muzzle-sprites][weapon-visuals][remote-effects][no-stock]") {
  using namespace hlclient;
  for(const auto [marker,scale]:std::array<std::pair<unsigned,float>,4>{{{11,.1F},{20,.2F},{31,.3F},{41,.4F}}}) {
    const auto profile=games::halflife::muzzle_sprite_profile(marker,123);REQUIRE(profile);
    CHECK(profile->scale==Catch::Approx(scale));CHECK(profile->frame_seed==123);
    CHECK(std::string_view{profile->virtual_name.data()}==(marker==20 ? "sprites/muzzleflash1.spr":"sprites/muzzleflash2.spr"));
  }
  for(unsigned invalid:{0U,10U,99U,5001U})CHECK_FALSE(games::halflife::muzzle_sprite_profile(invalid,0));
  LocalContext context;auto created=create_muzzle_fixture(context);REQUIRE(created.projection);
  game_api::GameClientHost host{games::halflife::make_half_life_client_module()};host.reset({1,1,1});
  app::RemoteEffectPresentation owner;renderer::TransientVisuals shells;app::WorldImpactPresentation impacts;
  game_api::RemoteWeaponEffectsBatch batch;batch.count=1;auto& effect=batch.effects[0];
  effect.action.generation=1;effect.action.command_sequence=1;effect.action.emitter_entity=2;effect.action.started_at_seconds=1;
  effect.muzzle_origin={8,0,0};effect.flash=game_api::RemoteMuzzleFlash{{8,0,0},3,{1,1,1,1},1,1.075};
  effect.flash->sprite=muzzle_request().sprite;
  effect.light=game_api::LocalMuzzleLight{effect.action,96,.8F,{1,.62F,.28F},1,1.075};
  owner.consume(batch,host,shells,impacts,{},nullptr,{},false,false,1,created.projection.get());
  renderer::RenderScene scene;owner.present(scene,1.01,created.projection.get());
  REQUIRE(scene.world_muzzle_entities);CHECK(scene.world_muzzle_entities->frame->sprite_instances().size()==1);
  CHECK(scene.world_flash_count==0);REQUIRE(scene.transient_world_light);
  CHECK(scene.world_muzzle_entities->frame->sprite_instances()[0].transform.origin.x==8);
  owner.consume(batch,host,shells,impacts,{},nullptr,{},false,false,1.01,created.projection.get());
  renderer::RenderScene repeated;owner.present(repeated,1.02,created.projection.get());
  REQUIRE(repeated.world_muzzle_entities);CHECK(repeated.world_muzzle_entities->frame->sprite_instances().size()==1);
  CHECK(owner.statistics().consumed==1);CHECK(shells.statistics().shells_created==0);
  renderer::RenderScene expired;owner.present(expired,1.08,created.projection.get());
  CHECK_FALSE(expired.world_muzzle_entities);CHECK_FALSE(expired.transient_world_light);
  owner.reset();renderer::RenderScene reset;owner.present(reset,1.01,created.projection.get());CHECK_FALSE(reset.world_muzzle_entities);
  // Missing optional dependency remains a bounded skip, not the old generated
  // glow. Independent fire lighting still works in the normal resource path.
  auto missing=context.create();REQUIRE(missing.projection);
  owner.consume(batch,host,shells,impacts,{},nullptr,{},false,false,1,missing.projection.get());
  renderer::RenderScene absent;owner.present(absent,1.01,missing.projection.get());
  CHECK_FALSE(absent.world_muzzle_entities);CHECK(absent.world_flash_count==0);CHECK(absent.transient_world_light);
  CHECK(missing.projection->muzzle_sprite_statistics().unavailable==1);
}

TEST_CASE("Original installed muzzle sprites prepare through exact world root and reach GPU read-only",
          "[muzzle-sprites][muzzle-installed-assets][local-assets][actual-context]") {
  using namespace hlclient;
  const auto* root=SDL_getenv("HLCLIENT_LOCAL_GAME_ROOT");
  if(!root)SKIP("No opt-in local Half-Life root supplied");
  const std::filesystem::path base{root};
  const std::array<std::string,2> dependencies{"sprites/muzzleflash1.spr","sprites/muzzleflash2.spr"};
  std::array<std::filesystem::file_time_type,2> before;
  for(std::size_t i=0;i<dependencies.size();++i) {
    const auto path=base/"valve"/dependencies[i];
    if(!std::filesystem::is_regular_file(path))SKIP("Optional original muzzle sprite absent");
    before[i]=std::filesystem::last_write_time(path);
  }
  const auto resources=readiness::parse_resource_list({{2U,"maps/crossfire.bsp",9U,0U,0U}});
  auto prepared=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),
      base,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client,{},dependencies);
  REQUIRE(prepared.projection);CHECK(prepared.projection->muzzle_sprite_statistics().ready==2);
  CHECK(prepared.projection->muzzle_sprite_statistics().missing==0);
  CHECK(prepared.projection->muzzle_sprite_statistics().rejected==0);
  auto gl=fixture::entity_opengl_fixture::try_context();
  const bool gpu=gl && fixture::entity_opengl_fixture::capable_context();
  if(gpu)gl->initialize_renderer();else WARN("OpenGL unavailable: original approved CPU controls still checked");
  for(unsigned marker:{11U,20U,31U,41U}) {
    app::RuntimeReplayLocalAssets::MuzzleSpriteFrame request{{24,0,0},*games::halflife::muzzle_sprite_profile(marker,1)};
    const auto frame=prepared.projection->materialize_muzzle_sprites(std::span{&request,1U},0);REQUIRE(frame);
    const auto& instance=frame->frame->sprite_instances()[0];
    const auto& asset=*frame->package->sprite_assets()[instance.sprite_asset_index];
    CHECK(asset.frames().size()==2);CHECK(asset.render_profile()==entity_render::SpriteRenderTextureProfile::additive);
    CHECK(asset.orientation()==assets::SpriteOrientation::view_parallel);
    CHECK(asset.frames()[0].geometry.width==64);CHECK(asset.frames()[0].geometry.height==64);
    CHECK(std::ranges::any_of(asset.frames()[0].rgba8,[](std::byte v){return v!=std::byte{0};}));
    if(gpu) {
      renderer::RenderScene scene;scene.clear_color={0,0,0,1};scene.camera={{0,0,0},{1,0,0},{0,0,1}};
      gl->renderer().render(scene,{96,96});const auto baseline=fixture::entity_opengl_fixture::framebuffer();
      scene.world_muzzle_entities=frame;gl->renderer().render(scene,{96,96});
      const auto pixels=fixture::entity_opengl_fixture::framebuffer();CHECK(pixels!=baseline);
      const auto count=gl->renderer().entity_statistics().sprite_asset_upload_count;
      gl->renderer().render(scene,{96,96});CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==count);
      CHECK(glGetError()==GL_NO_ERROR);
      std::cout<<"muzzle_original_gpu marker="<<marker<<" scale="<<request.sprite.scale
          <<" frames="<<asset.frames().size()<<" changed="<<(pixels!=baseline)<<'\n';
    }
  }
  for(std::size_t i=0;i<dependencies.size();++i)CHECK(std::filesystem::last_write_time(base/"valve"/dependencies[i])==before[i]);
}
#endif

TEST_CASE("E31 missing resource report preserves safe virtual names and concrete map reason",
          "[e31][local-assets][no-stock]") {
  LocalContext context;
  auto ready=context.create();REQUIRE(ready.projection);
  const auto& issues=ready.projection->summary().resource_issues;
  const auto missing=std::ranges::find_if(issues,[](const auto& issue) {
    return issue.virtual_name=="models/missing.mdl";
  });
  REQUIRE(missing!=issues.end());CHECK(missing->reason=="missing");
  CHECK(missing->resource_index==20U);CHECK(missing->resource_type==2U);
  CHECK(issues.size()<=32U);
  for(const auto& issue:issues) {
    CHECK(issue.virtual_name.find(':')==std::string::npos);
    CHECK(issue.virtual_name.find('\\')==std::string::npos);
  }
  fixture::ScopedLocalResourceTestRoot empty;
  auto absent=app::RuntimeReplayLocalAssets::create(context.resources(),
      readiness::parse_server_info("maps/test_map.bsp"),empty.path(),"valve");
  REQUIRE_FALSE(absent.projection);REQUIRE(absent.error);
  CHECK(absent.error->context.find("map_resource_not_ready")!=std::string::npos);
  CHECK(absent.error->context.find("maps/test_map.bsp")!=std::string::npos);
  CHECK(absent.error->context.find("status=")!=std::string::npos);
  CHECK(absent.error->context.find(empty.path().string())==std::string::npos);
}

TEST_CASE("E33 cancelled asset work returns a typed empty result before touching roots",
          "[e33][loading][local-assets][no-stock]") {
  LocalContext context;
  auto work=std::make_shared<app::LoadingWork>(33U);
  work->stage.store(app::LoadingStage::idle);work->done.store(5U);work->total.store(9U);
  work->cancelled.store(true);
  const auto cancelled=app::RuntimeReplayLocalAssets::create(context.resources(),
      readiness::parse_server_info("maps/test_map.bsp"),{},"valve",
      app::ReplayLocalCameraPolicy::offline_spectator,{},{},{},work);
  REQUIRE_FALSE(cancelled.projection);REQUIRE(cancelled.error);
  CHECK(cancelled.error->code==app::RuntimeReplayVisualProjectionErrorCode::asset_preparation_cancelled);
  CHECK(cancelled.error->context=="asset_preparation_cancelled");
  CHECK(app::to_string(cancelled.error->code)=="asset_preparation_cancelled");
  CHECK(cancelled.resource_issues.empty());CHECK(cancelled.resource_issues_omitted==0U);
  CHECK(work->generation==33U);CHECK(work->stage.load()==app::LoadingStage::idle);
  CHECK(work->done.load()==5U);CHECK(work->total.load()==9U);
  app::LoadingPresentation presentation;presentation.begin(33U);
  CHECK_FALSE(presentation.observe(*work));CHECK(presentation.stage==app::LoadingStage::connecting);
}

TEST_CASE("E33 asset preparation publishes actual stages but never claims scene presentation ready",
          "[e33][loading][local-assets][no-stock]") {
  LocalContext context;
  auto work=std::make_shared<app::LoadingWork>(34U);
  const auto created=app::RuntimeReplayLocalAssets::create(context.resources(),
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve",
      app::ReplayLocalCameraPolicy::offline_spectator,{},{},{},work);
  REQUIRE(created.projection);CHECK_FALSE(created.error);
  CHECK(work->stage.load()==app::LoadingStage::scene);
  CHECK(work->total.load()==0U); // Scene/GPU totals are not fabricated.
  app::LoadingPresentation presentation;presentation.begin(34U);REQUIRE(presentation.observe(*work));
  CHECK(presentation.stage==app::LoadingStage::scene);CHECK_FALSE(presentation.percent);
  presentation.begin(35U);CHECK_FALSE(presentation.observe(*work));
  CHECK(presentation.stage==app::LoadingStage::connecting);
}

TEST_CASE("E33 failed resource texture and model preparation retain their real loading stage",
          "[e33][loading][local-assets][no-stock]") {
  LocalContext context;
  auto work=std::make_shared<app::LoadingWork>(36U);
  bool model_mismatch{};auto basedir=context.root.path();
  app::LoadingStage expected=app::LoadingStage::resources;
  fixture::ScopedLocalResourceTestRoot empty;
  SECTION("missing world is resource validation, not finished map import") {basedir=empty.path();}
  SECTION("malformed WAD fails during actual texture preparation") {
    context.root.write("valve","test.wad",std::array<std::byte,4>{});
    expected=app::LoadingStage::textures;
  }
  SECTION("model size mismatch fails during model preparation") {
    model_mismatch=true;expected=app::LoadingStage::models;
  }
  const auto resources=context.resources(model_mismatch);
  const auto failed=app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),basedir,"valve",
      app::ReplayLocalCameraPolicy::offline_spectator,{},{},{},work);
  REQUIRE_FALSE(failed.projection);REQUIRE(failed.error);
  CHECK(failed.error->code!=app::RuntimeReplayVisualProjectionErrorCode::asset_preparation_cancelled);
  CHECK(work->stage.load()==expected);CHECK(work->stage.load()!=app::LoadingStage::ready);
}

TEST_CASE("E31 failed world preparation preserves the actual missing WAD dependency",
          "[e31][local-assets][world-textures][no-stock]") {
  LocalContext source;
  fixture::ScopedLocalResourceTestRoot root;
  root.write("valve","maps/test_map.bsp",source.map);
  root.write("valve","models/shared.mdl",source.model);
  root.write("valve","sound/shared.wav","sound");
  auto failed=app::RuntimeReplayLocalAssets::create(source.resources(),
      readiness::parse_server_info("maps/test_map.bsp"),root.path(),"valve");
  REQUIRE_FALSE(failed.projection);REQUIRE(failed.error);
  const auto issue=std::ranges::find_if(failed.resource_issues,[](const auto& candidate) {
    return candidate.virtual_name=="test.wad" && candidate.reason=="wad_missing";
  });
  REQUIRE(issue!=failed.resource_issues.end());
  CHECK(issue->resource_type==255U);CHECK(issue->resource_index==0U);
  CHECK(failed.resource_issues.size()<=32U);
  CHECK(failed.error->context.find("incomplete")!=std::string::npos);
  for(const auto& candidate:failed.resource_issues) {
    CHECK(candidate.virtual_name.find(':')==std::string::npos);
    CHECK(candidate.virtual_name.find('\\')==std::string::npos);
  }
}

TEST_CASE("E31 malformed WAD reports its safe basename and typed catalog failure",
          "[e31][local-assets][world-textures][no-stock]") {
  LocalContext context;
  context.root.write("valve","test.wad",std::array<std::byte,4>{});
  auto failed=context.create();REQUIRE_FALSE(failed.projection);REQUIRE(failed.error);
  const auto issue=std::ranges::find_if(failed.resource_issues,[](const auto& candidate) {
    return candidate.virtual_name=="test.wad" && candidate.reason=="wad_catalog_failed";
  });
  REQUIRE(issue!=failed.resource_issues.end());
  CHECK(issue->resource_type==255U);CHECK(issue->resource_index==0U);
  CHECK(failed.resource_issues.size()<=32U);
}

TEST_CASE("E31 brush-only missing WAD keeps its own declaration identity on failure",
          "[e31][local-assets][world-textures][no-stock]") {
  LocalContext context{true,"BRUSH_ONLY",false,{},"test.wad;missing.wad"};
  fixture::SyntheticWad3Entry world_texture;
  world_texture.name="TEST_QUAD";
  world_texture.payload=fixture::synthetic_goldsrc_miptex("TEST_QUAD",64U,64U);
  context.root.write("valve","test.wad",fixture::synthetic_wad3({world_texture}).bytes);
  auto failed=context.create();REQUIRE_FALSE(failed.projection);REQUIRE(failed.error);
  CHECK(failed.error->context.starts_with("brush geometry preparation:"));
  const auto issue=std::ranges::find_if(failed.resource_issues,[](const auto& candidate) {
    return candidate.virtual_name=="missing.wad" && candidate.reason=="wad_missing";
  });
  REQUIRE(issue!=failed.resource_issues.end());
  CHECK(issue->resource_type==255U);CHECK(issue->resource_index==1U);
  CHECK(std::ranges::none_of(failed.resource_issues,[](const auto& candidate) {
    return candidate.virtual_name=="test.wad";
  }));
  CHECK(failed.resource_issues.size()<=32U);
}

// Independent neutral presentation only, deliberately no concrete game factory.
// It exercises the very same production host/materializer as normal hlclient.
class E10TestGameClientModule final : public hlclient::game_api::IGameClientModule {
public:
  using State = client::RuntimeClientObservationState;
  void reset(hlclient::game_api::GameSessionIdentity) noexcept override {}
  void teardown() noexcept override {}
  const hlclient::game_api::GameMovementPolicy& movement_policy() const noexcept override {
    return movement_;
  }
  hlclient::game_api::GameRecordResult stage_record(
      const hlclient::game_api::GameRecordInput&) const override {
    return {hlclient::game_api::GameRecordState{}, {}};
  }
  void commit_record(hlclient::game_api::GameRecordState&&) noexcept override {}
  void bind_model(std::optional<hlclient::game_api::LocalWeaponModelMetadata>) override {}
  void observe(const State&, double) override {}
  void submit(const hlclient::game_api::LocalWeaponSubmittedCommand&, double) override {}
  void cancel_uncommitted() noexcept override {}
  hlclient::game_api::LocalWeaponPresentationSnapshot sample(double) noexcept override { return {}; }
  hlclient::game_api::LocalAudioBatch drain_audio() noexcept override { return {}; }
  hlclient::game_api::ViewmodelIntent viewmodel(const State&,
      std::optional<hlclient::game_api::LocalWeaponModelMetadata>, double,
      std::optional<hlclient::game_api::LocalWeaponVisual>) override { return {}; }
  hlclient::game_api::HudState hud(const State&, double) override { return {}; }
  hlclient::game_api::CameraIntent camera(const State&, double) const noexcept override { return {}; }
  void observe_action_evidence(const State&,
      const std::optional<hlclient::game_api::GameActionTraffic>&) noexcept override {}
  hlclient::game_api::GameActionEvidenceSnapshot action_evidence() const noexcept override { return {}; }
  std::optional<hlclient::game_api::GameCommandRequest> inventory_selection(
      const State&, std::uint8_t) const override { return {}; }
  std::optional<std::uint8_t> select_group(const State&, std::uint8_t,
      std::optional<std::uint8_t>) const override { return {}; }
  std::optional<std::uint8_t> cycle_inventory(const State&,
      std::optional<std::uint8_t>, int) const override { return {}; }
  hlclient::game_api::GameCommandRequest self_kill_request() const override { return {}; }
  std::optional<std::uint8_t> scenario_inventory_target(const State&, std::size_t) const override { return {}; }
  hlclient::game_api::GameScenarioDirective scenario_command(hlclient::game_api::GameScenario,
      const State*, std::size_t, double, double) noexcept override { return {}; }
  hlclient::game_api::DamageRespawnScriptSnapshot damage_respawn_snapshot() const noexcept override { return {}; }
  hlclient::game_api::RemotePlayerPresentationPolicy remote_player_policy() const noexcept override {
    return {true, 0.1, 0.2, 128.0, maximum_players, 0U};
  }
  hlclient::game_api::RemotePlayerPresentationIntent remote_player(
      const hlclient::game_api::RemotePlayerPresentationContext& input) noexcept override {
    ++requests;
    previous_server_seconds = input.previous_server_seconds;
    current_server_seconds = input.current_server_seconds;
    hlclient::game_api::RemotePlayerPresentationIntent result;
    result.status = hlclient::game_api::RemotePlayerPresentationStatus::ready;
    result.sample.sequence = 0U;
    result.sample.body = selected_body;
    result.transform_angles = {input.entity.angles.x.value_or(0.0),
        input.entity.angles.y.value_or(0.0), input.entity.angles.z.value_or(0.0)};
    result.bone_count = input.model.bones.size();
    if(attachments && input.entity.weapon_model_index && *input.entity.weapon_model_index)
      result.attachment_model_index=input.entity.weapon_model_index;
    return result;
  }
  std::int32_t selected_body{1};
  std::uint32_t maximum_players{8};
  bool attachments{};
  bool sockets_enabled{};
  std::array<hlclient::game_api::RemoteWeaponPoseSample,32> sockets{};
  std::size_t socket_count{};
  hlclient::game_api::RemoteWeaponSocketPlan remote_weapon_socket_plan(
      const hlclient::game_api::RemoteWeaponModelContext&) noexcept override {
    if(!sockets_enabled) return {};
    // Independent project-owned policy, deliberately not a Half-Life socket.
    hlclient::game_api::RemoteWeaponSocketPlan result;
    result.semantic_match=true;
    result.source=hlclient::game_api::RemoteSocketSource::validated_pmodel_socket;
    result.muzzle={0,{0,3,4}};result.shell={0,{0,-3,4}};
    return result;
  }
  void remote_weapon_poses(std::span<const hlclient::game_api::RemoteWeaponPoseSample> input) noexcept override {
    socket_count=std::min(input.size(),sockets.size());
    std::copy_n(input.begin(),socket_count,sockets.begin());
  }
  std::size_t requests{};
  double previous_server_seconds{};
  double current_server_seconds{};
private:
  hlclient::game_api::GameMovementPolicy movement_;
};

[[nodiscard]] client::RuntimeClientObservationState e10_observation(
    const std::uint64_t record, const double server_seconds,
    const double remote_x, const double ordinary_x, const double brush_z,
    const bool include_removed_player = true) {
  auto state = observation();
  state.publication_revision = record;
  state.entity_metadata.source = client::RuntimeObservationSource{
      record, static_cast<std::size_t>(record),
      static_cast<std::uint32_t>(record + 42U), 0U, 8U};
  state.server_time_seconds = server_seconds;
  state.server_time_metadata = state.entity_metadata;
  state.packet_entities.clear();
  for (const auto number : {1U, 2U, 3U, 40U, 80U}) {
    if (number == 3U && !include_removed_player) continue;
    client::RuntimePacketEntityObservation entity;
    entity.entity_number = number;
    entity.ordinary_visual_schema = true;
    entity.player_movement_schema = number <= 3U;
    entity.model_index = number == 80U ? 27U : 7U;
    entity.origin = {number == 2U ? remote_x : number == 40U ? ordinary_x : 0.0,
        0.0, number == 80U ? brush_z : 0.0};
    entity.angles = {0.0, 0.0, 0.0};
    entity.sequence = 0U;
    entity.frame = 0.0;
    entity.body = 0U;
    entity.skin = 0;
    state.packet_entities.push_back(entity);
  }
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  return state;
}

[[nodiscard]] const hlclient::entity_render::StudioEntityRenderInstance&
e10_instance(const client::ClientWorldState& world, const std::uint32_t number) {
  REQUIRE(world.entity_frame());
  const auto instances = world.entity_frame()->studio_instances();
  const auto found = std::ranges::find(instances, number,
      &hlclient::entity_render::StudioEntityRenderInstance::entity_number);
  REQUIRE(found != instances.end());
  return *found;
}

[[nodiscard]] app::RuntimeReplayVisualProjectionResult e10_present(
    app::RuntimeReplayLocalAssets& assets, hlclient::game_api::GameClientHost& host,
    const double application_seconds, const std::optional<std::uint32_t> receiving,
    client::ClientWorldState& world, const hlclient::renderer::RenderExtent extent) {
  const auto result = assets.present_entities(host, application_seconds, receiving, world, extent);
  INFO("application-time=" << application_seconds << " presentation-error=" <<
      (result.error ? result.error->context : std::string{}));
  REQUIRE(result);
  return result;
}

TEST_CASE("Opt-in ordinary installed NPC presents signed animation between unchanged raw snapshots",
          "[studio][animation][installed-npc-production][local-capture-assets][runtime-presentation]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value=nullptr;std::size_t count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;
  std::free(value);
#else
  if(const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT"))root=value;
#endif
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("No exact opt-in read-only research root for ordinary Studio control");
  const std::array tokens{"maps/crossfire.bsp","models/tentacle2.mdl"};
  std::array<std::filesystem::file_time_type,tokens.size()> times;
  for(std::size_t i=0;i<tokens.size();++i) {
    if(!std::filesystem::is_regular_file(root/"valve"/tokens[i]))SKIP("Optional approved NPC asset absent");
    times[i]=std::filesystem::last_write_time(root/"valve"/tokens[i]);
  }
  const auto resources=readiness::parse_resource_list({{2U,tokens[0],9U,0U,0U},{2U,tokens[1],7U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info(tokens[0]),root,"valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : std::string{}));REQUIRE(created.projection);
  const auto model=created.projection->studio_model_data(7U);REQUIRE(model);
  namespace studio=hlclient::goldsrc::studio;
  using Palette=std::vector<std::array<float,16U>>;
  // Independent SDK frame formula, sampled through the imported real model.
  const auto expected=[&](std::uint32_t sequence,double frame)->std::optional<Palette> {
    studio::StudioPoseInput input;input.sequence_index=sequence;input.frame_coordinate=frame;
    input.compatibility_profile=studio::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
    const auto pose=studio::StudioPoseEvaluator{}.evaluate({"installed_npc_control",{31U,47U}},*model,input);
    if(!pose)return {};
    Palette result;
    for(const auto& bone:pose.pose->world_bones()) {
      const auto& m=bone.transform.values;
      result.push_back({m[0],m[4],m[8],0,m[1],m[5],m[9],0,m[2],m[6],m[10],0,m[3],m[7],m[11],1});
    }
    return result;
  };
  const auto distance=[](const Palette& a,const Palette& b) {
    REQUIRE(a.size()==b.size());double maximum=0.0;
    for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<a[i].size();++j)
      maximum=std::max(maximum,std::abs(static_cast<double>(a[i][j])-b[i][j]));
    return maximum;
  };
  std::optional<std::uint32_t> selected;
  for(std::uint32_t i=0;i<model->sequences.size();++i) {
    const auto& sequence=model->sequences[i];const double fps=sequence.frames_per_second;
    if(sequence.frame_count<4U || !std::isfinite(fps) || fps<=0.0 || sequence.frame_count-1.0<=fps*.4)continue;
    const double center=(sequence.frame_count-1.0)*.5;
    const auto base=expected(i,center),forward=expected(i,center+fps*.04),reverse=expected(i,center-fps*.04);
    if(base && forward && reverse && distance(*base,*forward)>1e-4 && distance(*base,*reverse)>1e-4) {selected=i;break;}
  }
  REQUIRE(selected);const auto& sequence=model->sequences[*selected];
  const double last=sequence.frame_count-1.0,fps=sequence.frames_per_second;
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};host.reset({1,1,1});
  for(const double rate:{1.0,-1.0}) {
    INFO("signed-rate=" << rate << " sequence=" << *selected);
    client::ClientWorldState world;auto first=e10_observation(1U,1.0,0,0,0,false);
    first.packet_entities.erase(std::remove_if(first.packet_entities.begin(),first.packet_entities.end(),
        [](const auto& entity){return entity.entity_number!=40U;}),first.packet_entities.end());
    REQUIRE(first.packet_entities.size()==1U);auto& npc=first.packet_entities[0];npc.brush_move_type=5U;
    npc.sequence=*selected;npc.frame=128.0;npc.animation_time_seconds=1.0;npc.frame_rate=rate;
    npc.controllers.fill(0U);npc.blending.fill(0U);npc.effects=0U;
    first.canonical_state_hash=client::runtime_observation_canonical_hash(first);
    REQUIRE(created.projection->reset_generation(first,world));
    auto second=first;second.publication_revision=2U;
    second.entity_metadata.source=client::RuntimeObservationSource{2U,2U,44U,0U,8U};
    second.server_time_seconds=1.1;second.server_time_metadata=second.entity_metadata;
    second.packet_entities[0].animation_time_seconds=1.1;
    second.packet_entities[0].frame=128.0+rate*fps*.1*256.0/last;
    second.canonical_state_hash=client::runtime_observation_canonical_hash(second);
    REQUIRE(created.projection->project_entities(second,world));const auto retained=second;
    Palette initial;
    for(const double elapsed:{0.0,.04,.08}) {
      REQUIRE(e10_present(*created.projection,host,100.0+elapsed,1U,world,{320,240}));
      const auto& instance=e10_instance(world,40U);
      const auto& actual=world.entity_frame()->studio_poses()[instance.pose_index].bone_matrices;
      const auto oracle=expected(*selected,last*.5+rate*fps*elapsed);REQUIRE(oracle);REQUIRE(actual.size()==oracle->size());
      for(std::size_t i=0;i<actual.size();++i)for(std::size_t j=0;j<actual[i].size();++j)
        CHECK(actual[i][j]==Catch::Approx((*oracle)[i][j]).margin(1e-4F));
      if(elapsed==0.0)initial=actual;else CHECK(distance(actual,initial)>1e-4);
      CHECK(second.canonical_state_hash==retained.canonical_state_hash);CHECK(second.packet_entities==retained.packet_entities);
    }
  }
  CHECK(host.drain_audio().count==0U);host.teardown();
  for(std::size_t i=0;i<tokens.size();++i)CHECK(times[i]==std::filesystem::last_write_time(root/"valve"/tokens[i]));
}

// Owned warm pipeline, not a replay checkpoint for the historical frame7270.
// Packet preparation, resource import, assertions, fingerprints and encoding
// are outside measured scopes. No socket, Steam, renderer or installed asset.
[[nodiscard]] hlclient::goldsrc::ServerInfoState b080_server_info(std::uint32_t actors) {
  std::vector<std::byte> body;
  readiness::append_u32_le(body,48);readiness::append_u32_le(body,1);readiness::append_u32_le(body,2);
  body.insert(body.end(),16,std::byte{});
  body.push_back(static_cast<std::byte>(actors));body.push_back(std::byte{});body.push_back(std::byte{1});
  for(const auto text:{"valve","Owned B080 CPU workload","maps/test_map.bsp","owned"})
    readiness::append_string(body,text);
  body.push_back(std::byte{});
  auto parsed=hlclient::goldsrc::ServerInfoParser{}.parse(body);REQUIRE(parsed);REQUIRE(parsed.state);
  return std::move(*parsed.state);
}

[[nodiscard]] hlclient::goldsrc::RuntimeReplayRecord b080_record(
    std::uint32_t actors,std::uint32_t ordinal) {
  namespace pf=hlclient::test::player_origin_fixture;
  hlclient::test::delta_fixture::BitWriter writer;
  writer.write(7,8);writer.write(std::bit_cast<std::uint32_t>(1.F+float(ordinal)*.005F),32);
  pf::client_data(writer,{double(ordinal)*.125,0,0});
  writer.write(40,8);writer.write(actors+64U,16);
  for(std::uint32_t entity=1;entity<=actors+64U;++entity) {
    writer.write(1,1);writer.write(0,1);writer.write(0,1); // sequential, ordinary schema, no intra base
    writer.write(1,3);writer.write(255,8);
    pf::coordinate(writer,double(entity)+double(ordinal)*.125,8,18);
    pf::coordinate(writer,double(entity%8U)*2,8,18);pf::coordinate(writer,0,8,18);
    writer.write(0,16);writer.write(0,16);writer.write(0,16);writer.write(7,10);writer.write(0,8);
  }
  writer.write(0,16);writer.align_zero();
  hlclient::goldsrc::RuntimeReplayRecord record;
  record.generation=1;record.record_identity=record.record_ordinal=ordinal;
  record.payload=pf::payload(writer.bytes(),1000U+ordinal);
  record.received_at=std::chrono::steady_clock::time_point{std::chrono::milliseconds{ordinal*5U}};
  record.initial_cursor=*hlclient::goldsrc::StockRuntimeSourceCursor::create(0,0,record.payload.bytes.size());
  return record;
}

struct B080Cost final {
  std::array<std::vector<double>,5> durations;
  std::uint64_t signature{14695981039346656037ULL},events{};
  std::size_t history_entities{},history_value_bytes{},capture_bytes{},encoded_bytes{};
  std::size_t ordinary_pose_evaluations{},ordinary_pose_reuses{};
  std::size_t projected_frames{};
  void integer(std::uint64_t value) {
    for(unsigned byte=0;byte<8;++byte) {signature^=(value>>(byte*8U))&255U;signature*=1099511628211ULL;}
  }
  void real(float value) {integer(std::bit_cast<std::uint32_t>(value));}
};

[[nodiscard]] B080Cost b080_warm_pipeline(LocalContext& context,std::uint32_t actors,bool capture,
    bool retain_only=false) {
  namespace g=hlclient::goldsrc;
  namespace pf=hlclient::test::player_origin_fixture;
  using Clock=std::chrono::steady_clock;
  constexpr std::uint32_t warm=80,samples=256,total=warm+samples;
  std::vector<g::RuntimeReplayRecord> records;records.reserve(total);
  for(std::uint32_t ordinal=1;ordinal<=total;++ordinal)records.push_back(b080_record(actors,ordinal));
  g::RuntimeReplayInitialization initialization;
  initialization.generation=1;initialization.max_clients=actors;initialization.receiving_player_entity=1;
  initialization.schemas=pf::schemas();initialization.baselines=pf::baselines(*initialization.schemas);
  client::ClientWorldState authoritative,visual_world;
  auto session=g::RuntimeReplaySession::initialize(initialization,authoritative);REQUIRE(session);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shared.mdl",7U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"sprites/b080_trail.spr",19U,0U,0U}});
  auto assets=app::RuntimeReplayLocalAssets::create(resources,b080_server_info(actors),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(assets.projection);
  auto module=std::make_unique<E10TestGameClientModule>();module->maximum_players=actors;
  hlclient::game_api::GameClientHost host{std::move(module)};host.reset({1,1,1});
  visual_world.set_camera({{0,-100,40},{10,0,0},{0,0,1}});
  app::MovementIncidentRecorder recorder{capture};B080Cost cost;
  for(auto& values:cost.durations)values.reserve(samples);
  bool initialized=false;
  for(const auto& record:records) {
    const auto ordinal=static_cast<std::uint32_t>(record.record_ordinal);
    const double seconds=double(ordinal)*.005;
    app::MovementFrameRecord frame;frame.frame_number=ordinal;frame.monotonic_us=std::uint64_t(ordinal)*5000;
    frame.network_generation=frame.map_generation=1;frame.frame_interval_ms=5;frame.entities=actors+64U;
    const auto begin=Clock::now();
    auto applied=session.session->apply_record(record);
    const auto parsed=Clock::now();
    if(!applied) {INFO((applied.error ? applied.error->context : "missing apply result"));REQUIRE(applied);}
    const auto& observation=*authoritative.runtime_observation();
    auto projected=initialized
        ? retain_only ? assets.projection->observe_entities(observation)
                      : assets.projection->project_entities(observation,visual_world)
        : assets.projection->reset_generation(observation,visual_world);
    const auto committed=Clock::now();
    if(!projected) {INFO((projected.error ? projected.error->context : "missing projection"));REQUIRE(projected);}
    auto presented=assets.projection->present_entities(host,seconds,1,visual_world,{640,480},capture);
    const auto materialized=Clock::now();
    if(!presented) {INFO((presented.error ? presented.error->context : "missing presentation"));REQUIRE(presented);}
    frame.parse_commit_ms=std::chrono::duration<double,std::milli>(parsed-begin).count();
    frame.studio_ms=std::chrono::duration<double,std::milli>(materialized-committed).count();
    static_cast<void>(recorder.observe(frame));
    if(capture) {
      const auto remote=assets.projection->remote_movement_observations();
      if(remote)static_cast<void>(recorder.observe_remote(frame,*remote));
    }
    const auto ended=Clock::now();
    // Populate real, evolving follow history outside timings; subsequent timed
    // projections must preserve it, including presentation-only samples.
    if(!initialized) {
      client::CommittedTemporaryEffect follow;follow.generation=1;follow.record=follow.record_ordinal=1;
      follow.received_at_seconds=seconds;follow.kind=client::TemporaryEffectKind::beam_follow;
      follow.entity_attachment=static_cast<std::uint16_t>(actors+64U);follow.sprite_index=19;
      follow.life_tenths=40;follow.width_tenths=5;follow.color={224,224,255};follow.brightness=255;
      assets.projection->consume_follow(follow,seconds);initialized=true;
    }
    hlclient::renderer::RenderScene effects;assets.projection->present_entity_effects(effects,seconds);
    REQUIRE(applied.event);cost.events+=applied.event->decoded_batch.events.size();
    REQUIRE(observation.packet_entities.size()==actors+64U);
    CHECK(observation.canonical_state_hash==client::runtime_observation_canonical_hash(observation));
    CHECK(observation.packet_entities.back().origin.x==double(actors+64U)+double(ordinal)*.125);
    REQUIRE(observation.receiving_client);CHECK(observation.receiving_client->origin.x==double(ordinal)*.125);
    REQUIRE(visual_world.entity_frame());
    const auto& output=*visual_world.entity_frame();
    REQUIRE(output.studio_instances().size()==actors+63U);
    cost.integer(observation.canonical_state_hash);cost.integer(output.studio_poses().size());
    for(const auto& instance:output.studio_instances()) {
      cost.integer(instance.entity_number);cost.integer(instance.studio_asset_index);cost.integer(instance.pose_index);
      cost.integer(instance.body_value);cost.integer(instance.skin_family_index);cost.integer(static_cast<unsigned>(instance.visibility_status));
      cost.real(instance.transform.origin.x);cost.real(instance.transform.origin.y);cost.real(instance.transform.origin.z);
      cost.real(instance.transform.rotation_degrees.x);cost.real(instance.transform.rotation_degrees.y);cost.real(instance.transform.rotation_degrees.z);
      cost.real(instance.transform.uniform_scale);cost.integer(static_cast<unsigned>(instance.material_support_status));
      for(const auto point:{instance.interpolated_bounds.minimum,instance.interpolated_bounds.maximum}) {
        cost.real(point.x);cost.real(point.y);cost.real(point.z);
      }
      for(const auto light:instance.static_light_rgb.value_or(std::array<float,3>{-1,-1,-1}))cost.real(light);
    }
    for(const auto& pose:output.studio_poses())for(const auto& matrix:pose.bone_matrices)
      for(const auto value:matrix)cost.real(value);
    const auto& trail=assets.projection->entity_effect_statistics();
    cost.integer(trail.received);cost.integer(trail.accepted);cost.integer(trail.duplicate);
    cost.integer(trail.detached);cost.integer(trail.discontinuity);cost.integer(trail.sampled);
    for(const auto& lifetime:assets.projection->server_decal_lifetimes()) {
      cost.integer(lifetime.entity_number);cost.integer(lifetime.lifetime);
      cost.integer(lifetime.model_slot);cost.integer(lifetime.source_model_index);
    }
    if(ordinal>warm) {
      REQUIRE(effects.follow_trail_entities);
      for(const auto& sprite:effects.follow_trail_entities->frame->sprite_instances()) {
        cost.integer(sprite.entity_number);cost.integer(sprite.sprite_asset_index);cost.integer(sprite.selected_frame_index);
        cost.real(sprite.transform.origin.x);cost.real(sprite.transform.origin.y);cost.real(sprite.transform.origin.z);
        cost.real(sprite.transform.uniform_scale);cost.real(sprite.opacity);cost.real(sprite.ribbon_width);
        for(const auto color:sprite.tint)cost.real(color);
        cost.integer(sprite.ribbon_end.has_value());
        if(sprite.ribbon_end) {cost.real(sprite.ribbon_end->x);cost.real(sprite.ribbon_end->y);cost.real(sprite.ribbon_end->z);}
      }
    }
    if(ordinal>warm) {
      const std::array points{begin,parsed,committed,materialized,ended};
      for(std::size_t scope=0;scope<4;++scope)
        cost.durations[scope].push_back(std::chrono::duration<double,std::micro>(points[scope+1]-points[scope]).count());
      cost.durations[4].push_back(std::chrono::duration<double,std::micro>(ended-begin).count());
    }
  }
  const auto& history=session.session->decoder_state().history();
  cost.ordinary_pose_evaluations=assets.projection->summary().ordinary_pose_evaluations;
  cost.ordinary_pose_reuses=assets.projection->summary().ordinary_pose_reuses;
  cost.projected_frames=assets.projection->summary().projected_frames;
  REQUIRE(history.snapshot_count()==g::kDefaultMaximumEntitySnapshotHistory);
  CHECK(history.oldest_reference()->value()==1000U+total-63U);CHECK(history.newest_reference()->value()==1000U+total);
  for(const auto& snapshot:history.snapshots())cost.history_entities+=snapshot.entity_count();
  cost.history_value_bytes=history.accounted_value_bytes();
  CHECK(assets.projection->entity_effect_statistics().accepted==1);
  CHECK(assets.projection->entity_effect_statistics().sampled>10);
  // Rejected packet tail and forged observation must not partially publish.
  const auto prior=authoritative.runtime_observation();const auto visual_prior=visual_world.entity_frame();
  auto malformed=b080_record(actors,total+1U);malformed.payload.bytes.push_back(std::byte{254});
  REQUIRE_FALSE(session.session->apply_record(malformed));CHECK(authoritative.runtime_observation()==prior);
  auto invalid=*prior;invalid.packet_entities.back().origin.x=std::numeric_limits<double>::quiet_NaN();
  const auto summary_prior=assets.projection->summary();
  const auto effects_prior=assets.projection->entity_effect_statistics();
  const std::vector lifetimes_prior(assets.projection->server_decal_lifetimes().begin(),
      assets.projection->server_decal_lifetimes().end());
  const auto invalid_projection=retain_only
      ? assets.projection->observe_entities(invalid)
      : assets.projection->project_entities(invalid,visual_world);
  REQUIRE_FALSE(invalid_projection);CHECK(visual_world.entity_frame()==visual_prior);
  CHECK(assets.projection->summary().projected_frames==summary_prior.projected_frames);
  CHECK(assets.projection->entity_effect_statistics().received==effects_prior.received);
  const auto lifetimes_after=assets.projection->server_decal_lifetimes();
  REQUIRE(lifetimes_after.size()==lifetimes_prior.size());
  for(std::size_t i=0;i<lifetimes_prior.size();++i) {
    CHECK(lifetimes_after[i].entity_number==lifetimes_prior[i].entity_number);
    CHECK(lifetimes_after[i].lifetime==lifetimes_prior[i].lifetime);
    CHECK(lifetimes_after[i].model_slot==lifetimes_prior[i].model_slot);
    CHECK(lifetimes_after[i].source_model_index==lifetimes_prior[i].source_model_index);
  }
  static_cast<void>(recorder.mark(app::MovementIncidentTrigger::manual,std::uint64_t(total)*5000));
  const auto stats=recorder.statistics();cost.capture_bytes=stats.total_capacity_bytes;
  const auto encoded=recorder.encode({"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,123,456});
  if(capture) {REQUIRE(encoded);cost.encoded_bytes=encoded->size();}
  return cost;
}

TEST_CASE("B080 owned warmed decode commit and Studio paths retain exact output with diagnostics On Off",
    "[b080-cpu-workload][benchmark][runtime-presentation][local-capture-assets][no-stock]") {
  constexpr std::array<std::string_view,5> names{"parse_commit","authoritative_materialization", "sampled_materialization","capture_collection","owned_update_subset"};
  LocalContext context;
  context.root.write("valve","sprites/b080_trail.spr",fixture::sprite_fixture::literal_single_sprite(1));
  for(const auto actors:{2U,4U,8U,16U,32U}) {
    std::array<B080Cost,2> modes;
    for(unsigned order=0;order<2;++order) {
      const auto mode=(order+(actors==16U ? 1U : 0U))%2U;
      modes[mode]=b080_warm_pipeline(context,actors,mode!=0);
    }
    CHECK(modes[0].signature==modes[1].signature);CHECK(modes[0].events==modes[1].events);
    for(unsigned mode=0;mode<2;++mode)for(std::size_t scope=0;scope<names.size();++scope) {
      auto& values=modes[mode].durations[scope];std::sort(values.begin(),values.end());REQUIRE(values.size()==256);
      for(const auto value:values)CHECK(std::isfinite(value));
      std::cout<<"[b080-cpu-workload] {\"actors\":"<<actors<<",\"ordinary_entities\":64,\"capture\":"<<(mode ? "true" : "false")
        <<",\"scope\":\""<<names[scope]<<"\",\"samples\":256,\"p50_us\":"<<values[127]<<",\"p95_us\":"<<values[243]
        <<",\"p99_us\":"<<values[253]<<",\"max_us\":"<<values.back()<<",\"output_signature\":\""<<modes[mode].signature
        <<"\",\"events\":"<<modes[mode].events<<",\"retained_history_entities\":"<<modes[mode].history_entities
        <<",\"retained_history_value_bytes\":"<<modes[mode].history_value_bytes<<",\"follow_fixed_bytes\":"<<sizeof(app::EntityFollowPresentation)
        <<",\"capture_capacity_bytes\":"<<modes[mode].capture_bytes<<",\"encoded_bytes\":"<<modes[mode].encoded_bytes
        <<",\"ordinary_pose_evaluations\":"<<modes[mode].ordinary_pose_evaluations<<",\"ordinary_pose_reuses\":"<<modes[mode].ordinary_pose_reuses<<"}\n";
    }
  }
}

TEST_CASE("B080 retain-only observation preserves authoritative presentation while avoiding a throwaway frame",
    "[b080-cpu-workload][runtime-presentation][local-capture-assets][no-stock]") {
  LocalContext context;
  context.root.write("valve","sprites/b080_trail.spr",
      fixture::sprite_fixture::literal_single_sprite(1));

  const auto projected=b080_warm_pipeline(context,2U,false,false);
  const auto observed=b080_warm_pipeline(context,2U,false,true);

  // The retained histories, lifetime-derived decal ownership, follow output,
  // poses and transforms must be byte-for-byte equivalent at presentation.
  CHECK(observed.signature==projected.signature);
  CHECK(observed.events==projected.events);
  CHECK(observed.history_entities==projected.history_entities);
  CHECK(observed.history_value_bytes==projected.history_value_bytes);
  CHECK(observed.ordinary_pose_evaluations==projected.ordinary_pose_evaluations);
  CHECK(observed.ordinary_pose_reuses<=projected.ordinary_pose_reuses);

  // Record one is the shared reset frame. Every later authoritative record is
  // retained without publishing the discrete frame that present_entities
  // would immediately replace.
  CHECK(projected.projected_frames-observed.projected_frames==335U);
}

TEST_CASE("E32 opt-in actor diagnostics join actual presentation without changing it",
    "[e32-remote-diagnostics][e10][runtime-presentation][local-capture-assets]") {
  LocalContext context;
  auto on=context.create(),off=context.create();
  REQUIRE(on.projection);REQUIRE(off.projection);
  hlclient::game_api::GameClientHost on_host{std::make_unique<E10TestGameClientModule>()};
  hlclient::game_api::GameClientHost off_host{std::make_unique<E10TestGameClientModule>()};
  on_host.reset({1,1,1});off_host.reset({1,1,1});
  client::ClientWorldState on_world,off_world;
  app::MovementIncidentRecorder receipt{true};
  std::uint64_t frame_number{};
  const auto first=e10_observation(1,1.0,0,40,0);
  const auto second=e10_observation(2,1.02,20,140,0);
  REQUIRE(on.projection->reset_generation(first,on_world));
  REQUIRE(off.projection->reset_generation(first,off_world));
  REQUIRE(on.projection->project_entities(second,on_world));
  REQUIRE(off.projection->project_entities(second,off_world));
  const auto captured=[&](std::uint32_t number) {
    const auto rows=on.projection->remote_movement_observations();REQUIRE(rows);
    const auto found=std::ranges::find(*rows,number,&app::RemoteMovementObservation::entity_index);
    REQUIRE(found!=rows->end());return *found;
  };
  for(const auto time:{100.0,100.005}) {
    REQUIRE(on.projection->present_entities(on_host,time,1,on_world,{640,480},true));
    REQUIRE(off.projection->present_entities(off_host,time,1,off_world,{640,480}));
    CHECK_FALSE(off.projection->remote_movement_observations());
    const auto& a=e10_instance(on_world,2);const auto& b=e10_instance(off_world,2);
    CHECK(a.transform.origin.x==b.transform.origin.x);
    CHECK(on_world.entity_frame()->studio_poses()[a.pose_index].bone_matrices==
        off_world.entity_frame()->studio_poses()[b.pose_index].bone_matrices);
    // This asset-materialization fixture does not publish a network world
    // observation. Preserve that absence instead of inventing an RX update.
    CHECK_FALSE(on_world.runtime_observation());CHECK_FALSE(off_world.runtime_observation());
    CHECK(client::runtime_observation_canonical_hash(second)==second.canonical_state_hash);
    const auto row=captured(2);
    REQUIRE(row.previous_raw_origin);REQUIRE(row.current_raw_origin);REQUIRE(row.latest_raw_origin);
    REQUIRE(row.sampled_origin);REQUIRE(row.rendered_origin);
    CHECK((*row.previous_raw_origin)[0]==0);CHECK((*row.current_raw_origin)[0]==20);
    CHECK((*row.latest_raw_origin)[0]==20);
    CHECK((*row.sampled_origin)[0]==Catch::Approx(a.transform.origin.x).margin(.002));
    CHECK((*row.rendered_origin)[0]==a.transform.origin.x);
    CHECK(row.previous_source_ordinal==1);CHECK(row.current_source_ordinal==2);
    CHECK(row.latest_source_ordinal==2);CHECK(row.pose_submitted==true);
    CHECK(row.visible==(a.visibility_status==hlclient::entity_render::RuntimeEntityVisibilityStatus::visible));
    CHECK(row.lifetime!=0);
    app::MovementFrameRecord frame;frame.frame_number=++frame_number;
    frame.monotonic_us=frame_number*5'000;frame.network_generation=frame.map_generation=1;
    REQUIRE(receipt.observe(frame));
    REQUIRE(receipt.observe_remote(frame,*on.projection->remote_movement_observations()));
  }
  REQUIRE(receipt.mark(app::MovementIncidentTrigger::manual,10'000));
  const auto report=receipt.encode({"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,123,456});
  REQUIRE(report);CHECK(report->find("\"remote_players\":{\"schema\":\"hlclient.remote-players.v1\"")!=std::string::npos);
  CHECK(report->find("\"rendered_origin\":[5,")!=std::string::npos);
  // A per-actor same-time boundary must disclose the position source actually
  // selected, not the still-bracketed global or raw pose interpolation pair.
  auto correction=e10_observation(3,1.02,30,140,0);
  REQUIRE(on.projection->project_entities(correction,on_world));
  REQUIRE(on.projection->present_entities(on_host,100.01,1,on_world,{640,480},true));
  const auto corrected=captured(2);
  CHECK(corrected.presentation.decision==hlclient::entity_visual::PlayerPresentationDecision::held_floor);
  CHECK(corrected.previous_source_ordinal==3);CHECK(corrected.current_source_ordinal==3);
  REQUIRE(corrected.rendered_origin);CHECK((*corrected.rendered_origin)[0]==30);
  // Hidden/missing materialization is not a rendered coordinate; absence is
  // distinct from both and is reported by the recorder, not invented here.
  auto hidden=e10_observation(4,1.04,30,140,0,false);
  hidden.packet_entities[1].effects=128U;
  hidden.canonical_state_hash=client::runtime_observation_canonical_hash(hidden);
  REQUIRE(on.projection->project_entities(hidden,on_world));
  REQUIRE(on.projection->present_entities(on_host,100.04,1,on_world,{640,480},true));
  const auto hidden_row=captured(2);
  CHECK(hidden_row.pose_submitted==false);CHECK_FALSE(hidden_row.rendered_origin);
  REQUIRE(on.projection->remote_movement_observations()->size()==1);
  REQUIRE_FALSE(on.projection->present_entities(on_host,std::numeric_limits<double>::quiet_NaN(),1,on_world,{640,480},true));
  CHECK_FALSE(on.projection->remote_movement_observations());
  REQUIRE(on.projection->present_entities(on_host,100.05,1,on_world,{640,480},true));
  on.projection->invalidate_player_continuity();
  CHECK_FALSE(on.projection->remote_movement_observations());
}

#if HLCLIENT_HAS_GAME_HALFLIFE
TEST_CASE("E13 normal materializer binds server corpse proxy independently and body queue reuse is immediate",
    "[e13][corpse][local-capture-assets][actual-context]") {
  LocalContext context(false,"BRUSH_ONLY",false,std::uint8_t{80});
  // Owned triangle must face the +X test camera, not be edge-on in the XY plane.
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset,0,-2,-4);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+12U,0,2,-4);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+24U,0,0,4);
  for(std::size_t i=0;i<3U;++i)
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioNormalsOffset+i*12U,1,0,0);
  context.root.write("valve","models/shared.mdl",context.model);
  auto created=context.create();REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,1});client::ClientWorldState world;
  std::unique_ptr<fixture::entity_opengl_fixture::HiddenContext> gl;
  try { gl=std::make_unique<fixture::entity_opengl_fixture::HiddenContext>(320,240); }
  catch(const std::exception& e) {WARN(e.what());}
  if(gl && fixture::entity_opengl_fixture::capable_context()) gl->initialize_renderer();
  else {gl.reset();WARN("E13 GPU subcheck unavailable; CPU corpse binding still checked");}
  std::uint64_t previous_pixels=0;
  for(std::uint64_t cycle=1;cycle<=3;++cycle) {
    auto state=e10_observation(cycle,static_cast<double>(cycle),500,0,0,false);
    state.packet_entities.resize(3); // receiving player1, remote2, body queue40
    auto& corpse=state.packet_entities.back();
    corpse.render_effect=17;corpse.render_amount=cycle==2 ? 1U : 2U;
    corpse.model_index=999; // Never resolved from corpse model slot or filename.
    corpse.origin={0.0,static_cast<double>(cycle-1)*5.0,24.0};corpse.effects=32;
    corpse.animation_time_seconds=static_cast<double>(cycle);corpse.frame_rate=0.0;
    corpse.controllers.fill(17U);corpse.blending.fill(0U);
    corpse.frame=cycle==2 ? 128.0 : 256.0;
    state.packet_entities[1].effects=32;state.packet_entities[1].weapon_model_index=21;
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    if(cycle==1) REQUIRE(created.projection->reset_generation(state,world));
    else REQUIRE(created.projection->project_entities(state,world));
    world.set_camera({{40,0,24},{0,0,24},{0,0,1}});
    REQUIRE(e10_present(*created.projection,host,100+static_cast<double>(cycle),1,world,{320,240}));
    const auto& instance=e10_instance(world,40);
    CHECK(instance.transform.origin.y==Catch::Approx(static_cast<float>((cycle-1)*5)));
    CHECK(instance.body_value==0);CHECK(instance.skin_family_index==0);
    REQUIRE(world.entity_frame()->studio_instances().size()==1);
    CHECK(instance.static_light_rgb.has_value());
    CHECK(created.projection->remote_weapon_poses().empty());
    CHECK(world.entity_frame()->studio_poses()[instance.pose_index].bone_matrices.size()==1);
    CHECK(state.canonical_state_hash==client::runtime_observation_canonical_hash(state));
    if(gl) {
      auto scene=client::build_render_scene(world);scene.static_world.reset();
      gl->renderer().render(scene,{320,240});
      const auto actual=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(actual);
      CHECK(actual.non_clear_pixel_count>0);
      if(previous_pixels) CHECK(actual.color_signature!=previous_pixels);
      previous_pixels=actual.color_signature;
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1);
    }
  }
  auto invalid=e10_observation(4,4,500,0,0,false);invalid.packet_entities.resize(3);
  invalid.packet_entities.back().render_effect=17;invalid.packet_entities.back().render_amount=9;
  invalid.canonical_state_hash=client::runtime_observation_canonical_hash(invalid);
  REQUIRE(created.projection->project_entities(invalid,world));
  REQUIRE(e10_present(*created.projection,host,104,1,world,{320,240}));
  CHECK(world.entity_frame()->studio_instances().empty());
  if(gl) gl->release_renderer();
}
#endif

TEST_CASE("E11 same production host attaches exact slots and renders weapon-only pixels",
          "[e11][e12][game-api][local-capture-assets][actual-context]") {
  LocalContext context;
  const auto geometry=[](std::vector<std::byte>& bytes,float y,float scale) {
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset,0,y-2*scale,-4*scale);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+12U,0,y+2*scale,-4*scale);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+24U,0,y,4*scale);
    for(unsigned i=0;i<3U;++i)
      fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioNormalsOffset+i*12U,1,0,0);
  };
  geometry(context.model,0,1);context.root.write("valve","models/shared.mdl",context.model);
  auto first_weapon=context.model,second_weapon=context.model;
  geometry(first_weapon,7,0.5F);geometry(second_weapon,-7,0.75F);
  context.root.write("valve","models/project_weapon_a.mdl",first_weapon);
  context.root.write("valve","models/project_weapon_b.mdl",second_weapon);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shared.mdl",7U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"models/project_weapon_a.mdl",21U,static_cast<std::uint32_t>(first_weapon.size()),0U},
      {2U,"models/project_weapon_b.mdl",22U,static_cast<std::uint32_t>(second_weapon.size()),0U},
      {2U,"models/missing.mdl",20U,4U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");
  REQUIRE(created.projection);
  auto module=std::make_unique<E10TestGameClientModule>();auto* selected=module.get();
  selected->attachments=true;
  selected->sockets_enabled=true;
  hlclient::game_api::GameClientHost host{std::move(module)};host.reset({1,1,1});
  client::ClientWorldState world;
  world.set_camera({{32,0,16},{0,0,16},{0,0,1}});
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if(gl && fixture::entity_opengl_fixture::capable_context()) gl->initialize_renderer();
  else {gl.reset();WARN("E11 GPU subcheck unavailable; production CPU frame assertions still run");}
  std::uint64_t previous_signature{},previous_image{};
  const auto stable_signature=[&]() {
    const auto scene=client::build_render_scene(world);REQUIRE(scene.dynamic_entities);
    hlclient::entity_render::EntityRenderFrameBuildInput input;
    input.resource_id=1U;input.resource_revision=1U;
    input.interpolation={0.0,0.0,1.0,0.0F,1U,2U,
        hlclient::entity_render::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    const auto& frame=*scene.dynamic_entities->frame;
    input.studio_poses.assign(frame.studio_poses().begin(),frame.studio_poses().end());
    input.studio_instances.assign(frame.studio_instances().begin(),frame.studio_instances().end());
    const auto built=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*scene.dynamic_entities->package,std::move(input));
    REQUIRE(built);return built.frame->frame_signature();
  };
  std::uint64_t record=1;
  for(const auto slot:{21U,22U,21U,0U,999U,20U}) {
    const auto step=record-1U;
    const auto anchor=100.0+static_cast<double>(step)*0.2;
    const auto clock=anchor+0.11;
    auto state=e10_observation(record++,1.0+static_cast<double>(step)*0.1,0,0,0,false);state.packet_entities.resize(2U);
    state.packet_entities[1].origin={0.0,0.0,16.0};state.packet_entities[1].weapon_model_index=slot;
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    const auto canonical=state.canonical_state_hash;
    if(step==0U) REQUIRE(created.projection->reset_generation(state,world));
    else REQUIRE(created.projection->project_entities(state,world));
    // First sample anchors this record's existing E10 interpolation clock.
    // Only after its bounded pair delay should a discrete weapon switch appear.
    REQUIRE(e10_present(*created.projection,host,anchor,1U,world,{320,240}));
    REQUIRE(e10_present(*created.projection,host,clock,1U,world,{320,240}));
    const auto frame=world.entity_frame();REQUIRE(frame);
    const bool ready=slot==21U||slot==22U;
    REQUIRE(frame->studio_instances().size()==(ready?2U:1U));
    CHECK(frame->studio_instances()[0].entity_number==2U);
    CHECK(frame->studio_instances()[0].subinstance_ordinal==0U);
    if(ready) {
      const auto& body=frame->studio_instances()[0];const auto& child=frame->studio_instances()[1];
      CHECK(child.entity_number==body.entity_number);CHECK(child.subinstance_ordinal==1U);
      CHECK(child.studio_asset_index!=body.studio_asset_index);CHECK(child.pose_index!=body.pose_index);
      CHECK(child.transform.origin.x==body.transform.origin.x);CHECK(child.transform.origin.y==body.transform.origin.y);
      CHECK(child.transform.origin.z==body.transform.origin.z);
      CHECK(child.transform.rotation_degrees.x==body.transform.rotation_degrees.x);
      CHECK(child.transform.rotation_degrees.y==body.transform.rotation_degrees.y);
      CHECK(child.transform.rotation_degrees.z==body.transform.rotation_degrees.z);
      CHECK(child.transform.uniform_scale==body.transform.uniform_scale);CHECK(child.static_light_rgb==body.static_light_rgb);
      CHECK(frame->studio_poses()[child.pose_index].bone_matrices==frame->studio_poses()[body.pose_index].bone_matrices);
      CHECK(frame->draw_commands()[0].instance_index==0U);CHECK(frame->draw_commands()[1].instance_index==1U);
      REQUIRE(selected->socket_count==1U);
      const auto& socket=selected->sockets[0];
      CHECK(socket.entity==child.entity_number);CHECK(socket.model_slot==slot);
      CHECK(socket.source==hlclient::game_api::RemoteSocketSource::validated_pmodel_socket);
      REQUIRE(frame->studio_poses()[child.pose_index].bone_matrices.size()==1U);
      const auto& bone=frame->studio_poses()[child.pose_index].bone_matrices[0];
      const auto matrix=hlclient::entity_render::entity_render_model_matrix(child.transform);
      // Independent vertex-style transform from the exact published render palette.
      const float x=bone[4]*3+bone[8]*4+bone[12];
      const float y=bone[5]*3+bone[9]*4+bone[13];
      const float z=bone[6]*3+bone[10]*4+bone[14];
      CHECK(socket.muzzle.origin.x==Catch::Approx(matrix[0]*x+matrix[4]*y+matrix[8]*z+matrix[12]));
      CHECK(socket.muzzle.origin.y==Catch::Approx(matrix[1]*x+matrix[5]*y+matrix[9]*z+matrix[13]));
      CHECK(socket.muzzle.origin.z==Catch::Approx(matrix[2]*x+matrix[6]*y+matrix[10]*z+matrix[14]));
      const auto signature=stable_signature();
      if(previous_signature) CHECK(signature!=previous_signature);previous_signature=signature;
      REQUIRE(e10_present(*created.projection,host,clock,1U,world,{320,240}));
      CHECK(stable_signature()==signature); // Remove per-publication revision, not the actual pose/instance identity.
      if(gl) {
        auto scene=client::build_render_scene(world);scene.static_world.reset();
        gl->renderer().render(scene,{320,240});
        const auto with_weapon=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(with_weapon);
        const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
        if(previous_image) CHECK(with_weapon.color_signature!=previous_image);previous_image=with_weapon.color_signature;
        selected->attachments=false;REQUIRE(e10_present(*created.projection,host,clock,1U,world,{320,240}));
        scene=client::build_render_scene(world);scene.static_world.reset();
        gl->renderer().render(scene,{320,240});
        const auto body_only=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(body_only);
        std::size_t changed=0U,body_preserved=0U;
        for(std::size_t pixel=0;pixel<body_only.rgba8.size()/4U;++pixel) {
          const auto at=pixel*4U;
          const bool equal=std::equal(body_only.rgba8.begin()+at,body_only.rgba8.begin()+at+3U,with_weapon.rgba8.begin()+at);
          if(!equal) ++changed;
          const auto clear=scene.clear_color;
          const bool body_pixel=std::abs(int(body_only.rgba8[at])-int(std::lround(clear.red*255)))>2 ||
              std::abs(int(body_only.rgba8[at+1U])-int(std::lround(clear.green*255)))>2 ||
              std::abs(int(body_only.rgba8[at+2U])-int(std::lround(clear.blue*255)))>2;
          if(body_pixel) {CHECK(equal);if(equal) ++body_preserved;}
        }
        CHECK(changed>10U);CHECK(changed<320U*240U/8U);CHECK(body_preserved>10U);
        CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
        selected->attachments=true;
        CHECK(glGetError()==GL_NO_ERROR);
      }
    }
    CHECK(state.canonical_state_hash==canonical);
    CHECK(std::ranges::none_of(frame->studio_instances(),[](const auto& value){return value.entity_number==1U;}));
  }
  auto reciprocal=e10_observation(99U,2.0,0,0,0,false);reciprocal.packet_entities.resize(2U);
  reciprocal.packet_entities[0].origin={0.0,0.0,16.0};reciprocal.packet_entities[0].weapon_model_index=21U;
  reciprocal.packet_entities[1].weapon_model_index=22U;
  reciprocal.canonical_state_hash=client::runtime_observation_canonical_hash(reciprocal);
  REQUIRE(created.projection->reset_generation(reciprocal,world));
  REQUIRE(e10_present(*created.projection,host,200,2U,world,{320,240}));
  REQUIRE(world.entity_frame()->studio_instances().size()==2U);
  for(const auto& instance:world.entity_frame()->studio_instances()) CHECK(instance.entity_number==1U);
  if(gl) gl->release_renderer();host.teardown();
}
TEST_CASE("E10 decoded equal-height peers keep world position and actual pixels during approach",
          "[e10-origin][e10][runtime-replay][actual-context]") {
  namespace p=hlclient::test::player_origin_fixture;
  namespace g=hlclient::goldsrc;
  LocalContext context;
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset,-16.0F,0.0F,-36.0F);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+12U,16.0F,0.0F,-36.0F);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+24U,0.0F,0.0F,36.0F);
  for(std::size_t i=0U;i<3U;++i)
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioNormalsOffset+i*12U,0.0F,-1.0F,0.0F);
  context.root.write("valve","models/shared.mdl",context.model);
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if(gl && fixture::entity_opengl_fixture::capable_context()) gl->initialize_renderer();
  else { gl.reset(); WARN("GPU subcheck unavailable: OpenGL 3.3 Core; decoder/host assertions still run"); }
  for(const auto receiver : {1U,2U}) {
    INFO("receiving entity=" << receiver);
    auto created=context.create(); REQUIRE(created.projection);
    auto host=std::make_shared<hlclient::game_api::GameClientHost>(std::make_unique<E10TestGameClientModule>());
    g::RuntimeReplayInitialization init;
    init.generation=1U; init.max_clients=2U; init.schemas=p::schemas();
    init.baselines=p::baselines(*init.schemas); init.receiving_player_entity=receiver; init.game_client=host;
    client::ClientWorldState world;
    auto session=g::RuntimeReplaySession::initialize(std::move(init),world); REQUIRE(session);
    REQUIRE(created.projection->reset_generation(*world.runtime_observation(),world));
    const p::Position remote{-602.5,834.0,-1660.0};
    std::uint32_t step{};
    for(const float distance : {256.0F,192.0F,128.0F,96.0F,48.0F,32.0F,192.0F}) {
      ++step;
      // First, differing height forces explicit Z; then equal-height inheritance.
      const p::Position local{remote[0],remote[1]-distance,remote[2]-(step==1U ? 2.0 : 0.0)};
      auto source=p::payload(p::full(receiver,local,remote,true,true,true,false,
          30.0F+static_cast<float>(step)*0.02F),100U+step);
      const g::RuntimeReplayRecord record{1U,step,step,std::move(source),{}};
      auto applied=session.session->apply_record(record);
      INFO((applied.error ? applied.error->context : "")); REQUIRE(applied);
      const auto& observed=world.runtime_observation()->packet_entities[2U-receiver];
      REQUIRE(observed.entity_number==3U-receiver);
      REQUIRE(observed.origin.z==remote[2]);
      world.set_camera({{static_cast<float>(remote[0]),static_cast<float>(local[1]),-1632.0F},
          {static_cast<float>(remote[0]),static_cast<float>(remote[1]),-1632.0F},{0.0F,0.0F,1.0F}});
      REQUIRE(created.projection->project_entities(*world.runtime_observation(),world));
      REQUIRE(e10_present(*created.projection,*host,step*0.02,receiver,world,{640,480}));
      const auto& instance=e10_instance(world,3U-receiver);
      CHECK(instance.transform.origin.z==-1660.0F);
      CHECK(instance.interpolated_bounds.maximum.z < -1500.0F);
      CHECK(instance.visibility_status==hlclient::entity_render::RuntimeEntityVisibilityStatus::visible);
      CHECK(world.entity_frame()->studio_instances().size()==1U);
      if(gl) {
        auto scene=client::build_render_scene(world); scene.static_world.reset();
        gl->renderer().render(scene,{640,480});
        const auto pixels=gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);
        REQUIRE(pixels); CHECK(pixels.non_clear_pixel_count>0U);
        auto baseline=scene; baseline.dynamic_entities.reset();
        gl->renderer().render(baseline,{640,480});
        const auto empty=gl->renderer().observe_framebuffer({640,480},baseline.clear_color,true);
        REQUIRE(empty); CHECK(empty.non_clear_pixel_count==0U);
        CHECK(glGetError()==GL_NO_ERROR);
        std::cout << "e10-origin receiver=" << receiver << " distance=" << distance
            << " z=" << instance.transform.origin.z << " gpu_pixels=" << pixels.non_clear_pixel_count << '\n';
      }
    }
    CHECK(host->drain_audio().count==0U);
  }
  if(gl) gl->release_renderer();
}

TEST_CASE("E10 normal local materializer advances 20ms players at 5ms render cadence",
          "[e10][local-capture-assets][game-api][runtime-presentation]") {
  LocalContext context(true);
  auto created = app::RuntimeReplayLocalAssets::create(context.resources(),
      readiness::parse_server_info("maps/test_map.bsp"), context.root.path(), "valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : std::string{}));
  REQUIRE(created.projection);
  auto alternate = std::make_unique<E10TestGameClientModule>();
  auto* module = alternate.get(); // Test owner remains the same host for all frames.
  hlclient::game_api::GameClientHost host{std::move(alternate)};
  host.reset({1U, 1U, 1U});
  client::ClientWorldState world;
  world.set_camera({{0.0F, -100.0F, 40.0F}, {10.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}});
  const auto first = e10_observation(1U, 1.0, 0.0, 40.0, 0.0);
  REQUIRE(client::valid_runtime_observation(first));
  REQUIRE(created.projection->reset_generation(first, world));
  const auto package = world.entity_scene();
  const auto current = e10_observation(2U, 1.02, 20.0, 140.0, 20.0, false);
  REQUIRE(client::valid_runtime_observation(current));
  REQUIRE(created.projection->project_entities(current, world));
  REQUIRE(e10_present(*created.projection, host, 100.0, 1U, world, {640, 480}));
  REQUIRE(created.projection->summary().remote_snapshot_mode);
  CHECK(app::remote_snapshot_mode_name(*created.projection->summary().remote_snapshot_mode)=="exact_previous");
  CHECK(e10_instance(world, 2U).transform.origin.x == Catch::Approx(0.0F));
  const auto frame0 = world.entity_frame();
  CHECK(e10_instance(world, 2U).body_value == static_cast<std::uint32_t>(module->selected_body));
  REQUIRE(world.entity_frame()->studio_instances().size() == 2U);
  CHECK(std::ranges::none_of(world.entity_frame()->studio_instances(), [](const auto& value) {
    return value.entity_number == 1U || value.entity_number == 3U;
  }));
  CHECK(e10_instance(world, 40U).transform.origin.x == 140.0F);
  REQUIRE(world.runtime_brushes());
  REQUIRE(world.runtime_brushes()->instances.size() == 1U);
  CHECK(world.runtime_brushes()->instances[0U].transformed_bounds.minimum.z == 20.0F);
  CHECK(world.entity_frame()->interpolation().profile == hlclient::entity_render::
      EntityRenderInterpolationProfile::public_runtime_server_seconds_v1);
  for (const auto [elapsed, expected] : std::array{
           std::pair{0.005, 5.0F}, std::pair{0.010, 10.0F},
           std::pair{0.015, 15.0F}, std::pair{0.020, 20.0F}}) {
    REQUIRE(e10_present(*created.projection, host, 100.0 + elapsed, 1U, world, {640, 480}));
    CHECK(e10_instance(world, 2U).transform.origin.x == Catch::Approx(expected).margin(0.002F));
    CHECK(e10_instance(world, 40U).transform.origin.x == 140.0F);
    CHECK(world.runtime_brushes()->instances[0U].transformed_bounds.minimum.z == 20.0F);
    CHECK(world.entity_scene() == package);
    REQUIRE(created.projection->summary().remote_snapshot_mode);
    if(elapsed<.020)CHECK(app::remote_snapshot_mode_name(*created.projection->summary().remote_snapshot_mode)=="bracketed");
  }
  CHECK(frame0->studio_instances()[0U].transform.origin.x == Catch::Approx(0.0F));
  CHECK(current.packet_entities[1U].origin.x == 20.0);
  CHECK(current.canonical_state_hash == client::runtime_observation_canonical_hash(current));
  CHECK(module->requests == 5U); // Only the remote player, never non-player/brush/local.
  CHECK(host.drain_audio().count == 0U);
  REQUIRE(e10_present(*created.projection, host, 100.0 + 0.050, 1U, world, {640, 480}));
  CHECK(e10_instance(world, 2U).transform.origin.x == Catch::Approx(20.0F));
  REQUIRE(created.projection->summary().remote_snapshot_mode);
  CHECK(app::remote_snapshot_mode_name(*created.projection->summary().remote_snapshot_mode)=="exact_current");
}

TEST_CASE("E32 normal materializer uses buffered committed player brackets without packet-phase jumps",
    "[e32-remote-buffer][e10][local-capture-assets][runtime-presentation]") {
  LocalContext context(true);auto created=context.create();REQUIRE(created.projection);
  auto module=std::make_unique<E10TestGameClientModule>();auto* seen=module.get();
  hlclient::game_api::GameClientHost host{std::move(module)};host.reset({1,1,1});
  client::ClientWorldState world;
  world.set_camera({{250,-1000,100},{0,1,0},{0,0,1}});
  const auto first=e10_observation(1,1,0,40,0,false);
  REQUIRE(created.projection->reset_generation(first,world));
  auto current=e10_observation(2,1.04,12.8,140,20,false);
  REQUIRE(created.projection->project_entities(current,world));
  REQUIRE(e10_present(*created.projection,host,10,1,world,{640,480}));
  double last=created.projection->summary().remote_sample_seconds;
  std::size_t next=3;constexpr std::array jitter{.012,-.008,.006,-.005,0.0};
  for(std::size_t frame=1;frame<=180;++frame) {
    const double now=10+static_cast<double>(frame)*.005;
    while(next<40 && now>=10+(static_cast<double>(next)-2)*.04+jitter[next%jitter.size()]) {
      const double time=1+(static_cast<double>(next)-1)*.04;
      current=e10_observation(next++,time,320*(time-1),140,20,false);
      REQUIRE(created.projection->project_entities(current,world));
    }
    const auto immutable=current;
    REQUIRE(e10_present(*created.projection,host,now,1,world,{640,480}));
    const auto& summary=created.projection->summary();
    CHECK(summary.remote_sample_seconds>=last-1e-9);
    if(frame>100)CHECK(summary.remote_sample_seconds-last<=.005001);
    CHECK(e10_instance(world,2).transform.origin.x==Catch::Approx(320*(summary.remote_sample_seconds-1)).margin(.001));
    CHECK(seen->previous_server_seconds<=summary.remote_sample_seconds+1e-8);
    CHECK(seen->current_server_seconds>=summary.remote_sample_seconds-1e-8);
    CHECK(current==immutable);CHECK(e10_instance(world,40).transform.origin.x==140);
    REQUIRE(world.runtime_brushes());CHECK(world.runtime_brushes()->instances[0].transformed_bounds.minimum.z==20);
    CHECK(host.drain_audio().count==0);last=summary.remote_sample_seconds;
  }
}

TEST_CASE("E32 buffered old pmodel cannot suppress a new weapon event with a mismatching socket",
    "[e32-remote-buffer][e11][e12][local-capture-assets][runtime-presentation]") {
  LocalContext context;
  context.root.write("valve","models/project_weapon_a.mdl",context.model);
  context.root.write("valve","models/project_weapon_b.mdl",context.model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shared.mdl",7U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"models/project_weapon_a.mdl",21U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"models/project_weapon_b.mdl",22U,static_cast<std::uint32_t>(context.model.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");REQUIRE(created.projection);
  auto module=std::make_unique<E10TestGameClientModule>();auto* seen=module.get();
  seen->attachments=true;seen->sockets_enabled=true;
  hlclient::game_api::GameClientHost host{std::move(module)};host.reset({1,1,1});client::ClientWorldState world;
  world.set_camera({{32,0,16},{0,0,16},{0,0,1}});
  auto first=e10_observation(1,1,0,0,0,false);first.packet_entities.resize(2);
  first.packet_entities[1].origin.z=16;first.packet_entities[1].weapon_model_index=21;
  first.canonical_state_hash=client::runtime_observation_canonical_hash(first);
  REQUIRE(created.projection->reset_generation(first,world));
  auto second=first;second.publication_revision=2;second.server_time_seconds=1.04;
  second.entity_metadata.source->record_identity=2;second.entity_metadata.source->record_ordinal=2;
  second.server_time_metadata=second.entity_metadata;second.packet_entities[1].weapon_model_index=22;
  second.canonical_state_hash=client::runtime_observation_canonical_hash(second);
  REQUIRE(created.projection->project_entities(second,world));
  REQUIRE(e10_present(*created.projection,host,10,1,world,{320,240}));
  REQUIRE(world.entity_frame()->studio_instances().size()==2);
  CHECK(seen->socket_count==0); // Existing server-event fallback stays available.
  REQUIRE(e10_present(*created.projection,host,10.041,1,world,{320,240}));
  REQUIRE(seen->socket_count==1);CHECK(seen->sockets[0].model_slot==22);
}

TEST_CASE("E10 local materializer reset discards previous anchors without resurrecting players",
          "[e10][local-capture-assets][game-api][runtime-presentation][reset]") {
  LocalContext context;
  auto created = context.create();
  REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};
  host.reset({1U, 1U, 1U});
  client::ClientWorldState world;
  const auto first = e10_observation(1U, 1.0, 0.0, 40.0, 0.0);
  const auto second = e10_observation(2U, 1.02, 20.0, 40.0, 0.0, false);
  REQUIRE(created.projection->reset_generation(first, world));
  REQUIRE(created.projection->project_entities(second, world));
  REQUIRE(e10_present(*created.projection, host, 100.0, 1U, world, {640, 480}));
  CHECK(e10_instance(world, 2U).transform.origin.x == Catch::Approx(0.0F));
  const auto reset = e10_observation(3U, 1.04, 88.0, 40.0, 0.0, false);
  REQUIRE(created.projection->reset_generation(reset, world));
  REQUIRE(e10_present(*created.projection, host, 200.0, 1U, world, {640, 480}));
  CHECK(e10_instance(world, 2U).transform.origin.x == Catch::Approx(88.0F));
  const auto metadata = world.entity_frame()->interpolation();
  CHECK(metadata.previous_time_seconds == metadata.current_time_seconds);
  CHECK(metadata.alpha == 0.0F);
  CHECK(metadata.previous_state_identity == metadata.current_state_identity);
  CHECK(std::ranges::none_of(world.entity_frame()->studio_instances(), [](const auto& value) {
    return value.entity_number == 1U || value.entity_number == 3U;
  }));
}

TEST_CASE("E10 normal local materializer samples retained floor light and preserves unlit fallback",
          "[e10][local-capture-assets][game-api][runtime-presentation][lighting]") {
  for (const bool lit : {true, false}) {
    INFO("project-owned floor light=" << lit);
    LocalContext context(false, "BRUSH_ONLY", false,
        lit ? std::optional<std::uint8_t>{static_cast<std::uint8_t>(80)} : std::nullopt);
    auto created = context.create();
    INFO((created.error ? created.error->context : std::string{}));
    REQUIRE(created.projection);
    hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};
    host.reset({1U, 1U, 1U});
    client::ClientWorldState world;
    auto state = e10_observation(1U, 1.0, 32.0, 40.0, 0.0, false);
    state.packet_entities[1U].origin = {32.0, 32.0, 24.0};
    state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
    REQUIRE(created.projection->reset_generation(state, world));
    CHECK(e10_instance(world, 2U).static_light_rgb.has_value() == lit);
    REQUIRE(e10_present(*created.projection, host, 100.0, 1U, world, {640,480}));
    const auto& instance = e10_instance(world, 2U);
    CHECK(instance.static_light_rgb.has_value() == lit);
    if (lit) {
      REQUIRE(instance.static_light_rgb);
      for (const auto component : *instance.static_light_rgb)
        CHECK(component == Catch::Approx(80.0F / 255.0F).margin(0.0001F));
    }
    // Ordinary models now share the same lighting; the materializer no longer
    // reserves static light for the player-policy path.
    CHECK(e10_instance(world, 40U).static_light_rgb.has_value() == lit);
    const auto floor_time = std::filesystem::last_write_time(
        context.root.game_path("valve") / "maps/test_map.bsp");
    // Moving beyond the bounded lighting-cache distance reprobes existing CPU
    // triangles/atlas. It cannot reopen or mutate the approved map resource.
    state.publication_revision = 2U;
    state.entity_metadata.source->record_identity = 2U;
    state.entity_metadata.source->record_ordinal = 2U;
    state.server_time_metadata.source->record_identity = 2U;
    state.server_time_metadata.source->record_ordinal = 2U;
    state.server_time_seconds = 1.02;
    state.packet_entities[1U].origin.x = 48.0;
    state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
    REQUIRE(created.projection->project_entities(state, world));
    REQUIRE(e10_present(*created.projection, host, 100.1, 1U, world, {640,480}));
    REQUIRE(e10_present(*created.projection, host, 100.12, 1U, world, {640,480}));
    CHECK(e10_instance(world, 2U).static_light_rgb.has_value() == lit);
    if (lit) for (const auto component : *e10_instance(world, 2U).static_light_rgb)
      CHECK(component == Catch::Approx(80.0F / 255.0F).margin(0.0001F));
    CHECK(std::filesystem::last_write_time(context.root.game_path("valve") /
        "maps/test_map.bsp") == floor_time);
  }
}

TEST_CASE("Ordinary Studio pickups receive retained world light without player classification",
          "[studio-world-light][local-capture-assets][lighting]") {
  for (const auto level : {std::uint8_t{0}, std::uint8_t{48}, std::uint8_t{192}}) {
    CAPTURE(level);
    LocalContext context(false,"BRUSH_ONLY",false,level);
    auto created=context.create();REQUIRE(created.projection);
    client::ClientWorldState world;
    auto state=observation();
    const auto canonical=state.canonical_state_hash;
    REQUIRE(created.projection->reset_generation(state,world));
    for (const auto& instance:world.entity_frame()->studio_instances()) {
      REQUIRE(instance.static_light_rgb);
      for (const auto component:*instance.static_light_rgb)
        CHECK(component==Catch::Approx(float(level)/255.0F).margin(.0001F));
    }
    CHECK(client::runtime_observation_canonical_hash(state)==canonical);
    CHECK(world.entity_frame()->studio_instances().size()==2U);
  }
}

TEST_CASE("Viewmodel held origin moves geometry and both socket samplers exactly once",
          "[stock-viewmodel][local-capture-assets][first-person]") {
  LocalContext context;auto created=context.create();REQUIRE(created.projection);
  auto& assets=*created.projection;client::ClientWorldState world;
  REQUIRE(assets.reset_generation(observation(),world));
  hlclient::game_api::ViewmodelIntent intent;
  intent.status=hlclient::game_api::ViewmodelStatus::ready;
  intent.generation=1;intent.publication_revision=1;intent.model_index=7;
  const auto original=assets.materialize_viewmodel(intent);REQUIRE(original);
  const auto socket=assets.viewmodel_attachment_zero();REQUIRE(socket);
  intent.camera_local_origin={0,0,-1};
  for(double frame:{0.,.5,1.}) {
    intent.frame_coordinate=frame;
    const auto moved=assets.materialize_viewmodel(intent);REQUIRE(moved);
    const auto& instance=moved->frame->studio_instances()[0];
    CHECK(instance.transform.origin.z==-1.F);CHECK(instance.transform.uniform_scale==1.F);
    const auto current=assets.viewmodel_attachment_zero();REQUIRE(current);
    const auto marker=assets.viewmodel_attachment_at_frame(intent,0,frame);REQUIRE(marker);
    CHECK(current->x==Catch::Approx(marker->x));CHECK(current->y==Catch::Approx(marker->y));
    CHECK(current->z==Catch::Approx(marker->z));
    if(frame==0.) CHECK(current->z==Catch::Approx(socket->z-1.F));
  }
  intent.camera_local_origin[2]=std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(assets.materialize_viewmodel(intent));CHECK_FALSE(assets.viewmodel_attachment_zero());
  CHECK_FALSE(assets.viewmodel_attachment_at_frame(intent,0,0));
  intent.camera_local_origin[2]=65.F;
  CHECK_FALSE(assets.materialize_viewmodel(intent));CHECK_FALSE(assets.viewmodel_attachment_at_frame(intent,0,0));
}

TEST_CASE("Same-frame viewmodel socket reuse preserves historical and invalidated requests",
          "[stock-viewmodel][local-capture-assets][first-person][pose-reuse]") {
  LocalContext context;auto created=context.create();REQUIRE(created.projection);
  auto& assets=*created.projection;client::ClientWorldState world;
  REQUIRE(assets.reset_generation(observation(),world));
  hlclient::game_api::ViewmodelIntent intent;
  intent.status=hlclient::game_api::ViewmodelStatus::ready;
  intent.generation=1U;intent.publication_revision=7U;intent.model_index=7U;
  intent.frame_coordinate=.375;intent.local_time_seconds=.125;
  REQUIRE(assets.materialize_viewmodel(intent));
  const auto same=assets.viewmodel_attachment_at_frame(intent,0U,intent.frame_coordinate);
  REQUIRE(same);
  const auto zero=assets.viewmodel_attachment_zero();REQUIRE(zero);
  const auto exact=[](const auto& left,const auto& right) {
    return std::bit_cast<std::uint32_t>(left.x)==std::bit_cast<std::uint32_t>(right.x) &&
        std::bit_cast<std::uint32_t>(left.y)==std::bit_cast<std::uint32_t>(right.y) &&
        std::bit_cast<std::uint32_t>(left.z)==std::bit_cast<std::uint32_t>(right.z);
  };
  CHECK(exact(*same,*zero));
  auto fresh=intent;++fresh.publication_revision;
  const auto independently_evaluated=assets.viewmodel_attachment_at_frame(
      fresh,0U,intent.frame_coordinate);
  REQUIRE(independently_evaluated);
  CHECK(exact(*same,*independently_evaluated));
  const auto historical=assets.viewmodel_attachment_at_frame(intent,0U,.625);
  const auto historical_fresh=assets.viewmodel_attachment_at_frame(fresh,0U,.625);
  REQUIRE(historical);REQUIRE(historical_fresh);
  CHECK(exact(*historical,*historical_fresh));
  auto invalid=intent;invalid.status=hlclient::game_api::ViewmodelStatus::hidden;
  CHECK_FALSE(assets.materialize_viewmodel(invalid));
  CHECK_FALSE(assets.viewmodel_attachment_zero());
  const auto after_invalidation=assets.viewmodel_attachment_at_frame(
      intent,0U,intent.frame_coordinate);
  REQUIRE(after_invalidation);
  CHECK(exact(*same,*after_invalidation));
}

TEST_CASE("Opt-in same-frame viewmodel and muzzle attachment work retains exact output",
          "[optimization-studio-frame][benchmark][first-person][no-stock]") {
  LocalContext context;auto created=context.create();REQUIRE(created.projection);
  auto& assets=*created.projection;client::ClientWorldState world;
  REQUIRE(assets.reset_generation(observation(),world));
  hlclient::game_api::ViewmodelIntent intent;
  intent.status=hlclient::game_api::ViewmodelStatus::ready;
  intent.generation=1;intent.model_index=7;
  constexpr std::size_t warm=48U,samples=256U;
  std::array<double,samples> durations{};
  std::uint64_t signature=14695981039346656037ULL;
  const auto mix=[&](std::uint32_t value) {
    for(unsigned byte=0;byte<4U;++byte) {
      signature^=(value>>(byte*8U))&255U;signature*=1099511628211ULL;
    }
  };
  for(std::size_t i=0;i<warm+samples;++i) {
    intent.publication_revision=i+1U;
    intent.frame_coordinate=double(i%4U)*.25;
    intent.local_time_seconds=double(i)*.01;
    const auto begin=std::chrono::steady_clock::now();
    const auto rendered=assets.materialize_viewmodel(intent);
    const auto attachment=assets.viewmodel_attachment_at_frame(intent,0U,intent.frame_coordinate);
    const auto end=std::chrono::steady_clock::now();
    REQUIRE(rendered);REQUIRE(rendered->frame);REQUIRE(attachment);
    const auto current=assets.viewmodel_attachment_zero();REQUIRE(current);
    CHECK(std::bit_cast<std::uint32_t>(attachment->x)==std::bit_cast<std::uint32_t>(current->x));
    CHECK(std::bit_cast<std::uint32_t>(attachment->y)==std::bit_cast<std::uint32_t>(current->y));
    CHECK(std::bit_cast<std::uint32_t>(attachment->z)==std::bit_cast<std::uint32_t>(current->z));
    if(i<warm)continue;
    durations[i-warm]=std::chrono::duration<double,std::micro>(end-begin).count();
    mix(std::bit_cast<std::uint32_t>(attachment->x));
    mix(std::bit_cast<std::uint32_t>(attachment->y));
    mix(std::bit_cast<std::uint32_t>(attachment->z));
    for(const auto& pose:rendered->frame->studio_poses())
      for(const auto& matrix:pose.bone_matrices)
        for(const auto component:matrix)mix(std::bit_cast<std::uint32_t>(component));
  }
  std::sort(durations.begin(),durations.end());
  std::cout<<"[optimization-viewmodel] {\"samples\":"<<samples
      <<",\"p50_us\":"<<durations[127U]<<",\"p95_us\":"<<durations[242U]
      <<",\"p99_us\":"<<durations[252U]<<",\"max_us\":"<<durations.back()
      <<",\"output_signature\":\""<<signature<<"\"}\n";
}

TEST_CASE("Studio viewmodel and transient models sample world light without moving local geometry",
          "[studio-world-light][local-capture-assets][first-person][lighting]") {
  for (const bool lit:{true,false}) {
    LocalContext context(false,"BRUSH_ONLY",false,lit ? std::optional<std::uint8_t>{64} : std::nullopt);
    auto created=context.create();REQUIRE(created.projection);
    auto& assets=*created.projection;
    client::ClientWorldState world;
    REQUIRE(assets.reset_generation(observation(),world));
    hlclient::game_api::ViewmodelIntent intent;
    intent.status=hlclient::game_api::ViewmodelStatus::ready;
    intent.generation=1;intent.publication_revision=1;intent.model_index=7;
    const auto baseline=assets.materialize_viewmodel(intent);REQUIRE(baseline);
    CHECK_FALSE(baseline->frame->studio_instances()[0].static_light_rgb);
    const auto local_transform=baseline->frame->studio_instances()[0].transform;
    const auto check=[&](const auto& entities) {
      REQUIRE(entities);REQUIRE(entities->frame->studio_instances().size()==1);
      const auto& instance=entities->frame->studio_instances()[0];
      CHECK(instance.static_light_rgb.has_value()==lit);
      if(lit) for(const auto c:*instance.static_light_rgb) CHECK(c==Catch::Approx(64.0F/255.0F));
    };
    for(const auto origin:std::array{hlclient::assets::AssetVector3{16,32,24},
        hlclient::assets::AssetVector3{48,32,40}}) {
      const auto viewmodel=assets.materialize_viewmodel(intent,origin);check(viewmodel);
      const auto& transform=viewmodel->frame->studio_instances()[0].transform;
      CHECK(transform.origin.x==local_transform.origin.x);
      CHECK(transform.origin.y==local_transform.origin.y);
      CHECK(transform.origin.z==local_transform.origin.z);
      CHECK(transform.rotation_degrees.x==local_transform.rotation_degrees.x);
      CHECK(transform.rotation_degrees.y==local_transform.rotation_degrees.y);
      CHECK(transform.rotation_degrees.z==local_transform.rotation_degrees.z);
      CHECK(transform.uniform_scale==local_transform.uniform_scale);
      CHECK(std::ranges::equal(viewmodel->frame->studio_poses()[0].bone_matrices,
          baseline->frame->studio_poses()[0].bone_matrices));
      hlclient::renderer::TransientShellState shell;shell.position=origin;
      constexpr std::string_view name="models/shared.mdl";
      std::copy(name.begin(),name.end(),shell.model_name.begin());
      check(assets.materialize_shells(std::span{&shell,1U},0));
    }
    const auto outside=assets.materialize_viewmodel(intent,hlclient::assets::AssetVector3{256,256,24});
    REQUIRE(outside);CHECK_FALSE(outside->frame->studio_instances()[0].static_light_rgb);
    const auto invalid=assets.materialize_viewmodel(intent,hlclient::assets::AssetVector3{
        std::numeric_limits<float>::quiet_NaN(),32,24});
    REQUIRE(invalid);CHECK_FALSE(invalid->frame->studio_instances()[0].static_light_rgb);
    check(assets.materialize_viewmodel(intent,hlclient::assets::AssetVector3{32,32,24}));
    REQUIRE(assets.reset_generation(observation(),world));
    check(assets.materialize_viewmodel(intent,hlclient::assets::AssetVector3{32,32,24}));
  }
}

TEST_CASE("Normal Studio materialization reaches GPU with dark room light fullbright and muzzle response",
          "[studio-world-light][local-capture-assets][lighting][actual-context]") {
  auto gl=fixture::entity_opengl_fixture::try_context();
  if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 Core unavailable");
  gl->initialize_renderer();
  for(const bool fullbright:{false,true}) {
    CAPTURE(fullbright);
    LocalContext context(false,"BRUSH_ONLY",false,std::uint8_t{48});
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset,8,-4,-4);
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+12,8,4,-4);
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+24,8,0,4);
    fixture::studio_write_i32le(context.model,fixture::kSyntheticStudioTextureOffset+64,fullbright ? 4:0);
    const auto palette=fixture::kSyntheticStudioTextureDataOffset+1;
    context.model[palette]=std::byte{200};context.model[palette+1]=std::byte{160};
    context.model[palette+2]=std::byte{100};
    context.root.write("valve","models/shared.mdl",context.model);
    auto created=context.create();REQUIRE(created.projection);
    client::ClientWorldState world;auto state=observation();state.packet_entities.resize(1);
    state.packet_entities[0].origin={32,32,24};state.packet_entities[0].angles={0,0,0};
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    REQUIRE(created.projection->reset_generation(state,world));
    world.set_camera({{16,32,24},{32,32,24},{0,0,1}});
    auto scene=client::build_render_scene(world);scene.static_world.reset();
    scene.clear_color={0,0,0,1};
    const auto draw=[&](const auto& value) {
      gl->renderer().render(value,{96,96});
      const auto pixels=fixture::entity_opengl_fixture::framebuffer();
      std::array<unsigned,3> maxima{};
      for(std::size_t i=0;i<pixels.size();i+=4) for(std::size_t c=0;c<3;++c)
        maxima[c]=std::max(maxima[c],unsigned(std::to_integer<std::uint8_t>(pixels[i+c])));
      CHECK(glGetError()==GL_NO_ERROR);
      return maxima;
    };
    const auto verify=[&](const auto& rgb) {
      // This fixture's posed normal is +Z, facing the downward environment
      // light. Keep the exact source-palette/lightmap pixel assertion, now
      // including the directional Studio response instead of a flat multiply.
      const double factor=fullbright ? 1.0:(48.0/255.0)*(224.0/185.6);
      for(std::size_t c=0;c<3;++c) {
        const double base=std::array{200,160,100}[c];
        CHECK(double(rgb[c])==Catch::Approx(base*factor).margin(2));
      }
    };
    verify(draw(scene));
    const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
    hlclient::game_api::ViewmodelIntent intent;
    intent.status=hlclient::game_api::ViewmodelStatus::ready;
    intent.generation=1;intent.publication_revision=1;intent.model_index=7;
    scene.dynamic_entities.reset();
    scene.first_person_entities=created.projection->materialize_viewmodel(intent,
        hlclient::assets::AssetVector3{32,32,24});
    const auto dark=draw(scene);verify(dark);
    scene.transient_first_person_light=hlclient::renderer::RenderPointLight{{8,0,0},64,0.5F,{1,1,1}};
    const auto flash=draw(scene);
    CHECK(flash[0]>dark[0]);
    scene.transient_first_person_light.reset();verify(draw(scene));
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    std::cout << "studio_light_gpu fullbright=" << fullbright << " rgb=" << dark[0] << ',' << dark[1]
        << ',' << dark[2] << " flash_rgb=" << flash[0] << ',' << flash[1] << ',' << flash[2] << '\n';
  }
}

TEST_CASE("E10 player-slot boundary rejects stale pair and same-time changes use current metadata",
          "[e10][local-capture-assets][game-api][runtime-presentation][lifecycle]") {
  LocalContext context;
  auto created = context.create();
  REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};
  host.reset({1U,1U,1U});
  client::ClientWorldState world;
  const auto first = e10_observation(1U,1.0,0.0,40.0,0.0,false);
  const auto second = e10_observation(2U,1.02,20.0,40.0,0.0,false);
  REQUIRE(created.projection->reset_generation(first,world));
  REQUIRE(created.projection->project_entities(second,world));
  REQUIRE(e10_present(*created.projection,host,100.0,1U,world,{640,480}));
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(0.0F));
  created.projection->player_slot_boundary(2U);
  REQUIRE(e10_present(*created.projection,host,100.005,1U,world,{640,480}));
  CHECK(std::ranges::none_of(world.entity_frame()->studio_instances(),[](const auto& instance) {
    return instance.entity_number == 2U;
  }));
  const auto fresh = e10_observation(3U,1.04,60.0,40.0,0.0,false);
  REQUIRE(created.projection->project_entities(fresh,world));
  REQUIRE(e10_present(*created.projection,host,100.020,1U,world,{640,480}));
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(60.0F));
  const auto same_time = e10_observation(4U,1.04,70.0,40.0,0.0,false);
  REQUIRE(created.projection->project_entities(same_time,world));
  REQUIRE(e10_present(*created.projection,host,100.025,1U,world,{640,480}));
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(70.0F));
  const auto& metadata = world.entity_frame()->interpolation();
  CHECK(metadata.previous_time_seconds == metadata.current_time_seconds);
  CHECK(metadata.alpha == 0.0F);
  CHECK(metadata.previous_state_identity == same_time.entity_metadata.source->record_identity);
  CHECK(metadata.current_state_identity == same_time.entity_metadata.source->record_identity);
  auto frame_only = same_time;
  frame_only.publication_revision = 5U;
  frame_only.entity_metadata.source->record_identity = 5U;
  frame_only.entity_metadata.source->record_ordinal = 5U;
  frame_only.server_time_metadata.source->record_identity = 5U;
  frame_only.server_time_metadata.source->record_ordinal = 5U;
  frame_only.packet_entities[1U].frame = 80.0;
  frame_only.canonical_state_hash = client::runtime_observation_canonical_hash(frame_only);
  CHECK(frame_only.canonical_state_hash == same_time.canonical_state_hash);
  REQUIRE(created.projection->project_entities(frame_only,world));
  REQUIRE(e10_present(*created.projection,host,100.030,1U,world,{640,480}));
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(70.0F));
  CHECK(world.entity_frame()->interpolation().previous_state_identity == 5U);
  CHECK(world.entity_frame()->interpolation().current_state_identity == 5U);
}

TEST_CASE("E10 held rendering retains committed anchor clocks at the game API",
          "[e10][crouch-regression][local-capture-assets][game-api][runtime-presentation]") {
  LocalContext context;
  auto created = context.create();
  REQUIRE(created.projection);
  auto module = std::make_unique<E10TestGameClientModule>();
  auto* observed = module.get();
  hlclient::game_api::GameClientHost host{std::move(module)};
  host.reset({1U,1U,1U});
  client::ClientWorldState world;
  const auto first = e10_observation(1U,1.0,0.0,40.0,0.0,false);
  const auto second = e10_observation(2U,1.4,20.0,40.0,0.0,false);
  REQUIRE(created.projection->reset_generation(first,world));
  REQUIRE(created.projection->project_entities(second,world));
  REQUIRE(e10_present(*created.projection,host,100.0,1U,world,{640,480}));
  CHECK(observed->requests == 1U);
  CHECK(observed->previous_server_seconds == Catch::Approx(1.0));
  CHECK(observed->current_server_seconds == Catch::Approx(1.4));
  // The renderer holds the latest endpoint across an excessive gap, but this
  // must not disguise the raw gap as a zero-time pair to a game policy.
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(20.0F));
  CHECK(world.entity_frame()->interpolation().previous_time_seconds == Catch::Approx(1.4));
  CHECK(world.entity_frame()->interpolation().current_time_seconds == Catch::Approx(1.4));
  REQUIRE(e10_present(*created.projection,host,100.005,1U,world,{640,480}));
  CHECK(observed->previous_server_seconds == Catch::Approx(1.0));
  CHECK(observed->current_server_seconds == Catch::Approx(1.4));
}

TEST_CASE("E10 opaque source IDs follow ordinal order and lost slot events reset all players",
          "[e10][local-capture-assets][game-api][runtime-presentation][lifecycle]") {
  LocalContext context;
  auto created = context.create();
  REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};
  host.reset({1U,1U,1U});
  client::ClientWorldState world;
  auto first = e10_observation(1U,1.0,0.0,40.0,0.0);
  first.entity_metadata.source->record_identity = 900U;
  first.server_time_metadata.source->record_identity = 900U;
  auto second = e10_observation(2U,1.02,20.0,140.0,0.0);
  second.entity_metadata.source->record_identity = 100U;
  second.server_time_metadata.source->record_identity = 100U;
  REQUIRE(client::valid_runtime_observation(first));
  REQUIRE(client::valid_runtime_observation(second));
  REQUIRE(created.projection->reset_generation(first,world));
  REQUIRE(created.projection->project_entities(second,world));
  REQUIRE(e10_present(*created.projection,host,100.0,1U,world,{640,480}));
  REQUIRE(e10_present(*created.projection,host,100.005,1U,world,{640,480}));
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(5.0F).margin(0.002F));
  CHECK(e10_instance(world,3U).transform.origin.x == Catch::Approx(0.0F));
  CHECK(world.entity_frame()->interpolation().previous_state_identity == 900U);
  CHECK(world.entity_frame()->interpolation().current_state_identity == 100U);
  CHECK(e10_instance(world,40U).transform.origin.x == 140.0F);
  // A bounded userinfo-event queue overflow cannot retain a possibly replaced
  // player's old interpolation/model-light owner. This visual-only seam does
  // not reset network/map state or remove current non-player instances.
  created.projection->invalidate_player_continuity();
  REQUIRE(e10_present(*created.projection,host,100.010,1U,world,{640,480}));
  CHECK(std::ranges::none_of(world.entity_frame()->studio_instances(),[](const auto& instance) {
    return instance.entity_number == 1U || instance.entity_number == 2U || instance.entity_number == 3U;
  }));
  CHECK(e10_instance(world,40U).transform.origin.x == 140.0F);
  auto fresh = e10_observation(3U,1.04,80.0,160.0,0.0);
  fresh.entity_metadata.source->record_identity = 50U;
  fresh.server_time_metadata.source->record_identity = 50U;
  REQUIRE(client::valid_runtime_observation(fresh));
  REQUIRE(created.projection->project_entities(fresh,world));
  REQUIRE(e10_present(*created.projection,host,100.015,1U,world,{640,480}));
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(80.0F));
  CHECK(e10_instance(world,3U).transform.origin.x == Catch::Approx(0.0F));
  CHECK(e10_instance(world,40U).transform.origin.x == 160.0F);
  REQUIRE(e10_present(*created.projection,host,100.020,1U,world,{640,480}));
  CHECK(e10_instance(world,2U).transform.origin.x == Catch::Approx(80.0F));
  CHECK(host.drain_audio().count == 0U);
}

TEST_CASE("E10 normal host retains close-range upright player through camera-only approach and return",
          "[e10][e10-near][local-capture-assets][game-api][runtime-presentation][actual-context]") {
  LocalContext context;
  // Project-authored, center-origin upright silhouette. It traverses the same
  // approved fixture opener, Studio importer, pose evaluator and materializer;
  // no installed model or engine-only handcrafted render frame is substituted.
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset,-16.0F,0.0F,-36.0F);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+12U,16.0F,0.0F,-36.0F);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+24U,0.0F,0.0F,36.0F);
  for (std::size_t i=0U;i<3U;++i)
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioNormalsOffset+i*12U,0.0F,-1.0F,0.0F);
  context.root.write("valve","models/shared.mdl",context.model);
  auto created=context.create();
  REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};
  host.reset({1U,1U,1U});
  auto first=e10_observation(1U,1.0,32.0,0.0,0.0,false);
  first.packet_entities.resize(2U);
  first.packet_entities[1U].origin={32.0,32.0,36.0};
  first.canonical_state_hash=client::runtime_observation_canonical_hash(first);
  auto current=first;
  current.publication_revision=2U;
  current.entity_metadata.source->record_identity=2U;
  current.entity_metadata.source->record_ordinal=2U;
  current.server_time_metadata.source->record_identity=2U;
  current.server_time_metadata.source->record_ordinal=2U;
  current.server_time_seconds=1.02;
  current.canonical_state_hash=client::runtime_observation_canonical_hash(current);
  client::ClientWorldState world;
  world.set_camera({{32.0F,-96.0F,64.0F},{32.0F,32.0F,64.0F},{0.0F,0.0F,1.0F}});
  REQUIRE(created.projection->reset_generation(first,world));
  REQUIRE(created.projection->project_entities(current,world));
  REQUIRE(e10_present(*created.projection,host,100.0,1U,world,{640,480}));
  const auto retained_package=world.entity_scene();
  const auto& initial_instance=e10_instance(world,2U);
  const auto retained_bounds=initial_instance.interpolated_bounds;
  const auto retained_palette=world.entity_frame()->studio_poses()[initial_instance.pose_index].bone_matrices;
  const auto retained_light=initial_instance.static_light_rgb;
  const float center_x=(retained_bounds.minimum.x+retained_bounds.maximum.x)*0.5F;
  const float center_y=(retained_bounds.minimum.y+retained_bounds.maximum.y)*0.5F;
  const float center_z=(retained_bounds.minimum.z+retained_bounds.maximum.z)*0.5F;
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if (gl && fixture::entity_opengl_fixture::capable_context()) gl->initialize_renderer();
  else {
    gl.reset();
    WARN("Project-owned near player GPU check skipped: OpenGL 3.3 Core unavailable; CPU checks still run");
  }
  if (gl) {
    auto clear_scene=client::build_render_scene(world);
    clear_scene.static_world.reset();
    clear_scene.dynamic_entities.reset();
    gl->renderer().render(clear_scene,{640,480});
  }
  const auto verify_camera=[&](const client::RenderCameraState& camera,const bool visible) {
    world.set_camera(camera);
    REQUIRE(e10_present(*created.projection,host,100.0,1U,world,{640,480}));
    REQUIRE(created.projection->summary().remote_player_count==1U);
    const auto& diagnostic=created.projection->summary().remote_players[0U];
    CHECK(diagnostic.status==hlclient::game_api::RemotePlayerPresentationStatus::ready);
    CHECK(diagnostic.pose_submitted);
    REQUIRE(world.entity_frame()->studio_instances().size()==1U);
    const auto& instance=e10_instance(world,2U);
    INFO("near visibility=" << static_cast<unsigned>(instance.visibility_status)
        << " camera=" << camera.position.x << ',' << camera.position.y << ',' << camera.position.z);
    CHECK((instance.visibility_status==hlclient::entity_render::RuntimeEntityVisibilityStatus::visible)==visible);
    CHECK(world.entity_frame()->draw_commands().empty()!=visible);
    CHECK(world.entity_scene()==retained_package);
    CHECK(world.entity_frame()->studio_poses()[instance.pose_index].bone_matrices==retained_palette);
    CHECK(instance.static_light_rgb==retained_light);
    const auto leaf=hlclient::world_spatial::WorldSpatialQuery::locate_point(
        created.projection->surface_scene()->spatial_package(),camera.position);
    std::cout << "offline_near_player source=project_owned visibility=" << static_cast<unsigned>(instance.visibility_status)
        << " bounds=" << instance.interpolated_bounds.minimum.x << ',' << instance.interpolated_bounds.minimum.y
        << ',' << instance.interpolated_bounds.minimum.z << ';' << instance.interpolated_bounds.maximum.x
        << ',' << instance.interpolated_bounds.maximum.y << ',' << instance.interpolated_bounds.maximum.z
        << " camera_leaf=" << (leaf ? std::to_string(leaf.result->leaf_index) : "unavailable")
        << " near=" << camera.near_plane;
    if (gl) {
      auto scene=client::build_render_scene(world);
      scene.static_world.reset(); // Dynamic-only GPU control; CPU PVS remains on.
      gl->renderer().render(scene,{640,480});
      const auto pixels=gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);
      REQUIRE(pixels);
      CHECK((pixels.non_clear_pixel_count>0U)==visible);
      if (visible) CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
      CHECK(glGetError()==GL_NO_ERROR);
      std::cout << " gpu_pixels=" << pixels.non_clear_pixel_count;
    } else std::cout << " gpu=skipped";
    std::cout << '\n';
  };
  for (const float distance:{128.0F,64.0F,48.0F,32.0F,24.0F,16.0F,8.0F,128.0F}) {
    INFO("center distance=" << distance);
    verify_camera({{center_x,center_y-distance,center_z+28.0F},
        {center_x,center_y,center_z+28.0F},{0.0F,0.0F,1.0F}},true);
    CHECK(world.entity_frame()->statistics().culled_by_pvs_count==0U);
    CHECK(world.entity_frame()->statistics().culled_by_frustum_count==0U);
  }
  const float edge_distance=48.0F;
  const float horizontal_half_width=std::tan(world.camera().vertical_field_of_view_radians*0.5F)*
      edge_distance*(640.0F/480.0F);
  const float edge_x=center_x+horizontal_half_width+
      (retained_bounds.maximum.x-retained_bounds.minimum.x)*0.25F;
  const client::RenderCameraState edge_camera{{edge_x,center_y-edge_distance,center_z},
      {edge_x,center_y,center_z},{0.0F,0.0F,1.0F}};
  const auto edge_frustum=hlclient::world_visibility::WorldViewFrustum::from_camera(
      {edge_camera.position,edge_camera.target,edge_camera.up,edge_camera.vertical_field_of_view_radians,
       edge_camera.near_plane,edge_camera.far_plane},{640,480});
  REQUIRE(edge_frustum);
  const auto classification=edge_frustum.frustum->classify(retained_bounds);
  REQUIRE(classification);
  CHECK(classification.classification==hlclient::world_visibility::WorldBoundsClassification::intersecting);
  verify_camera(edge_camera,true);
  verify_camera({{center_x,center_y-48.0F,center_z+28.0F},
      {center_x,center_y-49.0F,center_z+28.0F},{0.0F,0.0F,1.0F}},false);
  CHECK(world.entity_frame()->statistics().culled_by_frustum_count==1U);
  CHECK(host.drain_audio().count==0U);
  if (gl) gl->release_renderer();
}

#if HLCLIENT_HAS_GAME_HALFLIFE
TEST_CASE("E11 approved installed player and p-model slots use normal merge and OpenGL",
          "[e11][e12][installed-pmodels][local-capture-assets][actual-context]") {
  std::filesystem::path root;
  wchar_t* value=nullptr;std::size_t size=0U;
  if(_wdupenv_s(&value,&size,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
  std::free(value);
  if(root.empty()) SKIP("No opt-in read-only local game root");
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("Only exact prepared research root authorized for optional p-model control");
  const std::array tokens{"maps/crossfire.bsp","models/player.mdl","models/p_9mmhandgun.mdl","models/p_crowbar.mdl"};
  std::array<std::filesystem::file_time_type,4U> times;
  for(std::size_t i=0;i<tokens.size();++i) {
    const auto file=root/"valve"/tokens[i];
    if(!std::filesystem::is_regular_file(file)) SKIP("Required optional approved p-model/map absent");
    times[i]=std::filesystem::last_write_time(file);
  }
  const auto resources=readiness::parse_resource_list({
      {2U,tokens[0],9U,0U,0U},{2U,tokens[1],7U,0U,0U},{2U,tokens[2],21U,0U,0U},{2U,tokens[3],22U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info(tokens[0]),root,"valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  if(!created.projection) SKIP("Optional approved production provider did not reach readiness");
  const auto body=created.projection->studio_model_data(7U);REQUIRE(body);
  const auto& map=*created.projection->surface_scene()->world_package();
  hlclient::assets::AssetVector3 origin{};bool floor_found=false;
  for(const auto& surface:map.surface_ranges()) {
    for(std::size_t at=surface.first_index;at+2U<surface.first_index+surface.index_count;at+=3U) {
      const auto& a=map.vertices()[map.indices()[at]];
      if(a.normal.z<0.9F) continue;
      const auto b=map.vertices()[map.indices()[at+1U]].position,c=map.vertices()[map.indices()[at+2U]].position;
      origin={(a.position.x+b.x+c.x)/3.0F,(a.position.y+b.y+c.y)/3.0F,(a.position.z+b.z+c.z)/3.0F+36.0F};
      floor_found=true;break;
    }
    if(floor_found) break;
  }
  REQUIRE(floor_found);
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if(gl && fixture::entity_opengl_fixture::capable_context()) gl->initialize_renderer();
  else {gl.reset();WARN("Optional installed p-model GPU unavailable; approved merge assertions still run");}
  client::ClientWorldState world;
  world.set_camera({{origin.x+160.0F,origin.y,origin.z+12.0F},{origin.x,origin.y,origin.z+12.0F},{0,0,1}});
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});
  hlclient::game_api::ScriptedEventBinding glock_binding;glock_binding.index=71;
  host.set_movement_audio_time_origin(0);
  constexpr char glock_event_name[]="events/glock1.sc";
  std::copy_n(glock_event_name,sizeof(glock_event_name),glock_binding.name.begin());
  host.configure_scripted_events(std::span{&glock_binding,1});
  std::uint64_t record=1U;
  for(const auto slot:{21U,22U}) {
    REQUIRE(created.projection->studio_model_data(slot));
    const auto inspected=created.projection->studio_model_data(slot);
    std::cout << "e12_asset_inspection virtual_resource=" << tokens[slot==21U?2U:3U]
      << " attachments=" << inspected->attachments.size() << " bones=" << inspected->bones.size() << '\n';
    for(std::size_t at=0;at<inspected->attachments.size();++at) {
      const auto& a=inspected->attachments[at];
      std::cout << "attachment=" << at << " name=" << a.name << " type=" << a.source_type
        << " bone=" << a.bone_index << " origin=" << a.local_origin.x << ',' << a.local_origin.y << ',' << a.local_origin.z << '\n';
    }
    for(std::size_t at=0;at<inspected->bones.size();++at) {
      const auto& b=inspected->bones[at];
      hlclient::assets::AssetVector3 lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};std::size_t count=0;
      for(const auto& sub:inspected->submodels) for(const auto& v:sub.vertices) if(v.position_bone_index==at) {
        ++count;lo.x=std::min(lo.x,v.source_position.x);lo.y=std::min(lo.y,v.source_position.y);lo.z=std::min(lo.z,v.source_position.z);
        hi.x=std::max(hi.x,v.source_position.x);hi.y=std::max(hi.y,v.source_position.y);hi.z=std::max(hi.z,v.source_position.z);
      }
      std::cout << "bone=" << at << " name=" << b.name << " parent=" << b.parent_index
        << " translation=" << b.default_translation.x << ',' << b.default_translation.y << ',' << b.default_translation.z
        << " rotation=" << b.default_rotation_radians.x << ',' << b.default_rotation_radians.y << ',' << b.default_rotation_radians.z
        << " vertices=" << count << " local_bounds=" << lo.x << ',' << lo.y << ',' << lo.z << ':' << hi.x << ',' << hi.y << ',' << hi.z << '\n';
    }
    if(slot==21U) for(const auto& sub:inspected->submodels) for(const auto& v:sub.vertices)
      if(v.source_position.x>3.7F || v.source_position.x<-3.7F)
        std::cout << "glock_end_vertex=" << v.source_position.x << ',' << v.source_position.y << ',' << v.source_position.z << '\n';
    auto state=e10_observation(record++,1.0,origin.x,0,0,false);state.packet_entities.resize(2U);
    auto& player=state.packet_entities[1];player.origin={origin.x,origin.y,origin.z};player.weapon_model_index=slot;
    player.animation_time_seconds=1.0;player.frame_rate=1.0;player.gait_sequence=1U;
    for(auto& c:player.controllers) c=127U;for(auto& b:player.blending) b=127U;
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    REQUIRE(created.projection->reset_generation(state,world));
    REQUIRE(e10_present(*created.projection,host,100,1U,world,{640,480}));
    REQUIRE(world.entity_frame()->studio_instances().size()==2U);
    const auto& child=world.entity_frame()->studio_instances()[1];
    CHECK(child.entity_number==2U);CHECK(child.subinstance_ordinal==1U);
    const auto& diagnostic=created.projection->summary().remote_players[0];
    CHECK(diagnostic.attachment_matched_bones>0U);
    const auto sockets=created.projection->remote_weapon_poses();
    REQUIRE(sockets.size()==1U);CHECK(sockets[0].entity==2U);CHECK(sockets[0].model_slot==slot);
    if(slot==21U) {
      CHECK(sockets[0].source==hlclient::game_api::RemoteSocketSource::validated_pmodel_socket);
      CHECK(sockets[0].muzzle_bone==11U);CHECK(sockets[0].shell_bone==11U);
      CHECK(std::isfinite(sockets[0].muzzle.origin.x));CHECK(std::isfinite(sockets[0].shell.origin.z));
      CHECK(sockets[0].muzzle.origin.x!=sockets[0].shell.origin.x);
    } else {
      CHECK_FALSE(sockets[0].semantic_match);
      CHECK(sockets[0].source==hlclient::game_api::RemoteSocketSource::unavailable);
    }
    // The actual approved pose goes through the same committed module event seam.
    hlclient::game_api::CommittedScriptedEvent event;
    event.generation=1;event.record=state.entity_metadata.source->record_identity;
    event.record_ordinal=state.entity_metadata.source->record_ordinal;
    event.emitter_entity=2;event.event_index=71;event.weapon_model_index=slot;
    event.origin=origin;event.received_at_seconds=100;
    host.committed_scripted_event(event);
    const auto shot=host.drain_remote_effects(100);REQUIRE(shot.count==1U);
    if(slot==21U) {
      CHECK(shot.effects[0].pose.status==hlclient::game_api::RemotePoseResolution::resolved);
      REQUIRE(shot.effects[0].flash);REQUIRE(shot.effects[0].shell);
      CHECK(shot.effects[0].flash->origin.x==sockets[0].muzzle.origin.x);
      CHECK(shot.effects[0].flash->origin.z==sockets[0].muzzle.origin.z);
      CHECK(shot.effects[0].shell->origin.y==sockets[0].shell.origin.y);
    } else {
      CHECK(shot.effects[0].pose.status==hlclient::game_api::RemotePoseResolution::weapon_mismatch);
      CHECK_FALSE(shot.effects[0].flash);CHECK_FALSE(shot.effects[0].shell);
    }
    if(gl) {
      auto scene=client::build_render_scene(world);scene.static_world.reset();
      gl->renderer().render(scene,{640,480});
      const auto pixels=gl->renderer().observe_framebuffer({640,480},scene.clear_color);REQUIRE(pixels);
      CHECK(pixels.non_clear_pixel_count>0U);CHECK(gl->renderer().entity_statistics().studio_draw_count>=2U);
      const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
      // Same normal package and evaluated body pose; remove only the child
      // instance for an independent A/B, never a second gameplay runtime.
      hlclient::entity_render::EntityRenderFrameBuildInput input;
      input.resource_id=1U;input.resource_revision=1U;input.interpolation=world.entity_frame()->interpolation();
      input.studio_poses.assign(world.entity_frame()->studio_poses().begin(),world.entity_frame()->studio_poses().end());
      input.studio_instances.push_back(world.entity_frame()->studio_instances()[0]);
      auto body_only=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*scene.dynamic_entities->package,std::move(input));
      REQUIRE(body_only);
      scene.dynamic_entities->frame=std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*body_only.frame));
      gl->renderer().render(scene,{640,480});
      const auto without=gl->renderer().observe_framebuffer({640,480},scene.clear_color);REQUIRE(without);
      CHECK(pixels.color_signature!=without.color_signature);
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
      CHECK(glGetError()==GL_NO_ERROR);
    }
    std::cout << "offline_pmodel_control virtual_resource=" << tokens[slot==21U?2U:3U]
      << " exact_slot=" << slot << " matched=" << diagnostic.attachment_matched_bones
      << " unmatched=" << diagnostic.attachment_bone_count-diagnostic.attachment_matched_bones
      << " merge=ready live_state=false\n";
  }
  for(std::size_t i=0;i<tokens.size();++i) CHECK(std::filesystem::last_write_time(root/"valve"/tokens[i])==times[i]);
  if(gl) gl->release_renderer();host.teardown();
}
TEST_CASE("Opt-in exact-root player traverses normal approved bundle and Half-Life presentation",
          "[e10][e10-near][installed-player-production][local-capture-assets][game-api]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value = nullptr;
  std::size_t count = 0U;
  if (_wdupenv_s(&value, &count, L"HLCLIENT_LOCAL_GAME_ROOT") == 0 && value) root = value;
  std::free(value);
#else
  if (const char* value = std::getenv("HLCLIENT_LOCAL_GAME_ROOT")) root = value;
#endif
  if (root.empty()) SKIP("No opt-in read-only local game root");
  const std::filesystem::path approved_root{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"};
  if (root.lexically_normal() != approved_root.lexically_normal())
    SKIP("Optional production player control authorizes only the exact prepared research root");
  const auto map_path = root / "valve/maps/crossfire.bsp";
  const auto model_path = root / "valve/models/player.mdl";
  if (!std::filesystem::is_regular_file(map_path)) SKIP("maps/crossfire.bsp is absent");
  if (!std::filesystem::is_regular_file(model_path)) SKIP("models/player.mdl is absent");
  const auto map_time = std::filesystem::last_write_time(map_path);
  const auto model_time = std::filesystem::last_write_time(model_path);
  // Project-owned slot metadata, not a fabricated stock server capture. The
  // normal provider performs exact-root approved source/companion materialization.
  const auto resources = readiness::parse_resource_list({
      {2U, "maps/crossfire.bsp", 9U, 0U, 0U},
      {2U, "models/player.mdl", 7U, 0U, 0U}});
  auto created = app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/crossfire.bsp"), root, "valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  if (!created.projection) {
    INFO((created.error ? created.error->context : "no typed provider error"));
    SKIP("Optional normal provider did not reach approved map/player readiness");
  }
  client::ClientWorldState world;
  auto first = e10_observation(1U, 1.0, 0.0, 0.0, 0.0, false);
  first.packet_entities.resize(2U); // Only local1 and remote2 use the approved model.
  REQUIRE(created.projection->surface_scene());
  const auto& map = *created.projection->surface_scene()->world_package();
  const auto vertices = map.vertices();
  const auto indices = map.indices();
  hlclient::assets::AssetVector3 position{32.0F, 32.0F, 24.0F};
  bool selected_lit_floor = false;
  for (const auto& surface : map.surface_ranges()) {
    const auto* binding = map.lightmaps().binding_for_surface(surface.source_world_surface_index);
    if (!binding || binding->status != hlclient::assets::WorldSurfaceLightmapBindingStatus::resolved)
      continue;
    for (std::size_t i = surface.first_index;
         i + 2U < static_cast<std::size_t>(surface.first_index) + surface.index_count; i += 3U) {
      const auto& a = vertices[indices[i]];
      const auto& b = vertices[indices[i + 1U]];
      const auto& c = vertices[indices[i + 2U]];
      if (a.normal.z < 0.9F || b.normal.z < 0.9F || c.normal.z < 0.9F) continue;
      position = {(a.position.x + b.position.x + c.position.x) / 3.0F,
          (a.position.y + b.position.y + c.position.y) / 3.0F,
          (a.position.z + b.position.z + c.position.z) / 3.0F + 1.0F};
      selected_lit_floor = true;
      break;
    }
    if (selected_lit_floor) break;
  }
  for (auto& entity : first.packet_entities) {
    entity.origin = {static_cast<double>(position.x), static_cast<double>(position.y),
        static_cast<double>(position.z)};
    entity.angles = {12.0, 35.0, 0.0};
    entity.sequence = 0U;
    entity.frame = 32.0;
    entity.animation_time_seconds = 0.98;
    entity.frame_rate = 1.0;
    entity.gait_sequence = 1U;
    entity.body = 0U;
    entity.skin = 0;
    entity.controllers.fill(64U);
    entity.blending = {64U, 192U};
    entity.effects = 0U;
  }
  first.canonical_state_hash = client::runtime_observation_canonical_hash(first);
  REQUIRE(client::valid_runtime_observation(first));
  REQUIRE(created.projection->reset_generation(first, world));
  if (created.projection->summary().imported_studio == 0U) {
    for (const auto reason : {app::ReplayLocalVisualStatus::dependency_missing,
             app::ReplayLocalVisualStatus::missing_asset, app::ReplayLocalVisualStatus::unsafe_asset,
             app::ReplayLocalVisualStatus::ambiguous_asset, app::ReplayLocalVisualStatus::unsupported_asset,
             app::ReplayLocalVisualStatus::import_failed}) {
      if (created.projection->summary().coverage.contains(reason))
        SKIP(std::string{"Optional approved player bundle unavailable: "} + std::string{app::to_string(reason)});
    }
    SKIP("Optional approved player bundle did not produce a Studio asset");
  }
  const auto metadata = created.projection->presentation_model(first.generation, 7U);
  REQUIRE(metadata);
  REQUIRE(metadata->sequences.size() > 1U);
  REQUIRE(metadata->sequences[0U].frame_count > 0U);
  REQUIRE(metadata->sequences[1U].frame_count > 0U);
  REQUIRE(metadata->supported_bodies[0U]);
  REQUIRE(world.entity_scene());
  REQUIRE(world.entity_scene()->studio_assets().size() == 1U);
  const auto actual_bones = world.entity_scene()->studio_assets()[0U]->statistics().bone_count;
  REQUIRE(actual_bones > 0U);
  REQUIRE(actual_bones <= hlclient::game_api::kMaximumRemotePlayerBones);
  const auto skeleton = created.projection->studio_model_data(7U);
  REQUIRE(skeleton);
  REQUIRE(skeleton->bones.size() == actual_bones);
  // Optional read-only evidence from the same approved imported model. Bound
  // the work and report numeric track behavior; do not infer a user's exact
  // glitch merely because a model contains a crouch sequence or an angle wrap.
  std::size_t crouch_sequences=0U,rotation_intervals=0U,angle_wrap_intervals=0U;
  double maximum_rotation_step=0.0;
  constexpr std::size_t maximum_rotation_intervals=65'536U;
  for(const auto& sequence:skeleton->sequences) {
    if(sequence.label.find("crouch")==std::string::npos) continue;
    ++crouch_sequences;
    for(const auto& blend:sequence.animation_blends) for(const auto& track:blend.bone_tracks)
      for(std::size_t axis=3U;axis<6U;++axis)
        for(std::uint32_t frame=0U;frame+1U<sequence.frame_count &&
            rotation_intervals<maximum_rotation_intervals;++frame) {
          const auto sample=hlclient::goldsrc::studio::StudioFractionalAnimationChannelSampler::
              sample_fractional_frame(track.channels[axis],static_cast<double>(frame)+0.5);
          REQUIRE(sample);REQUIRE(sample.sample);
          const auto difference=std::abs(static_cast<double>(sample.sample->scaled_one)-
              sample.sample->scaled_zero);
          CHECK(std::isfinite(difference));
          maximum_rotation_step=std::max(maximum_rotation_step,difference);
          if(difference>std::acos(-1.0)) ++angle_wrap_intervals;
          ++rotation_intervals;
        }
  }
  std::cout << "offline_crouch_tracks virtual_resource=models/player.mdl sequences=" << crouch_sequences
      << " rotation_intervals=" << rotation_intervals << " angle_wrap_intervals=" << angle_wrap_intervals
      << " maximum_step_degrees=" << maximum_rotation_step*180.0/std::acos(-1.0)
      << " interval_limit=" << maximum_rotation_intervals << " live_state=false\n";
  // The already approved normal bundle is the sole source of this optional
  // diagnostic. Model-space names and bounded numeric metadata only: no native
  // path, source identity, asset bytes or second parser escapes the provider.
  const auto bounded_name = [](const std::string& name) {
    std::string result;
    const auto length = std::min<std::size_t>(name.size(),64U);
    result.reserve(length);
    for (std::size_t i=0U;i<length;++i) {
      const auto c = static_cast<unsigned char>(name[i]);
      result.push_back(c >= 32U && c <= 126U && c != '"' && c != '\\' ?
          static_cast<char>(c) : '?');
    }
    return result;
  };
  std::cout << "offline_player_skeleton virtual_resource=models/player.mdl bones="
      << skeleton->bones.size() << " controllers=" << skeleton->bone_controllers.size() << '\n';
  for (std::size_t i=0U;i<skeleton->bones.size();++i) {
    const auto& bone = skeleton->bones[i];
    std::cout << "offline_player_skeleton bone=" << i << " name=\"" << bounded_name(bone.name)
        << "\" parent=" << bone.parent_index << " controller_records=";
    for (std::size_t axis=0U;axis<bone.controller_indices.size();++axis)
      std::cout << (axis ? "," : "") << bone.controller_indices[axis];
    std::cout << '\n';
  }
  std::size_t controller_count = 0U;
  for (std::size_t i=0U;i<skeleton->bone_controllers.size() && controller_count<4U;++i) {
    const auto& controller = skeleton->bone_controllers[i];
    if (controller.controller_index<0 || controller.controller_index>=4) continue;
    ++controller_count;
    std::cout << "offline_player_skeleton controller_record=" << i
        << " input=" << controller.controller_index << " bone=" << controller.bone_index
        << " type=" << controller.source_type << " start=" << controller.start
        << " end=" << controller.end << " rest=" << controller.rest << '\n';
  }
  std::cout.flush();
  world.set_camera({{position.x, position.y - 100.0F, position.z + 40.0F},
      {position.x, position.y, position.z + 24.0F}, {0.0F, 0.0F, 1.0F}});
  hlclient::game_api::GameClientHost host{
      hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1U, 1U, 1U});
  auto current = first;
  current.publication_revision = 2U;
  current.entity_metadata.source->record_identity = 2U;
  current.entity_metadata.source->record_ordinal = 2U;
  current.server_time_metadata.source->record_identity = 2U;
  current.server_time_metadata.source->record_ordinal = 2U;
  current.server_time_seconds = 1.02;
  current.packet_entities[1U].origin.x = *first.packet_entities[1U].origin.x + 0.2;
  current.packet_entities[1U].angles = {15.0, 40.0, 0.0};
  current.packet_entities[1U].frame = 48.0;
  current.packet_entities[1U].animation_time_seconds = 1.0;
  current.packet_entities[1U].controllers.fill(80U);
  current.packet_entities[1U].blending = {96U, 176U};
  current.canonical_state_hash = client::runtime_observation_canonical_hash(current);
  REQUIRE(client::valid_runtime_observation(current));
  REQUIRE(created.projection->project_entities(current, world));
  for (const auto clock : {100.0, 100.005, 100.010}) {
    REQUIRE(e10_present(*created.projection, host, clock, 1U, world, {640,480}));
    const auto& summary = created.projection->summary();
    REQUIRE(summary.remote_player_count == 1U);
    const auto& diagnostic = summary.remote_players[0U];
    INFO("approved-player status=" << static_cast<unsigned>(diagnostic.status));
    // Once the bundle is ready, strict skeleton/sequence incompatibility is a
    // failed positive control, never disguised as an optional resource skip.
    REQUIRE(diagnostic.status == hlclient::game_api::RemotePlayerPresentationStatus::ready);
    CHECK(diagnostic.pose_submitted);
    CHECK(diagnostic.intent.bone_count == actual_bones);
    REQUIRE(diagnostic.intent.gait_sample);
    REQUIRE(world.entity_frame()->studio_instances().size() == 1U);
    const auto& instance = e10_instance(world, 2U);
    CHECK(instance.body_value == 0U);
    CHECK(instance.skin_family_index == 0U);
    const auto& pose = world.entity_frame()->studio_poses()[instance.pose_index];
    REQUIRE(pose.bone_matrices.size() == actual_bones);
    for (const auto& bone : pose.bone_matrices)
      for (const auto component : bone) CHECK(std::isfinite(component));
    if (instance.static_light_rgb) for (const auto component : *instance.static_light_rgb)
      CHECK(std::isfinite(component));
  }
  // This optional GPU control renders the actual approved player asset from
  // the normal host/materializer. The retained BSP still provides production
  // lighting/PVS; static-world GPU drawing alone is removed so arbitrary map
  // geometry cannot occlude the controlled camera/model A/B observation.
  auto gl = fixture::entity_opengl_fixture::try_context(640,480);
  if (!gl || !fixture::entity_opengl_fixture::capable_context()) {
    WARN("Optional actual-player dynamic-only GPU control skipped: OpenGL 3.3 Core unavailable");
    std::cout << "offline_player_gpu_control virtual_resource=models/player.mdl gpu=skipped"
        " reason=opengl_3_3_core_unavailable scope=dynamic_only\n";
  } else {
    gl->initialize_renderer();
    REQUIRE(e10_present(*created.projection,host,100.020,1U,world,{640,480}));
    const auto retained_package = world.entity_scene();
    const auto& first_instance = e10_instance(world,2U);
    const auto first_palette = world.entity_frame()->studio_poses()[first_instance.pose_index].bone_matrices;
    const auto first_light = first_instance.static_light_rgb;
    const auto first_origin = first_instance.transform.origin;
    auto scene_a = client::build_render_scene(world);
    scene_a.static_world.reset();
    REQUIRE(scene_a.dynamic_entities);
    REQUIRE_FALSE(scene_a.dynamic_entities->frame->draw_commands().empty());
    const auto stable_camera = scene_a.camera;
    auto baseline_scene = scene_a;
    baseline_scene.dynamic_entities.reset();
    gl->renderer().render(baseline_scene,{640,480});
    const auto baseline = gl->renderer().observe_framebuffer({640,480},scene_a.clear_color,true);
    REQUIRE(baseline);
    gl->renderer().render(scene_a,{640,480});
    const auto pixels_a = gl->renderer().observe_framebuffer({640,480},scene_a.clear_color,true);
    REQUIRE(pixels_a);
    CHECK(pixels_a.non_clear_pixel_count > 0U);
    CHECK(pixels_a.rgba8 != baseline.rgba8);
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count == 1U);
    // Independent center-origin placement for the crossfire near control:
    // the chosen upward face supplied floor+1 above, whereas a standing player
    // body center is floor+36. Do not rewrite the older A/B placement/history.
    const auto original_camera=world.camera();
    auto near_state=current;
    near_state.publication_revision=3U;
    near_state.entity_metadata.source->record_identity=3U;
    near_state.entity_metadata.source->record_ordinal=3U;
    near_state.server_time_metadata.source->record_identity=3U;
    near_state.server_time_metadata.source->record_ordinal=3U;
    near_state.server_time_seconds=1.04;
    near_state.packet_entities[1U].origin.z=*current.packet_entities[1U].origin.z+
        (selected_lit_floor ? 35.0 : 0.0);
    near_state.canonical_state_hash=client::runtime_observation_canonical_hash(near_state);
    REQUIRE(client::valid_runtime_observation(near_state));
    REQUIRE(created.projection->project_entities(near_state,world));
    REQUIRE(e10_present(*created.projection,host,110.0,1U,world,{640,480}));
    REQUIRE(e10_present(*created.projection,host,110.020,1U,world,{640,480}));
    const auto near_origin=e10_instance(world,2U).transform.origin;
    const auto near_pose=world.entity_frame()->studio_poses()[e10_instance(world,2U).pose_index].bone_matrices;
    const auto near_light=e10_instance(world,2U).static_light_rgb;
    for (const float distance:{128.0F,64.0F,48.0F,32.0F,24.0F,16.0F,8.0F,128.0F}) {
      INFO("approved player center distance=" << distance);
      const client::RenderCameraState near_camera{{near_origin.x,near_origin.y-distance,near_origin.z+28.0F},
          {near_origin.x,near_origin.y,near_origin.z+28.0F},{0.0F,0.0F,1.0F}};
      world.set_camera(near_camera);
      REQUIRE(e10_present(*created.projection,host,110.020,1U,world,{640,480}));
      REQUIRE(created.projection->summary().remote_player_count==1U);
      const auto& diagnostic=created.projection->summary().remote_players[0U];
      CHECK(diagnostic.status==hlclient::game_api::RemotePlayerPresentationStatus::ready);
      CHECK(diagnostic.pose_submitted);
      REQUIRE(world.entity_frame()->studio_instances().size()==1U); // Local receiving player excluded.
      const auto& instance=e10_instance(world,2U);
      CHECK(world.entity_frame()->studio_poses()[instance.pose_index].bone_matrices==near_pose);
      CHECK(instance.static_light_rgb==near_light);
      CHECK(world.entity_scene()==retained_package);
      const auto leaf=hlclient::world_spatial::WorldSpatialQuery::locate_point(
          created.projection->surface_scene()->spatial_package(),near_camera.position);
      std::cout << "offline_near_player source=approved_player virtual_resource=models/player.mdl"
          << " map=maps/crossfire.bsp center_distance=" << distance
          << " visibility=" << static_cast<unsigned>(instance.visibility_status)
          << " bounds=" << instance.interpolated_bounds.minimum.x << ',' << instance.interpolated_bounds.minimum.y
          << ',' << instance.interpolated_bounds.minimum.z << ';' << instance.interpolated_bounds.maximum.x
          << ',' << instance.interpolated_bounds.maximum.y << ',' << instance.interpolated_bounds.maximum.z
          << " camera_leaf=" << (leaf ? std::to_string(leaf.result->leaf_index) : "unavailable")
          << " pvs_available=" << (leaf && leaf.result->pvs_available)
          << " solid_or_special=" << (leaf && leaf.result->solid_or_special)
          << " near=" << near_camera.near_plane;
      CHECK(instance.visibility_status==hlclient::entity_render::RuntimeEntityVisibilityStatus::visible);
      REQUIRE_FALSE(world.entity_frame()->draw_commands().empty());
      auto near_scene=client::build_render_scene(world);
      near_scene.static_world.reset(); // Dynamic-only GPU; normal CPU PVS is unchanged.
      gl->renderer().render(near_scene,{640,480});
      const auto near_pixels=gl->renderer().observe_framebuffer({640,480},near_scene.clear_color,true);
      REQUIRE(near_pixels);
      const auto& b=instance.interpolated_bounds;
      const auto& eye=near_camera.position;
      const bool inside_bounds=eye.x>=b.minimum.x && eye.x<=b.maximum.x &&
          eye.y>=b.minimum.y && eye.y<=b.maximum.y && eye.z>=b.minimum.z && eye.z<=b.maximum.z;
      // At8 an observer may overlap the geometry; report its actual pixels,
      // without declaring absence at an inside-surface viewpoint a GPU defect.
      if (distance>=16.0F || !inside_bounds) CHECK(near_pixels.non_clear_pixel_count>0U);
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
      CHECK(glGetError()==GL_NO_ERROR);
      std::cout << " gpu_pixels=" << near_pixels.non_clear_pixel_count
          << " camera_inside_bounds=" << inside_bounds << " live_state=false\n";
    }
    // No-entity frames intentionally release an OpenGlRenderer's entity cache.
    // Keep A/B baselines in their own renderer and keep this extended control
    // out of the older pose A/B renderer; all share the existing current context.
    // Constructors initialize production GL resources; reset below destroys
    // them explicitly while that context remains current.
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
    auto reciprocal_player_renderer=
        std::make_unique<hlclient::renderer::opengl::OpenGlRenderer>();
    auto reciprocal_baseline_renderer=
        std::make_unique<hlclient::renderer::opengl::OpenGlRenderer>();
    // Independent reciprocal control at the retained two-client run's final
    // touching positions. The two local samples have different server times;
    // this is NOT a replay or a claim about the absent remote wire fields.
    // In particular sequence/controllers/effects below are explicit fixtures.
    // Main's eye was not retained: only its presented origin + the standing
    // 28-unit view offset constructs that camera. Peer eye/yaw/pitch were retained.
    struct ReciprocalControl {
      std::uint32_t receiving;
      hlclient::assets::AssetVector3 center;
      hlclient::assets::AssetVector3 eye;
      double yaw;
      double pitch;
      const char* eye_source;
    };
    const std::array reciprocal_controls{
        ReciprocalControl{1U,{201.984375F,1212.109375F,-1819.968750F},
            {201.984375F,1212.109375F,-1819.991821F+28.0F},89.9,1.6,
            "constructed_presented_origin_plus_standing_offset"},
        ReciprocalControl{2U,{202.515625F,1244.140625F,-1819.968750F},
            {202.515625F,1244.140625F,-1791.973267F},-94.3,-3.7,
            "retained_local_eye"}};
    const auto set_reciprocal_record = [](client::RuntimeClientObservationState& reciprocal_state,
        const std::uint64_t reciprocal_record, const double reciprocal_seconds) {
      reciprocal_state.publication_revision=reciprocal_record;
      reciprocal_state.entity_metadata.source->record_identity=reciprocal_record;
      reciprocal_state.entity_metadata.source->record_ordinal=static_cast<std::size_t>(reciprocal_record);
      reciprocal_state.entity_metadata.source->source_transport_sequence=static_cast<std::uint32_t>(reciprocal_record+42U);
      reciprocal_state.server_time_metadata.source=reciprocal_state.entity_metadata.source;
      reciprocal_state.server_time_seconds=reciprocal_seconds;
      reciprocal_state.canonical_state_hash=client::runtime_observation_canonical_hash(reciprocal_state);
    };
    for (const auto& reciprocal_control:reciprocal_controls) {
      INFO("reciprocal receiving entity=" << reciprocal_control.receiving);
      host.reset({1U,1U,1U});
      auto reciprocal_first=first;
      const auto reciprocal_remote=reciprocal_control.receiving==1U ? 2U : 1U;
      const auto reciprocal_remote_index=static_cast<std::size_t>(reciprocal_remote-1U);
      const double reciprocal_sign=reciprocal_control.receiving==1U ? 1.0 : -1.0;
      for (std::size_t reciprocal_entity_index=0U;reciprocal_entity_index<2U;++reciprocal_entity_index) {
        auto& reciprocal_entity=reciprocal_first.packet_entities[reciprocal_entity_index];
        const auto& reciprocal_source=reciprocal_controls[reciprocal_entity_index];
        reciprocal_entity.origin={static_cast<double>(reciprocal_source.center.x),
            static_cast<double>(reciprocal_source.center.y),static_cast<double>(reciprocal_source.center.z)};
        reciprocal_entity.angles={reciprocal_source.pitch,reciprocal_source.yaw,0.0};
        reciprocal_entity.sequence=0U;
        reciprocal_entity.frame=0.0;
        reciprocal_entity.animation_time_seconds=5.0;
        reciprocal_entity.frame_rate=0.0;
        reciprocal_entity.gait_sequence=0U;
        reciprocal_entity.controllers.fill(127U);
        reciprocal_entity.blending={128U,128U};
        reciprocal_entity.body=0U;
        reciprocal_entity.skin=0;
        reciprocal_entity.effects=0U;
        reciprocal_entity.render_mode=0U;
      }
      const auto reciprocal_contact_y=*reciprocal_first.packet_entities[reciprocal_remote_index].origin.y;
      reciprocal_first.packet_entities[reciprocal_remote_index].origin.y=
          reciprocal_contact_y+reciprocal_sign*96.0;
      const auto reciprocal_record_base=100U+static_cast<std::uint64_t>(reciprocal_control.receiving)*20U;
      set_reciprocal_record(reciprocal_first,reciprocal_record_base,5.0);
      REQUIRE(client::valid_runtime_observation(reciprocal_first));
      REQUIRE(created.projection->reset_generation(reciprocal_first,world));
      const auto reciprocal_forward=hlclient::gameplay_camera::forward_from_yaw_pitch(
          reciprocal_control.yaw,reciprocal_control.pitch);
      REQUIRE(reciprocal_forward);
      const client::RenderCameraState reciprocal_camera{reciprocal_control.eye,
          {reciprocal_control.eye.x+reciprocal_forward->x,
              reciprocal_control.eye.y+reciprocal_forward->y,
              reciprocal_control.eye.z+reciprocal_forward->z},{0.0F,0.0F,1.0F}};
      world.set_camera(reciprocal_camera);
      std::size_t reciprocal_step=0U;
      for (const double reciprocal_separation_offset:{96.0,48.0,24.0,8.0,0.0,8.0,24.0,48.0,96.0}) {
        INFO("reciprocal separation offset=" << reciprocal_separation_offset);
        auto reciprocal_state=reciprocal_first;
        ++reciprocal_step;
        const double reciprocal_seconds=5.0+0.04*static_cast<double>(reciprocal_step);
        reciprocal_state.packet_entities[reciprocal_remote_index].origin.y=
            reciprocal_contact_y+reciprocal_sign*reciprocal_separation_offset;
        set_reciprocal_record(reciprocal_state,reciprocal_record_base+reciprocal_step,reciprocal_seconds);
        REQUIRE(client::valid_runtime_observation(reciprocal_state));
        REQUIRE(created.projection->project_entities(reciprocal_state,world));
        const double reciprocal_clock=200.0+0.08*static_cast<double>(reciprocal_step);
        REQUIRE(e10_present(*created.projection,host,reciprocal_clock,
            reciprocal_control.receiving,world,{640,480}));
        REQUIRE(e10_present(*created.projection,host,reciprocal_clock+0.04,
            reciprocal_control.receiving,world,{640,480}));
        REQUIRE(created.projection->summary().remote_player_count==1U);
        CHECK(created.projection->summary().remote_players[0U].entity==reciprocal_remote);
        CHECK(created.projection->summary().remote_players[0U].status==
            hlclient::game_api::RemotePlayerPresentationStatus::ready);
        CHECK(created.projection->summary().remote_players[0U].pose_submitted);
        REQUIRE(world.entity_frame()->studio_instances().size()==1U);
        const auto& reciprocal_instance=e10_instance(world,reciprocal_remote);
        CHECK(reciprocal_instance.visibility_status==
            hlclient::entity_render::RuntimeEntityVisibilityStatus::visible);
        CHECK(reciprocal_instance.transform.origin.y==Catch::Approx(
            *reciprocal_state.packet_entities[reciprocal_remote_index].origin.y).margin(0.001));
        REQUIRE_FALSE(world.entity_frame()->draw_commands().empty());
        const auto reciprocal_leaf=hlclient::world_spatial::WorldSpatialQuery::locate_point(
            created.projection->surface_scene()->spatial_package(),reciprocal_camera.position);
        const auto reciprocal_remote_leaf=hlclient::world_spatial::WorldSpatialQuery::locate_point(
            created.projection->surface_scene()->spatial_package(),reciprocal_instance.transform.origin);
        const auto reciprocal_rgb=reciprocal_instance.static_light_rgb.value_or(
            std::array<float,3>{1.0F,1.0F,1.0F});
        const auto& reciprocal_bounds=reciprocal_instance.interpolated_bounds;
        const bool reciprocal_inside_bounds=reciprocal_camera.position.x>=reciprocal_bounds.minimum.x &&
            reciprocal_camera.position.x<=reciprocal_bounds.maximum.x &&
            reciprocal_camera.position.y>=reciprocal_bounds.minimum.y &&
            reciprocal_camera.position.y<=reciprocal_bounds.maximum.y &&
            reciprocal_camera.position.z>=reciprocal_bounds.minimum.z &&
            reciprocal_camera.position.z<=reciprocal_bounds.maximum.z;
        for (const bool reciprocal_full_world:{false,true}) {
          auto reciprocal_scene=client::build_render_scene(world);
          if (!reciprocal_full_world) reciprocal_scene.static_world.reset();
          if (reciprocal_full_world) REQUIRE(reciprocal_scene.static_world);
          REQUIRE(reciprocal_scene.dynamic_entities);
          auto reciprocal_baseline=reciprocal_scene;
          reciprocal_baseline.dynamic_entities.reset();
          reciprocal_baseline_renderer->render(reciprocal_baseline,{640,480});
          const auto reciprocal_baseline_pixels=reciprocal_baseline_renderer->observe_framebuffer(
              {640,480},reciprocal_scene.clear_color,true);
          REQUIRE(reciprocal_baseline_pixels);
          reciprocal_player_renderer->render(reciprocal_scene,{640,480});
          const auto reciprocal_pixels=reciprocal_player_renderer->observe_framebuffer(
              {640,480},reciprocal_scene.clear_color,true);
          REQUIRE(reciprocal_pixels);
          REQUIRE(reciprocal_pixels.rgba8.size()==reciprocal_baseline_pixels.rgba8.size());
          std::size_t reciprocal_changed_pixels=0U;
          for (std::size_t reciprocal_pixel=0U;reciprocal_pixel<reciprocal_pixels.rgba8.size();reciprocal_pixel+=4U)
            reciprocal_changed_pixels+=reciprocal_pixels.rgba8[reciprocal_pixel]!=reciprocal_baseline_pixels.rgba8[reciprocal_pixel] ||
                reciprocal_pixels.rgba8[reciprocal_pixel+1U]!=reciprocal_baseline_pixels.rgba8[reciprocal_pixel+1U] ||
                reciprocal_pixels.rgba8[reciprocal_pixel+2U]!=reciprocal_baseline_pixels.rgba8[reciprocal_pixel+2U];
          // Farther synthetic positions can be genuinely occluded by the real
          // map. At the two retained touching centers the player's body must
          // remain observable in both reciprocal full-world compositions.
          if (!reciprocal_full_world || reciprocal_separation_offset==0.0)
            CHECK(reciprocal_changed_pixels>0U);
          CHECK(reciprocal_player_renderer->entity_statistics().studio_asset_upload_count==1U);
          CHECK(reciprocal_baseline_renderer->entity_statistics().studio_asset_upload_count==0U);
          CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
          CHECK(world.entity_scene()==retained_package);
          CHECK(glGetError()==GL_NO_ERROR);
          std::cout << "offline_reciprocal_player source=approved_player map=maps/crossfire.bsp"
              << " receiving=" << reciprocal_control.receiving << " remote=" << reciprocal_remote
              << " separation_offset=" << reciprocal_separation_offset
              << " scope=" << (reciprocal_full_world ? "world_and_dynamic" : "dynamic_only")
              << " changed_pixels=" << reciprocal_changed_pixels
              << " visibility=" << static_cast<unsigned>(reciprocal_instance.visibility_status)
              << " camera_leaf=" << (reciprocal_leaf ? std::to_string(reciprocal_leaf.result->leaf_index) : "unavailable")
              << " remote_leaf=" << (reciprocal_remote_leaf ? std::to_string(reciprocal_remote_leaf.result->leaf_index) : "unavailable")
              << " pvs_available=" << (reciprocal_leaf && reciprocal_leaf.result->pvs_available)
              << " camera_solid_or_special=" << (reciprocal_leaf && reciprocal_leaf.result->solid_or_special)
              << " static_light=" << (reciprocal_instance.static_light_rgb ? "sampled" : "fallback")
              << " rgb=" << reciprocal_rgb[0U] << ',' << reciprocal_rgb[1U] << ',' << reciprocal_rgb[2U]
              << " camera_inside_bounds=" << reciprocal_inside_bounds
              << " eye_source=" << reciprocal_control.eye_source
              << " sequence=0 gait=0 effects=0 render_mode=0 controlled_fields=true live_state=false\n";
        }
      }
    }
    reciprocal_baseline_renderer.reset();
    reciprocal_player_renderer.reset();
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
    CHECK(glGetError()==GL_NO_ERROR);
    // Restore the original independent pair/module before the existing death
    // A/B proof. Reusing a source ordinal for different committed fixtures in
    // one retained policy history would correctly be rejected as incoherent.
    host.reset({1U,1U,1U});
    REQUIRE(created.projection->reset_generation(first,world));
    world.set_camera(original_camera);
    REQUIRE(created.projection->project_entities(current,world));
    REQUIRE(e10_present(*created.projection,host,100.0,1U,world,{640,480}));
    REQUIRE(e10_present(*created.projection,host,100.020,1U,world,{640,480}));
    CHECK(world.entity_scene()==retained_package);
    CHECK(world.entity_frame()->studio_poses()[e10_instance(world,2U).pose_index].bone_matrices==first_palette);
    // Select a confirmed valid, visibly distinct sequence from the imported
    // metadata, not an assumed game-wide numeric sequence index. The explicit
    // server gait zero lets this project-owned death sample use the complete
    // main pose instead of inheriting the prior lower-body gait layer.
    std::optional<std::size_t> different_sequence;
    for (std::size_t i=1U;i<skeleton->sequences.size();++i) {
      const auto& sequence = skeleton->sequences[i];
      if (!sequence.frame_count || (sequence.blend_count!=1U && sequence.blend_count!=2U &&
          sequence.blend_count!=4U)) continue;
      different_sequence=i;
      if (sequence.label.find("die")!=std::string::npos || sequence.label.find("death")!=std::string::npos)
        break;
    }
    REQUIRE(different_sequence);
    auto changed = current;
    changed.publication_revision=3U;
    changed.entity_metadata.source->record_identity=3U;
    changed.entity_metadata.source->record_ordinal=3U;
    changed.server_time_metadata.source->record_identity=3U;
    changed.server_time_metadata.source->record_ordinal=3U;
    changed.server_time_seconds=1.04;
    auto& changed_player = changed.packet_entities[1U];
    changed_player.sequence=static_cast<std::uint32_t>(*different_sequence);
    changed_player.frame=255.0;
    changed_player.animation_time_seconds=1.04;
    changed_player.frame_rate=0.0;
    changed_player.gait_sequence=0U;
    changed.canonical_state_hash=client::runtime_observation_canonical_hash(changed);
    REQUIRE(client::valid_runtime_observation(changed));
    REQUIRE(created.projection->project_entities(changed,world));
    REQUIRE(e10_present(*created.projection,host,101.0,1U,world,{640,480}));
    REQUIRE(e10_present(*created.projection,host,101.020,1U,world,{640,480}));
    // The transition's age is svc_time-derived, so a later committed record,
    // not excess render-clock time, supplies the existing 200ms expiry.
    auto settled = changed;
    settled.publication_revision=4U;
    settled.entity_metadata.source->record_identity=4U;
    settled.entity_metadata.source->record_ordinal=4U;
    settled.server_time_metadata.source->record_identity=4U;
    settled.server_time_metadata.source->record_ordinal=4U;
    settled.server_time_seconds=1.25;
    settled.canonical_state_hash=client::runtime_observation_canonical_hash(settled);
    REQUIRE(client::valid_runtime_observation(settled));
    REQUIRE(created.projection->project_entities(settled,world));
    REQUIRE(e10_present(*created.projection,host,102.0,1U,world,{640,480}));
    REQUIRE(e10_present(*created.projection,host,102.2,1U,world,{640,480}));
    REQUIRE(created.projection->summary().remote_player_count==1U);
    const auto second_intent = created.projection->summary().remote_players[0U].intent;
    REQUIRE(second_intent.status==hlclient::game_api::RemotePlayerPresentationStatus::ready);
    CHECK_FALSE(second_intent.previous_sample);
    CHECK_FALSE(second_intent.gait_sample);
    const auto& second_instance = e10_instance(world,2U);
    const auto second_palette = world.entity_frame()->studio_poses()[second_instance.pose_index].bone_matrices;
    CHECK(second_palette != first_palette);
    CHECK(second_instance.transform.origin.x==first_origin.x);
    CHECK(second_instance.transform.origin.y==first_origin.y);
    CHECK(second_instance.transform.origin.z==first_origin.z);
    CHECK(second_instance.static_light_rgb==first_light);
    CHECK(world.entity_scene()==retained_package);
    auto scene_b = client::build_render_scene(world);
    scene_b.static_world.reset();
    scene_b.camera=stable_camera;
    REQUIRE(scene_b.dynamic_entities);
    REQUIRE_FALSE(scene_b.dynamic_entities->frame->draw_commands().empty());
    gl->renderer().render(scene_b,{640,480});
    const auto pixels_b = gl->renderer().observe_framebuffer({640,480},scene_b.clear_color,true);
    REQUIRE(pixels_b);
    CHECK(pixels_b.non_clear_pixel_count > 0U);
    CHECK(pixels_b.rgba8 != baseline.rgba8);
    CHECK(pixels_b.rgba8 != pixels_a.rgba8);
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
    REQUIRE(e10_present(*created.projection,host,102.2,1U,world,{640,480}));
    const auto& repeated_intent = created.projection->summary().remote_players[0U].intent;
    CHECK(repeated_intent.sample==second_intent.sample);
    CHECK(repeated_intent.transition_identity==second_intent.transition_identity);
    const auto& repeated_instance = e10_instance(world,2U);
    CHECK(world.entity_frame()->studio_poses()[repeated_instance.pose_index].bone_matrices==second_palette);
    auto repeated_scene = client::build_render_scene(world);
    repeated_scene.static_world.reset();
    repeated_scene.camera=stable_camera;
    gl->renderer().render(repeated_scene,{640,480});
    const auto repeated_pixels = gl->renderer().observe_framebuffer({640,480},repeated_scene.clear_color,true);
    REQUIRE(repeated_pixels);
    CHECK(repeated_pixels.rgba8==pixels_b.rgba8);
    CHECK(world.entity_scene()==retained_package);
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
    CHECK(glGetError()==GL_NO_ERROR);
    std::cout << "offline_player_gpu_control virtual_resource=models/player.mdl gpu=rendered"
        << " scope=dynamic_only sequence_b=" << *different_sequence
        << " pose_a_pixels=" << pixels_a.non_clear_pixel_count
        << " pose_b_pixels=" << pixels_b.non_clear_pixel_count
        << " pose_a_hash=" << pixels_a.color_signature << " pose_b_hash=" << pixels_b.color_signature
        << " poses_distinct=true repeated_pose_stable=true model_uploads=1 live_state=false\n";
    gl->release_renderer();
  }
  constexpr std::array<std::string_view,5U> categories{"idle","walk","run","crouch","jump"};
  for (std::size_t category=0U;category<categories.size();++category) {
    std::optional<std::size_t> selected;
    for (std::size_t i=0U;i<skeleton->sequences.size();++i) {
      const auto& sequence=skeleton->sequences[i];
      if (!sequence.frame_count || (sequence.blend_count!=1U && sequence.blend_count!=2U &&
          sequence.blend_count!=4U)) continue;
      auto label=sequence.label;
      for (auto& c:label) if (c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
      if (label.find(categories[category])!=std::string::npos) { selected=i; break; }
    }
    if (!selected) {
      std::cout << "offline_player_sequence_control virtual_resource=models/player.mdl category="
          << categories[category] << " status=unavailable reason=no_matching_imported_label\n";
      continue;
    }
    // Independent project-owned sequence observations exercise the same policy
    // and generic composition. A visual slot boundary isolates each sample;
    // it is not a fabricated gameplay event or a claim of live server coverage.
    created.projection->player_slot_boundary(2U);
    auto representative=current;
    const auto ordinal=5U+static_cast<std::uint64_t>(category);
    representative.publication_revision=ordinal;
    representative.entity_metadata.source->record_identity=ordinal;
    representative.entity_metadata.source->record_ordinal=static_cast<std::size_t>(ordinal);
    representative.server_time_metadata.source->record_identity=ordinal;
    representative.server_time_metadata.source->record_ordinal=static_cast<std::size_t>(ordinal);
    representative.server_time_seconds=1.3+static_cast<double>(category)*0.02;
    auto& player=representative.packet_entities[1U];
    player.sequence=static_cast<std::uint32_t>(*selected);
    player.gait_sequence=(categories[category]=="walk" || categories[category]=="run") ?
        static_cast<std::uint32_t>(*selected) : 0U;
    player.frame=128.0;
    player.animation_time_seconds=representative.server_time_seconds;
    player.frame_rate=0.0;
    representative.canonical_state_hash=client::runtime_observation_canonical_hash(representative);
    REQUIRE(client::valid_runtime_observation(representative));
    REQUIRE(created.projection->project_entities(representative,world));
    REQUIRE(e10_present(*created.projection,host,103.0+static_cast<double>(category),1U,world,{640,480}));
    REQUIRE(created.projection->summary().remote_player_count==1U);
    const auto& intent=created.projection->summary().remote_players[0U].intent;
    REQUIRE(intent.status==hlclient::game_api::RemotePlayerPresentationStatus::ready);
    CHECK(intent.sample.sequence==*selected);
    CHECK(created.projection->summary().remote_players[0U].pose_submitted);
    const auto& instance=e10_instance(world,2U);
    const auto& palette=world.entity_frame()->studio_poses()[instance.pose_index].bone_matrices;
    REQUIRE(palette.size()==actual_bones);
    for (const auto& bone:palette) for (const auto component:bone) CHECK(std::isfinite(component));
    std::cout << "offline_player_sequence_control virtual_resource=models/player.mdl category="
        << categories[category] << " status=posed sequence=" << *selected
        << " label=\"" << bounded_name(skeleton->sequences[*selected].label)
        << "\" project_owned_observations=true live_state=false\n";
  }
  const auto& final_summary = created.projection->summary();
  std::cout << "offline_player_production_control virtual_resource=models/player.mdl approved_bundle=true bones="
      << actual_bones << " sequences=" << metadata->sequences.size()
      << " policy=ready posed_palette=true lit_floor_selected=" << selected_lit_floor
      << " static_light=" << (final_summary.remote_players[0U].static_light ? "sampled" : "fallback")
      << " project_owned_observations=true live_state=false\n";
  CHECK(std::filesystem::last_write_time(map_path) == map_time);
  CHECK(std::filesystem::last_write_time(model_path) == model_time);
  CHECK(host.drain_audio().count == 0U);
  host.teardown();
}
#endif

TEST_CASE("E5 exact-root approved decal preparation distinguishes ready absent and rejected",
          "[world-impacts][approved-assets]") {
  hlclient::game_api::LocalImpactAssetProfile profile;
  constexpr char wad[]="decals.wad",texture[]="{SHOT1";
  std::copy_n(wad,sizeof(wad),profile.wad_name.begin());
  std::copy_n(texture,sizeof(texture),profile.texture_name.begin());
  LocalContext missing;
  auto absent=missing.create();
  REQUIRE(absent.projection);
  CHECK(absent.projection->prepare_impact_decal(profile)==app::LocalImpactDecalStatus::absent);
  CHECK_FALSE(absent.projection->impact_decal_texture());
  LocalContext ready;
  fixture::SyntheticWad3Entry entry;
  entry.name="{SHOT1";
  entry.payload=fixture::synthetic_goldsrc_miptex("{SHOT1",64U,64U);
  ready.root.write("valve","decals.wad",fixture::synthetic_wad3({entry}).bytes);
  auto created=ready.create();
  REQUIRE(created.projection);
  CHECK(created.projection->prepare_impact_decal(profile)==app::LocalImpactDecalStatus::ready);
  REQUIRE(created.projection->impact_decal_texture());
  CHECK(created.projection->impact_decal_texture()->alpha_mode==
      hlclient::assets::WorldTextureAlphaMode::masked_index_255);
  CHECK(created.projection->prepare_impact_decal(profile)==app::LocalImpactDecalStatus::ready);
  LocalContext malformed;
  malformed.root.write("valve","decals.wad",std::array<std::byte,4>{});
  auto rejected=malformed.create();
  REQUIRE(rejected.projection);
  CHECK(rejected.projection->prepare_impact_decal(profile)==app::LocalImpactDecalStatus::rejected);
}

TEST_CASE("Normal local-assets composition selects missing textures without a feature flag",
    "[runtime-local-assets][world-textures][external-map-compat]") {
  LocalContext context;
  context.root.write("valve", "test.wad", fixture::synthetic_valid_wad3("OTHER").bytes);
  auto created = context.create();
  INFO((created.error ? created.error->context : std::string{}));
  REQUIRE(created.projection);
  CHECK(created.projection->summary().missing_texture_placeholder_bindings == 1U);
  REQUIRE(created.projection->surface_scene());
  const auto& package = *created.projection->surface_scene()->world_package();
  CHECK(package.textured_world().textures.renderable_for_world_materials());
  CHECK_FALSE(package.textured_world().textures.complete_for_world_materials());
  CHECK(package.textured_world().textures.bindings()[0U].status ==
      hlclient::assets::WorldMaterialTextureBindingStatus::substituted_missing_texture);
  client::ClientWorldState world;
  CHECK(created.projection->reset_generation(observation(), world));
}

TEST_CASE("E7 materials text uses the existing approved exact-root projection", "[e7][approved-assets]") {
  LocalContext context;
  constexpr std::string_view table="M TEST_QUAD\nW WOOD\n";
  context.root.write("valve","sound/materials.txt",table);
  auto created=context.create();
  REQUIRE(created.projection);
  CHECK(created.projection->movement_materials()==table);
  LocalContext absent;
  auto missing=absent.create();
  REQUIRE(missing.projection);
  CHECK(missing.projection->movement_materials().empty());
}

TEST_CASE("E6 two approved decal profiles retain independent bounded slots",
          "[crowbar-impacts][approved-assets]") {
  LocalContext ready;
  fixture::SyntheticWad3Entry glock;
  glock.name="{SHOT1";
  glock.payload=fixture::synthetic_goldsrc_miptex("{SHOT1",64U,64U);
  fixture::SyntheticWad3Entry crowbar;
  crowbar.name="{SHOT2";
  crowbar.payload=fixture::synthetic_goldsrc_miptex("{SHOT2",64U,64U);
  ready.root.write("valve","decals.wad",fixture::synthetic_wad3({glock,crowbar}).bytes);
  auto created=ready.create(); REQUIRE(created.projection);
  const auto profile=hlclient::games::halflife::make_half_life_client_module();
  auto glock_profile=profile->local_impact_assets();
  auto crowbar_profile=profile->local_crowbar_impact_assets();
  // The module must be session-active for host access, but concrete profile
  // declarations are immutable and do not depend on network observations.
  REQUIRE(glock_profile); REQUIRE(crowbar_profile);
  CHECK(created.projection->prepare_impact_decal(*glock_profile)==app::LocalImpactDecalStatus::ready);
  CHECK(created.projection->prepare_impact_decal(crowbar_profile->decal,1U)==
      app::LocalImpactDecalStatus::ready);
  REQUIRE(created.projection->impact_decal_texture());
  REQUIRE(created.projection->impact_decal_texture(1U));
  CHECK(created.projection->impact_decal_texture()->name=="{SHOT1");
  CHECK(created.projection->impact_decal_texture(1U)->name=="{SHOT2");
  CHECK(created.projection->impact_decal_status(2U)==app::LocalImpactDecalStatus::rejected);
}

TEST_CASE("E5 opt-in installed Valve decal passes the approved exact-root importer",
          "[world-impacts][installed-decal]") {
  std::string root_path;
#ifdef _WIN32
  char* value=nullptr; std::size_t count=0U;
  if (_dupenv_s(&value,&count,"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root_path=value;
  std::free(value);
#else
  if (const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT")) root_path=value;
#endif
  if (root_path.empty()) SKIP("No opt-in read-only local game root");
  const auto source=std::filesystem::path{root_path}/"valve/decals.wad";
  if (!std::filesystem::is_regular_file(source)) SKIP("Installed decals.wad unavailable");
  const auto size=std::filesystem::file_size(source);
  REQUIRE(size>0U); REQUIRE(size<=4U*1024U*1024U);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream input(source,std::ios::binary); REQUIRE(input.is_open());
  input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  REQUIRE(input.gcount()==static_cast<std::streamsize>(bytes.size()));
  LocalContext context;
  context.root.write("valve","decals.wad",bytes);
  auto created=context.create(); REQUIRE(created.projection);
  hlclient::game_api::LocalImpactAssetProfile profile;
  constexpr char wad[]="decals.wad",texture[]="{SHOT1";
  std::copy_n(wad,sizeof(wad),profile.wad_name.begin());
  std::copy_n(texture,sizeof(texture),profile.texture_name.begin());
  const auto status=created.projection->prepare_impact_decal(profile);
  INFO("installed decal status=" << app::to_string(status));
  REQUIRE(status==app::LocalImpactDecalStatus::ready);
  REQUIRE(created.projection->impact_decal_texture());
  CHECK(created.projection->impact_decal_texture()->alpha_mode==
      hlclient::assets::WorldTextureAlphaMode::masked_index_255);
  const auto& rgba=created.projection->impact_decal_texture()->mip_levels[0].rgba_pixels;
  std::size_t opaque_white=0U, transparent=0U, dark=0U;
  for(std::size_t i=0;i<rgba.size();i+=4U) {
    opaque_white+=rgba[i]==std::byte{0xff} && rgba[i+1]==std::byte{0xff} &&
        rgba[i+2]==std::byte{0xff} && rgba[i+3]==std::byte{0xff};
    transparent+=rgba[i+3]==std::byte{0};
    dark+=rgba[i]<std::byte{0x80} && rgba[i+3]==std::byte{0xff};
  }
  CHECK(opaque_white>0U);
  CHECK(dark>0U);
  INFO("actual {SHOT1 opaque-white=" << opaque_white << " dark=" << dark
      << " transparent=" << transparent);
  hlclient::game_api::LocalImpactAssetProfile crowbar_profile;
  constexpr char crowbar_texture[]="{SHOT2";
  std::copy_n(wad,sizeof(wad),crowbar_profile.wad_name.begin());
  std::copy_n(crowbar_texture,sizeof(crowbar_texture),crowbar_profile.texture_name.begin());
  REQUIRE(created.projection->prepare_impact_decal(crowbar_profile,1U)==
      app::LocalImpactDecalStatus::ready);
  REQUIRE(created.projection->impact_decal_texture(1U));
  const auto& club_rgba=created.projection->impact_decal_texture(1U)->mip_levels[0].rgba_pixels;
  std::size_t club_white=0U,club_dark=0U;
  for(std::size_t i=0;i<club_rgba.size();i+=4U) {
    club_white+=club_rgba[i]==std::byte{0xff} && club_rgba[i+3]==std::byte{0xff};
    club_dark+=club_rgba[i]<std::byte{0x80} && club_rgba[i+3]==std::byte{0xff};
  }
  CHECK(club_white>0U);
  CHECK(club_dark>0U);
  INFO("actual {SHOT2 opaque-white=" << club_white << " dark=" << club_dark);
}

#if HLCLIENT_HAS_GAME_HALFLIFE
TEST_CASE("E5 accepted Half-Life shot reaches approved decal, neutral scene and production GL",
          "[world-impacts][composition][opengl]") {
  LocalContext context;
  fixture::SyntheticWad3Entry entry;
  entry.name="{SHOT1";
  entry.payload=fixture::synthetic_goldsrc_miptex("{SHOT1",64U,64U);
  context.root.write("valve","decals.wad",fixture::synthetic_wad3({entry}).bytes);
  auto prepared=context.create(); REQUIRE(prepared.projection);
  hlclient::game_api::GameClientHost host{
      hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,{1}});
  const auto profile=host.local_impact_assets(); REQUIRE(profile);
  REQUIRE(prepared.projection->prepare_impact_decal(*profile)==app::LocalImpactDecalStatus::ready);
  hlclient::game_api::LocalWeaponModelMetadata model;
  model.generation=1; model.model_index=59; model.resource_revision=3;
  model.resource_name="models/v_9mmhandgun.mdl";
  model.supported_bodies.fill(true); model.selectable_bodies.fill(true);
  model.sequences.resize(10,{30,46,false,{}});
  hlclient::assets::ModelSequenceEvent marker;
  marker.frame=0; marker.event_number=5001;
  marker.options={std::byte{'1'},std::byte{'1'}};
  model.sequences[3].events.push_back(marker);
  model.sequences[4].events.push_back(marker);
  host.bind_model(model);
  client::RuntimeClientObservationState observed;
  observed.generation=1; observed.publication_revision=1;
  observed.lifecycle.life_epoch=1;
  observed.lifecycle.state=client::LocalPlayerLifeState::alive;
  observed.client_metadata.generation=1;
  observed.client_metadata.freshness=client::RuntimeObservationFreshness::observed_in_record;
  observed.client_metadata.source=client::RuntimeObservationSource{.record_identity=1,.record_ordinal=1};
  observed.receiving_client.emplace();
  observed.receiving_client->viewmodel_index=59;
  observed.receiving_client->health=100;
  observed.weapon_hud.active_weapon_id=2;
  observed.weapon_hud.catalogue.push_back({.id=2,.command_name="weapon_9mmhandgun",.primary_ammo_type=1});
  observed.weapon_hud.reserve_ammo[1]=68;
  observed.weapon_hud.clips[2]=8;
  observed.weapon_slots.push_back({.wire_slot=2,.clip=8,.in_reload=false,
      .next_primary_attack=0.0,.weapon_id=2});
  host.observe(observed,0);
  hlclient::game_api::LocalWeaponSubmittedCommand command{1,1,1,0.04};
  command.shot_context=hlclient::game_api::LocalWeaponSubmittedCommand::ShotContext{
      {5,8,8},{-1,0,0}};
  host.submit(command,0.04);
  (void)host.sample(0.04);
  const auto visual=host.local_visuals({{5,8,8},{-1,0,0},{0,-1,0},{0,0,1},{},0.04});
  REQUIRE(visual.world_impact);
  auto world=fixture::world_render_fixture::make_world();
  for(auto& vertex:world.vertices) {
    const auto p=vertex.position;
    vertex.position={0,p.x,p.y}; vertex.normal={1,0,0};
  }
  world.bounds={{0,0,0},{0,16,16}};
  world.surfaces[0].bounds=world.bounds;
  auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
      {std::move(world),fixture::world_render_fixture::make_texture_set(false)},
      fixture::world_render_fixture::make_lightmap_set({}));
  REQUIRE(built);
  const auto render_world=std::make_shared<const hlclient::world_render::WorldRenderPackage>(
      std::move(*built.package));
  app::WorldImpactPresentation presenter;
  const auto hit=presenter.submit(*visual.world_impact,
      fixture::collision_brush_fixture::package(true,0.0,0U),render_world.get(),{},true,0.04);
  REQUIRE(hit.status==app::WorldImpactStatus::hit);
  REQUIRE(hit.point);
  host.accepted_world_impact(visual.world_impact->action,*hit.point,0.04);
  const auto audio=host.drain_audio();
  CHECK(std::count_if(audio.cues.begin(),audio.cues.begin()+audio.count,
      [](const auto& cue){return cue.kind==hlclient::game_api::LocalSoundKind::impact;})==1U);
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if(!gl) SKIP("OpenGL 3.3 Core context unavailable");
  gl->initialize_renderer();
  hlclient::renderer::RenderScene scene;
  scene.camera.position={25,8,8};scene.camera.target={0,8,8};
  scene.static_world=hlclient::renderer::RenderStaticWorld{render_world};
  gl->renderer().render(scene,{320,240});
  const auto before=gl->renderer().observe_framebuffer({320,240},scene.clear_color);
  REQUIRE(before);
  scene.world_decals=presenter.frame(prepared.projection->impact_decal_texture(),profile->material_mode);
  REQUIRE(scene.world_decals);
  gl->renderer().render(scene,{320,240});
  const auto after=gl->renderer().observe_framebuffer({320,240},scene.clear_color);
  REQUIRE(after);
  CHECK(after.color_signature!=before.color_signature);
  gl->release_renderer();
}

TEST_CASE("E6 accepted crowbar world hit reaches approved WAD slot and production OpenGL",
          "[crowbar-impacts][composition][opengl]") {
  LocalContext context;
  fixture::SyntheticWad3Entry entry;
  entry.name="{SHOT2";
  entry.payload=fixture::synthetic_goldsrc_miptex("{SHOT2",64U,64U);
  context.root.write("valve","decals.wad",fixture::synthetic_wad3({entry}).bytes);
  auto prepared=context.create(); REQUIRE(prepared.projection);
  hlclient::game_api::GameClientHost host{
      hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,{1}});
  const auto profile=host.local_crowbar_impact_assets(); REQUIRE(profile);
  REQUIRE(prepared.projection->prepare_impact_decal(profile->decal,1U)==
      app::LocalImpactDecalStatus::ready);
  hlclient::game_api::LocalWeaponModelMetadata model;
  model.generation=1; model.model_index=60; model.resource_revision=3;
  model.resource_name="models/v_crowbar.mdl";
  model.supported_bodies.fill(true); model.selectable_bodies.fill(true);
  model.sequences.resize(9,{30,46,false,{}});
  host.bind_model(model);
  client::RuntimeClientObservationState observed;
  observed.generation=1; observed.publication_revision=1;
  observed.lifecycle.life_epoch=1;
  observed.lifecycle.state=client::LocalPlayerLifeState::alive;
  observed.client_metadata.generation=1;
  observed.client_metadata.freshness=client::RuntimeObservationFreshness::observed_in_record;
  observed.client_metadata.source=client::RuntimeObservationSource{.record_identity=1,.record_ordinal=1};
  observed.receiving_client.emplace();
  observed.receiving_client->viewmodel_index=60;
  observed.receiving_client->health=100;
  observed.weapon_hud.active_weapon_id=1;
  observed.weapon_hud.catalogue.push_back({.id=1,.command_name="weapon_crowbar"});
  observed.weapon_slots.push_back({.wire_slot=1,.clip=0,.in_reload=false,
      .next_primary_attack=0.0,.weapon_id=1});
  host.observe(observed,0);
  hlclient::game_api::LocalWeaponSubmittedCommand command{1,1,1,0.04};
  command.shot_context=hlclient::game_api::LocalWeaponSubmittedCommand::ShotContext{
      {5,8,8},{-1,0,0}};
  host.submit(command,0.04);
  REQUIRE(host.sample(0.04).visual->sequence==4U);
  const auto visual=host.local_visuals({{5,8,8},{-1,0,0},{0,-1,0},{0,0,1},{},0.04});
  REQUIRE(visual.world_impact);
  auto world=fixture::world_render_fixture::make_world();
  for(auto& vertex:world.vertices) {
    const auto p=vertex.position;
    vertex.position={0,p.x,p.y}; vertex.normal={1,0,0};
  }
  world.bounds={{0,0,0},{0,16,16}};
  world.surfaces[0].bounds=world.bounds;
  auto built=hlclient::world_render::WorldRenderPackageBuilder{}.build(
      {std::move(world),fixture::world_render_fixture::make_texture_set(false)},
      fixture::world_render_fixture::make_lightmap_set({}));
  REQUIRE(built);
  const auto render_world=std::make_shared<const hlclient::world_render::WorldRenderPackage>(
      std::move(*built.package));
  app::WorldImpactPresentation presenter;
  const auto hit=presenter.submit(*visual.world_impact,
      fixture::collision_brush_fixture::package(true,0.0,0U),render_world.get(),{},true,0.04);
  REQUIRE(hit.status==app::WorldImpactStatus::hit);
  REQUIRE(hit.point);
  REQUIRE(host.resolved_world_impact(visual.world_impact->action,
      hlclient::game_api::LocalWorldImpactOutcome::static_world_hit,*hit.point,0.04));
  REQUIRE(host.sample(0.04).visual->sequence==3U);
  CHECK_FALSE(presenter.frame(prepared.projection->impact_decal_texture(1U),
      profile->decal.material_mode));
  presenter.update(0.241);
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if(!gl) SKIP("OpenGL 3.3 Core context unavailable");
  gl->initialize_renderer();
  hlclient::renderer::RenderScene scene;
  scene.camera.position={25,8,8};scene.camera.target={0,8,8};
  scene.static_world=hlclient::renderer::RenderStaticWorld{render_world};
  gl->renderer().render(scene,{320,240});
  const auto before=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
  REQUIRE(before);
  scene.secondary_world_decals=presenter.frame(
      prepared.projection->impact_decal_texture(1U),profile->decal.material_mode);
  REQUIRE(scene.secondary_world_decals);
  gl->renderer().render(scene,{320,240});
  const auto after=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
  REQUIRE(after);
  CHECK(after.color_signature!=before.color_signature);
  CHECK(glGetError()==GL_NO_ERROR);
  gl->release_renderer();
}
#endif

TEST_CASE("E4 approved shell resource reaches a transient world render frame",
          "[weapon-visuals][runtime-replay][shell-resource]") {
  LocalContext context;
  context.root.write("valve","models/shell.mdl",context.model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shell.mdl",50U,static_cast<std::uint32_t>(context.model.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");
  REQUIRE(created.projection);
  REQUIRE(created.projection->has_shell_model());
  CHECK(created.projection->shell_resource_status()==app::ReplayLocalVisualStatus::ready_studio);
  hlclient::renderer::TransientShellState shell;
  shell.position={0,0,0};shell.velocity={10,0,0};
  shell.starts_at_seconds=0.1;shell.expires_at_seconds=2.6;
  const std::array shells{shell};
  const auto frame=created.projection->materialize_shells(shells,0.2);
  REQUIRE(frame);
  REQUIRE(frame->frame);
  CHECK(frame->frame->studio_instances().size()==1U);
  CHECK_FALSE(frame->frame->draw_commands().empty());
  CHECK(created.projection->last_shell_frame_status()==app::ReplayShellFrameStatus::submitted);
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if (!gl || !fixture::entity_opengl_fixture::capable_context())
    SKIP("OpenGL 3.3 Core context unavailable");
  gl->initialize_renderer();
  hlclient::renderer::RenderScene scene;
  // The project-owned fixture is a one-sided XY triangle, so view its face.
  scene.camera.position={0,0,3};scene.camera.target={0,0,0};
  scene.camera.up={0,1,0};
  gl->renderer().render(scene,{320,240});
  const auto baseline=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
  REQUIRE(baseline);
  scene.transient_world_entities=*frame;
  gl->renderer().render(scene,{320,240});
  const auto visible=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
  REQUIRE(visible);
  CHECK(visible.color_signature!=baseline.color_signature);
  CHECK(glGetError()==GL_NO_ERROR);
  gl->release_renderer();
}

TEST_CASE("E4.1 shell resource failures remain typed and do not fabricate instances",
          "[weapon-visuals][runtime-replay][shell-resource]") {
  LocalContext context;
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shell.mdl",50U,4U,0U}});
  auto missing=app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");
  REQUIRE(missing.projection);
  CHECK_FALSE(missing.projection->has_shell_model());
  CHECK(missing.projection->shell_resource_status()!=app::ReplayLocalVisualStatus::ready_studio);
  context.root.write("valve","models/shell.mdl",std::array<std::byte,4>{});
  auto invalid=app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");
  REQUIRE(invalid.projection);
  CHECK_FALSE(invalid.projection->has_shell_model());
  CHECK(invalid.projection->shell_resource_status()!=app::ReplayLocalVisualStatus::ready_studio);
  const std::array shells{hlclient::renderer::TransientShellState{}};
  CHECK_FALSE(invalid.projection->materialize_shells(shells,0.1));
  CHECK(invalid.projection->last_shell_frame_status()==
      app::ReplayShellFrameStatus::binding_unavailable);
}

TEST_CASE("E4.1 opt-in installed shell reaches production OpenGL world pixels",
          "[weapon-visuals][installed-shell][opengl]") {
  std::string root_path;
#ifdef _WIN32
  char* value=nullptr;
  std::size_t count=0U;
  if (_dupenv_s(&value,&count,"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)
    root_path=value;
  std::free(value);
#else
  if (const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT")) root_path=value;
#endif
  if (root_path.empty()) SKIP("No opt-in read-only local game root");
  const auto source=std::filesystem::path{root_path}/"valve/models/shell.mdl";
  if (!std::filesystem::is_regular_file(source)) SKIP("Installed shell model unavailable");
  const auto size=std::filesystem::file_size(source);
  REQUIRE(size>0U);
  REQUIRE(size<=2U*1024U*1024U);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream input(source,std::ios::binary);
  REQUIRE(input.is_open());
  input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  REQUIRE(input.gcount()==static_cast<std::streamsize>(bytes.size()));
  LocalContext context;
  context.root.write("valve","models/shell.mdl",bytes);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shell.mdl",50U,static_cast<std::uint32_t>(bytes.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");
  INFO((created.error ? created.error->context : "ready"));
  REQUIRE(created.projection);
  REQUIRE(created.projection->has_shell_model());
  hlclient::renderer::TransientShellState shell;
  shell.position={8,0,0};shell.expires_at_seconds=2.5;
  const std::array shells{shell};
  const auto materialized=created.projection->materialize_shells(shells,0.1);
  REQUIRE(materialized);
  REQUIRE_FALSE(materialized->frame->draw_commands().empty());
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if (!gl || !fixture::entity_opengl_fixture::capable_context())
    SKIP("OpenGL 3.3 Core context unavailable");
  gl->initialize_renderer();
  hlclient::renderer::RenderScene scene;
  scene.camera.position={0,0,0};scene.camera.target={1,0,0};
  gl->renderer().render(scene,{640,480});
  const auto baseline=gl->renderer().observe_framebuffer({640,480},scene.clear_color);
  REQUIRE(baseline);
  scene.transient_world_entities=*materialized;
  gl->renderer().render(scene,{640,480});
  const auto visible=gl->renderer().observe_framebuffer({640,480},scene.clear_color);
  REQUIRE(visible);
  CHECK(visible.color_signature!=baseline.color_signature);
  CHECK(glGetError()==GL_NO_ERROR);
  gl->release_renderer();
}

client::RuntimeClientObservationState observation() {
  client::RuntimeClientObservationState state;
  state.generation = 1U;
  state.publication_revision = 1U;
  state.server_time_metadata.generation = 1U;
  state.client_metadata.generation = 1U;
  state.entity_metadata = {
      1U, client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{1U, 1U, 42U, 0U, 8U}};
  for (const auto [number, slot] :
       std::array{std::pair{2U, 7U}, std::pair{40U, 15U}}) {
    client::RuntimePacketEntityObservation entity;
    entity.entity_number = number;
    entity.ordinary_visual_schema = true;
    entity.model_index = slot;
    entity.origin = {10.0, 20.0, 30.0};
    entity.angles = {10.0, 20.0, 30.0};
    state.packet_entities.push_back(entity);
  }
  state.canonical_state_hash =
      client::runtime_observation_canonical_hash(state);
  return state;
}

TEST_CASE("Ordinary Studio diagnostics observe real raw and evaluated samples without extra pose work",
          "[multiplayer-diagnostics][ordinary-studio-diagnostics][local-capture-assets][no-stock]") {
  namespace core=hlclient::core;
  struct Sink final : core::MultiplayerDiagnosticSink {
    bool active{true},accept{true};std::vector<core::MultiplayerDiagnosticEvent> events;
    bool enabled() const noexcept override {return active;}
    bool observe(const core::MultiplayerDiagnosticEvent& value) noexcept override {
      if(events.size()<1024U)events.push_back(value);
      return accept;
    }
  } sink;
  sink.events.reserve(1024U);
  LocalContext context;context.model=fixture::synthetic_weapon_presentation_studio();
  context.root.write("valve","models/shared.mdl",context.model);
  auto on=context.create(),off=context.create();REQUIRE(on.projection);REQUIRE(off.projection);
  hlclient::game_api::GameClientHost host_on{std::make_unique<E10TestGameClientModule>()};
  hlclient::game_api::GameClientHost host_off{std::make_unique<E10TestGameClientModule>()};
  host_on.reset({1,7,1});host_off.reset({1,7,1});
  client::ClientWorldState world_on,world_off;
  auto first=e10_observation(1U,1.0,0,0,0,false);
  auto npc=first.packet_entities[2]; // Entity40, the ordinary schema, not a player.
  REQUIRE(npc.entity_number==40U);
  npc.frame=64.0;npc.animation_time_seconds=1.0;npc.frame_rate=-1.0;npc.brush_move_type=5U;
  npc.controllers={1U,2U,3U,4U};npc.blending={5U,6U};npc.effects=0U;
  first.packet_entities={npc};auto twin=npc;twin.entity_number=41U;twin.origin.y=10.0;
  first.packet_entities.push_back(twin);
  first.canonical_state_hash=client::runtime_observation_canonical_hash(first);
  REQUIRE(on.projection->reset_generation(first,world_on));REQUIRE(off.projection->reset_generation(first,world_off));
  auto second=first;second.publication_revision=2U;second.server_time_seconds=1.1;
  second.entity_metadata.source=client::RuntimeObservationSource{22U,2U,44U,0U,8U};
  second.server_time_metadata=second.entity_metadata;
  for(auto& row:second.packet_entities){row.frame=80.0;row.animation_time_seconds=1.1;}
  second.canonical_state_hash=client::runtime_observation_canonical_hash(second);
  REQUIRE(on.projection->project_entities(second,world_on));REQUIRE(off.projection->project_entities(second,world_off));
  on.projection->set_diagnostic_sink(&sink,7U);
  const auto present=[&](double now) {
    REQUIRE(e10_present(*on.projection,host_on,now,1U,world_on,{320,240}));
    REQUIRE(e10_present(*off.projection,host_off,now,1U,world_off,{320,240}));
    CHECK(on.projection->summary().ordinary_pose_evaluations==off.projection->summary().ordinary_pose_evaluations);
    CHECK(on.projection->summary().ordinary_pose_reuses==off.projection->summary().ordinary_pose_reuses);
    REQUIRE(world_on.entity_frame());REQUIRE(world_off.entity_frame());
    REQUIRE(world_on.entity_frame()->studio_poses().size()==world_off.entity_frame()->studio_poses().size());
    for(std::size_t i=0;i<world_on.entity_frame()->studio_poses().size();++i)
      CHECK(world_on.entity_frame()->studio_poses()[i].bone_matrices==world_off.entity_frame()->studio_poses()[i].bone_matrices);
  };
  present(100.0);REQUIRE(sink.events.size()==2U);
  for(const auto& event:sink.events) {
    CHECK(event.identity.producer==core::MultiplayerDiagnosticProducer::ordinary_studio);
    CHECK(event.identity.network_generation==1U);CHECK(event.identity.map_generation==7U);
    CHECK(event.identity.source_record==22U);CHECK(event.identity.source_record_ordinal==2U);
    CHECK(event.identity.source_sequence==44U);CHECK(event.identity.actor_visual_lifetime.has_value());
    CHECK_FALSE(event.identity.actor_model_revision);CHECK_FALSE(event.identity.render_frame);
    const auto& p=std::get<core::MultiplayerPresentationPayload>(event.payload);
    CHECK(p.raw_animation_sequence==0U);CHECK(p.raw_animation_frame==80.0);
    CHECK(p.raw_animtime_seconds==1.1);CHECK(p.raw_framerate==-1.0);
    CHECK((p.raw_controllers==std::array<std::uint8_t,4>{1,2,3,4}));
    CHECK((p.raw_blending==std::array<std::uint8_t,2>{5,6}));
    CHECK(p.pose_seconds.has_value());CHECK(p.current_source_seconds==1.1);
    CHECK(p.sequence==0U);CHECK(p.animation_frame.has_value());
    CHECK_FALSE(p.visible);CHECK_FALSE(p.held);CHECK_FALSE(p.clamped);
  }
  CHECK(std::get<core::MultiplayerPresentationPayload>(sink.events[0].payload).cache_hit==false);
  CHECK(std::get<core::MultiplayerPresentationPayload>(sink.events[1].payload).cache_hit==true);
  // Same input and sample retain exact cache decisions while suppressing diagnostic repetition.
  present(100.0);CHECK(sink.events.size()<=4U);
  for(std::size_t i=2U;i<sink.events.size();++i) {
    const auto original=sink.events[i].identity.actor_entity==40U ? 0U : 1U;
    CHECK(sink.events[i].local_steady_ns-sink.events[original].local_steady_ns>=125'000'000U);
  }
  const auto before_off=sink.events.size();
  sink.active=false;present(100.04);CHECK(sink.events.size()==before_off);
  sink.active=true;sink.accept=false;
  // Changing the explicit map identity clears only diagnostic suppression state.
  on.projection->set_diagnostic_sink(&sink,8U);present(100.08);REQUIRE(sink.events.size()==before_off+2U);
  CHECK(sink.events.back().identity.map_generation==8U);
  CHECK(second.packet_entities[0].frame==80.0);CHECK(second.packet_entities[0].frame_rate==-1.0);
  on.projection->set_diagnostic_sink(nullptr);present(100.09);CHECK(sink.events.size()==before_off+2U);
}

TEST_CASE("Ordinary Studio diagnostics retain absent source fields as unknown with a bounded cache",
          "[multiplayer-diagnostics][ordinary-studio-diagnostics][local-capture-assets][no-stock][bounds]") {
  namespace core=hlclient::core;
  struct Sink final : core::MultiplayerDiagnosticSink {
    std::vector<core::MultiplayerDiagnosticEvent> events;
    bool enabled() const noexcept override {return true;}
    bool observe(const core::MultiplayerDiagnosticEvent& event) noexcept override {
      if(events.size()<1024U)events.push_back(event);return true;
    }
  } sink;
  sink.events.reserve(1024U);
  LocalContext context;auto created=context.create();REQUIRE(created.projection);
  client::ClientWorldState world;auto state=observation();auto row=state.packet_entities.front();
  state.packet_entities.clear();
  for(unsigned i=0;i<140U;++i){row.entity_number=40U+i;state.packet_entities.push_back(row);}
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->reset_generation(state,world));
  created.projection->set_diagnostic_sink(&sink);
  REQUIRE(created.projection->project_entities(state,world));REQUIRE(sink.events.size()==140U);
  CHECK(std::get<core::MultiplayerPresentationPayload>(sink.events[127].payload).diagnostic_cache_evictions==0U);
  CHECK(std::get<core::MultiplayerPresentationPayload>(sink.events.back().payload).diagnostic_cache_evictions==12U);
  CHECK(created.projection->summary().ordinary_pose_evaluations==1U);
  CHECK(created.projection->summary().ordinary_pose_reuses==139U);
  for(const auto& event:sink.events) {
    CHECK(event.identity.map_generation==0U);
    const auto& p=std::get<core::MultiplayerPresentationPayload>(event.payload);
    CHECK_FALSE(p.raw_animation_sequence);CHECK_FALSE(p.raw_animation_frame);
    CHECK_FALSE(p.raw_animtime_seconds);CHECK_FALSE(p.raw_framerate);
    CHECK_FALSE(p.raw_controllers);CHECK_FALSE(p.raw_blending);CHECK_FALSE(p.pose_seconds);
    CHECK(p.sequence==0U);CHECK(p.animation_frame==0.0F);
  }
}

TEST_CASE("B080 ordinary frame-local pose reuse preserves exact per-instance output and distinct inputs",
          "[b080-ordinary-pose-cache][local-capture-assets][no-stock]") {
  LocalContext context;context.model=fixture::synthetic_weapon_presentation_studio();
  // Valid authored second skin family: one skin reference per family, both
  // selecting texture0. Other absolute source offsets remain unchanged.
  const auto skin_table=context.model.size();context.model.resize(skin_table+4U);
  fixture::studio_write_i32le(context.model,72U,static_cast<std::int32_t>(context.model.size()));
  fixture::studio_write_i32le(context.model,196U,2);
  fixture::studio_write_i32le(context.model,200U,static_cast<std::int32_t>(skin_table));
  fixture::studio_write_u16le(context.model,skin_table,0);fixture::studio_write_u16le(context.model,skin_table+2U,0);
  context.root.write("valve","models/shared.mdl",context.model);
  // Default LocalContext aliases slots7/15 to one virtual file, intentionally
  // sharing one visual record. This control instead needs a genuinely distinct
  // immutable model/revision, with observable pose data as well as a new name.
  auto distinct=context.model;
  fixture::studio_write_f32le(distinct,fixture::kSyntheticStudioBoneOffset+64U,.5F);
  context.root.write("valve","models/b080_distinct.mdl",distinct);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shared.mdl",7U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"models/b080_distinct.mdl",15U,static_cast<std::uint32_t>(distinct.size()),0U}});
  const auto make_assets=[&] {return app::RuntimeReplayLocalAssets::create(resources,
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");};
  auto batched=make_assets(),single=make_assets();REQUIRE(batched.projection);REQUIRE(single.projection);
  client::ClientWorldState batch_world,single_world;auto state=observation();
  const auto base=state.packet_entities.front();state.packet_entities.clear();
  for(unsigned i=0;i<18;++i) {
    auto row=base;const auto variant=i%9;row.entity_number=i+1;row.model_index=7;
    row.sequence=0;row.frame=0;row.body=0;row.skin=0;row.controllers.fill(0);row.blending.fill(0);
    if(variant==1)row.frame=64;
    if(variant==2)row.sequence=1;
    if(variant==3)row.body=1;
    if(variant==4)row.controllers[0]=127;
    if(variant==5)row.blending[1]=211;
    if(variant==6)row.model_index=15;
    if(variant==7)row.frame=.000001; // Same quantized pose, distinct raw input.
    if(variant==8)row.skin=1;
    row.origin.x=double(i)*2;state.packet_entities.push_back(row);
  }
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(batched.projection->reset_generation(state,batch_world));
  CHECK(batched.projection->summary().ordinary_pose_evaluations==9);
  CHECK(batched.projection->summary().ordinary_pose_reuses==9);
  const auto batch=batch_world.entity_frame();REQUIRE(batch);REQUIRE(batch->studio_instances().size()==18);
  const auto& base_pose=batch->studio_poses()[batch->studio_instances()[0].pose_index];
  const auto& distinct_pose=batch->studio_poses()[batch->studio_instances()[6].pose_index];
  CHECK(base_pose.model_resource_identity!=distinct_pose.model_resource_identity);
  CHECK(base_pose.bone_matrices!=distinct_pose.bone_matrices);
  for(const auto& row:state.packet_entities) {
    auto one=state;one.packet_entities={row};one.canonical_state_hash=client::runtime_observation_canonical_hash(one);
    REQUIRE(single.projection->reset_generation(one,single_world));
    CHECK(single.projection->summary().ordinary_pose_evaluations==1);CHECK(single.projection->summary().ordinary_pose_reuses==0);
    REQUIRE(single_world.entity_frame());const auto& reference=*single_world.entity_frame();REQUIRE(reference.studio_instances().size()==1);
    const auto actual=std::ranges::find(batch->studio_instances(),row.entity_number,&hlclient::entity_render::StudioEntityRenderInstance::entity_number);
    REQUIRE(actual!=batch->studio_instances().end());const auto& expected=reference.studio_instances()[0];
    CHECK(actual->studio_asset_index==expected.studio_asset_index);CHECK(actual->body_value==expected.body_value);
    CHECK(actual->skin_family_index==expected.skin_family_index);CHECK(actual->visibility_status==expected.visibility_status);
    CHECK(actual->material_support_status==expected.material_support_status);CHECK(actual->static_light_rgb==expected.static_light_rgb);
    CHECK(actual->transform.origin.x==expected.transform.origin.x);CHECK(actual->transform.origin.y==expected.transform.origin.y);
    CHECK(actual->transform.origin.z==expected.transform.origin.z);CHECK(actual->transform.rotation_degrees.x==expected.transform.rotation_degrees.x);
    CHECK(actual->transform.rotation_degrees.y==expected.transform.rotation_degrees.y);CHECK(actual->transform.rotation_degrees.z==expected.transform.rotation_degrees.z);
    CHECK(actual->transform.uniform_scale==expected.transform.uniform_scale);
    CHECK(batch->studio_poses()[actual->pose_index].model_resource_identity==reference.studio_poses()[expected.pose_index].model_resource_identity);
    CHECK(batch->studio_poses()[actual->pose_index].bone_matrices==reference.studio_poses()[expected.pose_index].bone_matrices);
    CHECK(actual->interpolated_bounds.minimum.x==expected.interpolated_bounds.minimum.x);
    CHECK(actual->interpolated_bounds.minimum.y==expected.interpolated_bounds.minimum.y);
    CHECK(actual->interpolated_bounds.minimum.z==expected.interpolated_bounds.minimum.z);
    CHECK(actual->interpolated_bounds.maximum.x==expected.interpolated_bounds.maximum.x);
    CHECK(actual->interpolated_bounds.maximum.y==expected.interpolated_bounds.maximum.y);
    CHECK(actual->interpolated_bounds.maximum.z==expected.interpolated_bounds.maximum.z);
  }
  // A new call starts empty even with identical immutable resources/inputs.
  REQUIRE(batched.projection->project_entities(state,batch_world));
  CHECK(batched.projection->summary().ordinary_pose_evaluations==9);CHECK(batched.projection->summary().ordinary_pose_reuses==9);
  state.generation=state.server_time_metadata.generation=state.client_metadata.generation=state.entity_metadata.generation=2;
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  const auto prior=batch_world.entity_frame();CHECK_FALSE(batched.projection->reset_generation(state,batch_world));
  CHECK(batch_world.entity_frame()==prior);
  auto generation=make_assets();REQUIRE(generation.projection);
  REQUIRE(generation.projection->reset_generation(state,batch_world));
  CHECK(generation.projection->summary().ordinary_pose_evaluations==9);CHECK(generation.projection->summary().ordinary_pose_reuses==9);
}
TEST_CASE("B080 ordinary pose cache caps retention and does not retain failed evaluation",
          "[b080-ordinary-pose-cache][local-capture-assets][no-stock][bounds]") {
  LocalContext context;auto created=context.create();REQUIRE(created.projection);
  client::ClientWorldState world;auto state=observation();const auto base=state.packet_entities.front();state.packet_entities.clear();
  for(unsigned i=0;i<140;++i) {auto row=base;row.entity_number=i+1;row.model_index=7;row.body=i%70;state.packet_entities.push_back(row);}
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->reset_generation(state,world));REQUIRE(world.entity_frame());
  CHECK(world.entity_frame()->studio_instances().size()==140);
  CHECK(created.projection->summary().ordinary_pose_evaluations==76);CHECK(created.projection->summary().ordinary_pose_reuses==64);
  // The imported model is valid; sampling its finite scale and quantized value
  // overflows only inside the existing evaluator. Repeated failures must not
  // become successful hits or suppress a later valid row.
  fixture::studio_write_f32le(context.model,fixture::kSyntheticStudioBoneOffset+88U,std::numeric_limits<float>::max());
  context.root.write("valve","models/shared.mdl",context.model);
  const auto valid_model=fixture::literal_minimal_goldsrc_studio_v10();
  context.root.write("valve","models/b080_valid.mdl",valid_model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shared.mdl",7U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"models/b080_valid.mdl",15U,static_cast<std::uint32_t>(valid_model.size()),0U}});
  auto failing=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve");
  REQUIRE(failing.projection);state=observation();state.packet_entities.clear();
  for(unsigned i=0;i<2;++i) {auto row=base;row.entity_number=i+1;row.model_index=7;state.packet_entities.push_back(row);}
  for(unsigned i=2;i<4;++i) {auto row=base;row.entity_number=i+1;row.model_index=15;state.packet_entities.push_back(row);}
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(failing.projection->reset_generation(state,world));REQUIRE(world.entity_frame());
  REQUIRE(world.entity_frame()->studio_instances().size()==2);
  CHECK(world.entity_frame()->studio_instances()[0].entity_number==3);CHECK(world.entity_frame()->studio_instances()[1].entity_number==4);
  CHECK(failing.projection->summary().ordinary_pose_evaluations==3);CHECK(failing.projection->summary().ordinary_pose_reuses==1);
}

TEST_CASE("Local capture visual CLI stays explicitly offline",
          "[local-capture-assets][command-line]") {
  using namespace hlclient::core;
  const std::vector<std::string_view> base{"--renderer",
                                           "opengl",
                                           "--runtime-replay-capture",
                                           "run",
                                           "--runtime-replay-visuals",
                                           "local-assets",
                                           "--basedir",
                                           "explicit-root",
                                           "--game",
                                           "valve"};
  const auto parsed = parse_command_line(base);
  REQUIRE(parsed);
  CHECK(parsed.options->runtime_replay_visuals ==
        RuntimeReplayVisualOption::local_assets);
  for (const auto &extra :
       std::array{std::pair{"--connect", "127.0.0.1:27015"},
                  std::pair{"--auth-provider", "file"},
                  std::pair{"--auth-material-file", "secret"},
                  std::pair{"--resource-consistency-provider", "local"},
                  std::pair{"--stop-after", "precache-manifest"},
                  std::pair{"--game", "other"}}) {
    auto arguments = base;
    arguments.push_back(extra.first);
    arguments.push_back(extra.second);
    CHECK_FALSE(parse_command_line(arguments));
  }
  auto screenshot = base;
  screenshot.insert(screenshot.end(),
                    {"--runtime-replay-screenshot", "frame.png"});
  CHECK(parse_command_line(screenshot));
  screenshot[1] = "null";
  CHECK_FALSE(parse_command_line(screenshot));
  auto no_root = base;
  no_root.erase(no_root.begin() + 6, no_root.begin() + 8);
  CHECK_FALSE(parse_command_line(no_root));
  auto wrong_mode = base;
  wrong_mode[5] = "diagnostic";
  CHECK_FALSE(parse_command_line(wrong_mode));
}

TEST_CASE("Public model slots retain sparse namespace identity and library "
          "alias reuse",
          "[local-capture-assets][binding]") {
  fixture::ScopedLocalResourceTestRoot root;
  root.write("valve", "maps/test_map.bsp", "map");
  root.write("valve", "models/exact.mdl", "model");
  root.write("valve", "sound/exact.wav", "sound");
  auto context = fixture::entity_visual_fixture::manifest(
      root, {{0U, "exact.wav", 7U, 5U, 0U},
             {2U, "maps/test_map.bsp", 9U, 3U, 0U},
             {2U, "models/exact.mdl", 7U, 5U, 0U},
             {2U, "models/exact.mdl", 15U, 5U, 0U},
             {2U, "models/missing.mdl", 20U, 5U, 0U}});
  visual::PublicGoldSrc48ModelResolver resolver;
  const auto ref = [](std::uint32_t n) {
    return visual::EntityVisualModelReference::public_goldsrc48_model_slot(n);
  };
  const auto exact = resolver.resolve(ref(7U), context.manifest);
  REQUIRE(exact);
  CHECK(exact.resource->wire_ordinal == 2U);
  CHECK(exact.model_slot == 7U);
  CHECK(exact.evidence_profile ==
        visual::EntityVisualModelResolutionEvidenceProfile::
            public_goldsrc48_type_local_model_slot);
  CHECK(resolver.resolve(ref(0U), context.manifest).status ==
        visual::EntityVisualModelResolutionStatus::invalid_model_reference);
  CHECK(resolver.resolve(ref(1U), context.manifest).status ==
        visual::EntityVisualModelResolutionStatus::missing_model_slot);
  CHECK(resolver.resolve(ref(20U), context.manifest).status ==
        visual::EntityVisualModelResolutionStatus::manifest_entry_not_ready);
  CHECK_FALSE(resolver.resolve(
      visual::EntityVisualModelReference::synthetic_model_slot(7U),
      context.manifest));
  const std::array refs{ref(7U), ref(15U), ref(7U)};
  const auto plan = visual::EntityVisualAssetLibraryBuilder{}.plan_references(
      72U, refs, context.manifest, resolver);
  REQUIRE(plan);
  REQUIRE(plan.plan->requests().size() == 1U);
  const std::array completions{
      fixture::entity_visual_fixture::studio_completion(
          plan.plan->requests()[0])};
  const auto library = visual::EntityVisualAssetLibraryBuilder{}.publish(
      *plan.plan, completions);
  REQUIRE(library);
  REQUIRE(library.library->records().size() == 1U);
  CHECK(library.library->find_exact_index(ref(7U)) ==
        library.library->find_exact_index(ref(15U)));
  CHECK(library.bindings.front().evidence_profile() ==
        visual::EntityVisualBindingEvidenceProfile::
            public_goldsrc48_model_slot_and_local_source);
}

TEST_CASE("Local asset CPU composition preserves owning identities transforms "
          "and atomic failures",
          "[local-capture-assets][null-renderer]") {
  LocalContext context;
  const auto model_time = std::filesystem::last_write_time(
      context.root.game_path("valve") / "models/shared.mdl");
  auto created = context.create();
  INFO((created.error ? created.error->context : ""));
  REQUIRE(created.projection);
  REQUIRE(created.projection->collision_world_package());
  CHECK(created.projection->summary().imported_studio == 1U);
  client::ClientWorldState world;
  auto state = observation();
  REQUIRE(client::valid_runtime_observation(state));
  REQUIRE(created.projection->reset_generation(state, world));
  REQUIRE(world.static_world());
  REQUIRE(world.entity_scene());
  REQUIRE(world.entity_frame());
  const auto package = world.entity_scene();
  const auto initial = world.entity_frame();
  REQUIRE(initial->studio_instances().size() == 2U);
  const auto &a = initial->studio_instances()[0];
  const auto &b = initial->studio_instances()[1];
  CHECK(a.studio_asset_index == b.studio_asset_index);
  CHECK(a.transform.origin.x == 10.0F);
  CHECK(a.transform.rotation_degrees.x == 30.0F);
  CHECK(a.transform.rotation_degrees.y == -10.0F);
  CHECK(a.transform.rotation_degrees.z == 20.0F);
  hlclient::renderer::null::NullRenderer renderer;
  renderer.initialize();
  renderer.render(client::build_render_scene(world), {640, 480});
  CHECK(renderer.statistics().studio_entity_instance_count == 2U);
  created.projection->acknowledge_unchanged();
  CHECK(world.entity_frame() == initial);
  state.packet_entities.erase(state.packet_entities.begin());
  state.packet_entities[0].origin.x = 99.0;
  state.canonical_state_hash =
      client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->project_entities(state, world));
  CHECK(world.entity_scene() == package);
  REQUIRE(world.entity_frame()->studio_instances().size() == 1U);
  CHECK(world.entity_frame()->studio_instances()[0].transform.origin.x ==
        99.0F);
  const auto retained = world.entity_frame();
  state.packet_entities[0].origin.x = std::numeric_limits<double>::infinity();
  state.canonical_state_hash =
      client::runtime_observation_canonical_hash(state);
  CHECK_FALSE(created.projection->project_entities(state, world));
  CHECK(world.entity_frame() == retained);
  auto other = observation();
  other.generation = 2U;
  CHECK_FALSE(created.projection->reset_generation(other, world));
  CHECK(created.projection->summary().imported_studio == 1U);
  CHECK(std::filesystem::last_write_time(context.root.game_path("valve") /
                                         "models/shared.mdl") == model_time);
  renderer.shutdown();
}

TEST_CASE("Live local assets leave the receiving-client camera under app ownership",
          "[local-capture-assets][live-camera]") {
  LocalContext context;
  auto created = app::RuntimeReplayLocalAssets::create(
      context.resources(), readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(), "valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : ""));
  REQUIRE(created.projection);
  client::ClientWorldState world;
  const client::RenderCameraState receiving_camera{
      {101.0F, 202.0F, 303.0F}, {102.0F, 202.0F, 303.0F}, {0.0F, 0.0F, 1.0F}};
  world.set_camera(receiving_camera);
  REQUIRE(created.projection->reset_generation(observation(), world));
  CHECK(world.camera() == receiving_camera);
}

#if HLCLIENT_HAS_GAME_HALFLIFE
// Project-owned geometry/animation bytes, imported by the approved provider.
// Only the eight public MP5 role labels are shared with the HL profile.
std::vector<std::byte> e16_owned_mp5_viewmodel() {
  auto model = fixture::synthetic_weapon_presentation_studio();
  constexpr std::array<std::string_view,8U> labels{
      "longidle","idle1","grenade","reload","deploy","shoot","shoot","shoot"};
  fixture::studio_write_i32le(model,164U,static_cast<std::int32_t>(labels.size()));
  for (std::size_t i=0U; i<labels.size(); ++i) {
    const auto sequence=fixture::kSyntheticStudioSourceSize+i*176U;
    fixture::studio_write_fixed_string(model,sequence,32U,labels[i]);
    fixture::studio_write_i32le(model,sequence+36U,i<2U ? 1 : 0);
  }
  return model;
}

client::RuntimeClientObservationState e16_mp5_viewmodel_state() {
  auto state=observation();
  state.packet_entities.clear();
  state.client_metadata={1U,client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{1U,1U,42U,0U,8U}};
  state.receiving_client.emplace();
  state.receiving_client->health=100.0;
  state.receiving_client->viewmodel_index=21U;
  state.receiving_client->owned_weapon_bits=1U<<4U;
  state.receiving_client->weapon_animation=0U; // Retained server idle must not mask a local action.
  state.receiving_client->water_level=0U;
  state.weapon_hud.active_weapon_id=std::uint8_t{4U};
  state.weapon_hud.clips[4U]=50;
  state.weapon_hud.reserve_ammo[1U]=100;
  state.weapon_hud.reserve_ammo[3U]=3;
  state.weapon_hud.catalogue.push_back({.id=4U,.command_name="weapon_9mmAR",
      .primary_ammo_type=1,.secondary_ammo_type=3,.slot=2,
      .source={.record_identity=1U,.record_ordinal=1U}});
  state.weapon_slots.push_back({.wire_slot=4U,.clip=50,.in_reload=false,
      .next_primary_attack=0.0,.weapon_id=4U,.next_secondary_attack=0.0,.time_weapon_idle=0.0});
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  return state;
}

std::pair<hlclient::renderer::RenderDynamicEntities,hlclient::renderer::RenderDynamicEntities>
e16_materialized_mp5_action(app::RuntimeReplayLocalAssets& assets,
    const client::RuntimeClientObservationState& state,std::uint16_t buttons) {
  namespace api=hlclient::game_api;
  const auto metadata=assets.presentation_model(state.generation,21U);
  REQUIRE(metadata);
  REQUIRE(hlclient::games::halflife::mp5_sequences(*metadata));
  CHECK(metadata->supported_bodies[0U]);CHECK(metadata->supported_bodies[2U]);
  CHECK(metadata->selectable_bodies[0U]);CHECK(metadata->selectable_bodies[2U]);
  const auto canonical=state.canonical_state_hash;
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({state.generation,1U,1U});host.set_movement_audio_time_origin(0.0);
  host.bind_model(metadata);host.observe(state,0.0);(void)host.drain_audio();
  const auto idle=host.viewmodel(state,metadata,0.0);
  REQUIRE(idle.status==api::ViewmodelStatus::ready);CHECK(idle.sequence==0U);

  const api::LocalWeaponSubmittedCommand accepted{state.generation,1U,buttons,.02};
  host.submit(accepted,.02);
  const auto started=host.sample(.02);REQUIRE(started.visual);REQUIRE(started.identity);
  const auto intent=host.viewmodel(state,metadata,.02,started.visual);
  REQUIRE(intent.status==api::ViewmodelStatus::ready);
  CHECK(intent.sequence==started.visual->sequence);CHECK(intent.body==2U);
  CHECK(intent.sequence!=idle.sequence); // Fails with the former Glock/crowbar-only viewmodel gate.
  CHECK(intent.frame_coordinate==0.0);
  if (buttons==hlclient::goldsrc::kReferenceGoldSrcButtonAttack2) {
    CHECK(intent.sequence==2U);CHECK(started.action==api::LocalWeaponAction::secondary_launch);
  } else {
    CHECK(intent.sequence>=5U);CHECK(intent.sequence<=7U);
    CHECK(started.action==api::LocalWeaponAction::primary_fire);
  }
  const auto first=assets.materialize_viewmodel(intent);REQUIRE(first);REQUIRE(first->frame);
  REQUIRE(first->frame->studio_instances().size()==1U);
  CHECK(first->frame->studio_instances().front().body_value==2U);
  const auto& first_instance=first->frame->studio_instances().front();
  CHECK(first_instance.transform.origin.x==0.0F);CHECK(first_instance.transform.origin.y==0.0F);
  CHECK(first_instance.transform.origin.z==-1.0F);
  CHECK(first_instance.transform.uniform_scale==1.0F);

  const auto later=host.sample(.08);REQUIRE(later.visual);
  CHECK(later.visual->restart_identity==started.visual->restart_identity);
  const auto advanced=host.viewmodel(state,metadata,.08,later.visual);
  CHECK(advanced.sequence==intent.sequence);CHECK(advanced.body==2U);
  CHECK(advanced.frame_coordinate==Catch::Approx(.06*metadata->sequences[intent.sequence].fps));
  REQUIRE(advanced.frame_coordinate>0.0);
  const auto second=assets.materialize_viewmodel(advanced);REQUIRE(second);REQUIRE(second->frame);
  REQUIRE(second->frame->studio_instances().size()==1U);
  const auto& second_instance=second->frame->studio_instances().front();
  CHECK(second_instance.studio_asset_index==first_instance.studio_asset_index);
  CHECK(second_instance.body_value==2U);CHECK(second->package==first->package);
  const auto& first_palette=first->frame->studio_poses()[first_instance.pose_index].bone_matrices;
  const auto& second_palette=second->frame->studio_poses()[second_instance.pose_index].bone_matrices;
  REQUIRE_FALSE(first_palette.empty());CHECK_FALSE(std::ranges::equal(first_palette,second_palette));
  host.submit(accepted,.08);const auto repeated=host.sample(.08);REQUIRE(repeated.visual);
  CHECK(repeated.visual->restart_identity==started.visual->restart_identity);
  CHECK(host.viewmodel(state,metadata,.08,repeated.visual)==advanced);
  const auto repeated_frame=assets.materialize_viewmodel(advanced);REQUIRE(repeated_frame);
  const auto& repeated_instance=repeated_frame->frame->studio_instances().front();
  CHECK(std::ranges::equal(second_palette,
      repeated_frame->frame->studio_poses()[repeated_instance.pose_index].bone_matrices));
  CHECK(client::runtime_observation_canonical_hash(state)==canonical);
  CHECK(state.weapon_slots.front().clip==50);CHECK(state.weapon_hud.reserve_ammo[3U]==3U);
  return {*first,*second};
}

TEST_CASE("E16 approved project-owned MP5 primary and secondary flow from normal host into animated Studio frames",
          "[e16][e15][local-capture-assets][first-person][game-api]") {
  LocalContext context;const auto model=e16_owned_mp5_viewmodel();
  context.root.write("valve","models/v_9mmAR.mdl",model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/v_9mmAR.mdl",21U,static_cast<std::uint32_t>(model.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : "ready"));REQUIRE(created.projection);
  const auto state=e16_mp5_viewmodel_state();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  for (const auto buttons:{hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                           hlclient::goldsrc::kReferenceGoldSrcButtonAttack2}) {
    CAPTURE(buttons);(void)e16_materialized_mp5_action(*created.projection,state,buttons);
  }
}

// This test-only workload is intentionally source-compatible with 43ad240.
// In a clean baseline source tree it runs the legacy value path; in the
// candidate it also runs the immutable-handle normal path in the same binary.
// A separate clean-source + harness baseline build can use the same workload.
TEST_CASE("P2 warm metadata cache follows the normal bind and viewmodel path",
          "[p2-metadata][benchmark][local-capture-assets][first-person][no-stock]") {
  LocalContext context;const auto model=e16_owned_mp5_viewmodel();
  context.root.write("valve","models/v_9mmAR.mdl",model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/v_9mmAR.mdl",21U,static_cast<std::uint32_t>(model.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  auto& assets=*created.projection;
  const auto state=e16_mp5_viewmodel_state();client::ClientWorldState world;
  REQUIRE(assets.reset_generation(state,world));
  const auto bind=[]<bool Handle,class A,class H>(A& a,H& h,std::uint64_t generation) {
    if constexpr (Handle) {
      auto metadata=a.presentation_model_handle(generation,21U);REQUIRE(metadata);
      h.bind_model_handle(std::move(metadata));
    } else {
      auto metadata=a.presentation_model(generation,21U);REQUIRE(metadata);
      h.bind_model(std::move(metadata));
    }
  };
  const auto view=[]<bool Handle,class A,class H>(A& a,H& h,const auto& observation,double now,
      std::optional<hlclient::game_api::LocalWeaponVisual> visual) {
    if constexpr (Handle) {
      auto metadata=a.presentation_model_handle(observation.generation,21U);REQUIRE(metadata);
      return h.viewmodel_handle(observation,std::move(metadata),now,visual);
    } else {
      auto metadata=a.presentation_model(observation.generation,21U);REQUIRE(metadata);
      return h.viewmodel(observation,std::move(metadata),now,visual);
    }
  };
  constexpr std::size_t warmup=48U,samples=256U;
  const auto run=[&]<bool Handle>(bool animated) {
    hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({state.generation,1U,1U});host.set_movement_audio_time_origin(0.0);
    bind.template operator()<Handle>(assets,host,state.generation);host.observe(state,0.0);
    if(animated) host.submit({state.generation,1U,hlclient::goldsrc::kReferenceGoldSrcButtonAttack,.02},.02);
    std::array<double,samples> elapsed{};
    std::uint64_t signature=14695981039346656037ULL;
    const auto mix=[&](std::uint64_t value) {
      for(unsigned byte=0U;byte<8U;++byte){signature^=(value>>(byte*8U))&255U;signature*=1099511628211ULL;}
    };
    for(std::size_t i=0U;i<warmup+samples;++i) {
      const double now=.03+double(i)*.002;
      const auto begin=std::chrono::steady_clock::now();
      bind.template operator()<Handle>(assets,host,state.generation);host.observe(state,now);
      bind.template operator()<Handle>(assets,host,state.generation);host.observe(state,now);
      const auto sampled=host.sample(now);
      const auto pose=view.template operator()<Handle>(assets,host,state,now,sampled.visual);
      const auto end=std::chrono::steady_clock::now();
      REQUIRE(pose.status==hlclient::game_api::ViewmodelStatus::ready);
      if(i<warmup)continue;
      elapsed[i-warmup]=std::chrono::duration<double,std::micro>(end-begin).count();
      mix(static_cast<std::uint64_t>(pose.status));mix(pose.sequence);mix(pose.body);
      mix(std::bit_cast<std::uint64_t>(pose.frame_coordinate));
    }
    std::sort(elapsed.begin(),elapsed.end());
    std::cout<<"[p2-metadata] {\"path\":\""<<(Handle?"handle":"legacy_value")<<"\",\"animated\":"<<(animated?"true":"false")
        <<",\"warmup\":"<<warmup<<",\"samples\":"<<samples
        <<",\"p50_us\":"<<elapsed[127U]<<",\"p95_us\":"<<elapsed[242U]
        <<",\"p99_us\":"<<elapsed[252U]<<",\"max_us\":"<<elapsed.back()
        <<",\"output_signature\":\""<<signature<<"\",\"metadata_value_copies_per_frame_lower_bound\":"
        <<(Handle?0:3)<<"}\n";
    return signature;
  };
  const auto dispatch=[&]<class A,class H>(A& asset_owner,H*) {
    constexpr bool supports_handle=requires(A& a,H& host) {
      host.viewmodel_handle(state,a.presentation_model_handle(state.generation,21U),0.0);
    };
    (void)asset_owner;
    for(const bool animated:{false,true}) {
      const auto legacy_signature=run.template operator()<false>(animated);
      if constexpr (supports_handle)
        CHECK(run.template operator()<true>(animated)==legacy_signature);
    }
  };
  dispatch(assets,static_cast<hlclient::game_api::GameClientHost*>(nullptr));
}

TEST_CASE("P2 metadata owner survives replacement and never mutates a handed-out value",
          "[p2-metadata][local-capture-assets][lifecycle][no-stock]") {
  LocalContext context;const auto model=e16_owned_mp5_viewmodel();
  context.root.write("valve","models/v_9mmAR.mdl",model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/v_9mmAR.mdl",21U,static_cast<std::uint32_t>(model.size()),0U}});
  auto create=[&] {
    return app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
        context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  };
  auto created=create();REQUIRE(created.projection);
  const auto state=e16_mp5_viewmodel_state();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  const auto verify=[&]<class A>(A& assets) {
    if constexpr (requires(A& a) { a.presentation_model_handle(state.generation,21U); }) {
      const auto first=assets.presentation_model_handle(state.generation,21U);
      REQUIRE(first);REQUIRE(first->sequences.size()==8U);
      CHECK(assets.presentation_model_handle(state.generation,21U)==first);
      CHECK_FALSE(assets.presentation_model_handle(state.generation+1U,21U));
      CHECK_FALSE(assets.presentation_model_handle(state.generation,20U));
      const auto legacy=assets.presentation_model(state.generation,21U);REQUIRE(legacy);
      CHECK(*legacy==*first);
      CHECK(legacy->sequences.data()!=first->sequences.data());
      const auto original_generation=first->generation;
      auto other=create();REQUIRE(other.projection);
      client::ClientWorldState other_world;
      REQUIRE(other.projection->reset_generation(state,other_world));
      const auto replacement=other.projection->presentation_model_handle(state.generation,21U);
      REQUIRE(replacement);CHECK(replacement!=first);CHECK(*replacement==*first);
      created.projection.reset();
      CHECK(first->generation==original_generation);
      CHECK(first->resource_name=="models/v_9mmAR.mdl");
      CHECK(first->sequences.size()==8U);
      CHECK(replacement->sequences.data()!=first->sequences.data());
    } else SKIP("Immutable metadata handle is unavailable in the clean baseline source");
  };
  verify(*created.projection);
}

TEST_CASE("E16 normal MP5 primary and secondary animation changes first-person GPU pixels without reupload",
          "[e16][e15][local-capture-assets][first-person][opengl][actual-context]") {
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if (!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  LocalContext context;const auto model=e16_owned_mp5_viewmodel();
  context.root.write("valve","models/v_9mmAR.mdl",model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/v_9mmAR.mdl",21U,static_cast<std::uint32_t>(model.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  const auto state=e16_mp5_viewmodel_state();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  hlclient::renderer::RenderScene scene;
  const auto draw=[&] {
    gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
    return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
  };
  const auto empty=draw();REQUIRE(empty);
  for (const auto buttons:{hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                           hlclient::goldsrc::kReferenceGoldSrcButtonAttack2}) {
    CAPTURE(buttons);
    const auto [first,second]=e16_materialized_mp5_action(*created.projection,state,buttons);
    scene.first_person_entities=first;const auto initial=draw();REQUIRE(initial);
    REQUIRE(initial.non_clear_pixel_count>0U);
    CHECK_FALSE(std::ranges::equal(initial.rgba8,empty.rgba8));
    const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;REQUIRE(uploads==1U);
    scene.first_person_entities=second;const auto animated=draw();REQUIRE(animated);
    REQUIRE(animated.non_clear_pixel_count>0U);
    CHECK_FALSE(std::ranges::equal(initial.rgba8,animated.rgba8));
    scene.camera.position={123.0F,-77.0F,28.0F};scene.camera.target={123.0F,-76.5F,28.866F};
    const auto turned=draw();REQUIRE(turned);
    CHECK(std::ranges::equal(animated.rgba8,turned.rgba8));
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
  }
  gl->release_renderer();
}

// Independently authored fixture geometry/animation; only public role labels
// and marker contracts are shared. No installed model bytes enter this fixture.
std::vector<std::byte> e17_owned_shotgun_viewmodel() {
  auto model=fixture::synthetic_weapon_presentation_studio();
  constexpr std::array<std::string_view,10U> labels{
      "sm_idle","shoot","shoot_big","reload","pump","start_reload","draw","reholster","idle4","deepidle"};
  for(std::size_t i=0;i<labels.size();++i) {
    const auto sequence=fixture::kSyntheticStudioSourceSize+i*176U;
    fixture::studio_write_fixed_string(model,sequence,32U,labels[i]);
    fixture::studio_write_i32le(model,sequence+36U,i==0U || i>=8U ? 1 : 0);
  }
  const auto events=model.size();model.resize(events+2U*76U);
  fixture::studio_write_i32le(model,72U,static_cast<std::int32_t>(model.size()));
  for(std::size_t i=0;i<2U;++i) {
    const auto sequence=fixture::kSyntheticStudioSourceSize+(i+1U)*176U;
    const auto event=events+i*76U;
    fixture::studio_write_i32le(model,sequence+48U,1);
    fixture::studio_write_i32le(model,sequence+52U,static_cast<std::int32_t>(event));
    fixture::studio_write_i32le(model,event,0);
    fixture::studio_write_i32le(model,event+4U,5001);
    fixture::studio_write_fixed_string(model,event+12U,64U,i ? "41" : "31");
  }
  return model;
}

// Small independently authored MDL files exercise the approved importer and
// real HL module, not a forged pre-merged palette. Only the bounded validated
// p-model profile's public names/numeric geometry contract is reproduced.
std::vector<std::byte> e17_owned_remote_model(bool weapon) {
  auto bytes=fixture::literal_minimal_goldsrc_studio_v10();
  constexpr std::array<std::string_view,12U> names{"Bip01","Bip01 Pelvis","Bip01 Spine",
      "Bip01 Spine1","Bip01 Spine2","Bip01 Spine3","Bip01 Neck","Bip01 R Arm",
      "Bip01 R Arm1","Bip01 R Arm2","Bip01 R Hand","Box01"};
  const auto bones=bytes.size();bytes.resize(bones+names.size()*112U);
  fixture::studio_write_i32le(bytes,140U,static_cast<std::int32_t>(names.size()));
  fixture::studio_write_i32le(bytes,144U,static_cast<std::int32_t>(bones));
  fixture::studio_write_i32le(bytes,212U,0);fixture::studio_write_i32le(bytes,216U,0);
  for(std::size_t i=0;i<names.size();++i) {
    const auto at=bones+i*112U;
    fixture::studio_write_fixed_string(bytes,at,32U,!weapon && i==11U ? "project_body" : names[i]);
    fixture::studio_write_i32le(bytes,at+32U,static_cast<std::int32_t>(i)-1);
    for(std::size_t axis=0;axis<6U;++axis) {
      fixture::studio_write_i32le(bytes,at+40U+axis*4U,-1);
      fixture::studio_write_f32le(bytes,at+88U+axis*4U,1.0F);
    }
  }
  if(weapon) {
    fixture::studio_write_vector3(bytes,bones+11U*112U+64U,12.295609F,1.386845F,1.667124F);
    fixture::studio_write_vector3(bytes,bones+11U*112U+76U,1.598449F,-.010846F,.096108F);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset,-12.26F,-4.01F,-1.57F);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+12U,12.26F,4.01F,-1.57F);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+24U,0,0,1.57F);
    for(std::size_t i=0;i<3U;++i) {
      bytes[fixture::kSyntheticStudioVertexBonesOffset+i]=std::byte{11};
      bytes[fixture::kSyntheticStudioNormalBonesOffset+i]=std::byte{11};
      fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioNormalsOffset+i*12U,0,0,1);
    }
  } else {
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset,0,-5,-12);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+12U,0,5,-12);
    fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+24U,0,0,12);
    for(std::size_t i=0;i<3U;++i)
      fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioNormalsOffset+i*12U,1,0,0);
    const auto controllers=bytes.size();bytes.resize(controllers+4U*24U);
    fixture::studio_write_i32le(bytes,148U,4);
    fixture::studio_write_i32le(bytes,152U,static_cast<std::int32_t>(controllers));
    for(std::size_t i=0;i<4U;++i) {
      const auto at=controllers+i*24U;
      fixture::studio_write_i32le(bytes,at,static_cast<std::int32_t>(i+3U));
      fixture::studio_write_i32le(bytes,at+4U,0x0008);
      fixture::studio_write_f32le(bytes,at+8U,-45);
      fixture::studio_write_f32le(bytes,at+12U,45);
      fixture::studio_write_i32le(bytes,at+20U,static_cast<std::int32_t>(i));
      fixture::studio_write_i32le(bytes,bones+(i+3U)*112U+40U+3U*4U,static_cast<std::int32_t>(i));
    }
  }
  constexpr std::array<std::string_view,5U> labels{"idle","walk","crouch","shootshotgun","die"};
  const auto count=weapon ? 1U : labels.size();
  const auto sequences=bytes.size();bytes.resize(sequences+count*176U);
  fixture::studio_write_i32le(bytes,164U,static_cast<std::int32_t>(count));
  fixture::studio_write_i32le(bytes,168U,static_cast<std::int32_t>(sequences));
  for(std::size_t i=0;i<count;++i) {
    const auto at=sequences+i*176U;
    const auto animation=bytes.size();bytes.resize(animation+12U*12U+10U);
    fixture::studio_write_fixed_string(bytes,at,32U,labels[i]);
    fixture::studio_write_f32le(bytes,at+32U,10);
    fixture::studio_write_i32le(bytes,at+36U,i<3U ? 1 : 0);
    fixture::studio_write_i32le(bytes,at+56U,4);
    fixture::studio_write_vector3(bytes,at+96U,-32,-32,-32);
    fixture::studio_write_vector3(bytes,at+108U,32,32,32);
    fixture::studio_write_i32le(bytes,at+120U,1);
    fixture::studio_write_i32le(bytes,at+124U,static_cast<std::int32_t>(animation));
    fixture::studio_write_i32le(bytes,at+152U,-1);
    if(!weapon) {
      // Server sequence changes move the hand differently. The child must
      // inherit this track through production named-bone merge, not its idle.
      const auto track=animation+10U*12U,stream=animation+12U*12U;
      fixture::studio_write_u16le(bytes,track+2U,static_cast<std::uint16_t>(stream-track));
      bytes[stream]=std::byte{4};bytes[stream+1U]=std::byte{4};
      for(std::size_t frame=0;frame<4U;++frame)
        fixture::studio_write_i16le(bytes,stream+2U+frame*2U,
            static_cast<std::int16_t>(i*6U+frame));
    }
  }
  fixture::studio_write_i32le(bytes,72U,static_cast<std::int32_t>(bytes.size()));
  return bytes;
}

void e17_remote_socket_fire(hlclient::game_api::GameClientHost& host,
    const hlclient::game_api::RemoteWeaponPoseSample& socket,
    std::uint64_t record,double now,bool twice,bool expect_attachment=true) {
  namespace api=hlclient::game_api;
  api::CommittedScriptedEvent event;
  event.generation=1;event.record=event.record_ordinal=record;event.emitter_entity=2;
  event.event_index=twice ? 77U : 901U;event.message_bit_offset=24;
  event.received_at_seconds=now;event.weapon_model_index=22U;
  event.origin={0,0,16};event.angles={0,0,0};
  host.committed_scripted_event(event);
  // Only one large fixed production batch exists in this short-lived frame.
  const auto effects=host.drain_remote_effects(now);REQUIRE(effects.count==1U);
  const auto& shot=effects.effects[0U];
  if(!expect_attachment) {
    CHECK(shot.pose.status==api::RemotePoseResolution::weapon_mismatch);
    CHECK(shot.pose.source==api::RemoteSocketSource::unavailable);
    CHECK_FALSE(shot.flash);CHECK_FALSE(shot.light);CHECK_FALSE(shot.shell);
    CHECK(shot.extra_shell_count==0U);
    CHECK(host.drain_remote_audio().count==1U);
    return;
  }
  REQUIRE(shot.pose.status==api::RemotePoseResolution::resolved);
  REQUIRE(shot.flash);REQUIRE(shot.shell);REQUIRE(shot.light);
  CHECK(shot.pose.server_sequence==socket.server_sequence);
  CHECK(shot.flash->origin.x==Catch::Approx(socket.muzzle.origin.x));
  CHECK(shot.flash->origin.y==Catch::Approx(socket.muzzle.origin.y));
  CHECK(shot.flash->origin.z==Catch::Approx(socket.muzzle.origin.z));
  CHECK(shot.shell->origin.x==Catch::Approx(socket.shell.origin.x));
  CHECK(shot.shell->origin.y==Catch::Approx(socket.shell.origin.y));
  CHECK(shot.shell->origin.z==Catch::Approx(socket.shell.origin.z));
  CHECK(shot.extra_shell_count==(twice ? 1U : 0U));
  CHECK(shot.extra_impact_count==(twice ? 7U : 3U));
  CHECK(host.drain_remote_audio().count==1U);
}

void e17_remote_shotgun_merge_control(app::RuntimeReplayLocalAssets& assets,
    hlclient::assets::AssetVector3 origin,const std::array<std::uint32_t,4U>& sequences,
    bool installed) {
  namespace api=hlclient::game_api;
  const auto player=assets.studio_model_data(7U),weapon=assets.studio_model_data(22U);
  REQUIRE(player);REQUIRE(weapon);REQUIRE(weapon->bones.size()==12U);
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if(gl && fixture::entity_opengl_fixture::capable_context()) gl->initialize_renderer();
  else {gl.reset();WARN("E17 remote p_shotgun GPU subcheck unavailable; approved merge/socket/lifecycle CPU assertions still run");}
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,1});host.set_movement_audio_time_origin(0);
  std::array<api::ScriptedEventBinding,2U> bindings;
  bindings[0U].index=901;bindings[1U].index=77;
  constexpr std::array<std::string_view,2U> tokens{"events/shotgun1.sc","events/shotgun2.sc"};
  for(std::size_t i=0;i<bindings.size();++i) std::copy(tokens[i].begin(),tokens[i].end(),bindings[i].name.begin());
  host.configure_scripted_events(bindings);
  client::ClientWorldState world;
  world.set_camera({{origin.x+96,origin.y-64,origin.z+36},{origin.x,origin.y,origin.z+4},{0,0,1}});
  std::optional<hlclient::assets::AssetVector3> previous_socket;
  std::size_t gpu_checks=0;
  // Server movement, crouch and fire sequences are inputs; death removes the
  // attachment, re-arm restores it, then weapon slot zero removes it again.
  for(std::size_t step=0;step<6U;++step) {
    CAPTURE(installed,step);
    const auto record=static_cast<std::uint64_t>(step+1U);
    const auto server=1.0+static_cast<double>(step)*.2;
    const auto anchor=100.0+static_cast<double>(step)*.3,now=anchor+.21;
    auto state=e10_observation(record,server,origin.x,0,0,false);state.packet_entities.resize(2U);
    auto& entity=state.packet_entities[1U];
    entity.origin={origin.x+static_cast<double>(step)*2,origin.y,origin.z};
    entity.angles={0,0,0};entity.sequence=sequences[step==3U ? 3U : step<3U ? step : 0U];
    entity.gait_sequence=0U;entity.weapon_model_index=step==3U || step==5U ? 0U : 22U;
    entity.frame=64.0;entity.animation_time_seconds=server;entity.frame_rate=1.0;
    entity.controllers.fill(127U);entity.blending.fill(127U);
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    const auto canonical=state.canonical_state_hash;
    if(step==0U) REQUIRE(assets.reset_generation(state,world));
    else REQUIRE(assets.project_entities(state,world));
    REQUIRE(e10_present(assets,host,anchor,1U,world,{640,480}));
    REQUIRE(e10_present(assets,host,now,1U,world,{640,480}));
    const auto frame=world.entity_frame();REQUIRE(frame);
    const bool attached=step!=3U && step!=5U;
    REQUIRE(frame->studio_instances().size()==(attached ? 2U : 1U));
    const auto& body=frame->studio_instances()[0U];
    REQUIRE(assets.summary().remote_player_count==1U);
    const auto& diagnostic=assets.summary().remote_players[0U];
    REQUIRE(diagnostic.status==api::RemotePlayerPresentationStatus::ready);
    CHECK(diagnostic.intent.sample.sequence==entity.sequence);
    CHECK(body.entity_number==2U);CHECK(body.subinstance_ordinal==0U);
    if(attached) {
      const auto& child=frame->studio_instances()[1U];
      CHECK(child.entity_number==2U);CHECK(child.subinstance_ordinal==1U);
      REQUIRE(diagnostic.attachment_matched_bones>=11U);
      const auto& parent_pose=frame->studio_poses()[body.pose_index].bone_matrices;
      const auto& child_pose=frame->studio_poses()[child.pose_index].bone_matrices;
      REQUIRE(child_pose.size()==12U);
      for(std::size_t bone=0;bone<11U;++bone) {
        const auto match=std::ranges::find(player->bones,weapon->bones[bone].name,&hlclient::assets::ModelBone::name);
        REQUIRE(match!=player->bones.end());
        const auto index=static_cast<std::size_t>(match-player->bones.begin());
        CHECK(child_pose[bone]==parent_pose[index]);
      }
      const auto sockets=assets.remote_weapon_poses();REQUIRE(sockets.size()==1U);
      const auto& socket=sockets[0U];
      CHECK(socket.model_slot==22U);CHECK(socket.semantic_key==7U);
      CHECK(socket.source==api::RemoteSocketSource::validated_pmodel_socket);
      CHECK(socket.server_sequence==entity.sequence);CHECK(socket.muzzle_bone==11U);CHECK(socket.shell_bone==11U);
      const auto matrix=hlclient::entity_render::entity_render_model_matrix(child.transform);
      const auto verify_socket=[&](const hlclient::assets::EvaluatedModelSocket& value,hlclient::assets::AssetVector3 point) {
        const auto& bone=child_pose[11U];
        const float x=bone[0]*point.x+bone[4]*point.y+bone[8]*point.z+bone[12];
        const float y=bone[1]*point.x+bone[5]*point.y+bone[9]*point.z+bone[13];
        const float z=bone[2]*point.x+bone[6]*point.y+bone[10]*point.z+bone[14];
        CHECK(value.origin.x==Catch::Approx(matrix[0]*x+matrix[4]*y+matrix[8]*z+matrix[12]));
        CHECK(value.origin.y==Catch::Approx(matrix[1]*x+matrix[5]*y+matrix[9]*z+matrix[13]));
        CHECK(value.origin.z==Catch::Approx(matrix[2]*x+matrix[6]*y+matrix[10]*z+matrix[14]));
      };
      verify_socket(socket.muzzle,{12.35F,2.77F,0});verify_socket(socket.shell,{2,2.77F,-1.65F});
      if(previous_socket) CHECK_FALSE((socket.muzzle.origin.x==previous_socket->x &&
          socket.muzzle.origin.y==previous_socket->y && socket.muzzle.origin.z==previous_socket->z));
      previous_socket=socket.muzzle.origin;
      if(step==1U || step==2U) {
        const auto server_pose=parent_pose;
        e17_remote_socket_fire(host,socket,record,now,step==2U);
        REQUIRE(e10_present(assets,host,now,1U,world,{640,480}));
        CHECK(assets.summary().remote_players[0U].intent.sample.sequence==entity.sequence);
        CHECK(world.entity_frame()->studio_poses()[e10_instance(world,2U).pose_index].bone_matrices==server_pose);
      }
      if(gl) {
        auto scene=client::build_render_scene(world);scene.static_world.reset();
        gl->renderer().render(scene,{640,480});
        const auto full=gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);REQUIRE(full);
        REQUIRE(full.non_clear_pixel_count>0U);
        REQUIRE(gl->renderer().entity_statistics().studio_draw_count>=2U);
        const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
        hlclient::entity_render::EntityRenderFrameBuildInput input;
        input.resource_id=1;input.resource_revision=1;input.interpolation=scene.dynamic_entities->frame->interpolation();
        input.studio_poses.assign(scene.dynamic_entities->frame->studio_poses().begin(),scene.dynamic_entities->frame->studio_poses().end());
        input.studio_instances.push_back(scene.dynamic_entities->frame->studio_instances()[0U]);
        auto built=hlclient::entity_render::EntityRenderFrameBuilder{}.build(*scene.dynamic_entities->package,std::move(input));
        REQUIRE(built);scene.dynamic_entities->frame=std::make_shared<const hlclient::entity_render::EntityRenderFrame>(std::move(*built.frame));
        gl->renderer().render(scene,{640,480});
        const auto body_only=gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);REQUIRE(body_only);
        CHECK_FALSE(std::ranges::equal(full.rgba8,body_only.rgba8));
        CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
        CHECK(glGetError()==GL_NO_ERROR);++gpu_checks;
      }
    } else {
      // The existing host publishes an explicit per-entity unavailable sample
      // to invalidate the prior weapon, not an empty all-player pose view.
      // Also prove that a late named Shotgun event cannot reuse its old socket.
      const auto sockets=assets.remote_weapon_poses();REQUIRE(sockets.size()==1U);
      CHECK(sockets[0U].entity==2U);CHECK(sockets[0U].model_slot==0U);
      REQUIRE(sockets[0U].current_model_slot);CHECK(*sockets[0U].current_model_slot==0U);
      CHECK(sockets[0U].source==api::RemoteSocketSource::unavailable);
      CHECK_FALSE(sockets[0U].semantic_match);CHECK(sockets[0U].resource_id==0U);
      e17_remote_socket_fire(host,sockets[0U],record,now,false,false);
      REQUIRE(world.entity_frame()->studio_instances().size()==1U);
    }
    CHECK(state.canonical_state_hash==canonical);
  }
  if(gl) {CHECK(gpu_checks==4U);gl->release_renderer();}
  std::cout << "offline_shotgun_remote_control profile=" << (installed ? "approved_installed" : "project_owned")
      << " merge=ready sockets=ready server_body_authority=preserved lifecycle=checked gpu="
      << (gl ? "passed" : "skipped_opengl_3_3_core_unavailable") << " live_state=false\n";
  host.teardown();
}

TEST_CASE("E32 normal approved materializer keeps firing players continuous on rising and falling lifts",
    "[e32-nointerp-motion][runtime-presentation][actual-context][local-capture-assets]") {
  for(const double vertical_speed:{120.0,-120.0}) {
    LocalContext local;
    auto bytes=e17_owned_remote_model(false);
    std::uint32_t sequences{};
    for(std::size_t i=0;i<4;++i)sequences|=std::to_integer<std::uint32_t>(bytes[168U+i])<<(i*8U);
    fixture::studio_write_fixed_string(bytes,sequences+3U*176U,32U,"ref_shoot_onehanded");
    local.model=bytes;local.root.write("valve","models/shared.mdl",bytes);
    auto created=local.create();REQUIRE(created.projection);
    hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    host.reset({1,1,1});client::ClientWorldState world;
    world.set_camera({{400,0,256},{0,0,0},{0,0,1}});
    std::size_t ordinal=1;
    const auto observation_for=[&](std::size_t record) {
      const double t=.04*static_cast<double>(record-1);
      auto state=e10_observation(record,1+t,160*t,0,0,false);
      state.packet_entities.resize(2);
      auto& player=state.packet_entities[1];player.origin.z=48+vertical_speed*t;
      player.sequence=3;player.gait_sequence=1;player.weapon_model_index=22;
      player.effects=(record==17 || record==18) ? 34U : 0U;
      player.animation_time_seconds=1+t;player.frame_rate=1;player.frame=0;
      player.controllers.fill(127);player.blending.fill(127);
      state.canonical_state_hash=client::runtime_observation_canonical_hash(state);return state;
    };
    auto latest=observation_for(ordinal);
    REQUIRE(created.projection->reset_generation(latest,world));
    std::optional<double> last_x,last_z;
    for(std::size_t frame=0;frame<=220;++frame) {
      const double now=100+.005*static_cast<double>(frame);
      while(now+1e-9>=100+.04*static_cast<double>(ordinal)) {
        latest=observation_for(++ordinal);REQUIRE(created.projection->project_entities(latest,world));
      }
      const auto canonical=latest.canonical_state_hash;
      REQUIRE(created.projection->present_entities(host,now,1,world,{640,480},true));
      const auto observed=created.projection->remote_movement_observations();REQUIRE(observed);REQUIRE(observed->size()==1);
      const auto& row=observed->front();REQUIRE(row.sample_server_s);REQUIRE(row.rendered_origin);
      const double selected=*row.sample_server_s-1;
      CHECK((*row.rendered_origin)[0]==Catch::Approx(160*selected).margin(.001));
      CHECK((*row.rendered_origin)[2]==Catch::Approx(48+vertical_speed*selected).margin(.001));
      if(frame>110 && last_x && last_z) {
        CHECK(std::abs((*row.rendered_origin)[0]-*last_x)<=.801);
        CHECK(std::abs((*row.rendered_origin)[2]-*last_z)<=.601);
        CHECK((row.presentation.new_boundary_reasons&static_cast<std::uint32_t>(
            hlclient::entity_visual::PlayerContinuityReason::nointerp_effect))==0);
      }
      CHECK(latest.canonical_state_hash==canonical);
      last_x=(*row.rendered_origin)[0];last_z=(*row.rendered_origin)[2];
    }
    host.teardown();
  }
}

TEST_CASE("E32 optional approved installed player metadata qualifies the observed firing pulse",
    "[e32-nointerp-approved][e32-nointerp-motion][installed-player-production][local-capture-assets]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value=nullptr;std::size_t count{};
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;
  std::free(value);
#else
  if(const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT"))root=value;
#endif
  if(root.empty())SKIP("No opt-in read-only local game root");
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("Only the existing exact prepared research root is authorized");
  const auto resources=readiness::parse_resource_list({
      {2,"maps/crossfire.bsp",9,0,0},{2,"models/player.mdl",7,0,0},
      {2,"models/player/zombie/zombie.mdl",8,0,0}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),
      root,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  if(!created.projection)SKIP("Approved optional asset preparation unavailable");
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});
  for(const auto slot:{7U,8U}) {
    const auto model=created.projection->studio_model_data(slot);
    REQUIRE(model);REQUIRE(model->sequences.size()>34);
    auto before=e10_observation(1,25785.3828125,-872.34375,0,0,false).packet_entities[1];
    before.model_index=slot;before.sequence=34;before.gait_sequence=4;before.weapon_model_index=91;
    before.effects=0;before.origin={-872.34375,1246.3125,-1659.96875};
    auto now=before;now.effects=34;now.origin={-870.25,1240.40625,-1659.96875};
    const auto intent=host.world_entity({now,&before,{},12,model.get(),25785.3828125,25785.42578125});
    INFO("approved_slot=" << slot << " sequence=" << model->sequences[34].label
        << " loop=" << (model->sequences[34].source_flags&1U));
    CHECK(intent.animation_only_effect_mask==32);
    std::cout << "[e32-nointerp-approved] slot=" << slot << " sequence=" << model->sequences[34].label
        << " loop=" << (model->sequences[34].source_flags&1U) << " motion_mask=" << intent.animation_only_effect_mask
        << " source=approved_read_only live=false\n";
  }
  host.teardown();
}

struct E31AppearanceFixture {
  LocalContext local;
  std::unique_ptr<app::RuntimeReplayLocalAssets> assets;
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  client::ClientWorldState world;
  client::RuntimeClientObservationState state;
  std::uint64_t ordinal{1};
  bool committed{};
  E31AppearanceFixture() {
    auto base=e17_owned_remote_model(false),red=base,green=base,weapon=e17_owned_remote_model(true);
    const auto color=[](auto& bytes,std::array<std::uint8_t,3> rgb,float half_width) {
      fixture::studio_write_fixed_string(bytes,fixture::kSyntheticStudioTextureOffset,64,"DM_Base.bmp");
      bytes[fixture::kSyntheticStudioTextureDataOffset]=std::byte{160};
      for(std::size_t c=0;c<3;++c)bytes[fixture::kSyntheticStudioTextureDataOffset+1+160*3+c]=std::byte{rgb[c]};
      fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset,0,-half_width,-12);
      fixture::studio_write_vector3(bytes,fixture::kSyntheticStudioVerticesOffset+12,0,half_width,-12);
    };
    color(base,{200,40,20},5);color(red,{200,40,20},7);color(green,{20,200,40},11);
    local.root.write("valve","models/player.mdl",base);
    local.root.write("valve","models/player/fixture_red/fixture_red.mdl",red);
    local.root.write("valve","models/player/fixture_green/fixture_green.mdl",green);
    local.root.write("valve","models/player/incompatible/incompatible.mdl",fixture::literal_minimal_goldsrc_studio_v10());
    local.root.write("valve","models/p_shotgun.mdl",weapon);
    const auto resources=readiness::parse_resource_list({
        {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(local.map.size()),0U},
        {2U,"models/player.mdl",7U,static_cast<std::uint32_t>(base.size()),0U},
        {2U,"models/p_shotgun.mdl",22U,static_cast<std::uint32_t>(weapon.size()),0U}});
    auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
        local.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(created.projection);
    assets=std::move(created.projection);host.reset({1,1,1});host.set_movement_audio_time_origin(0);
    state=e10_observation(1,1,0,0,0,true);state.packet_entities.resize(3);
    for(std::size_t i=1;i<3;++i) {
      auto& entity=state.packet_entities[i];entity.origin={0,static_cast<double>(i==1 ? -20 : 20),16};
      entity.gait_sequence=0;entity.weapon_model_index=22;entity.animation_time_seconds=1;entity.frame_rate=1;
      entity.controllers.fill(127);entity.blending.fill(127);
    }
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    REQUIRE(assets->reset_generation(state,world));world.set_camera({{110,0,30},{0,0,16},{0,0,1}});
    present();
  }
  void identity(std::uint32_t slot,std::string_view info,bool replacement=false,bool present=true) {
    namespace api=hlclient::game_api;
    const auto bytes=hlclient::test::user_info_fixture::make_message(static_cast<std::uint8_t>(slot-1),static_cast<std::int32_t>(42+slot),info);
    const auto decoded=hlclient::goldsrc::UserInfoUpdateParser{}.parse(bytes);REQUIRE(decoded);
    api::PlayerDisplayUpdate update{slot,present,replacement,"Fixture",decoded.state->player_appearance()};
    api::GameMessageView message{api::GameMessageKind::player_display,{}, {},
        {ordinal,static_cast<std::size_t>(ordinal),static_cast<std::uint32_t>(ordinal),0,512},std::nullopt,update};
    ++ordinal;
    auto staged=host.stage_record({committed ? &state : nullptr,state,std::span{&message,1U},1U,8U});REQUIRE(staged);
    host.commit_record(std::move(*staged.state));committed=true;
  }
  void present() {REQUIRE(e10_present(*assets,host,100,1U,world,{640,480}));}
  const app::ReplayLocalVisualSummary::RemotePlayer& diagnostic(std::uint32_t slot) {
    const auto& summary=assets->summary();
    const auto values=std::span{summary.remote_players}.first(summary.remote_player_count);
    const auto found=std::ranges::find(values,slot,&app::ReplayLocalVisualSummary::RemotePlayer::entity);
    REQUIRE(found!=values.end());return *found;
  }
  void ready(std::uint32_t slot,hlclient::game_api::PlayerAppearanceStatus status) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {present();if(diagnostic(slot).appearance.status==status)return;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<until);
    REQUIRE(diagnostic(slot).appearance.status==status);
  }
};
TEST_CASE("E31 normal approved materializer selects two userinfo models and freezes corpse appearance",
    "[e31][appearance][local-capture-assets]") {
  namespace api=hlclient::game_api;
  E31AppearanceFixture fixture;
  const auto base_index=e10_instance(fixture.world,2).studio_asset_index;
  fixture.identity(2,"\\model\\fixture_red\\topcolor\\30",true);
  fixture.identity(3,"\\model\\fixture_green\\topcolor\\120",true);
  fixture.ready(2,api::PlayerAppearanceStatus::selected_model_ready);fixture.ready(3,api::PlayerAppearanceStatus::selected_model_ready);
  const auto index2=e10_instance(fixture.world,2).studio_asset_index,index3=e10_instance(fixture.world,3).studio_asset_index;
  CHECK(index2!=base_index);CHECK(index3!=base_index);CHECK(index2!=index3);
  CHECK(fixture.diagnostic(2).resolved_visual_model=="models/player/fixture_red/fixture_red.mdl");
  CHECK(fixture.diagnostic(3).resolved_visual_model=="models/player/fixture_green/fixture_green.mdl");
  REQUIRE(fixture.world.entity_frame()->studio_instances().size()==4U);
  CHECK(fixture.diagnostic(2).attachment_matched_bones>=11U);
  CHECK(fixture.diagnostic(3).attachment_matched_bones>=11U);
  const auto scene=client::build_render_scene(fixture.world);REQUIRE(scene.dynamic_entities);
  const auto sources=scene.dynamic_entities->package->presentation_studio_sources();REQUIRE(sources.size()==2U);
  CHECK(sources[0].model->skeletal_data->textures[0].rgba8_level_zero!=sources[1].model->skeletal_data->textures[0].rgba8_level_zero);
  for(int i=0;i<10;++i)fixture.present();
  CHECK(client::build_render_scene(fixture.world).dynamic_entities->package->presentation_studio_sources().size()==2U);
  auto corpse=fixture.state.packet_entities[1];corpse.entity_number=40;corpse.player_movement_schema=false;
  corpse.render_effect=17;corpse.render_amount=2;corpse.model_index=999;corpse.effects=32;corpse.sequence=4;
  corpse.frame=255;corpse.weapon_model_index=0;corpse.frame_rate=0;corpse.origin={0,0,16};
  fixture.state.packet_entities.push_back(corpse);++fixture.state.publication_revision;
  fixture.state.entity_metadata.source->record_ordinal=10;fixture.state.entity_metadata.source->record_identity=10;
  fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
  REQUIRE(fixture.assets->project_entities(fixture.state,fixture.world));fixture.present();
  const auto corpse_index=e10_instance(fixture.world,40).studio_asset_index;CHECK(corpse_index==index2);
  fixture.identity(2,"\\model\\fixture_green\\topcolor\\240",true);fixture.assets->player_slot_boundary(2);
  // Slot boundaries deliberately reject pre-boundary entity snapshots. Publish
  // the replacement occupant's next committed snapshot before presenting it.
  fixture.state.entity_metadata.source->record_ordinal=11;fixture.state.entity_metadata.source->record_identity=11;
  ++fixture.state.publication_revision;fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
  REQUIRE(fixture.assets->project_entities(fixture.state,fixture.world));
  fixture.ready(2,api::PlayerAppearanceStatus::selected_model_ready);
  CHECK(e10_instance(fixture.world,2).studio_asset_index!=index2);
  CHECK(e10_instance(fixture.world,40).studio_asset_index==corpse_index);
  // Original body queues are MOVETYPE_TOSS, not frozen positions. A retained
  // body's physics motion must not adopt the respawned/reused source slot.
  auto& moving_corpse=fixture.state.packet_entities.back();moving_corpse.origin.z=12;moving_corpse.brush_move_type=6U;
  fixture.state.entity_metadata.source->record_ordinal=12;fixture.state.entity_metadata.source->record_identity=12;
  ++fixture.state.publication_revision;fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
  REQUIRE(fixture.assets->project_entities(fixture.state,fixture.world));fixture.present();
  CHECK(e10_instance(fixture.world,40).studio_asset_index==corpse_index);
  fixture.identity(2,"\\model\\missing",false);fixture.ready(2,api::PlayerAppearanceStatus::approved_asset_missing);
  CHECK(e10_instance(fixture.world,2).studio_asset_index==base_index);
  fixture.identity(2,"\\model\\incompatible",false);fixture.ready(2,api::PlayerAppearanceStatus::pose_incompatible);
  CHECK(e10_instance(fixture.world,2).studio_asset_index==base_index);
  fixture.identity(2,"\\model\\../bad",false);fixture.present();
  CHECK(fixture.diagnostic(2).appearance.status==api::PlayerAppearanceStatus::model_token_rejected);
  CHECK(e10_instance(fixture.world,2).studio_asset_index==base_index);
  CHECK(e10_instance(fixture.world,40).studio_asset_index==corpse_index);
  CHECK(fixture.state.packet_entities[1].model_index==7);
  CHECK(fixture.state.packet_entities[2].model_index==7);
  CHECK(fixture.state.canonical_state_hash==client::runtime_observation_canonical_hash(fixture.state));
}

TEST_CASE("E31 retain-only removal ends frozen corpse appearance before entity-number reuse",
    "[e31][appearance][corpse][runtime-presentation][b080-cpu-workload]") {
  namespace api=hlclient::game_api;
  E31AppearanceFixture fixture;
  fixture.identity(2,"\\model\\fixture_red\\topcolor\\30",true);
  fixture.ready(2,api::PlayerAppearanceStatus::selected_model_ready);
  const auto red_index=e10_instance(fixture.world,2).studio_asset_index;

  auto corpse=fixture.state.packet_entities[1];
  corpse.entity_number=40;corpse.player_movement_schema=false;
  corpse.render_effect=17;corpse.render_amount=2;corpse.model_index=999;
  corpse.effects=32;corpse.sequence=4;corpse.frame=255;
  corpse.weapon_model_index=0;corpse.frame_rate=0;corpse.origin={0,0,16};
  fixture.state.packet_entities.push_back(corpse);
  fixture.state.entity_metadata.source->record_ordinal=20;
  fixture.state.entity_metadata.source->record_identity=20;
  ++fixture.state.publication_revision;
  fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
  REQUIRE(fixture.assets->project_entities(fixture.state,fixture.world));fixture.present();
  REQUIRE(e10_instance(fixture.world,40).studio_asset_index==red_index);

  // The live player changes after the first body has frozen. This must not
  // repaint that body, but must apply to a new body after a remove/reuse gap.
  fixture.identity(2,"\\model\\fixture_green\\topcolor\\120",true);
  fixture.assets->player_slot_boundary(2);
  fixture.state.entity_metadata.source->record_ordinal=21;
  fixture.state.entity_metadata.source->record_identity=21;
  ++fixture.state.publication_revision;
  fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
  REQUIRE(fixture.assets->observe_entities(fixture.state));
  fixture.ready(2,api::PlayerAppearanceStatus::selected_model_ready);
  const auto green_index=e10_instance(fixture.world,2).studio_asset_index;
  REQUIRE(green_index!=red_index);
  CHECK(e10_instance(fixture.world,40).studio_asset_index==red_index);

  const auto retained_frame=fixture.world.entity_frame();
  fixture.state.packet_entities.pop_back();
  fixture.state.entity_metadata.source->record_ordinal=22;
  fixture.state.entity_metadata.source->record_identity=22;
  ++fixture.state.publication_revision;
  fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
  const auto removed=fixture.assets->observe_entities(fixture.state);
  REQUIRE(removed);CHECK_FALSE(removed.frame_changed);
  CHECK(fixture.world.entity_frame()==retained_frame);

  fixture.state.packet_entities.push_back(corpse); // exact server metadata reuse
  fixture.state.entity_metadata.source->record_ordinal=23;
  fixture.state.entity_metadata.source->record_identity=23;
  ++fixture.state.publication_revision;
  fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
  REQUIRE(fixture.assets->observe_entities(fixture.state));fixture.present();
  CHECK(e10_instance(fixture.world,40).studio_asset_index==green_index);
  CHECK(fixture.state.canonical_state_hash==
      client::runtime_observation_canonical_hash(fixture.state));
}

TEST_CASE("71b97 color-only appearance hits borrow the immutable base and preserve exact palette invalidation",
    "[gauss-71b97][appearance][local-capture-assets]") {
  E31AppearanceFixture fixture;
  const auto base=e10_instance(fixture.world,2).studio_asset_index;
  fixture.identity(2,"\\topcolor\\30\\bottomcolor\\240",true);
  fixture.present();
  const auto red=e10_instance(fixture.world,2).studio_asset_index;
  REQUIRE(red!=base);
  auto scene=client::build_render_scene(fixture.world);REQUIRE(scene.dynamic_entities);
  const auto sources=scene.dynamic_entities->package->presentation_studio_sources();
  REQUIRE(sources.size()==1U);
  const auto owner=sources[0].model;
  const auto palette=owner->skeletal_data->textures[0].rgba8_level_zero;
  for(unsigned repeat=0;repeat<64;++repeat) {
    fixture.present();
    CHECK(e10_instance(fixture.world,2).studio_asset_index==red);
    const auto& values=fixture.world.entity_scene()->presentation_studio_sources();
    REQUIRE(values.size()==1U);CHECK(values[0].model==owner);
    CHECK(values[0].model->skeletal_data->textures[0].rgba8_level_zero==palette);
  }
  fixture.identity(2,"\\topcolor\\120\\bottomcolor\\240",false);
  fixture.present();
  const auto green=e10_instance(fixture.world,2).studio_asset_index;
  CHECK(green!=red);CHECK(green!=base);
  REQUIRE(fixture.world.entity_scene()->presentation_studio_sources().size()==2U);
  CHECK(fixture.world.entity_scene()->presentation_studio_sources()[1].model->skeletal_data->textures[0].rgba8_level_zero!=palette);
  // Return to the exact old key reuses the old owner without repainting it.
  fixture.identity(2,"\\topcolor\\30\\bottomcolor\\240",false);fixture.present();
  CHECK(e10_instance(fixture.world,2).studio_asset_index==red);
  CHECK(fixture.world.entity_scene()->presentation_studio_sources()[0].model==owner);
  CHECK(owner->skeletal_data->textures[0].rgba8_level_zero==palette);
  fixture.identity(2,"\\name\\Fixture",false);fixture.present();
  CHECK(e10_instance(fixture.world,2).studio_asset_index==base);
  CHECK(fixture.state.canonical_state_hash==client::runtime_observation_canonical_hash(fixture.state));
}
TEST_CASE("E31 approved player model cache attests distinct source content and bounds requests",
    "[e31][appearance][local-capture-assets]") {
  E31AppearanceFixture fixture;
  hlclient::goldsrc::ApprovedPlayerModelAssets loader{fixture.assets->sound_resources()};
  const auto wait=[&](std::string_view name) {
    auto model=loader.request(name);
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(model.status==hlclient::game_api::PlayerAppearanceStatus::asset_pending && std::chrono::steady_clock::now()<until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));model=loader.request(name);
    }
    return model;
  };
  const auto red=wait("models/player/fixture_red/fixture_red.mdl"),green=wait("models/player/fixture_green/fixture_green.mdl");
  REQUIRE(red.model);REQUIRE(green.model);CHECK(red.revision!=green.revision);
  CHECK(loader.request("models/player/fixture_red/fixture_red.mdl").model==red.model);
  CHECK(loader.request("models/player/../evil.mdl").status==hlclient::game_api::PlayerAppearanceStatus::not_authorized);
  for(std::size_t i=2;i<hlclient::goldsrc::kMaximumPlayerModelRequests;++i) {
    const auto name="models/player/missing"+std::to_string(i)+"/missing"+std::to_string(i)+".mdl";
    CHECK(loader.request(name).status==hlclient::game_api::PlayerAppearanceStatus::asset_pending);
  }
  CHECK(loader.request("models/player/overflow/overflow.mdl").status==hlclient::game_api::PlayerAppearanceStatus::resource_limit);
}
TEST_CASE("E31 late userinfo remaps one shared model independently and rejects incomplete assets without partial publication",
    "[e31][appearance][local-capture-assets]") {
  namespace api=hlclient::game_api;
  E31AppearanceFixture fixture;
  const auto base_index=e10_instance(fixture.world,2).studio_asset_index;
  CHECK(e10_instance(fixture.world,3).studio_asset_index==base_index);
  // The first normal frame already rendered the shared server base model.
  // Userinfo arriving later supplies two independent color variants of one MDL.
  fixture.identity(2,"\\model\\fixture_red\\topcolor\\30\\bottomcolor\\240",true);
  fixture.identity(3,"\\model\\fixture_red\\topcolor\\120\\bottomcolor\\60",true);
  fixture.ready(2,api::PlayerAppearanceStatus::selected_model_ready);
  fixture.ready(3,api::PlayerAppearanceStatus::selected_model_ready);
  const auto first_index=e10_instance(fixture.world,2).studio_asset_index;
  const auto peer_index=e10_instance(fixture.world,3).studio_asset_index;
  CHECK(first_index!=peer_index);CHECK(first_index!=base_index);CHECK(peer_index!=base_index);
  const auto scene=client::build_render_scene(fixture.world);REQUIRE(scene.dynamic_entities);
  const auto sources=scene.dynamic_entities->package->presentation_studio_sources();REQUIRE(sources.size()==2U);
  CHECK(sources[0].model->skeletal_data->textures[0].rgba8_level_zero!=sources[1].model->skeletal_data->textures[0].rgba8_level_zero);
  CHECK(fixture.diagnostic(2).palette_remapped);CHECK(fixture.diagnostic(3).palette_remapped);
  CHECK(fixture.diagnostic(2).attachment_matched_bones>=11U);CHECK(fixture.diagnostic(3).attachment_matched_bones>=11U);
  const auto epoch=fixture.diagnostic(2).appearance.occupant_epoch;
  fixture.identity(2,"\\model\\fixture_green\\topcolor\\240",false);
  fixture.ready(2,api::PlayerAppearanceStatus::selected_model_ready);
  CHECK(fixture.diagnostic(2).appearance.occupant_epoch==epoch);
  CHECK(e10_instance(fixture.world,2).studio_asset_index!=first_index);
  CHECK(e10_instance(fixture.world,3).studio_asset_index==peer_index);

  auto malformed=e17_owned_remote_model(false);malformed[0]=std::byte{0};
  fixture.local.root.write("valve","models/player/malformed/malformed.mdl",malformed);
  fixture.local.root.write("valve","models/player/incomplete/incomplete.mdl",fixture::synthetic_split_texture_main());
  const auto retained=client::build_render_scene(fixture.world).dynamic_entities->package->presentation_studio_sources().size();
  for(const auto model:{"malformed","incomplete"}) {
    fixture.identity(2,std::string("\\model\\")+model,false);
    fixture.ready(2,api::PlayerAppearanceStatus::asset_import_failed);
    CHECK(e10_instance(fixture.world,2).studio_asset_index==base_index);
    CHECK(e10_instance(fixture.world,3).studio_asset_index==peer_index);
    CHECK(client::build_render_scene(fixture.world).dynamic_entities->package->presentation_studio_sources().size()==retained);
  }
  CHECK(fixture.state.packet_entities[1].model_index==7);
  CHECK(fixture.state.packet_entities[2].model_index==7);
  CHECK(fixture.state.canonical_state_hash==client::runtime_observation_canonical_hash(fixture.state));
}
TEST_CASE("E31 selected player models retain server walk crouch fire pose and named pmodel merge",
    "[e31][appearance][local-capture-assets]") {
  E31AppearanceFixture fixture;
  fixture.identity(2,"\\model\\fixture_red",true);
  fixture.identity(3,"\\model\\fixture_green",true);
  fixture.ready(2,hlclient::game_api::PlayerAppearanceStatus::selected_model_ready);
  fixture.ready(3,hlclient::game_api::PlayerAppearanceStatus::selected_model_ready);
  for(std::uint32_t sequence=1;sequence<=3;++sequence) {
    auto& entity=fixture.state.packet_entities[1];entity.sequence=sequence;
    entity.animation_time_seconds=1.0+sequence*.2;entity.frame=64;
    fixture.state.server_time_seconds=entity.animation_time_seconds;
    fixture.state.publication_revision=sequence+1;
    fixture.state.entity_metadata.source->record_identity=sequence+1;
    fixture.state.entity_metadata.source->record_ordinal=sequence+1;
    fixture.state.canonical_state_hash=client::runtime_observation_canonical_hash(fixture.state);
    REQUIRE(fixture.assets->project_entities(fixture.state,fixture.world));
    REQUIRE(e10_present(*fixture.assets,fixture.host,100.0+sequence*.3,1,fixture.world,{640,480}));
    REQUIRE(e10_present(*fixture.assets,fixture.host,100.21+sequence*.3,1,fixture.world,{640,480}));
    CHECK(fixture.diagnostic(2).intent.sample.sequence==sequence);
    CHECK(fixture.diagnostic(2).attachment_matched_bones>=11U);
    CHECK(fixture.diagnostic(2).appearance.status==hlclient::game_api::PlayerAppearanceStatus::selected_model_ready);
    const auto frame=fixture.world.entity_frame();REQUIRE(frame);
    const auto body=e10_instance(fixture.world,2);
    const auto child=std::ranges::find_if(frame->studio_instances(),[](const auto& instance){
      return instance.entity_number==2U && instance.subinstance_ordinal==1U;
    });REQUIRE(child!=frame->studio_instances().end());
    const auto& parent_pose=frame->studio_poses()[body.pose_index].bone_matrices;
    const auto& child_pose=frame->studio_poses()[child->pose_index].bone_matrices;
    REQUIRE(parent_pose.size()>=11U);REQUIRE(child_pose.size()>=11U);
    for(std::size_t bone=0;bone<11;++bone)CHECK(child_pose[bone]==parent_pose[bone]);
    CHECK(fixture.state.packet_entities[1].model_index==7);
  }
}
TEST_CASE("E31 actual OpenGL two simultaneous appearances differ after approved userinfo selection",
    "[e31][appearance][local-capture-assets][actual-context]") {
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if(!gl || !fixture::entity_opengl_fixture::capable_context())SKIP("OpenGL3.3 context unavailable; separate mandatory CPU fixture remains");
  gl->initialize_renderer();E31AppearanceFixture fixture;
  const auto draw=[&] {
    auto scene=client::build_render_scene(fixture.world);scene.static_world.reset();
    gl->renderer().render(scene,{640,480});
    const auto pixels=gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);REQUIRE(pixels);
    REQUIRE(pixels.non_clear_pixel_count>0U);CHECK(glGetError()==GL_NO_ERROR);return pixels;
  };
  const auto baseline=draw();
  fixture.identity(2,"\\model\\fixture_red\\topcolor\\30",true);
  fixture.identity(3,"\\model\\fixture_green\\topcolor\\120",true);
  fixture.ready(2,hlclient::game_api::PlayerAppearanceStatus::selected_model_ready);
  fixture.ready(3,hlclient::game_api::PlayerAppearanceStatus::selected_model_ready);
  const auto selected=draw();CHECK_FALSE(std::ranges::equal(baseline.rgba8,selected.rgba8));
  REQUIRE(gl->renderer().entity_statistics().studio_draw_count>=4U);
  std::size_t warm=0,green=0;
  for(std::size_t i=0;i+3<selected.rgba8.size();i+=4) {
    const auto r=static_cast<unsigned>(selected.rgba8[i]),g=static_cast<unsigned>(selected.rgba8[i+1]);
    if(r>g*3/2 && r>20)++warm;if(g>r*3/2 && g>20)++green;
  }
  CHECK(warm>20U);CHECK(green>20U);
  const auto count=gl->renderer().entity_statistics().studio_asset_upload_count;
  for(int i=0;i<3;++i){fixture.present();draw();}
  CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==count);
  std::string output_directory;
#ifdef _WIN32
  char* output=nullptr;std::size_t output_size=0;
  if(_dupenv_s(&output,&output_size,"HLCLIENT_E31_APPEARANCE_OUTPUT_DIR")==0 && output)output_directory=output;
  std::free(output);
#else
  if(const char* output=std::getenv("HLCLIENT_E31_APPEARANCE_OUTPUT_DIR"))output_directory=output;
#endif
  if(!output_directory.empty()) {
    const std::filesystem::path directory{output_directory};
    const auto save=[&](std::string_view name,const auto& pixels) {
      std::ofstream file(directory/std::string(name),std::ios::binary);REQUIRE(file.good());
      file<<"P6\n640 480\n255\n";
      for(std::size_t y=480;y>0;--y)for(std::size_t x=0;x<640;++x)
        file.write(reinterpret_cast<const char*>(pixels.rgba8.data()+((y-1)*640+x)*4),3);
      REQUIRE(file.good());
    };
    save("appearance-before.ppm",baseline);save("appearance-after.ppm",selected);
  }
  std::cout<<"e31_appearance_gpu=passed project_owned=true simultaneous=2 warm_pixels="<<warm
      <<" green_pixels="<<green<<" live_state=false\n";
  gl->release_renderer();
}

TEST_CASE("E31 optional approved Gordon and Barney use canonical model importer and isolated palettes read only",
    "[e31][appearance][installed-player-appearance][local-capture-assets]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value=nullptr;std::size_t count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value)root=value;
  std::free(value);
#else
  if(const char* value=std::getenv("HLCLIENT_LOCAL_GAME_ROOT"))root=value;
#endif
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("Exact opt-in approved research root not provided");
  constexpr std::array<std::string_view,3> names{"maps/crossfire.bsp","models/player/gordon/gordon.mdl","models/player/barney/barney.mdl"};
  std::array<std::filesystem::file_time_type,3> times;
  for(std::size_t i=0;i<names.size();++i) {
    if(!std::filesystem::is_regular_file(root/"valve"/names[i]))SKIP("Optional player appearance resource absent");
    times[i]=std::filesystem::last_write_time(root/"valve"/names[i]);
  }
  const auto resources=readiness::parse_resource_list({{2U,"maps/crossfire.bsp",9U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info(names[0]),root,"valve");
  REQUIRE(created.projection);
  hlclient::goldsrc::ApprovedPlayerModelAssets loader{created.projection->sound_resources()};
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};host.reset({1,1,1});
  std::array<hlclient::goldsrc::PreparedPlayerModel,2> prepared;
  for(std::size_t i=0;i<prepared.size();++i) {
    auto result=loader.request(names[i+1]);const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(result.status==hlclient::game_api::PlayerAppearanceStatus::asset_pending && std::chrono::steady_clock::now()<until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));result=loader.request(names[i+1]);
    }
    INFO(hlclient::game_api::to_string(result.status));REQUIRE(result.model);REQUIRE(result.model->skeletal_data);
    CHECK(host.player_model_compatible(*result.model->skeletal_data));
    hlclient::game_api::PlayerAppearanceFields fields;fields.top_color=120;fields.bottom_color=240;
    const auto ranges=host.player_palette(fields,*result.model->skeletal_data);REQUIRE(ranges.size()==2U);
    const auto remapped=hlclient::goldsrc::remap_player_model_palette(*result.model,ranges);REQUIRE(remapped);
    const auto index=ranges.front().texture;
    CHECK(remapped->skeletal_data->textures[index].rgba8_level_zero!=result.model->skeletal_data->textures[index].rgba8_level_zero);
    for(std::size_t texture=0;texture<result.model->skeletal_data->textures.size();++texture)
      if(texture!=index)CHECK(remapped->skeletal_data->textures[texture].rgba8_level_zero==result.model->skeletal_data->textures[texture].rgba8_level_zero);
    prepared[i]=std::move(result);
  }
  CHECK(prepared[0].revision!=prepared[1].revision);
  for(std::size_t i=0;i<names.size();++i)CHECK(times[i]==std::filesystem::last_write_time(root/"valve"/names[i]));
  std::cout<<"e31_appearance_original_import=passed models=gordon,barney palette=isolated read_only=true live_state=false\n";
}

TEST_CASE("E17 owned approved remote Shotgun merges server movement crouch fire and life boundaries",
          "[e17][shotgun-pmodel][game-api][local-capture-assets][actual-context]") {
  LocalContext context;
  const auto player=e17_owned_remote_model(false),weapon=e17_owned_remote_model(true);
  context.root.write("valve","models/project_player.mdl",player);
  context.root.write("valve","models/p_shotgun.mdl",weapon);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/project_player.mdl",7U,static_cast<std::uint32_t>(player.size()),0U},
      {2U,"models/p_shotgun.mdl",22U,static_cast<std::uint32_t>(weapon.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  e17_remote_shotgun_merge_control(*created.projection,{0,0,16},{1U,2U,3U,4U},false);
}

client::RuntimeClientObservationState e17_shotgun_viewmodel_state() {
  auto state=e16_mp5_viewmodel_state();
  state.receiving_client->owned_weapon_bits=1U<<7U;
  state.weapon_hud.active_weapon_id=std::uint8_t{7U};
  state.weapon_hud.clips.fill(std::nullopt);state.weapon_hud.clips[7U]=8;
  state.weapon_hud.reserve_ammo.fill(std::nullopt);state.weapon_hud.reserve_ammo[2U]=16;
  state.weapon_hud.catalogue.clear();
  state.weapon_hud.catalogue.push_back({.id=7U,.command_name="weapon_shotgun",.primary_ammo_type=2,
      .secondary_ammo_type=-1,.slot=2,.source={.record_identity=1U,.record_ordinal=1U}});
  state.weapon_slots.clear();
  state.weapon_slots.push_back({.wire_slot=7U,.clip=8,.in_reload=false,.next_primary_attack=0.0,
      .weapon_id=7U,.next_secondary_attack=0.0,.time_weapon_idle=0.0,.special_reload_state=0});
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  return state;
}

std::pair<hlclient::renderer::RenderDynamicEntities,hlclient::renderer::RenderDynamicEntities>
e17_materialized_shotgun_action(app::RuntimeReplayLocalAssets& assets,
    const client::RuntimeClientObservationState& state,std::uint16_t buttons,
    bool interior_animation=false) {
  namespace api=hlclient::game_api;
  const auto metadata=assets.presentation_model(state.generation,21U);REQUIRE(metadata);
  REQUIRE(hlclient::games::halflife::shotgun_sequences(*metadata));
  const bool double_shot=buttons==hlclient::goldsrc::kReferenceGoldSrcButtonAttack2;
  const auto expected_sequence=double_shot ? 2U : 1U;
  REQUIRE(metadata->sequences[expected_sequence].events.size()==1U);
  CHECK(metadata->sequences[expected_sequence].events[0U].event_number==5001);
  CHECK(metadata->sequences[expected_sequence].events[0U].frame==0);
  const auto canonical=client::runtime_observation_canonical_hash(state);
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({state.generation,1U,1U});host.set_movement_audio_time_origin(0.0);
  host.bind_model(metadata);host.observe(state,0.0);(void)host.drain_audio();
  api::LocalWeaponSubmittedCommand accepted{state.generation,1U,buttons,.02};
  accepted.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{
      {100,200,64},{1,0,0},{0,-1,0},{0,0,1}};
  host.submit(accepted,.02);
  const auto started=host.sample(.02);REQUIRE(started.visual);REQUIRE(started.identity);
  CHECK(started.action==(double_shot ? api::LocalWeaponAction::secondary_fire : api::LocalWeaponAction::primary_fire));
  const auto intent=host.viewmodel(state,metadata,.02,started.visual);
  REQUIRE(intent.status==api::ViewmodelStatus::ready);
  CHECK(intent.sequence==expected_sequence);CHECK(intent.body==2U);CHECK(intent.frame_coordinate==0.0);
  const auto first=assets.materialize_viewmodel(intent);REQUIRE(first);REQUIRE(first->frame);
  REQUIRE(first->frame->studio_instances().size()==1U);
  CHECK(first->frame->studio_instances()[0U].body_value==2U);
  REQUIRE(assets.viewmodel_attachment_at_frame(intent,0U,0.0));

  // A changed render-camera context must not relocate the accepted-shot origin.
  api::LocalVisualContext view{{999,888,777},{0,1,0},{1,0,0},{0,0,1},{},.02};
  const auto effects=host.local_visuals(view);
  REQUIRE(effects.flash);REQUIRE(effects.light);REQUIRE(effects.shell);REQUIRE(effects.world_impact);
  CHECK(effects.flash->sequence==expected_sequence);
  CHECK(effects.flash->variant==(double_shot ? 41U : 31U));
  CHECK(effects.extra_shell_count==(double_shot ? 1U : 0U));
  CHECK(effects.extra_impact_count==(double_shot ? 7U : 3U));
  CHECK(effects.world_impact->maximum_distance_units==2048.0F);CHECK_FALSE(effects.world_impact->tracer);
  CHECK(effects.world_impact->origin.x==100.0F);CHECK(effects.world_impact->origin.y==200.0F);
  CHECK(effects.shell->origin.x==132.0F);CHECK(effects.shell->origin.y==194.0F);
  CHECK(effects.shell->origin.z==52.0F);
  CHECK(effects.shell->expires_at_seconds-effects.shell->starts_at_seconds==Catch::Approx(2.5));
  const auto shell_name=[](const auto& shell) {
    return std::string_view{shell.model_name.data(),
        static_cast<std::size_t>(std::find(shell.model_name.begin(),shell.model_name.end(),'\0')-shell.model_name.begin())};
  };
  CHECK(shell_name(*effects.shell)=="models/shotgunshell.mdl");
  hlclient::renderer::TransientVisuals shells;
  REQUIRE(shells.spawn(*effects.shell,.02));
  for(std::size_t i=0;i<effects.extra_shell_count;++i) {
    CHECK(shell_name(effects.extra_shells[i])=="models/shotgunshell.mdl");
    CHECK(effects.extra_shells[i].action!=effects.shell->action);
    REQUIRE(shells.spawn(effects.extra_shells[i],.02));
  }
  CHECK_FALSE(shells.spawn(*effects.shell,.02));shells.update(.02);
  REQUIRE(shells.shells().size()==(double_shot ? 2U : 1U));
  REQUIRE(assets.has_shell_model("models/shotgunshell.mdl"));
  const auto rendered_shells=assets.materialize_shells(shells.shells(),.02);
  REQUIRE(rendered_shells);REQUIRE(rendered_shells->frame);
  REQUIRE(rendered_shells->frame->studio_instances().size()==shells.shells().size());
  const auto shell_asset=rendered_shells->frame->studio_instances()[0U].studio_asset_index;
  for(const auto& shell:rendered_shells->frame->studio_instances()) CHECK(shell.studio_asset_index==shell_asset);
  // Both resources are ready: a named shotshell must not silently use brass.
  if(assets.has_shell_model("models/shell.mdl")) {
    auto brass=shells.shells()[0U];brass.model_name.fill('\0');
    constexpr std::string_view name="models/shell.mdl";std::copy(name.begin(),name.end(),brass.model_name.begin());
    const auto other=assets.materialize_shells(std::span{&brass,1U},.02);REQUIRE(other);REQUIRE(other->frame);
    CHECK(other->frame->studio_instances()[0U].studio_asset_index!=shell_asset);
  }
  auto bad=shells.shells()[0U];bad.model_name.fill('\0');
  constexpr std::string_view absent="models/not_authorized.mdl";
  std::copy(absent.begin(),absent.end(),bad.model_name.begin());
  CHECK_FALSE(assets.materialize_shells(std::span{&bad,1U},.02));
  const auto audio=host.drain_audio();REQUIRE(audio.count==1U);
  CHECK(audio.cues[0U].reference.name()==(double_shot ? "weapons/dbarrel1.wav" : "weapons/sbarrel1.wav"));
  CHECK(audio.cues[0U].kind==api::LocalSoundKind::fire);

  // Installed animation may move only hidden parts during its first fraction
  // of a frame. The optional visual control uses an interior metadata-derived
  // point; fixed early sampling remains the mandatory authored-fixture test.
  const auto elapsed=interior_animation ?
      0.25*static_cast<double>(metadata->sequences[expected_sequence].frame_count-1U)/
          metadata->sequences[expected_sequence].fps : .06;
  const auto later_time=.02+elapsed;
  const auto later=host.sample(later_time);REQUIRE(later.visual);
  const auto advanced=host.viewmodel(state,metadata,later_time,later.visual);
  CHECK(advanced.sequence==expected_sequence);CHECK(advanced.body==2U);
  CHECK(advanced.frame_coordinate==Catch::Approx(elapsed*metadata->sequences[expected_sequence].fps));
  CHECK(later.visual->restart_identity==started.visual->restart_identity);
  const auto second=assets.materialize_viewmodel(advanced);REQUIRE(second);REQUIRE(second->frame);
  const auto& a=first->frame->studio_instances()[0U];const auto& b=second->frame->studio_instances()[0U];
  CHECK(a.studio_asset_index==b.studio_asset_index);CHECK(first->package==second->package);
  CHECK_FALSE(std::ranges::equal(first->frame->studio_poses()[a.pose_index].bone_matrices,
      second->frame->studio_poses()[b.pose_index].bone_matrices));
  host.submit(accepted,later_time);const auto repeated=host.sample(later_time);REQUIRE(repeated.visual);
  CHECK(repeated.visual->restart_identity==started.visual->restart_identity);
  view.now_seconds=later_time;const auto duplicate=host.local_visuals(view);
  CHECK_FALSE(duplicate.shell);CHECK(duplicate.extra_shell_count==0U);
  CHECK_FALSE(duplicate.world_impact);CHECK(duplicate.extra_impact_count==0U);
  CHECK(host.drain_audio().count==0U);
  shells.update(2.6);CHECK(shells.shells().empty());
  CHECK(client::runtime_observation_canonical_hash(state)==canonical);
  CHECK(state.weapon_slots[0U].clip==8);CHECK(state.weapon_hud.reserve_ammo[2U]==16U);
  return {*first,*second};
}

using E17FramePair=std::pair<hlclient::renderer::RenderDynamicEntities,hlclient::renderer::RenderDynamicEntities>;
std::array<E17FramePair,3U> e17_materialized_reload(app::RuntimeReplayLocalAssets& assets,
    const client::RuntimeClientObservationState& initial,bool interior_animation=false) {
  namespace api=hlclient::game_api;
  auto state=initial;state.weapon_slots[0U].clip=2;state.weapon_hud.clips[7U]=2;
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  const auto metadata=assets.presentation_model(state.generation,21U);REQUIRE(metadata);
  api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({state.generation,1U,1U});host.set_movement_audio_time_origin(0.0);
  host.bind_model(metadata);host.observe(state,0.0);(void)host.drain_audio();
  host.submit({state.generation,1U,hlclient::goldsrc::kReferenceGoldSrcButtonReload,.02},.02);
  const auto elapsed=[&](std::uint32_t sequence) {
    // Inspected installed insert frame2.25 moves the left hand/shell below
    // the 60-degree view frustum, while weapon bones29/30 stay unchanged
    // through frame4. At frame6 the shell enters the frustum and weapon
    // bone29 also moves. Observe that actual visible insert phase, not only
    // a hidden CPU-palette change; authored-fixture sampling stays 40ms.
    const auto fraction=sequence==3U ? 2.0/3.0 : 0.25;
    return interior_animation ? fraction*static_cast<double>(metadata->sequences[sequence].frame_count-1U)/
        metadata->sequences[sequence].fps : .04;
  };
  const auto materialize=[&](std::uint32_t sequence,double now) -> E17FramePair {
    const auto start=host.sample(now);REQUIRE(start.visual);CHECK(start.visual->sequence==sequence);
    const auto intent=host.viewmodel(state,metadata,now,start.visual);
    REQUIRE(intent.status==api::ViewmodelStatus::ready);CHECK(intent.body==0U);
    const auto first=assets.materialize_viewmodel(intent);REQUIRE(first);REQUIRE(first->frame);
    const auto later_time=now+elapsed(sequence);
    const auto advanced=host.sample(later_time);REQUIRE(advanced.visual);
    CHECK(advanced.visual->restart_identity==start.visual->restart_identity);
    const auto later=host.viewmodel(state,metadata,later_time,advanced.visual);
    CHECK(later.sequence==sequence);CHECK(later.frame_coordinate>intent.frame_coordinate);
    const auto second=assets.materialize_viewmodel(later);REQUIRE(second);REQUIRE(second->frame);
    const auto& a=first->frame->studio_instances()[0U];const auto& b=second->frame->studio_instances()[0U];
    CHECK_FALSE(std::ranges::equal(first->frame->studio_poses()[a.pose_index].bone_matrices,
        second->frame->studio_poses()[b.pose_index].bone_matrices));
    CHECK(host.drain_audio().count==0U); // Pump/reload audio remains server-authoritative.
    api::LocalVisualContext context{{0,0,64},{1,0,0},{0,-1,0},{0,0,1},{},later_time};
    const auto effects=host.local_visuals(context);CHECK_FALSE(effects.flash);CHECK_FALSE(effects.shell);
    CHECK_FALSE(effects.world_impact);CHECK(effects.extra_shell_count==0U);CHECK(effects.extra_impact_count==0U);
    return {*first,*second};
  };
  const auto fresh=[&](std::int32_t stage,std::int16_t clip,std::uint8_t reserve,double now) {
    ++state.publication_revision;state.client_metadata.source->record_identity=state.publication_revision;
    REQUIRE(state.publication_revision<=std::numeric_limits<std::size_t>::max());
    state.client_metadata.source->record_ordinal=static_cast<std::size_t>(state.publication_revision);
    state.weapon_slots[0U].special_reload_state=stage;state.weapon_slots[0U].clip=clip;
    state.weapon_hud.clips[7U]=clip;state.weapon_hud.reserve_ammo[2U]=reserve;
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);host.observe(state,now);
  };
  const auto start=materialize(5U,.02);
  CHECK(state.weapon_slots[0U].clip==2);CHECK(state.weapon_hud.reserve_ammo[2U]==16U);
  fresh(1,2,16,std::max(.08,.04+elapsed(5U)));fresh(2,2,16,.64);
  const auto insert=materialize(3U,.64);
  const auto insert_sample_time=.64+elapsed(3U);
  const auto insert_identity=host.sample(insert_sample_time).visual->restart_identity;
  const auto repeat_time=std::max(.70,insert_sample_time+.02);
  host.observe(state,repeat_time);CHECK(host.sample(repeat_time).visual->restart_identity==insert_identity);
  fresh(1,3,15,1.16);CHECK(host.sample(1.16).visual->sequence==3U);
  CHECK(host.sample(1.16).visual->restart_identity==insert_identity);
  fresh(2,3,15,1.18);CHECK(host.sample(1.18).visual->restart_identity!=insert_identity);
  fresh(0,8,10,1.75);const auto finish=materialize(4U,1.75);
  CHECK(state.weapon_slots[0U].clip==8);CHECK(state.weapon_hud.reserve_ammo[2U]==10U);
  CHECK(client::runtime_observation_canonical_hash(state)==state.canonical_state_hash);
  return {start,insert,finish};
}

TEST_CASE("E17 approved project-owned Shotgun host animates single double reload and materializes named shotshells",
          "[e17][local-capture-assets][first-person][game-api][shotshell]") {
  LocalContext context;const auto model=e17_owned_shotgun_viewmodel();
  context.root.write("valve","models/v_shotgun.mdl",model);
  context.root.write("valve","models/shell.mdl",context.model);
  auto shotshell=context.model;fixture::studio_write_fixed_string(shotshell,8U,64U,"owned_shotshell.mdl");
  context.root.write("valve","models/shotgunshell.mdl",shotshell);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/v_shotgun.mdl",21U,static_cast<std::uint32_t>(model.size()),0U},
      {2U,"models/shell.mdl",22U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"models/shotgunshell.mdl",23U,static_cast<std::uint32_t>(shotshell.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : "ready"));REQUIRE(created.projection);
  const auto state=e17_shotgun_viewmodel_state();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  for(const auto buttons:{hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                         hlclient::goldsrc::kReferenceGoldSrcButtonAttack2}) {
    CAPTURE(buttons);(void)e17_materialized_shotgun_action(*created.projection,state,buttons);
  }
  (void)e17_materialized_reload(*created.projection,state);
}

TEST_CASE("E17 normal Shotgun single double and reload alter GPU pixels and stay camera local",
          "[e17][local-capture-assets][first-person][opengl][actual-context]") {
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  LocalContext context;const auto model=e17_owned_shotgun_viewmodel();
  context.root.write("valve","models/v_shotgun.mdl",model);
  context.root.write("valve","models/shotgunshell.mdl",context.model);
  const auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/v_shotgun.mdl",21U,static_cast<std::uint32_t>(model.size()),0U},
      {2U,"models/shotgunshell.mdl",23U,static_cast<std::uint32_t>(context.model.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);const auto state=e17_shotgun_viewmodel_state();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  std::vector<E17FramePair> pairs;
  for(const auto buttons:{hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                         hlclient::goldsrc::kReferenceGoldSrcButtonAttack2})
    pairs.push_back(e17_materialized_shotgun_action(*created.projection,state,buttons));
  for(const auto& pair:e17_materialized_reload(*created.projection,state)) pairs.push_back(pair);
  hlclient::renderer::RenderScene scene;
  const auto draw=[&] {
    gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
    return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
  };
  const auto baseline=draw();REQUIRE(baseline);
  for(std::size_t i=0;i<pairs.size();++i) {
    CAPTURE(i);const auto& [first,second]=pairs[i];
    scene.first_person_entities=first;const auto initial=draw();REQUIRE(initial);
    CHECK(initial.non_clear_pixel_count>0U);CHECK_FALSE(std::ranges::equal(initial.rgba8,baseline.rgba8));
    const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
    scene.first_person_entities=second;const auto animated=draw();REQUIRE(animated);
    CHECK(animated.non_clear_pixel_count>0U);CHECK_FALSE(std::ranges::equal(initial.rgba8,animated.rgba8));
    scene.camera.position={123,-77,28};scene.camera.target={123,-76.5F,28.866F};
    const auto turned=draw();REQUIRE(turned);CHECK(std::ranges::equal(animated.rgba8,turned.rgba8));
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
  }
  scene.first_person_entities.reset();CHECK(std::ranges::equal(draw().rgba8,baseline.rgba8));
  gl->release_renderer();
}

// The same independent asset provider now consumes the concrete module's
// neutral selection, rather than interpreting weapon state itself.
struct HalfLifeViewmodelFixture {
  app::RuntimeReplayLocalAssets& assets;
  hlclient::games::halflife::HalfLifePresentation presentation;
  std::optional<hlclient::renderer::RenderDynamicEntities> operator()(
      const client::RuntimeClientObservationState& state, double now,
      std::optional<hlclient::game_api::LocalWeaponVisual> local = {}) {
    const auto index = state.receiving_client
        ? state.receiving_client->viewmodel_index.value_or(0U) : 0U;
    return assets.materialize_viewmodel(presentation.viewmodel(
        state, assets.presentation_model(state.generation,index),now,local));
  }
};
TEST_CASE("First-person binding follows the exact sparse viewmodel and clears on no weapon",
          "[local-capture-assets][first-person][damage-respawn]") {
  LocalContext context;
  context.root.write("valve", "models/v_shared.mdl", context.model);
  context.root.write("valve", "models/v_other.mdl", context.model);
  auto resources = readiness::parse_resource_list({
      {2U, "maps/test_map.bsp", 9U,
       static_cast<std::uint32_t>(context.map.size()), 0U},
      {2U, "models/v_shared.mdl", 7U,
       static_cast<std::uint32_t>(context.model.size()), 0U},
      {2U, "models/v_other.mdl", 8U,
       static_cast<std::uint32_t>(context.model.size()), 0U},
      {2U, "models/shared.mdl", 15U,
       static_cast<std::uint32_t>(context.model.size()), 0U}});
  auto created = app::RuntimeReplayLocalAssets::create(
      resources, readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(), "valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : ""));
  REQUIRE(created.projection);
  HalfLifeViewmodelFixture viewmodel{*created.projection};
  client::ClientWorldState world;
  auto state = observation();
  state.receiving_client.emplace();
  state.client_metadata = {
      1U, client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{1U, 1U, 42U, 0U, 8U}};
  state.receiving_client->viewmodel_index = 7U;
  state.weapon_hud.active_weapon_id = std::uint8_t{2U};
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->reset_generation(state, world));
  const auto shown = viewmodel(state, 1.0);
  REQUIRE(shown);
  REQUIRE(shown->frame);
  CHECK(shown->frame->studio_instances().size() == 1U);
  CHECK(shown->frame->studio_instances()[0].transform.origin.x == 0.0F);
  CHECK(shown->frame->studio_instances()[0].transform.origin.y == 0.0F);
  CHECK(shown->frame->studio_instances()[0].transform.origin.z == -1.0F);
  CHECK(shown->frame->studio_instances()[0].transform.uniform_scale == 1.0F);
  const auto turned = viewmodel(state, 1.01);
  REQUIRE(turned);
  CHECK(turned->frame->studio_instances()[0].transform.rotation_degrees.x == 0.0F);
  CHECK(turned->frame->studio_instances()[0].transform.rotation_degrees.y == 0.0F);
  CHECK(turned->frame->studio_instances()[0].transform.rotation_degrees.z == 0.0F);
  state.receiving_client->viewmodel_index = 8U;
  CHECK_FALSE(viewmodel(state, 1.015));
  state.weapon_hud.active_weapon_id = std::uint8_t{4U};
  const auto other = viewmodel(state, 1.02);
  REQUIRE(other);
  CHECK(other->package == shown->package);
  CHECK(other->frame->studio_instances()[0].studio_asset_index !=
        shown->frame->studio_instances()[0].studio_asset_index);
  state.receiving_client->viewmodel_index = 7U;
  state.weapon_hud.active_weapon_id = std::uint8_t{2U};
  const auto returned = viewmodel(state, 1.025);
  REQUIRE(returned);
  CHECK(returned->frame->studio_instances()[0].studio_asset_index ==
        shown->frame->studio_instances()[0].studio_asset_index);
  state.receiving_client->viewmodel_index = 15U; // p/w/world namespace, not v_*.mdl.
  CHECK_FALSE(viewmodel(state, 1.02));
  state.receiving_client->viewmodel_index = 0U;
  CHECK_FALSE(viewmodel(state, 1.04));
  state.receiving_client->viewmodel_index = 7U;
  CHECK(viewmodel(state, 1.06));
  state.weapon_hud.active_weapon_id = std::uint8_t{0U};
  CHECK_FALSE(viewmodel(state, 1.08));
  state.weapon_hud.active_weapon_id = std::uint8_t{2U};
  state.lifecycle.life_epoch = 1;
  state.lifecycle.deaths = 1;
  state.lifecycle.state = client::LocalPlayerLifeState::dead;
  state.lifecycle.last_death_source = client::RuntimeObservationSource{2,2,43,0,8};
  state.receiving_client->health = 0;
  CHECK_FALSE(viewmodel(state,1.1));
  state.lifecycle.life_epoch = 2;
  state.lifecycle.state = client::LocalPlayerLifeState::alive;
  state.receiving_client->health = 81;
  CHECK_FALSE(viewmodel(state,1.2)); // no stale dead binding
  state.weapon_hud.active_source = client::RuntimeObservationSource{3,3,44,0,8};
  const auto new_life = viewmodel(state,1.3);
  REQUIRE(new_life);
  CHECK(new_life->package == shown->package);
  CHECK(new_life->frame->studio_instances()[0].studio_asset_index ==
        shown->frame->studio_instances()[0].studio_asset_index);
}

TEST_CASE("Opt-in local Valve viewmodel resolves through the production Studio binding",
          "[local-game-assets][first-person]") {
  std::string root_path;
#ifdef _WIN32
  char* local_root = nullptr;
  std::size_t local_root_size = 0U;
  if (_dupenv_s(&local_root, &local_root_size,
                "HLCLIENT_LOCAL_GAME_ROOT") == 0 && local_root) {
    root_path = local_root;
    std::free(local_root);
  }
#else
  if (const char* local_root = std::getenv("HLCLIENT_LOCAL_GAME_ROOT"))
    root_path = local_root;
#endif
  if (root_path.empty()) { SKIP("No opt-in local game asset root"); }
  for (const auto model_name : {"v_crowbar.mdl", "v_9mmhandgun.mdl", "v_357.mdl"}) {
  const auto source = std::filesystem::path{root_path} /
      "valve/models" / model_name;
  INFO("model=" << model_name);
  if (!std::filesystem::is_regular_file(source)) {
    SKIP("Local Valve viewmodel unavailable");
  }
  LocalContext context;
  const auto size = std::filesystem::file_size(source);
  REQUIRE(size > 0U);
  REQUIRE(size <= 2U * 1024U * 1024U);
  std::vector<std::byte> model(static_cast<std::size_t>(size));
  std::ifstream input(source, std::ios::binary);
  REQUIRE(input.is_open());
  input.read(reinterpret_cast<char*>(model.data()),
             static_cast<std::streamsize>(model.size()));
  REQUIRE(input.good());
  const auto resource_name = std::string{"models/"} + model_name;
  context.root.write("valve", resource_name, model);
  auto resources = readiness::parse_resource_list({
      {2U, "maps/test_map.bsp", 9U,
       static_cast<std::uint32_t>(context.map.size()), 0U},
      {2U, resource_name, 7U,
       static_cast<std::uint32_t>(model.size()), 0U}});
  auto created = app::RuntimeReplayLocalAssets::create(
      resources, readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(), "valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : ""));
  REQUIRE(created.projection);
  HalfLifeViewmodelFixture viewmodel{*created.projection};
  client::ClientWorldState world;
  auto state = observation();
  state.packet_entities.clear();
  state.receiving_client.emplace();
  state.receiving_client->health = 100.0;
  state.receiving_client->viewmodel_index = 7U;
  state.client_metadata = {
      1U, client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{1U, 1U, 42U, 0U, 8U}};
  state.weapon_hud.active_weapon_id = std::uint8_t{
      std::string_view{model_name} == "v_crowbar.mdl" ? 1U :
      std::string_view{model_name} == "v_357.mdl" ? 3U : 2U};
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->reset_generation(state, world));
  hlclient::renderer::RenderCamera camera;
  camera.position = {0.0, 0.0, 32.0};
  camera.target = {1.0, 0.0, 32.0};
  const auto shown = viewmodel(state, 1.0);
  INFO("imported_studio=" << created.projection->summary().imported_studio);
  INFO("viewmodel_status=" << app::to_string(
      created.projection->first_person_status()));
  REQUIRE(world.entity_scene());
  std::string material_detail;
  for (const auto& asset : world.entity_scene()->studio_assets()) {
    if (!asset) continue;
    for (const auto& material : asset->materials()) {
      material_detail += std::to_string(material.source_flags) + ":" +
          std::to_string(static_cast<int>(material.support_status)) + ",";
    }
  }
  INFO("materials=" << material_detail);
  if (std::string_view{model_name} == "v_357.mdl") {
    const auto& materials = world.entity_scene()->studio_assets().back()->materials();
    CHECK(std::ranges::count_if(materials,[](const auto& material) {
      return material.support_status == hlclient::entity_render::StudioRenderMaterialSupportStatus::supported_chrome;
    }) == 3);
  }
  REQUIRE(shown);
  INFO("draw_commands=" << shown->frame->draw_commands().size()
       << " visible=" << shown->frame->statistics().visible_count);
  CHECK(shown->frame->studio_instances().size() == 1U);
  auto gl = hlclient::tests::entity_opengl_fixture::try_context(1280, 720);
  if (!gl) { SKIP("OpenGL 3.3 Core context unavailable for local model"); }
  gl->initialize_renderer();
  auto scene = client::build_render_scene(world);
  scene.camera = camera;
  scene.first_person_entities = *shown;
  gl->renderer().render(scene, {1280, 720});
  const auto with_sample = gl->renderer().observe_framebuffer(
      {1280, 720}, scene.clear_color, true);
  scene.first_person_entities.reset();
  gl->renderer().render(scene, {1280, 720});
  const auto without_sample = gl->renderer().observe_framebuffer(
      {1280, 720}, scene.clear_color, true);
  REQUIRE(with_sample);
  REQUIRE(without_sample);
  const bool pixels_distinct = with_sample.rgba8 != without_sample.rgba8;
  CHECK(pixels_distinct);
  CHECK(with_sample.color_signature != without_sample.color_signature);
  // With no world behind it, the entire first-person image is invariant to
  // world camera position and yaw/pitch. This uses the production provider,
  // real MDL, shared Studio shader and actual OpenGL pass.
  scene.static_world.reset();
  scene.first_person_entities = *shown;
  scene.camera = camera;
  gl->renderer().render(scene, {1280, 720});
  const auto local_baseline = gl->renderer().observe_framebuffer(
      {1280, 720}, scene.clear_color);
  REQUIRE(local_baseline);
  CHECK(local_baseline.non_clear_pixel_count > 0U);
  constexpr float radians = 0.01745329251994329577F;
  for (const auto [pitch, yaw] : std::array<std::pair<float, float>, 8U>{
           {{30.0F, 0.0F}, {-30.0F, 0.0F}, {80.0F, 0.0F},
            {-80.0F, 0.0F}, {0.0F, 90.0F}, {0.0F, 180.0F},
            {30.0F, 90.0F}, {-80.0F, 180.0F}}}) {
    scene.camera.position = {123.0F, -77.0F, 28.0F};
    const float horizontal = std::cos(pitch * radians);
    scene.camera.target = {
        scene.camera.position.x + horizontal * std::cos(yaw * radians),
        scene.camera.position.y + horizontal * std::sin(yaw * radians),
        scene.camera.position.z + std::sin(pitch * radians)};
    gl->renderer().render(scene, {1280, 720});
    const auto local_turned = gl->renderer().observe_framebuffer(
        {1280, 720}, scene.clear_color);
    REQUIRE(local_turned);
    INFO("pitch=" << pitch << " yaw=" << yaw);
    CHECK(local_turned.color_signature == local_baseline.color_signature);
    CHECK(local_turned.minimum_x == local_baseline.minimum_x);
    CHECK(local_turned.minimum_y == local_baseline.minimum_y);
    CHECK(local_turned.maximum_x == local_baseline.maximum_x);
    CHECK(local_turned.maximum_y == local_baseline.maximum_y);
  }
  const auto metadata = created.projection->presentation_model(state.generation, *state.receiving_client->viewmodel_index);
  REQUIRE(metadata);
  const bool glock = *state.weapon_hud.active_weapon_id == 2U;
  const bool python = *state.weapon_hud.active_weapon_id == 3U;
  CHECK(metadata->sequences.size() >= (python ? 8U : glock ? 10U : 9U));
  const auto initial_uploads = gl->renderer().entity_statistics().studio_asset_upload_count;
  std::uint64_t restart = 100U;
  double animation_time = 3.0;
  std::optional<std::uint64_t> idle_signature;
  for (const auto seq : glock ? std::vector<std::uint32_t>{0U,3U,6U,0U}
                             : python ? std::vector<std::uint32_t>{0U,2U,3U,0U}
                             : std::vector<std::uint32_t>{0U,4U,0U}) {
    REQUIRE(seq < metadata->sequences.size());
    const auto& data = metadata->sequences[seq];
    REQUIRE(data.fps > 0.0); REQUIRE(data.frame_count > 1U);
    const double duration = static_cast<double>(data.frame_count - 1U) / data.fps;
    const std::uint8_t body = glock ? (seq == 3U ? std::uint8_t{2U} : std::uint8_t{0U})
                                   : std::uint8_t{1U};
    hlclient::game_api::LocalWeaponVisual action{seq,body,++restart,animation_time,
        hlclient::game_api::LocalWeaponAnimationSource::provisional_command,
        hlclient::game_api::LocalWeaponActionStatus::predicted_pending};
    scene.first_person_entities = viewmodel(
        state,animation_time + duration * 0.35,action);
    REQUIRE(scene.first_person_entities);
    scene.camera = camera;
    gl->renderer().render(scene,{1280,720});
    const auto animated = gl->renderer().observe_framebuffer({1280,720},scene.clear_color);
    REQUIRE(animated); CHECK(animated.non_clear_pixel_count > 0U);
    if (seq == 0U) {
      if (idle_signature) CHECK(animated.color_signature == *idle_signature);
      else idle_signature = animated.color_signature;
    } else {
      REQUIRE(idle_signature); CHECK(animated.color_signature != *idle_signature);
    }
    for (const float pitch : {80.0F,-80.0F}) {
      scene.camera.position = {123,-77,28};
      scene.camera.target = {123 + std::cos(pitch*radians),-77,28 + std::sin(pitch*radians)};
      gl->renderer().render(scene,{1280,720});
      const auto turned = gl->renderer().observe_framebuffer({1280,720},scene.clear_color);
      REQUIRE(turned); CHECK(turned.color_signature == animated.color_signature);
      CHECK(turned.minimum_x == animated.minimum_x); CHECK(turned.maximum_y == animated.maximum_y);
      CHECK(glGetError() == GL_NO_ERROR);
    }
    animation_time += duration + 0.1;
  }
  CHECK(gl->renderer().entity_statistics().studio_asset_upload_count == initial_uploads);
  gl->release_renderer();
  }
}
#endif

TEST_CASE("Local visual omissions retain exact decoded observations and "
          "separate visual hash",
          "[local-capture-assets][coverage]") {
  LocalContext context;
  auto created = context.create();
  REQUIRE(created.projection);
  auto state = observation();
  const auto original = state.canonical_state_hash;
  const auto visual_hash = client::runtime_observation_visual_hash(state);
  state.packet_entities[0].model_index = 0U;
  CHECK(client::runtime_observation_canonical_hash(state) == original);
  CHECK(client::runtime_observation_visual_hash(state) != visual_hash);
  state.packet_entities[1].model_index = 27U;
  client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state, world));
  CHECK(created.projection->summary().rendered_instances == 0U);
  CHECK(created.projection->summary().coverage.at(
            app::ReplayLocalVisualStatus::absent_model) == 1U);
  CHECK(created.projection->summary().coverage.at(
            app::ReplayLocalVisualStatus::invalid_brush_reference) == 1U);
  state.packet_entities[0].model_index = 20U;
  state.packet_entities[1].model_index = 999U;
  REQUIRE(created.projection->project_entities(state, world));
  CHECK(created.projection->summary().coverage.at(
            app::ReplayLocalVisualStatus::missing_asset) == 1U);
  CHECK(created.projection->summary().coverage.at(
            app::ReplayLocalVisualStatus::unknown_model_slot) == 1U);
  state.packet_entities[0].controllers[0] = 256U;
  CHECK_FALSE(client::valid_runtime_observation(state));
}

TEST_CASE("Server item hide absence and return retain imported assets and never change inventory",
          "[pickups][local-capture-assets][visibility][game-module][opengl]") {
  LocalContext context;
  auto created = context.create();
  REQUIRE(created.projection);
  auto state = observation();
  state.receiving_client.emplace();
  state.receiving_client->owned_weapon_bits = 1U << 2U;
  state.client_metadata = {1U,client::RuntimeObservationFreshness::observed_in_record,
      client::RuntimeObservationCompleteness::complete_reconstruction,
      client::RuntimeObservationSource{1U,1U,42U,0U,8U}};
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  const auto package = world.entity_scene();
  REQUIRE(package);
  REQUIRE(world.entity_frame()->studio_instances().size() == 2U);
  auto gl = hlclient::tests::entity_opengl_fixture::try_context(1280,720);
  if (!gl) SKIP("Actual OpenGL context unavailable");
  gl->initialize_renderer();
  hlclient::renderer::RenderScene scene;
  scene.camera = client::build_render_scene(world).camera;
  const auto render = [&] {
    scene.dynamic_entities = hlclient::renderer::RenderDynamicEntities{
        world.entity_scene(),world.entity_frame(),{}};
    gl->renderer().render(scene,{1280,720});
  };
  render();
  const auto uploads = gl->renderer().entity_statistics().studio_asset_upload_count;
  REQUIRE(uploads > 0U);
  state.packet_entities[0].effects = 128U;
  ++state.publication_revision;
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->project_entities(state,world));
  CHECK(world.entity_frame()->studio_instances().size() == 1U);
  CHECK(created.projection->summary().coverage.at(app::ReplayLocalVisualStatus::hidden_by_effects) == 1U);
  render();
  auto retained = state.packet_entities[0];
  state.packet_entities.erase(state.packet_entities.begin()); // reconstructed full-set absence
  ++state.publication_revision;
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->project_entities(state,world));
  CHECK(world.entity_frame()->studio_instances().size() == 1U);
  render();
  retained.effects = 0U;
  state.packet_entities.insert(state.packet_entities.begin(),retained);
  ++state.publication_revision;
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->project_entities(state,world));
  CHECK(world.entity_frame()->studio_instances().size() == 2U);
  render();
  CHECK(world.entity_scene() == package);
  CHECK(created.projection->summary().imported_studio == 1U);
  CHECK(state.receiving_client->owned_weapon_bits == (1U << 2U));
  CHECK(gl->renderer().entity_statistics().studio_asset_upload_count == uploads);
  gl->release_renderer();
}

TEST_CASE("Recorded positive model size mismatch cannot silently substitute a "
          "local file",
          "[local-capture-assets][security]") {
  LocalContext context;
  const auto created = context.create(true);
  CHECK_FALSE(created.projection);
  REQUIRE(created.error);
  CHECK(created.error->context.find("size mismatch") != std::string::npos);
}

TEST_CASE("Studio render modes preserve amount and tint instead of hiding the model",
          "[local-capture-assets][studio-translucency]") {
  LocalContext context;auto created=context.create();REQUIRE(created.projection);
  auto state=observation();state.packet_entities.resize(1U);client::ClientWorldState world;
  for(const auto mode:{0U,1U,2U,3U,4U,5U}) {
    auto& entity=state.packet_entities[0];entity.render_mode=mode;entity.render_amount=64U;entity.render_color={32U,128U,255U};
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    const auto projected=created.projection->reset_generation(state,world);
    INFO((projected.error ? projected.error->context : "ready"));REQUIRE(projected);
    REQUIRE(world.entity_frame());REQUIRE(world.entity_frame()->studio_instances().size()==1U);
    const auto& instance=world.entity_frame()->studio_instances()[0];
    CHECK(instance.blend_mode==(mode==0U ? hlclient::entity_render::StudioBlendMode::opaque :
        mode==5U ? hlclient::entity_render::StudioBlendMode::additive : hlclient::entity_render::StudioBlendMode::alpha_blended));
    CHECK(instance.opacity==Catch::Approx(mode==0U ? 1.0F : 64.0F/255.0F));
    CHECK(instance.tint[0]==Catch::Approx(mode==1U ? 32.0F/255.0F : 1.0F));
    CHECK(instance.tint[1]==Catch::Approx(mode==1U ? 128.0F/255.0F : 1.0F));
  }
}

TEST_CASE("Live local map validation uses the raw BSP lump CRC and assigned slot",
          "[local-capture-assets][live-resource-policy][map-crc][security]") {
  LocalContext context;
  context.map = fixture::literal_minimal_goldsrc_bsp_v30();
  // Pinned independently from the literal bytes, not from the implementation.
  constexpr std::uint32_t expected_crc = 0x6c71'e117U;
  REQUIRE(fixture_map_crc(context.map) == expected_crc);
  fixture_replace_map_entities(context.map,
      "{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n");
  REQUIRE(fixture_map_crc(context.map) == expected_crc);
  context.root.write("valve", "maps/test_map.bsp", context.map);
  for (const auto slot : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{7}}) {
    INFO(static_cast<unsigned>(slot));
    const auto created = create_live_resource_fixture(
        context, context.resources(), fixture_server_map_crc(expected_crc, slot));
    INFO((created.error ? created.error->context : "ready"));
    REQUIRE(created.projection);
    CHECK(created.projection->summary().world_crc_verified);
    CHECK(created.projection->summary().size_mismatches == 0U);
  }
}

TEST_CASE("Live download sizes are diagnostic for valid local BSP Studio and sprite assets",
          "[local-capture-assets][live-resource-policy][map-crc][security]") {
  LocalContext context;
  const auto sprite = muzzle_fixture_sprite();
  context.root.write("valve", "sprites/local.spr", sprite);
  const auto server = fixture_server_map_crc(fixture_map_crc(context.map));
  for (const unsigned mismatch_mask : {1U, 2U, 4U, 7U}) {
    INFO(mismatch_mask);
    const auto resources = readiness::parse_resource_list({
        {2U, "maps/test_map.bsp", 9U,
         static_cast<std::uint32_t>(context.map.size()) + ((mismatch_mask & 1U) ? 17U : 0U), 0U},
        {2U, "models/shared.mdl", 7U,
         static_cast<std::uint32_t>(context.model.size()) + ((mismatch_mask & 2U) ? 1U : 0U), 0U},
        {2U, "sprites/local.spr", 16U,
         static_cast<std::uint32_t>(sprite.size()) - ((mismatch_mask & 4U) ? 1U : 0U), 0U},
    });
    const auto created = create_live_resource_fixture(context, resources, server);
    INFO((created.error ? created.error->context : "ready"));
    REQUIRE(created.projection);
    const auto& summary = created.projection->summary();
    CHECK(summary.world_crc_verified);
    CHECK(summary.imported_studio == 1U);
    CHECK(summary.imported_sprites == 1U);
    CHECK(summary.size_mismatches == static_cast<std::size_t>(std::popcount(mismatch_mask)));
    CHECK(summary.size_matches + summary.size_mismatches == 3U);

    // Selecting the live camera is not authorization to weaken a capture's
    // default exact-size contract; the new resource policy must be explicit.
    const auto recorded = app::RuntimeReplayLocalAssets::create(resources, server,
        context.root.path(), "valve",
        app::ReplayLocalCameraPolicy::external_live_receiving_client);
    CHECK_FALSE(recorded.projection);
    REQUIRE(recorded.error);
    CHECK(recorded.error->context.find("size mismatch") != std::string::npos);
  }
}

TEST_CASE("Live local map entity-only edits and unreferenced padding retain the server CRC",
          "[local-capture-assets][live-resource-policy][map-crc][security]") {
  LocalContext context;
  const auto original_crc = fixture_map_crc(context.map);
  const auto resources = context.resources(); // Original server download size.
  context.map.insert(context.map.end(), 31U, std::byte{0x5a});
  fixture_replace_map_entities(context.map,
      "{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n"
      "\"message\" \"A local entity-only edit is not a geometry mismatch\"\n}\n");
  REQUIRE(fixture_map_crc(context.map) == original_crc);
  context.root.write("valve", "maps/test_map.bsp", context.map);
  const auto created = create_live_resource_fixture(
      context, resources, fixture_server_map_crc(original_crc));
  INFO((created.error ? created.error->context : "ready"));
  REQUIRE(created.projection);
  CHECK(created.projection->summary().world_crc_verified);
  CHECK(created.projection->summary().size_mismatches == 1U);
}

TEST_CASE("Live local map geometry mismatch and multiplayer zero checksum fail closed",
          "[local-capture-assets][live-resource-policy][map-crc][security]") {
  LocalContext context;
  const auto original_crc = fixture_map_crc(context.map);
  REQUIRE(original_crc != 0U);
  std::uint32_t advertised_crc = original_crc;
  SECTION("same-size nonentity edit cannot pass the former size-only check") {
    const auto offset = fixture::synthetic_read_u32le(context.map,
        fixture::synthetic_lump_descriptor_offset(fixture::SyntheticBspLumpId::vertices));
    fixture::synthetic_write_f32le(context.map, offset, 1.0F);
    REQUIRE(fixture_map_crc(context.map) != original_crc);
    context.root.write("valve", "maps/test_map.bsp", context.map);
  }
  SECTION("zero is not an unavailable multiplayer CRC") {
    advertised_crc = 0U;
  }
  SECTION("finalized CRC32 is not the raw map CRC") {
    advertised_crc = original_crc ^ 0xffff'ffffU;
  }
  const auto created = create_live_resource_fixture(
      context, context.resources(), fixture_server_map_crc(advertised_crc));
  CHECK_FALSE(created.projection);
  REQUIRE(created.error);
  CHECK(created.error->context.starts_with("map_crc_mismatch"));
}

TEST_CASE("Live singleplayer uses its explicit zero map checksum convention",
          "[local-capture-assets][live-resource-policy][map-crc][security]") {
  LocalContext context;
  const auto created = create_live_resource_fixture(
      context, context.resources(), fixture_server_map_crc(0U, 0U, 1U));
  INFO((created.error ? created.error->context : "ready"));
  REQUIRE(created.projection);
  CHECK_FALSE(created.projection->summary().world_crc_verified);
  const auto unexpected_crc = create_live_resource_fixture(
      context, context.resources(), fixture_server_map_crc(fixture_map_crc(context.map), 0U, 1U));
  CHECK_FALSE(unexpected_crc.projection);
  REQUIRE(unexpected_crc.error);
  CHECK(unexpected_crc.error->context.starts_with("map_crc_mismatch"));
}

TEST_CASE("Live size tolerance cannot bypass malformed local BSP validation",
          "[local-capture-assets][live-resource-policy][map-crc][security]") {
  LocalContext context;
  const auto original_crc = fixture_map_crc(context.map);
  const auto resources = context.resources();
  const auto descriptor = fixture::synthetic_lump_descriptor_offset(
      fixture::SyntheticBspLumpId::planes);
  SECTION("truncated header") { context.map.resize(100U); }
  SECTION("unsupported version") {
    fixture::synthetic_write_u32le(context.map, 0U, 29U);
  }
  SECTION("negative lump length") {
    fixture::synthetic_write_u32le(context.map, descriptor + 4U, 0xffff'ffffU);
  }
  SECTION("out-of-bounds lump offset") {
    fixture::synthetic_write_u32le(context.map, descriptor,
        static_cast<std::uint32_t>(context.map.size()));
  }
  SECTION("nonempty lump overlapping the header") {
    fixture::synthetic_write_u32le(context.map, descriptor, 4U);
  }
  SECTION("overlapping nonempty lumps") {
    const auto vertices_descriptor = fixture::synthetic_lump_descriptor_offset(
        fixture::SyntheticBspLumpId::vertices);
    fixture::synthetic_write_u32le(context.map, vertices_descriptor,
        fixture::synthetic_read_u32le(context.map, descriptor));
  }
  context.root.write("valve", "maps/test_map.bsp", context.map);
  const auto created = create_live_resource_fixture(
      context, resources, fixture_server_map_crc(original_crc));
  CHECK_FALSE(created.projection);
  REQUIRE(created.error);
  CHECK(created.error->context.starts_with("map_crc_invalid_bsp"));
}

TEST_CASE("Nonbindable checker rows cannot consume genuine missing-resource diagnostics",
          "[local-capture-assets][live-resource-policy][map-crc][security]") {
  LocalContext context;
  std::vector<resource_list_test_fixture::EntrySpec> rows;
  for (unsigned index = 0U; index < 40U; ++index)
    rows.push_back({3U, "checker_" + std::to_string(index) + ".dll", 4095U, 1U, 0U});
  rows.push_back({2U, "maps/test_map.bsp", 9U,
      static_cast<std::uint32_t>(context.map.size()), 0U});
  rows.push_back({2U, "models/genuinely_missing.mdl", 7U, 64U, 0U});
  const auto created = create_live_resource_fixture(context,
      readiness::parse_resource_list(rows), fixture_server_map_crc(fixture_map_crc(context.map)));
  INFO((created.error ? created.error->context : "ready"));
  REQUIRE(created.projection);
  const auto& summary = created.projection->summary();
  CHECK(summary.captured_resources == rows.size());
  CHECK(summary.world_crc_verified);
  CHECK(summary.resource_issues_omitted == 0U);
  REQUIRE(summary.resource_issues.size() == 1U);
  CHECK(summary.resource_issues.front().virtual_name == "models/genuinely_missing.mdl");
  CHECK(summary.resource_issues.front().resource_index == 7U);
  CHECK(summary.resource_issues.front().resource_type == 2U);
}

TEST_CASE("Live brush references reject world zero syntax range and overflow without fallback",
          "[live-brush][local-capture-assets][binding]") {
  LocalContext context(true);
  for (const auto name : {"*0", "*3", "*+1", "*4294967296"}) {
    INFO(name);
    auto created = app::RuntimeReplayLocalAssets::create(
        context.resources(false, name), readiness::parse_server_info("maps/test_map.bsp"),
        context.root.path(), "valve");
    REQUIRE(created.projection);
    auto state = observation();
    state.packet_entities[0].model_index = 27U;
    state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
    client::ClientWorldState world;
    REQUIRE(created.projection->reset_generation(state, world));
    CHECK(world.runtime_brushes()->instances.empty());
    CHECK(world.entity_frame()->studio_instances().size() == 1U);
    const auto& summary = created.projection->summary();
    CHECK(summary.coverage.at(app::ReplayLocalVisualStatus::invalid_brush_reference) == 1U);
    REQUIRE(summary.first_brush_rejection);
    CHECK(summary.first_brush_rejection->model_slot == 27U);
    CHECK_FALSE(summary.first_brush_rejection->submodel);
  }
}

TEST_CASE("Live brushes bind sparse slots and atomically replace only committed instances",
          "[live-brush][live-visual][local-capture-assets]") {
  LocalContext context(true);
  auto created = context.create();
  INFO((created.error ? created.error->context : "ready"));
  REQUIRE(created.projection);
  auto state = observation();
  state.packet_entities.clear();
  for (const auto number : {80U, 81U}) {
    client::RuntimePacketEntityObservation entity;
    entity.entity_number = number; entity.model_index = 27U;
    entity.ordinary_visual_schema = true;
    entity.origin = {0.0, 0.0, number == 80U ? 0.0 : 16.0};
    entity.angles = {0.0, 0.0, 0.0};
    state.packet_entities.push_back(entity);
  }
  auto hash = [&] { state.canonical_state_hash = client::runtime_observation_canonical_hash(state); };
  hash();
  client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state, world));
  REQUIRE(world.runtime_brushes());
  const auto scene = world.world_scene();
  REQUIRE(scene);
  CHECK(scene->brush_instances().empty());
  CHECK(scene->world_package()->surface_ranges().size() == 1U);
  CHECK(scene->brush_library().models().size() == 2U);
  CHECK(scene->brush_library().render_package()->surface_ranges().size() == 2U);
  CHECK(scene->brush_library().render_package()->textured_world().textures.textures()[0].name == "BRUSH_ONLY");
  CHECK(scene->brush_library().render_package()->lightmaps().page_count() == 1U);
  REQUIRE(world.runtime_brushes()->instances.size() == 2U);
  CHECK(world.runtime_brushes()->instances[0].model_slot == 27U);
  CHECK(world.runtime_brushes()->instances[0].source_model_index == 1U);
  CHECK(world.runtime_brushes()->instances[0].transformed_bounds.minimum.z == 0.0F);
  CHECK(world.runtime_brushes()->instances[1].transformed_bounds.minimum.z == 16.0F);
  CHECK(created.projection->summary().submitted_brushes == 2U);
  const auto collision = created.projection->collision_world_package();
  for (const auto offset : {20.0, 40.0, 60.0}) {
    state.packet_entities[0].origin = {offset, 0.0, 10.0};
    state.packet_entities[0].angles = {0.0, 90.0, 0.0};
    ++state.publication_revision; hash();
    REQUIRE(created.projection->project_entities(state, world));
    REQUIRE(world.runtime_brushes()->instances.size() == 2U);
    CHECK(world.runtime_brushes()->instances[0].transformed_bounds.minimum.x == Catch::Approx(offset - 64.0));
    CHECK(world.runtime_brushes()->instances[0].transformed_bounds.maximum.x == Catch::Approx(offset).margin(0.0001));
    CHECK(world.world_scene() == scene);
    CHECK(created.projection->collision_world_package() == collision);
  }
  const auto retained = world.runtime_brushes();
  CHECK(created.projection->summary().brush_transform_changes==3U);
  CHECK(created.projection->summary().last_changed_brush_entity==80U);
  CHECK(created.projection->summary().last_changed_brush_model==1U);
  created.projection->acknowledge_unchanged(); // Production clientdata-only dispatch retains the frame.
  CHECK(world.runtime_brushes() == retained);
  CHECK(created.projection->summary().brush_transform_changes==3U);
  auto invalid = state; invalid.generation = 9U;
  CHECK_FALSE(created.projection->project_entities(invalid, world));
  CHECK(world.runtime_brushes() == retained);
  CHECK_FALSE(created.projection->reset_generation(invalid, world));
  CHECK(world.runtime_brushes() == retained);
  state.packet_entities[0].effects = 128U;
  state.packet_entities[1].render_mode = 4U;
  ++state.publication_revision; hash();
  REQUIRE(created.projection->project_entities(state, world));
  REQUIRE(world.runtime_brushes()->instances.size() == 1U);
  CHECK(created.projection->summary().hidden_brushes == 1U);
  CHECK(world.runtime_brushes()->instances[0].entity_number == 81U);
  state.packet_entities[0].effects = 0U;
  state.packet_entities[1].model_index = 7U; // same entity number now Studio
  state.packet_entities[1].render_mode = 0U;
  ++state.publication_revision; hash();
  REQUIRE(created.projection->project_entities(state, world));
  CHECK(world.runtime_brushes()->instances.size() == 1U);
  CHECK(world.entity_frame()->studio_instances().size() == 1U);
  state.packet_entities.clear(); ++state.publication_revision; hash();
  REQUIRE(created.projection->project_entities(state, world));
  CHECK(world.runtime_brushes()->instances.empty());
  client::RuntimePacketEntityObservation reused;
  reused.entity_number = 80U; reused.model_index = 27U;
  reused.ordinary_visual_schema = true;
  reused.origin = {123.0, 0.0, 0.0}; reused.angles = {0.0, 0.0, 0.0};
  state.packet_entities.push_back(reused); ++state.publication_revision; hash();
  REQUIRE(created.projection->project_entities(state, world));
  REQUIRE(world.runtime_brushes()->instances.size() == 1U);
  CHECK(world.runtime_brushes()->instances[0].transformed_bounds.minimum.x == 123.0F);
  world.reset();
  CHECK_FALSE(world.runtime_brushes());
  CHECK_FALSE(world.world_scene());
}

TEST_CASE("Production brush instances change actual pixels depth and culling without uploads",
          "[live-brush][opengl][live-visual]") {
  auto gl = fixture::entity_opengl_fixture::try_context(320, 240);
  if (!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  LocalContext context(true);
  auto created = context.create();
  REQUIRE(created.projection);
  auto state = observation(); state.packet_entities.resize(1);
  auto& entity = state.packet_entities[0];
  entity.model_index = 27U; entity.origin = {0.0,0.0,16.0}; entity.angles = {0.0,0.0,0.0};
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state, world));
  world.set_camera({{32.0F,32.0F,160.0F},{32.0F,32.0F,0.0F},{0.0F,1.0F,0.0F}});
  auto draw = [&] {
    const auto scene = client::build_render_scene(world);
    gl->renderer().render(scene, {320,240});
    CHECK(glGetError() == GL_NO_ERROR);
    return gl->renderer().observe_framebuffer({320,240},scene.clear_color, true);
  };
  const auto with = draw();
  REQUIRE(with);
  SECTION("D4 derived brush motion is render-only and updates actual pixels") {
    auto source=state;
    source.packet_entities.front().solid=4;
    source.packet_entities.front().brush_move_type=7;
    source.entity_metadata={source.generation,client::RuntimeObservationFreshness::observed_in_record,
        client::RuntimeObservationCompleteness::complete_reconstruction,client::RuntimeObservationSource{100}};
    const auto library=hlclient::goldsrc::collision::build_brush_collision_model_library(created.projection->collision_world_package());
    REQUIRE(library);
    auto current=hlclient::goldsrc::build_reference_brush_collision(library.library,{{27,"*1"}},source,source.generation);
    REQUIRE(current.scene); current.server_time=1.1;
    source.packet_entities.front().origin.z=6.;
    auto previous=hlclient::goldsrc::build_reference_brush_collision(library.library,{{27,"*1"}},source,source.generation);
    previous.record=99; previous.server_time=1.;
    hlclient::goldsrc::ReferenceVerticalSupportMotion motion{previous,current}; REQUIRE(motion.active());
    const auto committed=world.runtime_brushes();
    auto scene=client::build_render_scene(world);
    const auto sampled=motion.sample(1.2); REQUIRE(sampled);
    scene.static_world->runtime_brushes=app::present_runtime_brushes(*committed,*sampled,state.generation);
    REQUIRE(scene.static_world->runtime_brushes);
    const auto& instance=scene.static_world->runtime_brushes->instances.front();
    CHECK(instance.model_transform.values[14]==Catch::Approx(26.));
    CHECK(instance.transformed_bounds.minimum.z==Catch::Approx(committed->instances.front().transformed_bounds.minimum.z+10.));
    CHECK(instance.touched_leaf_indices.empty());
    CHECK(world.runtime_brushes()==committed);
    CHECK(committed->instances.front().model_transform.values[14]==16.F);
    std::vector<hlclient::goldsrc::SoundSource> sound_sources;
    created.projection->sound_sources(state,sampled.get(),sound_sources);
    REQUIRE(sound_sources.size()==1);
    CHECK(sound_sources.front().entity==entity.entity_number);
    CHECK(sound_sources.front().position.z==Catch::Approx(
        (instance.transformed_bounds.minimum.z+instance.transformed_bounds.maximum.z)*0.5F));
    const auto audible_center=sound_sources.front().position;
    auto hidden=state; hidden.packet_entities.front().effects=128U;
    created.projection->sound_sources(hidden,sampled.get(),sound_sources);
    REQUIRE(sound_sources.size()==1);
    CHECK(sound_sources.front().position.z==audible_center.z);
    CHECK_FALSE(app::present_runtime_brushes(*committed,*sampled,state.generation+1));
    gl->renderer().render(scene,{320,240}); CHECK(glGetError()==GL_NO_ERROR);
    const auto moved=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
    REQUIRE(moved); CHECK(moved.rgba8!=with.rgba8);
    auto removed=*committed; removed.instances.clear();
    CHECK(app::present_runtime_brushes(removed,*sampled,state.generation)->instances.empty());
  }
  const auto uploads = gl->renderer().statistics().brush_upload_count;
  const auto frame = world.runtime_brushes();
  REQUIRE(world.set_runtime_brushes({}));
  const auto without = draw();
  CHECK(with.rgba8 != without.rgba8);
  REQUIRE(world.set_runtime_brushes(frame));
  entity.origin.z = -16.0; // behind the opaque world quad
  ++state.publication_revision;
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->project_entities(state,world));
  CHECK(draw().rgba8 == without.rgba8);
  entity.origin = {10000.0,0.0,16.0};
  ++state.publication_revision;
  state.canonical_state_hash = client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->project_entities(state,world));
  CHECK(draw().rgba8 == without.rgba8);
  CHECK(gl->renderer().statistics().runtime_brush_culled_count > 0U);
  CHECK(gl->renderer().statistics().brush_upload_count == uploads);
  gl->release_renderer();
}
TEST_CASE("Brush cutout and server texture state preserve depth without a Studio evaluator",
          "[live-brush][opengl][material]") {
  auto gl = fixture::entity_opengl_fixture::try_context(320,240);
  if (!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  for (const bool alternate : {false,true}) {
    LocalContext context(true,alternate ? "+0SWITCH" : "{FENCE",alternate);
    auto created = context.create();
    INFO((created.error ? created.error->context : "ready")); REQUIRE(created.projection);
    auto state = observation(); state.packet_entities.resize(1);
    auto& entity = state.packet_entities[0];
    entity.model_index=27U; entity.origin={0.0,0.0,16.0}; entity.angles={0.0,0.0,0.0}; entity.render_mode=4U;
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    client::ClientWorldState world; REQUIRE(created.projection->reset_generation(state,world));
    world.set_camera({{32.0F,32.0F,160.0F},{32.0F,32.0F,0.0F},{0.0F,1.0F,0.0F}});
    auto draw = [&] { auto scene=client::build_render_scene(world); gl->renderer().render(scene,{320,240});
      CHECK(glGetError()==GL_NO_ERROR); return gl->renderer().observe_framebuffer({320,240},scene.clear_color,true); };
    const auto first=draw(); REQUIRE(first);
    const auto uploads=gl->renderer().statistics().uploaded_base_texture_count;
    if (alternate) {
      REQUIRE(world.world_scene()->brush_library().alternate_textures());
      CHECK(world.world_scene()->brush_library().alternate_textures()->texture_count()==1U);
      entity.frame=1.0; ++state.publication_revision;
      state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
      REQUIRE(created.projection->project_entities(state,world));
      CHECK(draw().rgba8 != first.rgba8);
    } else {
      const auto& package=*world.world_scene()->brush_library().render_package();
      CHECK(package.materials()[0].base_texture_alpha_mode==hlclient::assets::WorldTextureAlphaMode::masked_index_255);
      REQUIRE(world.set_runtime_brushes({}));
      const auto without=draw();
      // Camera looks straight down: the transparent left half exposes world pixels.
      const auto pixel=[](const auto& image,int x,int y) { return image.rgba8[(y*320+x)*4]; };
      CHECK(pixel(first,140,120)==pixel(without,140,120));
      CHECK(pixel(first,180,120)!=pixel(without,180,120));
    }
    CHECK(gl->renderer().statistics().uploaded_base_texture_count==uploads);
  }
  gl->release_renderer();
}

TEST_CASE("E15 optional approved MP5 models expose imported sequence and socket metadata",
          "[e15][e16][installed-mp5][local-capture-assets]") {
  std::filesystem::path root;
  wchar_t* value=nullptr;std::size_t count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
  std::free(value);
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("No exact opt-in approved research root");
  const std::array tokens{"maps/crossfire.bsp","models/v_9mmAR.mdl","models/p_9mmAR.mdl","models/shell.mdl"};
  std::array<std::filesystem::file_time_type,4> times;
  for(std::size_t i=0;i<tokens.size();++i) {
    if(!std::filesystem::is_regular_file(root/"valve"/tokens[i])) SKIP("Optional MP5 asset absent");
    times[i]=std::filesystem::last_write_time(root/"valve"/tokens[i]);
  }
  const auto resources=readiness::parse_resource_list({{2U,tokens[0],9U,0U,0U},
      {2U,tokens[1],21U,0U,0U},{2U,tokens[2],22U,0U,0U},{2U,tokens[3],23U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info(tokens[0]),root,"valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  for(const auto slot:{21U,22U,23U}) {
    const auto model=created.projection->studio_model_data(slot); REQUIRE(model);
    std::cout << "e15_asset virtual=" << tokens[slot-20U] << " bones=" << model->bones.size()
      << " attachments=" << model->attachments.size() << " sequences=" << model->sequences.size()
      << " bodyparts=" << model->bodyparts.size() << " skins=" << model->skin_families.size()
      << " materials=" << model->textures.size() << '\n';
    for(std::size_t i=0;i<model->sequences.size();++i) {
      const auto& s=model->sequences[i];
      std::cout << "sequence=" << i << " label=" << s.label << " frames=" << s.frame_count
        << " fps=" << s.frames_per_second << '\n';
      for(const auto& e:s.events) std::cout << "marker=" << e.event_number << " frame=" << e.frame << '\n';
    }
    for(const auto& a:model->attachments) std::cout << "attachment=" << a.name << " bone=" << a.bone_index
      << " origin=" << a.local_origin.x << ',' << a.local_origin.y << ',' << a.local_origin.z << '\n';
    for(std::size_t i=0;i<model->bones.size();++i) {
      const auto& b=model->bones[i]; hlclient::assets::AssetVector3 lo{1e9F,1e9F,1e9F},hi{-1e9F,-1e9F,-1e9F};
      std::size_t n=0;
      for(const auto& sub:model->submodels) for(const auto& v:sub.vertices) if(v.position_bone_index==i) {
        ++n;const auto p=v.source_position;
        lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
      }
      std::cout << "bone=" << i << " name=" << b.name << " parent=" << b.parent_index
        << " translation=" << b.default_translation.x << ',' << b.default_translation.y << ',' << b.default_translation.z
        << " rotation=" << b.default_rotation_radians.x << ',' << b.default_rotation_radians.y << ',' << b.default_rotation_radians.z
        << " vertices=" << n << " bounds=" << lo.x << ',' << lo.y << ',' << lo.z << ':' << hi.x << ',' << hi.y << ',' << hi.z << '\n';
    }
  }
#if HLCLIENT_HAS_GAME_HALFLIFE
  // Actual imported metadata must pass the same gate and normal-host path as
  // the project-owned regression above; no copied or fabricated local pose.
  const auto state=e16_mp5_viewmodel_state();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  const bool gpu_available=gl && fixture::entity_opengl_fixture::capable_context();
  if (gpu_available) gl->initialize_renderer();
  else WARN("Optional MP5 device control unavailable; approved importer/host/Studio proof still runs");
  for (const auto buttons:{hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                           hlclient::goldsrc::kReferenceGoldSrcButtonAttack2}) {
    CAPTURE(buttons);
    const auto [first,second]=e16_materialized_mp5_action(*created.projection,state,buttons);
    if (gpu_available) {
      hlclient::renderer::RenderScene scene;scene.first_person_entities=first;
      gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
      const auto initial=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
      REQUIRE(initial);REQUIRE(initial.non_clear_pixel_count>0U);
      const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
      scene.first_person_entities=second;
      gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
      const auto animated=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
      REQUIRE(animated);REQUIRE(animated.non_clear_pixel_count>0U);
      CHECK_FALSE(std::ranges::equal(initial.rgba8,animated.rgba8));
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    }
  }
  if (gpu_available) gl->release_renderer();
#endif
  for(std::size_t i=0;i<tokens.size();++i) CHECK(times[i]==std::filesystem::last_write_time(root/"valve"/tokens[i]));
  hlclient::goldsrc::ApprovedSoundAssets sounds{created.projection->sound_resources()};
  for(const std::string_view sample:{"weapons/hks1.wav","weapons/hks2.wav"}) {
    hlclient::game_api::LocalSoundReference reference;
    reference.source=hlclient::game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    std::copy(sample.begin(),sample.end(),reference.sample.begin());
    auto result=sounds.request_local(reference);
    for(unsigned retry=0;retry<1000 && result.status==hlclient::goldsrc::SoundAssetStatus::pending;++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});result=sounds.request_local(reference);
    }
    REQUIRE(result.status==hlclient::goldsrc::SoundAssetStatus::ready);REQUIRE(result.asset);
    CHECK(std::any_of(result.asset->interleaved_samples.begin(),result.asset->interleaved_samples.end(),
        [](float x){return std::isfinite(x)&&x!=0;}));
  }
}

#if HLCLIENT_HAS_GAME_HALFLIFE
TEST_CASE("E17 optional approved Shotgun models and sounds pass normal importer host and Studio path read only",
          "[e17][installed-shotgun][shotgun-pmodel][local-capture-assets][first-person]") {
  std::filesystem::path root;
  wchar_t* value=nullptr;std::size_t count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
  std::free(value);
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("No exact opt-in approved research root");
  constexpr std::array<std::string_view,11U> tokens{
      "maps/crossfire.bsp","models/v_shotgun.mdl","models/p_shotgun.mdl","models/shotgunshell.mdl",
      "models/shell.mdl","sound/weapons/sbarrel1.wav","sound/weapons/dbarrel1.wav",
      "sound/weapons/reload1.wav","sound/weapons/scock1.wav","sound/weapons/sshell1.wav","models/player.mdl"};
  std::array<std::filesystem::file_time_type,tokens.size()> times;
  for(std::size_t i=0;i<tokens.size();++i) {
    if(!std::filesystem::is_regular_file(root/"valve"/tokens[i])) SKIP("Optional Shotgun resource absent");
    times[i]=std::filesystem::last_write_time(root/"valve"/tokens[i]);
  }
  const auto resources=readiness::parse_resource_list({
      {2U,std::string{tokens[0U]},9U,0U,0U},{2U,std::string{tokens[1U]},21U,0U,0U},
      {2U,std::string{tokens[2U]},22U,0U,0U},{2U,std::string{tokens[3U]},23U,0U,0U},
      {2U,std::string{tokens[4U]},24U,0U,0U},
      {2U,std::string{tokens[10U]},7U,0U,0U},
      {0U,"weapons/reload1.wav",31U,0U,0U},{0U,"weapons/scock1.wav",32U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info(tokens[0U]),
      root,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : "ready"));REQUIRE(created.projection);
  const auto v=created.projection->studio_model_data(21U);
  const auto p=created.projection->studio_model_data(22U);
  const auto shell=created.projection->studio_model_data(23U);
  REQUIRE(v);REQUIRE(p);REQUIRE(shell);
  CHECK(v->bones.size()==32U);CHECK(v->attachments.size()==1U);REQUIRE(v->sequences.size()==10U);
  CHECK(v->bodyparts.size()==2U);CHECK(v->skin_families.size()==1U);
  CHECK(p->bones.size()==12U);CHECK(p->attachments.empty());CHECK(p->sequences.size()==1U);
  CHECK(shell->bones.size()==1U);CHECK(shell->attachments.empty());CHECK(shell->sequences.size()==1U);
  constexpr std::array<std::string_view,10U> labels{
      "sm_idle","shoot","shoot_big","reload","pump","start_reload","draw","reholster","idle4","deepidle"};
  constexpr std::array<std::uint32_t,10U> frames{21U,21U,21U,10U,11U,9U,13U,11U,21U,61U};
  constexpr std::array<double,10U> fps{9,20,13,16,13,12,24,30,9,12};
  for(std::size_t i=0;i<labels.size();++i) {
    CAPTURE(i);CHECK(v->sequences[i].label==labels[i]);CHECK(v->sequences[i].frame_count==frames[i]);
    CHECK(v->sequences[i].frames_per_second==Catch::Approx(fps[i]));
  }
  const auto state=e17_shotgun_viewmodel_state();client::ClientWorldState world;
  REQUIRE(created.projection->reset_generation(state,world));
  std::vector<E17FramePair> pairs;
  for(const auto buttons:{hlclient::goldsrc::kReferenceGoldSrcButtonAttack,
                         hlclient::goldsrc::kReferenceGoldSrcButtonAttack2})
    pairs.push_back(e17_materialized_shotgun_action(*created.projection,state,buttons,true));
  for(const auto& pair:e17_materialized_reload(*created.projection,state,true)) pairs.push_back(pair);
  auto gl=fixture::entity_opengl_fixture::try_context(640,480);
  if(gl && fixture::entity_opengl_fixture::capable_context()) {
    gl->initialize_renderer();hlclient::renderer::RenderScene scene;
    const auto draw=[&] {
      gl->renderer().render(scene,{640,480});CHECK(glGetError()==GL_NO_ERROR);
      return gl->renderer().observe_framebuffer({640,480},scene.clear_color,true);
    };
    const auto baseline=draw();REQUIRE(baseline);
    for(std::size_t i=0;i<pairs.size();++i) {
      CAPTURE(i);const auto& [first,second]=pairs[i];scene.first_person_entities=first;
      if(i==3U) {
        const auto& a=first.frame->studio_instances()[0U];const auto& b=second.frame->studio_instances()[0U];
        const auto& before=first.frame->studio_poses()[a.pose_index].bone_matrices;
        const auto& after=second.frame->studio_poses()[b.pose_index].bone_matrices;
        REQUIRE(before.size()==32U);REQUIRE(after.size()==32U);
        CHECK(before[29U]!=after[29U]); // Visible weapon-owning bone, not just hidden fingers/shell.
      }
      const auto initial=draw();REQUIRE(initial);REQUIRE(initial.non_clear_pixel_count>0U);
      CHECK_FALSE(std::ranges::equal(initial.rgba8,baseline.rgba8));
      const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
      scene.first_person_entities=second;const auto animated=draw();REQUIRE(animated);
      CHECK_FALSE(std::ranges::equal(initial.rgba8,animated.rgba8));
      scene.camera.position={-30,84,105};scene.camera.target={-30.5F,84.5F,105.707F};
      CHECK(std::ranges::equal(draw().rgba8,animated.rgba8));
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
    }
    gl->release_renderer();
  } else WARN("Optional actual Shotgun GPU control unavailable; approved importer/host/Studio proof still runs");
  gl.reset(); // The remote control owns its own normal context below.
  const auto approved_player=created.projection->studio_model_data(7U);REQUIRE(approved_player);
  const auto select_sequence=[&](std::string_view first,std::string_view second={}) {
    const auto found=std::ranges::find_if(approved_player->sequences,[&](const auto& sequence) {
      return sequence.label.find(first)!=std::string::npos &&
          (second.empty() || sequence.label.find(second)!=std::string::npos);
    });
    REQUIRE(found!=approved_player->sequences.end());
    return static_cast<std::uint32_t>(found-approved_player->sequences.begin());
  };
  const std::array<std::uint32_t,4U> remote_sequences{
      select_sequence("walk"),select_sequence("crouch"),select_sequence("shoot","shotgun"),select_sequence("die")};
  const auto& map=*created.projection->surface_scene()->world_package();
  hlclient::assets::AssetVector3 remote_origin{};bool floor_found=false;
  for(const auto& surface:map.surface_ranges()) {
    for(std::size_t at=surface.first_index;at+2U<static_cast<std::size_t>(surface.first_index)+surface.index_count;at+=3U) {
      const auto& a=map.vertices()[map.indices()[at]];
      if(a.normal.z<.9F) continue;
      const auto b=map.vertices()[map.indices()[at+1U]].position,c=map.vertices()[map.indices()[at+2U]].position;
      remote_origin={(a.position.x+b.x+c.x)/3,(a.position.y+b.y+c.y)/3,(a.position.z+b.z+c.z)/3+36};
      floor_found=true;break;
    }
    if(floor_found) break;
  }
  REQUIRE(floor_found);
  e17_remote_shotgun_merge_control(*created.projection,remote_origin,remote_sequences,true);
  hlclient::goldsrc::ApprovedSoundAssets sounds{created.projection->sound_resources()};
  for(const std::string_view sample:{"weapons/sbarrel1.wav","weapons/dbarrel1.wav","weapons/sshell1.wav"}) {
    hlclient::game_api::LocalSoundReference reference;
    reference.source=hlclient::game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    std::copy(sample.begin(),sample.end(),reference.sample.begin());
    auto result=sounds.request_local(reference);
    for(unsigned retry=0;retry<1000U && result.status==hlclient::goldsrc::SoundAssetStatus::pending;++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});result=sounds.request_local(reference);
    }
    REQUIRE(result.status==hlclient::goldsrc::SoundAssetStatus::ready);REQUIRE(result.asset);
    REQUIRE_FALSE(result.asset->interleaved_samples.empty());
    CHECK(std::ranges::all_of(result.asset->interleaved_samples,[](float x){return std::isfinite(x);}));
    CHECK(std::ranges::any_of(result.asset->interleaved_samples,[](float x){return x!=0.0F;}));
  }
  for(const auto slot:{std::uint16_t{31U},std::uint16_t{32U}}) {
    auto result=sounds.request(slot);
    for(unsigned retry=0;retry<1000U && result.status==hlclient::goldsrc::SoundAssetStatus::pending;++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});result=sounds.request(slot);
    }
    REQUIRE(result.status==hlclient::goldsrc::SoundAssetStatus::ready);REQUIRE(result.asset);
    CHECK(std::ranges::all_of(result.asset->interleaved_samples,[](float x){return std::isfinite(x);}));
    CHECK(std::ranges::any_of(result.asset->interleaved_samples,[](float x){return x!=0.0F;}));
  }
  for(std::size_t i=0;i<tokens.size();++i)
    CHECK(times[i]==std::filesystem::last_write_time(root/"valve"/tokens[i]));
}
#endif

TEST_CASE("Opt-in crossfire inspected brushes traverse the live provider and produce pixels",
          "[live-brush][local-brush-control][opengl]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value=nullptr; std::size_t count=0U;
  if (_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
  std::free(value);
#endif
  if (root.empty()) SKIP("No opt-in read-only local game root");
  // Independent inspected state/slots, NOT a reconstruction of a server packet.
  auto resources=readiness::parse_resource_list({
      {2U,"maps/crossfire.bsp",9U,0U,0U}, {2U,"*1",101U,0U,0U},
      {2U,"*12",112U,0U,0U}, {2U,"*21",121U,0U,0U}, {2U,"*38",138U,0U,0U},
      {2U,"*5",105U,0U,0U}, {2U,"*44",144U,0U,0U}, {2U,"*46",146U,0U,0U},
      {2U,"*47",147U,0U,0U}, {2U,"*66",166U,0U,0U},
      {2U,"models/w_medkit.mdl",200U,0U,0U}, {2U,"models/w_battery.mdl",201U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),
      root,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : "ready")); REQUIRE(created.projection);
  CHECK(created.projection->summary().imported_studio==2U);
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if (!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  auto state=observation(); state.packet_entities.resize(1);state.packet_entities.reserve(2);
  auto& entity=state.packet_entities[0];
  entity.origin={0.0,0.0,0.0}; entity.angles={0.0,0.0,0.0};
  client::ClientWorldState world;
  for (const auto model_index : {1U,12U,21U,38U,5U,44U,46U,47U,66U}) {
    entity.model_index=100U+model_index; entity.render_mode=model_index==12U ? 4U : 0U;
    ++state.publication_revision; state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    if (!world.world_scene()) REQUIRE(created.projection->reset_generation(state,world));
    else REQUIRE(created.projection->project_entities(state,world));
    REQUIRE(world.runtime_brushes()->instances.size()==1U);
    if (model_index==21U) {
      // Read-only static control: the inspected lift's initial placement and
      // explicit test-owned solidity, not an assertion about a live server.
      entity.solid=4U; entity.brush_move_type=7U;
      auto models=hlclient::goldsrc::collision::build_brush_collision_model_library(
          created.projection->collision_world_package());
      REQUIRE(models);
      const auto context=hlclient::goldsrc::build_reference_brush_collision(models.library,
          {{121U,"*21"}},state,state.generation);
      REQUIRE(context.scene);
      hlclient::goldsrc::movement::BrushSceneMovementCollision collision{context.scene};
      hlclient::collision::CollisionQueryScratch scratch;
      const auto floor=collision.trace_hull({320,64,-1650},{320,64,-1700},
          hlclient::movement::PlayerMovementHull::standing,scratch);
      REQUIRE(floor); REQUIRE(floor.result->hit); REQUIRE(floor.result->collision_plane);
      CHECK(floor.result->hit->source_model_index==21U);
      CHECK(floor.result->collision_plane->normal.z==Catch::Approx(1.0));
      CHECK(floor.result->end_position.z>=-1662.0F);
      CHECK(floor.result->end_position.z<=-1659.0F);
    }
    const auto& library=world.world_scene()->brush_library();
    const auto& package=*library.render_package();
    const auto model=std::ranges::find_if(library.models(),[&](const auto& value){ return value.source_model_index()==model_index; });
    REQUIRE(model!=library.models().end());
    if(model_index==66U) {
      bool button_texture=false;
      for(const auto& surface:model->surfaces()) {
        const auto& material=package.materials()[surface.render_material_index];
        const auto& texture=package.textured_world().textures.textures()[material.base_texture_asset_index];
        if(texture.name=="+abutton2" || texture.name=="+Abutton2") {
          button_texture=true;
          CHECK(texture.alpha_mode==hlclient::assets::WorldTextureAlphaMode::opaque);
          CHECK_FALSE(texture.mip_levels[0].rgba_pixels.empty());
        }
      }
      CHECK(button_texture);
    }
    bool pixels=false;
    for (const auto& surface : model->surfaces()) {
      const auto& vertex=package.vertices()[package.indices()[surface.first_index]];
      const auto center=hlclient::assets::AssetVector3{
          (surface.bounds.minimum.x+surface.bounds.maximum.x)*0.5F,
          (surface.bounds.minimum.y+surface.bounds.maximum.y)*0.5F,
          (surface.bounds.minimum.z+surface.bounds.maximum.z)*0.5F};
      const auto normal=vertex.normal;
      world.set_camera({{center.x+normal.x*48.0F,center.y+normal.y*48.0F,center.z+normal.z*48.0F},
          center, std::abs(normal.z)>0.9F ? hlclient::assets::AssetVector3{0.0F,1.0F,0.0F} : hlclient::assets::AssetVector3{0.0F,0.0F,1.0F}});
      auto scene=client::build_render_scene(world);
      gl->renderer().render(scene,{320,240});
      const auto with=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
      scene.static_world->runtime_brushes.reset();
      gl->renderer().render(scene,{320,240});
      const auto without=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
      REQUIRE(with); REQUIRE(without); CHECK(glGetError()==GL_NO_ERROR);
      if (with.rgba8!=without.rgba8) {
        std::size_t changed=0U;
        for (std::size_t i=0; i<with.rgba8.size(); i+=4U)
          if (!std::equal(with.rgba8.begin()+i,with.rgba8.begin()+i+4U,without.rgba8.begin()+i)) ++changed;
        std::cout << "offline_brush_control submodel=" << model_index << " changed_pixels=" << changed
                  << " gl_error=0 live_state=false\n";
        pixels=true; break;
      }
    }
    INFO("offline inspected submodel=" << model_index); CHECK(pixels);
    if(model_index==66U) {
      // The earlier any-face control can pass on a side face. Exercise the
      // actual visible button face, its alternate bank and real console behind
      // it through production depth/PVS, without inventing a live server entity.
      const auto face=std::ranges::find_if(model->surfaces(),[&](const auto& surface){
        const auto& material=package.materials()[surface.render_material_index];
        const auto& texture=package.textured_world().textures.textures()[material.base_texture_asset_index];
        return texture.name=="+abutton2" || texture.name=="+Abutton2";
      });
      REQUIRE(face!=model->surfaces().end());
      REQUIRE(library.alternate_by_material()[face->render_material_index]);
      const auto center=hlclient::assets::AssetVector3{
          (face->bounds.minimum.x+face->bounds.maximum.x)*.5F,
          (face->bounds.minimum.y+face->bounds.maximum.y)*.5F,
          (face->bounds.minimum.z+face->bounds.maximum.z)*.5F};
      const auto normal=package.vertices()[package.indices()[face->first_index]].normal;
      std::cout<<"offline_crossfire_button_front center="<<center.x<<','<<center.y<<','<<center.z
          <<" normal="<<normal.x<<','<<normal.y<<','<<normal.z<<'\n';
      auto console=entity;console.entity_number=entity.entity_number+1U;console.model_index=144U;
      console.frame=0.0;console.solid=4U;console.brush_move_type=7U;state.packet_entities.push_back(console);
      // The real strike_ready_door covers this face at its initial origin.
      // Observe both test-owned server poses; never force the map's ready state.
      for(const auto console_z:{0.0,-62.0}) for(const auto distance:{48.0F,96.0F}) for(const auto offset:{0.0F,20.0F}) {
        state.packet_entities[1].origin.z=console_z;
        std::vector<std::uint8_t> first_bank;
        for(const auto bank:{0U,1U}) {
          CAPTURE(console_z,distance,offset,bank);entity.frame=static_cast<double>(bank);
          ++state.publication_revision;state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
          REQUIRE(created.projection->project_entities(state,world));
          REQUIRE(world.runtime_brushes()->instances.size()==2U);
          world.set_camera({{center.x+normal.x*distance+offset,center.y+normal.y*distance,
              center.z+normal.z*distance},center,{0,0,1}});
          auto scene=client::build_render_scene(world);
          const auto submitted_before=gl->renderer().statistics().runtime_brush_submitted_count;
          gl->renderer().render(scene,{320,240});
          const auto visible=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
          REQUIRE(visible);CHECK(glGetError()==GL_NO_ERROR);
          CHECK(gl->renderer().statistics().runtime_brush_submitted_count>=submitted_before+1U);
          auto without_button=std::make_shared<hlclient::world_scene_render::RuntimeBrushRenderFrame>(*scene.static_world->runtime_brushes);
          std::erase_if(without_button->instances,[](const auto& instance){return instance.source_model_index==66U;});
          REQUIRE(without_button->instances.size()==1U);
          scene.static_world->runtime_brushes=without_button;
          gl->renderer().render(scene,{320,240});
          const auto baseline=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);
          REQUIRE(baseline);CHECK(glGetError()==GL_NO_ERROR);
          std::size_t changed=0;
          for(std::size_t pixel=0;pixel<visible.rgba8.size();pixel+=4U)
            if(!std::equal(visible.rgba8.begin()+pixel,visible.rgba8.begin()+pixel+4U,baseline.rgba8.begin()+pixel))++changed;
          if(console_z==0.0)CHECK(changed==0U);
          else CHECK(changed>32U);
          std::cout<<"offline_crossfire_button bank="<<bank<<" distance="<<distance<<" offset="<<offset
              <<" changed_pixels="<<changed<<" console_z="<<console_z<<" live_state=false\n";
          if(bank==0U)first_bank=visible.rgba8;
          else {
            const bool banks_differ=first_bank!=visible.rgba8;
            CHECK(banks_differ==(console_z!=0.0));
          }
        }
      }
      state.packet_entities.pop_back();entity.frame=0.0;
    }
  }
  CHECK(gl->renderer().statistics().brush_upload_count==1U);
  REQUIRE(world.world_scene()->brush_library().alternate_textures());
  CHECK(world.world_scene()->brush_library().alternate_textures()->texture_count()>=2U);
  for (const auto slot : {200U,201U}) {
    entity.model_index=slot; entity.render_mode=0U;
    ++state.publication_revision; state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    REQUIRE(created.projection->project_entities(state,world));
    CHECK(world.runtime_brushes()->instances.empty());
    CHECK(world.entity_frame()->studio_instances().size()==1U);
  }
  gl->release_renderer();
}
TEST_CASE("Decoded brush delta retention client-only and malformed suffix use one commit boundary",
          "[live-brush][runtime-replay][transaction]") {
  namespace g = hlclient::goldsrc;
  namespace f = hlclient::test::delta_fixture;
  LocalContext context(true); auto created=context.create(); REQUIRE(created.projection);
  auto fixture_result=g::make_runtime_replay_fixture({g::RuntimeReplayFixtureKind::visual_entities,1U,100U,1000U});
  REQUIRE(fixture_result); auto replay=std::move(*fixture_result.fixture);
  const f::Field fields[]{
      {"origin[0]",0x80000004U,0U,16U,32000U,4000U},
      {"origin[1]",0x80000004U,4U,16U,32000U,4000U},
      {"origin[2]",0x80000004U,8U,16U,32000U,4000U},
      {"angles[0]",0x10U,12U,16U},{"angles[1]",0x10U,16U,16U},{"angles[2]",0x10U,20U,16U},
      {"modelindex",0x8U,24U,16U}};
  const auto schema=g::DeltaDescriptionParser{}.parse(f::schema("entity_state_t",fields),0U); REQUIRE(schema);
  g::DeltaSchemaRegistryBuilder schemas; REQUIRE(schemas.insert(*schema.schema));
  for (const auto& item : replay.initialization.schemas->schemas())
    if (item.name()!="entity_state_t") REQUIRE(schemas.insert(item));
  replay.initialization.schemas=std::make_shared<const g::DeltaSchemaRegistryState>(std::move(schemas).publish());
  f::BitWriter writer;
  for (const auto number : {2U,10U,20U,30U}) {
    writer.write(number,11U); writer.write(0U,2U);
    writer.write(1U,3U); writer.write(0x40U,8U); writer.write(27U,16U);
  }
  writer.write(0xffffU,16U); writer.write(0U,6U); writer.align_zero();
  auto payload=f::owning_payload(writer.bytes());
  const auto cursor=g::StockRuntimeSourceCursor::create(0U,0U,payload.bytes.size()); REQUIRE(cursor);
  auto baselines=g::GoldSrcEntityBaselineDecoder{}.decode({&payload,*cursor,1U,1U,1U,
      "entity_state_t","entity_state_t","entity_state_t",payload.bytes.size()*8U},*replay.initialization.schemas);
  REQUIRE(baselines);
  replay.initialization.baselines=std::make_shared<const g::EntityBaselineRegistryState>(std::move(*baselines.registry));
  client::ClientWorldState world;
  auto session=g::RuntimeReplaySession::initialize(std::move(replay.initialization),world); REQUIRE(session);
  REQUIRE(created.projection->reset_generation(*world.runtime_observation(),world));
  for (std::size_t i=0; i<replay.records.size(); ++i) {
    const auto before=world.runtime_brushes();
    if (i==4U) {
      auto bad=replay.records[i]; bad.payload.bytes.push_back(std::byte{255});
      const auto publication=world.runtime_observation();
      CHECK_FALSE(session.session->apply_record(bad));
      CHECK(world.runtime_observation()==publication); CHECK(world.runtime_brushes()==before);
    }
    const auto result=session.session->apply_record(replay.records[i]); REQUIRE(result);
    if (result.event->entities_observed) {
      REQUIRE(created.projection->project_entities(*world.runtime_observation(),world));
      REQUIRE(world.runtime_brushes()->instances.size()==3U);
      if (i==1U) {
        // Delta mentions entity 2 only: the other two remain, unchanged.
        CHECK(world.runtime_brushes()->instances[1].entity_number==10U);
        CHECK(world.runtime_brushes()->instances[1].model_transform.values==before->instances[1].model_transform.values);
      }
      if (i==3U) CHECK(world.runtime_brushes()->instances[2].entity_number==30U);
    } else {
      created.projection->acknowledge_unchanged();
      CHECK(world.runtime_brushes()==before);
    }
  }
}
// E16 fixtures deliberately use no Half-Life asset names. Resource slots are
// sparse and independently assigned; only approved session bindings select them.
[[nodiscard]] std::vector<std::byte> e16_animated_sprite(const bool index_alpha) {
  namespace sf = fixture::sprite_fixture;
  auto bytes=sf::literal_header_and_palette(4,index_alpha ? 2 : 1,index_alpha ? 4 : 2,0,8,8);
  const auto color=[&](std::size_t index,std::array<unsigned char,3> rgb) {
    for(std::size_t c=0;c<3;++c) bytes[sf::kPaletteOffset+index*3+c]=std::byte{rgb[c]};
  };
  color(0,{0,0,0});color(1,{180,90,30});color(2,{30,90,180});color(255,{180,160,140});
  for(unsigned frame=0;frame<4;++frame) {
    sf::FrameSpec image{-4,4,8,8,std::vector<std::byte>(64,std::byte{0})};
    for(unsigned y=2;y<6;++y) for(unsigned x=2;x<6;++x)
      image.pixels[y*8+x]=index_alpha ? std::byte{128} : std::byte{static_cast<unsigned char>(1+frame%2)};
    sf::append_single_entry(bytes,image);
  }
  return bytes;
}

[[nodiscard]] client::RuntimeClientObservationState e16_projectiles(
    std::uint64_t record,double server_seconds,double offset=0.0) {
  auto state=e10_observation(record,server_seconds,0,0,0,false);
  state.packet_entities.clear();
  for(const auto number:{40U,41U}) {
    client::RuntimePacketEntityObservation entity;
    entity.entity_number=number;entity.ordinary_visual_schema=true;
    entity.model_index=number==40 ? 7U : 15U;
    entity.brush_move_type=10U; // Pinned public MOVETYPE_BOUNCE; never simulated here.
    entity.origin={0.0,(number==40 ? -5.0 : 5.0)+offset,24.0};
    entity.angles={0.0,0.0,0.0};entity.body=0U;entity.skin=0;entity.sequence=0U;entity.frame=0.0;
    state.packet_entities.push_back(entity);
  }
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  return state;
}

TEST_CASE("E16 approved ordinary kinetic Studio entities interpolate without game event or physics",
    "[e16][projectile][local-capture-assets][game-api][runtime-presentation]") {
  LocalContext context;
  auto other=context.model;
  fixture::studio_write_fixed_string(other,8U,64U,"independent_projectile.mdl");
  context.root.write("valve","models/independent.mdl",other);
  auto resources=readiness::parse_resource_list({
      {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(context.map.size()),0U},
      {2U,"models/shared.mdl",7U,static_cast<std::uint32_t>(context.model.size()),0U},
      {2U,"models/independent.mdl",15U,static_cast<std::uint32_t>(other.size()),0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};host.reset({1,1,1});
  client::ClientWorldState world;world.set_camera({{40,0,24},{0,0,24},{0,0,1}});
  const auto first=e16_projectiles(1,1.0);
  REQUIRE(created.projection->reset_generation(first,world));
  REQUIRE(e10_present(*created.projection,host,99.0,1,world,{320,240}));
  CHECK(e10_instance(world,40).transform.origin.y==Catch::Approx(-5.0F)); // Single sample.
  auto current=e16_projectiles(2,1.1,8.0);
  REQUIRE(created.projection->project_entities(current,world));
  const auto canonical=current.canonical_state_hash;
  REQUIRE(e10_present(*created.projection,host,99.1,1,world,{320,240}));
  CHECK(e10_instance(world,40).transform.origin.y==Catch::Approx(-5.0F));
  REQUIRE(e10_present(*created.projection,host,99.15,1,world,{320,240}));
  CHECK(e10_instance(world,40).transform.origin.y==Catch::Approx(-1.0F).margin(0.002F));
  CHECK(e10_instance(world,41).transform.origin.y==Catch::Approx(9.0F).margin(0.002F));
  CHECK(e10_instance(world,40).studio_asset_index!=e10_instance(world,41).studio_asset_index);
  CHECK(current.canonical_state_hash==canonical);
  CHECK(current.packet_entities[0].origin.y==3.0);
  REQUIRE(e10_present(*created.projection,host,99.6,1,world,{320,240}));
  CHECK(e10_instance(world,40).transform.origin.y==Catch::Approx(3.0F)); // No extrapolation.
  CHECK(host.drain_audio().count==0U); // No matching launch event is required.

  // Unknown movement stays the established discrete generic ordinary path.
  auto unknown=e16_projectiles(3,1.2,12.0);unknown.packet_entities[0].brush_move_type.reset();
  unknown.canonical_state_hash=client::runtime_observation_canonical_hash(unknown);
  REQUIRE(created.projection->project_entities(unknown,world));
  REQUIRE(e10_present(*created.projection,host,101.0,1,world,{320,240}));
  CHECK(e10_instance(world,40).transform.origin.y==Catch::Approx(7.0F));

  auto removed=e16_projectiles(4,1.3,12.0);removed.packet_entities.erase(removed.packet_entities.begin());
  removed.canonical_state_hash=client::runtime_observation_canonical_hash(removed);
  REQUIRE(created.projection->project_entities(removed,world));
  REQUIRE(e10_present(*created.projection,host,102.0,1,world,{320,240}));
  REQUIRE(world.entity_frame()->studio_instances().size()==1U);
  CHECK(world.entity_frame()->studio_instances()[0].entity_number==41U);
  auto reused=e16_projectiles(5,1.4,20.0);reused.packet_entities[0].model_index=15U;
  reused.canonical_state_hash=client::runtime_observation_canonical_hash(reused);
  REQUIRE(created.projection->project_entities(reused,world));
  REQUIRE(e10_present(*created.projection,host,103.0,1,world,{320,240}));
  CHECK(e10_instance(world,40).transform.origin.y==Catch::Approx(15.0F));
  CHECK(e10_instance(world,40).studio_asset_index==e10_instance(world,41).studio_asset_index);

  // New network/map generation discards old positions even with the same slot.
  auto generation=e16_projectiles(1,0.5,40.0);generation.generation=2;
  generation.entity_metadata.generation=2;generation.server_time_metadata.generation=2;
  generation.client_metadata.generation=2;
  generation.canonical_state_hash=client::runtime_observation_canonical_hash(generation);
  // Approved resource bindings cannot be reused for an unrelated capture.
  CHECK_FALSE(created.projection->reset_generation(generation,world));
  auto fresh=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
      context.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(fresh.projection);
  host.reset({2,2,1});REQUIRE(fresh.projection->reset_generation(generation,world));
  REQUIRE(e10_present(*fresh.projection,host,200.0,1,world,{320,240}));
  CHECK(e10_instance(world,40).transform.origin.y==Catch::Approx(35.0F));
  CHECK(world.entity_frame()->interpolation().previous_state_identity==
      world.entity_frame()->interpolation().current_state_identity);
}

TEST_CASE("Sorted source candidates refresh only with committed authoritative observations",
    "[e16][e10][local-capture-assets][game-api][runtime-presentation][source-candidates]") {
  LocalContext context;
  // The live receiving-client profile enables the same ordinary RX-clock
  // interpolation path exercised by the E16 grenade smoothing regression.
  auto created=app::RuntimeReplayLocalAssets::create(context.resources(),
      readiness::parse_server_info("maps/test_map.bsp"),context.root.path(),"valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);
  REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};host.reset({1,1,1});
  client::ClientWorldState world;world.set_camera({{40,0,24},{0,0,24},{0,0,1}});
  const auto ready=[](std::uint64_t record,double time,double offset) {
    auto state=e16_projectiles(record,time,offset);
    state.packet_entities[1].model_index=7U; // Both candidates use the fixture's ready Studio slot.
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
    return state;
  };
  const auto first=ready(1U,1.0,0.0);
  REQUIRE(created.projection->reset_generation(first,world));
  // Anchor the application-to-server clock before the second RX record, as
  // normal gameplay does. A first-ever render after both RXs holds newest.
  REQUIRE(e10_present(*created.projection,host,99.0,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx(-5.0F));
  CHECK(e10_instance(world,41U).transform.origin.y==Catch::Approx(5.0F));
  const auto second=ready(2U,1.1,8.0);
  REQUIRE(created.projection->project_entities(second,world));
  REQUIRE(e10_present(*created.projection,host,99.1,1U,world,{320,240}));
  REQUIRE(e10_present(*created.projection,host,99.15,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx(-1.0F).margin(.002F));
  CHECK(e10_instance(world,41U).transform.origin.y==Catch::Approx(9.0F).margin(.002F));
  for(unsigned frame=0;frame<3;++frame) {
    REQUIRE(e10_present(*created.projection,host,99.15,1U,world,{320,240}));
    CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx(-1.0F).margin(.002F));
    CHECK(e10_instance(world,41U).transform.origin.y==Catch::Approx(9.0F).margin(.002F));
  }

  auto changed=ready(3U,1.2,12.0);
  changed.packet_entities[0].brush_move_type.reset(); // 40 becomes discrete, 41 stays kinetic.
  changed.canonical_state_hash=client::runtime_observation_canonical_hash(changed);
  REQUIRE(created.projection->project_entities(changed,world));
  REQUIRE(e10_present(*created.projection,host,99.2,1U,world,{320,240}));
  REQUIRE(e10_present(*created.projection,host,99.25,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx(7.0F).margin(.002F));
  CHECK(e10_instance(world,41U).transform.origin.y==Catch::Approx(15.0F).margin(.002F));

  auto invalid=ready(4U,1.3,20.0);
  std::swap(invalid.packet_entities[0],invalid.packet_entities[1]);
  invalid.canonical_state_hash=client::runtime_observation_canonical_hash(invalid);
  CHECK_FALSE(client::valid_runtime_observation(invalid));
  CHECK_FALSE(created.projection->project_entities(invalid,world));
  REQUIRE(e10_present(*created.projection,host,99.25,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx(7.0F).margin(.002F));
  CHECK(e10_instance(world,41U).transform.origin.y==Catch::Approx(15.0F).margin(.002F));

  auto removed=ready(5U,1.4,24.0);
  removed.packet_entities.erase(removed.packet_entities.begin());
  removed.canonical_state_hash=client::runtime_observation_canonical_hash(removed);
  REQUIRE(created.projection->project_entities(removed,world));
  REQUIRE(e10_present(*created.projection,host,99.3,1U,world,{320,240}));
  REQUIRE(world.entity_frame());
  REQUIRE(world.entity_frame()->studio_instances().size()==1U);
  CHECK(world.entity_frame()->studio_instances()[0].entity_number==41U);
}

TEST_CASE("E16 ordinary server projectile produces localized OpenGL pixels and immediate removal",
    "[e16][projectile][local-capture-assets][opengl][actual-context]") {
  LocalContext context;
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset,0,-2,-2);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+12U,0,2,-2);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+24U,0,0,2);
  for(std::size_t i=0;i<3;++i)
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioNormalsOffset+i*12U,1,0,0);
  context.root.write("valve","models/shared.mdl",context.model);
  auto created=context.create();REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};host.reset({1,1,1});
  client::ClientWorldState world;
  auto state=e16_projectiles(1,1.0);state.packet_entities.resize(1);
  state.packet_entities[0].origin={0.0,0.0,24.0};
  // Missing frame/sequence remain a valid static pose, not client animation.
  state.packet_entities[0].frame.reset();state.packet_entities[0].sequence.reset();
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->reset_generation(state,world));
  world.set_camera({{40,0,24},{0,0,24},{0,0,1}});
  REQUIRE(e10_present(*created.projection,host,100,1,world,{320,240}));
  REQUIRE(world.entity_frame()->studio_instances().size()==1U);
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  auto scene=client::build_render_scene(world);scene.static_world.reset();
  auto baseline=scene;baseline.dynamic_entities.reset();
  gl->renderer().render(baseline,{320,240});
  const auto empty=gl->renderer().observe_framebuffer({320,240},baseline.clear_color,true);REQUIRE(empty);
  gl->renderer().render(scene,{320,240});
  const auto present=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(present);
  CHECK_FALSE(std::ranges::equal(present.rgba8,empty.rgba8));CHECK(present.non_clear_pixel_count>20U);
  CHECK(present.non_clear_pixel_count<320U*240U/8U);
  const auto uploads=gl->renderer().entity_statistics().studio_asset_upload_count;
  state=e16_projectiles(2,1.02);state.packet_entities.clear();
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->project_entities(state,world));
  REQUIRE(e10_present(*created.projection,host,100.02,1,world,{320,240}));
  scene=client::build_render_scene(world);scene.static_world.reset();
  gl->renderer().render(scene,{320,240});
  const auto removed=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(removed);
  REQUIRE(world.entity_frame()->studio_instances().empty());
  CHECK(std::ranges::equal(removed.rgba8,empty.rgba8));CHECK(glGetError()==GL_NO_ERROR);
  CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==uploads);
  gl->release_renderer();
}

TEST_CASE("E16 ordinary projectile normal materializer keeps a continuous clock across irregular RX",
    "[e16][grenade-smoothing][projectile][local-capture-assets][game-api][runtime-presentation][opengl]") {
  LocalContext context;
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset,0,-2,-2);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+12U,0,2,-2);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+24U,0,0,2);
  for(std::size_t i=0U;i<3U;++i)
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioNormalsOffset+i*12U,1,0,0);
  context.root.write("valve","models/shared.mdl",context.model);
  auto created=context.create();REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{std::make_unique<E10TestGameClientModule>()};host.reset({1,1,1});
  client::ClientWorldState world;world.set_camera({{40,0,24},{0,0,24},{0,0,1}});
  auto current=e16_projectiles(1U,1.0);current.packet_entities.resize(1U);
  current.packet_entities.front().origin={0.0,-5.0,24.0};
  current.canonical_state_hash=client::runtime_observation_canonical_hash(current);
  REQUIRE(created.projection->reset_generation(current,world));
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  const bool gpu=gl && fixture::entity_opengl_fixture::capable_context();
  if(gpu) gl->initialize_renderer();else WARN("OpenGL unavailable; normal approved CPU materializer proof still runs");
  std::optional<std::uint64_t> previous_pixels;
  std::size_t pixel_changes{};
  const std::array arrivals{0.0,.025,.045,.055};std::size_t next=1U;
  double before=-5.0;
  for(unsigned frame=0U;frame<=16U;++frame) {
    const double app=static_cast<double>(frame)*.005;
    while(next<arrivals.size() && arrivals[next]<=app+1e-9) {
      current=e16_projectiles(next+1U,1.0+static_cast<double>(next)*.02,
          static_cast<double>(next)*1.6);
      current.packet_entities.resize(1U);current.packet_entities.front().origin.x=0.0;
      current.packet_entities.front().origin.z=24.0;
      current.canonical_state_hash=client::runtime_observation_canonical_hash(current);
      REQUIRE(created.projection->project_entities(current,world));++next;
    }
    const auto canonical=current.canonical_state_hash;
    REQUIRE(e10_present(*created.projection,host,100.0+app,1U,world,{320,240}));
    const auto& instance=e10_instance(world,40U);
    const double expected=-5.0+std::min(static_cast<double>(next-1U)*1.6,std::max(0.0,app-.02)*80.0);
    CHECK(instance.transform.origin.y==Catch::Approx(expected).margin(.0001));
    CHECK(instance.transform.origin.y>=before-.0001);CHECK(instance.transform.origin.y-before<=.4001);
    CHECK(current.canonical_state_hash==canonical);
    const auto palette=world.entity_frame()->studio_poses()[instance.pose_index].bone_matrices;
    REQUIRE(palette.size()==1U);
    const auto position=instance.transform.origin.y;
    REQUIRE(e10_present(*created.projection,host,100.0+app,1U,world,{320,240}));
    CHECK(e10_instance(world,40U).transform.origin.y==position);
    if(gpu) {
      auto scene=client::build_render_scene(world);scene.static_world.reset();
      gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
      const auto pixels=gl->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(pixels);
      REQUIRE(pixels.non_clear_pixel_count>0U);
      if(previous_pixels && *previous_pixels!=pixels.color_signature) ++pixel_changes;
      previous_pixels=pixels.color_signature;
      CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
    }
    before=position;
  }
  CHECK(before> -5.0); // Short80ms lifetime visibly moves, with no100ms hold.
  current=e16_projectiles(5U,1.08);current.packet_entities.clear();
  current.canonical_state_hash=client::runtime_observation_canonical_hash(current);
  REQUIRE(created.projection->project_entities(current,world));
  REQUIRE(e10_present(*created.projection,host,100.08,1U,world,{320,240}));
  CHECK(world.entity_frame()->studio_instances().empty());CHECK(host.drain_audio().count==0U);
  if(gpu) {
    CHECK(pixel_changes>3U);
    auto scene=client::build_render_scene(world);scene.static_world.reset();
    gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
    const auto removed=gl->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(removed);
    CHECK(removed.non_clear_pixel_count==0U);gl->release_renderer();
  }
}

#if HLCLIENT_HAS_GAME_HALFLIFE
TEST_CASE("E16 late server projectile birth moves through the normal HalfLife materializer and GPU after one cadence",
    "[e16][grenade-smoothing][grenade-first-frame][projectile][local-capture-assets][game-api][runtime-presentation][opengl]") {
  LocalContext context;
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset,0,-2,-2);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+12U,0,2,-2);
  fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioVerticesOffset+24U,0,0,2);
  for(std::size_t i=0U;i<3U;++i)
    fixture::studio_write_vector3(context.model,fixture::kSyntheticStudioNormalsOffset+i*12U,1,0,0);
  context.root.write("valve","models/shared.mdl",context.model);
  auto created=context.create();REQUIRE(created.projection);
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,1});CHECK(host.remote_player_policy().maximum_observation_gap_seconds==.25);
  client::ClientWorldState world;world.set_camera({{40,0,24},{0,0,24},{0,0,1}});
  constexpr unsigned warm_steps=48U;
  constexpr double birth_server=1.0+warm_steps*.02,birth_application=100.0+warm_steps*.015;
  auto initial=e16_projectiles(1U,1.0);initial.packet_entities.clear();
  initial.canonical_state_hash=client::runtime_observation_canonical_hash(initial);
  REQUIRE(created.projection->reset_generation(initial,world));
  for(unsigned i=0U;i<warm_steps;++i) {
    auto empty=e16_projectiles(i+1U,1.0+i*.02);empty.packet_entities.clear();
    empty.canonical_state_hash=client::runtime_observation_canonical_hash(empty);
    REQUIRE(created.projection->project_entities(empty,world));
    REQUIRE(e10_present(*created.projection,host,100.0+i*.015,1U,world,{320,240}));
    REQUIRE(world.entity_frame()->studio_instances().empty());
  }
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  const bool gpu=gl && fixture::entity_opengl_fixture::capable_context();
  if(gpu) gl->initialize_renderer();else WARN("OpenGL unavailable; normal HalfLife approved CPU birth-clock proof still runs");
  const auto pixels=[&]() {
    auto scene=client::build_render_scene(world);scene.static_world.reset();
    gl->renderer().render(scene,{320,240});CHECK(glGetError()==GL_NO_ERROR);
    const auto observed=gl->renderer().observe_framebuffer({320,240},scene.clear_color);REQUIRE(observed);
    CHECK(gl->renderer().entity_statistics().studio_asset_upload_count==1U);
    return observed;
  };
  const auto sample=[](std::uint64_t record,double server,double y) {
    auto state=e16_projectiles(record,server);state.packet_entities.resize(1U);
    state.packet_entities.front().origin={0.0,y,24.0};
    state.canonical_state_hash=client::runtime_observation_canonical_hash(state);return state;
  };
  auto current=sample(warm_steps+1U,birth_server,0.0);
  REQUIRE(created.projection->project_entities(current,world));
  REQUIRE(e10_present(*created.projection,host,birth_application,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==0.0);
  std::optional<std::uint64_t> initial_pixels;
  if(gpu) {const auto image=pixels();REQUIRE(image.non_clear_pixel_count>0U);initial_pixels=image.color_signature;}
  current=sample(warm_steps+2U,birth_server+.02,1.6);
  REQUIRE(created.projection->project_entities(current,world));
  REQUIRE(e10_present(*created.projection,host,birth_application+.02,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx(0.0).margin(.0001));
  const auto canonical=current.canonical_state_hash;
  REQUIRE(e10_present(*created.projection,host,birth_application+.03,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx(.8).margin(.0001));
  CHECK(current.canonical_state_hash==canonical);
  std::optional<std::uint64_t> moving_pixels;
  if(gpu) {const auto image=pixels();REQUIRE(image.non_clear_pixel_count>0U);
    CHECK(image.color_signature!=*initial_pixels);moving_pixels=image.color_signature;}
  const auto position=e10_instance(world,40U).transform.origin.y;
  REQUIRE(e10_present(*created.projection,host,birth_application+.03,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==position);
  REQUIRE(e10_present(*created.projection,host,birth_application+.029,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==position);
  if(gpu) CHECK(pixels().color_signature==*moving_pixels);
  for(unsigned i=2U;i<=3U;++i) {
    current=sample(warm_steps+i+1U,birth_server+i*.02,i*1.6);
    REQUIRE(created.projection->project_entities(current,world));
    REQUIRE(e10_present(*created.projection,host,birth_application+i*.02,1U,world,{320,240}));
    CHECK(e10_instance(world,40U).transform.origin.y==Catch::Approx((i-1U)*1.6).margin(.0001));
  }
  // An80ms authoritative flight ends immediately despite its delayed visual clock.
  current=sample(warm_steps+5U,birth_server+.08,6.4);current.packet_entities.clear();
  current.canonical_state_hash=client::runtime_observation_canonical_hash(current);
  REQUIRE(created.projection->project_entities(current,world));
  REQUIRE(e10_present(*created.projection,host,birth_application+.08,1U,world,{320,240}));
  CHECK(world.entity_frame()->studio_instances().empty());
  CHECK(host.drain_audio().count==0U);CHECK(host.drain_remote_audio().count==0U);
  if(gpu) {CHECK(pixels().non_clear_pixel_count==0U);gl->release_renderer();}
  current=sample(warm_steps+6U,birth_server+.1,-3.0);
  REQUIRE(created.projection->reset_generation(current,world));
  REQUIRE(e10_present(*created.projection,host,birth_application+.1,1U,world,{320,240}));
  CHECK(e10_instance(world,40U).transform.origin.y==-3.0);
}

struct E16TemporarySpriteContext {
  LocalContext local;
  std::vector<std::byte> explosion=e16_animated_sprite(false),smoke=e16_animated_sprite(true);
  E16TemporarySpriteContext() {
    local.root.write("valve","sprites/owned_explosion.spr",explosion);
    local.root.write("valve","sprites/owned_smoke.spr",smoke);
  }
  auto create() const {
    const auto resources=readiness::parse_resource_list({
        {2U,"maps/test_map.bsp",9U,static_cast<std::uint32_t>(local.map.size()),0U},
        {2U,"sprites/owned_explosion.spr",19U,static_cast<std::uint32_t>(explosion.size()),0U},
        {2U,"sprites/owned_smoke.spr",73U,static_cast<std::uint32_t>(smoke.size()),0U}});
    return app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/test_map.bsp"),
        local.root.path(),"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  }
};

[[nodiscard]] client::CommittedTemporaryEffect e16_effect(const bool smoke=false) {
  client::CommittedTemporaryEffect event;
  event.generation=1;event.record=smoke ? 2 : 1;
  event.record_ordinal=static_cast<std::size_t>(event.record);
  event.message_bit_offset=24;event.received_at_seconds=smoke ? 10.25 : 10.0;
  event.kind=smoke ? client::TemporaryEffectKind::smoke : client::TemporaryEffectKind::explosion;
  event.sprite_index=smoke ? 73 : 19;event.scale_tenths=10;event.framerate=10;
  event.flags=2U|8U; // Sprite A/B intentionally isolates away light and particles.
  return event;
}

TEST_CASE("E16 committed HalfLife effects use approved animated sparse sprite slots and bounded owner",
    "[e16][temporary-effects][local-capture-assets][game-api][transaction]") {
  E16TemporarySpriteContext context;auto created=context.create();REQUIRE(created.projection);
  client::ClientWorldState world;auto state=observation();state.packet_entities.clear();
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  REQUIRE(created.projection->reset_generation(state,world));
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,1});host.set_movement_audio_time_origin(10.0);
  app::TemporaryEffectPresentation owner;owner.reset(1);
  const auto event=e16_effect();host.committed_temporary_effect(event);
  const auto batch=host.drain_temporary_effects(0.0);REQUIRE(batch.count==1U);
  owner.consume(batch,0.0);hlclient::renderer::RenderScene scene;owner.present(scene,0.0);
  REQUIRE(owner.sprites().size()==1U);
  const auto first=created.projection->materialize_temporary_sprites(owner.sprites(),0.0);REQUIRE(first);
  REQUIRE(first->frame->sprite_instances().size()==1U);
  const auto& first_instance=first->frame->sprite_instances()[0];
  CHECK(first_instance.selected_frame_index==0U);
  CHECK(first_instance.blend_mode==hlclient::entity_render::SpriteBlendMode::additive);
  CHECK(first->package->sprite_assets()[first_instance.sprite_asset_index]->frames().size()==4U);
  CHECK(first->package->sprite_assets()[first_instance.sprite_asset_index]->render_profile()==
      hlclient::entity_render::SpriteRenderTextureProfile::additive);
  owner.present(scene,0.15);
  const auto advanced=created.projection->materialize_temporary_sprites(owner.sprites(),0.15);REQUIRE(advanced);
  CHECK(advanced->frame->sprite_instances()[0].selected_frame_index==1U);
  CHECK(advanced->frame->sprite_instances()[0].transform.origin.z==Catch::Approx(1.2F));
  CHECK(advanced->package==first->package);
  // A viewmodel switch/death notification is not a world-effect lifetime reset.
  state.weapon_hud.active_weapon_id=4;
  state.lifecycle.life_epoch=1;state.lifecycle.state=client::LocalPlayerLifeState::dead;
  state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  host.observe(state,0.18);owner.present(scene,0.18);
  REQUIRE(owner.sprites().size()==1U);
  CHECK(owner.sprites()[0].identity==batch.effects[0].identity);
  const auto after_life_change=created.projection->materialize_temporary_sprites(owner.sprites(),0.18);
  REQUIRE(after_life_change);CHECK(after_life_change->frame->sprite_instances()[0].selected_frame_index==1U);
  host.committed_temporary_effect(event);CHECK(host.drain_temporary_effects(0.15).count==0U);
  owner.consume(batch,0.15);CHECK(owner.statistics().accepted==1U);CHECK(owner.statistics().duplicate==1U);
  owner.present(scene,0.4);CHECK_FALSE(created.projection->materialize_temporary_sprites(owner.sprites(),0.4));
  CHECK(created.projection->temporary_sprite_statistics().expired>=1U);
  // A second server event at the same position uses its own resource/identity.
  host.committed_temporary_effect(e16_effect(true));
  const auto smoke_batch=host.drain_temporary_effects(0.25);REQUIRE(smoke_batch.count==1U);
  owner.reset(1);owner.consume(smoke_batch,0.25);owner.present(scene,0.25);
  const auto smoke=created.projection->materialize_temporary_sprites(owner.sprites(),0.25);REQUIRE(smoke);
  const auto& smoke_instance=smoke->frame->sprite_instances()[0];
  CHECK(smoke_instance.sprite_asset_index!=first_instance.sprite_asset_index);
  CHECK(smoke_instance.orientation==hlclient::assets::SpriteOrientation::view_parallel_oriented);
  CHECK(smoke_instance.blend_mode==hlclient::entity_render::SpriteBlendMode::alpha_blended);
  CHECK(smoke_instance.opacity==Catch::Approx(0.65F));
  CHECK(smoke->package->sprite_assets()[smoke_instance.sprite_asset_index]->render_profile()==
      hlclient::entity_render::SpriteRenderTextureProfile::index_alpha);
  const auto& pixels=smoke->package->sprite_assets()[smoke_instance.sprite_asset_index]->frames()[0].rgba8;
  REQUIRE(pixels.size()==8U*8U*4U);CHECK(pixels[3]==std::byte{0});CHECK(pixels[(3U*8U+3U)*4U+3U]==std::byte{128});
  owner.present(scene,4.4);CHECK(owner.sprites().empty());
  owner.reset(2);owner.consume(batch,0.0);owner.present(scene,0.0);CHECK(owner.sprites().empty());
  CHECK(owner.statistics().stale==1U);host.teardown();CHECK(host.drain_temporary_effects(5.0).count==0U);
}

[[nodiscard]] std::shared_ptr<const hlclient::world_render::WorldRenderPackage> e16_dark_wall() {
  namespace wf=fixture::world_render_fixture;
  auto world=wf::make_world();
  for(auto& vertex:world.vertices) {
    vertex.position={vertex.position.x-8.0F,3.0F,vertex.position.y-8.0F};vertex.normal={0,-1,0};
  }
  world.bounds={{-8,3,-8},{8,3,8}};world.surfaces[0].bounds=world.bounds;
  auto texture=wf::make_texture(false);
  for(auto& mip:texture.mip_levels) for(std::size_t y=0;y<mip.height;++y) for(std::size_t x=0;x<mip.width;++x) {
    const auto value=static_cast<unsigned char>((x+y)%2 ? 32 : 48);
    for(std::size_t c=0;c<3;++c) mip.rgba_pixels[(y*mip.width+x)*4+c]=std::byte{value};
  }
  hlclient::assets::WorldMaterialTextureBinding binding;
  binding.material_index=0;binding.texture_asset_index=0;binding.source_bsp_texture_index=0;
  binding.status=hlclient::assets::WorldMaterialTextureBindingStatus::resolved_embedded;
  auto textures=hlclient::assets::WorldTextureSet::create({texture},{binding},{},1U);REQUIRE(textures);
  auto package=hlclient::world_render::WorldRenderPackageBuilder{}.build(
      {std::move(world),std::move(*textures.texture_set)},wf::make_lightmap_set({}));REQUIRE(package);
  return std::make_shared<const hlclient::world_render::WorldRenderPackage>(std::move(*package.package));
}

[[nodiscard]] std::size_t e16_pixel_at(const hlclient::renderer::RenderCamera& camera,
    hlclient::assets::AssetVector3 point) {
  const auto matrix=hlclient::renderer::camera_view_projection(camera,{320,240});REQUIRE(matrix);
  const auto& m=matrix.matrix->values;
  const auto x=m[0]*point.x+m[4]*point.y+m[8]*point.z+m[12];
  const auto y=m[1]*point.x+m[5]*point.y+m[9]*point.z+m[13];
  const auto w=m[3]*point.x+m[7]*point.y+m[11]*point.z+m[15];REQUIRE(w>0);
  const auto px=static_cast<std::size_t>((x/w*0.5F+0.5F)*320);
  const auto py=static_cast<std::size_t>((y/w*0.5F+0.5F)*240);
  REQUIRE(px<320);REQUIRE(py<240);return (py*320+px)*4;
}

TEST_CASE("E16 actual HalfLife temporary sprite GL preserves dark wall coverage depth and expiry",
    "[e16][temporary-effects][local-capture-assets][opengl][actual-context]") {
  E16TemporarySpriteContext context;auto created=context.create();REQUIRE(created.projection);
  auto state=observation();state.packet_entities.clear();state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  client::ClientWorldState world;REQUIRE(created.projection->reset_generation(state,world));
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if(!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  hlclient::game_api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
  host.reset({1,1,1});host.set_movement_audio_time_origin(10);
  app::TemporaryEffectPresentation owner;owner.reset(1);
  hlclient::renderer::RenderScene baseline;
  baseline.camera={{0,-24,0},{0,0,0},{0,0,1}};
  baseline.static_world=hlclient::renderer::RenderStaticWorld{e16_dark_wall()};
  gl->renderer().render(baseline,{320,240});
  const auto before=gl->renderer().observe_framebuffer({320,240},baseline.clear_color,true);REQUIRE(before);
  const auto center=e16_pixel_at(baseline.camera,{0,0,0});
  const auto neutral=e16_pixel_at(baseline.camera,{-3,0,0});
  const auto outside=e16_pixel_at(baseline.camera,{-6,0,0});
  const auto equal_pixel=[](const auto& a,const auto& b,std::size_t at) {
    return std::equal(a.rgba8.begin()+at,a.rgba8.begin()+at+3,b.rgba8.begin()+at);
  };
  for(const bool smoke:{false,true}) {
    host.reset({1,1,1});host.set_movement_audio_time_origin(10);owner.reset(1);
    const auto event=e16_effect(smoke);host.committed_temporary_effect(event);
    const double now=smoke ? 0.25 : 0.0;
    const auto batch=host.drain_temporary_effects(now);REQUIRE(batch.count==1U);
    owner.consume(batch,now);auto scene=baseline;owner.present(scene,now);
    scene.transient_sprite_entities=created.projection->materialize_temporary_sprites(owner.sprites(),now);
    REQUIRE(scene.transient_sprite_entities);
    gl->renderer().render(scene,{320,240});
    const auto visible=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(visible);
    CHECK_FALSE(equal_pixel(visible,before,center));
    CHECK(equal_pixel(visible,before,neutral));CHECK(equal_pixel(visible,before,outside));
    std::size_t changed=0;
    for(std::size_t i=0;i<before.rgba8.size();i+=4) if(!equal_pixel(visible,before,i)) ++changed;
    CHECK(changed>20);CHECK(changed<320U*240U/4U);
    const auto uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;
    owner.present(scene,now+0.15);
    scene.transient_sprite_entities=created.projection->materialize_temporary_sprites(owner.sprites(),now+0.15);
    REQUIRE(scene.transient_sprite_entities);
    CHECK(scene.transient_sprite_entities->frame->sprite_instances()[0].selected_frame_index==1U);
    gl->renderer().render(scene,{320,240});
    const auto advanced=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(advanced);
    CHECK_FALSE(std::ranges::equal(advanced.rgba8,visible.rgba8));
    CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==uploads);
    GLboolean depth{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);CHECK(depth==GL_TRUE);
    CHECK(glIsEnabled(GL_BLEND)==GL_FALSE);CHECK(glGetError()==GL_NO_ERROR);
    // Server position behind the wall must be occluded, including blend formats.
    auto behind=owner.sprites()[0];behind.origin.y=6;behind.age_seconds=0;
    scene=baseline;scene.transient_sprite_entities=created.projection->materialize_temporary_sprites(std::span{&behind,1U},now+0.15);
    gl->renderer().render(scene,{320,240});
    const auto occluded=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(occluded);
    CHECK(std::ranges::equal(occluded.rgba8,before.rgba8));
    scene=baseline;owner.present(scene,now+0.5);
    scene.transient_sprite_entities=created.projection->materialize_temporary_sprites(owner.sprites(),now+0.5);
    CHECK_FALSE(scene.transient_sprite_entities);
    gl->renderer().render(scene,{320,240});
    const auto expired=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(expired);
    CHECK(std::ranges::equal(expired.rgba8,before.rgba8));
  }
  owner.reset(2);host.teardown();gl->release_renderer();
}

TEST_CASE("E16 opt-in approved installed projectile explosion smoke and launch PCM are read-only",
    "[e16][local-m203-control][local-capture-assets]") {
  std::filesystem::path root;
#ifdef _WIN32
  wchar_t* value=nullptr;std::size_t count=0;
  if(_wdupenv_s(&value,&count,L"HLCLIENT_LOCAL_GAME_ROOT")==0 && value) root=value;
  std::free(value);
#endif
  if(root.lexically_normal()!=std::filesystem::path{"D:/DEV/HLCLIENT-RESEARCH/Half-Life"}.lexically_normal())
    SKIP("No exact opt-in approved research root");
  const std::array tokens{"maps/crossfire.bsp","models/grenade.mdl","sprites/zerogxplode.spr",
      "sprites/WXplo1.spr","sprites/steam1.spr","sound/weapons/glauncher.wav","sound/weapons/glauncher2.wav"};
  std::array<std::filesystem::file_time_type,tokens.size()> times;
  for(std::size_t i=0;i<tokens.size();++i) {
    if(!std::filesystem::is_regular_file(root/"valve"/tokens[i])) SKIP("Optional M203 asset absent");
    times[i]=std::filesystem::last_write_time(root/"valve"/tokens[i]);
  }
  const auto resources=readiness::parse_resource_list({{2U,tokens[0],9U,0U,0U},{2U,tokens[1],7U,0U,0U},
      {2U,tokens[2],19U,0U,0U},{2U,tokens[3],20U,0U,0U},{2U,tokens[4],73U,0U,0U},
      {0U,"weapons/glauncher.wav",1U,0U,0U},{0U,"weapons/glauncher2.wav",2U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info(tokens[0]),root,"valve",
      app::ReplayLocalCameraPolicy::external_live_receiving_client);REQUIRE(created.projection);
  const auto grenade=created.projection->studio_model_data(7);REQUIRE(grenade);CHECK_FALSE(grenade->submodels.empty());
  auto state=observation();state.packet_entities.clear();state.canonical_state_hash=client::runtime_observation_canonical_hash(state);
  client::ClientWorldState world;REQUIRE(created.projection->reset_generation(state,world));
  for(const auto slot:{19U,20U,73U}) {
    app::TemporarySpriteFrame sprite;
    sprite.identity={1,1,24,slot};sprite.sprite.model_slot=static_cast<std::uint16_t>(slot);
    sprite.sprite.scale=1;sprite.sprite.frames_per_second=10;
    sprite.sprite.blend=slot==73 ? hlclient::game_api::TemporarySpriteBlend::alpha_blended :
        hlclient::game_api::TemporarySpriteBlend::additive;
    const auto first=created.projection->materialize_temporary_sprites(std::span{&sprite,1U},0.0);REQUIRE(first);
    const auto& instance=first->frame->sprite_instances()[0];
    const auto& asset=*first->package->sprite_assets()[instance.sprite_asset_index];
    CHECK(asset.frames().size()==(slot==73 ? 16U : 15U));
    CHECK(asset.render_profile()==(slot==73 ? hlclient::entity_render::SpriteRenderTextureProfile::index_alpha :
        hlclient::entity_render::SpriteRenderTextureProfile::additive));
    sprite.age_seconds=.25;
    const auto advanced=created.projection->materialize_temporary_sprites(std::span{&sprite,1U},.25);REQUIRE(advanced);
    CHECK(advanced->frame->sprite_instances()[0].selected_frame_index==2U);
    CHECK(advanced->package==first->package);
  }
  // Optional installed controls retain source assets and use the same actual
  // sprite/Studio backend as the project-owned mandatory GL A/B above.
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if(gl && fixture::entity_opengl_fixture::capable_context()) {
    gl->initialize_renderer();
    hlclient::renderer::RenderScene baseline;baseline.camera={{0,-80,0},{0,0,0},{0,0,1}};
    gl->renderer().render(baseline,{320,240});
    const auto empty=gl->renderer().observe_framebuffer({320,240},baseline.clear_color,true);REQUIRE(empty);
    for(const auto slot:{19U,20U,73U}) {
      app::TemporarySpriteFrame sprite;
      sprite.identity={1,1,24,slot};sprite.sprite.model_slot=static_cast<std::uint16_t>(slot);
      sprite.sprite.scale=.5F;sprite.sprite.frames_per_second=10;sprite.age_seconds=.25;
      sprite.sprite.blend=slot==73 ? hlclient::game_api::TemporarySpriteBlend::alpha_blended :
          hlclient::game_api::TemporarySpriteBlend::additive;
      auto scene=baseline;
      scene.transient_sprite_entities=created.projection->materialize_temporary_sprites(std::span{&sprite,1U},.25);
      REQUIRE(scene.transient_sprite_entities);
      gl->renderer().render(scene,{320,240});
      const auto pixels=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(pixels);
      CHECK_FALSE(std::ranges::equal(pixels.rgba8,empty.rgba8));CHECK(pixels.non_clear_pixel_count>0);
      const auto uploads=gl->renderer().entity_statistics().sprite_asset_upload_count;
      gl->renderer().render(scene,{320,240});
      CHECK(gl->renderer().entity_statistics().sprite_asset_upload_count==uploads);
      CHECK(glGetError()==GL_NO_ERROR);
    }
    auto projectile=e16_projectiles(2,1.0);projectile.packet_entities.resize(1);
    projectile.packet_entities[0].origin={0.0,0.0,24.0};
    projectile.canonical_state_hash=client::runtime_observation_canonical_hash(projectile);
    REQUIRE(created.projection->project_entities(projectile,world));
    world.set_camera({{40,0,24},{0,0,24},{0,0,1}});
    REQUIRE(world.entity_frame()->studio_instances().size()==1);
    auto scene=client::build_render_scene(world);scene.static_world.reset();
    gl->renderer().render(scene,{320,240});
    const auto pixels=gl->renderer().observe_framebuffer({320,240},scene.clear_color,true);REQUIRE(pixels);
    CHECK(pixels.non_clear_pixel_count>0);CHECK(glGetError()==GL_NO_ERROR);
    gl->release_renderer();
  } else WARN("Optional installed asset GPU control unavailable; approved CPU imports/PCM still checked");
  hlclient::goldsrc::ApprovedSoundAssets sounds{created.projection->sound_resources()};
  for(const auto slot:{1U,2U}) {
    auto sample=sounds.request(static_cast<std::uint16_t>(slot));
    for(unsigned retry=0;retry<1000 && sample.status==hlclient::goldsrc::SoundAssetStatus::pending;++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});sample=sounds.request(static_cast<std::uint16_t>(slot));
    }
    REQUIRE(sample.status==hlclient::goldsrc::SoundAssetStatus::ready);REQUIRE(sample.asset);
    CHECK(std::ranges::all_of(sample.asset->interleaved_samples,[](float x){return std::isfinite(x);}));
    CHECK(std::ranges::any_of(sample.asset->interleaved_samples,[](float x){return x!=0;}));
    hlclient::audio::Mixer mixer;mixer.listener({{0,0,0},{0,-1,0},1.0F,false});
    hlclient::audio::Command command;command.operation=hlclient::audio::Operation::start;command.voice=1;
    command.asset=sample.asset;command.local=true;command.volume=1;command.attenuation=0;command.loop_enabled=false;
    mixer.command(command);std::vector<float> pcm(48000U*2U);mixer.render(pcm);
    CHECK(mixer.statistics().started==1U);
    CHECK(std::ranges::all_of(pcm,[](float x){return std::isfinite(x);}));
    CHECK(std::ranges::any_of(pcm,[](float x){return x!=0;}));
  }
  for(std::size_t i=0;i<tokens.size();++i) CHECK(times[i]==std::filesystem::last_write_time(root/"valve"/tokens[i]));
}
#endif
} // namespace

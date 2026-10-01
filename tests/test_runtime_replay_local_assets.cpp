#include <hlclient/app/runtime_replay_local_assets.hpp>
#if HLCLIENT_HAS_GAME_HALFLIFE
#include <hlclient/games/halflife/presentation.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#endif
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include "entity_visual/entity_visual_test_fixture.hpp"
#include "entity_render/entity_opengl_test_support.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "collision_brush_test_fixture.hpp"
#include "world_render_test_fixture.hpp"
#include "player_origin_test_fixture.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
               std::optional<std::uint8_t> world_light = {}) {
    fixture::SyntheticBspBuilder builder;
    const std::string text = "{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
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
    return {true, 0.1, 0.2, 128.0, 8U, 0U};
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
    return result;
  }
  std::int32_t selected_body{1};
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
  }
  CHECK(frame0->studio_instances()[0U].transform.origin.x == Catch::Approx(0.0F));
  CHECK(current.packet_entities[1U].origin.x == 20.0);
  CHECK(current.canonical_state_hash == client::runtime_observation_canonical_hash(current));
  CHECK(module->requests == 5U); // Only the remote player, never non-player/brush/local.
  CHECK(host.drain_audio().count == 0U);
  REQUIRE(e10_present(*created.projection, host, 100.0 + 0.050, 1U, world, {640, 480}));
  CHECK(e10_instance(world, 2U).transform.origin.x == Catch::Approx(20.0F));
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
    CHECK_FALSE(e10_instance(world, 2U).static_light_rgb); // No hidden default in committed frames.
    REQUIRE(e10_present(*created.projection, host, 100.0, 1U, world, {640,480}));
    const auto& instance = e10_instance(world, 2U);
    CHECK(instance.static_light_rgb.has_value() == lit);
    if (lit) {
      REQUIRE(instance.static_light_rgb);
      for (const auto component : *instance.static_light_rgb)
        CHECK(component == Catch::Approx(80.0F / 255.0F).margin(0.0001F));
    }
    CHECK_FALSE(e10_instance(world, 40U).static_light_rgb); // Non-player owner unchanged.
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
  CHECK(shown->frame->studio_instances()[0].transform.origin.z == 0.0F);
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
  for (const auto model_name : {"v_crowbar.mdl", "v_9mmhandgun.mdl"}) {
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
      std::string_view{model_name} == "v_crowbar.mdl" ? 1U : 2U};
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
  CHECK(metadata->sequences.size() >= (glock ? 10U : 9U));
  const auto initial_uploads = gl->renderer().entity_statistics().studio_asset_upload_count;
  std::uint64_t restart = 100U;
  double animation_time = 3.0;
  std::optional<std::uint64_t> idle_signature;
  for (const auto seq : glock ? std::vector<std::uint32_t>{0U,3U,6U,0U}
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
      {2U,"models/w_medkit.mdl",200U,0U,0U}, {2U,"models/w_battery.mdl",201U,0U,0U}});
  auto created=app::RuntimeReplayLocalAssets::create(resources,readiness::parse_server_info("maps/crossfire.bsp"),
      root,"valve",app::ReplayLocalCameraPolicy::external_live_receiving_client);
  INFO((created.error ? created.error->context : "ready")); REQUIRE(created.projection);
  CHECK(created.projection->summary().imported_studio==2U);
  auto gl=fixture::entity_opengl_fixture::try_context(320,240);
  if (!gl || !fixture::entity_opengl_fixture::capable_context()) SKIP("OpenGL 3.3 unavailable");
  gl->initialize_renderer();
  auto state=observation(); state.packet_entities.resize(1);
  auto& entity=state.packet_entities[0];
  entity.origin={0.0,0.0,0.0}; entity.angles={0.0,0.0,0.0};
  client::ClientWorldState world;
  for (const auto model_index : {1U,12U,21U,38U}) {
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
} // namespace

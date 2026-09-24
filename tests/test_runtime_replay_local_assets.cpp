#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>

#include "entity_visual/entity_visual_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace {
namespace app = hlclient::app;
namespace client = hlclient::client;
namespace visual = hlclient::entity_visual;
namespace fixture = hlclient::tests;
namespace readiness = fixture::readiness_fixture;

struct LocalContext {
  fixture::ScopedLocalResourceTestRoot root;
  std::vector<std::byte> map;
  std::vector<std::byte> model = fixture::literal_minimal_goldsrc_studio_v10();
  LocalContext() {
    fixture::SyntheticBspBuilder builder;
    const std::string text = "{\n\"classname\" \"worldspawn\"\n\"wad\" \"test.wad\"\n}\n";
    const auto bytes = std::as_bytes(std::span{text});
    builder.lump(fixture::SyntheticBspLumpId::entities)
        .assign(bytes.begin(), bytes.end());
    map = builder.build();
    fixture::SyntheticWad3Entry texture;
    texture.name="TEST_QUAD";
    texture.payload=fixture::synthetic_goldsrc_miptex("TEST_QUAD",64U,64U);
    root.write("valve","test.wad",fixture::synthetic_wad3({std::move(texture)}).bytes);
    root.write("valve", "maps/test_map.bsp", map);
    root.write("valve", "models/shared.mdl", model);
    root.write("valve", "sound/shared.wav", "sound");
  }
  auto resources(const bool mismatch = false) const {
    return readiness::parse_resource_list({
        {0U, "shared.wav", 7U, 5U, 0U},
        {2U, "maps/test_map.bsp", 9U, static_cast<std::uint32_t>(map.size()),
         0U},
        {2U, "models/shared.mdl", 7U,
         static_cast<std::uint32_t>(model.size()) + (mismatch ? 1U : 0U), 0U},
        {2U, "models/shared.mdl", 15U, static_cast<std::uint32_t>(model.size()),
         0U},
        {2U, "models/missing.mdl", 20U, 4U, 0U},
        {2U, "*1", 27U, 0U, 0U},
    });
  }
  auto create(const bool mismatch = false) const {
    return app::RuntimeReplayLocalAssets::create(
        resources(mismatch), readiness::parse_server_info("maps/test_map.bsp"),
        root.path(), "valve");
  }
};

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
            app::ReplayLocalVisualStatus::inline_brush_unsupported) == 1U);
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

TEST_CASE("Recorded positive model size mismatch cannot silently substitute a "
          "local file",
          "[local-capture-assets][security]") {
  LocalContext context;
  const auto created = context.create(true);
  CHECK_FALSE(created.projection);
  REQUIRE(created.error);
  CHECK(created.error->context.find("size mismatch") != std::string::npos);
}
} // namespace

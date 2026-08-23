#include <hlclient/goldsrc/precache_asset_dispatch.hpp>

#include "local_resource_readiness_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "synthetic_asset_importers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace {

namespace assets = hlclient::assets;
namespace fixture = hlclient::tests::readiness_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace synthetic = hlclient::tests::synthetic_assets;
using hlclient::tests::ScopedLocalResourceTestRoot;

[[nodiscard]] goldsrc::PrecacheManifestState make_manifest(
    const hlclient::local_resources::LocalResourceEnvironment &environment) {
  auto list = fixture::parse_resource_list({
      {0U, "misleading.mdl", 71U, 0U, 0U},
      {3U, "{metadata", 5U, 0U, 0U},
      {2U, "maps/world.data", 901U, 0U, 0U},
      {2U, "models/not_world.bsp", 3U, 0U, 0U},
      {4U, "generic/fake_world.bsp", 44U, 0U, 0U},
      {5U, "events/fake_audio.wav", 45U, 0U, 0U},
  });
  auto inventory = fixture::build_inventory(list, environment);
  const auto server = fixture::parse_server_info("maps/world.data");
  auto built = fixture::build_manifest(list, inventory, server, environment);
  INFO((built.error ? built.error->context : std::string{}));
  REQUIRE(built);
  REQUIRE(built.state);
  return std::move(*built.state);
}

void check_plan_metadata(const goldsrc::AssetDispatchPlan &plan,
                         const goldsrc::PrecacheManifestEntry &entry) {
  CHECK(plan.wire_ordinal() == entry.wire_ordinal());
  CHECK(plan.resource_type() == entry.resource_type());
  CHECK(plan.resource_index() == entry.resource_index());
  CHECK(plan.compatibility_profile() == entry.compatibility_profile());
  CHECK(plan.evidence_profile() == entry.evidence_profile());
}

TEST_CASE("Precache asset plans use exact world and resource-type evidence",
          "[goldsrc][asset-dispatch][plan]") {
  ScopedLocalResourceTestRoot root;
  root.write("valve", "sound/misleading.mdl", "sound");
  root.write("valve", "maps/world.data", "world");
  root.write("valve", "models/not_world.bsp", "model");
  root.write("valve", "generic/fake_world.bsp", "generic");
  root.write("valve", "events/fake_audio.wav", "event");
  auto environment = fixture::make_environment(root);
  const auto manifest = make_manifest(*environment);
  REQUIRE(manifest.entry_count() == 6U);
  REQUIRE(manifest.world_entry());
  CHECK(manifest.world_entry()->resource_index() == 901U);

  const goldsrc::AssetDispatchPlanBuilder builder;
  for (const auto &entry : manifest.entries()) {
    const auto built = builder.build(manifest, entry);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.plan);
    check_plan_metadata(*built.plan, entry);

    switch (entry.wire_ordinal()) {
    case 0U: {
      CHECK(built.plan->role() == assets::AssetDispatchRole::audio);
      CHECK_FALSE(built.plan->selected_world());
      const std::array expected{assets::AssetImporterCategory::audio};
      CHECK(std::ranges::equal(built.plan->allowed_importer_categories(),
                               expected));
      break;
    }
    case 1U:
      CHECK(built.plan->role() == assets::AssetDispatchRole::metadata_only);
      CHECK_FALSE(built.plan->selected_world());
      CHECK(built.plan->allowed_importer_categories().empty());
      break;
    case 2U: {
      CHECK(&entry == manifest.world_entry());
      CHECK(built.plan->role() == assets::AssetDispatchRole::world);
      CHECK(built.plan->selected_world());
      const std::array expected{assets::AssetImporterCategory::world};
      CHECK(std::ranges::equal(built.plan->allowed_importer_categories(),
                               expected));
      break;
    }
    case 3U: {
      CHECK(built.plan->role() == assets::AssetDispatchRole::model_or_sprite);
      CHECK_FALSE(built.plan->selected_world());
      const std::array expected{
          assets::AssetImporterCategory::model,
          assets::AssetImporterCategory::sprite,
      };
      CHECK(std::ranges::equal(built.plan->allowed_importer_categories(),
                               expected));
      break;
    }
    case 4U:
    case 5U:
      CHECK(built.plan->role() == assets::AssetDispatchRole::unsupported);
      CHECK_FALSE(built.plan->selected_world());
      CHECK(built.plan->allowed_importer_categories().empty());
      break;
    default:
      FAIL("Unexpected synthetic manifest ordinal");
    }
  }
}

TEST_CASE("Precache asset plan never infers world from extension or index",
          "[goldsrc][asset-dispatch][plan][evidence]") {
  ScopedLocalResourceTestRoot root;
  root.write("valve", "sound/misleading.mdl", "sound");
  root.write("valve", "maps/world.data", "world");
  root.write("valve", "models/not_world.bsp", "model");
  root.write("valve", "generic/fake_world.bsp", "generic");
  root.write("valve", "events/fake_audio.wav", "event");
  auto environment = fixture::make_environment(root);
  const auto manifest = make_manifest(*environment);
  const goldsrc::AssetDispatchPlanBuilder builder;

  REQUIRE(manifest.world_entry());
  CHECK(manifest.world_entry()->resource_index() == 901U);
  const auto fake_bsp_model = manifest.find(goldsrc::ResourceType::model, 3U);
  REQUIRE(fake_bsp_model);
  const auto model_plan = builder.build(manifest, *fake_bsp_model);
  REQUIRE(model_plan);
  REQUIRE(model_plan.plan);
  CHECK(model_plan.plan->role() == assets::AssetDispatchRole::model_or_sprite);
  CHECK_FALSE(model_plan.plan->selected_world());

  const auto fake_bsp_generic =
      manifest.find(goldsrc::ResourceType::generic, 44U);
  REQUIRE(fake_bsp_generic);
  const auto generic_plan = builder.build(manifest, *fake_bsp_generic);
  REQUIRE(generic_plan);
  REQUIRE(generic_plan.plan);
  CHECK(generic_plan.plan->role() == assets::AssetDispatchRole::unsupported);

  const auto misleading_sound =
      manifest.find(goldsrc::ResourceType::sound, 71U);
  REQUIRE(misleading_sound);
  const auto sound_plan = builder.build(manifest, *misleading_sound);
  REQUIRE(sound_plan);
  REQUIRE(sound_plan.plan);
  CHECK(sound_plan.plan->role() == assets::AssetDispatchRole::audio);
}

TEST_CASE("Precache asset plan requires the exact owning manifest entry",
          "[goldsrc][asset-dispatch][plan][identity]") {
  ScopedLocalResourceTestRoot root;
  root.write("valve", "sound/misleading.mdl", "sound");
  root.write("valve", "maps/world.data", "world");
  root.write("valve", "models/not_world.bsp", "model");
  root.write("valve", "generic/fake_world.bsp", "generic");
  root.write("valve", "events/fake_audio.wav", "event");
  auto environment = fixture::make_environment(root);
  const auto manifest = make_manifest(*environment);
  REQUIRE(manifest.world_entry());
  const auto detached = *manifest.world_entry();

  const auto built =
      goldsrc::AssetDispatchPlanBuilder{}.build(manifest, detached);

  REQUIRE_FALSE(built);
  CHECK_FALSE(built.plan);
  REQUIRE(built.error);
  CHECK(built.error->code ==
        goldsrc::AssetDispatchPlanErrorCode::entry_not_in_manifest);
  CHECK(built.error->wire_ordinal == detached.wire_ordinal());
}

TEST_CASE("Approved GoldSrc dispatcher rejects a source-plan mismatch before "
          "probing",
          "[goldsrc][asset-dispatch][plan][approved]") {
  ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  root.write("valve", "sound/misleading.mdl", "sound");
  root.write("valve", "maps/world.data", world_bytes);
  root.write("valve", "models/not_world.bsp", "model");
  root.write("valve", "generic/fake_world.bsp", "generic");
  root.write("valve", "events/fake_audio.wav", "event");
  auto owned_environment = fixture::make_environment(root);
  std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
      environment{std::move(owned_environment)};
  const auto manifest = make_manifest(*environment);
  REQUIRE(manifest.world_entry());
  const auto *other_model = manifest.find(goldsrc::ResourceType::model, 3U);
  REQUIRE(other_model);

  ScopedLocalResourceTestRoot replay_root;
  replay_root.write("valve", "sound/misleading.mdl", "sound");
  replay_root.write("valve", "maps/world.data", world_bytes);
  replay_root.write("valve", "models/not_world.bsp", "model");
  replay_root.write("valve", "generic/fake_world.bsp", "generic");
  replay_root.write("valve", "events/fake_audio.wav", "event");
  auto owned_replay_environment = fixture::make_environment(replay_root);
  std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
      replay_environment{std::move(owned_replay_environment)};
  const auto replay_manifest = make_manifest(*replay_environment);
  REQUIRE(replay_manifest.world_entry());

  const goldsrc::AssetDispatchPlanBuilder builder;
  const auto world_built = builder.build(manifest, *manifest.world_entry());
  const auto other_built = builder.build(manifest, *other_model);
  const auto replay_built =
      builder.build(replay_manifest, *replay_manifest.world_entry());
  REQUIRE(world_built);
  REQUIRE(world_built.plan);
  REQUIRE(other_built);
  REQUIRE(other_built.plan);
  REQUIRE(replay_built);
  REQUIRE(replay_built.plan);
  CHECK(replay_built.plan->wire_ordinal() == world_built.plan->wire_ordinal());
  CHECK(replay_built.plan->resource_type() == world_built.plan->resource_type());
  CHECK(replay_built.plan->resource_index() ==
        world_built.plan->resource_index());
  CHECK(replay_built.plan->role() == world_built.plan->role());
  CHECK(replay_built.plan->compatibility_profile() ==
        world_built.plan->compatibility_profile());
  CHECK(replay_built.plan->evidence_profile() ==
        world_built.plan->evidence_profile());
  REQUIRE(manifest.world_entry()->locator());
  REQUIRE(replay_manifest.world_entry()->locator());
  CHECK((manifest.world_entry()->locator()->root_id() !=
             replay_manifest.world_entry()->locator()->root_id() ||
         manifest.world_entry()->locator()->expected_identity() !=
             replay_manifest.world_entry()->locator()->expected_identity()));

  goldsrc::ApprovedAssetSourceOpener opener;
  auto begun = opener.begin(*world_built.plan, environment);
  INFO((begun.error ? begun.error->context : std::string{}));
  REQUIRE(begun);
  REQUIRE(begun.operation);
  auto operation = std::move(*begun.operation);
  for (std::size_t update = 0U; update < 16U; ++update) {
    if (operation.state() ==
        goldsrc::ApprovedAssetSourceOpenState::source_ready) {
      break;
    }
    operation.update(
        goldsrc::ApprovedAssetSourceOpenTimePoint{} +
        std::chrono::milliseconds{static_cast<std::int64_t>(update)});
  }
  REQUIRE(operation.state() ==
          goldsrc::ApprovedAssetSourceOpenState::source_ready);
  auto source = operation.take_result();
  REQUIRE(source);

  synthetic::SyntheticImporterCounts world_counts;
  synthetic::SyntheticImporterCounts model_counts;
  synthetic::SyntheticImporterCounts sprite_counts;
  assets::AssetImporterRegistries registries;
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<synthetic::SyntheticWorldImporter>("approved-world",
                                                          world_counts)));
  REQUIRE(registries.models.register_importer(
      std::make_unique<synthetic::SyntheticModelImporter>("mismatched-model",
                                                          model_counts)));
  REQUIRE(registries.sprites.register_importer(
      std::make_unique<synthetic::SyntheticSpriteImporter>("mismatched-sprite",
                                                           sprite_counts)));
  const goldsrc::ApprovedAssetImporterDispatcher dispatcher{registries};

  const auto replay_mismatch =
      dispatcher.dispatch(*source, *replay_built.plan);

  CHECK(replay_mismatch.state == assets::AssetDispatchState::source_invalid);
  CHECK_FALSE(replay_mismatch.asset);
  CHECK(replay_mismatch.selected_category ==
        assets::AssetImporterCategory::none);
  REQUIRE(replay_mismatch.error);
  CHECK(replay_mismatch.error->code == assets::AssetErrorCode::ImportFailed);
  CHECK(world_counts.probe_count == 0U);
  CHECK(world_counts.import_count == 0U);

  const auto mismatch = dispatcher.dispatch(*source, *other_built.plan);

  CHECK(mismatch.state == assets::AssetDispatchState::source_invalid);
  CHECK_FALSE(mismatch.asset);
  CHECK(mismatch.selected_category == assets::AssetImporterCategory::none);
  REQUIRE(mismatch.error);
  CHECK(mismatch.error->code == assets::AssetErrorCode::ImportFailed);
  CHECK(world_counts.probe_count == 0U);
  CHECK(model_counts.probe_count == 0U);
  CHECK(sprite_counts.probe_count == 0U);
  CHECK(world_counts.import_count == 0U);
  CHECK(model_counts.import_count == 0U);
  CHECK(sprite_counts.import_count == 0U);

  const auto matched = dispatcher.dispatch(*source, *world_built.plan);
  REQUIRE(matched.imported());
  CHECK(matched.state == assets::AssetDispatchState::imported);
  REQUIRE(matched.asset);
  CHECK(std::holds_alternative<assets::WorldAsset>(*matched.asset));
  CHECK(world_counts.probe_count == 1U);
  CHECK(world_counts.import_count == 1U);
  CHECK(model_counts.probe_count == 0U);
  CHECK(sprite_counts.probe_count == 0U);
}

} // namespace

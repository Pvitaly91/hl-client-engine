#include <hlclient/assets/asset_importer_dispatcher.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace assets = hlclient::assets;

struct ImporterCounts {
  int probes{0};
  int imports{0};
};

class SyntheticAttachment final : public assets::AssetImportAttachment {
public:
  explicit SyntheticAttachment(const int marker) : marker_{marker} {}
  [[nodiscard]] int marker() const noexcept { return marker_; }

private:
  int marker_{0};
};

class AttachedWorldImporter final : public assets::IWorldImporter {
public:
  [[nodiscard]] std::string_view id() const noexcept override {
    return "attached-world";
  }

  [[nodiscard]] assets::AssetProbeConfidence
  probe(const assets::AssetProbe &) const noexcept override {
    return 100U;
  }

  [[nodiscard]] assets::WorldAssetResult
  import(const assets::AssetSource &) const override {
    return assets::WorldAssetResult::success(
        assets::WorldAsset{},
        std::make_shared<const SyntheticAttachment>(42));
  }
};

enum class ImportBehavior {
  success,
  malformed,
  unsupported_after_match,
  standard_exception,
  unknown_exception,
};

template <class Asset>
class RecordingImporter final : public assets::IAssetImporter<Asset> {
public:
  RecordingImporter(std::string importer_id,
                    const assets::AssetProbeConfidence confidence,
                    ImporterCounts &counts,
                    const ImportBehavior behavior = ImportBehavior::success)
      : importer_id_{std::move(importer_id)}, confidence_{confidence},
        counts_{counts}, behavior_{behavior} {}

  [[nodiscard]] std::string_view id() const noexcept override {
    return importer_id_;
  }

  [[nodiscard]] assets::AssetProbeConfidence
  probe(const assets::AssetProbe &) const noexcept override {
    ++counts_.probes;
    return confidence_;
  }

  [[nodiscard]] assets::AssetResult<Asset>
  import(const assets::AssetSource &) const override {
    ++counts_.imports;
    switch (behavior_) {
    case ImportBehavior::success: {
      Asset asset;
      asset.identity.source_name = importer_id_;
      return assets::AssetResult<Asset>::success(std::move(asset));
    }
    case ImportBehavior::malformed:
      return assets::AssetResult<Asset>::failure(assets::AssetError{
          assets::AssetErrorCode::MalformedData,
          {},
          {},
          "Synthetic source is malformed",
          {},
      });
    case ImportBehavior::unsupported_after_match:
      return assets::AssetResult<Asset>::failure(assets::AssetError{
          assets::AssetErrorCode::UnsupportedFormat,
          {},
          {},
          "Synthetic importer rejected a source it had probed",
          {},
      });
    case ImportBehavior::standard_exception:
      throw std::runtime_error{"synthetic importer exception"};
    case ImportBehavior::unknown_exception:
      throw 42;
    }
    throw std::runtime_error{"Invalid synthetic importer behavior"};
  }

private:
  std::string importer_id_;
  assets::AssetProbeConfidence confidence_{assets::kAssetProbeNoMatch};
  ImporterCounts &counts_;
  ImportBehavior behavior_{ImportBehavior::success};
};

[[nodiscard]] assets::AssetSource make_source(
    std::filesystem::path path = "maps/test_map.bsp",
    std::optional<assets::AssetSourceMetadata> metadata = std::nullopt) {
  auto created = assets::AssetSource::create(
      std::move(path),
      {std::byte{0x53}, std::byte{0x59}, std::byte{0x4E}, std::byte{0x54}},
      std::move(metadata));
  if (!created) {
    throw std::runtime_error{"Unable to create synthetic asset source"};
  }
  return std::move(*created.source);
}

template <class Asset>
void register_importer(
    assets::AssetImporterRegistry<Asset> &registry, std::string id,
    const assets::AssetProbeConfidence confidence, const int priority,
    ImporterCounts &counts,
    const ImportBehavior behavior = ImportBehavior::success) {
  const auto registered = registry.register_importer(
      std::make_unique<RecordingImporter<Asset>>(std::move(id), confidence,
                                                 counts, behavior),
      priority);
  if (!registered) {
    throw std::runtime_error{"Unable to register synthetic importer"};
  }
}

TEST_CASE("World dispatch probes and imports only the world category",
          "[assets][dispatch]") {
  ImporterCounts model;
  ImporterCounts world;
  ImporterCounts sprite;
  ImporterCounts image;
  ImporterCounts audio;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "mdl", 100U, 0, model);
  register_importer(registries.worlds, "bsp", 100U, 0, world);
  register_importer(registries.sprites, "spr", 100U, 0, sprite);
  register_importer(registries.images, "image", 100U, 0, image);
  register_importer(registries.audio, "wav", 100U, 0, audio);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source(), assets::AssetDispatchRole::world);

  REQUIRE(result.imported());
  CHECK(result.state == assets::AssetDispatchState::imported);
  CHECK(result.selected_category == assets::AssetImporterCategory::world);
  CHECK(result.selected_importer_id == "world:bsp");
  REQUIRE(result.asset);
  CHECK(std::holds_alternative<assets::WorldAsset>(*result.asset));
  CHECK(world.probes == 1);
  CHECK(world.imports == 1);
  CHECK(model.probes == 0);
  CHECK(sprite.probes == 0);
  CHECK(image.probes == 0);
  CHECK(audio.probes == 0);
}

TEST_CASE("World dispatch preserves an immutable importer attachment",
          "[assets][dispatch][attachment]") {
  assets::AssetImporterRegistries registries;
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<AttachedWorldImporter>(), 0));
  const assets::AssetImporterDispatcher dispatcher{registries};

  const auto result =
      dispatcher.dispatch(make_source(), assets::AssetDispatchRole::world);

  REQUIRE(result.imported());
  REQUIRE(result.attachment);
  const auto attachment =
      std::dynamic_pointer_cast<const SyntheticAttachment>(result.attachment);
  REQUIRE(attachment);
  CHECK(attachment->marker() == 42);
}

TEST_CASE("Audio dispatch ignores a misleading model extension",
          "[assets][dispatch]") {
  ImporterCounts model;
  ImporterCounts audio;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "model-guess", 100U, 100, model);
  register_importer(registries.audio, "structural-audio", 80U, -10, audio);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result = dispatcher.dispatch(make_source("sound/misnamed.mdl"),
                                          assets::AssetDispatchRole::audio);

  REQUIRE(result.imported());
  CHECK(result.selected_category == assets::AssetImporterCategory::audio);
  CHECK(std::holds_alternative<assets::AudioAsset>(*result.asset));
  CHECK(model.probes == 0);
  CHECK(model.imports == 0);
  CHECK(audio.probes == 1);
  CHECK(audio.imports == 1);
}

TEST_CASE(
    "Model-or-sprite dispatch compares confidence before priority globally",
    "[assets][dispatch]") {
  ImporterCounts model;
  ImporterCounts sprite;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "model", 100U, -100, model);
  register_importer(registries.sprites, "sprite", 80U, 100, sprite);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source("models/shared.bin"),
                          assets::AssetDispatchRole::model_or_sprite);

  REQUIRE(result.imported());
  CHECK(result.selected_category == assets::AssetImporterCategory::model);
  CHECK(result.selected_importer_id == "model:model");
  CHECK(std::holds_alternative<assets::ModelAsset>(*result.asset));
  CHECK(model.probes == 1);
  CHECK(sprite.probes == 1);
  CHECK(model.imports == 1);
  CHECK(sprite.imports == 0);
}

TEST_CASE(
    "Probe-only dispatch selection reuses global ranking and imports once",
    "[assets][dispatch][selection]") {
  ImporterCounts model;
  ImporterCounts sprite;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "model", 100U, -100, model);
  register_importer(registries.sprites, "sprite", 80U, 100, sprite);
  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto source = make_source("models/shared.bin");

  auto selection = dispatcher.select(
      source, assets::AssetDispatchRole::model_or_sprite);

  REQUIRE(selection.selected());
  CHECK(selection.state == assets::AssetDispatchSelectionState::selected);
  CHECK(selection.selected_category == assets::AssetImporterCategory::model);
  CHECK(selection.selected_importer_id == "model:model");
  REQUIRE(selection.top_candidates.size() == 1U);
  CHECK(model.probes == 1);
  CHECK(sprite.probes == 1);
  CHECK(model.imports == 0);
  CHECK(sprite.imports == 0);

  const auto result =
      dispatcher.import_selected(source, std::move(selection));

  REQUIRE(result.imported());
  CHECK(result.selected_category == assets::AssetImporterCategory::model);
  CHECK(result.selected_importer_id == "model:model");
  CHECK(model.probes == 1);
  CHECK(sprite.probes == 1);
  CHECK(model.imports == 1);
  CHECK(sprite.imports == 0);
}

TEST_CASE("Model-or-sprite dispatch uses priority for equal confidence",
          "[assets][dispatch]") {
  ImporterCounts model;
  ImporterCounts sprite;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "model", 100U, 4, model);
  register_importer(registries.sprites, "sprite", 100U, 5, sprite);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source("models/shared.bin"),
                          assets::AssetDispatchRole::model_or_sprite);

  REQUIRE(result.imported());
  CHECK(result.selected_category == assets::AssetImporterCategory::sprite);
  CHECK(result.selected_importer_id == "sprite:sprite");
  CHECK(std::holds_alternative<assets::SpriteAsset>(*result.asset));
  CHECK(model.imports == 0);
  CHECK(sprite.imports == 1);
}

TEST_CASE("A cross-category exact tie is ambiguous and imports nothing",
          "[assets][dispatch]") {
  ImporterCounts model;
  ImporterCounts sprite;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "zeta", 100U, 5, model);
  register_importer(registries.sprites, "alpha", 100U, 5, sprite);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source("models/shared.bin"),
                          assets::AssetDispatchRole::model_or_sprite);

  CHECK_FALSE(result.imported());
  CHECK(result.state == assets::AssetDispatchState::ambiguous_importer);
  CHECK(result.selected_category == assets::AssetImporterCategory::none);
  REQUIRE(result.error);
  CHECK(result.error->code == assets::AssetErrorCode::AmbiguousFormat);
  const std::vector<std::string> expected{"model:zeta", "sprite:alpha"};
  CHECK(result.error->candidate_importer_ids == expected);
  REQUIRE(result.top_candidates.size() == 2U);
  CHECK(model.probes == 1);
  CHECK(sprite.probes == 1);
  CHECK(model.imports == 0);
  CHECK(sprite.imports == 0);
}

TEST_CASE(
    "Lower-ranked category ambiguity does not block a unique global winner",
    "[assets][dispatch]") {
  ImporterCounts model_one;
  ImporterCounts model_two;
  ImporterCounts sprite;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "model-one", 80U, 5, model_one);
  register_importer(registries.models, "model-two", 80U, 5, model_two);
  register_importer(registries.sprites, "sprite", 100U, 0, sprite);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source("models/shared.bin"),
                          assets::AssetDispatchRole::model_or_sprite);

  REQUIRE(result.imported());
  CHECK(result.selected_category == assets::AssetImporterCategory::sprite);
  REQUIRE(result.top_candidates.size() == 1U);
  CHECK(result.top_candidates.front().importer_id == "sprite:sprite");
  CHECK(model_one.probes == 1);
  CHECK(model_two.probes == 1);
  CHECK(sprite.probes == 1);
  CHECK(model_one.imports == 0);
  CHECK(model_two.imports == 0);
  CHECK(sprite.imports == 1);
}

TEST_CASE("No allowed candidate reaches the typed importer boundary",
          "[assets][dispatch]") {
  ImporterCounts model;
  ImporterCounts sprite;
  assets::AssetImporterRegistries registries;
  register_importer(registries.models, "model", 0U, 0, model);
  register_importer(registries.sprites, "sprite", 0U, 0, sprite);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source("models/unknown.bin"),
                          assets::AssetDispatchRole::model_or_sprite);

  CHECK(result.state == assets::AssetDispatchState::importer_not_registered);
  REQUIRE(result.error);
  CHECK(result.error->code == assets::AssetErrorCode::UnsupportedFormat);
  CHECK(result.top_candidates.empty());
  CHECK(model.probes == 1);
  CHECK(sprite.probes == 1);
  CHECK(model.imports == 0);
  CHECK(sprite.imports == 0);
}

TEST_CASE("Metadata-only and unsupported roles perform no probe",
          "[assets][dispatch]") {
  ImporterCounts world;
  assets::AssetImporterRegistries registries;
  register_importer(registries.worlds, "world", 100U, 0, world);
  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto source = make_source();

  const auto metadata =
      dispatcher.dispatch(source, assets::AssetDispatchRole::metadata_only);
  const auto unsupported =
      dispatcher.dispatch(source, assets::AssetDispatchRole::unsupported);

  CHECK(metadata.state == assets::AssetDispatchState::metadata_only_resource);
  CHECK_FALSE(metadata.error);
  CHECK(unsupported.state ==
        assets::AssetDispatchState::unsupported_asset_role);
  REQUIRE(unsupported.error);
  CHECK(unsupported.error->code == assets::AssetErrorCode::UnsupportedFormat);
  CHECK(world.probes == 0);
  CHECK(world.imports == 0);
}

TEST_CASE("A selected importer failure remains an import failure",
          "[assets][dispatch]") {
  ImporterCounts malformed;
  assets::AssetImporterRegistries registries;
  register_importer(registries.worlds, "broken-bsp", 100U, 0, malformed,
                    ImportBehavior::malformed);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source(), assets::AssetDispatchRole::world);

  CHECK(result.state == assets::AssetDispatchState::import_failed);
  CHECK(result.selected_category == assets::AssetImporterCategory::world);
  CHECK(result.selected_importer_id == "world:broken-bsp");
  REQUIRE(result.error);
  CHECK(result.error->code == assets::AssetErrorCode::MalformedData);
  CHECK(result.error->importer_id == "world:broken-bsp");
  CHECK(malformed.probes == 1);
  CHECK(malformed.imports == 1);
}

TEST_CASE("A selected importer cannot turn a match into a no-importer boundary",
          "[assets][dispatch]") {
  ImporterCounts importer;
  assets::AssetImporterRegistries registries;
  register_importer(registries.worlds, "late-reject", 100U, 0, importer,
                    ImportBehavior::unsupported_after_match);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source(), assets::AssetDispatchRole::world);

  CHECK(result.state == assets::AssetDispatchState::import_failed);
  REQUIRE(result.error);
  CHECK(result.error->code == assets::AssetErrorCode::UnsupportedFormat);
  CHECK(importer.probes == 1);
  CHECK(importer.imports == 1);
}

TEST_CASE("Dispatcher preserves registry exception isolation",
          "[assets][dispatch]") {
  ImporterCounts standard;
  assets::AssetImporterRegistries registries;
  register_importer(registries.audio, "throwing-audio", 100U, 0, standard,
                    ImportBehavior::standard_exception);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result = dispatcher.dispatch(make_source("sound/test.wav"),
                                          assets::AssetDispatchRole::audio);

  CHECK(result.state == assets::AssetDispatchState::import_failed);
  REQUIRE(result.error);
  CHECK(result.error->code == assets::AssetErrorCode::ImportFailed);
  CHECK(result.error->importer_id == "audio:throwing-audio");
  CHECK(result.error->context.find("synthetic importer exception") !=
        std::string::npos);
  CHECK(standard.probes == 1);
  CHECK(standard.imports == 1);
}

TEST_CASE(
    "Dispatcher rejects inconsistent owning source metadata before probing",
    "[assets][dispatch]") {
  ImporterCounts world;
  assets::AssetImporterRegistries registries;
  register_importer(registries.worlds, "world", 100U, 0, world);
  assets::AssetSourceMetadata metadata;
  metadata.content_size = 999U;

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source("maps/test_map.bsp", metadata),
                          assets::AssetDispatchRole::world);

  CHECK(result.state == assets::AssetDispatchState::source_invalid);
  REQUIRE(result.error);
  CHECK(result.error->code == assets::AssetErrorCode::ImportFailed);
  CHECK(world.probes == 0);
  CHECK(world.imports == 0);
}

TEST_CASE("Dispatcher keeps category-qualified importer diagnostics bounded",
          "[assets][dispatch]") {
  ImporterCounts model;
  ImporterCounts sprite;
  assets::AssetImporterRegistries registries;
  const std::string maximum_id(assets::kMaximumAssetImporterIdBytes, 'x');
  register_importer(registries.models, maximum_id, 100U, 0, model);
  register_importer(registries.sprites, maximum_id, 100U, 0, sprite);

  const assets::AssetImporterDispatcher dispatcher{registries};
  const auto result =
      dispatcher.dispatch(make_source("models/shared.bin"),
                          assets::AssetDispatchRole::model_or_sprite);

  CHECK(result.state == assets::AssetDispatchState::ambiguous_importer);
  REQUIRE(result.error);
  REQUIRE(result.error->candidate_importer_ids.size() == 2U);
  for (const auto &id : result.error->candidate_importer_ids) {
    CHECK(id.size() <= assets::kMaximumAssetDispatchImporterIdBytes);
  }
  CHECK(model.imports == 0);
  CHECK(sprite.imports == 0);
}

} // namespace

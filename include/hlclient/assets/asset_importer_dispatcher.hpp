#pragma once

#include <hlclient/assets/asset_importer_registry.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hlclient::assets {

inline constexpr std::size_t kMaximumAssetDispatchImporterIdBytes =
    kMaximumAssetImporterIdBytes + 8U;

enum class AssetDispatchRole {
  world,
  model_or_sprite,
  audio,
  metadata_only,
  unsupported,
};

enum class AssetImporterCategory {
  none,
  model,
  world,
  sprite,
  image,
  audio,
};

enum class AssetDispatchState {
  imported,
  importer_not_registered,
  ambiguous_importer,
  unsupported_asset_role,
  metadata_only_resource,
  source_invalid,
  import_failed,
};

using ImportedAsset =
    std::variant<ModelAsset, WorldAsset, SpriteAsset, ImageAsset, AudioAsset>;

struct AssetDispatchProbeCandidate {
  AssetImporterCategory category{AssetImporterCategory::none};
  // Category-qualified and bounded, for example "model:synthetic-mdl".
  std::string importer_id;
  AssetProbeConfidence confidence{kAssetProbeNoMatch};
  int priority{0};
};

struct AssetDispatchResult {
  AssetDispatchState state{AssetDispatchState::source_invalid};
  std::optional<ImportedAsset> asset;
  AssetImporterCategory selected_category{AssetImporterCategory::none};
  // Category-qualified when an importer was selected.
  std::string selected_importer_id;
  // Only candidates at the globally best confidence/priority rank.
  std::vector<AssetDispatchProbeCandidate> top_candidates;
  std::optional<AssetError> error;

  [[nodiscard]] bool imported() const noexcept {
    return state == AssetDispatchState::imported && asset.has_value();
  }
};

// Borrows the application-owned registries, which must outlive the dispatcher
// and must not be mutated while dispatch is possible. Dispatch probes only
// categories authorized by the caller's role and never performs filesystem I/O.
class AssetImporterDispatcher final {
public:
  explicit AssetImporterDispatcher(
      const AssetImporterRegistries &registries) noexcept;

  [[nodiscard]] AssetDispatchResult dispatch(const AssetSource &source,
                                             AssetDispatchRole role) const;

private:
  const AssetImporterRegistries &registries_;
};

[[nodiscard]] constexpr std::string_view
to_string(const AssetDispatchRole role) noexcept {
  switch (role) {
  case AssetDispatchRole::world:
    return "world";
  case AssetDispatchRole::model_or_sprite:
    return "model_or_sprite";
  case AssetDispatchRole::audio:
    return "audio";
  case AssetDispatchRole::metadata_only:
    return "metadata_only";
  case AssetDispatchRole::unsupported:
    return "unsupported";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
to_string(const AssetImporterCategory category) noexcept {
  switch (category) {
  case AssetImporterCategory::none:
    return "none";
  case AssetImporterCategory::model:
    return "model";
  case AssetImporterCategory::world:
    return "world";
  case AssetImporterCategory::sprite:
    return "sprite";
  case AssetImporterCategory::image:
    return "image";
  case AssetImporterCategory::audio:
    return "audio";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
to_string(const AssetDispatchState state) noexcept {
  switch (state) {
  case AssetDispatchState::imported:
    return "imported";
  case AssetDispatchState::importer_not_registered:
    return "importer_not_registered";
  case AssetDispatchState::ambiguous_importer:
    return "ambiguous_importer";
  case AssetDispatchState::unsupported_asset_role:
    return "unsupported_asset_role";
  case AssetDispatchState::metadata_only_resource:
    return "metadata_only_resource";
  case AssetDispatchState::source_invalid:
    return "source_invalid";
  case AssetDispatchState::import_failed:
    return "import_failed";
  }
  return "unknown";
}

} // namespace hlclient::assets

#include <hlclient/assets/asset_importer_dispatcher.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::assets {
namespace {

struct RankedCandidate {
  AssetImporterCategory category{AssetImporterCategory::none};
  std::string raw_importer_id;
  AssetProbeConfidence confidence{kAssetProbeNoMatch};
  int priority{0};
};

[[nodiscard]] bool source_metadata_valid(const AssetSource &source) noexcept {
  const auto &metadata = source.metadata();
  if (!metadata || !metadata->content_size) {
    return true;
  }
  return *metadata->content_size ==
         static_cast<std::uintmax_t>(source.bytes().size());
}

[[nodiscard]] std::string
qualified_importer_id(const AssetImporterCategory category,
                      const std::string_view raw_id) {
  const auto category_name = to_string(category);
  std::string result;
  result.reserve((std::min)(kMaximumAssetDispatchImporterIdBytes,
                            category_name.size() + 1U + raw_id.size()));
  result.append(category_name);
  result.push_back(':');
  const auto remaining = kMaximumAssetDispatchImporterIdBytes - result.size();
  result.append(raw_id.substr(0U, remaining));
  return result;
}

[[nodiscard]] AssetDispatchProbeCandidate
public_candidate(const RankedCandidate &candidate) {
  return AssetDispatchProbeCandidate{
      candidate.category,
      qualified_importer_id(candidate.category, candidate.raw_importer_id),
      candidate.confidence,
      candidate.priority,
  };
}

[[nodiscard]] std::vector<AssetDispatchProbeCandidate>
public_candidates(const std::vector<RankedCandidate> &candidates) {
  std::vector<AssetDispatchProbeCandidate> result;
  result.reserve(candidates.size());
  for (const auto &candidate : candidates) {
    result.push_back(public_candidate(candidate));
  }
  std::ranges::sort(result, {}, &AssetDispatchProbeCandidate::importer_id);
  return result;
}

[[nodiscard]] std::vector<std::string>
candidate_ids(const std::vector<AssetDispatchProbeCandidate> &candidates) {
  std::vector<std::string> result;
  result.reserve(candidates.size());
  for (const auto &candidate : candidates) {
    result.push_back(candidate.importer_id);
  }
  return result;
}

[[nodiscard]] AssetDispatchSelection
no_importer_selection(
    const AssetSource &source,
    std::vector<AssetDispatchProbeCandidate> candidates = {}) {
  return AssetDispatchSelection{
      AssetDispatchSelectionState::importer_not_registered,
      AssetImporterCategory::none,
      {},
      std::move(candidates),
      AssetError{
          AssetErrorCode::UnsupportedFormat,
          source.virtual_path(),
          {},
          "No importer in an allowed category recognized the asset source",
          {},
      },
  };
}

[[nodiscard]] AssetDispatchSelection
ambiguous_selection(
    const AssetSource &source,
    std::vector<AssetDispatchProbeCandidate> candidates) {
  auto ids = candidate_ids(candidates);
  return AssetDispatchSelection{
      AssetDispatchSelectionState::ambiguous_importer,
      AssetImporterCategory::none,
      {},
      std::move(candidates),
      AssetError{
          AssetErrorCode::AmbiguousFormat,
          source.virtual_path(),
          {},
          "Multiple allowed importers matched with identical confidence and "
          "priority",
          std::move(ids),
      },
  };
}

[[nodiscard]] constexpr AssetDispatchState dispatch_state_for_selection(
    const AssetDispatchSelectionState state) noexcept {
  switch (state) {
  case AssetDispatchSelectionState::selected:
    return AssetDispatchState::source_invalid;
  case AssetDispatchSelectionState::importer_not_registered:
    return AssetDispatchState::importer_not_registered;
  case AssetDispatchSelectionState::ambiguous_importer:
    return AssetDispatchState::ambiguous_importer;
  case AssetDispatchSelectionState::unsupported_asset_role:
    return AssetDispatchState::unsupported_asset_role;
  case AssetDispatchSelectionState::metadata_only_resource:
    return AssetDispatchState::metadata_only_resource;
  case AssetDispatchSelectionState::source_invalid:
    return AssetDispatchState::source_invalid;
  }
  return AssetDispatchState::source_invalid;
}

[[nodiscard]] std::optional<std::string_view> raw_importer_id(
    const AssetDispatchSelection &selection) noexcept {
  const auto category = to_string(selection.selected_category);
  if (category == "none" ||
      selection.selected_importer_id.size() <= category.size() ||
      !selection.selected_importer_id.starts_with(category) ||
      selection.selected_importer_id[category.size()] != ':') {
    return std::nullopt;
  }
  return std::string_view{selection.selected_importer_id}.substr(
      category.size() + 1U);
}

template <class Asset>
[[nodiscard]] AssetDispatchResult
imported_result(AssetResult<Asset> imported,
                const AssetImporterCategory category,
                const std::string_view raw_importer_id,
                std::vector<AssetDispatchProbeCandidate> candidates) {
  const auto qualified_id = qualified_importer_id(category, raw_importer_id);
  if (imported) {
    std::optional<ImportedAsset> asset;
    asset.emplace(std::in_place_type<Asset>, std::move(imported).value());
    return AssetDispatchResult{
        AssetDispatchState::imported, std::move(asset), category, qualified_id,
        std::move(candidates),        std::nullopt,
    };
  }

  auto error = std::move(imported).error();
  error.importer_id = qualified_id;
  for (auto &candidate_id : error.candidate_importer_ids) {
    candidate_id = qualified_importer_id(category, candidate_id);
  }
  return AssetDispatchResult{
      AssetDispatchState::import_failed,
      std::nullopt,
      category,
      qualified_id,
      std::move(candidates),
      std::move(error),
  };
}

void append_candidates(std::vector<RankedCandidate> &destination,
                       const AssetImporterProbeResult &probe,
                       const AssetImporterCategory category) {
  for (const auto &candidate : probe.top_candidates) {
    destination.push_back(RankedCandidate{
        category,
        candidate.importer_id,
        candidate.confidence,
        candidate.priority,
    });
  }
}

[[nodiscard]] std::vector<RankedCandidate>
global_best_candidates(const std::vector<RankedCandidate> &candidates) {
  std::vector<RankedCandidate> result;
  AssetProbeConfidence best_confidence = kAssetProbeNoMatch;
  int best_priority = 0;
  for (const auto &candidate : candidates) {
    if (result.empty() || candidate.confidence > best_confidence ||
        (candidate.confidence == best_confidence &&
         candidate.priority > best_priority)) {
      best_confidence = candidate.confidence;
      best_priority = candidate.priority;
      result.assign(1U, candidate);
    } else if (candidate.confidence == best_confidence &&
               candidate.priority == best_priority) {
      result.push_back(candidate);
    }
  }
  return result;
}

} // namespace

AssetImporterDispatcher::AssetImporterDispatcher(
    const AssetImporterRegistries &registries) noexcept
    : registries_{registries} {}

AssetDispatchSelection
AssetImporterDispatcher::select(const AssetSource &source,
                                const AssetDispatchRole role) const {
  if (role == AssetDispatchRole::metadata_only) {
    return AssetDispatchSelection{
        AssetDispatchSelectionState::metadata_only_resource,
        AssetImporterCategory::none,
        {},
        {},
        std::nullopt,
    };
  }
  if (role == AssetDispatchRole::unsupported) {
    return AssetDispatchSelection{
        AssetDispatchSelectionState::unsupported_asset_role,
        AssetImporterCategory::none,
        {},
        {},
        AssetError{
            AssetErrorCode::UnsupportedFormat,
            source.virtual_path(),
            {},
            "The approved asset role has no importer mapping",
            {},
        },
    };
  }
  if (!source_metadata_valid(source)) {
    return AssetDispatchSelection{
        AssetDispatchSelectionState::source_invalid,
        AssetImporterCategory::none,
        {},
        {},
        AssetError{
            AssetErrorCode::ImportFailed,
            source.virtual_path(),
            {},
            "Asset source content size does not match its owned bytes",
            {},
        },
    };
  }

  if (role == AssetDispatchRole::world) {
    const auto probe = registries_.worlds.probe(source);
    std::vector<RankedCandidate> ranked;
    append_candidates(ranked, probe, AssetImporterCategory::world);
    auto candidates = public_candidates(ranked);
    if (probe.state == AssetImporterProbeState::no_match) {
      return no_importer_selection(source, std::move(candidates));
    }
    if (probe.state == AssetImporterProbeState::ambiguous) {
      return ambiguous_selection(source, std::move(candidates));
    }
    const auto &selected = probe.top_candidates.front();
    return AssetDispatchSelection{
        AssetDispatchSelectionState::selected,
        AssetImporterCategory::world,
        qualified_importer_id(
            AssetImporterCategory::world, selected.importer_id),
        std::move(candidates),
        std::nullopt,
    };
  }

  if (role == AssetDispatchRole::audio) {
    const auto probe = registries_.audio.probe(source);
    std::vector<RankedCandidate> ranked;
    append_candidates(ranked, probe, AssetImporterCategory::audio);
    auto candidates = public_candidates(ranked);
    if (probe.state == AssetImporterProbeState::no_match) {
      return no_importer_selection(source, std::move(candidates));
    }
    if (probe.state == AssetImporterProbeState::ambiguous) {
      return ambiguous_selection(source, std::move(candidates));
    }
    const auto &selected = probe.top_candidates.front();
    return AssetDispatchSelection{
        AssetDispatchSelectionState::selected,
        AssetImporterCategory::audio,
        qualified_importer_id(
            AssetImporterCategory::audio, selected.importer_id),
        std::move(candidates),
        std::nullopt,
    };
  }

  if (role != AssetDispatchRole::model_or_sprite) {
    return AssetDispatchSelection{
        AssetDispatchSelectionState::source_invalid,
        AssetImporterCategory::none,
        {},
        {},
        AssetError{
            AssetErrorCode::ImportFailed,
            source.virtual_path(),
            {},
            "Asset dispatch role is invalid",
            {},
        },
    };
  }

  const auto model_probe = registries_.models.probe(source);
  const auto sprite_probe = registries_.sprites.probe(source);
  std::vector<RankedCandidate> ranked;
  append_candidates(ranked, model_probe, AssetImporterCategory::model);
  append_candidates(ranked, sprite_probe, AssetImporterCategory::sprite);
  auto best = global_best_candidates(ranked);
  auto candidates = public_candidates(best);
  if (best.empty()) {
    return no_importer_selection(source, std::move(candidates));
  }
  if (best.size() > 1U) {
    return ambiguous_selection(source, std::move(candidates));
  }

  const auto &selected = best.front();
  return AssetDispatchSelection{
      AssetDispatchSelectionState::selected,
      selected.category,
      qualified_importer_id(selected.category, selected.raw_importer_id),
      std::move(candidates),
      std::nullopt,
  };
}

AssetDispatchResult AssetImporterDispatcher::import_selected(
    const AssetSource &source,
    AssetDispatchSelection selection) const {
  if (!selection.selected()) {
    return AssetDispatchResult{
        dispatch_state_for_selection(selection.state),
        std::nullopt,
        selection.selected_category,
        std::move(selection.selected_importer_id),
        std::move(selection.top_candidates),
        std::move(selection.error),
    };
  }

  const auto raw_id = raw_importer_id(selection);
  if (!raw_id) {
    return AssetDispatchResult{
        AssetDispatchState::source_invalid,
        std::nullopt,
        AssetImporterCategory::none,
        {},
        std::move(selection.top_candidates),
        AssetError{
            AssetErrorCode::ImportFailed,
            source.virtual_path(),
            {},
            "Asset dispatch selection token is invalid",
            {},
        },
    };
  }

  switch (selection.selected_category) {
  case AssetImporterCategory::model:
    return imported_result(
        registries_.models.import_selected(source, *raw_id),
        selection.selected_category, *raw_id,
        std::move(selection.top_candidates));
  case AssetImporterCategory::world:
    return imported_result(
        registries_.worlds.import_selected(source, *raw_id),
        selection.selected_category, *raw_id,
        std::move(selection.top_candidates));
  case AssetImporterCategory::sprite:
    return imported_result(
        registries_.sprites.import_selected(source, *raw_id),
        selection.selected_category, *raw_id,
        std::move(selection.top_candidates));
  case AssetImporterCategory::audio:
    return imported_result(
        registries_.audio.import_selected(source, *raw_id),
        selection.selected_category, *raw_id,
        std::move(selection.top_candidates));
  case AssetImporterCategory::none:
  case AssetImporterCategory::image:
    break;
  }

  return AssetDispatchResult{
      AssetDispatchState::source_invalid,
      std::nullopt,
      AssetImporterCategory::none,
      {},
      std::move(selection.top_candidates),
      AssetError{
          AssetErrorCode::ImportFailed,
          source.virtual_path(),
          {},
          "Selected asset importer category is invalid",
          {},
      },
  };
}

AssetDispatchResult AssetImporterDispatcher::import_selected_model_with(
    const AssetSource &source,
    AssetDispatchSelection selection,
    const SelectedModelImporterFunction &import_function) const {
  if (!selection.selected()) {
    return import_selected(source, std::move(selection));
  }

  const auto raw_id = raw_importer_id(selection);
  if (selection.selected_category != AssetImporterCategory::model ||
      !raw_id || !import_function) {
    return AssetDispatchResult{
        AssetDispatchState::source_invalid,
        std::nullopt,
        AssetImporterCategory::none,
        {},
        std::move(selection.top_candidates),
        AssetError{
            AssetErrorCode::ImportFailed,
            source.virtual_path(),
            {},
            "Selected model-import callback or selection token is invalid",
            {},
        },
    };
  }

  return imported_result(
      registries_.models.import_selected_with(
          source, *raw_id, import_function),
      selection.selected_category, *raw_id,
      std::move(selection.top_candidates));
}

AssetDispatchResult
AssetImporterDispatcher::dispatch(const AssetSource &source,
                                  const AssetDispatchRole role) const {
  return import_selected(source, select(source, role));
}

} // namespace hlclient::assets

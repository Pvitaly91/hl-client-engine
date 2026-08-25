#pragma once

#include <hlclient/assets/asset_importer.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::assets {

class AssetImporterDispatcher;

// Importer IDs are retained in registry and dispatch diagnostics. This is a
// project safety bound, not a format or plugin ABI limit.
inline constexpr std::size_t kMaximumAssetImporterIdBytes = 128U;

enum class AssetImporterRegistrationErrorCode {
    NullImporter,
    EmptyImporterId,
    DuplicateImporterId,
    ImporterIdTooLong,
};

struct AssetImporterRegistrationError {
    AssetImporterRegistrationErrorCode code{AssetImporterRegistrationErrorCode::NullImporter};
    std::string importer_id;
    std::string context;
};

struct AssetImporterRegistrationResult {
    bool registered{false};
    std::optional<AssetImporterRegistrationError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return registered;
    }
};

enum class AssetImporterProbeState {
    no_match,
    selected,
    ambiguous,
};

struct AssetImporterProbeCandidate {
    std::string importer_id;
    AssetProbeConfidence confidence{kAssetProbeNoMatch};
    int priority{0};
};

struct AssetImporterProbeResult {
    AssetImporterProbeState state{AssetImporterProbeState::no_match};
    AssetProbeConfidence best_confidence{kAssetProbeNoMatch};
    int best_priority{0};
    std::vector<AssetImporterProbeCandidate> top_candidates;

    [[nodiscard]] bool selected() const noexcept
    {
        return state == AssetImporterProbeState::selected;
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    const AssetImporterProbeState state) noexcept
{
    switch (state) {
    case AssetImporterProbeState::no_match: return "no_match";
    case AssetImporterProbeState::selected: return "selected";
    case AssetImporterProbeState::ambiguous: return "ambiguous";
    }
    return "unknown";
}

template<class Asset>
class AssetImporterRegistry final {
public:
    AssetImporterRegistry() = default;

    AssetImporterRegistry(const AssetImporterRegistry&) = delete;
    AssetImporterRegistry& operator=(const AssetImporterRegistry&) = delete;
    AssetImporterRegistry(AssetImporterRegistry&&) noexcept = default;
    AssetImporterRegistry& operator=(AssetImporterRegistry&&) noexcept = default;

    [[nodiscard]] AssetImporterRegistrationResult register_importer(
        std::unique_ptr<IAssetImporter<Asset>> importer,
        const int priority = 0)
    {
        if (!importer) {
            return AssetImporterRegistrationResult{
                false,
                AssetImporterRegistrationError{
                    AssetImporterRegistrationErrorCode::NullImporter,
                    {},
                    "Cannot register a null asset importer",
                },
            };
        }

        const auto importer_id_view = importer->id();
        if (importer_id_view.empty()) {
            return AssetImporterRegistrationResult{
                false,
                AssetImporterRegistrationError{
                    AssetImporterRegistrationErrorCode::EmptyImporterId,
                    {},
                    "Asset importer IDs must be non-empty and stable",
                },
            };
        }
        if (importer_id_view.size() > kMaximumAssetImporterIdBytes) {
            return AssetImporterRegistrationResult{
                false,
                AssetImporterRegistrationError{
                    AssetImporterRegistrationErrorCode::ImporterIdTooLong,
                    {},
                    "Asset importer ID exceeds the diagnostic byte limit",
                },
            };
        }

        const std::string importer_id{importer_id_view};

        const auto duplicate = std::ranges::find_if(entries_, [&importer_id](const Entry& entry) {
            return entry.id == importer_id;
        });
        if (duplicate != entries_.end()) {
            return AssetImporterRegistrationResult{
                false,
                AssetImporterRegistrationError{
                    AssetImporterRegistrationErrorCode::DuplicateImporterId,
                    importer_id,
                    "An asset importer with this ID is already registered",
                },
            };
        }

        entries_.push_back(Entry{importer_id, priority, std::move(importer)});
        return AssetImporterRegistrationResult{true, std::nullopt};
    }

    // Pure selection metadata. Every registered importer is probed exactly
    // once; import() is never called and no importer pointer is exposed.
    [[nodiscard]] AssetImporterProbeResult probe(
        const AssetSource& source) const
    {
        return select(source).result;
    }

    [[nodiscard]] AssetResult<Asset> import(const AssetSource& source) const
    {
        const auto selection = select(source);
        if (selection.result.state == AssetImporterProbeState::no_match) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::UnsupportedFormat,
                source.virtual_path(),
                {},
                "No registered importer recognized the asset source",
                {},
            });
        }

        if (selection.result.state == AssetImporterProbeState::ambiguous) {
            std::vector<std::string> candidate_ids;
            candidate_ids.reserve(selection.result.top_candidates.size());
            for (const auto& candidate : selection.result.top_candidates) {
                candidate_ids.push_back(candidate.importer_id);
            }
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::AmbiguousFormat,
                source.virtual_path(),
                {},
                "Multiple importers matched with identical confidence and priority",
                std::move(candidate_ids),
            });
        }

        return invoke_selected(source, *selection.selected_entry);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return entries_.size();
    }

private:
    friend class AssetImporterDispatcher;

    struct Entry {
        std::string id;
        int priority{0};
        std::unique_ptr<IAssetImporter<Asset>> importer;
    };

    struct Selection {
        AssetImporterProbeResult result;
        const Entry* selected_entry{nullptr};
    };

    [[nodiscard]] Selection select(const AssetSource& source) const
    {
        const auto source_probe = make_asset_probe(source);
        AssetProbeConfidence best_confidence = kAssetProbeNoMatch;
        int best_priority = std::numeric_limits<int>::min();
        std::vector<const Entry*> best_entries;

        for (const auto& entry : entries_) {
            const auto confidence = entry.importer->probe(source_probe);
            if (confidence == kAssetProbeNoMatch) {
                continue;
            }

            if (confidence > best_confidence ||
                (confidence == best_confidence &&
                 entry.priority > best_priority)) {
                best_confidence = confidence;
                best_priority = entry.priority;
                best_entries.assign(1U, &entry);
            } else if (confidence == best_confidence &&
                       entry.priority == best_priority) {
                best_entries.push_back(&entry);
            }
        }

        AssetImporterProbeResult result;
        if (best_entries.empty()) {
            return Selection{std::move(result), nullptr};
        }

        result.best_confidence = best_confidence;
        result.best_priority = best_priority;
        result.top_candidates.reserve(best_entries.size());
        for (const auto* entry : best_entries) {
            result.top_candidates.push_back(AssetImporterProbeCandidate{
                entry->id,
                best_confidence,
                best_priority,
            });
        }
        std::ranges::sort(
            result.top_candidates,
            {},
            &AssetImporterProbeCandidate::importer_id);

        if (best_entries.size() == 1U) {
            result.state = AssetImporterProbeState::selected;
            return Selection{std::move(result), best_entries.front()};
        }

        result.state = AssetImporterProbeState::ambiguous;
        return Selection{std::move(result), nullptr};
    }

    [[nodiscard]] AssetResult<Asset> import_selected(
        const AssetSource& source,
        const std::string_view importer_id) const
    {
        const auto selected = std::ranges::find_if(
            entries_,
            [importer_id](const Entry& entry) { return entry.id == importer_id; });
        if (selected == entries_.end()) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::UnsupportedFormat,
                source.virtual_path(),
                {},
                "The selected asset importer is no longer registered",
                {},
            });
        }
        return invoke_selected(source, *selected);
    }

    template<class Invoker>
    [[nodiscard]] AssetResult<Asset> import_selected_with(
        const AssetSource& source,
        const std::string_view importer_id,
        Invoker&& invoker) const
    {
        const auto selected = std::ranges::find_if(
            entries_,
            [importer_id](const Entry& entry) { return entry.id == importer_id; });
        if (selected == entries_.end()) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::UnsupportedFormat,
                source.virtual_path(),
                {},
                "The selected asset importer is no longer registered",
                {},
            });
        }

        try {
            auto result = std::forward<Invoker>(invoker)(
                static_cast<const IAssetImporter<Asset>&>(*selected->importer),
                source);
            if (result) {
                return result;
            }

            auto error = std::move(result).error();
            error.virtual_path = source.virtual_path();
            error.importer_id = selected->id;
            return AssetResult<Asset>::failure(std::move(error));
        } catch (const std::exception& exception) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::ImportFailed,
                source.virtual_path(),
                selected->id,
                std::string{"Importer failed with an exception: "} + exception.what(),
                {},
            });
        } catch (...) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::ImportFailed,
                source.virtual_path(),
                selected->id,
                "Importer failed with an unknown exception",
                {},
            });
        }
    }

    [[nodiscard]] static AssetResult<Asset> invoke_selected(
        const AssetSource& source,
        const Entry& selected)
    {
        try {
            auto result = selected.importer->import(source);
            if (result) {
                return result;
            }

            auto error = std::move(result).error();
            error.virtual_path = source.virtual_path();
            error.importer_id = selected.id;
            return AssetResult<Asset>::failure(std::move(error));
        } catch (const std::exception& exception) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::ImportFailed,
                source.virtual_path(),
                selected.id,
                std::string{"Importer failed with an exception: "} + exception.what(),
                {},
            });
        } catch (...) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::ImportFailed,
                source.virtual_path(),
                selected.id,
                "Importer failed with an unknown exception",
                {},
            });
        }
    }

    std::vector<Entry> entries_;
};

using ModelImporterRegistry = AssetImporterRegistry<ModelAsset>;
using WorldImporterRegistry = AssetImporterRegistry<WorldAsset>;
using SpriteImporterRegistry = AssetImporterRegistry<SpriteAsset>;
using ImageImporterRegistry = AssetImporterRegistry<ImageAsset>;
using AudioImporterRegistry = AssetImporterRegistry<AudioAsset>;

using ModelAssetImporterRegistry = ModelImporterRegistry;
using WorldAssetImporterRegistry = WorldImporterRegistry;
using SpriteAssetImporterRegistry = SpriteImporterRegistry;
using ImageAssetImporterRegistry = ImageImporterRegistry;
using AudioAssetImporterRegistry = AudioImporterRegistry;

struct AssetImporterRegistries {
    ModelImporterRegistry models;
    WorldImporterRegistry worlds;
    SpriteImporterRegistry sprites;
    ImageImporterRegistry images;
    AudioImporterRegistry audio;
};

} // namespace hlclient::assets

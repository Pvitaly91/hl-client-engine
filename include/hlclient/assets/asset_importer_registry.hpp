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

enum class AssetImporterRegistrationErrorCode {
    NullImporter,
    EmptyImporterId,
    DuplicateImporterId,
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

        const std::string importer_id{importer->id()};
        if (importer_id.empty()) {
            return AssetImporterRegistrationResult{
                false,
                AssetImporterRegistrationError{
                    AssetImporterRegistrationErrorCode::EmptyImporterId,
                    {},
                    "Asset importer IDs must be non-empty and stable",
                },
            };
        }

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

    [[nodiscard]] AssetResult<Asset> import(const AssetSource& source) const
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
                (confidence == best_confidence && entry.priority > best_priority)) {
                best_confidence = confidence;
                best_priority = entry.priority;
                best_entries.assign(1U, &entry);
            } else if (confidence == best_confidence && entry.priority == best_priority) {
                best_entries.push_back(&entry);
            }
        }

        if (best_entries.empty()) {
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::UnsupportedFormat,
                source.virtual_path(),
                {},
                "No registered importer recognized the asset source",
                {},
            });
        }

        if (best_entries.size() > 1U) {
            std::vector<std::string> candidate_ids;
            candidate_ids.reserve(best_entries.size());
            for (const auto* entry : best_entries) {
                candidate_ids.push_back(entry->id);
            }
            std::ranges::sort(candidate_ids);
            return AssetResult<Asset>::failure(AssetError{
                AssetErrorCode::AmbiguousFormat,
                source.virtual_path(),
                {},
                "Multiple importers matched with identical confidence and priority",
                std::move(candidate_ids),
            });
        }

        const auto& selected = *best_entries.front();
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

    [[nodiscard]] std::size_t size() const noexcept
    {
        return entries_.size();
    }

private:
    struct Entry {
        std::string id;
        int priority{0};
        std::unique_ptr<IAssetImporter<Asset>> importer;
    };

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

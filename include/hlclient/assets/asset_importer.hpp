#pragma once

#include <hlclient/assets/asset_source.hpp>
#include <hlclient/assets/asset_types.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hlclient::assets {

using AssetProbeConfidence = std::uint16_t;
inline constexpr AssetProbeConfidence kAssetProbeNoMatch = 0U;

struct AssetProbe {
    const std::filesystem::path& virtual_path;
    std::string_view extension_hint;
    std::span<const std::byte> signature;
    std::optional<std::uint32_t> version_hint;
    std::span<const std::byte> structural_bytes;
};

[[nodiscard]] inline AssetProbe make_asset_probe(const AssetSource& source) noexcept
{
    return AssetProbe{
        source.virtual_path(),
        source.extension_hint(),
        source.signature(),
        source.version_hint(),
        source.bytes(),
    };
}

enum class AssetErrorCode {
    InvalidVirtualPath,
    SourceReadFailed,
    UnsupportedFormat,
    AmbiguousFormat,
    ExternalDependencyRequired,
    MalformedData,
    ImportFailed,
};

struct AssetError {
    AssetErrorCode code{AssetErrorCode::ImportFailed};
    std::filesystem::path virtual_path;
    std::string importer_id;
    std::string context;
    std::vector<std::string> candidate_importer_ids;
};

template<class Asset>
class AssetResult final {
public:
    [[nodiscard]] static AssetResult success(Asset asset)
    {
        return AssetResult{std::in_place_index<0>, std::move(asset)};
    }

    [[nodiscard]] static AssetResult failure(AssetError error)
    {
        return AssetResult{std::in_place_index<1>, std::move(error)};
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return std::holds_alternative<Asset>(storage_);
    }

    [[nodiscard]] Asset& value() &
    {
        return std::get<Asset>(storage_);
    }

    [[nodiscard]] const Asset& value() const&
    {
        return std::get<Asset>(storage_);
    }

    [[nodiscard]] Asset&& value() &&
    {
        return std::get<Asset>(std::move(storage_));
    }

    [[nodiscard]] AssetError& error() &
    {
        return std::get<AssetError>(storage_);
    }

    [[nodiscard]] const AssetError& error() const&
    {
        return std::get<AssetError>(storage_);
    }

    [[nodiscard]] AssetError&& error() &&
    {
        return std::get<AssetError>(std::move(storage_));
    }

private:
    template<std::size_t Index, class Value>
    explicit AssetResult(std::in_place_index_t<Index> index, Value&& value)
        : storage_{index, std::forward<Value>(value)}
    {
    }

    std::variant<Asset, AssetError> storage_;
};

template<class Asset>
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    IAssetImporter(const IAssetImporter&) = delete;
    IAssetImporter& operator=(const IAssetImporter&) = delete;
    IAssetImporter(IAssetImporter&&) = delete;
    IAssetImporter& operator=(IAssetImporter&&) = delete;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual AssetProbeConfidence probe(const AssetProbe& probe) const noexcept = 0;
    [[nodiscard]] virtual AssetResult<Asset> import(const AssetSource& source) const = 0;

protected:
    IAssetImporter() = default;
};

using IModelImporter = IAssetImporter<ModelAsset>;
using IWorldImporter = IAssetImporter<WorldAsset>;
using ISpriteImporter = IAssetImporter<SpriteAsset>;
using IImageImporter = IAssetImporter<ImageAsset>;
using IAudioImporter = IAssetImporter<AudioAsset>;

using IModelAssetImporter = IModelImporter;
using IWorldAssetImporter = IWorldImporter;
using ISpriteAssetImporter = ISpriteImporter;
using IImageAssetImporter = IImageImporter;
using IAudioAssetImporter = IAudioImporter;

using ModelAssetResult = AssetResult<ModelAsset>;
using WorldAssetResult = AssetResult<WorldAsset>;
using SpriteAssetResult = AssetResult<SpriteAsset>;
using ImageAssetResult = AssetResult<ImageAsset>;
using AudioAssetResult = AssetResult<AudioAsset>;

} // namespace hlclient::assets

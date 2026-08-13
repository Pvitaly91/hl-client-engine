#include <hlclient/assets/asset_source.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace hlclient::assets {
namespace {

[[nodiscard]] bool contains_nul(const std::filesystem::path& path) noexcept
{
    const auto& native = path.native();
    return std::find(native.begin(), native.end(), std::filesystem::path::value_type{}) !=
           native.end();
}

[[nodiscard]] bool is_safe_virtual_path(const std::filesystem::path& path) noexcept
{
    if (path.empty() || path.is_absolute() || path.has_root_path() || contains_nul(path)) {
        return false;
    }

    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string path_component_as_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

[[nodiscard]] std::string normalize_extension(std::string extension)
{
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

} // namespace

AssetSourceCreateResult AssetSource::create(
    std::filesystem::path virtual_path,
    std::vector<std::byte> bytes,
    std::optional<AssetSourceMetadata> metadata)
{
    if (!is_safe_virtual_path(virtual_path)) {
        return AssetSourceCreateResult{
            std::nullopt,
            AssetSourceError{
                AssetSourceErrorCode::InvalidVirtualPath,
                std::move(virtual_path),
                "Asset virtual path must be relative and must not contain '..' or NUL bytes",
            },
        };
    }

    virtual_path = virtual_path.lexically_normal();
    if (virtual_path.empty() || virtual_path == ".") {
        return AssetSourceCreateResult{
            std::nullopt,
            AssetSourceError{
                AssetSourceErrorCode::InvalidVirtualPath,
                std::move(virtual_path),
                "Asset virtual path must identify a file",
            },
        };
    }

    std::string extension;
    if (metadata && metadata->extension_hint) {
        extension = normalize_extension(*metadata->extension_hint);
        metadata->extension_hint = extension;
    } else {
        extension = normalize_extension(path_component_as_utf8(virtual_path.extension()));
    }

    return AssetSourceCreateResult{
        AssetSource{
            std::move(virtual_path),
            std::move(bytes),
            std::move(metadata),
            std::move(extension),
        },
        std::nullopt,
    };
}

AssetSource::AssetSource(
    std::filesystem::path virtual_path,
    std::vector<std::byte> bytes,
    std::optional<AssetSourceMetadata> metadata,
    std::string extension_hint)
    : virtual_path_{std::move(virtual_path)},
      bytes_{std::move(bytes)},
      metadata_{std::move(metadata)},
      extension_hint_{std::move(extension_hint)}
{
}

const std::filesystem::path& AssetSource::virtual_path() const noexcept
{
    return virtual_path_;
}

std::span<const std::byte> AssetSource::bytes() const noexcept
{
    return bytes_;
}

std::span<const std::byte> AssetSource::signature() const noexcept
{
    return std::span<const std::byte>{bytes_}.first(
        std::min(bytes_.size(), kAssetProbeSignatureSize));
}

std::string_view AssetSource::extension_hint() const noexcept
{
    return extension_hint_;
}

std::optional<std::uint32_t> AssetSource::version_hint() const noexcept
{
    if (!metadata_) {
        return std::nullopt;
    }
    return metadata_->version_hint;
}

const std::optional<AssetSourceMetadata>& AssetSource::metadata() const noexcept
{
    return metadata_;
}

} // namespace hlclient::assets

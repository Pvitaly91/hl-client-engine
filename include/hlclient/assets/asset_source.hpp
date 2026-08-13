#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::assets {

inline constexpr std::size_t kAssetProbeSignatureSize = 16U;

struct AssetSourceMetadata {
    std::optional<std::uintmax_t> content_size;
    std::optional<std::filesystem::file_time_type> last_modified;
    std::optional<std::string> extension_hint;
    std::optional<std::uint32_t> version_hint;
};

enum class AssetSourceErrorCode {
    InvalidVirtualPath,
};

struct AssetSourceError {
    AssetSourceErrorCode code{AssetSourceErrorCode::InvalidVirtualPath};
    std::filesystem::path virtual_path;
    std::string context;
};

struct AssetSourceCreateResult;

class AssetSource final {
public:
    [[nodiscard]] static AssetSourceCreateResult create(
        std::filesystem::path virtual_path,
        std::vector<std::byte> bytes,
        std::optional<AssetSourceMetadata> metadata = std::nullopt);

    [[nodiscard]] const std::filesystem::path& virtual_path() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::span<const std::byte> signature() const noexcept;
    [[nodiscard]] std::string_view extension_hint() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> version_hint() const noexcept;
    [[nodiscard]] const std::optional<AssetSourceMetadata>& metadata() const noexcept;

private:
    AssetSource(
        std::filesystem::path virtual_path,
        std::vector<std::byte> bytes,
        std::optional<AssetSourceMetadata> metadata,
        std::string extension_hint);

    std::filesystem::path virtual_path_;
    std::vector<std::byte> bytes_;
    std::optional<AssetSourceMetadata> metadata_;
    std::string extension_hint_;
};

struct AssetSourceCreateResult {
    std::optional<AssetSource> source;
    std::optional<AssetSourceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return source.has_value();
    }
};

} // namespace hlclient::assets

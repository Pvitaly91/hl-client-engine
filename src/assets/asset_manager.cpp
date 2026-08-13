#include <hlclient/assets/asset_manager.hpp>

#include <optional>
#include <string>
#include <utility>

namespace hlclient::assets {
namespace {

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

[[nodiscard]] AssetError file_read_error(
    const std::filesystem::path& requested_path,
    const filesystem::FileReadResult& result)
{
    if (!result.error) {
        return AssetError{
            AssetErrorCode::SourceReadFailed,
            requested_path,
            {},
            "The filesystem returned neither file contents nor an error",
            {},
        };
    }

    const auto& read_error = *result.error;
    const auto code = read_error.code == filesystem::FileReadErrorCode::InvalidVirtualPath
                          ? AssetErrorCode::InvalidVirtualPath
                          : AssetErrorCode::SourceReadFailed;
    return AssetError{
        code,
        read_error.virtual_path.empty() ? requested_path : read_error.virtual_path,
        {},
        read_error.context,
        {},
    };
}

template<class Asset>
[[nodiscard]] AssetResult<Asset> load_asset(
    const filesystem::IFileSystem& file_system,
    const AssetImporterRegistry<Asset>& importers,
    const std::filesystem::path& virtual_path)
{
    auto file_result = file_system.read_file(virtual_path);
    if (!file_result) {
        return AssetResult<Asset>::failure(file_read_error(virtual_path, file_result));
    }

    auto file = std::move(*file_result.file);
    std::optional<AssetSourceMetadata> source_metadata;
    if (file.metadata) {
        source_metadata = AssetSourceMetadata{
            file.metadata->size,
            file.metadata->last_modified,
            std::nullopt,
            std::nullopt,
        };
    }

    auto source_result = AssetSource::create(
        std::move(file.virtual_path),
        std::move(file.bytes),
        std::move(source_metadata));
    if (!source_result) {
        return AssetResult<Asset>::failure(AssetError{
            AssetErrorCode::InvalidVirtualPath,
            source_result.error->virtual_path,
            {},
            source_result.error->context,
            {},
        });
    }

    auto result = importers.import(*source_result.source);
    if (result && result.value().identity.source_name.empty()) {
        result.value().identity.source_name = path_as_utf8(source_result.source->virtual_path());
    }
    return result;
}

} // namespace

AssetManager::AssetManager(
    const filesystem::IFileSystem& file_system,
    const AssetImporterRegistries& importers) noexcept
    : file_system_{file_system}, importers_{importers}
{
}

ModelAssetResult AssetManager::load_model(const std::filesystem::path& virtual_path) const
{
    return load_asset(file_system_, importers_.models, virtual_path);
}

WorldAssetResult AssetManager::load_world(const std::filesystem::path& virtual_path) const
{
    return load_asset(file_system_, importers_.worlds, virtual_path);
}

SpriteAssetResult AssetManager::load_sprite(const std::filesystem::path& virtual_path) const
{
    return load_asset(file_system_, importers_.sprites, virtual_path);
}

ImageAssetResult AssetManager::load_image(const std::filesystem::path& virtual_path) const
{
    return load_asset(file_system_, importers_.images, virtual_path);
}

AudioAssetResult AssetManager::load_audio(const std::filesystem::path& virtual_path) const
{
    return load_asset(file_system_, importers_.audio, virtual_path);
}

} // namespace hlclient::assets

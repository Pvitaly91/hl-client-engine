#pragma once

#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/filesystem/file_system.hpp>

#include <filesystem>

namespace hlclient::assets {

class AssetManager final {
public:
    AssetManager(
        const filesystem::IFileSystem& file_system,
        const AssetImporterRegistries& importers) noexcept;

    [[nodiscard]] ModelAssetResult load_model(
        const std::filesystem::path& virtual_path) const;
    [[nodiscard]] WorldAssetResult load_world(
        const std::filesystem::path& virtual_path) const;
    [[nodiscard]] SpriteAssetResult load_sprite(
        const std::filesystem::path& virtual_path) const;
    [[nodiscard]] ImageAssetResult load_image(
        const std::filesystem::path& virtual_path) const;
    [[nodiscard]] AudioAssetResult load_audio(
        const std::filesystem::path& virtual_path) const;

private:
    const filesystem::IFileSystem& file_system_;
    const AssetImporterRegistries& importers_;
};

} // namespace hlclient::assets

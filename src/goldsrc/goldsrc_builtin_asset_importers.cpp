#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace hlclient::goldsrc {

assets::AssetImporterRegistrationResult register_builtin_asset_importers(
    assets::AssetImporterRegistries& registries,
    bsp::GoldSrcBspImportLimits bsp_limits)
{
    try {
        auto world = registries.worlds.register_importer(
            std::make_unique<bsp::GoldSrcBspWorldImporter>(
                std::move(bsp_limits)),
            bsp::kGoldSrcBspWorldImporterPriority);
        if (!world) {
            return world;
        }
        auto model = registries.models.register_importer(
            std::make_unique<studio::GoldSrcStudioModelImporter>(),
            studio::kGoldSrcStudioModelImporterPriority);
        if (!model) {
            return model;
        }
        return registries.sprites.register_importer(
            std::make_unique<sprite::GoldSrcSpriteImporter>(),
            sprite::kGoldSrcSpriteImporterPriority);
    } catch (const std::exception& exception) {
        return assets::AssetImporterRegistrationResult{
            false,
            assets::AssetImporterRegistrationError{
                assets::AssetImporterRegistrationErrorCode::NullImporter,
                {},
                std::string{"Unable to construct a built-in asset importer: "} +
                    exception.what(),
            },
        };
    } catch (...) {
        return assets::AssetImporterRegistrationResult{
            false,
            assets::AssetImporterRegistrationError{
                assets::AssetImporterRegistrationErrorCode::NullImporter,
                {},
                "Unable to construct a built-in asset importer",
            },
        };
    }
}

} // namespace hlclient::goldsrc

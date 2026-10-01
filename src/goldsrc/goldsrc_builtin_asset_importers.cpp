#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/assets/wav_importer.hpp>
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
    bsp::GoldSrcBspImportLimits bsp_limits,
    bsp::GoldSrcBspParseOptions bsp_options)
{
    try {
        auto audio = registries.audio.register_importer(std::make_unique<assets::WavImporter>(),100);
        if (!audio) return audio;
        auto world = registries.worlds.register_importer(
            std::make_unique<bsp::GoldSrcBspWorldImporter>(
                std::move(bsp_limits), bsp_options),
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

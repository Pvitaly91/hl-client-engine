#pragma once

#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

namespace hlclient::goldsrc {

// Canonical production composition: registers the BSP world, Studio model,
// and sprite importers in their caller-owned registries. No global registry or
// importer state is retained.
[[nodiscard]] assets::AssetImporterRegistrationResult register_builtin_asset_importers(
    assets::AssetImporterRegistries& registries,
    bsp::GoldSrcBspImportLimits bsp_limits = {});

} // namespace hlclient::goldsrc

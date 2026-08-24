#pragma once

#include <hlclient/assets/asset_importer.hpp>
#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include <string_view>

namespace hlclient::goldsrc::bsp {

inline constexpr std::string_view kGoldSrcBspWorldImporterId = "goldsrc-bsp-v30";
inline constexpr int kGoldSrcBspWorldImporterPriority = 300;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspVersionProbeConfidence = 100U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspHeaderProbeConfidence = 200U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspDirectoryProbeConfidence = 300U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspGeometryProbeConfidence = 400U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspExtensionHintBoost = 1U;

class GoldSrcBspWorldImporter final : public assets::IWorldImporter {
public:
    explicit GoldSrcBspWorldImporter(GoldSrcBspImportLimits limits = {});

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override;
    [[nodiscard]] assets::WorldAssetResult import(
        const assets::AssetSource& source) const override;

private:
    GoldSrcBspImportLimits limits_;
};

// Registers the one production BSP v30 world importer in the caller-owned
// registries. No global registry or importer state is retained.
[[nodiscard]] assets::AssetImporterRegistrationResult register_builtin_asset_importers(
    assets::AssetImporterRegistries& registries,
    GoldSrcBspImportLimits limits = {});

} // namespace hlclient::goldsrc::bsp

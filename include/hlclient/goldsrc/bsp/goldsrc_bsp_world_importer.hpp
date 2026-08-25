#pragma once

#include <hlclient/assets/asset_importer.hpp>
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

} // namespace hlclient::goldsrc::bsp

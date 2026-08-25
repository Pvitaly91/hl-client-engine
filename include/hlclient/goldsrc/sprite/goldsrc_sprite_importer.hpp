#pragma once

#include <hlclient/assets/asset_importer.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>

#include <string_view>

namespace hlclient::goldsrc::sprite {

inline constexpr std::string_view kGoldSrcSpriteImporterId = "goldsrc-sprite-v2";
inline constexpr int kGoldSrcSpriteImporterPriority = 300;
inline constexpr assets::AssetProbeConfidence
    kGoldSrcSpriteSignatureProbeConfidence = 300U;
inline constexpr assets::AssetProbeConfidence kGoldSrcSpriteHeaderProbeConfidence = 400U;
inline constexpr assets::AssetProbeConfidence kGoldSrcSpriteExtensionHintBoost = 1U;

class GoldSrcSpriteImporter final : public assets::ISpriteImporter {
public:
    explicit GoldSrcSpriteImporter(GoldSrcSpriteImportLimits limits = {});

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override;
    [[nodiscard]] assets::SpriteAssetResult import(
        const assets::AssetSource& source) const override;

private:
    GoldSrcSpriteImportLimits limits_;
};

} // namespace hlclient::goldsrc::sprite

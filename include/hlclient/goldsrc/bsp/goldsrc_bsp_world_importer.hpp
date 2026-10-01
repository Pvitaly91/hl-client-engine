#pragma once

#include <hlclient/assets/asset_importer.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include <memory>
#include <string_view>

namespace hlclient::goldsrc::bsp {

inline constexpr std::string_view kGoldSrcBspWorldImporterId = "goldsrc-bsp-v30";
inline constexpr int kGoldSrcBspWorldImporterPriority = 300;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspVersionProbeConfidence = 100U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspHeaderProbeConfidence = 200U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspDirectoryProbeConfidence = 300U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspGeometryProbeConfidence = 400U;
inline constexpr assets::AssetProbeConfidence kGoldSrcBspExtensionHintBoost = 1U;

// Type-erased by generic dispatch, then recovered only by the GoldSrc
// collision/brush CPU stages. This owning canonical document comes from the
// same parser invocation as WorldAsset; it retains no raw BSP file/native path.
class GoldSrcBspCollisionImportAttachment final
    : public assets::AssetImportAttachment {
public:
    explicit GoldSrcBspCollisionImportAttachment(
        GoldSrcBspParsedDocument document);

    [[nodiscard]] const GoldSrcBspCollisionSource& collision_source()
        const noexcept;
    [[nodiscard]] const GoldSrcBspParsedDocument& document() const noexcept {
        return document_;
    }

private:
    GoldSrcBspParsedDocument document_;
};

class GoldSrcBspWorldImporter final : public assets::IWorldImporter {
public:
    // World-only consumers retain their historical validation boundary.
    // Live scene preparation requests brush materialization explicitly.
    explicit GoldSrcBspWorldImporter(
        GoldSrcBspImportLimits limits = {},
        GoldSrcBspParseOptions options = {false});

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override;
    [[nodiscard]] assets::WorldAssetResult import(
        const assets::AssetSource& source) const override;

private:
    GoldSrcBspImportLimits limits_;
    GoldSrcBspParseOptions options_;
};

} // namespace hlclient::goldsrc::bsp

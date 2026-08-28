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
// collision CPU stage. This state is produced by the same canonical parser
// invocation as WorldAsset and retains neither raw BSP bytes nor native paths.
class GoldSrcBspCollisionImportAttachment final
    : public assets::AssetImportAttachment {
public:
    explicit GoldSrcBspCollisionImportAttachment(
        GoldSrcBspCollisionSource collision_source);

    [[nodiscard]] const GoldSrcBspCollisionSource& collision_source()
        const noexcept;

private:
    GoldSrcBspCollisionSource collision_source_;
};

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

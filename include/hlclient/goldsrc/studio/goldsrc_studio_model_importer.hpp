#pragma once

#include <hlclient/assets/asset_importer.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include <string_view>

namespace hlclient::goldsrc::studio {

inline constexpr std::string_view kGoldSrcStudioModelImporterId =
    "goldsrc-studio-mdl-v10";
inline constexpr int kGoldSrcStudioModelImporterPriority = 300;
inline constexpr assets::AssetProbeConfidence kGoldSrcStudioSignatureProbeConfidence = 300U;
inline constexpr assets::AssetProbeConfidence kGoldSrcStudioHeaderProbeConfidence = 400U;
inline constexpr assets::AssetProbeConfidence kGoldSrcStudioDirectoryProbeConfidence = 500U;
inline constexpr assets::AssetProbeConfidence kGoldSrcStudioExtensionHintBoost = 1U;

// Context-capable contract for the production importer ID. Composition roots
// use this interface to apply per-operation limits to the exact selected
// caller-owned registry instance, without substituting another importer.
class IGoldSrcStudioModelImporterWithLimits : public assets::IModelImporter {
public:
    [[nodiscard]] virtual assets::ModelAssetResult import_with_limits(
        const assets::AssetSource& source,
        const GoldSrcStudioModelImportLimits& limits) const = 0;

protected:
    IGoldSrcStudioModelImporterWithLimits() = default;
};

class GoldSrcStudioModelImporter final
    : public IGoldSrcStudioModelImporterWithLimits {
public:
    explicit GoldSrcStudioModelImporter(GoldSrcStudioModelImportLimits limits = {});

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override;
    [[nodiscard]] assets::ModelAssetResult import(
        const assets::AssetSource& source) const override;
    [[nodiscard]] assets::ModelAssetResult import_with_limits(
        const assets::AssetSource& source,
        const GoldSrcStudioModelImportLimits& limits) const override;

    [[nodiscard]] assets::ModelAssetResult import_bundle(
        const assets::AssetSource& main_source,
        const GoldSrcStudioSourceBundleView& bundle) const;

private:
    [[nodiscard]] static assets::ModelAssetResult import_with_configuration(
        const assets::AssetSource& source,
        const GoldSrcStudioModelImportLimits& limits);
    [[nodiscard]] static assets::ModelAssetResult
    import_bundle_with_configuration(
        const assets::AssetSource& main_source,
        const GoldSrcStudioSourceBundleView& bundle,
        const GoldSrcStudioModelImportLimits& limits);

    GoldSrcStudioModelImportLimits limits_;
};

} // namespace hlclient::goldsrc::studio

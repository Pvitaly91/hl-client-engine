#pragma once

#include <hlclient/assets/asset_importer_dispatcher.hpp>
#include <hlclient/goldsrc/approved_asset_source.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_bundle_import.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::visual_assets {

using GoldSrcVisualAssetImportTimePoint =
    std::chrono::steady_clock::time_point;

struct GoldSrcVisualAssetImportLimits {
    GoldSrcStudioModelBundleImportLimits studio_bundle;
};

[[nodiscard]] bool valid_goldsrc_visual_asset_import_limits(
    const GoldSrcVisualAssetImportLimits& limits) noexcept;

enum class GoldSrcVisualAssetImportState {
    idle,
    dispatching,
    importing_studio_bundle,
    asset_ready,
    dependency_missing,
    dependency_invalid,
    cancelled,
    timed_out,
    failed,
};

enum class GoldSrcVisualAssetImportErrorCode {
    invalid_configuration,
    approved_source_invalid,
    dispatch_failed,
    unsupported_external_dependency,
    studio_bundle_begin_failed,
    dependency_missing,
    dependency_invalid,
    time_moved_backwards,
    cancelled,
    timed_out,
    unable_to_retain_state,
};

struct GoldSrcVisualAssetImportError {
    GoldSrcVisualAssetImportErrorCode code{
        GoldSrcVisualAssetImportErrorCode::invalid_configuration};
    std::optional<assets::AssetDispatchState> dispatch_state;
    std::optional<assets::AssetErrorCode> asset_code;
    std::optional<GoldSrcStudioModelBundleImportErrorCode>
        studio_bundle_code;
    std::string context;
};

class GoldSrcVisualAssetImportResult final {
public:
    GoldSrcVisualAssetImportResult(
        std::size_t wire_ordinal,
        ResourceType resource_type,
        std::uint16_t resource_index,
        assets::ImportedAsset asset,
        assets::AssetImporterCategory selected_category,
        std::string selected_importer_id,
        std::vector<assets::AssetDispatchProbeCandidate> top_candidates,
        std::optional<GoldSrcStudioModelSourceBundle> studio_sources,
        GoldSrcStudioModelDependencyStatistics dependency_statistics,
        std::vector<assets::AssetSourceFingerprint> source_fingerprints,
        PrecacheManifestCompatibilityProfile compatibility_profile,
        PrecacheManifestEvidenceProfile evidence_profile);

    GoldSrcVisualAssetImportResult(GoldSrcVisualAssetImportResult&&) noexcept =
        default;
    GoldSrcVisualAssetImportResult& operator=(
        GoldSrcVisualAssetImportResult&&) noexcept = default;
    GoldSrcVisualAssetImportResult(const GoldSrcVisualAssetImportResult&) =
        delete;
    GoldSrcVisualAssetImportResult& operator=(
        const GoldSrcVisualAssetImportResult&) = delete;
    ~GoldSrcVisualAssetImportResult() = default;

    [[nodiscard]] const assets::ImportedAsset& asset() const noexcept;
    [[nodiscard]] std::size_t wire_ordinal() const noexcept;
    [[nodiscard]] ResourceType resource_type() const noexcept;
    [[nodiscard]] std::uint16_t resource_index() const noexcept;
    [[nodiscard]] assets::AssetImporterCategory selected_category()
        const noexcept;
    [[nodiscard]] std::string_view selected_importer_id() const noexcept;
    [[nodiscard]] const std::vector<assets::AssetDispatchProbeCandidate>&
    top_candidates() const noexcept;
    [[nodiscard]] const std::optional<GoldSrcStudioModelSourceBundle>&
    studio_sources() const noexcept;
    [[nodiscard]] const GoldSrcStudioModelDependencyStatistics&
    dependency_statistics() const noexcept;
    // Ordered main, optional texture, then ascending sequence groups.
    [[nodiscard]] std::span<const assets::AssetSourceFingerprint>
    source_fingerprints() const noexcept;
    [[nodiscard]] PrecacheManifestCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] PrecacheManifestEvidenceProfile evidence_profile()
        const noexcept;

private:
    std::size_t wire_ordinal_{0U};
    ResourceType resource_type_{ResourceType::sound};
    std::uint16_t resource_index_{0U};
    assets::ImportedAsset asset_;
    assets::AssetImporterCategory selected_category_{
        assets::AssetImporterCategory::none};
    std::string selected_importer_id_;
    std::vector<assets::AssetDispatchProbeCandidate> top_candidates_;
    std::optional<GoldSrcStudioModelSourceBundle> studio_sources_;
    GoldSrcStudioModelDependencyStatistics dependency_statistics_{};
    std::vector<assets::AssetSourceFingerprint> source_fingerprints_;
    PrecacheManifestCompatibilityProfile compatibility_profile_{
        PrecacheManifestCompatibilityProfile::
            stock_protocol_48_standard_metadata_only};
    PrecacheManifestEvidenceProfile evidence_profile_{
        PrecacheManifestEvidenceProfile::
            exact_correlated_local_resource_metadata};
};

class GoldSrcVisualAssetImportOperation;
struct GoldSrcVisualAssetImportBeginResult;

// Reusable caller-driven composition boundary for an already-approved
// model-or-sprite source. Registries are borrowed and must outlive the
// operation; all source bytes and final assets are owned by the operation.
class GoldSrcVisualAssetImportOperation final {
public:
    // Network-free entry point for an exact-root LocalAssetSource. This is the
    // canonical offline composition route used by diagnostics: cross-category
    // dispatch still selects and invokes exactly one registered importer.
    [[nodiscard]] static GoldSrcVisualAssetImportBeginResult begin(
        const local_assets::LocalAssetSource& source,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& registries,
        GoldSrcVisualAssetImportLimits limits = {});

    [[nodiscard]] static GoldSrcVisualAssetImportBeginResult begin(
        const ApprovedAssetSource& approved_source,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        const assets::AssetImporterRegistries& registries,
        GoldSrcVisualAssetImportLimits limits = {});

    ~GoldSrcVisualAssetImportOperation();
    GoldSrcVisualAssetImportOperation(
        GoldSrcVisualAssetImportOperation&&) noexcept;
    GoldSrcVisualAssetImportOperation& operator=(
        GoldSrcVisualAssetImportOperation&&) noexcept;
    GoldSrcVisualAssetImportOperation(
        const GoldSrcVisualAssetImportOperation&) = delete;
    GoldSrcVisualAssetImportOperation& operator=(
        const GoldSrcVisualAssetImportOperation&) = delete;

    void update(GoldSrcVisualAssetImportTimePoint now) noexcept;
    void cancel() noexcept;

    [[nodiscard]] GoldSrcVisualAssetImportState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const GoldSrcVisualAssetImportResult* result()
        const noexcept;
    [[nodiscard]] const GoldSrcVisualAssetImportError* error() const noexcept;
    [[nodiscard]] std::optional<GoldSrcVisualAssetImportResult> take_result()
        noexcept;

private:
    class Implementation;
    explicit GoldSrcVisualAssetImportOperation(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

struct GoldSrcVisualAssetImportBeginResult {
    std::optional<GoldSrcVisualAssetImportOperation> operation;
    std::optional<GoldSrcVisualAssetImportError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return operation.has_value();
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    GoldSrcVisualAssetImportState state) noexcept
{
    switch (state) {
    case GoldSrcVisualAssetImportState::idle: return "idle";
    case GoldSrcVisualAssetImportState::dispatching: return "dispatching";
    case GoldSrcVisualAssetImportState::importing_studio_bundle:
        return "importing_studio_bundle";
    case GoldSrcVisualAssetImportState::asset_ready: return "asset_ready";
    case GoldSrcVisualAssetImportState::dependency_missing:
        return "dependency_missing";
    case GoldSrcVisualAssetImportState::dependency_invalid:
        return "dependency_invalid";
    case GoldSrcVisualAssetImportState::cancelled: return "cancelled";
    case GoldSrcVisualAssetImportState::timed_out: return "timed_out";
    case GoldSrcVisualAssetImportState::failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    GoldSrcVisualAssetImportErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcVisualAssetImportErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcVisualAssetImportErrorCode::approved_source_invalid:
        return "approved_source_invalid";
    case GoldSrcVisualAssetImportErrorCode::dispatch_failed:
        return "dispatch_failed";
    case GoldSrcVisualAssetImportErrorCode::
        unsupported_external_dependency:
        return "unsupported_external_dependency";
    case GoldSrcVisualAssetImportErrorCode::studio_bundle_begin_failed:
        return "studio_bundle_begin_failed";
    case GoldSrcVisualAssetImportErrorCode::dependency_missing:
        return "dependency_missing";
    case GoldSrcVisualAssetImportErrorCode::dependency_invalid:
        return "dependency_invalid";
    case GoldSrcVisualAssetImportErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case GoldSrcVisualAssetImportErrorCode::cancelled: return "cancelled";
    case GoldSrcVisualAssetImportErrorCode::timed_out: return "timed_out";
    case GoldSrcVisualAssetImportErrorCode::unable_to_retain_state:
        return "unable_to_retain_state";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::visual_assets

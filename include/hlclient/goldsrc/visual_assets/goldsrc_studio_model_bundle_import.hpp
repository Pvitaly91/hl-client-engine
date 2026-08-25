#pragma once

#include <hlclient/assets/asset_importer.hpp>
#include <hlclient/goldsrc/approved_asset_source.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_companion_names.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_source_bundle.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc::visual_assets {

inline constexpr std::chrono::milliseconds
    kHardMaximumGoldSrcStudioModelBundleImportTimeout{60'000};

using GoldSrcStudioModelBundleImportTimePoint =
    std::chrono::steady_clock::time_point;

struct GoldSrcStudioModelBundleImportLimits {
    studio::GoldSrcStudioModelImportLimits studio;
    GoldSrcStudioModelSourceBundleLimits bundle;
    local_assets::LocalAssetSourceOpenLimits companion_source_open;
    std::optional<std::chrono::milliseconds> timeout;
};

[[nodiscard]] bool valid_goldsrc_studio_model_bundle_import_limits(
    const GoldSrcStudioModelBundleImportLimits& limits) noexcept;

enum class GoldSrcStudioModelBundleImportState {
    idle,
    inspecting_main,
    planning_dependencies,
    opening_texture_companion,
    opening_sequence_group,
    validating_bundle,
    importing_model,
    model_ready,
    dependency_missing,
    dependency_invalid,
    cancelled,
    timed_out,
    failed,
};

enum class GoldSrcStudioModelBundleImportErrorCode {
    invalid_configuration,
    approved_source_invalid,
    invalid_main_virtual_name,
    main_inspection_failed,
    companion_name_invalid,
    companion_resolution_failed,
    companion_source_open_failed,
    dependency_missing,
    dependency_invalid,
    bundle_validation_failed,
    model_import_failed,
    time_moved_backwards,
    cancelled,
    timed_out,
    unable_to_retain_state,
};

struct GoldSrcStudioModelBundleImportError {
    GoldSrcStudioModelBundleImportErrorCode code{
        GoldSrcStudioModelBundleImportErrorCode::invalid_configuration};
    std::optional<assets::AssetErrorCode> asset_code;
    std::optional<studio::GoldSrcStudioErrorCode> studio_code;
    std::optional<GoldSrcStudioCompanionNameErrorCode> companion_name_code;
    std::optional<GoldSrcStudioModelSourceBundleErrorCode> bundle_code;
    std::optional<local_resources::LocalResourceResolutionCode>
        resolution_code;
    std::optional<local_assets::LocalAssetSourceOpenErrorCode>
        source_open_code;
    std::optional<std::uint32_t> sequence_group_ordinal;
    // Bounded metadata-only context. Native paths and virtual names are never
    // copied into this diagnostic.
    std::string context;
};

struct GoldSrcStudioModelBundleImportProgress {
    std::size_t expected_source_count{1U};
    std::size_t source_open_attempt_count{0U};
    std::size_t source_count_ready{1U};
    std::uint64_t source_bytes_ready{0U};
    std::uint64_t current_source_progress_bytes{0U};
    std::optional<std::uint32_t> current_sequence_group_ordinal;
};

class GoldSrcStudioModelBundleImportResult final {
public:
    GoldSrcStudioModelBundleImportResult(
        assets::ModelAsset model,
        GoldSrcStudioModelSourceBundle sources) noexcept;

    GoldSrcStudioModelBundleImportResult(
        GoldSrcStudioModelBundleImportResult&&) noexcept = default;
    GoldSrcStudioModelBundleImportResult& operator=(
        GoldSrcStudioModelBundleImportResult&&) noexcept = default;
    GoldSrcStudioModelBundleImportResult(
        const GoldSrcStudioModelBundleImportResult&) = delete;
    GoldSrcStudioModelBundleImportResult& operator=(
        const GoldSrcStudioModelBundleImportResult&) = delete;
    ~GoldSrcStudioModelBundleImportResult() = default;

    [[nodiscard]] const assets::ModelAsset& model() const & noexcept;
    [[nodiscard]] const GoldSrcStudioModelSourceBundle& sources()
        const & noexcept;
    [[nodiscard]] assets::ModelAsset&& model() && noexcept;
    [[nodiscard]] GoldSrcStudioModelSourceBundle&& sources() && noexcept;

private:
    assets::ModelAsset model_;
    GoldSrcStudioModelSourceBundle sources_;
};

class GoldSrcStudioModelBundleImportOperation;
struct GoldSrcStudioModelBundleImportBeginResult;

// Caller-driven, transactional composition of one approved main Studio source
// and its exact-root siblings. The operation owns every byte it needs and
// never retains a native path or an open handle in its published result.
class GoldSrcStudioModelBundleImportOperation final {
public:
    // Composition entry point for a caller that already owns immutable main
    // bytes and the opaque evidence captured when those bytes were approved.
    // Exact-root backing evidence is checked only when this method is called;
    // this lets model-or-sprite dispatch remain independent of filesystem
    // state until a selected Studio importer requests external dependencies.
    [[nodiscard]] static GoldSrcStudioModelBundleImportBeginResult
    begin_with_retained_main_evidence(
        const assets::AssetSource& main_source,
        local_resources::LocalResourceRootId main_root_id,
        local_resources::LocalVirtualResourceId main_virtual_resource_id,
        local_resources::LocalStableFileIdentity main_identity,
        std::uint64_t main_byte_count,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        GoldSrcStudioModelBundleImportLimits limits = {});

    // Safe non-manifest entry point for offline/read-only tools. The retained
    // root, virtual id, stable identity, and size are resolved again in the
    // same environment before the operation is created.
    [[nodiscard]] static GoldSrcStudioModelBundleImportBeginResult begin(
        const local_assets::LocalAssetSource& main_source,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        GoldSrcStudioModelBundleImportLimits limits = {});

    // Manifest capability facade. In addition to the checks above, this
    // overload requires all retained ApprovedAssetSource evidence to match.
    [[nodiscard]] static GoldSrcStudioModelBundleImportBeginResult begin(
        const ApprovedAssetSource& approved_main_source,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        GoldSrcStudioModelBundleImportLimits limits = {});

    ~GoldSrcStudioModelBundleImportOperation();
    GoldSrcStudioModelBundleImportOperation(
        GoldSrcStudioModelBundleImportOperation&&) noexcept;
    GoldSrcStudioModelBundleImportOperation& operator=(
        GoldSrcStudioModelBundleImportOperation&&) noexcept;
    GoldSrcStudioModelBundleImportOperation(
        const GoldSrcStudioModelBundleImportOperation&) = delete;
    GoldSrcStudioModelBundleImportOperation& operator=(
        const GoldSrcStudioModelBundleImportOperation&) = delete;

    void update(GoldSrcStudioModelBundleImportTimePoint now) noexcept;
    void cancel() noexcept;

    [[nodiscard]] GoldSrcStudioModelBundleImportState state() const noexcept;
    [[nodiscard]] bool terminal() const noexcept;
    [[nodiscard]] const GoldSrcStudioModelBundleImportProgress& progress()
        const noexcept;
    [[nodiscard]] const GoldSrcStudioModelBundleImportResult* result()
        const noexcept;
    [[nodiscard]] const GoldSrcStudioModelBundleImportError* error()
        const noexcept;
    [[nodiscard]] std::optional<GoldSrcStudioModelBundleImportResult>
    take_result() noexcept;

private:
    class Implementation;
    [[nodiscard]] static GoldSrcStudioModelBundleImportBeginResult
    begin_validated(
        const assets::AssetSource& main_source,
        local_resources::LocalResourceRootId main_root_id,
        local_resources::LocalVirtualResourceName main_virtual_name,
        local_resources::LocalStableFileIdentity main_identity,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        GoldSrcStudioModelBundleImportLimits limits);
    explicit GoldSrcStudioModelBundleImportOperation(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

struct GoldSrcStudioModelBundleImportBeginResult {
    std::optional<GoldSrcStudioModelBundleImportOperation> operation;
    std::optional<GoldSrcStudioModelBundleImportError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return operation.has_value();
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    GoldSrcStudioModelBundleImportState state) noexcept
{
    switch (state) {
    case GoldSrcStudioModelBundleImportState::idle: return "idle";
    case GoldSrcStudioModelBundleImportState::inspecting_main:
        return "inspecting_main";
    case GoldSrcStudioModelBundleImportState::planning_dependencies:
        return "planning_dependencies";
    case GoldSrcStudioModelBundleImportState::opening_texture_companion:
        return "opening_texture_companion";
    case GoldSrcStudioModelBundleImportState::opening_sequence_group:
        return "opening_sequence_group";
    case GoldSrcStudioModelBundleImportState::validating_bundle:
        return "validating_bundle";
    case GoldSrcStudioModelBundleImportState::importing_model:
        return "importing_model";
    case GoldSrcStudioModelBundleImportState::model_ready:
        return "model_ready";
    case GoldSrcStudioModelBundleImportState::dependency_missing:
        return "dependency_missing";
    case GoldSrcStudioModelBundleImportState::dependency_invalid:
        return "dependency_invalid";
    case GoldSrcStudioModelBundleImportState::cancelled: return "cancelled";
    case GoldSrcStudioModelBundleImportState::timed_out: return "timed_out";
    case GoldSrcStudioModelBundleImportState::failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    GoldSrcStudioModelBundleImportErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcStudioModelBundleImportErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcStudioModelBundleImportErrorCode::approved_source_invalid:
        return "approved_source_invalid";
    case GoldSrcStudioModelBundleImportErrorCode::invalid_main_virtual_name:
        return "invalid_main_virtual_name";
    case GoldSrcStudioModelBundleImportErrorCode::main_inspection_failed:
        return "main_inspection_failed";
    case GoldSrcStudioModelBundleImportErrorCode::companion_name_invalid:
        return "companion_name_invalid";
    case GoldSrcStudioModelBundleImportErrorCode::
        companion_resolution_failed:
        return "companion_resolution_failed";
    case GoldSrcStudioModelBundleImportErrorCode::
        companion_source_open_failed:
        return "companion_source_open_failed";
    case GoldSrcStudioModelBundleImportErrorCode::dependency_missing:
        return "dependency_missing";
    case GoldSrcStudioModelBundleImportErrorCode::dependency_invalid:
        return "dependency_invalid";
    case GoldSrcStudioModelBundleImportErrorCode::bundle_validation_failed:
        return "bundle_validation_failed";
    case GoldSrcStudioModelBundleImportErrorCode::model_import_failed:
        return "model_import_failed";
    case GoldSrcStudioModelBundleImportErrorCode::time_moved_backwards:
        return "time_moved_backwards";
    case GoldSrcStudioModelBundleImportErrorCode::cancelled:
        return "cancelled";
    case GoldSrcStudioModelBundleImportErrorCode::timed_out:
        return "timed_out";
    case GoldSrcStudioModelBundleImportErrorCode::unable_to_retain_state:
        return "unable_to_retain_state";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::visual_assets

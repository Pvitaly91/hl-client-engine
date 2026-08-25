#pragma once

#include <hlclient/assets/asset_importer_dispatcher.hpp>
#include <hlclient/goldsrc/approved_asset_source.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

namespace detail {
class PrecacheAssetDispatchStageTestAccess;
} // namespace detail

inline constexpr std::size_t kMaximumAssetDispatchPlanCategories = 2U;
inline constexpr std::size_t kPrecacheAssetDispatchDiagnosticTextLimit = 256U;

class ApprovedAssetSourceOpenOperation;
class ApprovedAssetSourceOpener;

class AssetDispatchPlan final {
public:
    AssetDispatchPlan(const AssetDispatchPlan&) = default;
    AssetDispatchPlan(AssetDispatchPlan&&) noexcept = default;
    AssetDispatchPlan& operator=(const AssetDispatchPlan&) = delete;
    AssetDispatchPlan& operator=(AssetDispatchPlan&&) noexcept = delete;
    ~AssetDispatchPlan() = default;

    [[nodiscard]] std::size_t wire_ordinal() const noexcept;
    [[nodiscard]] ResourceType resource_type() const noexcept;
    [[nodiscard]] std::uint16_t resource_index() const noexcept;
    [[nodiscard]] assets::AssetDispatchRole role() const noexcept;
    [[nodiscard]] std::span<const assets::AssetImporterCategory>
    allowed_importer_categories() const noexcept;
    [[nodiscard]] bool selected_world() const noexcept;
    [[nodiscard]] PrecacheManifestCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] PrecacheManifestEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend struct AssetDispatchPlanBuildResult;
    friend class AssetDispatchPlanBuilder;
    friend class ApprovedAssetImporterDispatcher;
    friend class ApprovedAssetSource;
    friend class ApprovedAssetSourceOpenOperation;
    friend class ApprovedAssetSourceOpener;

    AssetDispatchPlan(
        PrecacheManifestEntry entry,
        std::size_t wire_ordinal,
        ResourceType resource_type,
        std::uint16_t resource_index,
        assets::AssetDispatchRole role,
        std::array<assets::AssetImporterCategory,
                   kMaximumAssetDispatchPlanCategories> allowed_categories,
        std::size_t allowed_category_count,
        bool selected_world,
        PrecacheManifestCompatibilityProfile compatibility_profile,
        PrecacheManifestEvidenceProfile evidence_profile) noexcept;

    // Retain the exact manifest evidence from which the builder derived the
    // role. The source opener consumes this capability directly instead of
    // accepting a second, merely metadata-equal entry.
    PrecacheManifestEntry entry_;
    std::size_t wire_ordinal_{0U};
    ResourceType resource_type_{ResourceType::sound};
    std::uint16_t resource_index_{0U};
    assets::AssetDispatchRole role_{assets::AssetDispatchRole::unsupported};
    std::array<assets::AssetImporterCategory,
               kMaximumAssetDispatchPlanCategories> allowed_categories_{};
    std::size_t allowed_category_count_{0U};
    bool selected_world_{false};
    PrecacheManifestCompatibilityProfile compatibility_profile_{
        PrecacheManifestCompatibilityProfile::
            stock_protocol_48_standard_metadata_only};
    PrecacheManifestEvidenceProfile evidence_profile_{
        PrecacheManifestEvidenceProfile::
            exact_correlated_local_resource_metadata};
};

enum class AssetDispatchPlanErrorCode {
    entry_not_in_manifest,
    world_selection_invariant_failed,
    unable_to_retain_plan,
};

struct AssetDispatchPlanError {
    AssetDispatchPlanErrorCode code{
        AssetDispatchPlanErrorCode::entry_not_in_manifest};
    std::optional<std::size_t> wire_ordinal;
    std::string context;
};

struct AssetDispatchPlanBuildResult {
    std::optional<AssetDispatchPlan> plan;
    std::optional<AssetDispatchPlanError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return plan.has_value();
    }
};

class AssetDispatchPlanBuilder final {
public:
    [[nodiscard]] AssetDispatchPlanBuildResult build(
        const PrecacheManifestState& manifest,
        const PrecacheManifestEntry& entry) const noexcept;
};

enum class ApprovedAssetSourceErrorCode {
    entry_not_ready,
    locator_missing,
    unsupported_dispatch_role,
    dispatch_plan_mismatch,
    approved_source_mismatch,
    unable_to_retain_source,
};

struct ApprovedAssetSourceError {
    ApprovedAssetSourceErrorCode code{
        ApprovedAssetSourceErrorCode::entry_not_ready};
    std::optional<std::size_t> wire_ordinal;
    std::string context;
};

struct ApprovedAssetSourceCreateResult;

// Capability-checking facade used by GoldSrc production composition. The
// generic assets-layer dispatcher remains filesystem/protocol-neutral, while
// this facade refuses to strip the exact manifest plan from its approved
// source before role-based probing.
class ApprovedAssetImporterDispatcher final {
public:
    explicit ApprovedAssetImporterDispatcher(
        const assets::AssetImporterRegistries& registries) noexcept;

    [[nodiscard]] assets::AssetDispatchResult dispatch(
        const ApprovedAssetSource& source,
        const AssetDispatchPlan& plan) const;

private:
    assets::AssetImporterDispatcher dispatcher_;
};

struct ApprovedAssetSourceCreateResult {
    std::optional<ApprovedAssetSource> source;
    std::optional<ApprovedAssetSourceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return source.has_value();
    }
};

using ApprovedAssetSourceOpenLimits =
    local_assets::LocalAssetSourceOpenLimits;
using ApprovedAssetSourceOpenTimePoint =
    local_assets::LocalAssetSourceOpenTimePoint;

enum class ApprovedAssetSourceOpenState {
    idle,
    opening,
    reading,
    validating,
    source_ready,
    cancelled,
    timed_out,
    failed,
};

enum class ApprovedAssetSourceOpenErrorCode {
    invalid_configuration,
    locator_invalid,
    locator_environment_mismatch,
    locator_target_missing,
    stale_locator,
    source_too_large,
    source_read_failed,
    source_changed_during_read,
    source_creation_failed,
    cancelled,
    timed_out,
};

struct ApprovedAssetSourceOpenError {
    ApprovedAssetSourceOpenErrorCode code{
        ApprovedAssetSourceOpenErrorCode::invalid_configuration};
    std::optional<local_assets::LocalAssetSourceOpenErrorCode>
        local_source_code;
    std::optional<local_resources::LocalResourceLocatorReopenErrorCode>
        locator_reopen_code;
    std::optional<local_resources::LocalReadOnlyFileErrorCode> read_code;
    std::optional<ApprovedAssetSourceErrorCode> approved_source_code;
    std::string context;
};

// GoldSrc facade over the incremental same-handle local operation. It retains
// one manifest entry and its evidence-derived plan, and publishes the approved
// source only after the delegated operation has completed final validation.
class ApprovedAssetSourceOpenOperation final {
public:
    ApprovedAssetSourceOpenOperation(
        ApprovedAssetSourceOpenOperation&&) noexcept = default;
    ApprovedAssetSourceOpenOperation& operator=(
        ApprovedAssetSourceOpenOperation&&) noexcept = delete;
    ApprovedAssetSourceOpenOperation(
        const ApprovedAssetSourceOpenOperation&) = delete;
    ApprovedAssetSourceOpenOperation& operator=(
        const ApprovedAssetSourceOpenOperation&) = delete;
    ~ApprovedAssetSourceOpenOperation() = default;

    void update(ApprovedAssetSourceOpenTimePoint now) noexcept;
    void cancel() noexcept;

    [[nodiscard]] ApprovedAssetSourceOpenState state() const noexcept;
    [[nodiscard]] std::uint64_t progress_bytes() const noexcept;
    [[nodiscard]] const ApprovedAssetSource* result() const noexcept;
    [[nodiscard]] const ApprovedAssetSourceOpenError* error() const noexcept;
    [[nodiscard]] std::optional<ApprovedAssetSource> take_result() noexcept;

private:
    friend class ApprovedAssetSourceOpener;
    friend class detail::PrecacheAssetDispatchStageTestAccess;

    ApprovedAssetSourceOpenOperation(
        AssetDispatchPlan plan,
        local_assets::LocalAssetSourceOpenOperation operation) noexcept;
    void synchronize_from_local_operation() noexcept;
    void fail_from_local_operation(
        const local_assets::LocalAssetSourceOpenError& error) noexcept;
    void fail_from_approved_source(
        const ApprovedAssetSourceError& error) noexcept;

    AssetDispatchPlan plan_;
    local_assets::LocalAssetSourceOpenOperation operation_;
    ApprovedAssetSourceOpenState state_{
        ApprovedAssetSourceOpenState::opening};
    std::optional<ApprovedAssetSource> result_;
    std::optional<ApprovedAssetSourceOpenError> error_;
};

struct ApprovedAssetSourceOpenBeginResult {
    std::optional<ApprovedAssetSourceOpenOperation> operation;
    std::optional<ApprovedAssetSourceOpenError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return operation.has_value();
    }
};

class ApprovedAssetSourceOpener final {
public:
    ApprovedAssetSourceOpener() = default;
    ApprovedAssetSourceOpener(ApprovedAssetSourceOpener&&) noexcept = default;
    ApprovedAssetSourceOpener& operator=(ApprovedAssetSourceOpener&&) noexcept =
        default;
    ApprovedAssetSourceOpener(const ApprovedAssetSourceOpener&) = delete;
    ApprovedAssetSourceOpener& operator=(const ApprovedAssetSourceOpener&) =
        delete;
    ~ApprovedAssetSourceOpener() = default;

    [[nodiscard]] ApprovedAssetSourceOpenBeginResult begin(
        const AssetDispatchPlan& plan,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        ApprovedAssetSourceOpenLimits limits = {});

private:
    local_assets::LocalAssetSourceOpener opener_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const AssetDispatchPlanErrorCode code) noexcept
{
    switch (code) {
    case AssetDispatchPlanErrorCode::entry_not_in_manifest:
        return "entry_not_in_manifest";
    case AssetDispatchPlanErrorCode::world_selection_invariant_failed:
        return "world_selection_invariant_failed";
    case AssetDispatchPlanErrorCode::unable_to_retain_plan:
        return "unable_to_retain_plan";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ApprovedAssetSourceErrorCode code) noexcept
{
    switch (code) {
    case ApprovedAssetSourceErrorCode::entry_not_ready:
        return "entry_not_ready";
    case ApprovedAssetSourceErrorCode::locator_missing:
        return "locator_missing";
    case ApprovedAssetSourceErrorCode::unsupported_dispatch_role:
        return "unsupported_dispatch_role";
    case ApprovedAssetSourceErrorCode::dispatch_plan_mismatch:
        return "dispatch_plan_mismatch";
    case ApprovedAssetSourceErrorCode::approved_source_mismatch:
        return "approved_source_mismatch";
    case ApprovedAssetSourceErrorCode::unable_to_retain_source:
        return "unable_to_retain_source";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ApprovedAssetSourceOpenState state) noexcept
{
    switch (state) {
    case ApprovedAssetSourceOpenState::idle: return "idle";
    case ApprovedAssetSourceOpenState::opening: return "opening";
    case ApprovedAssetSourceOpenState::reading: return "reading";
    case ApprovedAssetSourceOpenState::validating: return "validating";
    case ApprovedAssetSourceOpenState::source_ready: return "source_ready";
    case ApprovedAssetSourceOpenState::cancelled: return "cancelled";
    case ApprovedAssetSourceOpenState::timed_out: return "timed_out";
    case ApprovedAssetSourceOpenState::failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ApprovedAssetSourceOpenErrorCode code) noexcept
{
    switch (code) {
    case ApprovedAssetSourceOpenErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ApprovedAssetSourceOpenErrorCode::locator_invalid:
        return "locator_invalid";
    case ApprovedAssetSourceOpenErrorCode::locator_environment_mismatch:
        return "locator_environment_mismatch";
    case ApprovedAssetSourceOpenErrorCode::locator_target_missing:
        return "locator_target_missing";
    case ApprovedAssetSourceOpenErrorCode::stale_locator:
        return "stale_locator";
    case ApprovedAssetSourceOpenErrorCode::source_too_large:
        return "source_too_large";
    case ApprovedAssetSourceOpenErrorCode::source_read_failed:
        return "source_read_failed";
    case ApprovedAssetSourceOpenErrorCode::source_changed_during_read:
        return "source_changed_during_read";
    case ApprovedAssetSourceOpenErrorCode::source_creation_failed:
        return "source_creation_failed";
    case ApprovedAssetSourceOpenErrorCode::cancelled: return "cancelled";
    case ApprovedAssetSourceOpenErrorCode::timed_out: return "timed_out";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

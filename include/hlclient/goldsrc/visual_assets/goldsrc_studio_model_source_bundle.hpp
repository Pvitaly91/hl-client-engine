#pragma once

#include <hlclient/assets/asset_source.hpp>
#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_companion_names.hpp>
#include <hlclient/local_resources/local_resource_identity.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::visual_assets {

inline constexpr std::uint64_t kDefaultGoldSrcStudioBundleMaximumBytes =
    64U * 1024U * 1024U;
inline constexpr std::uint64_t kHardGoldSrcStudioBundleMaximumBytes =
    256U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcStudioBundleMaximumSources = 17U;

struct GoldSrcStudioModelSourceBundleLimits {
    std::uint64_t maximum_total_source_bytes{
        kDefaultGoldSrcStudioBundleMaximumBytes};
    std::size_t maximum_source_count{kGoldSrcStudioBundleMaximumSources};
};

[[nodiscard]] bool valid_goldsrc_studio_model_source_bundle_limits(
    const GoldSrcStudioModelSourceBundleLimits& limits) noexcept;

enum class GoldSrcStudioModelSourceBundleErrorCode {
    invalid_configuration,
    source_count_limit_exceeded,
    total_source_bytes_limit_exceeded,
    invalid_sequence_group_ordinal,
    duplicate_sequence_group,
    duplicate_virtual_source,
    dependency_plan_mismatch,
    unable_to_retain_bundle,
};

struct GoldSrcStudioModelSourceBundleError {
    GoldSrcStudioModelSourceBundleErrorCode code{
        GoldSrcStudioModelSourceBundleErrorCode::invalid_configuration};
    std::optional<std::uint32_t> sequence_group_ordinal;
    std::string context;
};

// Path-free evidence for one verified source in an owning Studio bundle.
// Root provenance, the byte-exact virtual-name token, and the stable file
// identity remain comparable without exposing a native filesystem path.
class GoldSrcStudioVerifiedSourceIdentity final {
public:
    GoldSrcStudioVerifiedSourceIdentity(
        local_resources::LocalResourceRootId root_id,
        local_resources::LocalVirtualResourceId virtual_resource_id,
        local_resources::LocalStableFileIdentity stable_file_identity)
        noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] local_resources::LocalResourceRootId root_id()
        const noexcept;
    [[nodiscard]] local_resources::LocalVirtualResourceId
    virtual_resource_id() const noexcept;
    [[nodiscard]] local_resources::LocalStableFileIdentity
    stable_file_identity() const noexcept;

private:
    local_resources::LocalResourceRootId root_id_;
    local_resources::LocalVirtualResourceId virtual_resource_id_;
    local_resources::LocalStableFileIdentity stable_file_identity_;
};

struct GoldSrcStudioSequenceGroupSource {
    GoldSrcStudioSequenceGroupSource(
        std::uint32_t ordinal,
        assets::AssetSource source,
        GoldSrcStudioVerifiedSourceIdentity identity);

    std::uint32_t ordinal{0U};
    assets::AssetSource source;
    GoldSrcStudioVerifiedSourceIdentity identity;
    assets::AssetSourceFingerprint fingerprint{};
};

struct GoldSrcStudioModelDependencyStatistics {
    std::size_t source_count{0U};
    std::size_t sequence_group_source_count{0U};
    std::uint64_t total_source_bytes{0U};
    bool texture_companion_present{false};
};

struct GoldSrcStudioResolvedSequenceGroupDependency {
    GoldSrcStudioResolvedSequenceGroupDependency(
        std::uint32_t ordinal,
        local_resources::LocalVirtualResourceName virtual_name);

    std::uint32_t ordinal{0U};
    local_resources::LocalVirtualResourceName virtual_name;
};

enum class GoldSrcStudioResolvedDependencyPlanErrorCode {
    invalid_main_evidence,
    invalid_source_plan,
    companion_name_invalid,
    unable_to_retain_plan,
};

struct GoldSrcStudioResolvedDependencyPlanError {
    GoldSrcStudioResolvedDependencyPlanErrorCode code{
        GoldSrcStudioResolvedDependencyPlanErrorCode::invalid_main_evidence};
    std::optional<std::uint32_t> sequence_group_ordinal;
    std::optional<GoldSrcStudioCompanionNameErrorCode> companion_name_code;
    std::string context;
};

struct GoldSrcStudioResolvedDependencyPlanResult;

class GoldSrcStudioResolvedDependencyPlan final {
public:
    GoldSrcStudioResolvedDependencyPlan(
        const GoldSrcStudioResolvedDependencyPlan&) = default;
    GoldSrcStudioResolvedDependencyPlan(
        GoldSrcStudioResolvedDependencyPlan&&) noexcept = default;
    GoldSrcStudioResolvedDependencyPlan& operator=(
        const GoldSrcStudioResolvedDependencyPlan&) = default;
    GoldSrcStudioResolvedDependencyPlan& operator=(
        GoldSrcStudioResolvedDependencyPlan&&) noexcept = default;
    ~GoldSrcStudioResolvedDependencyPlan() = default;

    [[nodiscard]] const studio::GoldSrcStudioModelDependencyPlan& source_plan()
        const noexcept;
    [[nodiscard]] local_resources::LocalResourceRootId main_root_id()
        const noexcept;
    [[nodiscard]] const local_resources::LocalVirtualResourceName&
    main_virtual_name() const noexcept;
    [[nodiscard]] local_resources::LocalStableFileIdentity main_identity()
        const noexcept;
    [[nodiscard]] assets::AssetSourceFingerprint main_fingerprint()
        const noexcept;
    [[nodiscard]] const std::optional<
        local_resources::LocalVirtualResourceName>&
    texture_companion_name() const noexcept;
    [[nodiscard]] std::span<
        const GoldSrcStudioResolvedSequenceGroupDependency>
    sequence_group_dependencies() const noexcept;
    [[nodiscard]] assets::ModelSkeletalCompatibilityProfile
    compatibility_profile() const noexcept;
    [[nodiscard]] assets::ModelSkeletalEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend struct GoldSrcStudioResolvedDependencyPlanResult;
    friend GoldSrcStudioResolvedDependencyPlanResult
    resolve_goldsrc_studio_dependency_plan(
        const studio::GoldSrcStudioModelDependencyPlan&,
        const local_resources::LocalVirtualResourceName&,
        local_resources::LocalResourceRootId,
        local_resources::LocalStableFileIdentity,
        assets::AssetSourceFingerprint) noexcept;

    GoldSrcStudioResolvedDependencyPlan(
        studio::GoldSrcStudioModelDependencyPlan source_plan,
        local_resources::LocalResourceRootId main_root_id,
        local_resources::LocalVirtualResourceName main_virtual_name,
        local_resources::LocalStableFileIdentity main_identity,
        assets::AssetSourceFingerprint main_fingerprint,
        std::optional<local_resources::LocalVirtualResourceName>
            texture_companion_name,
        std::vector<GoldSrcStudioResolvedSequenceGroupDependency>
            sequence_group_dependencies) noexcept;

    studio::GoldSrcStudioModelDependencyPlan source_plan_;
    local_resources::LocalResourceRootId main_root_id_;
    local_resources::LocalVirtualResourceName main_virtual_name_;
    local_resources::LocalStableFileIdentity main_identity_;
    assets::AssetSourceFingerprint main_fingerprint_{};
    std::optional<local_resources::LocalVirtualResourceName>
        texture_companion_name_;
    std::vector<GoldSrcStudioResolvedSequenceGroupDependency>
        sequence_group_dependencies_;
};

struct GoldSrcStudioResolvedDependencyPlanResult {
    std::optional<GoldSrcStudioResolvedDependencyPlan> plan;
    std::optional<GoldSrcStudioResolvedDependencyPlanError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return plan.has_value();
    }
};

// Resolves metadata only. The signature accepts no Studio header name and no
// native path; every sibling is derived from the already-classified main name.
[[nodiscard]] GoldSrcStudioResolvedDependencyPlanResult
resolve_goldsrc_studio_dependency_plan(
    const studio::GoldSrcStudioModelDependencyPlan& source_plan,
    const local_resources::LocalVirtualResourceName& main_virtual_name,
    local_resources::LocalResourceRootId main_root_id,
    local_resources::LocalStableFileIdentity main_identity,
    assets::AssetSourceFingerprint main_fingerprint) noexcept;

class GoldSrcStudioModelSourceBundle;
struct GoldSrcStudioModelSourceBundleCreateResult;

// Owning, handle-free source set. Exact-root discovery is deliberately outside
// this type; the caller-driven local operation proves that policy before
// passing owning AssetSources here. The bundle itself contains no native path.
class GoldSrcStudioModelSourceBundle final {
public:
    [[nodiscard]] static GoldSrcStudioModelSourceBundleCreateResult create(
        assets::AssetSource main_source,
        std::optional<assets::AssetSource> texture_source,
        std::optional<GoldSrcStudioVerifiedSourceIdentity>
            texture_source_identity,
        std::vector<GoldSrcStudioSequenceGroupSource> sequence_group_sources,
        const GoldSrcStudioResolvedDependencyPlan& dependency_plan,
        GoldSrcStudioModelSourceBundleLimits limits = {}) noexcept;

    GoldSrcStudioModelSourceBundle(
        GoldSrcStudioModelSourceBundle&&) noexcept = default;
    GoldSrcStudioModelSourceBundle& operator=(
        GoldSrcStudioModelSourceBundle&&) noexcept = default;
    GoldSrcStudioModelSourceBundle(
        const GoldSrcStudioModelSourceBundle&) = delete;
    GoldSrcStudioModelSourceBundle& operator=(
        const GoldSrcStudioModelSourceBundle&) = delete;
    ~GoldSrcStudioModelSourceBundle() = default;

    [[nodiscard]] const assets::AssetSource& main_source() const noexcept;
    [[nodiscard]] const std::optional<assets::AssetSource>& texture_source()
        const noexcept;
    [[nodiscard]] const GoldSrcStudioVerifiedSourceIdentity&
    main_source_identity() const noexcept;
    [[nodiscard]] const std::optional<GoldSrcStudioVerifiedSourceIdentity>&
    texture_source_identity() const noexcept;
    [[nodiscard]] std::span<const GoldSrcStudioSequenceGroupSource>
    sequence_group_sources() const noexcept;
    [[nodiscard]] assets::AssetSourceFingerprint main_fingerprint()
        const noexcept;
    [[nodiscard]] const std::optional<assets::AssetSourceFingerprint>&
    texture_fingerprint() const noexcept;
    [[nodiscard]] const GoldSrcStudioModelDependencyStatistics& statistics()
        const noexcept;
    [[nodiscard]] const studio::GoldSrcStudioModelDependencyPlan&
    dependency_plan() const noexcept;
    [[nodiscard]] const GoldSrcStudioResolvedDependencyPlan&
    resolved_dependency_plan() const noexcept;

private:
    GoldSrcStudioModelSourceBundle(
        assets::AssetSource main_source,
        std::optional<assets::AssetSource> texture_source,
        GoldSrcStudioVerifiedSourceIdentity main_source_identity,
        std::optional<GoldSrcStudioVerifiedSourceIdentity>
            texture_source_identity,
        std::vector<GoldSrcStudioSequenceGroupSource> sequence_group_sources,
        GoldSrcStudioResolvedDependencyPlan dependency_plan,
        assets::AssetSourceFingerprint main_fingerprint,
        std::optional<assets::AssetSourceFingerprint> texture_fingerprint,
        GoldSrcStudioModelDependencyStatistics statistics) noexcept;

    assets::AssetSource main_source_;
    std::optional<assets::AssetSource> texture_source_;
    GoldSrcStudioVerifiedSourceIdentity main_source_identity_;
    std::optional<GoldSrcStudioVerifiedSourceIdentity>
        texture_source_identity_;
    std::vector<GoldSrcStudioSequenceGroupSource> sequence_group_sources_;
    GoldSrcStudioResolvedDependencyPlan dependency_plan_;
    assets::AssetSourceFingerprint main_fingerprint_{};
    std::optional<assets::AssetSourceFingerprint> texture_fingerprint_;
    GoldSrcStudioModelDependencyStatistics statistics_{};
};

struct GoldSrcStudioModelSourceBundleCreateResult {
    std::optional<GoldSrcStudioModelSourceBundle> bundle;
    std::optional<GoldSrcStudioModelSourceBundleError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return bundle.has_value();
    }
};

[[nodiscard]] assets::AssetSourceFingerprint
goldsrc_studio_source_fingerprint(std::span<const std::byte> source) noexcept;

[[nodiscard]] constexpr std::string_view to_string(
    GoldSrcStudioModelSourceBundleErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcStudioModelSourceBundleErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcStudioModelSourceBundleErrorCode::
        source_count_limit_exceeded:
        return "source_count_limit_exceeded";
    case GoldSrcStudioModelSourceBundleErrorCode::
        total_source_bytes_limit_exceeded:
        return "total_source_bytes_limit_exceeded";
    case GoldSrcStudioModelSourceBundleErrorCode::
        invalid_sequence_group_ordinal:
        return "invalid_sequence_group_ordinal";
    case GoldSrcStudioModelSourceBundleErrorCode::duplicate_sequence_group:
        return "duplicate_sequence_group";
    case GoldSrcStudioModelSourceBundleErrorCode::duplicate_virtual_source:
        return "duplicate_virtual_source";
    case GoldSrcStudioModelSourceBundleErrorCode::dependency_plan_mismatch:
        return "dependency_plan_mismatch";
    case GoldSrcStudioModelSourceBundleErrorCode::unable_to_retain_bundle:
        return "unable_to_retain_bundle";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::visual_assets

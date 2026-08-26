#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/entity_visual/entity_visual_binding.hpp>
#include <hlclient/local_resources/local_resource_identity.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::entity_visual {

inline constexpr std::size_t kDefaultMaximumEntityVisualAssetCount = 1'024U;
inline constexpr std::size_t kHardMaximumEntityVisualAssetCount = 4'096U;
inline constexpr std::uint64_t kDefaultMaximumEntityVisualModelSourceBytes =
    512ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kHardMaximumEntityVisualModelSourceBytes =
    2ULL * 1'024ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kDefaultMaximumEntityVisualSpriteSourceBytes =
    256ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kHardMaximumEntityVisualSpriteSourceBytes =
    1ULL * 1'024ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kDefaultMaximumEntityVisualTextureRgbaBytes =
    512ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kHardMaximumEntityVisualTextureRgbaBytes =
    1ULL * 1'024ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kDefaultMaximumEntityVisualGeometryBytes =
    512ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kHardMaximumEntityVisualGeometryBytes =
    1ULL * 1'024ULL * 1'024ULL * 1'024ULL;
inline constexpr std::size_t kDefaultMaximumEntityVisualPendingImports = 64U;
inline constexpr std::size_t kHardMaximumEntityVisualPendingImports = 1'024U;
inline constexpr std::size_t kDefaultMaximumEntityVisualImportsPerUpdate = 8U;
inline constexpr std::size_t kHardMaximumEntityVisualImportsPerUpdate = 64U;
inline constexpr std::size_t kDefaultMaximumEntityVisualLibraryEvents = 4'096U;
inline constexpr std::size_t kHardMaximumEntityVisualLibraryEvents = 65'536U;
inline constexpr std::size_t kMaximumEntityVisualSourceFingerprints = 17U;
inline constexpr std::size_t kMaximumEntityVisualImporterIdBytes =
    assets::kMaximumAssetDispatchImporterIdBytes;
inline constexpr std::size_t kEntityVisualAssetLibraryDiagnosticTextLimit =
    256U;

struct EntityVisualAssetLibraryLimits {
    std::size_t maximum_asset_count{kDefaultMaximumEntityVisualAssetCount};
    std::uint64_t maximum_total_model_source_bytes{
        kDefaultMaximumEntityVisualModelSourceBytes};
    std::uint64_t maximum_total_sprite_source_bytes{
        kDefaultMaximumEntityVisualSpriteSourceBytes};
    std::uint64_t maximum_total_texture_rgba_bytes{
        kDefaultMaximumEntityVisualTextureRgbaBytes};
    std::uint64_t maximum_total_geometry_bytes{
        kDefaultMaximumEntityVisualGeometryBytes};
    std::size_t maximum_pending_imports{
        kDefaultMaximumEntityVisualPendingImports};
    std::size_t maximum_imports_per_update{
        kDefaultMaximumEntityVisualImportsPerUpdate};
    std::size_t maximum_library_events{
        kDefaultMaximumEntityVisualLibraryEvents};
};

[[nodiscard]] bool valid_entity_visual_asset_library_limits(
    const EntityVisualAssetLibraryLimits& limits) noexcept;

// Exact path-free identity available before source bytes are opened. The
// opaque root token carries environment provenance. The resource ID is a
// caller-owned immutable manifest/importer-profile identity and must remain
// stable across incremental revisions.
class EntityVisualApprovedSourceKey final {
public:
    EntityVisualApprovedSourceKey(const EntityVisualApprovedSourceKey&) =
        default;
    EntityVisualApprovedSourceKey(EntityVisualApprovedSourceKey&&) noexcept =
        default;
    EntityVisualApprovedSourceKey& operator=(
        const EntityVisualApprovedSourceKey&) = default;
    EntityVisualApprovedSourceKey& operator=(
        EntityVisualApprovedSourceKey&&) noexcept = default;
    ~EntityVisualApprovedSourceKey() = default;

    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] local_resources::LocalResourceRootId root_id()
        const noexcept;
    [[nodiscard]] local_resources::LocalVirtualResourceId virtual_resource_id()
        const noexcept;
    [[nodiscard]] local_resources::LocalStableFileIdentity stable_identity()
        const noexcept;
    [[nodiscard]] std::uint64_t main_source_byte_count() const noexcept;
    [[nodiscard]] goldsrc::PrecacheManifestCompatibilityProfile
    manifest_profile() const noexcept;
    [[nodiscard]] goldsrc::PrecacheManifestEvidenceProfile manifest_evidence()
        const noexcept;

    [[nodiscard]] friend bool operator==(
        const EntityVisualApprovedSourceKey&,
        const EntityVisualApprovedSourceKey&) noexcept = default;

private:
    friend class EntityVisualAssetLibraryBuilder;

    EntityVisualApprovedSourceKey(
        std::uint64_t resource_id,
        local_resources::LocalResourceRootId root_id,
        local_resources::LocalVirtualResourceId virtual_resource_id,
        local_resources::LocalStableFileIdentity stable_identity,
        std::uint64_t main_source_byte_count,
        goldsrc::PrecacheManifestCompatibilityProfile manifest_profile,
        goldsrc::PrecacheManifestEvidenceProfile manifest_evidence) noexcept;

    std::uint64_t resource_id_{0U};
    local_resources::LocalResourceRootId root_id_;
    local_resources::LocalVirtualResourceId virtual_resource_id_;
    local_resources::LocalStableFileIdentity stable_identity_;
    std::uint64_t main_source_byte_count_{0U};
    goldsrc::PrecacheManifestCompatibilityProfile manifest_profile_{
        goldsrc::PrecacheManifestCompatibilityProfile::
            stock_protocol_48_standard_metadata_only};
    goldsrc::PrecacheManifestEvidenceProfile manifest_evidence_{
        goldsrc::PrecacheManifestEvidenceProfile::
            exact_correlated_local_resource_metadata};
};

enum class EntityVisualAssetCompatibilityProfile {
    goldsrc_studio_v10,
    goldsrc_palette_sprite_v2,
};

// Caller-supplied result of a current approved-source probe. This path-free
// evidence is required before an immutable record may be reused: the locator
// key alone cannot detect same-size in-place main-source changes and does not
// contain Studio companion identities. Fingerprints are ordered main source,
// optional texture companion, then sequence-group sources.
class EntityVisualAssetReuseEvidence final {
public:
    EntityVisualAssetReuseEvidence(
        EntityVisualApprovedSourceKey source_key,
        EntityVisualAssetKind kind,
        assets::AssetImporterCategory importer_category,
        EntityVisualAssetCompatibilityProfile compatibility_profile,
        std::string importer_id,
        std::uint64_t total_source_bytes,
        std::vector<assets::AssetSourceFingerprint> source_fingerprints);

    [[nodiscard]] const EntityVisualApprovedSourceKey& source_key()
        const noexcept;
    [[nodiscard]] EntityVisualAssetKind kind() const noexcept;
    [[nodiscard]] assets::AssetImporterCategory importer_category()
        const noexcept;
    [[nodiscard]] EntityVisualAssetCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] std::string_view importer_id() const noexcept;
    [[nodiscard]] std::uint64_t total_source_bytes() const noexcept;
    [[nodiscard]] std::span<const assets::AssetSourceFingerprint>
    source_fingerprints() const noexcept;

private:
    EntityVisualApprovedSourceKey source_key_;
    EntityVisualAssetKind kind_{EntityVisualAssetKind::studio_model};
    assets::AssetImporterCategory importer_category_{
        assets::AssetImporterCategory::none};
    EntityVisualAssetCompatibilityProfile compatibility_profile_{
        EntityVisualAssetCompatibilityProfile::goldsrc_studio_v10};
    std::string importer_id_;
    std::uint64_t total_source_bytes_{0U};
    std::vector<assets::AssetSourceFingerprint> source_fingerprints_;
};

class EntityVisualAssetRecord final {
public:
    EntityVisualAssetRecord(const EntityVisualAssetRecord&) = default;
    EntityVisualAssetRecord(EntityVisualAssetRecord&&) noexcept = default;
    EntityVisualAssetRecord& operator=(const EntityVisualAssetRecord&) =
        delete;
    EntityVisualAssetRecord& operator=(EntityVisualAssetRecord&&) noexcept =
        delete;
    ~EntityVisualAssetRecord() = default;

    [[nodiscard]] EntityVisualAssetKind kind() const noexcept;
    [[nodiscard]] const std::shared_ptr<const assets::ModelAsset>& model_asset()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<const assets::SpriteAsset>& sprite_asset()
        const noexcept;
    [[nodiscard]] std::uint16_t manifest_model_slot() const noexcept;
    [[nodiscard]] const EntityVisualManifestResourceMetadata&
    manifest_resource() const noexcept;
    [[nodiscard]] const EntityVisualApprovedSourceKey& approved_source_key()
        const noexcept;
    [[nodiscard]] std::span<const assets::AssetSourceFingerprint>
    source_fingerprints() const noexcept;
    [[nodiscard]] assets::AssetSourceFingerprint source_fingerprint()
        const noexcept;
    [[nodiscard]] std::uint64_t total_source_bytes() const noexcept;
    [[nodiscard]] std::uint64_t texture_rgba_bytes() const noexcept;
    [[nodiscard]] std::uint64_t geometry_bytes() const noexcept;
    [[nodiscard]] assets::AssetImporterCategory importer_category()
        const noexcept;
    [[nodiscard]] std::string_view importer_id() const noexcept;
    [[nodiscard]] EntityVisualAssetCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;

private:
    friend class EntityVisualAssetLibraryBuilder;

    EntityVisualAssetRecord(
        EntityVisualAssetKind kind,
        std::shared_ptr<const assets::ModelAsset> model_asset,
        std::shared_ptr<const assets::SpriteAsset> sprite_asset,
        std::uint16_t manifest_model_slot,
        EntityVisualManifestResourceMetadata manifest_resource,
        EntityVisualApprovedSourceKey approved_source_key,
        std::vector<assets::AssetSourceFingerprint> source_fingerprints,
        std::uint64_t total_source_bytes,
        std::uint64_t texture_rgba_bytes,
        std::uint64_t geometry_bytes,
        assets::AssetImporterCategory importer_category,
        std::string importer_id,
        EntityVisualAssetCompatibilityProfile compatibility_profile,
        std::uint64_t resource_id,
        std::uint64_t resource_revision) noexcept;

    EntityVisualAssetKind kind_{EntityVisualAssetKind::studio_model};
    std::shared_ptr<const assets::ModelAsset> model_asset_;
    std::shared_ptr<const assets::SpriteAsset> sprite_asset_;
    std::uint16_t manifest_model_slot_{0U};
    EntityVisualManifestResourceMetadata manifest_resource_{};
    EntityVisualApprovedSourceKey approved_source_key_;
    std::vector<assets::AssetSourceFingerprint> source_fingerprints_;
    std::uint64_t total_source_bytes_{0U};
    std::uint64_t texture_rgba_bytes_{0U};
    std::uint64_t geometry_bytes_{0U};
    assets::AssetImporterCategory importer_category_{
        assets::AssetImporterCategory::none};
    std::string importer_id_;
    EntityVisualAssetCompatibilityProfile compatibility_profile_{
        EntityVisualAssetCompatibilityProfile::goldsrc_studio_v10};
    std::uint64_t resource_id_{0U};
    std::uint64_t resource_revision_{0U};
};

struct EntityVisualAssetLibraryStatistics {
    std::uint64_t asset_count{0U};
    std::uint64_t studio_model_count{0U};
    std::uint64_t sprite_count{0U};
    std::uint64_t reference_count{0U};
    std::uint64_t cumulative_import_request_count{0U};
    std::uint64_t cumulative_imported_asset_count{0U};
    std::uint64_t cumulative_reused_reference_count{0U};
    std::uint64_t cumulative_deduplicated_asset_count{0U};
    std::uint64_t total_model_source_bytes{0U};
    std::uint64_t total_sprite_source_bytes{0U};
    std::uint64_t total_texture_rgba_bytes{0U};
    std::uint64_t total_geometry_bytes{0U};
    std::uint64_t cumulative_library_events{0U};
};

struct EntityVisualAssetReferenceEntry {
    EntityVisualModelReference reference{
        EntityVisualModelReference::synthetic_model_slot(0U)};
    std::size_t asset_library_index{0U};
};

class EntityVisualAssetLibraryState final {
public:
    EntityVisualAssetLibraryState(const EntityVisualAssetLibraryState&) =
        default;
    EntityVisualAssetLibraryState(EntityVisualAssetLibraryState&&) noexcept =
        default;
    EntityVisualAssetLibraryState& operator=(
        const EntityVisualAssetLibraryState&) = delete;
    EntityVisualAssetLibraryState& operator=(
        EntityVisualAssetLibraryState&&) noexcept = delete;
    ~EntityVisualAssetLibraryState() = default;

    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;
    [[nodiscard]] std::span<const EntityVisualAssetRecord> records()
        const noexcept;
    [[nodiscard]] std::span<const EntityVisualAssetReferenceEntry> references()
        const noexcept;
    [[nodiscard]] const EntityVisualAssetRecord* find_exact(
        const EntityVisualModelReference& reference) const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_exact_index(
        const EntityVisualModelReference& reference) const noexcept;
    [[nodiscard]] const EntityVisualAssetLibraryStatistics& statistics()
        const noexcept;

private:
    friend class EntityVisualAssetLibraryBuilder;

    EntityVisualAssetLibraryState(
        std::uint64_t resource_id,
        std::uint64_t resource_revision,
        std::vector<EntityVisualAssetRecord> records,
        std::vector<EntityVisualAssetReferenceEntry> references,
        EntityVisualAssetLibraryStatistics statistics) noexcept;

    std::uint64_t resource_id_{0U};
    std::uint64_t resource_revision_{0U};
    std::vector<EntityVisualAssetRecord> records_;
    std::vector<EntityVisualAssetReferenceEntry> references_;
    EntityVisualAssetLibraryStatistics statistics_{};
};

class EntityVisualAssetImportRequest final {
public:
    EntityVisualAssetImportRequest(const EntityVisualAssetImportRequest&) =
        default;
    EntityVisualAssetImportRequest(EntityVisualAssetImportRequest&&) noexcept =
        default;
    EntityVisualAssetImportRequest& operator=(
        const EntityVisualAssetImportRequest&) = delete;
    EntityVisualAssetImportRequest& operator=(
        EntityVisualAssetImportRequest&&) noexcept = delete;
    ~EntityVisualAssetImportRequest() = default;

    [[nodiscard]] std::size_t request_index() const noexcept;
    [[nodiscard]] const EntityVisualApprovedSourceKey& source_key()
        const noexcept;
    [[nodiscard]] std::size_t manifest_entry_offset() const noexcept;
    [[nodiscard]] std::uint16_t model_slot() const noexcept;
    [[nodiscard]] std::span<const EntityVisualModelReference> references()
        const noexcept;

private:
    friend class EntityVisualAssetLibraryBuilder;

    EntityVisualAssetImportRequest(
        std::size_t request_index,
        EntityVisualApprovedSourceKey source_key,
        std::size_t manifest_entry_offset,
        std::uint16_t model_slot,
        std::vector<EntityVisualModelReference> references) noexcept;

    std::size_t request_index_{0U};
    EntityVisualApprovedSourceKey source_key_;
    std::size_t manifest_entry_offset_{0U};
    std::uint16_t model_slot_{0U};
    std::vector<EntityVisualModelReference> references_;
};

struct EntityVisualAssetLibraryPlanEntry {
    EntityVisualModelSlotResolution resolution;
    std::optional<std::size_t> existing_asset_library_index;
    std::optional<std::size_t> import_request_index;
};

class EntityVisualAssetLibraryPlan final {
public:
    EntityVisualAssetLibraryPlan(const EntityVisualAssetLibraryPlan&) = default;
    EntityVisualAssetLibraryPlan(EntityVisualAssetLibraryPlan&&) noexcept =
        default;
    EntityVisualAssetLibraryPlan& operator=(
        const EntityVisualAssetLibraryPlan&) = delete;
    EntityVisualAssetLibraryPlan& operator=(
        EntityVisualAssetLibraryPlan&&) noexcept = delete;
    ~EntityVisualAssetLibraryPlan() = default;

    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::span<const EntityVisualAssetLibraryPlanEntry> entries()
        const noexcept;
    [[nodiscard]] std::span<const EntityVisualAssetImportRequest> requests()
        const noexcept;
    [[nodiscard]] std::size_t unique_reference_count() const noexcept;
    [[nodiscard]] std::size_t duplicate_reference_count() const noexcept;

private:
    friend class EntityVisualAssetLibraryBuilder;

    EntityVisualAssetLibraryPlan(
        std::uint64_t resource_id,
        std::vector<EntityVisualAssetLibraryPlanEntry> entries,
        std::vector<EntityVisualAssetImportRequest> requests,
        std::size_t duplicate_reference_count) noexcept;

    std::uint64_t resource_id_{0U};
    std::vector<EntityVisualAssetLibraryPlanEntry> entries_;
    std::vector<EntityVisualAssetImportRequest> requests_;
    std::size_t duplicate_reference_count_{0U};
};

enum class EntityVisualAssetImportCompletionStatus {
    imported,
    unsupported_asset_format,
    asset_import_failed,
    asset_dependency_missing,
    asset_ambiguous,
    asset_limit_exceeded,
};

class EntityVisualImportedAssetCandidate final {
public:
    [[nodiscard]] static EntityVisualImportedAssetCandidate studio_model(
        EntityVisualApprovedSourceKey source_key,
        std::shared_ptr<const assets::ModelAsset> asset,
        std::string importer_id,
        std::uint64_t total_source_bytes,
        std::vector<assets::AssetSourceFingerprint> source_fingerprints);
    [[nodiscard]] static EntityVisualImportedAssetCandidate sprite(
        EntityVisualApprovedSourceKey source_key,
        std::shared_ptr<const assets::SpriteAsset> asset,
        std::string importer_id,
        std::uint64_t total_source_bytes,
        std::vector<assets::AssetSourceFingerprint> source_fingerprints);

    EntityVisualImportedAssetCandidate(
        const EntityVisualImportedAssetCandidate&) = default;
    EntityVisualImportedAssetCandidate(
        EntityVisualImportedAssetCandidate&&) noexcept = default;
    EntityVisualImportedAssetCandidate& operator=(
        const EntityVisualImportedAssetCandidate&) = delete;
    EntityVisualImportedAssetCandidate& operator=(
        EntityVisualImportedAssetCandidate&&) noexcept = delete;
    ~EntityVisualImportedAssetCandidate() = default;

    [[nodiscard]] EntityVisualAssetKind kind() const noexcept;
    [[nodiscard]] const EntityVisualApprovedSourceKey& source_key()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<const assets::ModelAsset>& model_asset()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<const assets::SpriteAsset>& sprite_asset()
        const noexcept;
    [[nodiscard]] std::string_view importer_id() const noexcept;
    [[nodiscard]] std::uint64_t total_source_bytes() const noexcept;
    [[nodiscard]] std::span<const assets::AssetSourceFingerprint>
    source_fingerprints() const noexcept;

private:
    EntityVisualImportedAssetCandidate(
        EntityVisualAssetKind kind,
        EntityVisualApprovedSourceKey source_key,
        std::shared_ptr<const assets::ModelAsset> model_asset,
        std::shared_ptr<const assets::SpriteAsset> sprite_asset,
        std::string importer_id,
        std::uint64_t total_source_bytes,
        std::vector<assets::AssetSourceFingerprint> source_fingerprints)
        noexcept;

    EntityVisualAssetKind kind_{EntityVisualAssetKind::studio_model};
    EntityVisualApprovedSourceKey source_key_;
    std::shared_ptr<const assets::ModelAsset> model_asset_;
    std::shared_ptr<const assets::SpriteAsset> sprite_asset_;
    std::string importer_id_;
    std::uint64_t total_source_bytes_{0U};
    std::vector<assets::AssetSourceFingerprint> source_fingerprints_;
};

struct EntityVisualAssetImportCompletion {
    std::size_t request_index{0U};
    EntityVisualAssetImportCompletionStatus status{
        EntityVisualAssetImportCompletionStatus::asset_import_failed};
    std::optional<EntityVisualImportedAssetCandidate> candidate;
};

enum class EntityVisualAssetLibraryErrorCode {
    invalid_configuration,
    invalid_resource_id,
    previous_library_mismatch,
    projection_reference_limit_exceeded,
    pending_import_limit_exceeded,
    imports_per_update_limit_exceeded,
    library_event_limit_exceeded,
    invalid_manifest_source_identity,
    invalid_plan,
    missing_import_completion,
    duplicate_import_completion,
    imported_source_mismatch,
    invalid_imported_asset,
    exact_source_identity_conflict,
    asset_count_limit_exceeded,
    model_source_byte_limit_exceeded,
    sprite_source_byte_limit_exceeded,
    texture_rgba_byte_limit_exceeded,
    geometry_byte_limit_exceeded,
    revision_overflow,
    size_overflow,
    unable_to_retain_library,
};

struct EntityVisualAssetLibraryError {
    EntityVisualAssetLibraryErrorCode code{
        EntityVisualAssetLibraryErrorCode::invalid_configuration};
    std::optional<EntityVisualModelReference> model_reference;
    std::optional<std::size_t> request_index;
    std::string context;
};

struct EntityVisualAssetLibraryPlanResult {
    std::optional<EntityVisualAssetLibraryPlan> plan;
    std::optional<EntityVisualAssetLibraryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return plan.has_value();
    }
};

struct EntityVisualAssetLibraryBuildResult {
    std::shared_ptr<const EntityVisualAssetLibraryState> library;
    std::vector<EntityVisualBindingState> bindings;
    std::optional<EntityVisualAssetLibraryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return library != nullptr && !error.has_value();
    }
};

class EntityVisualAssetLibraryBuilder final {
public:
    [[nodiscard]] EntityVisualAssetLibraryPlanResult plan(
        std::uint64_t resource_id,
        std::span<const EntityVisualProjectionState> previous_projections,
        std::span<const EntityVisualProjectionState> current_projections,
        const goldsrc::PrecacheManifestState& manifest,
        const IEntityVisualModelReferenceResolver& resolver,
        std::shared_ptr<const EntityVisualAssetLibraryState> previous_library =
            {},
        EntityVisualAssetLibraryLimits limits = {}) const noexcept;

    [[nodiscard]] EntityVisualAssetLibraryPlanResult plan(
        std::uint64_t resource_id,
        std::span<const EntityVisualProjectionState> previous_projections,
        std::span<const EntityVisualProjectionState> current_projections,
        const goldsrc::PrecacheManifestState& manifest,
        const IEntityVisualModelReferenceResolver& resolver,
        std::shared_ptr<const EntityVisualAssetLibraryState> previous_library,
        EntityVisualAssetLibraryLimits limits,
        std::span<const EntityVisualAssetReuseEvidence> reuse_evidence)
        const noexcept;

    [[nodiscard]] EntityVisualAssetLibraryBuildResult publish(
        const EntityVisualAssetLibraryPlan& plan,
        std::span<const EntityVisualAssetImportCompletion> completions,
        std::shared_ptr<const EntityVisualAssetLibraryState> previous_library =
            {},
        EntityVisualAssetLibraryLimits limits = {}) const noexcept;
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityVisualAssetLibraryErrorCode code) noexcept
{
    switch (code) {
    case EntityVisualAssetLibraryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntityVisualAssetLibraryErrorCode::invalid_resource_id:
        return "invalid_resource_id";
    case EntityVisualAssetLibraryErrorCode::previous_library_mismatch:
        return "previous_library_mismatch";
    case EntityVisualAssetLibraryErrorCode::projection_reference_limit_exceeded:
        return "projection_reference_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::pending_import_limit_exceeded:
        return "pending_import_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::imports_per_update_limit_exceeded:
        return "imports_per_update_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::library_event_limit_exceeded:
        return "library_event_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::invalid_manifest_source_identity:
        return "invalid_manifest_source_identity";
    case EntityVisualAssetLibraryErrorCode::invalid_plan:
        return "invalid_plan";
    case EntityVisualAssetLibraryErrorCode::missing_import_completion:
        return "missing_import_completion";
    case EntityVisualAssetLibraryErrorCode::duplicate_import_completion:
        return "duplicate_import_completion";
    case EntityVisualAssetLibraryErrorCode::imported_source_mismatch:
        return "imported_source_mismatch";
    case EntityVisualAssetLibraryErrorCode::invalid_imported_asset:
        return "invalid_imported_asset";
    case EntityVisualAssetLibraryErrorCode::exact_source_identity_conflict:
        return "exact_source_identity_conflict";
    case EntityVisualAssetLibraryErrorCode::asset_count_limit_exceeded:
        return "asset_count_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::model_source_byte_limit_exceeded:
        return "model_source_byte_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::sprite_source_byte_limit_exceeded:
        return "sprite_source_byte_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::texture_rgba_byte_limit_exceeded:
        return "texture_rgba_byte_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::geometry_byte_limit_exceeded:
        return "geometry_byte_limit_exceeded";
    case EntityVisualAssetLibraryErrorCode::revision_overflow:
        return "revision_overflow";
    case EntityVisualAssetLibraryErrorCode::size_overflow:
        return "size_overflow";
    case EntityVisualAssetLibraryErrorCode::unable_to_retain_library:
        return "unable_to_retain_library";
    }
    return "unknown";
}

} // namespace hlclient::entity_visual

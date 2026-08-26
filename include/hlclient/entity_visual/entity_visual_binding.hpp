#pragma once

#include <hlclient/assets/asset_importer_dispatcher.hpp>
#include <hlclient/entity_visual/entity_visual_projection.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::entity_visual {

class EntityVisualAssetLibraryBuilder;

enum class EntityVisualModelResolutionStatus {
    resolved_model_slot,
    missing_model_slot,
    manifest_entry_not_ready,
    stock_modelindex_mapping_evidence_pending,
    invalid_model_reference,
    invalid_manifest_entry,
};

enum class EntityVisualModelResolutionEvidenceProfile {
    exact_synthetic_type_local_model_slot,
    stock_modelindex_mapping_pending,
};

struct EntityVisualManifestResourceMetadata {
    std::size_t wire_ordinal{0U};
    goldsrc::ResourceType resource_type{goldsrc::ResourceType::model};
    std::uint16_t resource_index{0U};
    std::uint16_t type_local_slot{0U};
    goldsrc::LocalResourceReadinessStatus readiness_status{
        goldsrc::LocalResourceReadinessStatus::local_io_error};
    goldsrc::LocalResourceReadinessImpact readiness_impact{
        goldsrc::LocalResourceReadinessImpact::local_io_failure};
    std::optional<std::uint64_t> local_file_size;
    goldsrc::PrecacheManifestCompatibilityProfile manifest_profile{
        goldsrc::PrecacheManifestCompatibilityProfile::
            stock_protocol_48_standard_metadata_only};
    goldsrc::PrecacheManifestEvidenceProfile manifest_evidence{
        goldsrc::PrecacheManifestEvidenceProfile::
            exact_correlated_local_resource_metadata};
};

struct EntityVisualModelSlotResolution {
    EntityVisualModelResolutionStatus status{
        EntityVisualModelResolutionStatus::invalid_model_reference};
    EntityVisualModelReference reference{
        EntityVisualModelReference::synthetic_model_slot(0U)};
    std::optional<std::uint16_t> model_slot;
    std::optional<std::size_t> manifest_entry_offset;
    std::optional<EntityVisualManifestResourceMetadata> resource;
    EntityVisualModelResolutionEvidenceProfile evidence_profile{
        EntityVisualModelResolutionEvidenceProfile::
            exact_synthetic_type_local_model_slot};
    std::string context;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return status ==
                   EntityVisualModelResolutionStatus::resolved_model_slot &&
               model_slot.has_value() && manifest_entry_offset.has_value() &&
               resource.has_value();
    }
};

class IEntityVisualModelReferenceResolver {
public:
    virtual ~IEntityVisualModelReferenceResolver() = default;

    [[nodiscard]] virtual EntityVisualModelSlotResolution resolve(
        const EntityVisualModelReference& reference,
        const goldsrc::PrecacheManifestState& manifest) const = 0;
};

class SyntheticModelSlotResolver final
    : public IEntityVisualModelReferenceResolver {
public:
    [[nodiscard]] EntityVisualModelSlotResolution resolve(
        const EntityVisualModelReference& reference,
        const goldsrc::PrecacheManifestState& manifest) const override;
};

class EvidencePendingStockModelResolver final
    : public IEntityVisualModelReferenceResolver {
public:
    [[nodiscard]] EntityVisualModelSlotResolution resolve(
        const EntityVisualModelReference& reference,
        const goldsrc::PrecacheManifestState& manifest) const override;
};

enum class EntityVisualAssetKind {
    studio_model,
    sprite,
};

enum class EntityVisualBindingCategory {
    studio_model,
    sprite,
    unsupported,
    missing,
    unsafe,
    ambiguous,
    import_failed,
};

enum class EntityVisualBindingStatus {
    resolved_studio_model,
    resolved_sprite,
    missing_model_slot,
    manifest_entry_not_ready,
    visual_projection_pending,
    unsupported_asset_format,
    asset_import_failed,
    asset_dependency_missing,
    asset_ambiguous,
    asset_limit_exceeded,
};

enum class EntityVisualBindingEvidenceProfile {
    exact_synthetic_model_slot_and_approved_source,
    stock_visual_mapping_pending,
};

class EntityVisualBindingState final {
public:
    EntityVisualBindingState(const EntityVisualBindingState&) = default;
    EntityVisualBindingState(EntityVisualBindingState&&) noexcept = default;
    EntityVisualBindingState& operator=(const EntityVisualBindingState&) =
        delete;
    EntityVisualBindingState& operator=(EntityVisualBindingState&&) noexcept =
        delete;
    ~EntityVisualBindingState() = default;

    [[nodiscard]] const EntityVisualModelReference& model_reference()
        const noexcept;
    [[nodiscard]] const std::optional<std::uint16_t>& model_slot()
        const noexcept;
    [[nodiscard]] const std::optional<EntityVisualManifestResourceMetadata>&
    manifest_resource() const noexcept;
    [[nodiscard]] EntityVisualBindingCategory selected_category()
        const noexcept;
    [[nodiscard]] const std::optional<std::size_t>& asset_library_index()
        const noexcept;
    [[nodiscard]] EntityVisualBindingStatus status() const noexcept;
    [[nodiscard]] const std::optional<assets::AssetSourceFingerprint>&
    source_fingerprint() const noexcept;
    [[nodiscard]] EntityVisualBindingEvidenceProfile evidence_profile()
        const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;

    // Public construction supports pure composition/test adapters. Every
    // field remains read-only after construction; the library builder is the
    // production validation boundary.
    EntityVisualBindingState(
        EntityVisualModelReference model_reference,
        std::optional<std::uint16_t> model_slot,
        std::optional<EntityVisualManifestResourceMetadata> manifest_resource,
        EntityVisualBindingCategory selected_category,
        std::optional<std::size_t> asset_library_index,
        EntityVisualBindingStatus status,
        std::optional<assets::AssetSourceFingerprint> source_fingerprint,
        EntityVisualBindingEvidenceProfile evidence_profile,
        std::uint64_t resource_id,
        std::uint64_t resource_revision) noexcept;

private:
    friend class EntityVisualAssetLibraryBuilder;

    EntityVisualModelReference model_reference_;
    std::optional<std::uint16_t> model_slot_;
    std::optional<EntityVisualManifestResourceMetadata> manifest_resource_;
    EntityVisualBindingCategory selected_category_{
        EntityVisualBindingCategory::missing};
    std::optional<std::size_t> asset_library_index_;
    EntityVisualBindingStatus status_{
        EntityVisualBindingStatus::missing_model_slot};
    std::optional<assets::AssetSourceFingerprint> source_fingerprint_;
    EntityVisualBindingEvidenceProfile evidence_profile_{
        EntityVisualBindingEvidenceProfile::
            exact_synthetic_model_slot_and_approved_source};
    std::uint64_t resource_id_{0U};
    std::uint64_t resource_revision_{0U};
};

[[nodiscard]] constexpr std::string_view to_string(
    EntityVisualModelResolutionStatus status) noexcept
{
    switch (status) {
    case EntityVisualModelResolutionStatus::resolved_model_slot:
        return "resolved_model_slot";
    case EntityVisualModelResolutionStatus::missing_model_slot:
        return "missing_model_slot";
    case EntityVisualModelResolutionStatus::manifest_entry_not_ready:
        return "manifest_entry_not_ready";
    case EntityVisualModelResolutionStatus::
        stock_modelindex_mapping_evidence_pending:
        return "stock_modelindex_mapping_evidence_pending";
    case EntityVisualModelResolutionStatus::invalid_model_reference:
        return "invalid_model_reference";
    case EntityVisualModelResolutionStatus::invalid_manifest_entry:
        return "invalid_manifest_entry";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    EntityVisualBindingStatus status) noexcept
{
    switch (status) {
    case EntityVisualBindingStatus::resolved_studio_model:
        return "resolved_studio_model";
    case EntityVisualBindingStatus::resolved_sprite:
        return "resolved_sprite";
    case EntityVisualBindingStatus::missing_model_slot:
        return "missing_model_slot";
    case EntityVisualBindingStatus::manifest_entry_not_ready:
        return "manifest_entry_not_ready";
    case EntityVisualBindingStatus::visual_projection_pending:
        return "visual_projection_pending";
    case EntityVisualBindingStatus::unsupported_asset_format:
        return "unsupported_asset_format";
    case EntityVisualBindingStatus::asset_import_failed:
        return "asset_import_failed";
    case EntityVisualBindingStatus::asset_dependency_missing:
        return "asset_dependency_missing";
    case EntityVisualBindingStatus::asset_ambiguous:
        return "asset_ambiguous";
    case EntityVisualBindingStatus::asset_limit_exceeded:
        return "asset_limit_exceeded";
    }
    return "unknown";
}

} // namespace hlclient::entity_visual

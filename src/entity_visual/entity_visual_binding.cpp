#include <hlclient/entity_visual/entity_visual_binding.hpp>

#include <limits>

namespace hlclient::entity_visual {
namespace {

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    return std::string{context.substr(
        0U, kEntityVisualProjectionDiagnosticTextLimit)};
}

[[nodiscard]] EntityVisualModelSlotResolution resolution_failure(
    const EntityVisualModelResolutionStatus status,
    const EntityVisualModelReference& reference,
    const EntityVisualModelResolutionEvidenceProfile evidence,
    const std::string_view context)
{
    return EntityVisualModelSlotResolution{
        status,
        reference,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        evidence,
        bounded_context(context)};
}

[[nodiscard]] EntityVisualManifestResourceMetadata metadata(
    const goldsrc::PrecacheManifestEntry& entry) noexcept
{
    return EntityVisualManifestResourceMetadata{
        entry.wire_ordinal(),
        entry.resource_type(),
        entry.resource_index(),
        entry.type_local_slot(),
        entry.readiness_status(),
        entry.readiness_impact(),
        entry.local_file_size(),
        entry.compatibility_profile(),
        entry.evidence_profile()};
}

} // namespace

EntityVisualModelSlotResolution SyntheticModelSlotResolver::resolve(
    const EntityVisualModelReference& reference,
    const goldsrc::PrecacheManifestState& manifest) const
{
    constexpr auto evidence = EntityVisualModelResolutionEvidenceProfile::
        exact_synthetic_type_local_model_slot;
    if (reference.profile() != EntityVisualModelReferenceProfile::
                                   synthetic_type_local_model_slot ||
        reference.value() > std::numeric_limits<std::uint16_t>::max()) {
        return resolution_failure(
            EntityVisualModelResolutionStatus::invalid_model_reference,
            reference,
            evidence,
            "Synthetic references must be exact uint16 model-slot indices");
    }

    const auto slot = static_cast<std::uint16_t>(reference.value());
    const auto entry_offset = manifest.model_slots().entry_offset(slot);
    if (!entry_offset) {
        return resolution_failure(
            EntityVisualModelResolutionStatus::missing_model_slot,
            reference,
            evidence,
            "The exact manifest model slot is unoccupied");
    }
    if (*entry_offset >= manifest.entries().size()) {
        return resolution_failure(
            EntityVisualModelResolutionStatus::invalid_manifest_entry,
            reference,
            evidence,
            "The model slot points outside the immutable manifest");
    }
    const auto& entry = manifest.entries()[*entry_offset];
    if (entry.resource_type() != goldsrc::ResourceType::model ||
        entry.resource_index() != slot || entry.type_local_slot() != slot) {
        return resolution_failure(
            EntityVisualModelResolutionStatus::invalid_manifest_entry,
            reference,
            evidence,
            "The model slot and manifest entry metadata disagree");
    }

    const auto resource = metadata(entry);
    if (entry.readiness_status() !=
            goldsrc::LocalResourceReadinessStatus::ready_local_file ||
        entry.readiness_impact() !=
            goldsrc::LocalResourceReadinessImpact::locally_usable ||
        !entry.locator() || !entry.local_file_size()) {
        return EntityVisualModelSlotResolution{
            EntityVisualModelResolutionStatus::manifest_entry_not_ready,
            reference,
            slot,
            *entry_offset,
            resource,
            evidence,
            bounded_context(
                "The exact manifest model entry is not a ready local file")};
    }

    return EntityVisualModelSlotResolution{
        EntityVisualModelResolutionStatus::resolved_model_slot,
        reference,
        slot,
        *entry_offset,
        resource,
        evidence,
        {}};
}

EntityVisualModelSlotResolution EvidencePendingStockModelResolver::resolve(
    const EntityVisualModelReference& reference,
    const goldsrc::PrecacheManifestState&) const
{
    return resolution_failure(
        EntityVisualModelResolutionStatus::
            stock_modelindex_mapping_evidence_pending,
        reference,
        EntityVisualModelResolutionEvidenceProfile::
            stock_modelindex_mapping_pending,
        "Stock Protocol 48 modelindex mapping remains evidence pending");
}

EntityVisualBindingState::EntityVisualBindingState(
    EntityVisualModelReference model_reference,
    std::optional<std::uint16_t> model_slot,
    std::optional<EntityVisualManifestResourceMetadata> manifest_resource,
    const EntityVisualBindingCategory selected_category,
    std::optional<std::size_t> asset_library_index,
    const EntityVisualBindingStatus status,
    std::optional<assets::AssetSourceFingerprint> source_fingerprint,
    const EntityVisualBindingEvidenceProfile evidence_profile,
    const std::uint64_t resource_id,
    const std::uint64_t resource_revision) noexcept
    : model_reference_{std::move(model_reference)},
      model_slot_{model_slot},
      manifest_resource_{std::move(manifest_resource)},
      selected_category_{selected_category},
      asset_library_index_{asset_library_index},
      status_{status},
      source_fingerprint_{source_fingerprint},
      evidence_profile_{evidence_profile},
      resource_id_{resource_id},
      resource_revision_{resource_revision}
{
}

const EntityVisualModelReference& EntityVisualBindingState::model_reference()
    const noexcept
{
    return model_reference_;
}

const std::optional<std::uint16_t>& EntityVisualBindingState::model_slot()
    const noexcept
{
    return model_slot_;
}

const std::optional<EntityVisualManifestResourceMetadata>&
EntityVisualBindingState::manifest_resource() const noexcept
{
    return manifest_resource_;
}

EntityVisualBindingCategory EntityVisualBindingState::selected_category()
    const noexcept
{
    return selected_category_;
}

const std::optional<std::size_t>&
EntityVisualBindingState::asset_library_index() const noexcept
{
    return asset_library_index_;
}

EntityVisualBindingStatus EntityVisualBindingState::status() const noexcept
{
    return status_;
}

const std::optional<assets::AssetSourceFingerprint>&
EntityVisualBindingState::source_fingerprint() const noexcept
{
    return source_fingerprint_;
}

EntityVisualBindingEvidenceProfile EntityVisualBindingState::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

std::uint64_t EntityVisualBindingState::resource_id() const noexcept
{
    return resource_id_;
}

std::uint64_t EntityVisualBindingState::resource_revision() const noexcept
{
    return resource_revision_;
}

} // namespace hlclient::entity_visual

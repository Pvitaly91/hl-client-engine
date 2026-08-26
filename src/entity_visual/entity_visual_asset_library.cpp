#include <hlclient/entity_visual/entity_visual_asset_library.hpp>

#include <hlclient/assets/model_asset_types.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::entity_visual {
namespace {

class RecordIdentityHasher final {
public:
    void add(const std::uint64_t value) noexcept
    {
        for (std::size_t byte_index = 0U; byte_index < 8U; ++byte_index) {
            value_ ^= static_cast<std::uint8_t>(
                value >> (byte_index * 8U));
            value_ *= 1'099'511'628'211ULL;
        }
    }

    void add(const std::string_view value) noexcept
    {
        add(static_cast<std::uint64_t>(value.size()));
        for (const auto byte : value) {
            value_ ^= static_cast<std::uint8_t>(byte);
            value_ *= 1'099'511'628'211ULL;
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? 14'695'981'039'346'656'037ULL : value_;
    }

private:
    std::uint64_t value_{14'695'981'039'346'656'037ULL};
};

[[nodiscard]] std::uint64_t record_resource_id(
    const EntityVisualImportedAssetCandidate& candidate,
    const std::span<const EntityVisualAssetRecord> existing_records) noexcept
{
    RecordIdentityHasher base;
    base.add(candidate.source_key().resource_id());
    base.add(candidate.source_key().root_id().value());
    base.add(candidate.source_key().virtual_resource_id().value());
    base.add(candidate.source_key().main_source_byte_count());
    base.add(static_cast<std::uint64_t>(candidate.kind()));
    base.add(candidate.importer_id());
    base.add(candidate.total_source_bytes());
    for (const auto& fingerprint : candidate.source_fingerprints()) {
        base.add(fingerprint.primary);
        base.add(fingerprint.secondary);
    }

    auto result = base.value();
    std::uint64_t salt = 0U;
    while (std::ranges::any_of(existing_records,
        [result](const EntityVisualAssetRecord& record) {
            return record.resource_id() == result;
        })) {
        RecordIdentityHasher collision;
        collision.add(base.value());
        collision.add(++salt);
        result = collision.value();
    }
    return result;
}

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    return std::string{context.substr(
        0U, kEntityVisualAssetLibraryDiagnosticTextLimit)};
}

[[nodiscard]] EntityVisualAssetLibraryError make_error(
    const EntityVisualAssetLibraryErrorCode code,
    const std::string_view context,
    std::optional<EntityVisualModelReference> reference = std::nullopt,
    std::optional<std::size_t> request_index = std::nullopt)
{
    return EntityVisualAssetLibraryError{
        code,
        std::move(reference),
        request_index,
        bounded_context(context)};
}

[[nodiscard]] EntityVisualAssetLibraryPlanResult plan_failure(
    EntityVisualAssetLibraryError error)
{
    return EntityVisualAssetLibraryPlanResult{
        std::nullopt, std::move(error)};
}

[[nodiscard]] EntityVisualAssetLibraryBuildResult build_failure(
    EntityVisualAssetLibraryError error)
{
    return EntityVisualAssetLibraryBuildResult{
        nullptr, {}, std::move(error)};
}

[[nodiscard]] bool reference_less(
    const EntityVisualModelReference& left,
    const EntityVisualModelReference& right) noexcept
{
    if (left.profile() != right.profile()) {
        return static_cast<unsigned>(left.profile()) <
               static_cast<unsigned>(right.profile());
    }
    return left.value() < right.value();
}

[[nodiscard]] bool add_overflows(
    const std::uint64_t left,
    const std::uint64_t right) noexcept
{
    return right > std::numeric_limits<std::uint64_t>::max() - left;
}

[[nodiscard]] bool multiply_size(
    const std::size_t count,
    const std::size_t element_size,
    std::uint64_t& result) noexcept
{
    if (element_size != 0U &&
        count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        return false;
    }
    result = static_cast<std::uint64_t>(count) * element_size;
    return true;
}

[[nodiscard]] bool add_size(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t element_size) noexcept
{
    std::uint64_t bytes = 0U;
    if (!multiply_size(count, element_size, bytes) ||
        add_overflows(total, bytes)) {
        return false;
    }
    total += bytes;
    return true;
}

struct AssetByteCounts {
    std::uint64_t texture_rgba_bytes{0U};
    std::uint64_t geometry_bytes{0U};
};

[[nodiscard]] std::optional<AssetByteCounts> model_byte_counts(
    const assets::ModelAsset& asset) noexcept
{
    AssetByteCounts counts;
    if (!add_size(
            counts.geometry_bytes,
            asset.vertices.size(),
            sizeof(assets::ModelVertex)) ||
        !add_size(
            counts.geometry_bytes,
            asset.indices.size(),
            sizeof(std::uint32_t))) {
        return std::nullopt;
    }
    if (!asset.skeletal_data) {
        return std::nullopt;
    }
    for (const auto& submodel : asset.skeletal_data->submodels) {
        if (!add_size(
                counts.geometry_bytes,
                submodel.vertices.size(),
                sizeof(assets::ModelSkinnedVertex)) ||
            !add_size(
                counts.geometry_bytes,
                submodel.indices.size(),
                sizeof(std::uint32_t))) {
            return std::nullopt;
        }
    }
    for (const auto& texture : asset.skeletal_data->textures) {
        if (add_overflows(
                counts.texture_rgba_bytes,
                static_cast<std::uint64_t>(texture.rgba8_level_zero.size()))) {
            return std::nullopt;
        }
        counts.texture_rgba_bytes += texture.rgba8_level_zero.size();
    }
    return counts;
}

[[nodiscard]] std::optional<AssetByteCounts> sprite_byte_counts(
    const assets::SpriteAsset& asset) noexcept
{
    if (!asset.source_data ||
        asset.source_data->compatibility_profile !=
            assets::SpriteSourceCompatibilityProfile::
                goldsrc_palette_sprite_v2) {
        return std::nullopt;
    }
    AssetByteCounts counts;
    for (const auto& frame : asset.frames) {
        if (add_overflows(
                counts.texture_rgba_bytes,
                static_cast<std::uint64_t>(frame.image.pixels.size()))) {
            return std::nullopt;
        }
        counts.texture_rgba_bytes += frame.image.pixels.size();
    }
    for (const auto& frame : asset.source_data->indexed_frames) {
        if (add_overflows(
                counts.texture_rgba_bytes,
                static_cast<std::uint64_t>(frame.derived_rgba8.size()))) {
            return std::nullopt;
        }
        counts.texture_rgba_bytes += frame.derived_rgba8.size();
    }
    return counts;
}

[[nodiscard]] bool exact_reuse_identity(
    const EntityVisualAssetRecord& record,
    const EntityVisualAssetReuseEvidence& evidence) noexcept
{
    if (record.approved_source_key() != evidence.source_key() ||
        record.kind() != evidence.kind() ||
        record.importer_category() != evidence.importer_category() ||
        record.compatibility_profile() != evidence.compatibility_profile() ||
        record.importer_id() != evidence.importer_id() ||
        record.total_source_bytes() != evidence.total_source_bytes()) {
        return false;
    }
    const auto retained = record.source_fingerprints();
    const auto current = evidence.source_fingerprints();
    return retained.size() == current.size() &&
           std::ranges::equal(retained, current);
}

[[nodiscard]] const EntityVisualAssetReuseEvidence* find_reuse_evidence(
    const std::span<const EntityVisualAssetReuseEvidence> evidence,
    const EntityVisualApprovedSourceKey& source_key) noexcept
{
    const auto found = std::ranges::find_if(
        evidence,
        [&source_key](const EntityVisualAssetReuseEvidence& candidate) {
            return candidate.source_key() == source_key;
        });
    return found == evidence.end() ? nullptr : std::addressof(*found);
}

[[nodiscard]] const EntityVisualAssetRecord* find_reusable_record(
    const EntityVisualAssetLibraryState& library,
    const EntityVisualAssetReuseEvidence& evidence,
    std::size_t& record_index) noexcept
{
    const auto records = library.records();
    const auto found = std::ranges::find_if(
        records,
        [&evidence](const EntityVisualAssetRecord& record) {
            return exact_reuse_identity(record, evidence);
        });
    if (found == records.end()) {
        return nullptr;
    }
    record_index = static_cast<std::size_t>(found - records.begin());
    return std::addressof(*found);
}

[[nodiscard]] bool exact_candidate_identity(
    const EntityVisualAssetRecord& record,
    const EntityVisualImportedAssetCandidate& candidate) noexcept
{
    if (record.approved_source_key() != candidate.source_key() ||
        record.kind() != candidate.kind() ||
        record.importer_id() != candidate.importer_id() ||
        record.total_source_bytes() != candidate.total_source_bytes()) {
        return false;
    }
    const auto left = record.source_fingerprints();
    const auto right = candidate.source_fingerprints();
    return left.size() == right.size() &&
           std::ranges::equal(left, right);
}

[[nodiscard]] std::optional<std::size_t> find_reference_index(
    const std::vector<EntityVisualAssetReferenceEntry>& references,
    const EntityVisualModelReference& reference) noexcept
{
    const auto found = std::lower_bound(
        references.begin(),
        references.end(),
        reference,
        [](const EntityVisualAssetReferenceEntry& candidate,
           const EntityVisualModelReference& value) {
            return reference_less(candidate.reference, value);
        });
    if (found == references.end() || found->reference != reference) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - references.begin());
}

[[nodiscard]] EntityVisualBindingCategory category_for_readiness(
    const EntityVisualModelSlotResolution& resolution) noexcept
{
    if (resolution.status ==
        EntityVisualModelResolutionStatus::missing_model_slot) {
        return EntityVisualBindingCategory::missing;
    }
    if (resolution.resource &&
        (resolution.resource->readiness_impact ==
             goldsrc::LocalResourceReadinessImpact::security_blocked ||
         resolution.resource->readiness_status ==
             goldsrc::LocalResourceReadinessStatus::unsafe_name)) {
        return EntityVisualBindingCategory::unsafe;
    }
    return EntityVisualBindingCategory::missing;
}

[[nodiscard]] EntityVisualBindingStatus binding_status_for_resolution(
    const EntityVisualModelSlotResolution& resolution) noexcept
{
    switch (resolution.status) {
    case EntityVisualModelResolutionStatus::
        stock_modelindex_mapping_evidence_pending:
        return EntityVisualBindingStatus::visual_projection_pending;
    case EntityVisualModelResolutionStatus::manifest_entry_not_ready:
        return EntityVisualBindingStatus::manifest_entry_not_ready;
    case EntityVisualModelResolutionStatus::resolved_model_slot:
        break;
    case EntityVisualModelResolutionStatus::missing_model_slot:
    case EntityVisualModelResolutionStatus::invalid_model_reference:
    case EntityVisualModelResolutionStatus::invalid_manifest_entry:
        return EntityVisualBindingStatus::missing_model_slot;
    }
    return EntityVisualBindingStatus::asset_import_failed;
}

[[nodiscard]] EntityVisualBindingStatus binding_status_for_completion(
    const EntityVisualAssetImportCompletionStatus status) noexcept
{
    switch (status) {
    case EntityVisualAssetImportCompletionStatus::imported:
        return EntityVisualBindingStatus::asset_import_failed;
    case EntityVisualAssetImportCompletionStatus::unsupported_asset_format:
        return EntityVisualBindingStatus::unsupported_asset_format;
    case EntityVisualAssetImportCompletionStatus::asset_import_failed:
        return EntityVisualBindingStatus::asset_import_failed;
    case EntityVisualAssetImportCompletionStatus::asset_dependency_missing:
        return EntityVisualBindingStatus::asset_dependency_missing;
    case EntityVisualAssetImportCompletionStatus::asset_ambiguous:
        return EntityVisualBindingStatus::asset_ambiguous;
    case EntityVisualAssetImportCompletionStatus::asset_limit_exceeded:
        return EntityVisualBindingStatus::asset_limit_exceeded;
    }
    return EntityVisualBindingStatus::asset_import_failed;
}

[[nodiscard]] EntityVisualBindingCategory category_for_completion(
    const EntityVisualAssetImportCompletionStatus status) noexcept
{
    switch (status) {
    case EntityVisualAssetImportCompletionStatus::unsupported_asset_format:
        return EntityVisualBindingCategory::unsupported;
    case EntityVisualAssetImportCompletionStatus::asset_ambiguous:
        return EntityVisualBindingCategory::ambiguous;
    case EntityVisualAssetImportCompletionStatus::asset_import_failed:
    case EntityVisualAssetImportCompletionStatus::asset_dependency_missing:
    case EntityVisualAssetImportCompletionStatus::asset_limit_exceeded:
    case EntityVisualAssetImportCompletionStatus::imported:
        return EntityVisualBindingCategory::import_failed;
    }
    return EntityVisualBindingCategory::import_failed;
}

[[nodiscard]] EntityVisualBindingState unresolved_binding(
    const EntityVisualModelSlotResolution& resolution,
    const EntityVisualBindingStatus status,
    const EntityVisualBindingCategory category,
    const std::uint64_t resource_id,
    const std::uint64_t revision)
{
    return EntityVisualBindingState{
        resolution.reference,
        resolution.model_slot,
        resolution.resource,
        category,
        std::nullopt,
        status,
        std::nullopt,
        resolution.evidence_profile ==
                EntityVisualModelResolutionEvidenceProfile::
                    stock_modelindex_mapping_pending
            ? EntityVisualBindingEvidenceProfile::stock_visual_mapping_pending
            : EntityVisualBindingEvidenceProfile::
                  exact_synthetic_model_slot_and_approved_source,
        resource_id,
        revision};
}

[[nodiscard]] EntityVisualBindingState resolved_binding(
    const EntityVisualAssetLibraryPlanEntry& entry,
    const EntityVisualAssetRecord& record,
    const std::size_t record_index,
    const std::uint64_t resource_id,
    const std::uint64_t revision)
{
    const auto studio = record.kind() == EntityVisualAssetKind::studio_model;
    return EntityVisualBindingState{
        entry.resolution.reference,
        entry.resolution.model_slot,
        entry.resolution.resource,
        studio ? EntityVisualBindingCategory::studio_model
               : EntityVisualBindingCategory::sprite,
        record_index,
        studio ? EntityVisualBindingStatus::resolved_studio_model
               : EntityVisualBindingStatus::resolved_sprite,
        record.source_fingerprint(),
        EntityVisualBindingEvidenceProfile::
            exact_synthetic_model_slot_and_approved_source,
        resource_id,
        revision};
}

} // namespace

bool valid_entity_visual_asset_library_limits(
    const EntityVisualAssetLibraryLimits& limits) noexcept
{
    return limits.maximum_asset_count > 0U &&
           limits.maximum_asset_count <=
               kHardMaximumEntityVisualAssetCount &&
           limits.maximum_total_model_source_bytes > 0U &&
           limits.maximum_total_model_source_bytes <=
               kHardMaximumEntityVisualModelSourceBytes &&
           limits.maximum_total_sprite_source_bytes > 0U &&
           limits.maximum_total_sprite_source_bytes <=
               kHardMaximumEntityVisualSpriteSourceBytes &&
           limits.maximum_total_texture_rgba_bytes > 0U &&
           limits.maximum_total_texture_rgba_bytes <=
               kHardMaximumEntityVisualTextureRgbaBytes &&
           limits.maximum_total_geometry_bytes > 0U &&
           limits.maximum_total_geometry_bytes <=
               kHardMaximumEntityVisualGeometryBytes &&
           limits.maximum_pending_imports > 0U &&
           limits.maximum_pending_imports <=
               kHardMaximumEntityVisualPendingImports &&
           limits.maximum_imports_per_update > 0U &&
           limits.maximum_imports_per_update <=
               kHardMaximumEntityVisualImportsPerUpdate &&
           limits.maximum_library_events > 0U &&
           limits.maximum_library_events <=
               kHardMaximumEntityVisualLibraryEvents;
}

EntityVisualApprovedSourceKey::EntityVisualApprovedSourceKey(
    const std::uint64_t resource_id,
    local_resources::LocalResourceRootId root_id,
    local_resources::LocalVirtualResourceId virtual_resource_id,
    const local_resources::LocalStableFileIdentity stable_identity,
    const std::uint64_t main_source_byte_count,
    const goldsrc::PrecacheManifestCompatibilityProfile manifest_profile,
    const goldsrc::PrecacheManifestEvidenceProfile manifest_evidence) noexcept
    : resource_id_{resource_id},
      root_id_{std::move(root_id)},
      virtual_resource_id_{virtual_resource_id},
      stable_identity_{stable_identity},
      main_source_byte_count_{main_source_byte_count},
      manifest_profile_{manifest_profile},
      manifest_evidence_{manifest_evidence}
{
}

std::uint64_t EntityVisualApprovedSourceKey::resource_id() const noexcept
{
    return resource_id_;
}

local_resources::LocalResourceRootId
EntityVisualApprovedSourceKey::root_id() const noexcept
{
    return root_id_;
}

local_resources::LocalVirtualResourceId
EntityVisualApprovedSourceKey::virtual_resource_id() const noexcept
{
    return virtual_resource_id_;
}

local_resources::LocalStableFileIdentity
EntityVisualApprovedSourceKey::stable_identity() const noexcept
{
    return stable_identity_;
}

std::uint64_t EntityVisualApprovedSourceKey::main_source_byte_count()
    const noexcept
{
    return main_source_byte_count_;
}

goldsrc::PrecacheManifestCompatibilityProfile
EntityVisualApprovedSourceKey::manifest_profile() const noexcept
{
    return manifest_profile_;
}

goldsrc::PrecacheManifestEvidenceProfile
EntityVisualApprovedSourceKey::manifest_evidence() const noexcept
{
    return manifest_evidence_;
}

EntityVisualAssetReuseEvidence::EntityVisualAssetReuseEvidence(
    EntityVisualApprovedSourceKey source_key,
    const EntityVisualAssetKind kind,
    const assets::AssetImporterCategory importer_category,
    const EntityVisualAssetCompatibilityProfile compatibility_profile,
    std::string importer_id,
    const std::uint64_t total_source_bytes,
    std::vector<assets::AssetSourceFingerprint> source_fingerprints)
    : source_key_{std::move(source_key)},
      kind_{kind},
      importer_category_{importer_category},
      compatibility_profile_{compatibility_profile},
      importer_id_{std::move(importer_id)},
      total_source_bytes_{total_source_bytes},
      source_fingerprints_{std::move(source_fingerprints)}
{
}

const EntityVisualApprovedSourceKey&
EntityVisualAssetReuseEvidence::source_key() const noexcept
{
    return source_key_;
}

EntityVisualAssetKind EntityVisualAssetReuseEvidence::kind() const noexcept
{
    return kind_;
}

assets::AssetImporterCategory
EntityVisualAssetReuseEvidence::importer_category() const noexcept
{
    return importer_category_;
}

EntityVisualAssetCompatibilityProfile
EntityVisualAssetReuseEvidence::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

std::string_view EntityVisualAssetReuseEvidence::importer_id() const noexcept
{
    return importer_id_;
}

std::uint64_t EntityVisualAssetReuseEvidence::total_source_bytes()
    const noexcept
{
    return total_source_bytes_;
}

std::span<const assets::AssetSourceFingerprint>
EntityVisualAssetReuseEvidence::source_fingerprints() const noexcept
{
    return source_fingerprints_;
}

EntityVisualAssetRecord::EntityVisualAssetRecord(
    const EntityVisualAssetKind kind,
    std::shared_ptr<const assets::ModelAsset> model_asset,
    std::shared_ptr<const assets::SpriteAsset> sprite_asset,
    const std::uint16_t manifest_model_slot,
    EntityVisualManifestResourceMetadata manifest_resource,
    EntityVisualApprovedSourceKey approved_source_key,
    std::vector<assets::AssetSourceFingerprint> source_fingerprints,
    const std::uint64_t total_source_bytes,
    const std::uint64_t texture_rgba_bytes,
    const std::uint64_t geometry_bytes,
    const assets::AssetImporterCategory importer_category,
    std::string importer_id,
    const EntityVisualAssetCompatibilityProfile compatibility_profile,
    const std::uint64_t resource_id,
    const std::uint64_t resource_revision) noexcept
    : kind_{kind},
      model_asset_{std::move(model_asset)},
      sprite_asset_{std::move(sprite_asset)},
      manifest_model_slot_{manifest_model_slot},
      manifest_resource_{std::move(manifest_resource)},
      approved_source_key_{std::move(approved_source_key)},
      source_fingerprints_{std::move(source_fingerprints)},
      total_source_bytes_{total_source_bytes},
      texture_rgba_bytes_{texture_rgba_bytes},
      geometry_bytes_{geometry_bytes},
      importer_category_{importer_category},
      importer_id_{std::move(importer_id)},
      compatibility_profile_{compatibility_profile},
      resource_id_{resource_id},
      resource_revision_{resource_revision}
{
}

EntityVisualAssetKind EntityVisualAssetRecord::kind() const noexcept
{
    return kind_;
}

const std::shared_ptr<const assets::ModelAsset>&
EntityVisualAssetRecord::model_asset() const noexcept
{
    return model_asset_;
}

const std::shared_ptr<const assets::SpriteAsset>&
EntityVisualAssetRecord::sprite_asset() const noexcept
{
    return sprite_asset_;
}

std::uint16_t EntityVisualAssetRecord::manifest_model_slot() const noexcept
{
    return manifest_model_slot_;
}

const EntityVisualManifestResourceMetadata&
EntityVisualAssetRecord::manifest_resource() const noexcept
{
    return manifest_resource_;
}

const EntityVisualApprovedSourceKey&
EntityVisualAssetRecord::approved_source_key() const noexcept
{
    return approved_source_key_;
}

std::span<const assets::AssetSourceFingerprint>
EntityVisualAssetRecord::source_fingerprints() const noexcept
{
    return source_fingerprints_;
}

assets::AssetSourceFingerprint EntityVisualAssetRecord::source_fingerprint()
    const noexcept
{
    return source_fingerprints_.front();
}

std::uint64_t EntityVisualAssetRecord::total_source_bytes() const noexcept
{
    return total_source_bytes_;
}

std::uint64_t EntityVisualAssetRecord::texture_rgba_bytes() const noexcept
{
    return texture_rgba_bytes_;
}

std::uint64_t EntityVisualAssetRecord::geometry_bytes() const noexcept
{
    return geometry_bytes_;
}

assets::AssetImporterCategory EntityVisualAssetRecord::importer_category()
    const noexcept
{
    return importer_category_;
}

std::string_view EntityVisualAssetRecord::importer_id() const noexcept
{
    return importer_id_;
}

EntityVisualAssetCompatibilityProfile
EntityVisualAssetRecord::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

std::uint64_t EntityVisualAssetRecord::resource_id() const noexcept
{
    return resource_id_;
}

std::uint64_t EntityVisualAssetRecord::resource_revision() const noexcept
{
    return resource_revision_;
}

EntityVisualAssetLibraryState::EntityVisualAssetLibraryState(
    const std::uint64_t resource_id,
    const std::uint64_t resource_revision,
    std::vector<EntityVisualAssetRecord> records,
    std::vector<EntityVisualAssetReferenceEntry> references,
    const EntityVisualAssetLibraryStatistics statistics) noexcept
    : resource_id_{resource_id},
      resource_revision_{resource_revision},
      records_{std::move(records)},
      references_{std::move(references)},
      statistics_{statistics}
{
}

std::uint64_t EntityVisualAssetLibraryState::resource_id() const noexcept
{
    return resource_id_;
}

std::uint64_t EntityVisualAssetLibraryState::resource_revision() const noexcept
{
    return resource_revision_;
}

std::span<const EntityVisualAssetRecord>
EntityVisualAssetLibraryState::records() const noexcept
{
    return records_;
}

std::span<const EntityVisualAssetReferenceEntry>
EntityVisualAssetLibraryState::references() const noexcept
{
    return references_;
}

std::optional<std::size_t> EntityVisualAssetLibraryState::find_exact_index(
    const EntityVisualModelReference& reference) const noexcept
{
    const auto entry_index = find_reference_index(references_, reference);
    if (!entry_index) {
        return std::nullopt;
    }
    const auto record_index = references_[*entry_index].asset_library_index;
    return record_index < records_.size() ? std::optional{record_index}
                                          : std::nullopt;
}

const EntityVisualAssetRecord* EntityVisualAssetLibraryState::find_exact(
    const EntityVisualModelReference& reference) const noexcept
{
    const auto index = find_exact_index(reference);
    return index ? std::addressof(records_[*index]) : nullptr;
}

const EntityVisualAssetLibraryStatistics&
EntityVisualAssetLibraryState::statistics() const noexcept
{
    return statistics_;
}

EntityVisualAssetImportRequest::EntityVisualAssetImportRequest(
    const std::size_t request_index,
    EntityVisualApprovedSourceKey source_key,
    const std::size_t manifest_entry_offset,
    const std::uint16_t model_slot,
    std::vector<EntityVisualModelReference> references) noexcept
    : request_index_{request_index},
      source_key_{std::move(source_key)},
      manifest_entry_offset_{manifest_entry_offset},
      model_slot_{model_slot},
      references_{std::move(references)}
{
}

std::size_t EntityVisualAssetImportRequest::request_index() const noexcept
{
    return request_index_;
}

const EntityVisualApprovedSourceKey&
EntityVisualAssetImportRequest::source_key() const noexcept
{
    return source_key_;
}

std::size_t EntityVisualAssetImportRequest::manifest_entry_offset()
    const noexcept
{
    return manifest_entry_offset_;
}

std::uint16_t EntityVisualAssetImportRequest::model_slot() const noexcept
{
    return model_slot_;
}

std::span<const EntityVisualModelReference>
EntityVisualAssetImportRequest::references() const noexcept
{
    return references_;
}

EntityVisualAssetLibraryPlan::EntityVisualAssetLibraryPlan(
    const std::uint64_t resource_id,
    std::vector<EntityVisualAssetLibraryPlanEntry> entries,
    std::vector<EntityVisualAssetImportRequest> requests,
    const std::size_t duplicate_reference_count) noexcept
    : resource_id_{resource_id},
      entries_{std::move(entries)},
      requests_{std::move(requests)},
      duplicate_reference_count_{duplicate_reference_count}
{
}

std::uint64_t EntityVisualAssetLibraryPlan::resource_id() const noexcept
{
    return resource_id_;
}

std::span<const EntityVisualAssetLibraryPlanEntry>
EntityVisualAssetLibraryPlan::entries() const noexcept
{
    return entries_;
}

std::span<const EntityVisualAssetImportRequest>
EntityVisualAssetLibraryPlan::requests() const noexcept
{
    return requests_;
}

std::size_t EntityVisualAssetLibraryPlan::unique_reference_count()
    const noexcept
{
    return entries_.size();
}

std::size_t EntityVisualAssetLibraryPlan::duplicate_reference_count()
    const noexcept
{
    return duplicate_reference_count_;
}

EntityVisualImportedAssetCandidate::EntityVisualImportedAssetCandidate(
    const EntityVisualAssetKind kind,
    EntityVisualApprovedSourceKey source_key,
    std::shared_ptr<const assets::ModelAsset> model_asset,
    std::shared_ptr<const assets::SpriteAsset> sprite_asset,
    std::string importer_id,
    const std::uint64_t total_source_bytes,
    std::vector<assets::AssetSourceFingerprint> source_fingerprints) noexcept
    : kind_{kind},
      source_key_{std::move(source_key)},
      model_asset_{std::move(model_asset)},
      sprite_asset_{std::move(sprite_asset)},
      importer_id_{std::move(importer_id)},
      total_source_bytes_{total_source_bytes},
      source_fingerprints_{std::move(source_fingerprints)}
{
}

EntityVisualImportedAssetCandidate
EntityVisualImportedAssetCandidate::studio_model(
    EntityVisualApprovedSourceKey source_key,
    std::shared_ptr<const assets::ModelAsset> asset,
    std::string importer_id,
    const std::uint64_t total_source_bytes,
    std::vector<assets::AssetSourceFingerprint> source_fingerprints)
{
    return EntityVisualImportedAssetCandidate{
        EntityVisualAssetKind::studio_model,
        std::move(source_key),
        std::move(asset),
        nullptr,
        std::move(importer_id),
        total_source_bytes,
        std::move(source_fingerprints)};
}

EntityVisualImportedAssetCandidate EntityVisualImportedAssetCandidate::sprite(
    EntityVisualApprovedSourceKey source_key,
    std::shared_ptr<const assets::SpriteAsset> asset,
    std::string importer_id,
    const std::uint64_t total_source_bytes,
    std::vector<assets::AssetSourceFingerprint> source_fingerprints)
{
    return EntityVisualImportedAssetCandidate{
        EntityVisualAssetKind::sprite,
        std::move(source_key),
        nullptr,
        std::move(asset),
        std::move(importer_id),
        total_source_bytes,
        std::move(source_fingerprints)};
}

EntityVisualAssetKind EntityVisualImportedAssetCandidate::kind() const noexcept
{
    return kind_;
}

const EntityVisualApprovedSourceKey&
EntityVisualImportedAssetCandidate::source_key() const noexcept
{
    return source_key_;
}

const std::shared_ptr<const assets::ModelAsset>&
EntityVisualImportedAssetCandidate::model_asset() const noexcept
{
    return model_asset_;
}

const std::shared_ptr<const assets::SpriteAsset>&
EntityVisualImportedAssetCandidate::sprite_asset() const noexcept
{
    return sprite_asset_;
}

std::string_view EntityVisualImportedAssetCandidate::importer_id()
    const noexcept
{
    return importer_id_;
}

std::uint64_t EntityVisualImportedAssetCandidate::total_source_bytes()
    const noexcept
{
    return total_source_bytes_;
}

std::span<const assets::AssetSourceFingerprint>
EntityVisualImportedAssetCandidate::source_fingerprints() const noexcept
{
    return source_fingerprints_;
}

EntityVisualAssetLibraryPlanResult EntityVisualAssetLibraryBuilder::plan(
    const std::uint64_t resource_id,
    const std::span<const EntityVisualProjectionState> previous_projections,
    const std::span<const EntityVisualProjectionState> current_projections,
    const goldsrc::PrecacheManifestState& manifest,
    const IEntityVisualModelReferenceResolver& resolver,
    std::shared_ptr<const EntityVisualAssetLibraryState> previous_library,
    const EntityVisualAssetLibraryLimits limits) const noexcept
{
    return plan(resource_id,
        previous_projections,
        current_projections,
        manifest,
        resolver,
        std::move(previous_library),
        limits,
        {});
}

EntityVisualAssetLibraryPlanResult EntityVisualAssetLibraryBuilder::plan(
    const std::uint64_t resource_id,
    const std::span<const EntityVisualProjectionState> previous_projections,
    const std::span<const EntityVisualProjectionState> current_projections,
    const goldsrc::PrecacheManifestState& manifest,
    const IEntityVisualModelReferenceResolver& resolver,
    std::shared_ptr<const EntityVisualAssetLibraryState> previous_library,
    const EntityVisualAssetLibraryLimits limits,
    const std::span<const EntityVisualAssetReuseEvidence> reuse_evidence)
    const noexcept
{
    if (!valid_entity_visual_asset_library_limits(limits)) {
        return plan_failure(make_error(
            EntityVisualAssetLibraryErrorCode::invalid_configuration,
            "Invalid entity visual asset library limits"));
    }
    if (resource_id == 0U) {
        return plan_failure(make_error(
            EntityVisualAssetLibraryErrorCode::invalid_resource_id,
            "Entity visual library resource ID must be nonzero"));
    }
    if (previous_library && previous_library->resource_id() != resource_id) {
        return plan_failure(make_error(
            EntityVisualAssetLibraryErrorCode::previous_library_mismatch,
            "Previous visual library belongs to another resource identity"));
    }
    if (reuse_evidence.size() > limits.maximum_library_events) {
        return plan_failure(make_error(
            EntityVisualAssetLibraryErrorCode::library_event_limit_exceeded,
            "Current visual source reuse evidence exceeds the event limit"));
    }
    for (std::size_t index = 0U; index < reuse_evidence.size(); ++index) {
        const auto& evidence = reuse_evidence[index];
        const auto model = evidence.kind() == EntityVisualAssetKind::studio_model;
        const auto sprite = evidence.kind() == EntityVisualAssetKind::sprite;
        if ((!model && !sprite) ||
            evidence.source_key().resource_id() != resource_id ||
            !evidence.source_key().root_id().valid() ||
            evidence.source_key().virtual_resource_id().value() == 0U ||
            !evidence.source_key().stable_identity().valid() ||
            evidence.importer_id().empty() ||
            evidence.importer_id().size() >
                kMaximumEntityVisualImporterIdBytes ||
            evidence.source_fingerprints().empty() ||
            evidence.source_fingerprints().size() >
                kMaximumEntityVisualSourceFingerprints ||
            evidence.total_source_bytes() <
                evidence.source_key().main_source_byte_count() ||
            (model &&
                (evidence.importer_category() !=
                        assets::AssetImporterCategory::model ||
                    evidence.compatibility_profile() !=
                        EntityVisualAssetCompatibilityProfile::
                            goldsrc_studio_v10)) ||
            (sprite &&
                (evidence.importer_category() !=
                        assets::AssetImporterCategory::sprite ||
                    evidence.compatibility_profile() !=
                        EntityVisualAssetCompatibilityProfile::
                            goldsrc_palette_sprite_v2))) {
            return plan_failure(make_error(
                EntityVisualAssetLibraryErrorCode::invalid_plan,
                "Current visual source reuse evidence is incomplete or inconsistent"));
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (reuse_evidence[earlier].source_key() ==
                evidence.source_key()) {
                return plan_failure(make_error(
                    EntityVisualAssetLibraryErrorCode::invalid_plan,
                    "Current visual source reuse evidence repeats one approved source key"));
            }
        }
    }

    try {
        std::vector<EntityVisualModelReference> references;
        if (previous_projections.size() >
            std::numeric_limits<std::size_t>::max() -
                current_projections.size()) {
            return plan_failure(make_error(
                EntityVisualAssetLibraryErrorCode::size_overflow,
                "Projection reference count overflow"));
        }
        const auto input_count =
            previous_projections.size() + current_projections.size();
        if (input_count > limits.maximum_library_events) {
            return plan_failure(make_error(
                EntityVisualAssetLibraryErrorCode::
                    projection_reference_limit_exceeded,
                "Projection reference count exceeds the bounded event limit"));
        }
        references.reserve(input_count);
        for (const auto& projection : previous_projections) {
            references.push_back(projection.model_reference());
        }
        for (const auto& projection : current_projections) {
            references.push_back(projection.model_reference());
        }
        std::ranges::sort(references, reference_less);
        const auto unique_end = std::unique(references.begin(), references.end());
        const auto duplicate_count = static_cast<std::size_t>(
            references.end() - unique_end);
        references.erase(unique_end, references.end());

        std::vector<EntityVisualAssetLibraryPlanEntry> entries;
        std::vector<EntityVisualAssetImportRequest> requests;
        entries.reserve(references.size());
        requests.reserve(references.size());
        for (const auto& reference : references) {
            auto resolution = resolver.resolve(reference, manifest);
            std::optional<std::size_t> existing_index;
            std::optional<std::size_t> request_index;
            if (resolution) {
                if (!resolution.manifest_entry_offset ||
                    !resolution.model_slot || !resolution.resource ||
                    *resolution.manifest_entry_offset >=
                        manifest.entries().size()) {
                    return plan_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::invalid_plan,
                        "Resolved model slot lacks exact manifest metadata",
                        reference));
                }
                const auto& entry =
                    manifest.entries()[*resolution.manifest_entry_offset];
                if (!entry.locator() || !entry.local_file_size() ||
                    entry.wire_ordinal() != resolution.resource->wire_ordinal ||
                    entry.resource_index() != *resolution.model_slot) {
                    return plan_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::
                            invalid_manifest_source_identity,
                        "Ready model entry lacks an exact approved source identity",
                        reference));
                }
                const auto& locator = *entry.locator();
                if (!locator.root_id().valid() ||
                    !locator.expected_identity().valid() ||
                    locator.expected_file_size() != *entry.local_file_size()) {
                    return plan_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::
                            invalid_manifest_source_identity,
                        "Manifest locator identity is invalid or inconsistent",
                        reference));
                }
                EntityVisualApprovedSourceKey source_key{
                    resource_id,
                    locator.root_id(),
                    locator.virtual_name().id(),
                    locator.expected_identity(),
                    locator.expected_file_size(),
                    entry.compatibility_profile(),
                    entry.evidence_profile()};

                if (previous_library) {
                    const auto* const current_identity =
                        find_reuse_evidence(reuse_evidence, source_key);
                    const auto reference_index =
                        previous_library->find_exact_index(reference);
                    if (current_identity && reference_index &&
                        exact_reuse_identity(
                            previous_library->records()[*reference_index],
                            *current_identity)) {
                        existing_index = *reference_index;
                    }
                    if (current_identity && !existing_index) {
                        std::size_t source_record_index = 0U;
                        if (find_reusable_record(*previous_library,
                                *current_identity,
                                source_record_index) != nullptr) {
                            existing_index = source_record_index;
                        }
                    }
                }
                if (!existing_index) {
                    const auto request_found = std::ranges::find_if(requests,
                        [&source_key](
                            const EntityVisualAssetImportRequest& request) {
                            return request.source_key() == source_key;
                        });
                    if (request_found == requests.end()) {
                        request_index = requests.size();
                        requests.emplace_back(EntityVisualAssetImportRequest{
                            *request_index,
                            std::move(source_key),
                            *resolution.manifest_entry_offset,
                            *resolution.model_slot,
                            std::vector<EntityVisualModelReference>{reference}});
                    } else {
                        request_index = request_found->request_index();
                        request_found->references_.push_back(reference);
                    }
                }
            }
            entries.push_back(EntityVisualAssetLibraryPlanEntry{
                std::move(resolution), existing_index, request_index});
        }

        if (requests.size() > limits.maximum_pending_imports) {
            return plan_failure(make_error(
                EntityVisualAssetLibraryErrorCode::
                    pending_import_limit_exceeded,
                "Unique pending visual imports exceed the configured limit"));
        }
        if (requests.size() > limits.maximum_imports_per_update) {
            return plan_failure(make_error(
                EntityVisualAssetLibraryErrorCode::
                    imports_per_update_limit_exceeded,
                "Unique visual imports exceed the per-update limit"));
        }
        if (entries.size() > limits.maximum_library_events - requests.size()) {
            return plan_failure(make_error(
                EntityVisualAssetLibraryErrorCode::library_event_limit_exceeded,
                "Visual plan event limit exceeded"));
        }

        return EntityVisualAssetLibraryPlanResult{
            EntityVisualAssetLibraryPlan{
                resource_id,
                std::move(entries),
                std::move(requests),
                duplicate_count},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return plan_failure(make_error(
            EntityVisualAssetLibraryErrorCode::unable_to_retain_library,
            "Unable to retain the bounded visual import plan"));
    } catch (...) {
        return plan_failure(make_error(
            EntityVisualAssetLibraryErrorCode::unable_to_retain_library,
            "Unable to build the visual import plan"));
    }
}

EntityVisualAssetLibraryBuildResult EntityVisualAssetLibraryBuilder::publish(
    const EntityVisualAssetLibraryPlan& plan,
    const std::span<const EntityVisualAssetImportCompletion> completions,
    std::shared_ptr<const EntityVisualAssetLibraryState> previous_library,
    const EntityVisualAssetLibraryLimits limits) const noexcept
{
    if (!valid_entity_visual_asset_library_limits(limits)) {
        return build_failure(make_error(
            EntityVisualAssetLibraryErrorCode::invalid_configuration,
            "Invalid entity visual asset library limits"));
    }
    if (plan.resource_id() == 0U) {
        return build_failure(make_error(
            EntityVisualAssetLibraryErrorCode::invalid_resource_id,
            "Visual import plan has a zero resource ID"));
    }
    if (previous_library &&
        previous_library->resource_id() != plan.resource_id()) {
        return build_failure(make_error(
            EntityVisualAssetLibraryErrorCode::previous_library_mismatch,
            "Previous visual library does not match the import plan"));
    }
    if (plan.requests().size() > limits.maximum_pending_imports ||
        plan.requests().size() > limits.maximum_imports_per_update) {
        return build_failure(make_error(
            EntityVisualAssetLibraryErrorCode::invalid_plan,
            "Visual import plan exceeds current operation limits"));
    }
    if (completions.size() != plan.requests().size()) {
        return build_failure(make_error(
            EntityVisualAssetLibraryErrorCode::missing_import_completion,
            "Every unique import request requires exactly one completion"));
    }

    try {
        std::vector<const EntityVisualAssetImportCompletion*> by_request(
            plan.requests().size(), nullptr);
        for (const auto& completion : completions) {
            if (completion.request_index >= by_request.size()) {
                return build_failure(make_error(
                    EntityVisualAssetLibraryErrorCode::invalid_plan,
                    "Import completion index is outside the plan",
                    std::nullopt,
                    completion.request_index));
            }
            if (by_request[completion.request_index] != nullptr) {
                return build_failure(make_error(
                    EntityVisualAssetLibraryErrorCode::
                        duplicate_import_completion,
                    "Duplicate completion for one visual import request",
                    std::nullopt,
                    completion.request_index));
            }
            by_request[completion.request_index] = std::addressof(completion);
        }
        if (std::ranges::any_of(by_request, [](const auto* completion) {
                return completion == nullptr;
            })) {
            return build_failure(make_error(
                EntityVisualAssetLibraryErrorCode::missing_import_completion,
                "A visual import request has no completion"));
        }

        std::vector<EntityVisualAssetRecord> records = previous_library
            ? std::vector<EntityVisualAssetRecord>{
                  previous_library->records().begin(),
                  previous_library->records().end()}
            : std::vector<EntityVisualAssetRecord>{};
        const auto previous_record_count = records.size();
        std::vector<bool> replaced_previous_records(
            previous_record_count, false);
        std::vector<EntityVisualAssetReferenceEntry> references =
            previous_library
                ? std::vector<EntityVisualAssetReferenceEntry>{
                      previous_library->references().begin(),
                      previous_library->references().end()}
                : std::vector<EntityVisualAssetReferenceEntry>{};
        std::vector<EntityVisualBindingState> bindings;
        bindings.reserve(plan.entries().size());
        bool changed = !previous_library;
        std::uint64_t imported_count = 0U;
        std::uint64_t reused_count = 0U;
        std::uint64_t deduplicated_count = 0U;

        for (const auto& entry : plan.entries()) {
            if (!entry.resolution) {
                bindings.push_back(unresolved_binding(
                    entry.resolution,
                    binding_status_for_resolution(entry.resolution),
                    category_for_readiness(entry.resolution),
                    plan.resource_id(),
                    0U));
                continue;
            }

            std::optional<std::size_t> record_index =
                entry.existing_asset_library_index;
            if (record_index) {
                if (*record_index >= records.size()) {
                    return build_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::invalid_plan,
                        "Plan references an asset outside the previous library",
                        entry.resolution.reference));
                }
                ++reused_count;
            } else {
                if (!entry.import_request_index ||
                    *entry.import_request_index >= by_request.size()) {
                    return build_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::invalid_plan,
                        "Resolved new reference lacks an import request",
                        entry.resolution.reference));
                }
                const auto& request =
                    plan.requests()[*entry.import_request_index];
                const auto& completion =
                    *by_request[*entry.import_request_index];
                if (completion.status !=
                    EntityVisualAssetImportCompletionStatus::imported) {
                    if (completion.candidate) {
                        return build_failure(make_error(
                            EntityVisualAssetLibraryErrorCode::invalid_plan,
                            "Failed import completion unexpectedly owns an asset",
                            entry.resolution.reference,
                            completion.request_index));
                    }
                    bindings.push_back(unresolved_binding(
                        entry.resolution,
                        binding_status_for_completion(completion.status),
                        category_for_completion(completion.status),
                        plan.resource_id(),
                        0U));
                    continue;
                }
                if (!completion.candidate) {
                    return build_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::
                            missing_import_completion,
                        "Successful completion owns no imported visual asset",
                        entry.resolution.reference,
                        completion.request_index));
                }
                const auto& candidate = *completion.candidate;
                if (candidate.source_key() != request.source_key()) {
                    return build_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::
                            imported_source_mismatch,
                        "Imported visual source identity differs from its request",
                        entry.resolution.reference,
                        completion.request_index));
                }
                const bool model =
                    candidate.kind() == EntityVisualAssetKind::studio_model;
                if ((model &&
                     (!candidate.model_asset() || candidate.sprite_asset())) ||
                    (!model &&
                     (!candidate.sprite_asset() || candidate.model_asset())) ||
                    candidate.importer_id().empty() ||
                    candidate.importer_id().size() >
                        kMaximumEntityVisualImporterIdBytes ||
                    candidate.source_fingerprints().empty() ||
                    candidate.source_fingerprints().size() >
                        kMaximumEntityVisualSourceFingerprints ||
                    candidate.total_source_bytes() <
                        candidate.source_key().main_source_byte_count()) {
                    return build_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::invalid_imported_asset,
                        "Imported visual asset metadata is incomplete or invalid",
                        entry.resolution.reference,
                        completion.request_index));
                }

                const auto byte_counts = model
                    ? model_byte_counts(*candidate.model_asset())
                    : sprite_byte_counts(*candidate.sprite_asset());
                if (!byte_counts) {
                    return build_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::invalid_imported_asset,
                        "Imported visual asset has unsupported owning source data",
                        entry.resolution.reference,
                        completion.request_index));
                }

                const auto existing = std::ranges::find_if(
                    records,
                    [&candidate](const EntityVisualAssetRecord& record) {
                        return exact_candidate_identity(record, candidate);
                    });
                if (existing != records.end()) {
                    record_index = static_cast<std::size_t>(
                        existing - records.begin());
                    ++deduplicated_count;
                } else {
                    const auto conflicting = std::ranges::find_if(
                        records,
                        [&candidate](const EntityVisualAssetRecord& record) {
                            return record.approved_source_key() ==
                                   candidate.source_key();
                        });
                    if (conflicting != records.end()) {
                        const auto conflicting_index =
                            static_cast<std::size_t>(
                                conflicting - records.begin());
                        if (conflicting_index >= previous_record_count ||
                            replaced_previous_records[conflicting_index]) {
                            return build_failure(make_error(
                                EntityVisualAssetLibraryErrorCode::
                                    exact_source_identity_conflict,
                                "One current approved source produced conflicting fingerprints or importer evidence",
                                entry.resolution.reference,
                                completion.request_index));
                        }

                        const auto asset_resource_id = record_resource_id(
                            candidate, records);
                        std::vector<EntityVisualAssetRecord>
                            replacement_records;
                        replacement_records.reserve(records.size());
                        for (std::size_t record_offset = 0U;
                             record_offset < records.size();
                             ++record_offset) {
                            if (record_offset != conflicting_index) {
                                replacement_records.emplace_back(
                                    records[record_offset]);
                                continue;
                            }
                            replacement_records.emplace_back(
                                EntityVisualAssetRecord{
                                    candidate.kind(),
                                    candidate.model_asset(),
                                    candidate.sprite_asset(),
                                    request.model_slot(),
                                    *entry.resolution.resource,
                                    candidate.source_key(),
                                    std::vector<
                                        assets::AssetSourceFingerprint>{
                                        candidate.source_fingerprints().begin(),
                                        candidate.source_fingerprints().end()},
                                    candidate.total_source_bytes(),
                                    byte_counts->texture_rgba_bytes,
                                    byte_counts->geometry_bytes,
                                    model
                                        ? assets::AssetImporterCategory::model
                                        : assets::AssetImporterCategory::sprite,
                                    std::string{candidate.importer_id()},
                                    model
                                        ? EntityVisualAssetCompatibilityProfile::
                                              goldsrc_studio_v10
                                        : EntityVisualAssetCompatibilityProfile::
                                              goldsrc_palette_sprite_v2,
                                    asset_resource_id,
                                    0U});
                        }
                        records.swap(replacement_records);
                        record_index = conflicting_index;
                        replaced_previous_records[conflicting_index] = true;
                        ++imported_count;
                        changed = true;
                    } else if (records.size() >=
                               limits.maximum_asset_count) {
                        return build_failure(make_error(
                            EntityVisualAssetLibraryErrorCode::
                                asset_count_limit_exceeded,
                            "Visual asset count limit exceeded",
                            entry.resolution.reference,
                            completion.request_index));
                    } else {
                        record_index = records.size();
                        const auto asset_resource_id = record_resource_id(
                            candidate, records);
                        records.emplace_back(EntityVisualAssetRecord{
                            candidate.kind(),
                            candidate.model_asset(),
                            candidate.sprite_asset(),
                            request.model_slot(),
                            *entry.resolution.resource,
                            candidate.source_key(),
                            std::vector<assets::AssetSourceFingerprint>{
                                candidate.source_fingerprints().begin(),
                                candidate.source_fingerprints().end()},
                            candidate.total_source_bytes(),
                            byte_counts->texture_rgba_bytes,
                            byte_counts->geometry_bytes,
                            model ? assets::AssetImporterCategory::model
                                  : assets::AssetImporterCategory::sprite,
                            std::string{candidate.importer_id()},
                            model ? EntityVisualAssetCompatibilityProfile::
                                        goldsrc_studio_v10
                                  : EntityVisualAssetCompatibilityProfile::
                                        goldsrc_palette_sprite_v2,
                            asset_resource_id,
                            0U});
                        ++imported_count;
                        changed = true;
                    }
                }
            }

            if (!record_index || *record_index >= records.size()) {
                return build_failure(make_error(
                    EntityVisualAssetLibraryErrorCode::invalid_plan,
                    "Resolved binding has no retained visual asset",
                    entry.resolution.reference));
            }
            const auto reference_entry_index =
                find_reference_index(references, entry.resolution.reference);
            if (!reference_entry_index) {
                references.push_back(EntityVisualAssetReferenceEntry{
                    entry.resolution.reference, *record_index});
                std::ranges::sort(
                    references,
                    [](const EntityVisualAssetReferenceEntry& left,
                       const EntityVisualAssetReferenceEntry& right) {
                        return reference_less(left.reference, right.reference);
                    });
                changed = true;
            } else if (references[*reference_entry_index].asset_library_index !=
                       *record_index) {
                references[*reference_entry_index].asset_library_index =
                    *record_index;
                changed = true;
            }
            bindings.push_back(resolved_binding(
                entry,
                records[*record_index],
                *record_index,
                plan.resource_id(),
                0U));
        }

        std::uint64_t revision = 1U;
        if (previous_library) {
            revision = previous_library->resource_revision();
            if (changed) {
                if (revision == std::numeric_limits<std::uint64_t>::max()) {
                    return build_failure(make_error(
                        EntityVisualAssetLibraryErrorCode::revision_overflow,
                        "Entity visual library revision overflow"));
                }
                ++revision;
            }
        }

        for (auto& record : records) {
            if (record.resource_revision_ == 0U) {
                record.resource_revision_ = revision;
            }
        }
        for (auto& binding : bindings) {
            binding.resource_revision_ = revision;
        }

        EntityVisualAssetLibraryStatistics statistics = previous_library
            ? previous_library->statistics()
            : EntityVisualAssetLibraryStatistics{};
        if (add_overflows(
                statistics.cumulative_import_request_count,
                static_cast<std::uint64_t>(plan.requests().size())) ||
            add_overflows(
                statistics.cumulative_imported_asset_count,
                imported_count) ||
            add_overflows(
                statistics.cumulative_reused_reference_count,
                reused_count) ||
            add_overflows(
                statistics.cumulative_deduplicated_asset_count,
                deduplicated_count)) {
            return build_failure(make_error(
                EntityVisualAssetLibraryErrorCode::size_overflow,
                "Entity visual library statistics overflow"));
        }
        statistics.cumulative_import_request_count += plan.requests().size();
        statistics.cumulative_imported_asset_count += imported_count;
        statistics.cumulative_reused_reference_count += reused_count;
        statistics.cumulative_deduplicated_asset_count += deduplicated_count;
        statistics.asset_count = records.size();
        statistics.reference_count = references.size();
        statistics.studio_model_count = 0U;
        statistics.sprite_count = 0U;
        statistics.total_model_source_bytes = 0U;
        statistics.total_sprite_source_bytes = 0U;
        statistics.total_texture_rgba_bytes = 0U;
        statistics.total_geometry_bytes = 0U;
        for (const auto& record : records) {
            auto& source_total = record.kind() ==
                                         EntityVisualAssetKind::studio_model
                                     ? statistics.total_model_source_bytes
                                     : statistics.total_sprite_source_bytes;
            if (add_overflows(source_total, record.total_source_bytes()) ||
                add_overflows(
                    statistics.total_texture_rgba_bytes,
                    record.texture_rgba_bytes()) ||
                add_overflows(
                    statistics.total_geometry_bytes,
                    record.geometry_bytes())) {
                return build_failure(make_error(
                    EntityVisualAssetLibraryErrorCode::size_overflow,
                    "Entity visual asset byte accounting overflow"));
            }
            source_total += record.total_source_bytes();
            statistics.total_texture_rgba_bytes +=
                record.texture_rgba_bytes();
            statistics.total_geometry_bytes += record.geometry_bytes();
            if (record.kind() == EntityVisualAssetKind::studio_model) {
                ++statistics.studio_model_count;
            } else {
                ++statistics.sprite_count;
            }
        }
        if (statistics.total_model_source_bytes >
            limits.maximum_total_model_source_bytes) {
            return build_failure(make_error(
                EntityVisualAssetLibraryErrorCode::
                    model_source_byte_limit_exceeded,
                "Entity visual model-source byte limit exceeded"));
        }
        if (statistics.total_sprite_source_bytes >
            limits.maximum_total_sprite_source_bytes) {
            return build_failure(make_error(
                EntityVisualAssetLibraryErrorCode::
                    sprite_source_byte_limit_exceeded,
                "Entity visual sprite-source byte limit exceeded"));
        }
        if (statistics.total_texture_rgba_bytes >
            limits.maximum_total_texture_rgba_bytes) {
            return build_failure(make_error(
                EntityVisualAssetLibraryErrorCode::
                    texture_rgba_byte_limit_exceeded,
                "Entity visual texture RGBA byte limit exceeded"));
        }
        if (statistics.total_geometry_bytes >
            limits.maximum_total_geometry_bytes) {
            return build_failure(make_error(
                EntityVisualAssetLibraryErrorCode::geometry_byte_limit_exceeded,
                "Entity visual geometry byte limit exceeded"));
        }
        const auto update_events = static_cast<std::uint64_t>(
            plan.entries().size() + plan.requests().size() +
            completions.size());
        if (update_events > limits.maximum_library_events ||
            add_overflows(
                statistics.cumulative_library_events,
                update_events)) {
            return build_failure(make_error(
                EntityVisualAssetLibraryErrorCode::library_event_limit_exceeded,
                "Entity visual library event accounting exceeded"));
        }
        statistics.cumulative_library_events += update_events;

        if (previous_library && !changed) {
            return EntityVisualAssetLibraryBuildResult{
                std::move(previous_library), std::move(bindings), std::nullopt};
        }
        auto library = std::make_shared<const EntityVisualAssetLibraryState>(
            EntityVisualAssetLibraryState{
                plan.resource_id(),
                revision,
                std::move(records),
                std::move(references),
                statistics});
        return EntityVisualAssetLibraryBuildResult{
            std::move(library), std::move(bindings), std::nullopt};
    } catch (const std::bad_alloc&) {
        return build_failure(make_error(
            EntityVisualAssetLibraryErrorCode::unable_to_retain_library,
            "Unable to retain immutable entity visual asset library"));
    } catch (...) {
        return build_failure(make_error(
            EntityVisualAssetLibraryErrorCode::unable_to_retain_library,
            "Unable to publish immutable entity visual asset library"));
    }
}

} // namespace hlclient::entity_visual

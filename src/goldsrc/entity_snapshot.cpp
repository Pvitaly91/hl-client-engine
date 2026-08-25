#include <hlclient/goldsrc/entity_snapshot.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    const auto size =
        (std::min)(context.size(), kEntitySnapshotDiagnosticTextLimit);
    return std::string{context.data(), size};
}

[[nodiscard]] bool add_overflows(
    const std::size_t left,
    const std::size_t right) noexcept
{
    return right > (std::numeric_limits<std::size_t>::max)() - left;
}

[[nodiscard]] bool is_synthetic(
    const EntitySnapshotCompatibilityProfile profile) noexcept
{
    return profile == EntitySnapshotCompatibilityProfile::synthetic_neutral_v1;
}

[[nodiscard]] bool valid_profile(
    const EntitySnapshotCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case EntitySnapshotCompatibilityProfile::
        stock_protocol_48_build_10210_evidence_pending:
    case EntitySnapshotCompatibilityProfile::synthetic_neutral_v1:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_schema_category(
    const EntitySchemaCategory category) noexcept
{
    switch (category) {
    case EntitySchemaCategory::ordinary_entity:
    case EntitySchemaCategory::player_entity:
    case EntitySchemaCategory::custom_entity:
    case EntitySchemaCategory::alternate_explicit_schema:
        return true;
    }
    return false;
}

[[nodiscard]] bool is_synthetic(
    const EntitySnapshotReference& reference) noexcept
{
    return reference.policy() ==
           EntitySnapshotReferencePolicy::synthetic_uint32_non_wrapping;
}

[[nodiscard]] EntityBaselineInsertResult baseline_failure(
    const EntityBaselineErrorCode code,
    const std::string_view context,
    const std::optional<EntityBaselineKeyKind> key_kind = std::nullopt,
    const std::optional<std::uint32_t> key_value = std::nullopt)
{
    return EntityBaselineInsertResult{
        false,
        EntityBaselineError{
            code, key_kind, key_value, bounded_context(context)},
    };
}

[[nodiscard]] EntityBaselinePublishResult baseline_publish_failure(
    const EntityBaselineErrorCode code,
    const std::string_view context)
{
    return EntityBaselinePublishResult{
        std::nullopt,
        EntityBaselineError{
            code, std::nullopt, std::nullopt, bounded_context(context)},
    };
}

[[nodiscard]] EntitySnapshotBuildResult snapshot_failure(
    const EntitySnapshotErrorCode code,
    const std::string_view context,
    const std::optional<std::uint32_t> entity_number = std::nullopt,
    const std::optional<std::uint32_t> snapshot_reference = std::nullopt)
{
    return EntitySnapshotBuildResult{
        std::nullopt,
        EntitySnapshotError{
            code,
            entity_number,
            snapshot_reference,
            bounded_context(context)},
    };
}

[[nodiscard]] EntitySnapshotHistoryMutationResult history_failure(
    const EntitySnapshotHistoryErrorCode code,
    const std::string_view context,
    const std::optional<std::uint32_t> snapshot_reference = std::nullopt)
{
    return EntitySnapshotHistoryMutationResult{
        false,
        EntitySnapshotHistoryError{
            code, snapshot_reference, bounded_context(context)},
    };
}

[[nodiscard]] EntitySnapshotHistoryPublishResult history_publish_failure(
    const EntitySnapshotHistoryErrorCode code,
    const std::string_view context)
{
    return EntitySnapshotHistoryPublishResult{
        std::nullopt,
        EntitySnapshotHistoryError{
            code, std::nullopt, bounded_context(context)},
    };
}

[[nodiscard]] bool object_matches_neutral_profile(
    const DeltaObjectState& object) noexcept
{
    return object.decode_profile() ==
           DeltaValueCompatibilityProfile::synthetic_neutral_v1;
}

[[nodiscard]] bool key_value_within_limit(
    const EntityBaselineKey& key,
    const EntitySnapshotLimits& limits) noexcept
{
    return key.value() <= limits.maximum_entity_number;
}

[[nodiscard]] bool reference_less(
    const EntitySnapshotReference& left,
    const EntitySnapshotReference& right) noexcept
{
    return left.policy() == right.policy() && left.value() < right.value();
}

[[nodiscard]] bool reference_less_equal(
    const EntitySnapshotReference& left,
    const EntitySnapshotReference& right) noexcept
{
    return left.policy() == right.policy() && left.value() <= right.value();
}

} // namespace

bool valid_entity_snapshot_limits(const EntitySnapshotLimits& limits) noexcept
{
    return limits.maximum_baselines > 0U &&
           limits.maximum_baselines <= kMaximumEntityBaselines &&
           limits.maximum_entities_per_snapshot > 0U &&
           limits.maximum_entities_per_snapshot <=
               kMaximumEntitiesPerSnapshot &&
           limits.maximum_entity_number <= kMaximumEntityNumber &&
           limits.maximum_fields_per_entity > 0U &&
           limits.maximum_fields_per_entity <= kMaximumEntityFields &&
           limits.maximum_changed_fields_per_entity > 0U &&
           limits.maximum_changed_fields_per_entity <=
               kMaximumChangedFieldsPerEntity &&
           limits.maximum_snapshot_history > 0U &&
           limits.maximum_snapshot_history <= kMaximumEntitySnapshotHistory &&
           limits.maximum_snapshot_total_value_bytes > 0U &&
           limits.maximum_snapshot_total_value_bytes <=
               kMaximumEntitySnapshotValueBytes &&
           limits.maximum_source_payload_bytes > 0U &&
           limits.maximum_source_payload_bytes <=
               kMaximumEntitySourcePayloadBytes;
}

bool valid_entity_source_geometry(
    const EntitySourceGeometry& geometry,
    const EntitySnapshotLimits& limits) noexcept
{
    if (geometry.payload_byte_count > limits.maximum_source_payload_bytes ||
        geometry.payload_byte_count >
            (std::numeric_limits<std::size_t>::max)() / 8U) {
        return false;
    }
    const auto payload_bits = geometry.payload_byte_count * 8U;
    return geometry.start_bit_offset <= payload_bits &&
           geometry.bits_consumed <=
               payload_bits - geometry.start_bit_offset;
}

EntityBaselineKey::EntityBaselineKey(
    const EntityBaselineKeyKind kind,
    const std::uint32_t value) noexcept
    : kind_{kind}, value_{value}
{
}

EntityBaselineKey EntityBaselineKey::for_entity(
    const std::uint32_t entity_number) noexcept
{
    return EntityBaselineKey{
        EntityBaselineKeyKind::entity_number, entity_number};
}

EntityBaselineKey EntityBaselineKey::for_alternate_slot(
    const std::uint32_t slot) noexcept
{
    return EntityBaselineKey{EntityBaselineKeyKind::alternate_slot, slot};
}

EntityBaselineKeyKind EntityBaselineKey::kind() const noexcept
{
    return kind_;
}

std::uint32_t EntityBaselineKey::value() const noexcept
{
    return value_;
}

EntitySnapshotReference::EntitySnapshotReference(
    const std::uint32_t value,
    const EntitySnapshotReferencePolicy policy) noexcept
    : value_{value}, policy_{policy}
{
}

EntitySnapshotReference EntitySnapshotReference::synthetic(
    const std::uint32_t value) noexcept
{
    return EntitySnapshotReference{
        value,
        EntitySnapshotReferencePolicy::synthetic_uint32_non_wrapping};
}

std::uint32_t EntitySnapshotReference::value() const noexcept
{
    return value_;
}

EntitySnapshotReferencePolicy EntitySnapshotReference::policy() const noexcept
{
    return policy_;
}

EntityServerTime::EntityServerTime(const std::int64_t value) noexcept
    : raw_value_{value}
{
}

EntityServerTime EntityServerTime::synthetic_raw(
    const std::int64_t value) noexcept
{
    return EntityServerTime{value};
}

std::int64_t EntityServerTime::raw_value() const noexcept
{
    return raw_value_;
}

EntitySnapshotEvidenceProfile EntityServerTime::evidence_profile()
    const noexcept
{
    return EntitySnapshotEvidenceProfile::caller_supplied_typed_records;
}

GoldSrcEntityProjectionStatus GoldSrcEntityStateProjection::status()
    const noexcept
{
    return GoldSrcEntityProjectionStatus::stock_semantics_evidence_pending;
}

EntityBaselineState::EntityBaselineState(
    EntityBaselineKey key,
    const EntitySchemaCategory schema_category,
    std::shared_ptr<const DeltaObjectState> object,
    const EntitySourceGeometry source_geometry,
    const EntitySnapshotCompatibilityProfile compatibility_profile) noexcept
    : key_{std::move(key)},
      schema_category_{schema_category},
      object_{std::move(object)},
      source_geometry_{source_geometry},
      compatibility_profile_{compatibility_profile}
{
}

const EntityBaselineKey& EntityBaselineState::key() const noexcept
{
    return key_;
}

EntitySchemaCategory EntityBaselineState::schema_category() const noexcept
{
    return schema_category_;
}

std::string_view EntityBaselineState::schema_name() const noexcept
{
    return object_->schema_name();
}

const DeltaObjectState& EntityBaselineState::object() const noexcept
{
    return *object_;
}

const std::optional<GoldSrcEntityStateProjection>&
EntityBaselineState::semantic_projection() const noexcept
{
    return semantic_projection_;
}

const EntitySourceGeometry& EntityBaselineState::source_geometry()
    const noexcept
{
    return source_geometry_;
}

EntitySnapshotCompatibilityProfile
EntityBaselineState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

EntitySnapshotEvidenceProfile EntityBaselineState::evidence_profile()
    const noexcept
{
    return is_synthetic(compatibility_profile_)
               ? EntitySnapshotEvidenceProfile::caller_supplied_typed_records
               : EntitySnapshotEvidenceProfile::
                     stock_runtime_grammar_evidence_pending;
}

EntityBaselineRegistryState::EntityBaselineRegistryState(
    std::vector<EntityBaselineState> baselines,
    const std::size_t accounted_value_bytes,
    const EntitySnapshotCompatibilityProfile compatibility_profile) noexcept
    : baselines_{std::move(baselines)},
      accounted_value_bytes_{accounted_value_bytes},
      compatibility_profile_{compatibility_profile}
{
}

std::span<const EntityBaselineState>
EntityBaselineRegistryState::baselines() const noexcept
{
    return baselines_;
}

std::size_t EntityBaselineRegistryState::baseline_count() const noexcept
{
    return baselines_.size();
}

const EntityBaselineState* EntityBaselineRegistryState::find_exact(
    const EntityBaselineKey& key) const noexcept
{
    const auto found = std::find_if(
        baselines_.begin(),
        baselines_.end(),
        [&key](const EntityBaselineState& baseline) {
            return baseline.key() == key;
        });
    return found == baselines_.end() ? nullptr : std::addressof(*found);
}

std::size_t EntityBaselineRegistryState::accounted_value_bytes() const noexcept
{
    return accounted_value_bytes_;
}

EntitySnapshotCompatibilityProfile
EntityBaselineRegistryState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

EntitySnapshotEvidenceProfile EntityBaselineRegistryState::evidence_profile()
    const noexcept
{
    return is_synthetic(compatibility_profile_)
               ? EntitySnapshotEvidenceProfile::caller_supplied_typed_records
               : EntitySnapshotEvidenceProfile::
                     stock_runtime_grammar_evidence_pending;
}

EntityBaselineRegistryBuilder::EntityBaselineRegistryBuilder(
    const DeltaSchemaRegistryState& schemas,
    const EntitySnapshotLimits limits,
    const EntitySnapshotCompatibilityProfile profile)
    : schemas_{schemas}, limits_{limits}, profile_{profile}
{
}

bool EntityBaselineRegistryBuilder::valid_configuration() const noexcept
{
    return valid_entity_snapshot_limits(limits_) && valid_profile(profile_);
}

const EntitySnapshotLimits& EntityBaselineRegistryBuilder::limits()
    const noexcept
{
    return limits_;
}

EntitySnapshotCompatibilityProfile EntityBaselineRegistryBuilder::profile()
    const noexcept
{
    return profile_;
}

EntityBaselineInsertResult EntityBaselineRegistryBuilder::insert(
    EntityBaselineKey key,
    const EntitySchemaCategory schema_category,
    const DeltaObjectState& object,
    const EntitySourceGeometry source_geometry)
{
    const auto key_kind = key.kind();
    const auto key_value = key.value();
    if (!valid_configuration()) {
        return baseline_failure(
            EntityBaselineErrorCode::invalid_configuration,
            "Invalid entity baseline safety limits",
            key_kind,
            key_value);
    }
    if (!is_synthetic(profile_)) {
        return baseline_failure(
            EntityBaselineErrorCode::evidence_pending,
            "Stock baseline grammar and schema selection remain evidence pending",
            key_kind,
            key_value);
    }
    if (!valid_schema_category(schema_category)) {
        return baseline_failure(
            EntityBaselineErrorCode::unsupported_schema,
            "Entity baseline schema category is invalid",
            key_kind,
            key_value);
    }
    if (!valid_entity_source_geometry(source_geometry, limits_)) {
        return baseline_failure(
            EntityBaselineErrorCode::invalid_source_geometry,
            "Entity baseline source geometry is outside the bounded payload",
            key_kind,
            key_value);
    }
    if (!key_value_within_limit(key, limits_)) {
        return baseline_failure(
            EntityBaselineErrorCode::entity_number_limit_exceeded,
            "Entity baseline key exceeds the configured neutral limit",
            key_kind,
            key_value);
    }
    if (baselines_.size() >= limits_.maximum_baselines) {
        return baseline_failure(
            EntityBaselineErrorCode::baseline_limit_exceeded,
            "Entity baseline registry limit exceeded",
            key_kind,
            key_value);
    }
    if (std::any_of(
            baselines_.begin(),
            baselines_.end(),
            [&key](const EntityBaselineState& candidate) {
                return candidate.key() == key;
            })) {
        return baseline_failure(
            EntityBaselineErrorCode::duplicate_baseline_identity,
            "Duplicate entity baseline identity",
            key_kind,
            key_value);
    }
    const auto* schema = schemas_.find_exact(object.schema_name());
    if (schema == nullptr) {
        return baseline_failure(
            EntityBaselineErrorCode::unsupported_schema,
            "Entity baseline names a schema absent from the exact registry",
            key_kind,
            key_value);
    }
    if (schema->profile() != object.schema_profile()) {
        return baseline_failure(
            EntityBaselineErrorCode::schema_profile_mismatch,
            "Entity baseline object and schema registry profiles differ",
            key_kind,
            key_value);
    }
    if (!object.matches_schema(*schema)) {
        return baseline_failure(
            EntityBaselineErrorCode::unsupported_schema,
            "Entity baseline object does not match the exact registry schema descriptor",
            key_kind,
            key_value);
    }
    if (!object_matches_neutral_profile(object)) {
        return baseline_failure(
            EntityBaselineErrorCode::object_profile_mismatch,
            "Synthetic entity baselines require a synthetic delta object",
            key_kind,
            key_value);
    }
    if (object.field_count() > limits_.maximum_fields_per_entity) {
        return baseline_failure(
            EntityBaselineErrorCode::field_limit_exceeded,
            "Entity baseline field limit exceeded",
            key_kind,
            key_value);
    }
    if (add_overflows(
            accounted_value_bytes_, object.accounted_value_bytes())) {
        return baseline_failure(
            EntityBaselineErrorCode::size_overflow,
            "Entity baseline value-byte accounting overflow",
            key_kind,
            key_value);
    }
    const auto next_value_bytes =
        accounted_value_bytes_ + object.accounted_value_bytes();
    if (next_value_bytes > limits_.maximum_snapshot_total_value_bytes) {
        return baseline_failure(
            EntityBaselineErrorCode::total_value_bytes_exceeded,
            "Entity baseline registry value-byte limit exceeded",
            key_kind,
            key_value);
    }

    try {
        auto retained = std::make_shared<const DeltaObjectState>(object);
        baselines_.emplace_back(EntityBaselineState{
            std::move(key),
            schema_category,
            std::move(retained),
            source_geometry,
            profile_});
        accounted_value_bytes_ = next_value_bytes;
        return EntityBaselineInsertResult{true, std::nullopt};
    } catch (const std::bad_alloc&) {
        return baseline_failure(
            EntityBaselineErrorCode::unable_to_retain_baseline,
            "Unable to allocate bounded entity baseline state",
            key_kind,
            key_value);
    } catch (...) {
        return baseline_failure(
            EntityBaselineErrorCode::unable_to_retain_baseline,
            "Unable to retain entity baseline state",
            key_kind,
            key_value);
    }
}

EntityBaselinePublishResult EntityBaselineRegistryBuilder::publish() &&
{
    if (!valid_configuration()) {
        return baseline_publish_failure(
            EntityBaselineErrorCode::invalid_configuration,
            "Invalid entity baseline safety limits");
    }
    if (!is_synthetic(profile_)) {
        return baseline_publish_failure(
            EntityBaselineErrorCode::evidence_pending,
            "Stock entity baseline publication remains evidence pending");
    }
    return EntityBaselinePublishResult{
        EntityBaselineRegistryState{
            std::move(baselines_), accounted_value_bytes_, profile_},
        std::nullopt,
    };
}

std::span<const EntityBaselineState>
EntityBaselineRegistryBuilder::candidate_baselines() const noexcept
{
    return baselines_;
}

EntitySnapshotEntityInput::EntitySnapshotEntityInput(
    const std::uint32_t entity_number,
    EntityBaselineKey baseline_key,
    std::optional<DeltaObjectState> object) noexcept
    : entity_number_{entity_number},
      baseline_key_{std::move(baseline_key)},
      object_{std::move(object)}
{
}

EntitySnapshotEntityInput EntitySnapshotEntityInput::from_baseline(
    const std::uint32_t entity_number,
    EntityBaselineKey baseline_key)
{
    return EntitySnapshotEntityInput{
        entity_number, std::move(baseline_key), std::nullopt};
}

EntitySnapshotEntityInput EntitySnapshotEntityInput::with_decoded_state(
    const std::uint32_t entity_number,
    EntityBaselineKey baseline_key,
    const DeltaObjectState& object)
{
    return EntitySnapshotEntityInput{
        entity_number, std::move(baseline_key), object};
}

std::uint32_t EntitySnapshotEntityInput::entity_number() const noexcept
{
    return entity_number_;
}

const EntityBaselineKey& EntitySnapshotEntityInput::baseline_key()
    const noexcept
{
    return baseline_key_;
}

const std::optional<DeltaObjectState>&
EntitySnapshotEntityInput::decoded_state() const noexcept
{
    return object_;
}

EntitySnapshotEntityState::EntitySnapshotEntityState(
    const std::uint32_t entity_number,
    EntityBaselineKey baseline_key,
    const EntitySchemaCategory schema_category,
    std::shared_ptr<const DeltaObjectState> object,
    std::optional<GoldSrcEntityStateProjection> semantic_projection) noexcept
    : entity_number_{entity_number},
      baseline_key_{std::move(baseline_key)},
      schema_category_{schema_category},
      object_{std::move(object)},
      semantic_projection_{std::move(semantic_projection)}
{
}

std::uint32_t EntitySnapshotEntityState::entity_number() const noexcept
{
    return entity_number_;
}

const EntityBaselineKey& EntitySnapshotEntityState::baseline_key()
    const noexcept
{
    return baseline_key_;
}

EntitySchemaCategory EntitySnapshotEntityState::schema_category()
    const noexcept
{
    return schema_category_;
}

const DeltaObjectState& EntitySnapshotEntityState::object() const noexcept
{
    return *object_;
}

const std::optional<GoldSrcEntityStateProjection>&
EntitySnapshotEntityState::semantic_projection() const noexcept
{
    return semantic_projection_;
}

bool EntitySnapshotEntityState::shares_object_with(
    const EntitySnapshotEntityState& other) const noexcept
{
    return object_ == other.object_;
}

EntitySnapshotState::EntitySnapshotState(
    EntitySnapshotReference reference,
    EntityServerTime server_time,
    const EntitySnapshotKind kind,
    std::optional<EntitySnapshotReference> base_reference,
    std::vector<EntitySnapshotEntityState> entities,
    std::vector<std::uint32_t> removed_entity_numbers,
    const EntitySourceGeometry source_geometry,
    const EntitySnapshotStatistics statistics,
    const EntitySnapshotCompatibilityProfile compatibility_profile) noexcept
    : reference_{std::move(reference)},
      server_time_{std::move(server_time)},
      kind_{kind},
      base_reference_{std::move(base_reference)},
      entities_{std::move(entities)},
      removed_entity_numbers_{std::move(removed_entity_numbers)},
      source_geometry_{source_geometry},
      statistics_{statistics},
      compatibility_profile_{compatibility_profile}
{
}

const EntitySnapshotReference& EntitySnapshotState::reference() const noexcept
{
    return reference_;
}

const EntityServerTime& EntitySnapshotState::server_time() const noexcept
{
    return server_time_;
}

EntitySnapshotKind EntitySnapshotState::kind() const noexcept
{
    return kind_;
}

const std::optional<EntitySnapshotReference>&
EntitySnapshotState::base_reference() const noexcept
{
    return base_reference_;
}

std::span<const EntitySnapshotEntityState> EntitySnapshotState::entities()
    const noexcept
{
    return entities_;
}

std::size_t EntitySnapshotState::entity_count() const noexcept
{
    return entities_.size();
}

const EntitySnapshotEntityState* EntitySnapshotState::find_exact(
    const std::uint32_t entity_number) const noexcept
{
    const auto found = std::lower_bound(
        entities_.begin(),
        entities_.end(),
        entity_number,
        [](const EntitySnapshotEntityState& entity, const std::uint32_t value) {
            return entity.entity_number() < value;
        });
    return found != entities_.end() && found->entity_number() == entity_number
               ? std::addressof(*found)
               : nullptr;
}

std::span<const std::uint32_t>
EntitySnapshotState::removed_entity_numbers() const noexcept
{
    return removed_entity_numbers_;
}

const EntitySourceGeometry& EntitySnapshotState::source_geometry()
    const noexcept
{
    return source_geometry_;
}

const EntitySnapshotStatistics& EntitySnapshotState::statistics()
    const noexcept
{
    return statistics_;
}

EntitySnapshotCompatibilityProfile
EntitySnapshotState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

EntitySnapshotEvidenceProfile EntitySnapshotState::evidence_profile()
    const noexcept
{
    return is_synthetic(compatibility_profile_)
               ? EntitySnapshotEvidenceProfile::caller_supplied_typed_records
               : EntitySnapshotEvidenceProfile::
                     stock_runtime_grammar_evidence_pending;
}

EntitySnapshotHistoryState::EntitySnapshotHistoryState(
    std::vector<EntitySnapshotState> snapshots,
    std::vector<EntitySnapshotReference> required_base_references,
    std::optional<EntitySnapshotReference> evicted_through,
    const std::size_t accounted_value_bytes,
    const EntitySnapshotCompatibilityProfile compatibility_profile) noexcept
    : snapshots_{std::move(snapshots)},
      required_base_references_{std::move(required_base_references)},
      evicted_through_{std::move(evicted_through)},
      accounted_value_bytes_{accounted_value_bytes},
      compatibility_profile_{compatibility_profile}
{
}

std::span<const EntitySnapshotState> EntitySnapshotHistoryState::snapshots()
    const noexcept
{
    return snapshots_;
}

std::size_t EntitySnapshotHistoryState::snapshot_count() const noexcept
{
    return snapshots_.size();
}

const EntitySnapshotState* EntitySnapshotHistoryState::find_exact(
    const EntitySnapshotReference& reference) const noexcept
{
    const auto found = std::find_if(
        snapshots_.begin(),
        snapshots_.end(),
        [&reference](const EntitySnapshotState& snapshot) {
            return snapshot.reference() == reference;
        });
    return found == snapshots_.end() ? nullptr : std::addressof(*found);
}

EntitySnapshotHistoryReferenceStatus EntitySnapshotHistoryState::classify(
    const EntitySnapshotReference& reference) const noexcept
{
    if (find_exact(reference) != nullptr) {
        return EntitySnapshotHistoryReferenceStatus::retained;
    }
    if (reference.policy() !=
        EntitySnapshotReferencePolicy::synthetic_uint32_non_wrapping) {
        return EntitySnapshotHistoryReferenceStatus::missing;
    }
    if (!snapshots_.empty() &&
        reference.value() > snapshots_.back().reference().value()) {
        return EntitySnapshotHistoryReferenceStatus::future;
    }
    if (evicted_through_ &&
        reference.value() <= evicted_through_->value()) {
        return EntitySnapshotHistoryReferenceStatus::evicted;
    }
    return EntitySnapshotHistoryReferenceStatus::missing;
}

std::optional<EntitySnapshotReference>
EntitySnapshotHistoryState::oldest_reference() const noexcept
{
    if (snapshots_.empty()) {
        return std::nullopt;
    }
    return snapshots_.front().reference();
}

std::optional<EntitySnapshotReference>
EntitySnapshotHistoryState::newest_reference() const noexcept
{
    if (snapshots_.empty()) {
        return std::nullopt;
    }
    return snapshots_.back().reference();
}

std::optional<EntitySnapshotReference>
EntitySnapshotHistoryState::evicted_through() const noexcept
{
    return evicted_through_;
}

std::span<const EntitySnapshotReference>
EntitySnapshotHistoryState::required_base_references() const noexcept
{
    return required_base_references_;
}

std::size_t EntitySnapshotHistoryState::accounted_value_bytes() const noexcept
{
    return accounted_value_bytes_;
}

EntitySnapshotCompatibilityProfile
EntitySnapshotHistoryState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

EntitySnapshotHistoryBuilder::EntitySnapshotHistoryBuilder(
    const EntitySnapshotLimits limits,
    const EntitySnapshotCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool EntitySnapshotHistoryBuilder::valid_configuration() const noexcept
{
    return valid_entity_snapshot_limits(limits_) && valid_profile(profile_);
}

const EntitySnapshotLimits& EntitySnapshotHistoryBuilder::limits()
    const noexcept
{
    return limits_;
}

const EntitySnapshotState* EntitySnapshotHistoryBuilder::find_candidate_exact(
    const EntitySnapshotReference& reference) const noexcept
{
    const auto found = std::find_if(
        snapshots_.begin(),
        snapshots_.end(),
        [&reference](const EntitySnapshotState& snapshot) {
            return snapshot.reference() == reference;
        });
    return found == snapshots_.end() ? nullptr : std::addressof(*found);
}

bool EntitySnapshotHistoryBuilder::is_required(
    const EntitySnapshotReference& reference) const noexcept
{
    return std::any_of(
        required_base_references_.begin(),
        required_base_references_.end(),
        [&reference](const EntitySnapshotReference& candidate) {
            return candidate == reference;
        });
}

EntitySnapshotHistoryMutationResult EntitySnapshotHistoryBuilder::insert(
    const EntitySnapshotState& snapshot)
{
    const auto reference_value = snapshot.reference().value();
    if (!valid_configuration()) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::invalid_configuration,
            "Invalid entity snapshot history safety limits",
            reference_value);
    }
    if (!is_synthetic(profile_)) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::wrap_policy_evidence_pending,
            "Stock snapshot reference width and wrap policy remain evidence pending",
            reference_value);
    }
    if (snapshot.compatibility_profile() != profile_ ||
        !is_synthetic(snapshot.reference())) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::incompatible_snapshot_profile,
            "Snapshot and history compatibility profiles differ",
            reference_value);
    }
    if (snapshot.entity_count() > limits_.maximum_entities_per_snapshot ||
        snapshot.removed_entity_numbers().size() >
            limits_.maximum_entities_per_snapshot) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::history_limit_exceeded,
            "Snapshot entity or removal count exceeds the configured history limit",
            reference_value);
    }
    for (const auto& entity : snapshot.entities()) {
        if (entity.entity_number() > limits_.maximum_entity_number ||
            !key_value_within_limit(entity.baseline_key(), limits_)) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::history_limit_exceeded,
                "Snapshot entity or baseline key exceeds the configured history limit",
                reference_value);
        }
        if (entity.baseline_key().kind() ==
                EntityBaselineKeyKind::entity_number &&
            entity.baseline_key().value() != entity.entity_number()) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::incompatible_snapshot_profile,
                "Snapshot contains a mismatched entity-number baseline key",
                reference_value);
        }
        if (entity.object().field_count() >
            limits_.maximum_fields_per_entity) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::history_limit_exceeded,
                "Snapshot entity field count exceeds the configured history limit",
                reference_value);
        }
        if (!valid_schema_category(entity.schema_category()) ||
            !object_matches_neutral_profile(entity.object())) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::incompatible_snapshot_profile,
                "Snapshot contains an incompatible schema category or object profile",
                reference_value);
        }
    }
    if (std::any_of(
            snapshot.removed_entity_numbers().begin(),
            snapshot.removed_entity_numbers().end(),
            [this](const std::uint32_t number) noexcept {
                return number > limits_.maximum_entity_number;
            })) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::history_limit_exceeded,
            "Snapshot removal exceeds the configured entity-number limit",
            reference_value);
    }
    if (find_candidate_exact(snapshot.reference()) != nullptr) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::duplicate_snapshot,
            "Duplicate entity snapshot reference",
            reference_value);
    }
    if (!snapshots_.empty() &&
        reference_value < snapshots_.back().reference().value()) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::old_snapshot,
            "Non-wrapping synthetic snapshot reference moved backwards",
            reference_value);
    }
    if (snapshot.kind() == EntitySnapshotKind::delta) {
        if (!snapshot.base_reference()) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::missing_snapshot_base,
                "Delta snapshot has no exact base reference",
                reference_value);
        }
        const auto& base = *snapshot.base_reference();
        if (!reference_less(base, snapshot.reference())) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::future_base_reference,
                "Delta snapshot base is not earlier than the new reference",
                base.value());
        }
        if (find_candidate_exact(base) == nullptr) {
            if (!snapshots_.empty() &&
                base.value() > snapshots_.back().reference().value()) {
                return history_failure(
                    EntitySnapshotHistoryErrorCode::future_base_reference,
                    "Delta snapshot base is newer than retained history",
                    base.value());
            }
            if (evicted_through_ &&
                base.value() <= evicted_through_->value()) {
                return history_failure(
                    EntitySnapshotHistoryErrorCode::evicted_snapshot_base,
                    "Delta snapshot base was evicted from bounded history",
                    base.value());
            }
            return history_failure(
                EntitySnapshotHistoryErrorCode::missing_snapshot_base,
                "Exact delta snapshot base is absent from history",
                base.value());
        }
    }
    if (snapshot.statistics().accounted_value_bytes >
        limits_.maximum_snapshot_total_value_bytes) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::history_limit_exceeded,
            "Snapshot value bytes exceed the configured history policy",
            reference_value);
    }

    const bool must_evict =
        snapshots_.size() == limits_.maximum_snapshot_history;
    if (must_evict && is_required(snapshots_.front().reference())) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::required_base_retention_limit,
            "Oldest snapshot is retained for a pending exact-base dependency",
            snapshots_.front().reference().value());
    }

    try {
        std::vector<EntitySnapshotState> next;
        next.reserve(
            must_evict ? snapshots_.size() : snapshots_.size() + 1U);
        const auto first = must_evict ? 1U : 0U;
        std::size_t next_accounted = 0U;
        for (std::size_t index = first; index < snapshots_.size(); ++index) {
            if (add_overflows(
                    next_accounted,
                    snapshots_[index].statistics().accounted_value_bytes)) {
                return history_failure(
                    EntitySnapshotHistoryErrorCode::size_overflow,
                    "Entity snapshot history value-byte accounting overflow",
                    reference_value);
            }
            next_accounted +=
                snapshots_[index].statistics().accounted_value_bytes;
            next.emplace_back(snapshots_[index]);
        }
        if (add_overflows(
                next_accounted,
                snapshot.statistics().accounted_value_bytes)) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::size_overflow,
                "Entity snapshot history value-byte accounting overflow",
                reference_value);
        }
        next_accounted += snapshot.statistics().accounted_value_bytes;
        if (next_accounted >
            limits_.maximum_snapshot_total_value_bytes) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::history_limit_exceeded,
                "Entity snapshot history exceeds the configured aggregate value-byte bound",
                reference_value);
        }
        next.emplace_back(snapshot);

        std::optional<EntitySnapshotReference> next_evicted;
        if (evicted_through_) {
            next_evicted.emplace(*evicted_through_);
        }
        if (must_evict) {
            next_evicted.reset();
            next_evicted.emplace(snapshots_.front().reference());
        }

        snapshots_.swap(next);
        accounted_value_bytes_ = next_accounted;
        evicted_through_.reset();
        if (next_evicted) {
            evicted_through_.emplace(*next_evicted);
        }
        return EntitySnapshotHistoryMutationResult{true, std::nullopt};
    } catch (const std::bad_alloc&) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::unable_to_retain_history,
            "Unable to allocate bounded entity snapshot history",
            reference_value);
    } catch (...) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::unable_to_retain_history,
            "Unable to retain entity snapshot history",
            reference_value);
    }
}

EntitySnapshotHistoryMutationResult
EntitySnapshotHistoryBuilder::retain_required_base(
    const EntitySnapshotReference& reference)
{
    if (!valid_configuration()) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::invalid_configuration,
            "Invalid entity snapshot history safety limits",
            reference.value());
    }
    if (!is_synthetic(profile_)) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::wrap_policy_evidence_pending,
            "Stock snapshot base retention remains evidence pending",
            reference.value());
    }
    if (find_candidate_exact(reference) == nullptr) {
        if (evicted_through_ &&
            reference.value() <= evicted_through_->value()) {
            return history_failure(
                EntitySnapshotHistoryErrorCode::evicted_snapshot_base,
                "Cannot retain an already evicted snapshot base",
                reference.value());
        }
        return history_failure(
            EntitySnapshotHistoryErrorCode::required_base_not_retained,
            "Required snapshot base is not retained",
            reference.value());
    }
    if (is_required(reference)) {
        return EntitySnapshotHistoryMutationResult{false, std::nullopt};
    }
    try {
        std::vector<EntitySnapshotReference> next;
        next.reserve(required_base_references_.size() + 1U);
        for (const auto& candidate : required_base_references_) {
            next.emplace_back(candidate);
        }
        next.emplace_back(reference);
        required_base_references_.swap(next);
        return EntitySnapshotHistoryMutationResult{true, std::nullopt};
    } catch (...) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::unable_to_retain_history,
            "Unable to retain pending snapshot base metadata",
            reference.value());
    }
}

EntitySnapshotHistoryMutationResult
EntitySnapshotHistoryBuilder::release_required_base(
    const EntitySnapshotReference& reference)
{
    if (!is_required(reference)) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::required_base_not_retained,
            "Snapshot reference is not marked as a required pending base",
            reference.value());
    }
    try {
        std::vector<EntitySnapshotReference> next;
        next.reserve(required_base_references_.size() - 1U);
        for (const auto& candidate : required_base_references_) {
            if (!(candidate == reference)) {
                next.emplace_back(candidate);
            }
        }
        required_base_references_.swap(next);
        return EntitySnapshotHistoryMutationResult{true, std::nullopt};
    } catch (...) {
        return history_failure(
            EntitySnapshotHistoryErrorCode::unable_to_retain_history,
            "Unable to release pending snapshot base metadata",
            reference.value());
    }
}

EntitySnapshotHistoryPublishResult EntitySnapshotHistoryBuilder::publish()
    const
{
    if (!valid_configuration()) {
        return history_publish_failure(
            EntitySnapshotHistoryErrorCode::invalid_configuration,
            "Invalid entity snapshot history safety limits");
    }
    if (!is_synthetic(profile_)) {
        return history_publish_failure(
            EntitySnapshotHistoryErrorCode::wrap_policy_evidence_pending,
            "Stock snapshot history policy remains evidence pending");
    }
    try {
        return EntitySnapshotHistoryPublishResult{
            EntitySnapshotHistoryState{
                snapshots_,
                required_base_references_,
                evicted_through_,
                accounted_value_bytes_,
                profile_},
            std::nullopt,
        };
    } catch (...) {
        return history_publish_failure(
            EntitySnapshotHistoryErrorCode::unable_to_retain_history,
            "Unable to publish immutable entity snapshot history");
    }
}

std::size_t EntitySnapshotHistoryBuilder::candidate_snapshot_count()
    const noexcept
{
    return snapshots_.size();
}

EntityFullSnapshotBuilder::EntityFullSnapshotBuilder(
    const EntitySnapshotLimits limits,
    const EntitySnapshotCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool EntityFullSnapshotBuilder::valid_configuration() const noexcept
{
    return valid_entity_snapshot_limits(limits_) && valid_profile(profile_);
}

EntitySnapshotBuildResult EntityFullSnapshotBuilder::build(
    EntitySnapshotReference reference,
    EntityServerTime server_time,
    const EntityBaselineRegistryState& baselines,
    const std::span<const EntitySnapshotEntityInput> entities,
    const EntitySourceGeometry source_geometry) const
{
    if (!valid_configuration()) {
        return snapshot_failure(
            EntitySnapshotErrorCode::invalid_configuration,
            "Invalid full entity snapshot safety limits",
            std::nullopt,
            reference.value());
    }
    if (!is_synthetic(profile_)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::evidence_pending,
            "Stock full entity snapshot grammar remains evidence pending",
            std::nullopt,
            reference.value());
    }
    if (!is_synthetic(reference)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::incompatible_reference_profile,
            "Full snapshot reference is outside the synthetic domain",
            std::nullopt,
            reference.value());
    }
    if (baselines.compatibility_profile() != profile_) {
        return snapshot_failure(
            EntitySnapshotErrorCode::incompatible_baseline_registry,
            "Full snapshot and baseline registry profiles differ",
            std::nullopt,
            reference.value());
    }
    if (!valid_entity_source_geometry(source_geometry, limits_)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::invalid_source_geometry,
            "Full snapshot source geometry is outside the bounded payload",
            std::nullopt,
            reference.value());
    }
    if (entities.size() > limits_.maximum_entities_per_snapshot) {
        return snapshot_failure(
            EntitySnapshotErrorCode::entity_limit_exceeded,
            "Full snapshot entity limit exceeded",
            std::nullopt,
            reference.value());
    }

    try {
        std::vector<EntitySnapshotEntityState> retained;
        retained.reserve(entities.size());
        std::size_t total_value_bytes = 0U;
        std::size_t explicit_state_count = 0U;
        std::optional<std::uint32_t> previous;
        for (const auto& input : entities) {
            const auto entity_number = input.entity_number();
            if (entity_number > limits_.maximum_entity_number) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::entity_number_limit_exceeded,
                    "Full snapshot entity number exceeds the configured limit",
                    entity_number,
                    reference.value());
            }
            if (previous && entity_number == *previous) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::duplicate_entity,
                    "Full snapshot contains a duplicate entity number",
                    entity_number,
                    reference.value());
            }
            if (previous && entity_number < *previous) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::out_of_order_entity,
                    "Full snapshot input is not strictly ascending",
                    entity_number,
                    reference.value());
            }
            previous = entity_number;

            if (!key_value_within_limit(input.baseline_key(), limits_)) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::entity_number_limit_exceeded,
                    "Full snapshot baseline key exceeds the configured limit",
                    entity_number,
                    reference.value());
            }
            if (input.baseline_key().kind() ==
                    EntityBaselineKeyKind::entity_number &&
                input.baseline_key().value() != entity_number) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::baseline_identity_mismatch,
                    "Full snapshot entity-number baseline key names a different entity",
                    entity_number,
                    reference.value());
            }
            const auto* baseline = baselines.find_exact(input.baseline_key());
            if (baseline == nullptr) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::missing_baseline,
                    "Full snapshot entity has no exact baseline",
                    entity_number,
                    reference.value());
            }

            std::shared_ptr<const DeltaObjectState> object;
            if (input.decoded_state()) {
                const auto& decoded = *input.decoded_state();
                if (!object_matches_neutral_profile(decoded)) {
                    return snapshot_failure(
                        EntitySnapshotErrorCode::object_profile_mismatch,
                        "Full snapshot requires a synthetic delta object",
                        entity_number,
                        reference.value());
                }
                if (!decoded.has_same_schema_as(baseline->object())) {
                    return snapshot_failure(
                        EntitySnapshotErrorCode::baseline_schema_mismatch,
                        "Full snapshot object does not match its exact baseline schema descriptor",
                        entity_number,
                        reference.value());
                }
                object = std::make_shared<const DeltaObjectState>(decoded);
                ++explicit_state_count;
            } else {
                object = baseline->object_;
            }
            if (object->field_count() > limits_.maximum_fields_per_entity) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::field_limit_exceeded,
                    "Full snapshot entity field limit exceeded",
                    entity_number,
                    reference.value());
            }
            if (add_overflows(
                    total_value_bytes, object->accounted_value_bytes())) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::size_overflow,
                    "Full snapshot value-byte accounting overflow",
                    entity_number,
                    reference.value());
            }
            total_value_bytes += object->accounted_value_bytes();
            if (total_value_bytes >
                limits_.maximum_snapshot_total_value_bytes) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::total_value_bytes_exceeded,
                    "Full snapshot value-byte limit exceeded",
                    entity_number,
                    reference.value());
            }
            retained.emplace_back(EntitySnapshotEntityState{
                entity_number,
                input.baseline_key(),
                baseline->schema_category(),
                std::move(object),
                baseline->semantic_projection()});
        }

        const EntitySnapshotStatistics statistics{
            retained.size(),
            explicit_state_count,
            retained.size(),
            0U,
            retained.size() - explicit_state_count,
            total_value_bytes,
        };
        return EntitySnapshotBuildResult{
            EntitySnapshotState{
                std::move(reference),
                std::move(server_time),
                EntitySnapshotKind::full,
                std::nullopt,
                std::move(retained),
                {},
                source_geometry,
                statistics,
                profile_},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return snapshot_failure(
            EntitySnapshotErrorCode::unable_to_retain_snapshot,
            "Unable to allocate bounded full entity snapshot",
            std::nullopt,
            reference.value());
    } catch (...) {
        return snapshot_failure(
            EntitySnapshotErrorCode::unable_to_retain_snapshot,
            "Unable to retain full entity snapshot",
            std::nullopt,
            reference.value());
    }
}

EntityDeltaSnapshotBuilder::EntityDeltaSnapshotBuilder(
    const EntitySnapshotLimits limits,
    const EntitySnapshotCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool EntityDeltaSnapshotBuilder::valid_configuration() const noexcept
{
    return valid_entity_snapshot_limits(limits_) && valid_profile(profile_);
}

EntitySnapshotBuildResult EntityDeltaSnapshotBuilder::build(
    EntitySnapshotReference reference,
    EntityServerTime server_time,
    EntitySnapshotReference base_reference,
    const EntitySnapshotHistoryState& history,
    const EntityBaselineRegistryState& baselines,
    const std::span<const EntitySnapshotEntityInput> updates,
    const std::span<const std::uint32_t> removals,
    const EntitySourceGeometry source_geometry) const
{
    if (!valid_configuration()) {
        return snapshot_failure(
            EntitySnapshotErrorCode::invalid_configuration,
            "Invalid delta entity snapshot safety limits",
            std::nullopt,
            reference.value());
    }
    if (!is_synthetic(profile_)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::evidence_pending,
            "Stock delta entity snapshot grammar remains evidence pending",
            std::nullopt,
            reference.value());
    }
    if (!is_synthetic(reference) || !is_synthetic(base_reference)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::incompatible_reference_profile,
            "Delta snapshot references are outside the synthetic domain",
            std::nullopt,
            reference.value());
    }
    if (history.compatibility_profile() != profile_) {
        return snapshot_failure(
            EntitySnapshotErrorCode::wrong_delta_snapshot_base,
            "Delta snapshot history has a different compatibility profile",
            std::nullopt,
            base_reference.value());
    }
    if (!reference_less(base_reference, reference)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::future_delta_snapshot_base,
            "Delta snapshot base is not earlier than the new reference",
            std::nullopt,
            base_reference.value());
    }
    const auto* base = history.find_exact(base_reference);
    if (base == nullptr) {
        switch (history.classify(base_reference)) {
        case EntitySnapshotHistoryReferenceStatus::evicted:
            return snapshot_failure(
                EntitySnapshotErrorCode::evicted_delta_snapshot_base,
                "Exact delta snapshot base was evicted",
                std::nullopt,
                base_reference.value());
        case EntitySnapshotHistoryReferenceStatus::future:
            return snapshot_failure(
                EntitySnapshotErrorCode::future_delta_snapshot_base,
                "Delta snapshot base is newer than retained history",
                std::nullopt,
                base_reference.value());
        case EntitySnapshotHistoryReferenceStatus::missing:
        case EntitySnapshotHistoryReferenceStatus::retained:
            return snapshot_failure(
                EntitySnapshotErrorCode::missing_delta_snapshot_base,
                "Exact delta snapshot base is absent",
                std::nullopt,
                base_reference.value());
        }
    }
    return build_with_resolved_base(
        std::move(reference),
        std::move(server_time),
        std::move(base_reference),
        *base,
        baselines,
        updates,
        removals,
        source_geometry);
}

EntitySnapshotBuildResult
EntityDeltaSnapshotBuilder::build_with_resolved_base(
    EntitySnapshotReference reference,
    EntityServerTime server_time,
    EntitySnapshotReference declared_base_reference,
    const EntitySnapshotState& resolved_base,
    const EntityBaselineRegistryState& baselines,
    const std::span<const EntitySnapshotEntityInput> updates,
    const std::span<const std::uint32_t> removals,
    const EntitySourceGeometry source_geometry) const
{
    const auto reference_value = reference.value();
    if (!valid_configuration()) {
        return snapshot_failure(
            EntitySnapshotErrorCode::invalid_configuration,
            "Invalid delta entity snapshot safety limits",
            std::nullopt,
            reference_value);
    }
    if (!is_synthetic(profile_)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::evidence_pending,
            "Stock delta entity snapshot grammar remains evidence pending",
            std::nullopt,
            reference_value);
    }
    if (!is_synthetic(reference) || !is_synthetic(declared_base_reference)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::incompatible_reference_profile,
            "Delta snapshot references are outside the synthetic domain",
            std::nullopt,
            reference_value);
    }
    if (!(resolved_base.reference() == declared_base_reference)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::wrong_delta_snapshot_base,
            "Resolved snapshot does not match the declared exact base",
            std::nullopt,
            declared_base_reference.value());
    }
    if (!reference_less(declared_base_reference, reference)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::future_delta_snapshot_base,
            "Delta snapshot base is not earlier than the new reference",
            std::nullopt,
            declared_base_reference.value());
    }
    if (resolved_base.compatibility_profile() != profile_ ||
        baselines.compatibility_profile() != profile_) {
        return snapshot_failure(
            EntitySnapshotErrorCode::incompatible_baseline_registry,
            "Delta snapshot base or baseline registry profile differs",
            std::nullopt,
            declared_base_reference.value());
    }
    if (!valid_entity_source_geometry(source_geometry, limits_)) {
        return snapshot_failure(
            EntitySnapshotErrorCode::invalid_source_geometry,
            "Delta snapshot source geometry is outside the bounded payload",
            std::nullopt,
            reference_value);
    }
    if (resolved_base.entity_count() >
        limits_.maximum_entities_per_snapshot) {
        return snapshot_failure(
            EntitySnapshotErrorCode::entity_limit_exceeded,
            "Resolved delta base exceeds the configured entity limit",
            std::nullopt,
            declared_base_reference.value());
    }
    for (const auto& entity : resolved_base.entities()) {
        const auto number = entity.entity_number();
        if (number > limits_.maximum_entity_number ||
            !key_value_within_limit(entity.baseline_key(), limits_)) {
            return snapshot_failure(
                EntitySnapshotErrorCode::entity_number_limit_exceeded,
                "Resolved delta base contains an entity or baseline key outside the configured limit",
                number,
                declared_base_reference.value());
        }
        if (entity.baseline_key().kind() ==
                EntityBaselineKeyKind::entity_number &&
            entity.baseline_key().value() != number) {
            return snapshot_failure(
                EntitySnapshotErrorCode::baseline_identity_mismatch,
                "Resolved delta base contains a mismatched entity-number baseline key",
                number,
                declared_base_reference.value());
        }
        if (entity.object().field_count() >
            limits_.maximum_fields_per_entity) {
            return snapshot_failure(
                EntitySnapshotErrorCode::field_limit_exceeded,
                "Resolved delta base contains an entity above the configured field limit",
                number,
                declared_base_reference.value());
        }
        if (!object_matches_neutral_profile(entity.object())) {
            return snapshot_failure(
                EntitySnapshotErrorCode::object_profile_mismatch,
                "Resolved delta base contains a non-synthetic delta object",
                number,
                declared_base_reference.value());
        }
        const auto* const baseline =
            baselines.find_exact(entity.baseline_key());
        if (baseline == nullptr) {
            return snapshot_failure(
                EntitySnapshotErrorCode::missing_baseline,
                "Resolved delta base has no exact baseline in the current registry",
                number,
                declared_base_reference.value());
        }
        if (baseline->schema_category() != entity.schema_category() ||
            !entity.object().has_same_schema_as(baseline->object())) {
            return snapshot_failure(
                EntitySnapshotErrorCode::baseline_schema_mismatch,
                "Resolved delta base does not match the current baseline category and schema descriptor",
                number,
                declared_base_reference.value());
        }
    }
    if (updates.size() > limits_.maximum_entities_per_snapshot ||
        removals.size() > limits_.maximum_entities_per_snapshot) {
        return snapshot_failure(
            EntitySnapshotErrorCode::entity_limit_exceeded,
            "Delta snapshot update or removal limit exceeded",
            std::nullopt,
            reference_value);
    }

    try {
        std::optional<std::uint32_t> previous;
        for (const auto& update : updates) {
            const auto number = update.entity_number();
            if (number > limits_.maximum_entity_number) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::entity_number_limit_exceeded,
                    "Delta update entity number exceeds the configured limit",
                    number,
                    reference_value);
            }
            if (previous && number == *previous) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::duplicate_entity,
                    "Delta snapshot contains a duplicate entity update",
                    number,
                    reference_value);
            }
            if (previous && number < *previous) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::out_of_order_entity,
                    "Delta snapshot updates are not strictly ascending",
                    number,
                    reference_value);
            }
            previous = number;
        }

        previous.reset();
        for (const auto number : removals) {
            if (number > limits_.maximum_entity_number) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::entity_number_limit_exceeded,
                    "Delta removal entity number exceeds the configured limit",
                    number,
                    reference_value);
            }
            if (previous && number == *previous) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::duplicate_removal,
                    "Delta snapshot contains a duplicate explicit removal",
                    number,
                    reference_value);
            }
            if (previous && number < *previous) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::out_of_order_removal,
                    "Delta snapshot removals are not strictly ascending",
                    number,
                    reference_value);
            }
            previous = number;
            if (resolved_base.find_exact(number) == nullptr) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::remove_nonexistent_entity,
                    "Explicit removal names an entity absent from the base",
                    number,
                    reference_value);
            }
            const auto conflict = std::lower_bound(
                updates.begin(),
                updates.end(),
                number,
                [](const EntitySnapshotEntityInput& update,
                   const std::uint32_t value) {
                    return update.entity_number() < value;
                });
            if (conflict != updates.end() &&
                conflict->entity_number() == number) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::update_remove_conflict,
                    "Entity cannot be updated and removed in one snapshot",
                    number,
                    reference_value);
            }
        }

        std::vector<EntitySnapshotEntityState> validated_updates;
        validated_updates.reserve(updates.size());
        std::size_t changed_count = 0U;
        std::size_t added_count = 0U;
        for (const auto& update : updates) {
            const auto number = update.entity_number();
            if (!key_value_within_limit(update.baseline_key(), limits_)) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::entity_number_limit_exceeded,
                    "Delta update baseline key exceeds the configured limit",
                    number,
                    reference_value);
            }
            if (update.baseline_key().kind() ==
                    EntityBaselineKeyKind::entity_number &&
                update.baseline_key().value() != number) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::baseline_identity_mismatch,
                    "Delta update entity-number baseline key names a different entity",
                    number,
                    reference_value);
            }
            const auto* baseline = baselines.find_exact(update.baseline_key());
            if (baseline == nullptr) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::missing_baseline,
                    "Delta entity update has no exact baseline",
                    number,
                    reference_value);
            }
            const auto* base_entity = resolved_base.find_exact(number);
            if (base_entity != nullptr &&
                !(base_entity->baseline_key() == update.baseline_key())) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::baseline_schema_mismatch,
                    "Delta update changed an existing entity baseline identity",
                    number,
                    reference_value);
            }

            std::shared_ptr<const DeltaObjectState> object;
            if (update.decoded_state()) {
                const auto& decoded = *update.decoded_state();
                if (!object_matches_neutral_profile(decoded)) {
                    return snapshot_failure(
                        EntitySnapshotErrorCode::object_profile_mismatch,
                        "Delta snapshot requires a synthetic delta object",
                        number,
                        reference_value);
                }
                if (!decoded.has_same_schema_as(baseline->object())) {
                    return snapshot_failure(
                        base_entity == nullptr
                            ? EntitySnapshotErrorCode::baseline_schema_mismatch
                            : EntitySnapshotErrorCode::schema_mismatch,
                        "Delta object does not match the required exact entity schema descriptor",
                        number,
                        reference_value);
                }
                if (base_entity != nullptr &&
                    !decoded.has_same_schema_as(base_entity->object())) {
                    return snapshot_failure(
                        EntitySnapshotErrorCode::schema_mismatch,
                        "Delta update changed an existing entity schema descriptor",
                        number,
                        reference_value);
                }
                object = std::make_shared<const DeltaObjectState>(decoded);
            } else {
                object = baseline->object_;
            }
            if (object->field_count() > limits_.maximum_fields_per_entity) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::field_limit_exceeded,
                    "Delta entity total field limit exceeded",
                    number,
                    reference_value);
            }
            const auto& comparison_object =
                base_entity != nullptr ? base_entity->object()
                                       : baseline->object();
            if (!comparison_object.has_same_schema_as(*object)) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::schema_mismatch,
                    "Delta entity comparison object has a different schema descriptor",
                    number,
                    reference_value);
            }
            std::size_t changed_field_count = 0U;
            for (std::size_t field_index = 0U;
                 field_index < object->field_count();
                 ++field_index) {
                const auto& before = comparison_object.fields()[field_index];
                const auto& after = object->fields()[field_index];
                if (before.value() != after.value()) {
                    ++changed_field_count;
                }
            }
            if (changed_field_count >
                limits_.maximum_changed_fields_per_entity) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::field_limit_exceeded,
                    "Delta entity changed-field limit exceeded",
                    number,
                    reference_value);
            }
            if (base_entity == nullptr) {
                ++added_count;
            } else if (changed_field_count == 0U) {
                continue;
            } else {
                ++changed_count;
            }
            validated_updates.emplace_back(EntitySnapshotEntityState{
                number,
                update.baseline_key(),
                baseline->schema_category(),
                std::move(object),
                baseline->semantic_projection()});
        }

        if (add_overflows(resolved_base.entity_count(), added_count) ||
            resolved_base.entity_count() + added_count < removals.size()) {
            return snapshot_failure(
                EntitySnapshotErrorCode::size_overflow,
                "Delta snapshot entity-count accounting overflow",
                std::nullopt,
                reference_value);
        }
        const auto resulting_count =
            resolved_base.entity_count() + added_count - removals.size();
        if (resulting_count > limits_.maximum_entities_per_snapshot) {
            return snapshot_failure(
                EntitySnapshotErrorCode::entity_limit_exceeded,
                "Delta snapshot resulting entity limit exceeded",
                std::nullopt,
                reference_value);
        }

        std::vector<EntitySnapshotEntityState> result_entities;
        result_entities.reserve(resulting_count);
        std::size_t base_index = 0U;
        std::size_t update_index = 0U;
        std::size_t removal_index = 0U;
        std::size_t unchanged_shared_count = 0U;
        while (base_index < resolved_base.entities().size() ||
               update_index < validated_updates.size()) {
            const auto base_number =
                base_index < resolved_base.entities().size()
                    ? resolved_base.entities()[base_index].entity_number()
                    : (std::numeric_limits<std::uint32_t>::max)();
            const auto update_number =
                update_index < validated_updates.size()
                    ? validated_updates[update_index].entity_number()
                    : (std::numeric_limits<std::uint32_t>::max)();

            if (base_number < update_number) {
                if (removal_index < removals.size() &&
                    removals[removal_index] == base_number) {
                    ++removal_index;
                } else {
                    result_entities.emplace_back(
                        resolved_base.entities()[base_index]);
                    ++unchanged_shared_count;
                }
                ++base_index;
            } else if (update_number < base_number) {
                result_entities.emplace_back(
                    validated_updates[update_index]);
                ++update_index;
            } else {
                result_entities.emplace_back(
                    validated_updates[update_index]);
                ++base_index;
                ++update_index;
            }
        }

        if (result_entities.size() != resulting_count ||
            removal_index != removals.size()) {
            return snapshot_failure(
                EntitySnapshotErrorCode::size_overflow,
                "Delta snapshot merge invariant failed",
                std::nullopt,
                reference_value);
        }

        std::size_t total_value_bytes = 0U;
        for (const auto& entity : result_entities) {
            if (add_overflows(
                    total_value_bytes,
                    entity.object().accounted_value_bytes())) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::size_overflow,
                    "Delta snapshot value-byte accounting overflow",
                    entity.entity_number(),
                    reference_value);
            }
            total_value_bytes += entity.object().accounted_value_bytes();
            if (total_value_bytes >
                limits_.maximum_snapshot_total_value_bytes) {
                return snapshot_failure(
                    EntitySnapshotErrorCode::total_value_bytes_exceeded,
                    "Delta snapshot value-byte limit exceeded",
                    entity.entity_number(),
                    reference_value);
            }
        }

        std::vector<std::uint32_t> retained_removals{
            removals.begin(), removals.end()};
        const EntitySnapshotStatistics statistics{
            result_entities.size(),
            changed_count,
            added_count,
            retained_removals.size(),
            unchanged_shared_count,
            total_value_bytes,
        };
        return EntitySnapshotBuildResult{
            EntitySnapshotState{
                std::move(reference),
                std::move(server_time),
                EntitySnapshotKind::delta,
                declared_base_reference,
                std::move(result_entities),
                std::move(retained_removals),
                source_geometry,
                statistics,
                profile_},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return snapshot_failure(
            EntitySnapshotErrorCode::unable_to_retain_snapshot,
            "Unable to allocate bounded delta entity snapshot",
            std::nullopt,
            reference_value);
    } catch (...) {
        return snapshot_failure(
            EntitySnapshotErrorCode::unable_to_retain_snapshot,
            "Unable to retain delta entity snapshot",
            std::nullopt,
            reference_value);
    }
}

} // namespace hlclient::goldsrc

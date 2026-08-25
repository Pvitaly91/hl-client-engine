#pragma once

#include <hlclient/goldsrc/delta_value_decoder.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// Project safety limits for the caller-supplied neutral profile. None of
// these constants claims a stock GoldSrc wire maximum.
inline constexpr std::size_t kDefaultMaximumEntityBaselines = 4'096U;
inline constexpr std::size_t kMaximumEntityBaselines = 16'384U;
inline constexpr std::size_t kDefaultMaximumEntitiesPerSnapshot = 4'096U;
inline constexpr std::size_t kMaximumEntitiesPerSnapshot = 16'384U;
inline constexpr std::uint32_t kDefaultMaximumEntityNumber = 65'535U;
inline constexpr std::uint32_t kMaximumEntityNumber = 1'048'575U;
inline constexpr std::size_t kDefaultMaximumEntityFields = 256U;
inline constexpr std::size_t kMaximumEntityFields = 1'024U;
inline constexpr std::size_t kDefaultMaximumChangedFieldsPerEntity = 256U;
inline constexpr std::size_t kMaximumChangedFieldsPerEntity = 1'024U;
inline constexpr std::size_t kDefaultMaximumEntitySnapshotHistory = 64U;
inline constexpr std::size_t kMaximumEntitySnapshotHistory = 256U;
inline constexpr std::size_t kDefaultMaximumEntitySnapshotValueBytes =
    8U * 1'024U * 1'024U;
inline constexpr std::size_t kMaximumEntitySnapshotValueBytes =
    64U * 1'024U * 1'024U;
inline constexpr std::size_t kDefaultMaximumEntitySourcePayloadBytes =
    1U * 1'024U * 1'024U;
inline constexpr std::size_t kMaximumEntitySourcePayloadBytes =
    8U * 1'024U * 1'024U;
inline constexpr std::size_t kEntitySnapshotDiagnosticTextLimit = 256U;

struct EntitySnapshotLimits {
    std::size_t maximum_baselines{kDefaultMaximumEntityBaselines};
    std::size_t maximum_entities_per_snapshot{
        kDefaultMaximumEntitiesPerSnapshot};
    std::uint32_t maximum_entity_number{kDefaultMaximumEntityNumber};
    std::size_t maximum_fields_per_entity{kDefaultMaximumEntityFields};
    std::size_t maximum_changed_fields_per_entity{
        kDefaultMaximumChangedFieldsPerEntity};
    std::size_t maximum_snapshot_history{
        kDefaultMaximumEntitySnapshotHistory};
    std::size_t maximum_snapshot_total_value_bytes{
        kDefaultMaximumEntitySnapshotValueBytes};
    std::size_t maximum_source_payload_bytes{
        kDefaultMaximumEntitySourcePayloadBytes};
};

[[nodiscard]] bool valid_entity_snapshot_limits(
    const EntitySnapshotLimits& limits) noexcept;

enum class EntitySnapshotCompatibilityProfile {
    stock_protocol_48_build_10210_evidence_pending,
    synthetic_neutral_v1,
};

enum class EntitySnapshotEvidenceProfile {
    stock_runtime_grammar_evidence_pending,
    caller_supplied_typed_records,
};

enum class EntitySnapshotReferencePolicy {
    stock_width_and_wrap_policy_evidence_pending,
    synthetic_uint32_non_wrapping,
};

enum class EntityBaselineKeyKind {
    entity_number,
    alternate_slot,
};

enum class EntitySchemaCategory {
    ordinary_entity,
    player_entity,
    custom_entity,
    alternate_explicit_schema,
};

// Geometry metadata only. It never retains the corresponding message bytes.
struct EntitySourceGeometry {
    std::size_t message_ordinal{0U};
    std::size_t payload_byte_count{0U};
    std::size_t start_bit_offset{0U};
    std::size_t bits_consumed{0U};
};

[[nodiscard]] bool valid_entity_source_geometry(
    const EntitySourceGeometry& geometry,
    const EntitySnapshotLimits& limits) noexcept;

class EntityBaselineKey final {
public:
    EntityBaselineKey(const EntityBaselineKey&) = default;
    EntityBaselineKey(EntityBaselineKey&&) noexcept = default;
    EntityBaselineKey& operator=(const EntityBaselineKey&) = delete;
    EntityBaselineKey& operator=(EntityBaselineKey&&) noexcept = delete;
    ~EntityBaselineKey() = default;

    [[nodiscard]] static EntityBaselineKey for_entity(
        std::uint32_t entity_number) noexcept;
    [[nodiscard]] static EntityBaselineKey for_alternate_slot(
        std::uint32_t slot) noexcept;

    [[nodiscard]] EntityBaselineKeyKind kind() const noexcept;
    [[nodiscard]] std::uint32_t value() const noexcept;

    [[nodiscard]] friend bool operator==(
        const EntityBaselineKey& left,
        const EntityBaselineKey& right) noexcept
    {
        return left.kind_ == right.kind_ && left.value_ == right.value_;
    }

private:
    EntityBaselineKey(EntityBaselineKeyKind kind, std::uint32_t value) noexcept;

    EntityBaselineKeyKind kind_{EntityBaselineKeyKind::entity_number};
    std::uint32_t value_{0U};
};

// The synthetic reference domain is deliberately sealed to one non-wrapping
// uint32 counter. No stock width or modular comparison is asserted here.
class EntitySnapshotReference final {
public:
    EntitySnapshotReference(const EntitySnapshotReference&) = default;
    EntitySnapshotReference(EntitySnapshotReference&&) noexcept = default;
    EntitySnapshotReference& operator=(const EntitySnapshotReference&) = delete;
    EntitySnapshotReference& operator=(EntitySnapshotReference&&) noexcept =
        delete;
    ~EntitySnapshotReference() = default;

    [[nodiscard]] static EntitySnapshotReference synthetic(
        std::uint32_t value) noexcept;

    [[nodiscard]] std::uint32_t value() const noexcept;
    [[nodiscard]] EntitySnapshotReferencePolicy policy() const noexcept;

    [[nodiscard]] friend bool operator==(
        const EntitySnapshotReference& left,
        const EntitySnapshotReference& right) noexcept
    {
        return left.policy_ == right.policy_ && left.value_ == right.value_;
    }

private:
    EntitySnapshotReference(
        std::uint32_t value,
        EntitySnapshotReferencePolicy policy) noexcept;

    std::uint32_t value_{0U};
    EntitySnapshotReferencePolicy policy_{
        EntitySnapshotReferencePolicy::synthetic_uint32_non_wrapping};
};

// Neutral typed time metadata. The raw value intentionally has no invented
// stock unit, scale, or wall-clock relationship.
class EntityServerTime final {
public:
    EntityServerTime(const EntityServerTime&) = default;
    EntityServerTime(EntityServerTime&&) noexcept = default;
    EntityServerTime& operator=(const EntityServerTime&) = delete;
    EntityServerTime& operator=(EntityServerTime&&) noexcept = delete;
    ~EntityServerTime() = default;

    [[nodiscard]] static EntityServerTime synthetic_raw(
        std::int64_t value) noexcept;

    [[nodiscard]] std::int64_t raw_value() const noexcept;
    [[nodiscard]] EntitySnapshotEvidenceProfile evidence_profile()
        const noexcept;

private:
    explicit EntityServerTime(std::int64_t value) noexcept;

    std::int64_t raw_value_{0};
};

enum class GoldSrcEntityProjectionStatus {
    stock_semantics_evidence_pending,
};

// Kept as an explicit type so future evidence-gated projection can be added
// without changing the generic DeltaObjectState source of truth. Neutral
// builders never fabricate an instance.
class GoldSrcEntityStateProjection final {
public:
    GoldSrcEntityStateProjection(const GoldSrcEntityStateProjection&) = default;
    GoldSrcEntityStateProjection(GoldSrcEntityStateProjection&&) noexcept =
        default;
    GoldSrcEntityStateProjection& operator=(
        const GoldSrcEntityStateProjection&) = delete;
    GoldSrcEntityStateProjection& operator=(
        GoldSrcEntityStateProjection&&) noexcept = delete;
    ~GoldSrcEntityStateProjection() = default;

    [[nodiscard]] GoldSrcEntityProjectionStatus status() const noexcept;

private:
    friend class EntityBaselineRegistryBuilder;

    GoldSrcEntityStateProjection() noexcept = default;
};

class EntityBaselineState final {
public:
    EntityBaselineState(const EntityBaselineState&) = default;
    EntityBaselineState(EntityBaselineState&&) noexcept = default;
    EntityBaselineState& operator=(const EntityBaselineState&) = delete;
    EntityBaselineState& operator=(EntityBaselineState&&) noexcept = delete;
    ~EntityBaselineState() = default;

    [[nodiscard]] const EntityBaselineKey& key() const noexcept;
    [[nodiscard]] EntitySchemaCategory schema_category() const noexcept;
    [[nodiscard]] std::string_view schema_name() const noexcept;
    [[nodiscard]] const DeltaObjectState& object() const noexcept;
    [[nodiscard]] const std::optional<GoldSrcEntityStateProjection>&
    semantic_projection() const noexcept;
    [[nodiscard]] const EntitySourceGeometry& source_geometry() const noexcept;
    [[nodiscard]] EntitySnapshotCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] EntitySnapshotEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class EntityBaselineRegistryBuilder;
    friend class EntityFullSnapshotBuilder;
    friend class EntityDeltaSnapshotBuilder;

    EntityBaselineState(
        EntityBaselineKey key,
        EntitySchemaCategory schema_category,
        std::shared_ptr<const DeltaObjectState> object,
        EntitySourceGeometry source_geometry,
        EntitySnapshotCompatibilityProfile compatibility_profile) noexcept;

    EntityBaselineKey key_;
    EntitySchemaCategory schema_category_{
        EntitySchemaCategory::ordinary_entity};
    std::shared_ptr<const DeltaObjectState> object_;
    std::optional<GoldSrcEntityStateProjection> semantic_projection_;
    EntitySourceGeometry source_geometry_;
    EntitySnapshotCompatibilityProfile compatibility_profile_{
        EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
};

class EntityBaselineRegistryState final {
public:
    EntityBaselineRegistryState(const EntityBaselineRegistryState&) = default;
    EntityBaselineRegistryState(EntityBaselineRegistryState&&) noexcept =
        default;
    EntityBaselineRegistryState& operator=(
        const EntityBaselineRegistryState&) = delete;
    EntityBaselineRegistryState& operator=(
        EntityBaselineRegistryState&&) noexcept = delete;
    ~EntityBaselineRegistryState() = default;

    [[nodiscard]] std::span<const EntityBaselineState> baselines()
        const noexcept;
    [[nodiscard]] std::size_t baseline_count() const noexcept;
    [[nodiscard]] const EntityBaselineState* find_exact(
        const EntityBaselineKey& key) const noexcept;
    [[nodiscard]] std::size_t accounted_value_bytes() const noexcept;
    [[nodiscard]] EntitySnapshotCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] EntitySnapshotEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class EntityBaselineRegistryBuilder;

    EntityBaselineRegistryState(
        std::vector<EntityBaselineState> baselines,
        std::size_t accounted_value_bytes,
        EntitySnapshotCompatibilityProfile compatibility_profile) noexcept;

    std::vector<EntityBaselineState> baselines_;
    std::size_t accounted_value_bytes_{0U};
    EntitySnapshotCompatibilityProfile compatibility_profile_{
        EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
};

enum class EntityBaselineErrorCode {
    invalid_configuration,
    evidence_pending,
    invalid_source_geometry,
    baseline_limit_exceeded,
    entity_number_limit_exceeded,
    duplicate_baseline_identity,
    unsupported_schema,
    schema_profile_mismatch,
    object_profile_mismatch,
    field_limit_exceeded,
    total_value_bytes_exceeded,
    size_overflow,
    unable_to_retain_baseline,
};

struct EntityBaselineError {
    EntityBaselineErrorCode code{
        EntityBaselineErrorCode::invalid_configuration};
    std::optional<EntityBaselineKeyKind> key_kind;
    std::optional<std::uint32_t> key_value;
    std::string context;
};

struct EntityBaselineInsertResult {
    bool inserted{false};
    std::optional<EntityBaselineError> error;

    [[nodiscard]] explicit operator bool() const noexcept { return inserted; }
};

struct EntityBaselinePublishResult {
    std::optional<EntityBaselineRegistryState> state;
    std::optional<EntityBaselineError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class EntityBaselineRegistryBuilder final {
public:
    explicit EntityBaselineRegistryBuilder(
        const DeltaSchemaRegistryState& schemas,
        EntitySnapshotLimits limits = {},
        EntitySnapshotCompatibilityProfile profile =
            EntitySnapshotCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending);

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const EntitySnapshotLimits& limits() const noexcept;
    [[nodiscard]] EntitySnapshotCompatibilityProfile profile() const noexcept;
    [[nodiscard]] EntityBaselineInsertResult insert(
        EntityBaselineKey key,
        EntitySchemaCategory schema_category,
        const DeltaObjectState& object,
        EntitySourceGeometry source_geometry = {});
    [[nodiscard]] EntityBaselinePublishResult publish() &&;
    [[nodiscard]] std::span<const EntityBaselineState> candidate_baselines()
        const noexcept;

private:
    DeltaSchemaRegistryState schemas_;
    EntitySnapshotLimits limits_;
    EntitySnapshotCompatibilityProfile profile_{
        EntitySnapshotCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    std::vector<EntityBaselineState> baselines_;
    std::size_t accounted_value_bytes_{0U};
};

class EntitySnapshotEntityInput final {
public:
    EntitySnapshotEntityInput(const EntitySnapshotEntityInput&) = default;
    EntitySnapshotEntityInput(EntitySnapshotEntityInput&&) noexcept = default;
    EntitySnapshotEntityInput& operator=(
        const EntitySnapshotEntityInput&) = delete;
    EntitySnapshotEntityInput& operator=(
        EntitySnapshotEntityInput&&) noexcept = delete;
    ~EntitySnapshotEntityInput() = default;

    [[nodiscard]] static EntitySnapshotEntityInput from_baseline(
        std::uint32_t entity_number,
        EntityBaselineKey baseline_key);
    [[nodiscard]] static EntitySnapshotEntityInput with_decoded_state(
        std::uint32_t entity_number,
        EntityBaselineKey baseline_key,
        const DeltaObjectState& object);

    [[nodiscard]] std::uint32_t entity_number() const noexcept;
    [[nodiscard]] const EntityBaselineKey& baseline_key() const noexcept;
    [[nodiscard]] const std::optional<DeltaObjectState>& decoded_state()
        const noexcept;

private:
    EntitySnapshotEntityInput(
        std::uint32_t entity_number,
        EntityBaselineKey baseline_key,
        std::optional<DeltaObjectState> object) noexcept;

    std::uint32_t entity_number_{0U};
    EntityBaselineKey baseline_key_;
    std::optional<DeltaObjectState> object_;
};

class EntitySnapshotEntityState final {
public:
    EntitySnapshotEntityState(const EntitySnapshotEntityState&) = default;
    EntitySnapshotEntityState(EntitySnapshotEntityState&&) noexcept = default;
    EntitySnapshotEntityState& operator=(
        const EntitySnapshotEntityState&) = delete;
    EntitySnapshotEntityState& operator=(
        EntitySnapshotEntityState&&) noexcept = delete;
    ~EntitySnapshotEntityState() = default;

    [[nodiscard]] std::uint32_t entity_number() const noexcept;
    [[nodiscard]] const EntityBaselineKey& baseline_key() const noexcept;
    [[nodiscard]] EntitySchemaCategory schema_category() const noexcept;
    [[nodiscard]] const DeltaObjectState& object() const noexcept;
    [[nodiscard]] const std::optional<GoldSrcEntityStateProjection>&
    semantic_projection() const noexcept;
    [[nodiscard]] bool shares_object_with(
        const EntitySnapshotEntityState& other) const noexcept;

private:
    friend class EntityFullSnapshotBuilder;
    friend class EntityDeltaSnapshotBuilder;

    EntitySnapshotEntityState(
        std::uint32_t entity_number,
        EntityBaselineKey baseline_key,
        EntitySchemaCategory schema_category,
        std::shared_ptr<const DeltaObjectState> object,
        std::optional<GoldSrcEntityStateProjection> semantic_projection)
        noexcept;

    std::uint32_t entity_number_{0U};
    EntityBaselineKey baseline_key_;
    EntitySchemaCategory schema_category_{
        EntitySchemaCategory::ordinary_entity};
    std::shared_ptr<const DeltaObjectState> object_;
    std::optional<GoldSrcEntityStateProjection> semantic_projection_;
};

enum class EntitySnapshotKind {
    full,
    delta,
};

struct EntitySnapshotStatistics {
    std::size_t entity_count{0U};
    std::size_t changed_count{0U};
    std::size_t added_count{0U};
    std::size_t removed_count{0U};
    std::size_t unchanged_shared_count{0U};
    std::size_t accounted_value_bytes{0U};
};

class EntitySnapshotState final {
public:
    EntitySnapshotState(const EntitySnapshotState&) = default;
    EntitySnapshotState(EntitySnapshotState&&) noexcept = default;
    EntitySnapshotState& operator=(const EntitySnapshotState&) = delete;
    EntitySnapshotState& operator=(EntitySnapshotState&&) noexcept = delete;
    ~EntitySnapshotState() = default;

    [[nodiscard]] const EntitySnapshotReference& reference() const noexcept;
    [[nodiscard]] const EntityServerTime& server_time() const noexcept;
    [[nodiscard]] EntitySnapshotKind kind() const noexcept;
    [[nodiscard]] const std::optional<EntitySnapshotReference>& base_reference()
        const noexcept;
    [[nodiscard]] std::span<const EntitySnapshotEntityState> entities()
        const noexcept;
    [[nodiscard]] std::size_t entity_count() const noexcept;
    [[nodiscard]] const EntitySnapshotEntityState* find_exact(
        std::uint32_t entity_number) const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> removed_entity_numbers()
        const noexcept;
    [[nodiscard]] const EntitySourceGeometry& source_geometry() const noexcept;
    [[nodiscard]] const EntitySnapshotStatistics& statistics() const noexcept;
    [[nodiscard]] EntitySnapshotCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] EntitySnapshotEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class EntityFullSnapshotBuilder;
    friend class EntityDeltaSnapshotBuilder;

    EntitySnapshotState(
        EntitySnapshotReference reference,
        EntityServerTime server_time,
        EntitySnapshotKind kind,
        std::optional<EntitySnapshotReference> base_reference,
        std::vector<EntitySnapshotEntityState> entities,
        std::vector<std::uint32_t> removed_entity_numbers,
        EntitySourceGeometry source_geometry,
        EntitySnapshotStatistics statistics,
        EntitySnapshotCompatibilityProfile compatibility_profile) noexcept;

    EntitySnapshotReference reference_;
    EntityServerTime server_time_;
    EntitySnapshotKind kind_{EntitySnapshotKind::full};
    std::optional<EntitySnapshotReference> base_reference_;
    std::vector<EntitySnapshotEntityState> entities_;
    std::vector<std::uint32_t> removed_entity_numbers_;
    EntitySourceGeometry source_geometry_;
    EntitySnapshotStatistics statistics_;
    EntitySnapshotCompatibilityProfile compatibility_profile_{
        EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
};

enum class EntitySnapshotErrorCode {
    invalid_configuration,
    evidence_pending,
    invalid_source_geometry,
    incompatible_reference_profile,
    incompatible_baseline_registry,
    entity_limit_exceeded,
    entity_number_limit_exceeded,
    field_limit_exceeded,
    total_value_bytes_exceeded,
    duplicate_entity,
    out_of_order_entity,
    missing_baseline,
    baseline_identity_mismatch,
    baseline_schema_mismatch,
    object_profile_mismatch,
    schema_mismatch,
    missing_delta_snapshot_base,
    evicted_delta_snapshot_base,
    future_delta_snapshot_base,
    wrong_delta_snapshot_base,
    duplicate_removal,
    out_of_order_removal,
    remove_nonexistent_entity,
    update_remove_conflict,
    size_overflow,
    unable_to_retain_snapshot,
};

struct EntitySnapshotError {
    EntitySnapshotErrorCode code{
        EntitySnapshotErrorCode::invalid_configuration};
    std::optional<std::uint32_t> entity_number;
    std::optional<std::uint32_t> snapshot_reference;
    std::string context;
};

struct EntitySnapshotBuildResult {
    std::optional<EntitySnapshotState> state;
    std::optional<EntitySnapshotError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

enum class EntitySnapshotHistoryReferenceStatus {
    retained,
    evicted,
    missing,
    future,
};

class EntitySnapshotHistoryState final {
public:
    EntitySnapshotHistoryState(const EntitySnapshotHistoryState&) = default;
    EntitySnapshotHistoryState(EntitySnapshotHistoryState&&) noexcept = default;
    EntitySnapshotHistoryState& operator=(
        const EntitySnapshotHistoryState&) = delete;
    EntitySnapshotHistoryState& operator=(
        EntitySnapshotHistoryState&&) noexcept = delete;
    ~EntitySnapshotHistoryState() = default;

    [[nodiscard]] std::span<const EntitySnapshotState> snapshots()
        const noexcept;
    [[nodiscard]] std::size_t snapshot_count() const noexcept;
    [[nodiscard]] const EntitySnapshotState* find_exact(
        const EntitySnapshotReference& reference) const noexcept;
    [[nodiscard]] EntitySnapshotHistoryReferenceStatus classify(
        const EntitySnapshotReference& reference) const noexcept;
    [[nodiscard]] std::optional<EntitySnapshotReference> oldest_reference()
        const noexcept;
    [[nodiscard]] std::optional<EntitySnapshotReference> newest_reference()
        const noexcept;
    [[nodiscard]] std::optional<EntitySnapshotReference> evicted_through()
        const noexcept;
    [[nodiscard]] std::span<const EntitySnapshotReference>
    required_base_references() const noexcept;
    [[nodiscard]] std::size_t accounted_value_bytes() const noexcept;
    [[nodiscard]] EntitySnapshotCompatibilityProfile compatibility_profile()
        const noexcept;

private:
    friend class EntitySnapshotHistoryBuilder;

    EntitySnapshotHistoryState(
        std::vector<EntitySnapshotState> snapshots,
        std::vector<EntitySnapshotReference> required_base_references,
        std::optional<EntitySnapshotReference> evicted_through,
        std::size_t accounted_value_bytes,
        EntitySnapshotCompatibilityProfile compatibility_profile) noexcept;

    std::vector<EntitySnapshotState> snapshots_;
    std::vector<EntitySnapshotReference> required_base_references_;
    std::optional<EntitySnapshotReference> evicted_through_;
    std::size_t accounted_value_bytes_{0U};
    EntitySnapshotCompatibilityProfile compatibility_profile_{
        EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
};

enum class EntitySnapshotHistoryErrorCode {
    invalid_configuration,
    evidence_pending,
    wrap_policy_evidence_pending,
    incompatible_snapshot_profile,
    duplicate_snapshot,
    old_snapshot,
    future_base_reference,
    missing_snapshot_base,
    evicted_snapshot_base,
    required_base_not_retained,
    required_base_retention_limit,
    history_limit_exceeded,
    size_overflow,
    unable_to_retain_history,
};

struct EntitySnapshotHistoryError {
    EntitySnapshotHistoryErrorCode code{
        EntitySnapshotHistoryErrorCode::invalid_configuration};
    std::optional<std::uint32_t> snapshot_reference;
    std::string context;
};

struct EntitySnapshotHistoryMutationResult {
    bool changed{false};
    std::optional<EntitySnapshotHistoryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

struct EntitySnapshotHistoryPublishResult {
    std::optional<EntitySnapshotHistoryState> state;
    std::optional<EntitySnapshotHistoryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class EntitySnapshotHistoryBuilder final {
public:
    explicit EntitySnapshotHistoryBuilder(
        EntitySnapshotLimits limits = {},
        EntitySnapshotCompatibilityProfile profile =
            EntitySnapshotCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const EntitySnapshotLimits& limits() const noexcept;
    [[nodiscard]] EntitySnapshotHistoryMutationResult insert(
        const EntitySnapshotState& snapshot);
    [[nodiscard]] EntitySnapshotHistoryMutationResult retain_required_base(
        const EntitySnapshotReference& reference);
    [[nodiscard]] EntitySnapshotHistoryMutationResult release_required_base(
        const EntitySnapshotReference& reference);
    [[nodiscard]] EntitySnapshotHistoryPublishResult publish() const;
    [[nodiscard]] std::size_t candidate_snapshot_count() const noexcept;

private:
    [[nodiscard]] const EntitySnapshotState* find_candidate_exact(
        const EntitySnapshotReference& reference) const noexcept;
    [[nodiscard]] bool is_required(
        const EntitySnapshotReference& reference) const noexcept;

    EntitySnapshotLimits limits_;
    EntitySnapshotCompatibilityProfile profile_{
        EntitySnapshotCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    std::vector<EntitySnapshotState> snapshots_;
    std::vector<EntitySnapshotReference> required_base_references_;
    std::optional<EntitySnapshotReference> evicted_through_;
    std::size_t accounted_value_bytes_{0U};
};

class EntityFullSnapshotBuilder final {
public:
    explicit EntityFullSnapshotBuilder(
        EntitySnapshotLimits limits = {},
        EntitySnapshotCompatibilityProfile profile =
            EntitySnapshotCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] EntitySnapshotBuildResult build(
        EntitySnapshotReference reference,
        EntityServerTime server_time,
        const EntityBaselineRegistryState& baselines,
        std::span<const EntitySnapshotEntityInput> entities,
        EntitySourceGeometry source_geometry = {}) const;

private:
    EntitySnapshotLimits limits_;
    EntitySnapshotCompatibilityProfile profile_{
        EntitySnapshotCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
};

class EntityDeltaSnapshotBuilder final {
public:
    explicit EntityDeltaSnapshotBuilder(
        EntitySnapshotLimits limits = {},
        EntitySnapshotCompatibilityProfile profile =
            EntitySnapshotCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] EntitySnapshotBuildResult build(
        EntitySnapshotReference reference,
        EntityServerTime server_time,
        EntitySnapshotReference base_reference,
        const EntitySnapshotHistoryState& history,
        const EntityBaselineRegistryState& baselines,
        std::span<const EntitySnapshotEntityInput> updates,
        std::span<const std::uint32_t> removals,
        EntitySourceGeometry source_geometry = {}) const;
    [[nodiscard]] EntitySnapshotBuildResult build_with_resolved_base(
        EntitySnapshotReference reference,
        EntityServerTime server_time,
        EntitySnapshotReference declared_base_reference,
        const EntitySnapshotState& resolved_base,
        const EntityBaselineRegistryState& baselines,
        std::span<const EntitySnapshotEntityInput> updates,
        std::span<const std::uint32_t> removals,
        EntitySourceGeometry source_geometry = {}) const;

private:
    EntitySnapshotLimits limits_;
    EntitySnapshotCompatibilityProfile profile_{
        EntitySnapshotCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
};

[[nodiscard]] constexpr std::string_view to_string(
    EntitySnapshotCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case EntitySnapshotCompatibilityProfile::
        stock_protocol_48_build_10210_evidence_pending:
        return "stock_protocol_48_build_10210_evidence_pending";
    case EntitySnapshotCompatibilityProfile::synthetic_neutral_v1:
        return "synthetic_neutral_v1";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    EntityBaselineErrorCode code) noexcept
{
    switch (code) {
    case EntityBaselineErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntityBaselineErrorCode::evidence_pending: return "evidence_pending";
    case EntityBaselineErrorCode::invalid_source_geometry:
        return "invalid_source_geometry";
    case EntityBaselineErrorCode::baseline_limit_exceeded:
        return "baseline_limit_exceeded";
    case EntityBaselineErrorCode::entity_number_limit_exceeded:
        return "entity_number_limit_exceeded";
    case EntityBaselineErrorCode::duplicate_baseline_identity:
        return "duplicate_baseline_identity";
    case EntityBaselineErrorCode::unsupported_schema:
        return "unsupported_schema";
    case EntityBaselineErrorCode::schema_profile_mismatch:
        return "schema_profile_mismatch";
    case EntityBaselineErrorCode::object_profile_mismatch:
        return "object_profile_mismatch";
    case EntityBaselineErrorCode::field_limit_exceeded:
        return "field_limit_exceeded";
    case EntityBaselineErrorCode::total_value_bytes_exceeded:
        return "total_value_bytes_exceeded";
    case EntityBaselineErrorCode::size_overflow: return "size_overflow";
    case EntityBaselineErrorCode::unable_to_retain_baseline:
        return "unable_to_retain_baseline";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    EntitySnapshotErrorCode code) noexcept
{
    switch (code) {
    case EntitySnapshotErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntitySnapshotErrorCode::evidence_pending: return "evidence_pending";
    case EntitySnapshotErrorCode::invalid_source_geometry:
        return "invalid_source_geometry";
    case EntitySnapshotErrorCode::incompatible_reference_profile:
        return "incompatible_reference_profile";
    case EntitySnapshotErrorCode::incompatible_baseline_registry:
        return "incompatible_baseline_registry";
    case EntitySnapshotErrorCode::entity_limit_exceeded:
        return "entity_limit_exceeded";
    case EntitySnapshotErrorCode::entity_number_limit_exceeded:
        return "entity_number_limit_exceeded";
    case EntitySnapshotErrorCode::field_limit_exceeded:
        return "field_limit_exceeded";
    case EntitySnapshotErrorCode::total_value_bytes_exceeded:
        return "total_value_bytes_exceeded";
    case EntitySnapshotErrorCode::duplicate_entity: return "duplicate_entity";
    case EntitySnapshotErrorCode::out_of_order_entity:
        return "out_of_order_entity";
    case EntitySnapshotErrorCode::missing_baseline: return "missing_baseline";
    case EntitySnapshotErrorCode::baseline_identity_mismatch:
        return "baseline_identity_mismatch";
    case EntitySnapshotErrorCode::baseline_schema_mismatch:
        return "baseline_schema_mismatch";
    case EntitySnapshotErrorCode::object_profile_mismatch:
        return "object_profile_mismatch";
    case EntitySnapshotErrorCode::schema_mismatch: return "schema_mismatch";
    case EntitySnapshotErrorCode::missing_delta_snapshot_base:
        return "missing_delta_snapshot_base";
    case EntitySnapshotErrorCode::evicted_delta_snapshot_base:
        return "evicted_delta_snapshot_base";
    case EntitySnapshotErrorCode::future_delta_snapshot_base:
        return "future_delta_snapshot_base";
    case EntitySnapshotErrorCode::wrong_delta_snapshot_base:
        return "wrong_delta_snapshot_base";
    case EntitySnapshotErrorCode::duplicate_removal:
        return "duplicate_removal";
    case EntitySnapshotErrorCode::out_of_order_removal:
        return "out_of_order_removal";
    case EntitySnapshotErrorCode::remove_nonexistent_entity:
        return "remove_nonexistent_entity";
    case EntitySnapshotErrorCode::update_remove_conflict:
        return "update_remove_conflict";
    case EntitySnapshotErrorCode::size_overflow: return "size_overflow";
    case EntitySnapshotErrorCode::unable_to_retain_snapshot:
        return "unable_to_retain_snapshot";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    EntitySnapshotHistoryErrorCode code) noexcept
{
    switch (code) {
    case EntitySnapshotHistoryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntitySnapshotHistoryErrorCode::evidence_pending:
        return "evidence_pending";
    case EntitySnapshotHistoryErrorCode::wrap_policy_evidence_pending:
        return "wrap_policy_evidence_pending";
    case EntitySnapshotHistoryErrorCode::incompatible_snapshot_profile:
        return "incompatible_snapshot_profile";
    case EntitySnapshotHistoryErrorCode::duplicate_snapshot:
        return "duplicate_snapshot";
    case EntitySnapshotHistoryErrorCode::old_snapshot: return "old_snapshot";
    case EntitySnapshotHistoryErrorCode::future_base_reference:
        return "future_base_reference";
    case EntitySnapshotHistoryErrorCode::missing_snapshot_base:
        return "missing_snapshot_base";
    case EntitySnapshotHistoryErrorCode::evicted_snapshot_base:
        return "evicted_snapshot_base";
    case EntitySnapshotHistoryErrorCode::required_base_not_retained:
        return "required_base_not_retained";
    case EntitySnapshotHistoryErrorCode::required_base_retention_limit:
        return "required_base_retention_limit";
    case EntitySnapshotHistoryErrorCode::history_limit_exceeded:
        return "history_limit_exceeded";
    case EntitySnapshotHistoryErrorCode::size_overflow:
        return "size_overflow";
    case EntitySnapshotHistoryErrorCode::unable_to_retain_history:
        return "unable_to_retain_history";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/stock_command_ack_evidence.hpp>
#include <hlclient/goldsrc/stock_server_time.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t
    kDefaultMaximumStockAuthoritativeFieldProvenance = 64U;
inline constexpr std::size_t
    kHardMaximumStockAuthoritativeFieldProvenance = 256U;

struct StockAuthoritativeMovementLimits {
    std::size_t maximum_provenance_records{
        kDefaultMaximumStockAuthoritativeFieldProvenance};
    std::size_t maximum_schema_name_bytes{96U};
    std::size_t maximum_field_name_bytes{96U};
    std::size_t maximum_total_metadata_bytes{16U * 1024U};
    float maximum_coordinate_magnitude{1'000'000.0F};
    float maximum_velocity_magnitude{1'000'000.0F};
    float maximum_scalar_magnitude{1'000'000.0F};
};

[[nodiscard]] bool valid_stock_authoritative_movement_limits(
    const StockAuthoritativeMovementLimits& limits) noexcept;

enum class StockAuthoritativeSemanticTarget : std::uint8_t {
    origin,
    velocity,
    view_offset,
    hull,
    flags,
    water_level,
    water_contents,
    maximum_speed,
    gravity_multiplier,
    friction_multiplier,
    base_velocity,
    ground_indicator,
    old_buttons,
    server_time,
    authoritative_update_identity,
    command_acknowledgement,
};

enum class StockAuthoritativeSourceKind : std::uint8_t {
    entity_state,
    client_local_data,
    acknowledged_usercmd,
    runtime_time,
    collision_query,
};

enum class StockAuthoritativeControlledScenario : std::uint8_t {
    unclassified,
    idle_runtime,
    controlled_translation,
    controlled_duck_stand,
    packet_loss_or_batching,
    two_client_differential,
};

enum class StockAuthoritativeEvidenceConfidence : std::uint8_t {
    evidence_pending,
    controlled_correlation,
    confirmed_for_profile,
};

enum class StockAuthoritativeValueOrigin : std::uint8_t {
    stock_field_direct,
    project_derived,
};

enum class StockAuthoritativeFieldSupport : std::uint8_t {
    evidence_pending,
    supported_for_profile,
    conflicting,
};

struct StockAuthoritativeFieldProvenance {
    StockAuthoritativeSemanticTarget semantic_target{
        StockAuthoritativeSemanticTarget::origin};
    std::string source_schema;
    std::string source_field_name;
    StockAuthoritativeSourceKind source_kind{
        StockAuthoritativeSourceKind::entity_state};
    StockRuntimeMessageCategory source_message_category{
        StockRuntimeMessageCategory::unsupported_runtime_message};
    std::size_t source_message_ordinal{0U};
    std::optional<std::size_t> source_field_ordinal;
    StockRuntimeSourceCursor source_start_cursor{};
    StockRuntimeSourceCursor source_end_cursor{};
    StockAuthoritativeControlledScenario controlled_scenario{
        StockAuthoritativeControlledScenario::unclassified};
    StockAuthoritativeEvidenceConfidence confidence{
        StockAuthoritativeEvidenceConfidence::evidence_pending};
    StockAuthoritativeValueOrigin value_origin{
        StockAuthoritativeValueOrigin::stock_field_direct};
    StockAuthoritativeFieldSupport support{
        StockAuthoritativeFieldSupport::evidence_pending};

    [[nodiscard]] friend bool operator==(
        const StockAuthoritativeFieldProvenance&,
        const StockAuthoritativeFieldProvenance&) = default;
};

struct StockAuthoritativeMovementValues {
    std::optional<assets::AssetVector3> origin;
    std::optional<assets::AssetVector3> velocity;
    std::optional<assets::AssetVector3> view_offset;
    std::optional<hlclient::movement::PlayerMovementHull> hull;
    std::optional<std::uint32_t> flags;
    std::optional<std::uint8_t> water_level;
    std::optional<hlclient::movement::PlayerMovementContents> water_contents;
    std::optional<float> maximum_speed;
    std::optional<float> gravity_multiplier;
    std::optional<float> friction_multiplier;
    std::optional<assets::AssetVector3> base_velocity;
    std::optional<bool> ground_indicator;
    std::optional<std::uint16_t> old_buttons;
    std::optional<StockServerTimeObservation> server_time;
    std::optional<std::uint64_t> authoritative_update_identity;
};

enum class StockAuthoritativeMovementObservationStatus : std::uint8_t {
    unobserved,
    partial_evidence_pending,
    complete_candidate_evidence_pending,
    field_conflict,
};

enum class StockAuthoritativeMovementErrorCode : std::uint8_t {
    invalid_configuration,
    invalid_runtime_generation,
    invalid_update_ordinal,
    invalid_local_player_candidate,
    invalid_numeric_value,
    value_limit_exceeded,
    invalid_hull,
    invalid_water_contents,
    provenance_limit_exceeded,
    metadata_limit_exceeded,
    invalid_provenance,
    missing_field_provenance,
    provenance_without_value,
    duplicate_provenance,
    authoritative_hull_conflict,
    command_acknowledgement_mismatch,
    profile_mismatch,
    stock_evidence_pending,
    allocation_failed,
};

struct StockAuthoritativeMovementError {
    StockAuthoritativeMovementErrorCode code{
        StockAuthoritativeMovementErrorCode::invalid_configuration};
    std::optional<StockAuthoritativeSemanticTarget> semantic_target;
    std::string_view context;
};

struct StockAuthoritativeMovementCreateInfo {
    std::uint64_t runtime_generation{0U};
    std::uint64_t update_ordinal{0U};
    std::uint32_t local_player_candidate_entity_number{0U};
    StockAuthoritativeMovementValues values;
    std::vector<StockAuthoritativeFieldProvenance> provenance;
    std::shared_ptr<const StockCommandAcknowledgementEvidenceState>
        command_acknowledgement_evidence;
    StockRuntimeCompatibilityProfile compatibility_profile{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
};

class StockAuthoritativeMovementObservation final {
public:
    struct CreationResult;

    StockAuthoritativeMovementObservation(
        const StockAuthoritativeMovementObservation&) = default;
    StockAuthoritativeMovementObservation(
        StockAuthoritativeMovementObservation&&) noexcept = default;
    StockAuthoritativeMovementObservation& operator=(
        const StockAuthoritativeMovementObservation&) = delete;
    StockAuthoritativeMovementObservation& operator=(
        StockAuthoritativeMovementObservation&&) = delete;
    ~StockAuthoritativeMovementObservation() = default;

    [[nodiscard]] static CreationResult create(
        const StockAuthoritativeMovementCreateInfo& create_info,
        const StockAuthoritativeMovementLimits& limits = {}) noexcept;

    [[nodiscard]] std::uint64_t runtime_generation() const noexcept;
    [[nodiscard]] std::uint64_t update_ordinal() const noexcept;
    [[nodiscard]] std::uint32_t local_player_candidate_entity_number()
        const noexcept;
    [[nodiscard]] const StockAuthoritativeMovementValues& values()
        const noexcept;
    [[nodiscard]] std::span<const StockAuthoritativeFieldProvenance>
    provenance() const noexcept;
    [[nodiscard]] std::size_t provenance_count_for(
        StockAuthoritativeSemanticTarget target) const noexcept;
    [[nodiscard]] bool has_value(
        StockAuthoritativeSemanticTarget target) const noexcept;
    [[nodiscard]] bool complete_candidate_fields() const noexcept;
    [[nodiscard]] StockAuthoritativeMovementObservationStatus status()
        const noexcept;
    [[nodiscard]] const std::shared_ptr<
        const StockCommandAcknowledgementEvidenceState>&
    command_acknowledgement_evidence() const noexcept;
    [[nodiscard]] StockRuntimeCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] StockRuntimeEvidenceProfile evidence_profile() const noexcept;

private:
    StockAuthoritativeMovementObservation(
        std::uint64_t runtime_generation,
        std::uint64_t update_ordinal,
        std::uint32_t local_player_candidate_entity_number,
        StockAuthoritativeMovementValues values,
        std::vector<StockAuthoritativeFieldProvenance> provenance,
        std::shared_ptr<const StockCommandAcknowledgementEvidenceState>
            command_acknowledgement_evidence,
        StockAuthoritativeMovementObservationStatus status,
        StockRuntimeCompatibilityProfile compatibility_profile,
        StockRuntimeEvidenceProfile evidence_profile) noexcept;

    std::uint64_t runtime_generation_{0U};
    std::uint64_t update_ordinal_{0U};
    std::uint32_t local_player_candidate_entity_number_{0U};
    StockAuthoritativeMovementValues values_;
    std::vector<StockAuthoritativeFieldProvenance> provenance_;
    std::shared_ptr<const StockCommandAcknowledgementEvidenceState>
        command_acknowledgement_evidence_;
    StockAuthoritativeMovementObservationStatus status_{
        StockAuthoritativeMovementObservationStatus::unobserved};
    StockRuntimeCompatibilityProfile compatibility_profile_{
        StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
    StockRuntimeEvidenceProfile evidence_profile_{
        StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending};
};

struct StockAuthoritativeMovementObservation::CreationResult {
    std::optional<StockAuthoritativeMovementObservation> observation;
    std::optional<StockAuthoritativeMovementError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return observation.has_value() && !error.has_value();
    }
};

[[nodiscard]] std::string_view to_string(
    StockAuthoritativeSemanticTarget target) noexcept;
[[nodiscard]] std::string_view to_string(
    StockAuthoritativeMovementObservationStatus status) noexcept;
[[nodiscard]] std::string_view to_string(
    StockAuthoritativeMovementErrorCode code) noexcept;

} // namespace hlclient::goldsrc

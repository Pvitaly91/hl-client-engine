#include <hlclient/goldsrc/stock_authoritative_movement.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] StockAuthoritativeMovementObservation::CreationResult failure(
    const StockAuthoritativeMovementErrorCode code,
    const std::string_view context,
    const std::optional<StockAuthoritativeSemanticTarget> target =
        std::nullopt) noexcept
{
    return {std::nullopt,
        StockAuthoritativeMovementError{code, target, context}};
}

[[nodiscard]] bool pending_profile_tuple(
    const StockRuntimeCompatibilityProfile compatibility,
    const StockRuntimeEvidenceProfile evidence) noexcept
{
    return compatibility == StockRuntimeCompatibilityProfile::
               stock_protocol_48_build_10210_evidence_pending &&
        evidence == StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending;
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool bounded_vector(
    const assets::AssetVector3& value,
    const float limit) noexcept
{
    return std::fabs(value.x) <= limit && std::fabs(value.y) <= limit &&
        std::fabs(value.z) <= limit;
}

[[nodiscard]] bool valid_hull(
    const hlclient::movement::PlayerMovementHull hull) noexcept
{
    return hull == hlclient::movement::PlayerMovementHull::standing ||
        hull == hlclient::movement::PlayerMovementHull::ducked;
}

[[nodiscard]] bool valid_contents(
    const hlclient::movement::PlayerMovementContents contents) noexcept
{
    switch (contents) {
    case hlclient::movement::PlayerMovementContents::empty:
    case hlclient::movement::PlayerMovementContents::solid:
    case hlclient::movement::PlayerMovementContents::water:
    case hlclient::movement::PlayerMovementContents::slime:
    case hlclient::movement::PlayerMovementContents::lava:
    case hlclient::movement::PlayerMovementContents::current:
    case hlclient::movement::PlayerMovementContents::sky:
    case hlclient::movement::PlayerMovementContents::special:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_semantic_target(
    const StockAuthoritativeSemanticTarget target) noexcept
{
    switch (target) {
    case StockAuthoritativeSemanticTarget::origin:
    case StockAuthoritativeSemanticTarget::velocity:
    case StockAuthoritativeSemanticTarget::view_offset:
    case StockAuthoritativeSemanticTarget::hull:
    case StockAuthoritativeSemanticTarget::flags:
    case StockAuthoritativeSemanticTarget::water_level:
    case StockAuthoritativeSemanticTarget::water_contents:
    case StockAuthoritativeSemanticTarget::maximum_speed:
    case StockAuthoritativeSemanticTarget::gravity_multiplier:
    case StockAuthoritativeSemanticTarget::friction_multiplier:
    case StockAuthoritativeSemanticTarget::base_velocity:
    case StockAuthoritativeSemanticTarget::ground_indicator:
    case StockAuthoritativeSemanticTarget::old_buttons:
    case StockAuthoritativeSemanticTarget::server_time:
    case StockAuthoritativeSemanticTarget::authoritative_update_identity:
    case StockAuthoritativeSemanticTarget::command_acknowledgement:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_source_kind(
    const StockAuthoritativeSourceKind kind) noexcept
{
    switch (kind) {
    case StockAuthoritativeSourceKind::entity_state:
    case StockAuthoritativeSourceKind::client_local_data:
    case StockAuthoritativeSourceKind::acknowledged_usercmd:
    case StockAuthoritativeSourceKind::runtime_time:
    case StockAuthoritativeSourceKind::collision_query:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_scenario(
    const StockAuthoritativeControlledScenario scenario) noexcept
{
    switch (scenario) {
    case StockAuthoritativeControlledScenario::unclassified:
    case StockAuthoritativeControlledScenario::idle_runtime:
    case StockAuthoritativeControlledScenario::controlled_translation:
    case StockAuthoritativeControlledScenario::controlled_duck_stand:
    case StockAuthoritativeControlledScenario::packet_loss_or_batching:
    case StockAuthoritativeControlledScenario::two_client_differential:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_confidence(
    const StockAuthoritativeEvidenceConfidence confidence) noexcept
{
    switch (confidence) {
    case StockAuthoritativeEvidenceConfidence::evidence_pending:
    case StockAuthoritativeEvidenceConfidence::controlled_correlation:
    case StockAuthoritativeEvidenceConfidence::confirmed_for_profile:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_value_origin(
    const StockAuthoritativeValueOrigin origin) noexcept
{
    return origin == StockAuthoritativeValueOrigin::stock_field_direct ||
        origin == StockAuthoritativeValueOrigin::project_derived;
}

[[nodiscard]] bool valid_field_support(
    const StockAuthoritativeFieldSupport support) noexcept
{
    switch (support) {
    case StockAuthoritativeFieldSupport::evidence_pending:
    case StockAuthoritativeFieldSupport::supported_for_profile:
    case StockAuthoritativeFieldSupport::conflicting:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_message_category(
    const StockRuntimeMessageCategory category) noexcept
{
    switch (category) {
    case StockRuntimeMessageCategory::runtime_control_candidate:
    case StockRuntimeMessageCategory::runtime_time_candidate:
    case StockRuntimeMessageCategory::baseline_candidate:
    case StockRuntimeMessageCategory::entity_full_candidate:
    case StockRuntimeMessageCategory::entity_delta_candidate:
    case StockRuntimeMessageCategory::client_local_data_candidate:
    case StockRuntimeMessageCategory::command_ack_candidate:
    case StockRuntimeMessageCategory::unsupported_runtime_message:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_provenance_shape(
    const StockAuthoritativeFieldProvenance& provenance) noexcept
{
    if (!valid_semantic_target(provenance.semantic_target) ||
        !valid_source_kind(provenance.source_kind) ||
        !valid_message_category(provenance.source_message_category) ||
        !valid_scenario(provenance.controlled_scenario) ||
        !valid_confidence(provenance.confidence) ||
        !valid_value_origin(provenance.value_origin) ||
        !valid_field_support(provenance.support) ||
        provenance.source_schema.empty() ||
        provenance.source_field_name.empty() ||
        !valid_stock_runtime_source_cursor(provenance.source_start_cursor) ||
        !valid_stock_runtime_source_cursor(provenance.source_end_cursor) ||
        provenance.source_end_cursor.absolute_bit_offset() <
            provenance.source_start_cursor.absolute_bit_offset()) {
        return false;
    }

    if (provenance.value_origin ==
            StockAuthoritativeValueOrigin::project_derived) {
        return provenance.source_kind ==
            StockAuthoritativeSourceKind::collision_query;
    }
    return provenance.source_kind !=
        StockAuthoritativeSourceKind::collision_query;
}

constexpr std::array<StockAuthoritativeSemanticTarget, 16U> kAllTargets{
    StockAuthoritativeSemanticTarget::origin,
    StockAuthoritativeSemanticTarget::velocity,
    StockAuthoritativeSemanticTarget::view_offset,
    StockAuthoritativeSemanticTarget::hull,
    StockAuthoritativeSemanticTarget::flags,
    StockAuthoritativeSemanticTarget::water_level,
    StockAuthoritativeSemanticTarget::water_contents,
    StockAuthoritativeSemanticTarget::maximum_speed,
    StockAuthoritativeSemanticTarget::gravity_multiplier,
    StockAuthoritativeSemanticTarget::friction_multiplier,
    StockAuthoritativeSemanticTarget::base_velocity,
    StockAuthoritativeSemanticTarget::ground_indicator,
    StockAuthoritativeSemanticTarget::old_buttons,
    StockAuthoritativeSemanticTarget::server_time,
    StockAuthoritativeSemanticTarget::authoritative_update_identity,
    StockAuthoritativeSemanticTarget::command_acknowledgement,
};

} // namespace

bool valid_stock_authoritative_movement_limits(
    const StockAuthoritativeMovementLimits& limits) noexcept
{
    return limits.maximum_provenance_records > 0U &&
        limits.maximum_provenance_records <=
            kHardMaximumStockAuthoritativeFieldProvenance &&
        limits.maximum_schema_name_bytes > 0U &&
        limits.maximum_schema_name_bytes <= 1'024U &&
        limits.maximum_field_name_bytes > 0U &&
        limits.maximum_field_name_bytes <= 1'024U &&
        limits.maximum_total_metadata_bytes > 0U &&
        limits.maximum_total_metadata_bytes <= 1U * 1024U * 1024U &&
        std::isfinite(limits.maximum_coordinate_magnitude) &&
        limits.maximum_coordinate_magnitude > 0.0F &&
        std::isfinite(limits.maximum_velocity_magnitude) &&
        limits.maximum_velocity_magnitude > 0.0F &&
        std::isfinite(limits.maximum_scalar_magnitude) &&
        limits.maximum_scalar_magnitude > 0.0F;
}

StockAuthoritativeMovementObservation::StockAuthoritativeMovementObservation(
    const std::uint64_t runtime_generation,
    const std::uint64_t update_ordinal,
    const std::uint32_t local_player_candidate_entity_number,
    StockAuthoritativeMovementValues values,
    std::vector<StockAuthoritativeFieldProvenance> provenance,
    std::shared_ptr<const StockCommandAcknowledgementEvidenceState>
        command_acknowledgement_evidence,
    const StockAuthoritativeMovementObservationStatus status,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : runtime_generation_{runtime_generation},
      update_ordinal_{update_ordinal},
      local_player_candidate_entity_number_{
          local_player_candidate_entity_number},
      values_{std::move(values)},
      provenance_{std::move(provenance)},
      command_acknowledgement_evidence_{
          std::move(command_acknowledgement_evidence)},
      status_{status},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

StockAuthoritativeMovementObservation::CreationResult
StockAuthoritativeMovementObservation::create(
    const StockAuthoritativeMovementCreateInfo& create_info,
    const StockAuthoritativeMovementLimits& limits) noexcept
{
    if (!valid_stock_authoritative_movement_limits(limits)) {
        return failure(StockAuthoritativeMovementErrorCode::
                           invalid_configuration,
            "invalid stock authoritative movement safety limits");
    }
    if (create_info.runtime_generation == 0U) {
        return failure(StockAuthoritativeMovementErrorCode::
                           invalid_runtime_generation,
            "runtime generation must be nonzero");
    }
    if (create_info.update_ordinal == 0U) {
        return failure(StockAuthoritativeMovementErrorCode::
                           invalid_update_ordinal,
            "authoritative observation ordinal must be nonzero");
    }
    if (create_info.local_player_candidate_entity_number == 0U) {
        return failure(StockAuthoritativeMovementErrorCode::
                           invalid_local_player_candidate,
            "local-player candidate entity number must be nonzero");
    }
    if (!valid_stock_runtime_compatibility_profile(
            create_info.compatibility_profile) ||
        !valid_stock_runtime_evidence_profile(create_info.evidence_profile) ||
        stock_runtime_evidence_profile_for(create_info.compatibility_profile) !=
            create_info.evidence_profile) {
        return failure(StockAuthoritativeMovementErrorCode::profile_mismatch,
            "stock runtime profile tuple is inconsistent");
    }
    if (!pending_profile_tuple(create_info.compatibility_profile,
            create_info.evidence_profile)) {
        return failure(
            StockAuthoritativeMovementErrorCode::stock_evidence_pending,
            "confirmed stock authoritative projection has no evidence gate");
    }

    const auto check_vector = [&limits](
                                  const std::optional<assets::AssetVector3>& value,
                                  const float bound,
                                  const StockAuthoritativeSemanticTarget target)
        -> std::optional<StockAuthoritativeMovementError> {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (!finite_vector(*value)) {
            return StockAuthoritativeMovementError{
                StockAuthoritativeMovementErrorCode::invalid_numeric_value,
                target, "authoritative vector candidate is non-finite"};
        }
        if (!bounded_vector(*value, bound)) {
            return StockAuthoritativeMovementError{
                StockAuthoritativeMovementErrorCode::value_limit_exceeded,
                target,
                "authoritative vector candidate exceeds configured limit"};
        }
        return std::nullopt;
    };

    if (const auto error = check_vector(create_info.values.origin,
            limits.maximum_coordinate_magnitude,
            StockAuthoritativeSemanticTarget::origin)) {
        return {std::nullopt, error};
    }
    if (const auto error = check_vector(create_info.values.velocity,
            limits.maximum_velocity_magnitude,
            StockAuthoritativeSemanticTarget::velocity)) {
        return {std::nullopt, error};
    }
    if (const auto error = check_vector(create_info.values.view_offset,
            limits.maximum_coordinate_magnitude,
            StockAuthoritativeSemanticTarget::view_offset)) {
        return {std::nullopt, error};
    }
    if (const auto error = check_vector(create_info.values.base_velocity,
            limits.maximum_velocity_magnitude,
            StockAuthoritativeSemanticTarget::base_velocity)) {
        return {std::nullopt, error};
    }
    if (create_info.values.hull.has_value() &&
        !valid_hull(*create_info.values.hull)) {
        return failure(StockAuthoritativeMovementErrorCode::invalid_hull,
            "authoritative hull candidate is invalid",
            StockAuthoritativeSemanticTarget::hull);
    }
    if (create_info.values.water_contents.has_value() &&
        !valid_contents(*create_info.values.water_contents)) {
        return failure(
            StockAuthoritativeMovementErrorCode::invalid_water_contents,
            "authoritative contents candidate is invalid",
            StockAuthoritativeSemanticTarget::water_contents);
    }
    if (create_info.values.server_time.has_value() &&
        !valid_pending_stock_server_time_observation(
            *create_info.values.server_time, create_info.runtime_generation)) {
        return failure(
            StockAuthoritativeMovementErrorCode::stock_evidence_pending,
            "server-time candidate lacks a valid opaque pending observation",
            StockAuthoritativeSemanticTarget::server_time);
    }

    const auto check_scalar = [&limits](const std::optional<float>& value,
                                  const bool require_positive,
                                  const StockAuthoritativeSemanticTarget target)
        -> std::optional<StockAuthoritativeMovementError> {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (!std::isfinite(*value)) {
            return StockAuthoritativeMovementError{
                StockAuthoritativeMovementErrorCode::invalid_numeric_value,
                target, "authoritative scalar candidate is non-finite"};
        }
        if (std::fabs(*value) > limits.maximum_scalar_magnitude ||
            (require_positive && *value <= 0.0F) ||
            (!require_positive && *value < 0.0F)) {
            return StockAuthoritativeMovementError{
                StockAuthoritativeMovementErrorCode::value_limit_exceeded,
                target,
                "authoritative scalar candidate exceeds configured domain"};
        }
        return std::nullopt;
    };
    if (const auto error = check_scalar(create_info.values.maximum_speed, false,
            StockAuthoritativeSemanticTarget::maximum_speed)) {
        return {std::nullopt, error};
    }
    if (const auto error = check_scalar(
            create_info.values.gravity_multiplier, true,
            StockAuthoritativeSemanticTarget::gravity_multiplier)) {
        return {std::nullopt, error};
    }
    if (const auto error = check_scalar(
            create_info.values.friction_multiplier, true,
            StockAuthoritativeSemanticTarget::friction_multiplier)) {
        return {std::nullopt, error};
    }

    if (create_info.provenance.size() > limits.maximum_provenance_records) {
        return failure(StockAuthoritativeMovementErrorCode::
                           provenance_limit_exceeded,
            "authoritative provenance safety limit was exceeded");
    }

    std::size_t metadata_bytes = 0U;
    for (auto current = create_info.provenance.begin();
         current != create_info.provenance.end(); ++current) {
        if (current->source_schema.size() > limits.maximum_schema_name_bytes ||
            current->source_field_name.size() >
                limits.maximum_field_name_bytes ||
            current->source_schema.size() >
                std::numeric_limits<std::size_t>::max() -
                    current->source_field_name.size()) {
            return failure(
                StockAuthoritativeMovementErrorCode::metadata_limit_exceeded,
                "authoritative provenance name exceeds configured limit",
                current->semantic_target);
        }
        const auto record_bytes = current->source_schema.size() +
            current->source_field_name.size();
        if (record_bytes > limits.maximum_total_metadata_bytes -
                std::min(metadata_bytes,
                    limits.maximum_total_metadata_bytes)) {
            return failure(
                StockAuthoritativeMovementErrorCode::metadata_limit_exceeded,
                "authoritative provenance metadata limit was exceeded",
                current->semantic_target);
        }
        metadata_bytes += record_bytes;
        if (!valid_provenance_shape(*current)) {
            return failure(
                StockAuthoritativeMovementErrorCode::invalid_provenance,
                "authoritative field provenance is malformed",
                current->semantic_target);
        }
        if (current->confidence ==
            StockAuthoritativeEvidenceConfidence::confirmed_for_profile) {
            return failure(
                StockAuthoritativeMovementErrorCode::stock_evidence_pending,
                "pending profile cannot retain confirmed stock provenance",
                current->semantic_target);
        }
        if (current->support ==
            StockAuthoritativeFieldSupport::supported_for_profile) {
            return failure(
                StockAuthoritativeMovementErrorCode::stock_evidence_pending,
                "pending profile cannot mark a field supported for profile",
                current->semantic_target);
        }
        if (std::find(create_info.provenance.begin(), current, *current) !=
            current) {
            return failure(
                StockAuthoritativeMovementErrorCode::duplicate_provenance,
                "duplicate authoritative field provenance",
                current->semantic_target);
        }
    }

    if (create_info.command_acknowledgement_evidence &&
        (create_info.command_acknowledgement_evidence->runtime_generation() !=
                create_info.runtime_generation ||
            create_info.command_acknowledgement_evidence->
                    compatibility_profile() !=
                create_info.compatibility_profile ||
            create_info.command_acknowledgement_evidence->evidence_profile() !=
                create_info.evidence_profile)) {
        return failure(StockAuthoritativeMovementErrorCode::
                           command_acknowledgement_mismatch,
            "acknowledgement evidence belongs to a different runtime/profile",
            StockAuthoritativeSemanticTarget::command_acknowledgement);
    }

    const auto present = [&create_info](
                             const StockAuthoritativeSemanticTarget target) {
        switch (target) {
        case StockAuthoritativeSemanticTarget::origin:
            return create_info.values.origin.has_value();
        case StockAuthoritativeSemanticTarget::velocity:
            return create_info.values.velocity.has_value();
        case StockAuthoritativeSemanticTarget::view_offset:
            return create_info.values.view_offset.has_value();
        case StockAuthoritativeSemanticTarget::hull:
            return create_info.values.hull.has_value();
        case StockAuthoritativeSemanticTarget::flags:
            return create_info.values.flags.has_value();
        case StockAuthoritativeSemanticTarget::water_level:
            return create_info.values.water_level.has_value();
        case StockAuthoritativeSemanticTarget::water_contents:
            return create_info.values.water_contents.has_value();
        case StockAuthoritativeSemanticTarget::maximum_speed:
            return create_info.values.maximum_speed.has_value();
        case StockAuthoritativeSemanticTarget::gravity_multiplier:
            return create_info.values.gravity_multiplier.has_value();
        case StockAuthoritativeSemanticTarget::friction_multiplier:
            return create_info.values.friction_multiplier.has_value();
        case StockAuthoritativeSemanticTarget::base_velocity:
            return create_info.values.base_velocity.has_value();
        case StockAuthoritativeSemanticTarget::ground_indicator:
            return create_info.values.ground_indicator.has_value();
        case StockAuthoritativeSemanticTarget::old_buttons:
            return create_info.values.old_buttons.has_value();
        case StockAuthoritativeSemanticTarget::server_time:
            return create_info.values.server_time.has_value();
        case StockAuthoritativeSemanticTarget::authoritative_update_identity:
            return create_info.values.authoritative_update_identity.has_value();
        case StockAuthoritativeSemanticTarget::command_acknowledgement:
            return create_info.command_acknowledgement_evidence &&
                create_info.command_acknowledgement_evidence->status() !=
                    StockCommandAcknowledgementEvidenceStatus::unobserved;
        }
        return false;
    };

    for (const auto target : kAllTargets) {
        const auto provenance_count = static_cast<std::size_t>(std::count_if(
            create_info.provenance.begin(), create_info.provenance.end(),
            [target](const auto& item) noexcept {
                return item.semantic_target == target;
            }));
        if (present(target) && provenance_count == 0U) {
            return failure(StockAuthoritativeMovementErrorCode::
                               missing_field_provenance,
                "authoritative candidate lacks field-level provenance",
                target);
        }
        if (!present(target) && provenance_count != 0U) {
            return failure(StockAuthoritativeMovementErrorCode::
                               provenance_without_value,
                "authoritative provenance has no corresponding candidate",
                target);
        }
    }

    if (create_info.values.server_time) {
        const auto& observed = *create_info.values.server_time;
        const bool exact_time_provenance = std::any_of(
            create_info.provenance.begin(), create_info.provenance.end(),
            [&observed](const auto& provenance) noexcept {
                return provenance.semantic_target ==
                           StockAuthoritativeSemanticTarget::server_time &&
                    provenance.source_kind ==
                        StockAuthoritativeSourceKind::runtime_time &&
                    provenance.source_message_category ==
                        observed.source_message_category &&
                    provenance.source_message_ordinal ==
                        observed.source_message_ordinal &&
                    provenance.source_start_cursor ==
                        observed.source_start_cursor &&
                    provenance.source_end_cursor ==
                        observed.source_end_cursor &&
                    provenance.value_origin ==
                        StockAuthoritativeValueOrigin::stock_field_direct;
            });
        if (!exact_time_provenance) {
            return failure(
                StockAuthoritativeMovementErrorCode::invalid_provenance,
                "server-time observation lacks matching canonical provenance",
                StockAuthoritativeSemanticTarget::server_time);
        }
    }

    const bool hull_conflict = std::any_of(
        create_info.provenance.begin(), create_info.provenance.end(),
        [](const auto& item) noexcept {
            return item.semantic_target ==
                       StockAuthoritativeSemanticTarget::hull &&
                   item.support ==
                       StockAuthoritativeFieldSupport::conflicting;
        });
    if (hull_conflict) {
        return failure(
            StockAuthoritativeMovementErrorCode::authoritative_hull_conflict,
            "authoritative hull candidates disagree across evidence sources",
            StockAuthoritativeSemanticTarget::hull);
    }

    const bool conflict = std::any_of(create_info.provenance.begin(),
                              create_info.provenance.end(),
                              [](const auto& item) noexcept {
                                  return item.support ==
                                      StockAuthoritativeFieldSupport::
                                          conflicting;
                              }) ||
        (create_info.command_acknowledgement_evidence &&
            create_info.command_acknowledgement_evidence->status() ==
                StockCommandAcknowledgementEvidenceStatus::conflicting);
    const bool any_value = std::any_of(kAllTargets.begin(), kAllTargets.end(),
        present);
    const bool all_values = std::all_of(kAllTargets.begin(), kAllTargets.end(),
        present);

    StockAuthoritativeMovementObservationStatus status{
        StockAuthoritativeMovementObservationStatus::unobserved};
    if (conflict) {
        status = StockAuthoritativeMovementObservationStatus::field_conflict;
    } else if (all_values) {
        status = StockAuthoritativeMovementObservationStatus::
            complete_candidate_evidence_pending;
    } else if (any_value) {
        status = StockAuthoritativeMovementObservationStatus::
            partial_evidence_pending;
    }

    try {
        auto values = create_info.values;
        auto provenance = create_info.provenance;
        auto acknowledgement = create_info.command_acknowledgement_evidence;
        return {StockAuthoritativeMovementObservation{
                    create_info.runtime_generation, create_info.update_ordinal,
                    create_info.local_player_candidate_entity_number,
                    std::move(values), std::move(provenance),
                    std::move(acknowledgement), status,
                    create_info.compatibility_profile,
                    create_info.evidence_profile},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure(StockAuthoritativeMovementErrorCode::allocation_failed,
            "unable to retain stock authoritative observation");
    }
}

std::uint64_t StockAuthoritativeMovementObservation::runtime_generation()
    const noexcept
{
    return runtime_generation_;
}

std::uint64_t StockAuthoritativeMovementObservation::update_ordinal()
    const noexcept
{
    return update_ordinal_;
}

std::uint32_t StockAuthoritativeMovementObservation::
    local_player_candidate_entity_number() const noexcept
{
    return local_player_candidate_entity_number_;
}

const StockAuthoritativeMovementValues&
StockAuthoritativeMovementObservation::values() const noexcept
{
    return values_;
}

std::span<const StockAuthoritativeFieldProvenance>
StockAuthoritativeMovementObservation::provenance() const noexcept
{
    return provenance_;
}

std::size_t StockAuthoritativeMovementObservation::provenance_count_for(
    const StockAuthoritativeSemanticTarget target) const noexcept
{
    return static_cast<std::size_t>(std::count_if(provenance_.begin(),
        provenance_.end(), [target](const auto& item) noexcept {
            return item.semantic_target == target;
        }));
}

bool StockAuthoritativeMovementObservation::has_value(
    const StockAuthoritativeSemanticTarget target) const noexcept
{
    switch (target) {
    case StockAuthoritativeSemanticTarget::origin:
        return values_.origin.has_value();
    case StockAuthoritativeSemanticTarget::velocity:
        return values_.velocity.has_value();
    case StockAuthoritativeSemanticTarget::view_offset:
        return values_.view_offset.has_value();
    case StockAuthoritativeSemanticTarget::hull:
        return values_.hull.has_value();
    case StockAuthoritativeSemanticTarget::flags:
        return values_.flags.has_value();
    case StockAuthoritativeSemanticTarget::water_level:
        return values_.water_level.has_value();
    case StockAuthoritativeSemanticTarget::water_contents:
        return values_.water_contents.has_value();
    case StockAuthoritativeSemanticTarget::maximum_speed:
        return values_.maximum_speed.has_value();
    case StockAuthoritativeSemanticTarget::gravity_multiplier:
        return values_.gravity_multiplier.has_value();
    case StockAuthoritativeSemanticTarget::friction_multiplier:
        return values_.friction_multiplier.has_value();
    case StockAuthoritativeSemanticTarget::base_velocity:
        return values_.base_velocity.has_value();
    case StockAuthoritativeSemanticTarget::ground_indicator:
        return values_.ground_indicator.has_value();
    case StockAuthoritativeSemanticTarget::old_buttons:
        return values_.old_buttons.has_value();
    case StockAuthoritativeSemanticTarget::server_time:
        return values_.server_time.has_value();
    case StockAuthoritativeSemanticTarget::authoritative_update_identity:
        return values_.authoritative_update_identity.has_value();
    case StockAuthoritativeSemanticTarget::command_acknowledgement:
        return command_acknowledgement_evidence_ &&
            command_acknowledgement_evidence_->status() !=
                StockCommandAcknowledgementEvidenceStatus::unobserved;
    }
    return false;
}

bool StockAuthoritativeMovementObservation::complete_candidate_fields()
    const noexcept
{
    return std::all_of(kAllTargets.begin(), kAllTargets.end(),
               [this](const auto target) noexcept {
                   return has_value(target);
               }) &&
        command_acknowledgement_evidence_->
            exact_usercmd_sequence_available() &&
        status_ != StockAuthoritativeMovementObservationStatus::field_conflict;
}

StockAuthoritativeMovementObservationStatus
StockAuthoritativeMovementObservation::status() const noexcept
{
    return status_;
}

const std::shared_ptr<const StockCommandAcknowledgementEvidenceState>&
StockAuthoritativeMovementObservation::command_acknowledgement_evidence()
    const noexcept
{
    return command_acknowledgement_evidence_;
}

StockRuntimeCompatibilityProfile
StockAuthoritativeMovementObservation::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

StockRuntimeEvidenceProfile
StockAuthoritativeMovementObservation::evidence_profile() const noexcept
{
    return evidence_profile_;
}

std::string_view to_string(
    const StockAuthoritativeSemanticTarget target) noexcept
{
    switch (target) {
    case StockAuthoritativeSemanticTarget::origin: return "origin";
    case StockAuthoritativeSemanticTarget::velocity: return "velocity";
    case StockAuthoritativeSemanticTarget::view_offset: return "view_offset";
    case StockAuthoritativeSemanticTarget::hull: return "hull";
    case StockAuthoritativeSemanticTarget::flags: return "flags";
    case StockAuthoritativeSemanticTarget::water_level: return "water_level";
    case StockAuthoritativeSemanticTarget::water_contents:
        return "water_contents";
    case StockAuthoritativeSemanticTarget::maximum_speed:
        return "maximum_speed";
    case StockAuthoritativeSemanticTarget::gravity_multiplier:
        return "gravity_multiplier";
    case StockAuthoritativeSemanticTarget::friction_multiplier:
        return "friction_multiplier";
    case StockAuthoritativeSemanticTarget::base_velocity:
        return "base_velocity";
    case StockAuthoritativeSemanticTarget::ground_indicator:
        return "ground_indicator";
    case StockAuthoritativeSemanticTarget::old_buttons: return "old_buttons";
    case StockAuthoritativeSemanticTarget::server_time: return "server_time";
    case StockAuthoritativeSemanticTarget::authoritative_update_identity:
        return "authoritative_update_identity";
    case StockAuthoritativeSemanticTarget::command_acknowledgement:
        return "command_acknowledgement";
    }
    return "unknown";
}

std::string_view to_string(
    const StockAuthoritativeMovementObservationStatus status) noexcept
{
    switch (status) {
    case StockAuthoritativeMovementObservationStatus::unobserved:
        return "unobserved";
    case StockAuthoritativeMovementObservationStatus::
            partial_evidence_pending:
        return "partial_evidence_pending";
    case StockAuthoritativeMovementObservationStatus::
            complete_candidate_evidence_pending:
        return "complete_candidate_evidence_pending";
    case StockAuthoritativeMovementObservationStatus::field_conflict:
        return "field_conflict";
    }
    return "unknown";
}

std::string_view to_string(
    const StockAuthoritativeMovementErrorCode code) noexcept
{
    switch (code) {
    case StockAuthoritativeMovementErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StockAuthoritativeMovementErrorCode::invalid_runtime_generation:
        return "invalid_runtime_generation";
    case StockAuthoritativeMovementErrorCode::invalid_update_ordinal:
        return "invalid_update_ordinal";
    case StockAuthoritativeMovementErrorCode::invalid_local_player_candidate:
        return "invalid_local_player_candidate";
    case StockAuthoritativeMovementErrorCode::invalid_numeric_value:
        return "invalid_numeric_value";
    case StockAuthoritativeMovementErrorCode::value_limit_exceeded:
        return "value_limit_exceeded";
    case StockAuthoritativeMovementErrorCode::invalid_hull:
        return "invalid_hull";
    case StockAuthoritativeMovementErrorCode::invalid_water_contents:
        return "invalid_water_contents";
    case StockAuthoritativeMovementErrorCode::provenance_limit_exceeded:
        return "provenance_limit_exceeded";
    case StockAuthoritativeMovementErrorCode::metadata_limit_exceeded:
        return "metadata_limit_exceeded";
    case StockAuthoritativeMovementErrorCode::invalid_provenance:
        return "invalid_provenance";
    case StockAuthoritativeMovementErrorCode::missing_field_provenance:
        return "missing_field_provenance";
    case StockAuthoritativeMovementErrorCode::provenance_without_value:
        return "provenance_without_value";
    case StockAuthoritativeMovementErrorCode::duplicate_provenance:
        return "duplicate_provenance";
    case StockAuthoritativeMovementErrorCode::authoritative_hull_conflict:
        return "authoritative_hull_conflict";
    case StockAuthoritativeMovementErrorCode::
            command_acknowledgement_mismatch:
        return "command_acknowledgement_mismatch";
    case StockAuthoritativeMovementErrorCode::profile_mismatch:
        return "profile_mismatch";
    case StockAuthoritativeMovementErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case StockAuthoritativeMovementErrorCode::allocation_failed:
        return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

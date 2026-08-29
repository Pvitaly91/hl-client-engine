#include <hlclient/goldsrc/stock_local_player_identity.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] StockLocalPlayerIdentityOperationResult failure(
    const StockLocalPlayerIdentityErrorCode code,
    const std::string_view context,
    const std::optional<StockLocalPlayerIdentityEvidenceSource> source =
        std::nullopt) noexcept
{
    return {StockLocalPlayerIdentityError{code, source, context}};
}

[[nodiscard]] StockLocalPlayerIdentityPublishResult publish_failure(
    const StockLocalPlayerIdentityErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt,
        StockLocalPlayerIdentityError{code, std::nullopt, context}};
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

[[nodiscard]] bool entity_candidate_source(
    const StockLocalPlayerIdentityEvidenceSource source) noexcept
{
    switch (source) {
    case StockLocalPlayerIdentityEvidenceSource::view_entity_candidate:
    case StockLocalPlayerIdentityEvidenceSource::player_entity_candidate:
    case StockLocalPlayerIdentityEvidenceSource::
            client_local_message_association:
    case StockLocalPlayerIdentityEvidenceSource::
            two_client_differential_correlation:
    case StockLocalPlayerIdentityEvidenceSource::status_metadata_projection:
        return true;
    case StockLocalPlayerIdentityEvidenceSource::server_info_slot_candidate:
    case StockLocalPlayerIdentityEvidenceSource::user_info_client_index:
        return false;
    }
    return false;
}

} // namespace

bool valid_stock_local_player_identity_limits(
    const StockLocalPlayerIdentityLimits& limits) noexcept
{
    return limits.maximum_evidence_records > 0U &&
        limits.maximum_evidence_records <=
            kHardMaximumStockIdentityEvidenceRecords &&
        limits.maximum_entity_number > 0U &&
        limits.maximum_entity_number <=
            kHardMaximumStockIdentityEntityNumber;
}

StockLocalPlayerIdentityState::StockLocalPlayerIdentityState(
    const std::uint64_t runtime_generation,
    const StockLocalPlayerIdentityStatus status,
    std::optional<std::uint32_t> candidate_entity_number,
    std::vector<StockLocalPlayerIdentityEvidenceRecord> evidence_records,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : runtime_generation_{runtime_generation},
      status_{status},
      candidate_entity_number_{candidate_entity_number},
      evidence_records_{std::move(evidence_records)},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

std::uint64_t StockLocalPlayerIdentityState::runtime_generation() const noexcept
{
    return runtime_generation_;
}

StockLocalPlayerIdentityStatus StockLocalPlayerIdentityState::status()
    const noexcept
{
    return status_;
}

const std::optional<std::uint32_t>&
StockLocalPlayerIdentityState::candidate_entity_number() const noexcept
{
    return candidate_entity_number_;
}

std::optional<std::uint32_t>
StockLocalPlayerIdentityState::confirmed_entity_number() const noexcept
{
    // The confirmed profile has no construction path in this evidence-boundary
    // implementation. Keeping this explicit prevents a candidate from being
    // consumed as a production identity by checking only that it is present.
    return status_ == StockLocalPlayerIdentityStatus::confirmed_for_profile
        ? candidate_entity_number_
        : std::nullopt;
}

std::span<const StockLocalPlayerIdentityEvidenceRecord>
StockLocalPlayerIdentityState::evidence_records() const noexcept
{
    return evidence_records_;
}

std::size_t StockLocalPlayerIdentityState::evidence_record_count()
    const noexcept
{
    return evidence_records_.size();
}

bool StockLocalPlayerIdentityState::has_multi_client_evidence() const noexcept
{
    return std::any_of(evidence_records_.begin(), evidence_records_.end(),
        [](const auto& record) noexcept {
            return record.kind ==
                StockLocalPlayerIdentityEvidenceKind::multi_client_relation;
        });
}

StockRuntimeCompatibilityProfile
StockLocalPlayerIdentityState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

StockRuntimeEvidenceProfile StockLocalPlayerIdentityState::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

StockLocalPlayerIdentityBuilder::StockLocalPlayerIdentityBuilder(
    const std::uint64_t runtime_generation,
    const StockLocalPlayerIdentityLimits limits,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : runtime_generation_{runtime_generation},
      limits_{limits},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile},
      valid_configuration_{
          runtime_generation != 0U &&
          valid_stock_local_player_identity_limits(limits) &&
          valid_stock_runtime_compatibility_profile(compatibility_profile) &&
          valid_stock_runtime_evidence_profile(evidence_profile) &&
          evidence_profile ==
              stock_runtime_evidence_profile_for(compatibility_profile) &&
          pending_profile_tuple(compatibility_profile, evidence_profile)}
{
}

bool StockLocalPlayerIdentityBuilder::valid_configuration() const noexcept
{
    return valid_configuration_;
}

const StockLocalPlayerIdentityLimits&
StockLocalPlayerIdentityBuilder::limits() const noexcept
{
    return limits_;
}

StockLocalPlayerIdentityOperationResult
StockLocalPlayerIdentityBuilder::observe_server_info_slot_candidate(
    const std::uint32_t slot,
    StockRuntimeSourceCursor source_cursor)
{
    return append({
        StockLocalPlayerIdentityEvidenceSource::server_info_slot_candidate,
        StockLocalPlayerIdentityEvidenceKind::routing_value_only,
        slot,
        std::nullopt,
        std::move(source_cursor),
    });
}

StockLocalPlayerIdentityOperationResult
StockLocalPlayerIdentityBuilder::observe_user_info_client_index(
    const std::uint8_t client_index,
    StockRuntimeSourceCursor source_cursor)
{
    return append({
        StockLocalPlayerIdentityEvidenceSource::user_info_client_index,
        StockLocalPlayerIdentityEvidenceKind::routing_value_only,
        client_index,
        std::nullopt,
        std::move(source_cursor),
    });
}

StockLocalPlayerIdentityOperationResult
StockLocalPlayerIdentityBuilder::observe_view_entity_candidate(
    const std::uint32_t entity_number,
    StockRuntimeSourceCursor source_cursor)
{
    return append({
        StockLocalPlayerIdentityEvidenceSource::view_entity_candidate,
        StockLocalPlayerIdentityEvidenceKind::entity_number_candidate,
        entity_number,
        entity_number,
        std::move(source_cursor),
    });
}

StockLocalPlayerIdentityOperationResult
StockLocalPlayerIdentityBuilder::observe_player_entity_candidate(
    const std::uint32_t entity_number,
    StockRuntimeSourceCursor source_cursor)
{
    return append({
        StockLocalPlayerIdentityEvidenceSource::player_entity_candidate,
        StockLocalPlayerIdentityEvidenceKind::entity_number_candidate,
        entity_number,
        entity_number,
        std::move(source_cursor),
    });
}

StockLocalPlayerIdentityOperationResult
StockLocalPlayerIdentityBuilder::observe_client_local_association_candidate(
    const std::uint32_t entity_number,
    StockRuntimeSourceCursor source_cursor)
{
    return append({
        StockLocalPlayerIdentityEvidenceSource::
            client_local_message_association,
        StockLocalPlayerIdentityEvidenceKind::entity_number_candidate,
        entity_number,
        entity_number,
        std::move(source_cursor),
    });
}

StockLocalPlayerIdentityOperationResult
StockLocalPlayerIdentityBuilder::observe_two_client_correlation(
    const std::uint8_t client_index,
    const std::uint32_t entity_number,
    StockRuntimeSourceCursor source_cursor)
{
    return append({
        StockLocalPlayerIdentityEvidenceSource::
            two_client_differential_correlation,
        StockLocalPlayerIdentityEvidenceKind::multi_client_relation,
        client_index,
        entity_number,
        std::move(source_cursor),
    });
}

StockLocalPlayerIdentityOperationResult
StockLocalPlayerIdentityBuilder::observe_status_metadata_candidate(
    const std::uint32_t routing_value,
    const std::uint32_t entity_number,
    StockRuntimeSourceCursor source_cursor)
{
    return append({
        StockLocalPlayerIdentityEvidenceSource::status_metadata_projection,
        StockLocalPlayerIdentityEvidenceKind::entity_number_candidate,
        routing_value,
        entity_number,
        std::move(source_cursor),
    });
}

StockLocalPlayerIdentityOperationResult StockLocalPlayerIdentityBuilder::append(
    StockLocalPlayerIdentityEvidenceRecord record)
{
    if (!valid_configuration_) {
        return failure(
            pending_profile_tuple(compatibility_profile_, evidence_profile_)
                ? StockLocalPlayerIdentityErrorCode::invalid_configuration
                : StockLocalPlayerIdentityErrorCode::stock_evidence_pending,
            "stock local-player identity builder configuration is unavailable",
            record.source);
    }
    if (!record.source_cursor.has_value()) {
        return failure(StockLocalPlayerIdentityErrorCode::invalid_source_cursor,
            "identity evidence requires an invariant-preserving source cursor",
            record.source);
    }
    if (record.kind ==
            StockLocalPlayerIdentityEvidenceKind::routing_value_only &&
        record.candidate_entity_number.has_value()) {
        return failure(StockLocalPlayerIdentityErrorCode::invalid_domain_value,
            "routing-only identity evidence cannot name an entity",
            record.source);
    }
    if (entity_candidate_source(record.source) !=
        record.candidate_entity_number.has_value()) {
        return failure(StockLocalPlayerIdentityErrorCode::invalid_domain_value,
            "identity evidence domain and entity candidate disagree",
            record.source);
    }
    if (record.candidate_entity_number &&
        (*record.candidate_entity_number == 0U ||
            *record.candidate_entity_number > limits_.maximum_entity_number)) {
        return failure(
            StockLocalPlayerIdentityErrorCode::entity_number_limit_exceeded,
            "identity evidence entity number is outside the configured domain",
            record.source);
    }
    if (evidence_records_.size() >= limits_.maximum_evidence_records) {
        return failure(
            StockLocalPlayerIdentityErrorCode::evidence_limit_exceeded,
            "identity evidence record limit is exhausted", record.source);
    }
    if (std::find(evidence_records_.begin(), evidence_records_.end(), record) !=
        evidence_records_.end()) {
        return failure(StockLocalPlayerIdentityErrorCode::duplicate_evidence,
            "duplicate identity evidence is not counted twice", record.source);
    }
    try {
        evidence_records_.push_back(std::move(record));
    } catch (const std::bad_alloc&) {
        return failure(StockLocalPlayerIdentityErrorCode::allocation_failed,
            "unable to retain bounded identity evidence");
    }
    return {};
}

StockLocalPlayerIdentityPublishResult
StockLocalPlayerIdentityBuilder::publish() const
{
    if (!valid_configuration_) {
        return publish_failure(
            pending_profile_tuple(compatibility_profile_, evidence_profile_)
                ? StockLocalPlayerIdentityErrorCode::invalid_configuration
                : StockLocalPlayerIdentityErrorCode::stock_evidence_pending,
            "stock local-player identity configuration is unavailable");
    }

    std::optional<std::uint32_t> candidate;
    bool conflicting = false;
    bool multi_client = false;
    for (const auto& record : evidence_records_) {
        multi_client = multi_client || record.kind ==
            StockLocalPlayerIdentityEvidenceKind::multi_client_relation;
        if (!record.candidate_entity_number) {
            continue;
        }
        if (!candidate) {
            candidate = record.candidate_entity_number;
        } else if (*candidate != *record.candidate_entity_number) {
            conflicting = true;
        }
    }

    auto status = StockLocalPlayerIdentityStatus::unresolved;
    if (conflicting) {
        status = StockLocalPlayerIdentityStatus::conflicting;
        candidate.reset();
    } else if (candidate && multi_client) {
        // Multi-client correlation is deliberately visible for research, but
        // the confirmed profile remains unavailable until accepted captures
        // are encoded in a later implementation.
        status = StockLocalPlayerIdentityStatus::multi_client_correlated;
    } else if (candidate) {
        status = StockLocalPlayerIdentityStatus::single_client_candidate;
    }

    try {
        return {StockLocalPlayerIdentityState{runtime_generation_, status,
                    candidate, evidence_records_, compatibility_profile_,
                    evidence_profile_},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return publish_failure(StockLocalPlayerIdentityErrorCode::allocation_failed,
            "unable to publish immutable identity evidence");
    }
}

std::span<const StockLocalPlayerIdentityEvidenceRecord>
StockLocalPlayerIdentityBuilder::candidate_evidence() const noexcept
{
    return evidence_records_;
}

std::string_view to_string(
    const StockLocalPlayerIdentityStatus status) noexcept
{
    switch (status) {
    case StockLocalPlayerIdentityStatus::unresolved: return "unresolved";
    case StockLocalPlayerIdentityStatus::single_client_candidate:
        return "single_client_candidate";
    case StockLocalPlayerIdentityStatus::multi_client_correlated:
        return "multi_client_correlated";
    case StockLocalPlayerIdentityStatus::confirmed_for_profile:
        return "confirmed_for_profile";
    case StockLocalPlayerIdentityStatus::conflicting: return "conflicting";
    }
    return "unknown";
}

std::string_view to_string(
    const StockLocalPlayerIdentityErrorCode code) noexcept
{
    switch (code) {
    case StockLocalPlayerIdentityErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StockLocalPlayerIdentityErrorCode::invalid_runtime_generation:
        return "invalid_runtime_generation";
    case StockLocalPlayerIdentityErrorCode::invalid_source_cursor:
        return "invalid_source_cursor";
    case StockLocalPlayerIdentityErrorCode::invalid_domain_value:
        return "invalid_domain_value";
    case StockLocalPlayerIdentityErrorCode::entity_number_limit_exceeded:
        return "entity_number_limit_exceeded";
    case StockLocalPlayerIdentityErrorCode::evidence_limit_exceeded:
        return "evidence_limit_exceeded";
    case StockLocalPlayerIdentityErrorCode::duplicate_evidence:
        return "duplicate_evidence";
    case StockLocalPlayerIdentityErrorCode::profile_mismatch:
        return "profile_mismatch";
    case StockLocalPlayerIdentityErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case StockLocalPlayerIdentityErrorCode::allocation_failed:
        return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

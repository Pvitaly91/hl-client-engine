#include <hlclient/goldsrc/stock_command_ack_evidence.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool pending_profile_tuple(
    const StockRuntimeCompatibilityProfile compatibility,
    const StockRuntimeEvidenceProfile evidence) noexcept
{
    return compatibility == StockRuntimeCompatibilityProfile::
               stock_protocol_48_build_10210_evidence_pending &&
        evidence == StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending;
}

[[nodiscard]] StockCommandAcknowledgementEvidenceOperationResult failure(
    const StockCommandAcknowledgementEvidenceErrorCode code,
    const std::string_view context,
    const std::optional<StockCommandAcknowledgementCandidateDomain> domain =
        std::nullopt) noexcept
{
    return {StockCommandAcknowledgementEvidenceError{code, domain, context}};
}

[[nodiscard]] StockCommandAcknowledgementEvidencePublishResult publish_failure(
    const StockCommandAcknowledgementEvidenceErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt,
        StockCommandAcknowledgementEvidenceError{
            code, std::nullopt, context}};
}

[[nodiscard]] bool references_domain(
    const StockCommandAcknowledgementCorrelationObservation& correlation,
    const StockCommandAcknowledgementCandidateDomain domain) noexcept
{
    return correlation.candidate_domain == domain ||
        correlation.reference_domain == domain;
}

[[nodiscard]] bool valid_candidate_domain(
    const StockCommandAcknowledgementCandidateDomain domain) noexcept
{
    switch (domain) {
    case StockCommandAcknowledgementCandidateDomain::
            client_to_server_netchan_sequence:
    case StockCommandAcknowledgementCandidateDomain::
            server_to_client_netchan_acknowledgement:
    case StockCommandAcknowledgementCandidateDomain::client_move_packet_ordinal:
    case StockCommandAcknowledgementCandidateDomain::
            server_runtime_frame_reference:
    case StockCommandAcknowledgementCandidateDomain::explicit_clientdata_field:
    case StockCommandAcknowledgementCandidateDomain::exact_usercmd_sequence:
        return true;
    }
    return false;
}

} // namespace

bool valid_stock_command_acknowledgement_evidence_limits(
    const StockCommandAcknowledgementEvidenceLimits& limits) noexcept
{
    return limits.maximum_candidates > 0U &&
        limits.maximum_candidates <= kHardMaximumStockAckCandidates &&
        limits.maximum_correlations > 0U &&
        limits.maximum_correlations <= kHardMaximumStockAckCorrelations &&
        limits.maximum_candidate_value > 0U;
}

StockCommandAcknowledgementEvidenceState::
    StockCommandAcknowledgementEvidenceState(
        const std::uint64_t runtime_generation,
        const StockCommandAcknowledgementEvidenceStatus status,
        std::vector<StockCommandAcknowledgementCandidateObservation>
            candidates,
        std::vector<StockCommandAcknowledgementCorrelationObservation>
            correlations,
        const StockRuntimeCompatibilityProfile compatibility_profile,
        const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : runtime_generation_{runtime_generation},
      status_{status},
      candidates_{std::move(candidates)},
      correlations_{std::move(correlations)},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

std::uint64_t StockCommandAcknowledgementEvidenceState::runtime_generation()
    const noexcept
{
    return runtime_generation_;
}

StockCommandAcknowledgementEvidenceStatus
StockCommandAcknowledgementEvidenceState::status() const noexcept
{
    return status_;
}

std::span<const StockCommandAcknowledgementCandidateObservation>
StockCommandAcknowledgementEvidenceState::candidates() const noexcept
{
    return candidates_;
}

std::span<const StockCommandAcknowledgementCorrelationObservation>
StockCommandAcknowledgementEvidenceState::correlations() const noexcept
{
    return correlations_;
}

bool StockCommandAcknowledgementEvidenceState::has_domain(
    const StockCommandAcknowledgementCandidateDomain domain) const noexcept
{
    return std::any_of(candidates_.begin(), candidates_.end(),
        [domain](const auto& candidate) noexcept {
            return candidate.domain == domain;
        });
}

bool StockCommandAcknowledgementEvidenceState::
    exact_usercmd_sequence_available() const noexcept
{
    // The evidence-pending builder has no path to this status. Keeping the
    // check tied to the explicit status prevents an exact-domain candidate
    // from being promoted by presence alone.
    return status_ == StockCommandAcknowledgementEvidenceStatus::
        exact_usercmd_sequence_confirmed;
}

StockRuntimeCompatibilityProfile
StockCommandAcknowledgementEvidenceState::compatibility_profile()
    const noexcept
{
    return compatibility_profile_;
}

StockRuntimeEvidenceProfile
StockCommandAcknowledgementEvidenceState::evidence_profile() const noexcept
{
    return evidence_profile_;
}

StockCommandAcknowledgementEvidenceBuilder::
    StockCommandAcknowledgementEvidenceBuilder(
        const std::uint64_t runtime_generation,
        const StockCommandAcknowledgementEvidenceLimits limits,
        const StockRuntimeCompatibilityProfile compatibility_profile,
        const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : runtime_generation_{runtime_generation},
      limits_{limits},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile},
      valid_configuration_{
          runtime_generation != 0U &&
          valid_stock_command_acknowledgement_evidence_limits(limits) &&
          valid_stock_runtime_compatibility_profile(compatibility_profile) &&
          valid_stock_runtime_evidence_profile(evidence_profile) &&
          evidence_profile ==
              stock_runtime_evidence_profile_for(compatibility_profile) &&
          pending_profile_tuple(compatibility_profile, evidence_profile)}
{
}

bool StockCommandAcknowledgementEvidenceBuilder::valid_configuration()
    const noexcept
{
    return valid_configuration_;
}

const StockCommandAcknowledgementEvidenceLimits&
StockCommandAcknowledgementEvidenceBuilder::limits() const noexcept
{
    return limits_;
}

StockCommandAcknowledgementEvidenceOperationResult
StockCommandAcknowledgementEvidenceBuilder::observe_candidate(
    const StockCommandAcknowledgementCandidateDomain domain,
    const std::uint64_t value,
    const std::size_t source_record_ordinal,
    StockRuntimeSourceCursor source_cursor)
{
    if (!valid_configuration_) {
        return failure(
            pending_profile_tuple(compatibility_profile_, evidence_profile_)
                ? StockCommandAcknowledgementEvidenceErrorCode::
                      invalid_configuration
                : StockCommandAcknowledgementEvidenceErrorCode::
                      stock_evidence_pending,
            "stock acknowledgement evidence configuration is unavailable",
            domain);
    }
    if (!valid_candidate_domain(domain)) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::
                invalid_candidate_domain,
            "acknowledgement candidate domain is invalid");
    }
    if (!valid_stock_runtime_source_cursor(source_cursor)) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::
                invalid_source_cursor,
            "acknowledgement candidate requires a valid source cursor",
            domain);
    }
    if (value > limits_.maximum_candidate_value) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::
                candidate_value_out_of_range,
            "acknowledgement candidate exceeds the configured safety limit",
            domain);
    }
    if (candidates_.size() >= limits_.maximum_candidates) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::
                candidate_limit_exceeded,
            "acknowledgement candidate safety limit was reached", domain);
    }

    StockCommandAcknowledgementCandidateObservation observation{
        domain, value, source_record_ordinal, std::move(source_cursor)};
    if (std::find(candidates_.begin(), candidates_.end(), observation) !=
        candidates_.end()) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::duplicate_candidate,
            "duplicate acknowledgement candidate", domain);
    }

    try {
        candidates_.push_back(std::move(observation));
    } catch (const std::bad_alloc&) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::allocation_failed,
            "unable to retain acknowledgement candidate", domain);
    }
    return {};
}

StockCommandAcknowledgementEvidenceOperationResult
StockCommandAcknowledgementEvidenceBuilder::observe_correlation(
    StockCommandAcknowledgementCorrelationObservation correlation)
{
    if (!valid_configuration_) {
        return failure(
            pending_profile_tuple(compatibility_profile_, evidence_profile_)
                ? StockCommandAcknowledgementEvidenceErrorCode::
                      invalid_configuration
                : StockCommandAcknowledgementEvidenceErrorCode::
                      stock_evidence_pending,
            "stock acknowledgement evidence configuration is unavailable");
    }
    if (!valid_candidate_domain(correlation.candidate_domain) ||
        !valid_candidate_domain(correlation.reference_domain) ||
        correlation.candidate_domain == correlation.reference_domain ||
        correlation.sample_count == 0U ||
        correlation.matching_progression_count > correlation.sample_count ||
        correlation.contradiction_count > correlation.sample_count -
                correlation.matching_progression_count ||
        correlation.loss_scenario_count > correlation.sample_count ||
        correlation.batching_scenario_count > correlation.sample_count ||
        correlation.reset_scenario_count > correlation.sample_count) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::invalid_correlation,
            "acknowledgement correlation counters are inconsistent",
            correlation.candidate_domain);
    }
    if (!has_candidate_domain(correlation.candidate_domain) ||
        !has_candidate_domain(correlation.reference_domain)) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::
                missing_candidate_domain,
            "correlation requires observed candidates in both typed domains",
            correlation.candidate_domain);
    }
    if (correlations_.size() >= limits_.maximum_correlations) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::
                correlation_limit_exceeded,
            "acknowledgement correlation safety limit was reached",
            correlation.candidate_domain);
    }
    if (std::find(correlations_.begin(), correlations_.end(), correlation) !=
        correlations_.end()) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::
                duplicate_correlation,
            "duplicate acknowledgement correlation",
            correlation.candidate_domain);
    }

    try {
        correlations_.push_back(std::move(correlation));
    } catch (const std::bad_alloc&) {
        return failure(
            StockCommandAcknowledgementEvidenceErrorCode::allocation_failed,
            "unable to retain acknowledgement correlation");
    }
    return {};
}

StockCommandAcknowledgementEvidencePublishResult
StockCommandAcknowledgementEvidenceBuilder::publish() const
{
    if (!valid_configuration_) {
        return publish_failure(
            pending_profile_tuple(compatibility_profile_, evidence_profile_)
                ? StockCommandAcknowledgementEvidenceErrorCode::
                      invalid_configuration
                : StockCommandAcknowledgementEvidenceErrorCode::
                      stock_evidence_pending,
            "stock acknowledgement evidence configuration is unavailable");
    }

    const bool correlation_conflict = std::any_of(
        correlations_.begin(), correlations_.end(),
        [](const auto& correlation) noexcept {
            return correlation.contradiction_count != 0U;
        });
    bool source_conflict = false;
    for (auto left = candidates_.begin(); left != candidates_.end(); ++left) {
        for (auto right = left + 1; right != candidates_.end(); ++right) {
            if (left->domain == right->domain &&
                left->source_record_ordinal == right->source_record_ordinal &&
                left->source_cursor == right->source_cursor &&
                left->value != right->value) {
                source_conflict = true;
                break;
            }
        }
        if (source_conflict) {
            break;
        }
    }

    StockCommandAcknowledgementEvidenceStatus status{
        StockCommandAcknowledgementEvidenceStatus::unobserved};
    const auto distinct_source_count = [this](
                                           const auto domain) noexcept {
        std::size_t count = 0U;
        for (auto current = candidates_.begin(); current != candidates_.end();
             ++current) {
            if (current->domain != domain) continue;
            const bool first_for_source = std::none_of(
                candidates_.begin(), current,
                [current](const auto& prior) noexcept {
                    return prior.domain == current->domain &&
                        prior.source_record_ordinal ==
                            current->source_record_ordinal;
                });
            if (first_for_source) ++count;
        }
        return count;
    };
    const auto correlation_is_covered = [&distinct_source_count](
                                             const auto& correlation) noexcept {
        return correlation.sample_count >= 2U &&
            correlation.matching_progression_count ==
                correlation.sample_count &&
            correlation.contradiction_count == 0U &&
            correlation.loss_scenario_count != 0U &&
            correlation.batching_scenario_count != 0U &&
            correlation.reset_scenario_count != 0U &&
            distinct_source_count(correlation.candidate_domain) >=
                correlation.sample_count &&
            distinct_source_count(correlation.reference_domain) >=
                correlation.sample_count;
    };
    if (correlation_conflict || source_conflict) {
        status = StockCommandAcknowledgementEvidenceStatus::conflicting;
    } else if (has_candidate_domain(
                   StockCommandAcknowledgementCandidateDomain::
                       exact_usercmd_sequence)) {
        status = StockCommandAcknowledgementEvidenceStatus::
            exact_usercmd_sequence_pending;
    } else if (std::any_of(correlations_.begin(), correlations_.end(),
                   [&correlation_is_covered](const auto& correlation) noexcept {
                       return correlation_is_covered(correlation) &&
                           references_domain(correlation,
                           StockCommandAcknowledgementCandidateDomain::
                               client_move_packet_ordinal);
                   })) {
        status = StockCommandAcknowledgementEvidenceStatus::
            correlates_with_move_packet;
    } else if (std::any_of(correlations_.begin(), correlations_.end(),
                   [&correlation_is_covered](const auto& correlation) noexcept {
                       return correlation_is_covered(correlation) &&
                           (references_domain(correlation,
                                StockCommandAcknowledgementCandidateDomain::
                                    client_to_server_netchan_sequence) ||
                               references_domain(correlation,
                                StockCommandAcknowledgementCandidateDomain::
                                    server_to_client_netchan_acknowledgement));
                   })) {
        status = StockCommandAcknowledgementEvidenceStatus::
            correlates_with_netchan_sequence;
    } else if (!candidates_.empty()) {
        status = StockCommandAcknowledgementEvidenceStatus::
            candidate_value_observed;
    }

    try {
        return {StockCommandAcknowledgementEvidenceState{
                    runtime_generation_, status, candidates_, correlations_,
                    compatibility_profile_, evidence_profile_},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return publish_failure(
            StockCommandAcknowledgementEvidenceErrorCode::allocation_failed,
            "unable to publish acknowledgement evidence state");
    }
}

bool StockCommandAcknowledgementEvidenceBuilder::has_candidate_domain(
    const StockCommandAcknowledgementCandidateDomain domain) const noexcept
{
    return std::any_of(candidates_.begin(), candidates_.end(),
        [domain](const auto& candidate) noexcept {
            return candidate.domain == domain;
        });
}

std::string_view to_string(
    const StockCommandAcknowledgementCandidateDomain domain) noexcept
{
    switch (domain) {
    case StockCommandAcknowledgementCandidateDomain::
            client_to_server_netchan_sequence:
        return "client_to_server_netchan_sequence";
    case StockCommandAcknowledgementCandidateDomain::
            server_to_client_netchan_acknowledgement:
        return "server_to_client_netchan_acknowledgement";
    case StockCommandAcknowledgementCandidateDomain::client_move_packet_ordinal:
        return "client_move_packet_ordinal";
    case StockCommandAcknowledgementCandidateDomain::
            server_runtime_frame_reference:
        return "server_runtime_frame_reference";
    case StockCommandAcknowledgementCandidateDomain::
            explicit_clientdata_field:
        return "explicit_clientdata_field";
    case StockCommandAcknowledgementCandidateDomain::exact_usercmd_sequence:
        return "exact_usercmd_sequence";
    }
    return "unknown";
}

std::string_view to_string(
    const StockCommandAcknowledgementEvidenceStatus status) noexcept
{
    switch (status) {
    case StockCommandAcknowledgementEvidenceStatus::unobserved:
        return "unobserved";
    case StockCommandAcknowledgementEvidenceStatus::candidate_value_observed:
        return "candidate_value_observed";
    case StockCommandAcknowledgementEvidenceStatus::
            correlates_with_netchan_sequence:
        return "correlates_with_netchan_sequence";
    case StockCommandAcknowledgementEvidenceStatus::
            correlates_with_move_packet:
        return "correlates_with_move_packet";
    case StockCommandAcknowledgementEvidenceStatus::
            exact_usercmd_sequence_pending:
        return "exact_usercmd_sequence_pending";
    case StockCommandAcknowledgementEvidenceStatus::
            exact_usercmd_sequence_confirmed:
        return "exact_usercmd_sequence_confirmed";
    case StockCommandAcknowledgementEvidenceStatus::conflicting:
        return "conflicting";
    }
    return "unknown";
}

std::string_view to_string(
    const StockCommandAcknowledgementEvidenceErrorCode code) noexcept
{
    switch (code) {
    case StockCommandAcknowledgementEvidenceErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StockCommandAcknowledgementEvidenceErrorCode::
            invalid_runtime_generation:
        return "invalid_runtime_generation";
    case StockCommandAcknowledgementEvidenceErrorCode::invalid_candidate_domain:
        return "invalid_candidate_domain";
    case StockCommandAcknowledgementEvidenceErrorCode::invalid_source_cursor:
        return "invalid_source_cursor";
    case StockCommandAcknowledgementEvidenceErrorCode::
            candidate_value_out_of_range:
        return "candidate_value_out_of_range";
    case StockCommandAcknowledgementEvidenceErrorCode::
            candidate_limit_exceeded:
        return "candidate_limit_exceeded";
    case StockCommandAcknowledgementEvidenceErrorCode::
            correlation_limit_exceeded:
        return "correlation_limit_exceeded";
    case StockCommandAcknowledgementEvidenceErrorCode::invalid_correlation:
        return "invalid_correlation";
    case StockCommandAcknowledgementEvidenceErrorCode::missing_candidate_domain:
        return "missing_candidate_domain";
    case StockCommandAcknowledgementEvidenceErrorCode::duplicate_candidate:
        return "duplicate_candidate";
    case StockCommandAcknowledgementEvidenceErrorCode::duplicate_correlation:
        return "duplicate_correlation";
    case StockCommandAcknowledgementEvidenceErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case StockCommandAcknowledgementEvidenceErrorCode::allocation_failed:
        return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

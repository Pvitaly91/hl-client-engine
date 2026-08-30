#include <hlclient/goldsrc/stock_runtime_reconnect_lifecycle.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

constexpr std::size_t kMaximumProfileIdentityBytes = 128U;
constexpr std::size_t kMaximumGenerationPackets = 1'000'000U;
constexpr std::size_t kMaximumConnectionlessExchanges = 16'384U;

[[nodiscard]] bool safe_identity_token(
    const std::string_view value,
    const std::size_t maximum) noexcept
{
    return !value.empty() && value.size() <= maximum &&
        std::ranges::all_of(value, [](const char character) {
            return (character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') ||
                   character == '_' || character == '-' || character == '.';
        });
}

[[nodiscard]] StockRuntimeReconnectLifecycleValidationResult failure(
    const StockRuntimeReconnectLifecycleErrorCode code,
    const std::size_t generation_ordinal,
    std::string context)
{
    return {std::nullopt,
            StockRuntimeReconnectLifecycleError{
                code, generation_ordinal, std::move(context)}};
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max)() - left) return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool boundary_geometry_valid(
    const StockRuntimeGenerationBoundaryObservation& boundary) noexcept
{
    if (!boundary.observed || boundary.bit_offset >= 8U ||
        boundary.source_payload_byte_count == 0U ||
        boundary.source_payload_byte_count >
            (std::numeric_limits<std::size_t>::max)() / 8U) {
        return false;
    }
    const auto expected_bits = boundary.source_payload_byte_count * 8U;
    if (boundary.source_payload_bit_count != expected_bits ||
        boundary.byte_offset > boundary.source_payload_byte_count) {
        return false;
    }
    const auto cursor_bits = boundary.byte_offset * 8U + boundary.bit_offset;
    return cursor_bits <= boundary.source_payload_bit_count &&
           boundary.next_unconsumed_bit_count ==
               boundary.source_payload_bit_count - cursor_bits &&
           boundary.next_unconsumed_bit_count != 0U;
}

[[nodiscard]] bool candidate_geometry_valid(
    const StockRuntimeGenerationCandidateObservation& candidate) noexcept
{
    if (!candidate.observed || candidate.candidate_bit_width == 0U ||
        candidate.candidate_bit_width > 8U ||
        candidate.numeric_candidate.has_value() ==
            candidate.bounded_bit_prefix.has_value()) {
        return false;
    }
    if (candidate.byte_aligned) {
        return candidate.candidate_bit_width == 8U &&
               candidate.numeric_candidate.has_value();
    }
    if (!candidate.bounded_bit_prefix) {
        return false;
    }
    const auto maximum = static_cast<unsigned int>(
        (1U << candidate.candidate_bit_width) - 1U);
    return static_cast<unsigned int>(*candidate.bounded_bit_prefix) <= maximum;
}

[[nodiscard]] bool candidate_equal(
    const StockRuntimeGenerationCandidateObservation& left,
    const StockRuntimeGenerationCandidateObservation& right) noexcept
{
    return left.candidate_bit_width == right.candidate_bit_width &&
           left.numeric_candidate == right.numeric_candidate &&
           left.bounded_bit_prefix == right.bounded_bit_prefix &&
           left.byte_aligned == right.byte_aligned;
}

} // namespace

bool validate_stock_active_capture_preflight_attestation(
    const StockActiveCapturePreflightAttestation& attestation) noexcept
{
    const bool timestamp_valid =
        attestation.timestamp_category == "current-session" ||
        attestation.timestamp_category == "recent-bounded";
    const bool ipv6_valid = attestation.ipv6_capability_available ==
                            attestation.ipv6_loopback_allowed;
    return attestation.elevated && attestation.binary_profile_valid &&
           attestation.app_manifest_valid && attestation.dynamic_wfp_session &&
           attestation.ipv4_loopback_allowed && ipv6_valid &&
           attestation.non_loopback_denied_by_os &&
           attestation.isolation_cleanup_exact && timestamp_valid &&
           attestation.success;
}

bool StockRuntimeReconnectRelayTransition::prepare_generation_a_retirement(
    const bool generation_a_transport_complete,
    const bool held_datagrams_empty) noexcept
{
    if (generation_a_retirement_prepared_ ||
        generation_a_shutdown_confirmed_ || generation_b_relearn_ready_ ||
        generation_b_endpoint_learned_ || generation_b_connect_observed_ ||
        generation_b_accept_observed_ ||
        retired_generation_a_server_tail_packet_count_ != 0U ||
        !generation_a_transport_complete || !held_datagrams_empty) {
        return false;
    }
    generation_a_retirement_prepared_ = true;
    return true;
}

bool StockRuntimeReconnectRelayTransition::confirm_generation_a_shutdown(
    const bool controlled_shutdown_observed,
    const bool owned_client_process_absent) noexcept
{
    if (!generation_a_retirement_prepared_ ||
        generation_a_shutdown_confirmed_ || generation_b_relearn_ready_ ||
        generation_b_endpoint_learned_ || generation_b_connect_observed_ ||
        generation_b_accept_observed_ || !controlled_shutdown_observed ||
        !owned_client_process_absent) {
        return false;
    }
    generation_a_shutdown_confirmed_ = true;
    return true;
}

bool StockRuntimeReconnectRelayTransition::begin_generation_b_relearn(
    const bool generation_a_transport_complete,
    const bool generation_a_client_source_quiet,
    const bool held_datagrams_empty) noexcept
{
    if (generation_b_relearn_ready_ || generation_b_endpoint_learned_ ||
        generation_b_connect_observed_ || generation_b_accept_observed_ ||
        !generation_a_transport_complete ||
        !generation_a_client_source_quiet || !held_datagrams_empty) {
        return false;
    }
    if (!generation_a_retirement_prepared_ ||
        !generation_a_shutdown_confirmed_) {
        return false;
    }
    generation_b_relearn_ready_ = true;
    return true;
}

bool StockRuntimeReconnectRelayTransition::
observe_generation_b_client_datagram(
    const bool endpoint_source_valid,
    const bool connectionless,
    const bool connect_observed) noexcept
{
    if (!generation_b_relearn_ready_ || !endpoint_source_valid) return false;
    if (!generation_b_endpoint_learned_) {
        if (!connectionless) return false;
        generation_b_endpoint_learned_ = true;
    }
    // Until the fresh ACCEPT, sequenced bytes from either direction cannot be
    // attributed to B. The stock B handshake remains connectionless here.
    if (!generation_b_accept_observed_ && !connectionless) return false;
    if (connect_observed) {
        if (!connectionless || generation_b_accept_observed_) return false;
        generation_b_connect_observed_ = true;
    }
    return true;
}

StockRuntimeReconnectServerRoute
StockRuntimeReconnectRelayTransition::route_server_datagram(
    const bool exact_server_source,
    const bool connectionless,
    const bool accept_observed) noexcept
{
    if (!generation_a_shutdown_confirmed_ || !exact_server_source) {
        return StockRuntimeReconnectServerRoute::reject;
    }
    if (!generation_b_relearn_ready_) {
        if (!connectionless) {
            if (retired_generation_a_server_tail_packet_count_ ==
                (std::numeric_limits<std::size_t>::max)()) {
                return StockRuntimeReconnectServerRoute::reject;
            }
            ++retired_generation_a_server_tail_packet_count_;
        }
        return StockRuntimeReconnectServerRoute::retired_generation_a;
    }
    if (!generation_b_endpoint_learned_) {
        if (!connectionless) {
            if (retired_generation_a_server_tail_packet_count_ ==
                (std::numeric_limits<std::size_t>::max)()) {
                return StockRuntimeReconnectServerRoute::reject;
            }
            ++retired_generation_a_server_tail_packet_count_;
        }
        return StockRuntimeReconnectServerRoute::retired_generation_a;
    }
    if (connectionless) {
        if (accept_observed) {
            if (!generation_b_connect_observed_ ||
                generation_b_accept_observed_) {
                return StockRuntimeReconnectServerRoute::reject;
            }
            generation_b_accept_observed_ = true;
        }
        return StockRuntimeReconnectServerRoute::generation_b;
    }
    if (generation_b_accept_observed_) {
        return StockRuntimeReconnectServerRoute::generation_b;
    }
    if (retired_generation_a_server_tail_packet_count_ ==
        (std::numeric_limits<std::size_t>::max)()) {
        return StockRuntimeReconnectServerRoute::reject;
    }
    ++retired_generation_a_server_tail_packet_count_;
    return StockRuntimeReconnectServerRoute::retired_generation_a;
}

StockRuntimeReconnectLifecycleState::StockRuntimeReconnectLifecycleState(
    std::vector<StockRuntimeConnectionGenerationObservation> generations,
    const std::size_t client_to_server_packet_count,
    const std::size_t server_to_client_packet_count) noexcept
    : generations_{std::move(generations)},
      client_to_server_packet_count_{client_to_server_packet_count},
      server_to_client_packet_count_{server_to_client_packet_count}
{
}

const std::vector<StockRuntimeConnectionGenerationObservation>&
StockRuntimeReconnectLifecycleState::generations() const noexcept
{
    return generations_;
}

std::size_t
StockRuntimeReconnectLifecycleState::connection_generation_count() const noexcept
{
    return generations_.size();
}

std::size_t StockRuntimeReconnectLifecycleState::exact_boundary_count() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(
        generations_, [](const auto& generation) {
            return generation.exact_post_resource_boundary.observed;
        }));
}

std::size_t StockRuntimeReconnectLifecycleState::runtime_candidate_count() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(
        generations_, [](const auto& generation) {
            return generation.candidate_observation.observed;
        }));
}

std::size_t
StockRuntimeReconnectLifecycleState::client_to_server_packet_count() const noexcept
{
    return client_to_server_packet_count_;
}

std::size_t
StockRuntimeReconnectLifecycleState::server_to_client_packet_count() const noexcept
{
    return server_to_client_packet_count_;
}

bool StockRuntimeReconnectLifecycleState::generation_distinct() const noexcept
{
    return generations_.size() == 2U &&
           generations_[0].learned_client_endpoint_role_identity !=
               generations_[1].learned_client_endpoint_role_identity &&
           generations_[0].owned_client_process_role_identity !=
               generations_[1].owned_client_process_role_identity &&
           generations_[0].last_observed_ordinal <
               generations_[1].first_observed_ordinal;
}

struct StockRuntimeReconnectLifecycleStateFactory final {
    [[nodiscard]] static StockRuntimeReconnectLifecycleState create(
        std::vector<StockRuntimeConnectionGenerationObservation> generations,
        const std::size_t client_to_server_packet_count,
        const std::size_t server_to_client_packet_count) noexcept
    {
        return StockRuntimeReconnectLifecycleState{
            std::move(generations), client_to_server_packet_count,
            server_to_client_packet_count};
    }
};

StockRuntimeReconnectLifecycleValidationResult
validate_stock_runtime_reconnect_lifecycle(
    const StockRuntimeReconnectLifecycleInput& input)
{
    if (input.generations.size() != 2U) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::wrong_generation_count,
            0U, "A reconnect run requires exactly two generations");
    }

    std::size_t client_packets = 0U;
    std::size_t server_packets = 0U;
    const std::array<std::string_view, 2U> expected_endpoint_roles{
        kStockRuntimeGenerationAEndpointRole,
        kStockRuntimeGenerationBEndpointRole};
    const std::array<std::string_view, 2U> expected_process_roles{
        kStockRuntimeGenerationAProcessRole,
        kStockRuntimeGenerationBProcessRole};

    for (std::size_t index = 0U; index < input.generations.size(); ++index) {
        const auto& generation = input.generations[index];
        const auto ordinal = index + 1U;
        if (generation.generation_ordinal != ordinal) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::invalid_generation_ordinal,
                generation.generation_ordinal,
                "Generation ordinals must be exactly one then two");
        }
        if (generation.learned_client_endpoint_role_identity !=
                expected_endpoint_roles[index] ||
            generation.owned_client_process_role_identity !=
                expected_process_roles[index]) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::invalid_role_identity,
                ordinal, "Generation role identities are not exact sanitized tokens");
        }
        if (!safe_identity_token(
                generation.profile_identity, kMaximumProfileIdentityBytes)) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::invalid_profile_identity,
                ordinal, "Profile identity is absent or unsafe");
        }
        if (index != 0U && generation.profile_identity !=
                input.generations[0].profile_identity) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::profile_identity_mismatch,
                ordinal, "Both generations must use one exact stock profile");
        }
        if (!generation.owned_client_process_observed) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::owned_client_process_not_observed,
                ordinal, "Owned client process identity was not observed");
        }
        if (!generation.fresh_owned_client_process) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::fresh_client_process_not_proven,
                ordinal, "A fresh owned client process was not proven");
        }
        if (!generation.learned_client_endpoint_observed) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::learned_client_endpoint_not_observed,
                ordinal, "A learned client endpoint was not observed");
        }
        if (index != 0U &&
            !generation.learned_client_endpoint_distinct_from_previous) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::endpoint_generation_not_proven,
                ordinal, "Generation B did not prove a distinct learned endpoint");
        }
        if (generation.first_observed_ordinal >
                generation.last_observed_ordinal ||
            generation.connectionless_exchange_count >
                kMaximumConnectionlessExchanges) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::invalid_observed_ordinal_range,
                ordinal, "Generation journal range is invalid");
        }
        if (generation.connectionless_exchange_count == 0U) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::missing_connectionless_exchange,
                ordinal, "Generation has no connectionless exchange");
        }
        if (!generation.connect_observed) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::missing_connect,
                ordinal, "Generation has no exact connect request observation");
        }
        if (!generation.accept_observed) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::missing_accept,
                ordinal, "Generation has no ACCEPT observation");
        }
        if (!generation.first_sequenced_packet_ordinal) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::missing_first_sequenced_packet,
                ordinal, "Generation has no first sequenced packet");
        }
        if (*generation.first_sequenced_packet_ordinal <
                generation.first_observed_ordinal ||
            *generation.first_sequenced_packet_ordinal >
                generation.last_observed_ordinal ||
            generation.client_to_server_packet_count == 0U ||
            generation.server_to_client_packet_count == 0U ||
            generation.client_to_server_packet_count > kMaximumGenerationPackets ||
            generation.server_to_client_packet_count > kMaximumGenerationPackets ||
            !checked_add(client_packets,
                         generation.client_to_server_packet_count,
                         client_packets) ||
            !checked_add(server_packets,
                         generation.server_to_client_packet_count,
                         server_packets)) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::invalid_packet_counters,
                ordinal, "Generation packet counts or first sequence are invalid");
        }
        if (!generation.exact_post_resource_boundary.observed) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::missing_post_resource_boundary,
                ordinal, "Generation lacks an exact post-resource boundary");
        }
        if (!boundary_geometry_valid(generation.exact_post_resource_boundary) ||
            generation.exact_post_resource_boundary.corpus_observed_ordinal <
                generation.first_observed_ordinal ||
            generation.exact_post_resource_boundary.corpus_observed_ordinal >
                generation.last_observed_ordinal) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::invalid_boundary_geometry,
                ordinal, "Generation boundary geometry is inconsistent");
        }
        if (!generation.candidate_observation.observed) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::missing_candidate,
                ordinal, "Generation lacks a neutral first candidate");
        }
        if (generation.candidate_observation.body_consumed) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::candidate_body_consumed,
                ordinal, "Candidate body consumption is forbidden");
        }
        if (generation.candidate_observation.semantic_category_assigned) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::candidate_semantic_name_assigned,
                ordinal, "Semantic candidate naming is forbidden");
        }
        if (!candidate_geometry_valid(generation.candidate_observation)) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::invalid_candidate_geometry,
                ordinal, "Neutral candidate geometry is invalid");
        }
        const bool boundary_byte_aligned =
            generation.exact_post_resource_boundary.bit_offset == 0U;
        if (generation.candidate_observation.byte_aligned !=
                boundary_byte_aligned ||
            generation.candidate_observation.candidate_bit_width >
                generation.exact_post_resource_boundary.
                    next_unconsumed_bit_count) {
            return failure(
                StockRuntimeReconnectLifecycleErrorCode::
                    invalid_candidate_geometry,
                ordinal,
                "Candidate alignment is not bound to its exact boundary");
        }
    }

    const auto& generation_a = input.generations[0];
    const auto& generation_b = input.generations[1];
    if (!generation_a.controlled_client_shutdown_observed) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::generation_a_shutdown_not_proven,
            1U, "Generation A controlled shutdown was not proven");
    }
    if (!generation_a.retired_client_endpoint_quiet) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::generation_a_endpoint_not_retired,
            1U, "Generation A endpoint was not quiet before relearn");
    }
    if (generation_a.last_observed_ordinal >=
            generation_b.first_observed_ordinal ||
        generation_a.learned_client_endpoint_role_identity ==
            generation_b.learned_client_endpoint_role_identity ||
        generation_a.owned_client_process_role_identity ==
            generation_b.owned_client_process_role_identity) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::generation_order_invalid,
            2U, "Generation ranges or identities overlap");
    }
    if (generation_a.exact_post_resource_boundary.bit_offset !=
            generation_b.exact_post_resource_boundary.bit_offset ||
        !candidate_equal(
            generation_a.candidate_observation,
            generation_b.candidate_observation)) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::candidate_conflicting,
            2U, "Generation A and B candidates conflict");
    }
    if (!input.guard_continuous_between_generations) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::guard_lost_between_generations,
            2U, "Isolation guard was not continuous");
    }
    if (!input.server_continuous_between_generations) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::server_exited_between_generations,
            2U, "HLDS was not continuous");
    }
    if (!input.relay_continuous_between_generations) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::relay_exited_between_generations,
            2U, "Relay was not continuous");
    }
    if (!input.cleanup_exact) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::cleanup_inexact,
            0U, "Owned process cleanup is not exact");
    }
    if (!input.restoration_exact) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::restoration_inexact,
            0U, "Research-tree restoration is not exact");
    }
    if (!input.transactional_publication_ready) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::transactional_publication_not_ready,
            0U, "Reconnect publication transaction is incomplete");
    }

    try {
        std::vector<StockRuntimeConnectionGenerationObservation> generations{
            input.generations.begin(), input.generations.end()};
        return {StockRuntimeReconnectLifecycleStateFactory::create(
                    std::move(generations), client_packets, server_packets),
                std::nullopt};
    } catch (...) {
        return failure(
            StockRuntimeReconnectLifecycleErrorCode::allocation_failed,
            0U, "Reconnect lifecycle state allocation failed");
    }
}

} // namespace hlclient::goldsrc

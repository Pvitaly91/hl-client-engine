#include <hlclient/goldsrc/stock_runtime_campaign.hpp>

#include <hlclient/hash/sha256.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::array kCampaignMatrix{
    StockRuntimeCampaignMatrixEntry{"boot_camp", "baseline", 6U},
    StockRuntimeCampaignMatrixEntry{"crossfire", "baseline", 4U},
    StockRuntimeCampaignMatrixEntry{"stalkyard", "baseline", 4U},
    StockRuntimeCampaignMatrixEntry{"crossfire", "idle-runtime", 4U},
    StockRuntimeCampaignMatrixEntry{
        "boot_camp", "drop-server-to-client-transport-ordinal", 2U},
    StockRuntimeCampaignMatrixEntry{
        "crossfire", "duplicate-server-to-client-transport-ordinal", 1U},
    StockRuntimeCampaignMatrixEntry{
        "stalkyard", "reorder-server-to-client-transport-ordinal", 1U},
    StockRuntimeCampaignMatrixEntry{"boot_camp", "reconnect", 2U},
};

[[nodiscard]] constexpr std::size_t required_run_count() noexcept
{
    std::size_t result = 0U;
    for (const auto& entry : kCampaignMatrix) result += entry.required_runs;
    return result;
}

[[nodiscard]] StockRuntimeCampaignBuildResult failure(
    const StockRuntimeCampaignErrorCode code,
    const std::size_t ordinal,
    std::string context)
{
    return {std::nullopt, StockRuntimeCampaignError{
        code, ordinal, std::move(context)}};
}

[[nodiscard]] bool valid_lower_hex(
    const std::string_view value,
    const std::size_t length) noexcept
{
    return value.size() == length &&
           std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool valid_nonzero_sha256(
    const std::string_view value) noexcept
{
    return valid_lower_hex(value, 64U) &&
           std::ranges::any_of(value, [](const char character) {
               return character != '0';
           });
}

[[nodiscard]] bool valid_profile_identity(
    const std::string_view value,
    const std::size_t maximum_bytes) noexcept
{
    return !value.empty() && value.size() <= maximum_bytes &&
           std::ranges::all_of(value, [](const char character) {
               const auto byte = static_cast<unsigned char>(character);
               return byte >= 0x21U && byte <= 0x7eU &&
                      character != '\\' && character != '"';
           });
}

[[nodiscard]] std::optional<std::size_t> matrix_index(
    const std::string_view map_category,
    const std::string_view scenario) noexcept
{
    for (std::size_t index = 0U; index < kCampaignMatrix.size(); ++index) {
        if (kCampaignMatrix[index].map_category == map_category &&
            kCampaignMatrix[index].scenario == scenario) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool add_bounded(
    std::size_t& destination,
    const std::size_t value) noexcept
{
    if (destination > (std::numeric_limits<std::size_t>::max)() - value) {
        return false;
    }
    destination += value;
    return true;
}

[[nodiscard]] bool valid_candidate(
    const StockRuntimeCampaignCandidateObservation& value) noexcept
{
    if (value.bit_offset > 7U || value.bit_width == 0U ||
        value.bit_width > 8U ||
        value.numeric_candidate.has_value() ==
            value.bounded_bit_prefix.has_value()) {
        return false;
    }
    if (value.byte_aligned) {
        return value.bit_offset == 0U && value.bit_width == 8U &&
               value.numeric_candidate.has_value();
    }
    if (value.bit_offset == 0U || !value.bounded_bit_prefix) {
        return false;
    }
    const auto maximum = static_cast<unsigned int>(1U << value.bit_width);
    return static_cast<unsigned int>(*value.bounded_bit_prefix) < maximum;
}

[[nodiscard]] bool same_candidate(
    const StockRuntimeCampaignCandidateObservation& left,
    const StockRuntimeCampaignCandidateObservation& right) noexcept
{
    return left.bit_offset == right.bit_offset &&
           left.bit_width == right.bit_width &&
           left.byte_aligned == right.byte_aligned &&
           left.numeric_candidate == right.numeric_candidate &&
           left.bounded_bit_prefix == right.bounded_bit_prefix;
}

[[nodiscard]] bool individual_acceptance_gates(
    const StockRuntimeCampaignRunObservation& value,
    const std::size_t minimum_s2c) noexcept
{
    const bool reconnect = value.scenario == "reconnect";
    const auto expected_generations = reconnect ? std::size_t{2U}
                                                : std::size_t{1U};
    if (!value.isolation_verified || !value.profile_verified ||
        !value.client_ready || !value.bounded_transport_complete ||
        value.wrong_source_datagrams != 0U || !value.restoration_exact ||
        !value.external_drift_none || !value.corpus_valid ||
        !value.independent_walker_valid || !value.signon_replay_complete ||
        !value.candidate_body_unconsumed ||
        !valid_nonzero_sha256(value.transport_structural_sha256) ||
        !valid_nonzero_sha256(value.replay_structural_sha256) ||
        value.sequenced_server_to_client < minimum_s2c ||
        value.connection_generation_count != expected_generations ||
        value.exact_post_resource_boundary_count != expected_generations ||
        value.candidates.size() != expected_generations ||
        (reconnect && (!value.reconnect_generations_distinct ||
                       value.reconnect_candidate_conflict))) {
        return false;
    }
    if (!std::ranges::all_of(value.candidates, valid_candidate)) return false;
    if (reconnect &&
        !same_candidate(value.candidates.front(), value.candidates.back())) {
        return false;
    }
    return true;
}

[[nodiscard]] std::string candidate_token(
    const StockRuntimeCampaignCandidateObservation& value)
{
    std::string result;
    result.reserve(48U);
    result.append(std::to_string(value.bit_offset));
    result.push_back(':');
    result.append(std::to_string(value.bit_width));
    result.push_back(':');
    result.append(value.byte_aligned ? "aligned:" : "prefix:");
    if (value.numeric_candidate) {
        result.append(std::to_string(
            static_cast<unsigned int>(*value.numeric_candidate)));
        if (value.bounded_bit_prefix) {
            result.append(":also-prefix:");
            result.append(std::to_string(
                static_cast<unsigned int>(*value.bounded_bit_prefix)));
        }
    } else if (value.bounded_bit_prefix) {
        result.append(std::to_string(
            static_cast<unsigned int>(*value.bounded_bit_prefix)));
    } else {
        result.append("missing");
    }
    return result;
}

void append_canonical_string(
    std::string& output,
    const std::string_view name,
    const std::string_view value)
{
    output.push_back('|');
    output.append(name);
    output.push_back('=');
    output.append(std::to_string(value.size()));
    output.push_back(':');
    output.append(value);
}

void append_canonical_number(
    std::string& output,
    const std::string_view name,
    const std::size_t value)
{
    append_canonical_string(output, name, std::to_string(value));
}

void append_canonical_boolean(
    std::string& output,
    const std::string_view name,
    const bool value)
{
    append_canonical_string(output, name, value ? "true" : "false");
}

[[nodiscard]] constexpr std::string_view publication_token(
    const StockRuntimeCampaignPublicationState state) noexcept
{
    switch (state) {
    case StockRuntimeCampaignPublicationState::accepted: return "accepted";
    case StockRuntimeCampaignPublicationState::rejected: return "rejected";
    case StockRuntimeCampaignPublicationState::incomplete: return "incomplete";
    }
    return "unknown";
}

void append_run_structure(
    std::string& output,
    const StockRuntimeCampaignRunObservation& observation)
{
    append_canonical_string(output, "run-id", observation.run_id);
    append_canonical_string(output, "map", observation.map_category);
    append_canonical_string(output, "scenario", observation.scenario);
    append_canonical_string(output, "profile", observation.profile_identity);
    append_canonical_string(
        output, "transport-structural-sha256",
        observation.transport_structural_sha256);
    append_canonical_string(
        output, "replay-structural-sha256",
        observation.replay_structural_sha256);
    append_canonical_string(
        output, "publication", publication_token(observation.publication));
    append_canonical_boolean(
        output, "isolation-verified", observation.isolation_verified);
    append_canonical_boolean(
        output, "profile-verified", observation.profile_verified);
    append_canonical_boolean(output, "client-ready", observation.client_ready);
    append_canonical_boolean(
        output, "bounded-transport-complete",
        observation.bounded_transport_complete);
    append_canonical_number(
        output, "wrong-source-datagrams",
        observation.wrong_source_datagrams);
    append_canonical_boolean(
        output, "restoration-exact", observation.restoration_exact);
    append_canonical_boolean(
        output, "external-drift-none", observation.external_drift_none);
    append_canonical_boolean(output, "corpus-valid", observation.corpus_valid);
    append_canonical_boolean(
        output, "independent-walker-valid",
        observation.independent_walker_valid);
    append_canonical_boolean(
        output, "signon-replay-complete",
        observation.signon_replay_complete);
    append_canonical_boolean(
        output, "candidate-body-unconsumed",
        observation.candidate_body_unconsumed);
    append_canonical_number(
        output, "sequenced-c2s", observation.sequenced_client_to_server);
    append_canonical_number(
        output, "sequenced-s2c", observation.sequenced_server_to_client);
    append_canonical_number(
        output, "reassembled", observation.reassembled_payloads);
    append_canonical_number(
        output, "decompressed", observation.decompressed_payloads);
    append_canonical_number(
        output, "connection-generations",
        observation.connection_generation_count);
    append_canonical_number(
        output, "exact-boundaries",
        observation.exact_post_resource_boundary_count);
    append_canonical_boolean(
        output, "reconnect-generations-distinct",
        observation.reconnect_generations_distinct);
    append_canonical_boolean(
        output, "reconnect-candidate-conflict",
        observation.reconnect_candidate_conflict);
    append_canonical_number(
        output, "candidate-count", observation.candidates.size());
    for (std::size_t index = 0U; index < observation.candidates.size(); ++index) {
        append_canonical_string(
            output, "candidate-" + std::to_string(index),
            candidate_token(observation.candidates[index]));
    }
}

void append_json_string(std::ostringstream& output, const std::string_view value)
{
    output << '"';
    for (const char character : value) {
        if (character == '"' || character == '\\') output << '\\';
        output << character;
    }
    output << '"';
}

} // namespace

std::span<const StockRuntimeCampaignMatrixEntry>
stock_runtime_first_campaign_matrix() noexcept
{
    return kCampaignMatrix;
}

StockRuntimeCampaignState::StockRuntimeCampaignState(
    std::string implementation_commit,
    std::string profile_identity,
    const std::size_t attempted_runs,
    const std::size_t accepted_runs,
    const std::size_t rejected_runs,
    const std::size_t incomplete_runs,
    const std::size_t pending_runs,
    const std::size_t sequenced_client_to_server,
    const std::size_t sequenced_server_to_client,
    const std::size_t reassembled_payloads,
    const std::size_t decompressed_payloads,
    const std::size_t exact_post_resource_boundaries,
    const std::size_t candidate_observations,
    const std::size_t reconnect_generations,
    std::vector<std::size_t> matrix_accepted_counts,
    const StockRuntimeCampaignCandidateStability candidate_stability,
    const StockRuntimeCampaignThresholdStatus threshold_status,
    std::vector<StockRuntimeCampaignPendingSlot> pending_slots,
    std::vector<std::string> accepted_run_ids,
    std::string structural_sha256) noexcept
    : implementation_commit_{std::move(implementation_commit)},
      profile_identity_{std::move(profile_identity)},
      attempted_runs_{attempted_runs},
      accepted_runs_{accepted_runs},
      rejected_runs_{rejected_runs},
      incomplete_runs_{incomplete_runs},
      pending_runs_{pending_runs},
      sequenced_client_to_server_{sequenced_client_to_server},
      sequenced_server_to_client_{sequenced_server_to_client},
      reassembled_payloads_{reassembled_payloads},
      decompressed_payloads_{decompressed_payloads},
      exact_post_resource_boundaries_{exact_post_resource_boundaries},
      candidate_observations_{candidate_observations},
      reconnect_generations_{reconnect_generations},
      matrix_accepted_counts_{std::move(matrix_accepted_counts)},
      candidate_stability_{candidate_stability},
      threshold_status_{threshold_status},
      pending_slots_{std::move(pending_slots)},
      accepted_run_ids_{std::move(accepted_run_ids)},
      structural_sha256_{std::move(structural_sha256)}
{
}

std::string_view StockRuntimeCampaignState::implementation_commit() const noexcept
{
    return implementation_commit_;
}
std::string_view StockRuntimeCampaignState::profile_identity() const noexcept
{
    return profile_identity_;
}
std::size_t StockRuntimeCampaignState::attempted_runs() const noexcept
{
    return attempted_runs_;
}
std::size_t StockRuntimeCampaignState::accepted_runs() const noexcept
{
    return accepted_runs_;
}
std::size_t StockRuntimeCampaignState::rejected_runs() const noexcept
{
    return rejected_runs_;
}
std::size_t StockRuntimeCampaignState::incomplete_runs() const noexcept
{
    return incomplete_runs_;
}
std::size_t StockRuntimeCampaignState::pending_runs() const noexcept
{
    return pending_runs_;
}
std::size_t StockRuntimeCampaignState::sequenced_client_to_server() const noexcept
{
    return sequenced_client_to_server_;
}
std::size_t StockRuntimeCampaignState::sequenced_server_to_client() const noexcept
{
    return sequenced_server_to_client_;
}
std::size_t StockRuntimeCampaignState::reassembled_payloads() const noexcept
{
    return reassembled_payloads_;
}
std::size_t StockRuntimeCampaignState::decompressed_payloads() const noexcept
{
    return decompressed_payloads_;
}
std::size_t StockRuntimeCampaignState::exact_post_resource_boundaries() const noexcept
{
    return exact_post_resource_boundaries_;
}
std::size_t StockRuntimeCampaignState::candidate_observations() const noexcept
{
    return candidate_observations_;
}
std::size_t StockRuntimeCampaignState::reconnect_generations() const noexcept
{
    return reconnect_generations_;
}
std::size_t StockRuntimeCampaignState::accepted_runs_for(
    const std::string_view map_category,
    const std::string_view scenario) const noexcept
{
    const auto index = matrix_index(map_category, scenario);
    return index && *index < matrix_accepted_counts_.size()
        ? matrix_accepted_counts_[*index]
        : 0U;
}
StockRuntimeCampaignCandidateStability
StockRuntimeCampaignState::candidate_stability() const noexcept
{
    return candidate_stability_;
}
StockRuntimeCampaignThresholdStatus
StockRuntimeCampaignState::threshold_status() const noexcept
{
    return threshold_status_;
}
const std::vector<StockRuntimeCampaignPendingSlot>&
StockRuntimeCampaignState::pending_slots() const noexcept
{
    return pending_slots_;
}
const std::vector<std::string>&
StockRuntimeCampaignState::accepted_run_ids() const noexcept
{
    return accepted_run_ids_;
}
std::string_view StockRuntimeCampaignState::structural_sha256() const noexcept
{
    return structural_sha256_;
}

StockRuntimeCampaignAggregator::StockRuntimeCampaignAggregator(
    StockRuntimeCampaignLimits limits) noexcept
    : limits_{std::move(limits)}
{
}

bool StockRuntimeCampaignAggregator::valid_configuration() const noexcept
{
    return limits_.maximum_attempted_runs >= required_run_count() &&
           limits_.maximum_attempted_runs <= 65'536U &&
           limits_.minimum_accepted_runs == required_run_count() &&
           limits_.minimum_sequenced_server_packets >= 1'000U &&
           limits_.minimum_reconnect_generations == 4U &&
           limits_.minimum_exact_boundaries == 26U &&
           limits_.minimum_candidate_observations == 26U &&
           limits_.minimum_sequenced_server_packets_per_run >= 100U &&
           limits_.maximum_profile_identity_bytes > 0U &&
           limits_.maximum_profile_identity_bytes <= 4'096U &&
           (limits_.required_profile_identity.empty() ||
            valid_profile_identity(
                limits_.required_profile_identity,
                limits_.maximum_profile_identity_bytes));
}

const StockRuntimeCampaignLimits&
StockRuntimeCampaignAggregator::limits() const noexcept
{
    return limits_;
}

StockRuntimeCampaignBuildResult StockRuntimeCampaignAggregator::build(
    const std::span<const StockRuntimeCampaignRunObservation> observations,
    const std::string_view implementation_commit) const
{
    if (!valid_configuration()) {
        return failure(
            StockRuntimeCampaignErrorCode::invalid_configuration, 0U,
            "campaign limits do not encode the exact 24/4/1000/26 threshold");
    }
    if (!valid_lower_hex(implementation_commit, 40U)) {
        return failure(
            StockRuntimeCampaignErrorCode::invalid_implementation_commit, 0U,
            "implementation commit is not 40 lowercase hexadecimal characters");
    }
    if (observations.size() > limits_.maximum_attempted_runs) {
        return failure(
            StockRuntimeCampaignErrorCode::run_limit_exceeded,
            observations.size(), "campaign run count exceeds its bound");
    }

    try {
        std::vector<const StockRuntimeCampaignRunObservation*> ordered;
        ordered.reserve(observations.size());
        for (const auto& observation : observations) ordered.push_back(&observation);
        std::ranges::sort(ordered, {}, [](const auto* value) {
            return value->run_id;
        });

        std::unordered_set<std::string_view> run_ids;
        run_ids.reserve(observations.size());
        for (std::size_t index = 0U; index < ordered.size(); ++index) {
            if (!valid_lower_hex(ordered[index]->run_id, 32U)) {
                return failure(
                    StockRuntimeCampaignErrorCode::invalid_run_id, index,
                    "campaign contains a non-canonical run id");
            }
            if (!run_ids.insert(ordered[index]->run_id).second) {
                return failure(
                    StockRuntimeCampaignErrorCode::duplicate_run_id, index,
                    "campaign contains the same run id more than once");
            }
        }

        std::vector<std::size_t> accepted_counts(kCampaignMatrix.size(), 0U);
        std::vector<std::string> accepted_run_ids;
        accepted_run_ids.reserve(required_run_count());
        std::size_t accepted = 0U;
        std::size_t rejected = 0U;
        std::size_t incomplete = 0U;
        std::size_t sequenced_c2s = 0U;
        std::size_t sequenced_s2c = 0U;
        std::size_t reassembled = 0U;
        std::size_t decompressed = 0U;
        std::size_t boundaries = 0U;
        std::size_t candidates = 0U;
        std::size_t reconnect_generations = 0U;
        std::optional<StockRuntimeCampaignCandidateObservation> baseline_candidate;
        bool candidate_conflicting = false;
        std::string selected_profile = limits_.required_profile_identity;

        std::string canonical{"hlclient.stock-runtime-first-campaign-structure.v1"};
        append_canonical_string(
            canonical, "implementation", implementation_commit);
        append_canonical_number(
            canonical, "matrix-entry-count", kCampaignMatrix.size());
        for (std::size_t index = 0U; index < kCampaignMatrix.size(); ++index) {
            append_canonical_number(canonical, "matrix-index", index);
            append_canonical_string(
                canonical, "matrix-map", kCampaignMatrix[index].map_category);
            append_canonical_string(
                canonical, "matrix-scenario", kCampaignMatrix[index].scenario);
            append_canonical_number(
                canonical, "matrix-required",
                kCampaignMatrix[index].required_runs);
        }
        append_canonical_number(
            canonical, "limit-maximum-attempted",
            limits_.maximum_attempted_runs);
        append_canonical_number(
            canonical, "limit-minimum-accepted",
            limits_.minimum_accepted_runs);
        append_canonical_number(
            canonical, "limit-minimum-s2c",
            limits_.minimum_sequenced_server_packets);
        append_canonical_number(
            canonical, "limit-minimum-reconnect-generations",
            limits_.minimum_reconnect_generations);
        append_canonical_number(
            canonical, "limit-minimum-boundaries",
            limits_.minimum_exact_boundaries);
        append_canonical_number(
            canonical, "limit-minimum-candidates",
            limits_.minimum_candidate_observations);
        append_canonical_number(
            canonical, "limit-minimum-s2c-per-run",
            limits_.minimum_sequenced_server_packets_per_run);
        append_canonical_number(
            canonical, "limit-maximum-profile-bytes",
            limits_.maximum_profile_identity_bytes);
        append_canonical_string(
            canonical, "required-profile", limits_.required_profile_identity);

        for (const auto* observation : ordered) {
            append_run_structure(canonical, *observation);

            if (observation->publication ==
                StockRuntimeCampaignPublicationState::incomplete) {
                ++incomplete;
                append_canonical_string(
                    canonical, "aggregation-result", "incomplete");
                continue;
            }
            if (observation->publication ==
                StockRuntimeCampaignPublicationState::rejected) {
                ++rejected;
                append_canonical_string(
                    canonical, "aggregation-result", "rejected");
                continue;
            }

            const auto slot = matrix_index(
                observation->map_category, observation->scenario);
            const bool profile_shape_valid = valid_profile_identity(
                observation->profile_identity,
                limits_.maximum_profile_identity_bytes);
            const bool profile_matches = profile_shape_valid &&
                (selected_profile.empty() ||
                 observation->profile_identity == selected_profile);
            if (!slot || accepted_counts[*slot] >=
                             kCampaignMatrix[*slot].required_runs ||
                !profile_matches ||
                !individual_acceptance_gates(
                    *observation,
                    limits_.minimum_sequenced_server_packets_per_run)) {
                ++rejected;
                append_canonical_string(
                    canonical, "aggregation-result",
                    "rejected-by-aggregation");
                continue;
            }
            if (selected_profile.empty()) selected_profile = observation->profile_identity;

            bool overflow = false;
            overflow = !add_bounded(sequenced_c2s,
                                    observation->sequenced_client_to_server) || overflow;
            overflow = !add_bounded(sequenced_s2c,
                                    observation->sequenced_server_to_client) || overflow;
            overflow = !add_bounded(reassembled,
                                    observation->reassembled_payloads) || overflow;
            overflow = !add_bounded(decompressed,
                                    observation->decompressed_payloads) || overflow;
            overflow = !add_bounded(boundaries,
                                    observation->exact_post_resource_boundary_count) || overflow;
            overflow = !add_bounded(candidates,
                                    observation->candidates.size()) || overflow;
            if (observation->scenario == "reconnect") {
                overflow = !add_bounded(
                    reconnect_generations,
                    observation->connection_generation_count) || overflow;
            }
            if (overflow) {
                return failure(
                    StockRuntimeCampaignErrorCode::counter_overflow, accepted,
                    "accepted campaign counters overflowed");
            }

            for (const auto& candidate : observation->candidates) {
                if (!baseline_candidate) baseline_candidate = candidate;
                else if (!same_candidate(*baseline_candidate, candidate)) {
                    candidate_conflicting = true;
                }
            }
            ++accepted_counts[*slot];
            ++accepted;
            accepted_run_ids.push_back(observation->run_id);
            append_canonical_string(
                canonical, "aggregation-result", "accepted");
        }

        std::vector<StockRuntimeCampaignPendingSlot> pending_slots;
        for (std::size_t index = 0U; index < kCampaignMatrix.size(); ++index) {
            for (std::size_t ordinal = accepted_counts[index];
                 ordinal < kCampaignMatrix[index].required_runs; ++ordinal) {
                pending_slots.push_back({
                    std::string{kCampaignMatrix[index].map_category},
                    std::string{kCampaignMatrix[index].scenario},
                    ordinal});
            }
        }
        const auto stability = candidate_conflicting
            ? StockRuntimeCampaignCandidateStability::candidate_conflicting
            : candidates >= 2U
                ? StockRuntimeCampaignCandidateStability::stable_observation
                : StockRuntimeCampaignCandidateStability::evidence_pending;
        const bool matrix_complete = pending_slots.empty();
        const bool threshold_passed =
            accepted >= limits_.minimum_accepted_runs && matrix_complete &&
            sequenced_s2c >= limits_.minimum_sequenced_server_packets &&
            reconnect_generations >= limits_.minimum_reconnect_generations &&
            boundaries >= limits_.minimum_exact_boundaries &&
            candidates >= limits_.minimum_candidate_observations &&
            stability == StockRuntimeCampaignCandidateStability::stable_observation;
        const auto threshold = candidate_conflicting
            ? StockRuntimeCampaignThresholdStatus::conflicting
            : threshold_passed ? StockRuntimeCampaignThresholdStatus::passed
                               : StockRuntimeCampaignThresholdStatus::pending;

        append_canonical_string(canonical, "selected-profile", selected_profile);
        append_canonical_number(canonical, "attempted", observations.size());
        append_canonical_number(canonical, "accepted", accepted);
        append_canonical_number(canonical, "rejected", rejected);
        append_canonical_number(canonical, "incomplete", incomplete);
        append_canonical_number(
            canonical, "pending", pending_slots.size());
        append_canonical_number(canonical, "sequenced-c2s-total", sequenced_c2s);
        append_canonical_number(canonical, "sequenced-s2c-total", sequenced_s2c);
        append_canonical_number(canonical, "reassembled-total", reassembled);
        append_canonical_number(canonical, "decompressed-total", decompressed);
        append_canonical_number(canonical, "boundaries-total", boundaries);
        append_canonical_number(canonical, "candidates-total", candidates);
        append_canonical_number(
            canonical, "reconnect-generations-total", reconnect_generations);
        for (std::size_t index = 0U; index < accepted_counts.size(); ++index) {
            append_canonical_number(
                canonical, "matrix-accepted-" + std::to_string(index),
                accepted_counts[index]);
        }
        append_canonical_string(
            canonical, "candidate-stability", to_string(stability));
        append_canonical_string(
            canonical, "threshold-status", to_string(threshold));
        const auto bytes = std::as_bytes(
            std::span{canonical.data(), canonical.size()});
        const auto digest = hash::sha256(bytes);
        if (!digest) {
            return failure(
                StockRuntimeCampaignErrorCode::structural_hash_failed, 0U,
                "campaign structural hash could not be computed");
        }

        return StockRuntimeCampaignBuildResult{
            StockRuntimeCampaignState{
                std::string{implementation_commit}, std::move(selected_profile),
                observations.size(), accepted, rejected, incomplete,
                pending_slots.size(), sequenced_c2s, sequenced_s2c,
                reassembled, decompressed, boundaries, candidates,
                reconnect_generations, std::move(accepted_counts), stability,
                threshold, std::move(pending_slots),
                std::move(accepted_run_ids), hash::sha256_hex(*digest)},
            std::nullopt};
    } catch (...) {
        return failure(
            StockRuntimeCampaignErrorCode::allocation_failed, 0U,
            "campaign aggregation failed transactionally");
    }
}

std::string serialize_stock_runtime_first_campaign_manifest(
    const StockRuntimeCampaignState& state)
{
    std::ostringstream output;
    output << "{\n  \"schema\": \"" << kStockRuntimeFirstCampaignSchema
           << "\",\n  \"implementation_commit\": ";
    append_json_string(output, state.implementation_commit());
    output << ",\n  \"profile_fingerprint\": ";
    append_json_string(
        output, state.accepted_runs() == 0U
            ? std::string_view{"evidence_pending"}
            : state.profile_identity());
    output << ",\n  \"required_matrix\": [";
    for (std::size_t index = 0U; index < kCampaignMatrix.size(); ++index) {
        if (index != 0U) output << ',';
        output << "\n    {\"map_category\": ";
        append_json_string(output, kCampaignMatrix[index].map_category);
        output << ", \"scenario\": ";
        append_json_string(output, kCampaignMatrix[index].scenario);
        output << ", \"required_runs\": "
               << kCampaignMatrix[index].required_runs
               << ", \"accepted_runs\": "
               << state.accepted_runs_for(
                      kCampaignMatrix[index].map_category,
                      kCampaignMatrix[index].scenario)
               << '}';
    }
    output << "\n  ],\n  \"attempted_slots\": " << state.attempted_runs()
           << ",\n  \"accepted_slots\": " << state.accepted_runs()
           << ",\n  \"rejected_slots\": " << state.rejected_runs()
           << ",\n  \"incomplete_slots\": " << state.incomplete_runs()
           << ",\n  \"pending_slots\": " << state.pending_runs()
           << ",\n  \"packet_totals\": {\"sequenced_c2s\": "
           << state.sequenced_client_to_server()
           << ", \"sequenced_s2c\": "
           << state.sequenced_server_to_client()
           << ", \"reassembled\": " << state.reassembled_payloads()
           << ", \"decompressed\": " << state.decompressed_payloads()
           << "},\n  \"boundary_totals\": {\"exact\": "
           << state.exact_post_resource_boundaries()
           << ", \"candidates\": " << state.candidate_observations()
           << ", \"reconnect_generations\": "
           << state.reconnect_generations()
           << "},\n  \"candidate_stability\": \""
           << to_string(state.candidate_stability())
           << "\",\n  \"threshold_status\": \""
           << to_string(state.threshold_status())
           << "\",\n  \"campaign_structural_sha256\": \""
           << state.structural_sha256() << "\"\n}\n";
    return output.str();
}

} // namespace hlclient::goldsrc

#include <hlclient/goldsrc/stock_runtime_first_observation.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] StockRuntimeFirstObservationBuildResult failure(
    const StockRuntimeFirstObservationErrorCode code,
    const std::size_t run_ordinal,
    std::string context)
{
    return StockRuntimeFirstObservationBuildResult{
        std::nullopt,
        StockRuntimeFirstObservationError{
            code, run_ordinal, std::move(context)},
    };
}

[[nodiscard]] bool valid_run_id(const std::string_view run_id) noexcept
{
    return run_id.size() == 32U &&
           std::ranges::all_of(run_id, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool valid_version_profile(
    const std::string_view profile,
    const std::size_t maximum_size) noexcept
{
    if (profile.empty() || profile.size() > maximum_size) {
        return false;
    }
    return std::ranges::all_of(profile, [](const char character) {
        const auto value = static_cast<unsigned char>(character);
        return value >= 0x21U && value <= 0x7eU && character != '\\';
    });
}

[[nodiscard]] bool valid_cursor_geometry(
    const StockRuntimeFirstObservationInput& input,
    const std::size_t maximum_payload_bytes,
    std::size_t& absolute_bit_offset,
    std::size_t& remaining_bits) noexcept
{
    const auto& cursor = input.cursor;
    if (input.source_payload.size() > maximum_payload_bytes ||
        input.source_payload.size() != cursor.source_payload_byte_count ||
        cursor.source_payload_byte_count >
            (std::numeric_limits<std::size_t>::max)() / 8U ||
        cursor.source_payload_bit_count !=
            cursor.source_payload_byte_count * 8U ||
        cursor.bit_offset >= 8U ||
        cursor.byte_offset > cursor.source_payload_byte_count ||
        cursor.byte_offset >
            (std::numeric_limits<std::size_t>::max)() / 8U) {
        return false;
    }

    absolute_bit_offset = cursor.byte_offset * 8U + cursor.bit_offset;
    if (absolute_bit_offset > cursor.source_payload_bit_count) {
        return false;
    }
    remaining_bits = cursor.source_payload_bit_count - absolute_bit_offset;
    return remaining_bits == cursor.next_unconsumed_bit_count;
}

struct Candidate final {
    std::size_t width{0U};
    std::size_t bit_offset{0U};
    std::optional<std::uint8_t> numeric;
    std::optional<std::uint8_t> prefix;
    bool byte_aligned{false};
};

[[nodiscard]] Candidate observe_candidate(
    const std::span<const std::byte> payload,
    const std::size_t absolute_bit_offset,
    const std::size_t remaining_bits) noexcept
{
    Candidate candidate;
    candidate.bit_offset = absolute_bit_offset & 7U;
    candidate.byte_aligned = candidate.bit_offset == 0U;
    candidate.width = candidate.byte_aligned
                          ? std::size_t{8U}
                          : (std::min)(std::size_t{8U}, remaining_bits);
    if (candidate.byte_aligned) {
        candidate.numeric = std::to_integer<std::uint8_t>(
            payload[absolute_bit_offset >> 3U]);
        return candidate;
    }

    std::uint8_t prefix = 0U;
    for (std::size_t bit = 0U; bit < candidate.width; ++bit) {
        const auto source_bit = absolute_bit_offset + bit;
        const auto source_byte = std::to_integer<std::uint8_t>(
            payload[source_bit >> 3U]);
        prefix |= static_cast<std::uint8_t>(
            ((source_byte >> (source_bit & 7U)) & 1U) << bit);
    }
    candidate.prefix = prefix;
    return candidate;
}

[[nodiscard]] bool candidates_equal(
    const Candidate& left,
    const Candidate& right) noexcept
{
    return left.width == right.width &&
           left.bit_offset == right.bit_offset &&
           left.numeric == right.numeric &&
           left.prefix == right.prefix &&
           left.byte_aligned == right.byte_aligned;
}

} // namespace

StockRuntimeFirstObservationState::StockRuntimeFirstObservationState(
    StockPostResourceResponseCursor exact_cursor,
    const std::size_t candidate_bit_width,
    std::optional<std::uint8_t> numeric_candidate,
    std::optional<std::uint8_t> bounded_bit_prefix,
    const bool byte_aligned,
    std::vector<StockRuntimeFirstObservationOccurrence> occurrences,
    const StockRuntimeFirstCandidateStability stability,
    std::string version_profile) noexcept
    : exact_cursor_{std::move(exact_cursor)},
      candidate_bit_width_{candidate_bit_width},
      numeric_candidate_{std::move(numeric_candidate)},
      bounded_bit_prefix_{std::move(bounded_bit_prefix)},
      byte_aligned_{byte_aligned},
      occurrences_{std::move(occurrences)},
      stability_{stability},
      version_profile_{std::move(version_profile)}
{
}

std::string_view StockRuntimeFirstObservationState::neutral_candidate_name()
    const noexcept
{
    return kStockRuntimeFirstCandidateNeutralName;
}

const StockPostResourceResponseCursor&
StockRuntimeFirstObservationState::exact_cursor() const noexcept
{
    return exact_cursor_;
}

std::size_t StockRuntimeFirstObservationState::candidate_bit_width() const noexcept
{
    return candidate_bit_width_;
}

const std::optional<std::uint8_t>&
StockRuntimeFirstObservationState::numeric_candidate() const noexcept
{
    return numeric_candidate_;
}

const std::optional<std::uint8_t>&
StockRuntimeFirstObservationState::bounded_bit_prefix() const noexcept
{
    return bounded_bit_prefix_;
}

bool StockRuntimeFirstObservationState::byte_aligned() const noexcept
{
    return byte_aligned_;
}

std::size_t StockRuntimeFirstObservationState::recurrence_count() const noexcept
{
    return occurrences_.size();
}

const std::vector<StockRuntimeFirstObservationOccurrence>&
StockRuntimeFirstObservationState::occurrences() const noexcept
{
    return occurrences_;
}

StockRuntimeFirstCandidateStability
StockRuntimeFirstObservationState::stability() const noexcept
{
    return stability_;
}

std::string_view StockRuntimeFirstObservationState::version_profile() const noexcept
{
    return version_profile_;
}

StockRuntimeFirstObservationEvidenceProfile
StockRuntimeFirstObservationState::evidence_profile() const noexcept
{
    return StockRuntimeFirstObservationEvidenceProfile::
        exact_post_resource_cursor_neutral_candidate;
}

StockRuntimeFirstObservationBuilder::StockRuntimeFirstObservationBuilder(
    StockRuntimeFirstObservationLimits limits) noexcept
    : limits_{std::move(limits)}
{
}

bool StockRuntimeFirstObservationBuilder::valid_configuration() const noexcept
{
    return limits_.maximum_runs > 0U &&
           limits_.maximum_runs <= 4'096U &&
           limits_.minimum_stable_runs >= 2U &&
           limits_.minimum_stable_runs <= limits_.maximum_runs &&
           limits_.maximum_payload_bytes > 0U &&
           limits_.maximum_payload_bytes <= 16U * 1'048'576U &&
           limits_.maximum_version_profile_bytes > 0U &&
           limits_.maximum_version_profile_bytes <= 4'096U;
}

const StockRuntimeFirstObservationLimits&
StockRuntimeFirstObservationBuilder::limits() const noexcept
{
    return limits_;
}

StockRuntimeFirstObservationBuildResult
StockRuntimeFirstObservationBuilder::build(
    const std::span<const StockRuntimeFirstObservationInput> observations) const
{
    if (!valid_configuration()) {
        return failure(
            StockRuntimeFirstObservationErrorCode::invalid_configuration, 0U,
            "first-observation limits are invalid");
    }
    if (observations.empty()) {
        return failure(
            StockRuntimeFirstObservationErrorCode::empty_input, 0U,
            "at least one accepted evidence run is required");
    }
    if (observations.size() > limits_.maximum_runs) {
        return failure(
            StockRuntimeFirstObservationErrorCode::run_limit_exceeded,
            observations.size(),
            "accepted evidence-run count exceeds its configured bound");
    }

    try {
        std::unordered_set<std::string_view> run_ids;
        run_ids.reserve(observations.size());
        std::vector<StockRuntimeFirstObservationOccurrence> occurrences;
        occurrences.reserve(observations.size());

        std::optional<Candidate> baseline;
        bool conflicting = false;
        std::string_view version_profile;

        for (std::size_t index = 0U; index < observations.size(); ++index) {
            const auto& input = observations[index];
            if (!valid_run_id(input.run_id)) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::invalid_run_id, index,
                    "run id is not 32 canonical lowercase hexadecimal characters");
            }
            if (!run_ids.insert(input.run_id).second) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::duplicate_run_id, index,
                    "the same evidence run appears more than once");
            }
            if (!input.accepted_evidence_run) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::unaccepted_run, index,
                    "candidate recurrence uses a run that was not accepted");
            }
            if (!input.known_signon_validated) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::signon_not_validated,
                    index,
                    "candidate recurrence requires the exact captured sign-on chain");
            }
            if (!valid_version_profile(
                    input.version_profile,
                    limits_.maximum_version_profile_bytes)) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::invalid_version_profile,
                    index,
                    "version profile is empty, oversized, or non-canonical");
            }
            if (index == 0U) {
                version_profile = input.version_profile;
            } else if (input.version_profile != version_profile) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::version_profile_mismatch,
                    index,
                    "candidate recurrence crosses stock version profiles");
            }

            std::size_t absolute_bit_offset = 0U;
            std::size_t remaining_bits = 0U;
            if (!valid_cursor_geometry(
                    input, limits_.maximum_payload_bytes,
                    absolute_bit_offset, remaining_bits)) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::payload_geometry_mismatch,
                    index,
                    "cursor geometry does not match its owning replay payload");
            }
            if (remaining_bits == 0U ||
                ((absolute_bit_offset & 7U) == 0U && remaining_bits < 8U)) {
                return failure(
                    StockRuntimeFirstObservationErrorCode::missing_candidate, index,
                    "the exact cursor has no complete neutral candidate");
            }

            const auto candidate = observe_candidate(
                input.source_payload, absolute_bit_offset, remaining_bits);
            if (!baseline) {
                baseline = candidate;
            } else if (!candidates_equal(*baseline, candidate)) {
                conflicting = true;
            }

            occurrences.push_back(StockRuntimeFirstObservationOccurrence{
                input.run_id,
                input.cursor.replay_payload_ordinal,
                input.cursor.corpus_observed_ordinal,
                input.cursor.delivery_ordinal,
                input.cursor.byte_offset,
                input.cursor.bit_offset,
                candidate.width,
                candidate.numeric,
                candidate.prefix,
                candidate.byte_aligned,
            });
        }

        const auto stability = conflicting
                                   ? StockRuntimeFirstCandidateStability::
                                         candidate_conflicting
                                   : observations.size() >=
                                             limits_.minimum_stable_runs
                                         ? StockRuntimeFirstCandidateStability::
                                               stable_observation
                                         : StockRuntimeFirstCandidateStability::
                                               single_observation;
        return StockRuntimeFirstObservationBuildResult{
            StockRuntimeFirstObservationState{
                observations.front().cursor,
                baseline->width,
                conflicting ? std::nullopt : baseline->numeric,
                conflicting ? std::nullopt : baseline->prefix,
                baseline->byte_aligned,
                std::move(occurrences),
                stability,
                std::string{version_profile}},
            std::nullopt,
        };
    } catch (...) {
        return failure(
            StockRuntimeFirstObservationErrorCode::allocation_failed, 0U,
            "first-observation metadata allocation failed transactionally");
    }
}

} // namespace hlclient::goldsrc

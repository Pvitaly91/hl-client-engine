#include "hlclient/goldsrc/stock_runtime_message_catalog.hpp"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool bounded_nonzero(
    const std::size_t value,
    const std::size_t hard_limit) noexcept
{
    return value != 0U && value <= hard_limit;
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] StockRuntimeMessageCatalogDecodeResult decode_failure(
    const StockRuntimeDecodeErrorCode code,
    std::string context,
    std::optional<StockRuntimeSourceCursor> cursor = std::nullopt)
{
    return StockRuntimeMessageCatalogDecodeResult{
        std::nullopt,
        StockRuntimeDecodeError{code, std::move(cursor), std::move(context)},
    };
}

[[nodiscard]] StockRuntimeMessageCatalogDecodeResult allocation_failure() noexcept
{
    return StockRuntimeMessageCatalogDecodeResult{
        std::nullopt,
        StockRuntimeDecodeError{
            StockRuntimeDecodeErrorCode::unable_to_retain_state,
            std::nullopt,
            {}},
    };
}

} // namespace

bool valid_stock_runtime_compatibility_profile(
    const StockRuntimeCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case StockRuntimeCompatibilityProfile::
        stock_protocol_48_build_10210_evidence_pending:
    case StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_runtime_v1:
        return true;
    }
    return false;
}

bool valid_stock_runtime_evidence_profile(
    const StockRuntimeEvidenceProfile profile) noexcept
{
    switch (profile) {
    case StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_pending:
    case StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_v1:
        return true;
    }
    return false;
}

bool valid_stock_runtime_delta_compatibility_profile(
    const StockRuntimeDeltaCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case StockRuntimeDeltaCompatibilityProfile::
        stock_protocol_48_build_10210_delta_evidence_pending:
    case StockRuntimeDeltaCompatibilityProfile::stock_protocol_48_build_10210_delta_v1:
        return true;
    }
    return false;
}

StockRuntimeEvidenceProfile stock_runtime_evidence_profile_for(
    const StockRuntimeCompatibilityProfile profile) noexcept
{
    if (profile ==
        StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_runtime_v1) {
        return StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_v1;
    }
    return StockRuntimeEvidenceProfile::controlled_signed_stock_transcript_pending;
}

StockRuntimeDeltaCompatibilityProfile stock_runtime_delta_profile_for(
    const StockRuntimeCompatibilityProfile profile) noexcept
{
    if (profile ==
        StockRuntimeCompatibilityProfile::stock_protocol_48_build_10210_runtime_v1) {
        return StockRuntimeDeltaCompatibilityProfile::
            stock_protocol_48_build_10210_delta_v1;
    }
    return StockRuntimeDeltaCompatibilityProfile::
        stock_protocol_48_build_10210_delta_evidence_pending;
}

const StockRuntimeDecodeLimits& hard_stock_runtime_decode_limits() noexcept
{
    static const StockRuntimeDecodeLimits limits{
        8U << 20U,
        512U,
        4096U,
        16384U,
        16384U,
        1024U,
        256U,
        256U,
        64U << 20U,
        8U << 20U,
        8U << 20U,
        256U,
    };
    return limits;
}

bool valid_stock_runtime_decode_limits(const StockRuntimeDecodeLimits& limits) noexcept
{
    const auto& hard = hard_stock_runtime_decode_limits();
    return bounded_nonzero(limits.maximum_payload_bytes, hard.maximum_payload_bytes) &&
        bounded_nonzero(
            limits.maximum_messages_per_payload,
            hard.maximum_messages_per_payload) &&
        bounded_nonzero(limits.maximum_runtime_frames, hard.maximum_runtime_frames) &&
        bounded_nonzero(limits.maximum_baselines, hard.maximum_baselines) &&
        bounded_nonzero(limits.maximum_entities, hard.maximum_entities) &&
        bounded_nonzero(
            limits.maximum_clientdata_fields,
            hard.maximum_clientdata_fields) &&
        bounded_nonzero(limits.maximum_weapon_entries, hard.maximum_weapon_entries) &&
        bounded_nonzero(limits.maximum_history_frames, hard.maximum_history_frames) &&
        bounded_nonzero(limits.maximum_value_bytes, hard.maximum_value_bytes) &&
        bounded_nonzero(limits.maximum_metadata_bytes, hard.maximum_metadata_bytes) &&
        bounded_nonzero(limits.maximum_decode_steps, hard.maximum_decode_steps) &&
        bounded_nonzero(
            limits.maximum_pending_delta_bases,
            hard.maximum_pending_delta_bases);
}

StockRuntimeSourceCursor::StockRuntimeSourceCursor(
    const std::size_t absolute_bit_offset) noexcept
    : absolute_bit_offset_{absolute_bit_offset}
{
}

std::optional<StockRuntimeSourceCursor> StockRuntimeSourceCursor::create(
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::size_t payload_byte_count) noexcept
{
    if (bit_offset >= 8U || byte_offset > payload_byte_count ||
        (byte_offset == payload_byte_count && bit_offset != 0U)) {
        return std::nullopt;
    }
    if (byte_offset >
        (std::numeric_limits<std::size_t>::max() - bit_offset) / 8U) {
        return std::nullopt;
    }
    return StockRuntimeSourceCursor{byte_offset * 8U + bit_offset};
}

std::size_t StockRuntimeSourceCursor::absolute_bit_offset() const noexcept
{
    return absolute_bit_offset_;
}

std::size_t StockRuntimeSourceCursor::byte_offset() const noexcept
{
    return absolute_bit_offset_ / 8U;
}

std::size_t StockRuntimeSourceCursor::bit_offset() const noexcept
{
    return absolute_bit_offset_ % 8U;
}

bool StockRuntimeSourceCursor::byte_aligned() const noexcept
{
    return bit_offset() == 0U;
}

bool valid_stock_runtime_source_cursor(const StockRuntimeSourceCursor& cursor) noexcept
{
    return cursor.bit_offset() < 8U;
}

bool valid_stock_runtime_source_cursor(
    const StockRuntimeSourceCursor& cursor,
    const std::size_t payload_byte_count) noexcept
{
    const auto byte_offset = cursor.byte_offset();
    return byte_offset < payload_byte_count ||
        (byte_offset == payload_byte_count && cursor.bit_offset() == 0U);
}

StockRuntimeMessageCatalogEntry::StockRuntimeMessageCatalogEntry(
    const std::uint8_t opcode,
    const StockRuntimeMessageCategory category,
    StockRuntimeSourceCursor start_cursor,
    StockRuntimeSourceCursor end_cursor,
    const StockRuntimeMessageAlignment alignment,
    std::optional<std::size_t> body_bit_length,
    const std::size_t message_ordinal,
    StockRuntimeSourceMetadata source,
    const std::size_t recurrence_count,
    const StockRuntimeScenarioCorrelationStatus scenario_correlation,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile,
    const StockRuntimeDeltaCompatibilityProfile delta_profile) noexcept
    : opcode_{opcode},
      category_{category},
      start_cursor_{start_cursor},
      end_cursor_{end_cursor},
      alignment_{alignment},
      body_bit_length_{body_bit_length},
      message_ordinal_{message_ordinal},
      source_{source},
      recurrence_count_{recurrence_count},
      scenario_correlation_{scenario_correlation},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile},
      delta_profile_{delta_profile}
{
}

std::uint8_t StockRuntimeMessageCatalogEntry::opcode() const noexcept
{
    return opcode_;
}

StockRuntimeMessageCategory StockRuntimeMessageCatalogEntry::category() const noexcept
{
    return category_;
}

const StockRuntimeSourceCursor&
StockRuntimeMessageCatalogEntry::start_cursor() const noexcept
{
    return start_cursor_;
}

const StockRuntimeSourceCursor& StockRuntimeMessageCatalogEntry::end_cursor() const noexcept
{
    return end_cursor_;
}

StockRuntimeMessageAlignment StockRuntimeMessageCatalogEntry::alignment() const noexcept
{
    return alignment_;
}

const std::optional<std::size_t>&
StockRuntimeMessageCatalogEntry::body_bit_length() const noexcept
{
    return body_bit_length_;
}

std::size_t StockRuntimeMessageCatalogEntry::message_ordinal() const noexcept
{
    return message_ordinal_;
}

const StockRuntimeSourceMetadata& StockRuntimeMessageCatalogEntry::source() const noexcept
{
    return source_;
}

std::size_t StockRuntimeMessageCatalogEntry::recurrence_count() const noexcept
{
    return recurrence_count_;
}

StockRuntimeScenarioCorrelationStatus
StockRuntimeMessageCatalogEntry::scenario_correlation() const noexcept
{
    return scenario_correlation_;
}

StockRuntimeCompatibilityProfile
StockRuntimeMessageCatalogEntry::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

StockRuntimeEvidenceProfile
StockRuntimeMessageCatalogEntry::evidence_profile() const noexcept
{
    return evidence_profile_;
}

StockRuntimeDeltaCompatibilityProfile
StockRuntimeMessageCatalogEntry::delta_profile() const noexcept
{
    return delta_profile_;
}

StockRuntimeMessageCatalogState::StockRuntimeMessageCatalogState(
    std::vector<StockRuntimeMessageCatalogEntry> entries,
    const std::size_t accounted_metadata_bytes,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile,
    const StockRuntimeDeltaCompatibilityProfile delta_profile) noexcept
    : entries_{std::move(entries)},
      accounted_metadata_bytes_{accounted_metadata_bytes},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile},
      delta_profile_{delta_profile}
{
}

std::span<const StockRuntimeMessageCatalogEntry>
StockRuntimeMessageCatalogState::entries() const noexcept
{
    return entries_;
}

std::size_t StockRuntimeMessageCatalogState::message_count() const noexcept
{
    return entries_.size();
}

std::size_t StockRuntimeMessageCatalogState::accounted_metadata_bytes() const noexcept
{
    return accounted_metadata_bytes_;
}

StockRuntimeCompatibilityProfile
StockRuntimeMessageCatalogState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

StockRuntimeEvidenceProfile
StockRuntimeMessageCatalogState::evidence_profile() const noexcept
{
    return evidence_profile_;
}

StockRuntimeDeltaCompatibilityProfile
StockRuntimeMessageCatalogState::delta_profile() const noexcept
{
    return delta_profile_;
}

StockRuntimeReadyState::StockRuntimeReadyState(
    const StockRuntimeReadyStatus status,
    std::optional<std::uint64_t> runtime_generation,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : status_{status},
      runtime_generation_{runtime_generation},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

StockRuntimeReadyStatus StockRuntimeReadyState::status() const noexcept
{
    return status_;
}

bool StockRuntimeReadyState::ready() const noexcept
{
    return status_ == StockRuntimeReadyStatus::first_complete_runtime_frame ||
        status_ == StockRuntimeReadyStatus::steady_state_confirmed;
}

const std::optional<std::uint64_t>&
StockRuntimeReadyState::runtime_generation() const noexcept
{
    return runtime_generation_;
}

StockRuntimeCompatibilityProfile
StockRuntimeReadyState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

StockRuntimeEvidenceProfile StockRuntimeReadyState::evidence_profile() const noexcept
{
    return evidence_profile_;
}

StockRuntimeUnsupportedBoundary::StockRuntimeUnsupportedBoundary(
    StockRuntimeSourceCursor cursor,
    const std::size_t remaining_payload_byte_count,
    const std::size_t remaining_payload_bit_count,
    const std::size_t unconfirmed_body_byte_count,
    const std::size_t message_ordinal,
    const std::uint8_t opcode,
    const StockRuntimeUnsupportedBoundaryReason reason,
    const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : cursor_{cursor},
      remaining_payload_byte_count_{remaining_payload_byte_count},
      remaining_payload_bit_count_{remaining_payload_bit_count},
      unconfirmed_body_byte_count_{unconfirmed_body_byte_count},
      message_ordinal_{message_ordinal},
      opcode_{opcode},
      reason_{reason},
      evidence_profile_{evidence_profile}
{
}

const StockRuntimeSourceCursor& StockRuntimeUnsupportedBoundary::cursor() const noexcept
{
    return cursor_;
}

std::size_t StockRuntimeUnsupportedBoundary::remaining_payload_byte_count() const noexcept
{
    return remaining_payload_byte_count_;
}

std::size_t StockRuntimeUnsupportedBoundary::remaining_payload_bit_count() const noexcept
{
    return remaining_payload_bit_count_;
}

std::size_t StockRuntimeUnsupportedBoundary::unconfirmed_body_byte_count() const noexcept
{
    return unconfirmed_body_byte_count_;
}

std::size_t StockRuntimeUnsupportedBoundary::message_ordinal() const noexcept
{
    return message_ordinal_;
}

std::uint8_t StockRuntimeUnsupportedBoundary::opcode() const noexcept
{
    return opcode_;
}

StockRuntimeUnsupportedBoundaryReason StockRuntimeUnsupportedBoundary::reason() const noexcept
{
    return reason_;
}

StockRuntimeEvidenceProfile StockRuntimeUnsupportedBoundary::evidence_profile() const noexcept
{
    return evidence_profile_;
}

StockRuntimeEvidenceBoundaryState::StockRuntimeEvidenceBoundaryState(
    StockRuntimeMessageCatalogState catalog,
    StockRuntimeReadyState ready_state,
    StockRuntimeUnsupportedBoundary unsupported_boundary) noexcept
    : catalog_{std::move(catalog)},
      ready_state_{std::move(ready_state)},
      unsupported_boundary_{std::move(unsupported_boundary)}
{
}

const StockRuntimeMessageCatalogState&
StockRuntimeEvidenceBoundaryState::catalog() const noexcept
{
    return catalog_;
}

const StockRuntimeReadyState& StockRuntimeEvidenceBoundaryState::ready_state() const noexcept
{
    return ready_state_;
}

const StockRuntimeUnsupportedBoundary&
StockRuntimeEvidenceBoundaryState::unsupported_boundary() const noexcept
{
    return unsupported_boundary_;
}

std::string_view to_string(const StockRuntimeDecodeErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeDecodeErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StockRuntimeDecodeErrorCode::invalid_profile: return "invalid_profile";
    case StockRuntimeDecodeErrorCode::evidence_not_confirmed:
        return "evidence_not_confirmed";
    case StockRuntimeDecodeErrorCode::payload_too_large: return "payload_too_large";
    case StockRuntimeDecodeErrorCode::invalid_boundary_geometry:
        return "invalid_boundary_geometry";
    case StockRuntimeDecodeErrorCode::missing_delta_registry:
        return "missing_delta_registry";
    case StockRuntimeDecodeErrorCode::message_limit_exceeded:
        return "message_limit_exceeded";
    case StockRuntimeDecodeErrorCode::metadata_limit_exceeded:
        return "metadata_limit_exceeded";
    case StockRuntimeDecodeErrorCode::decode_step_limit_exceeded:
        return "decode_step_limit_exceeded";
    case StockRuntimeDecodeErrorCode::size_overflow: return "size_overflow";
    case StockRuntimeDecodeErrorCode::unable_to_retain_state:
        return "unable_to_retain_state";
    }
    return "unknown";
}

StockRuntimeMessageCatalogDecoder::StockRuntimeMessageCatalogDecoder(
    StockRuntimeDecodeLimits limits,
    const StockRuntimeCompatibilityProfile compatibility_profile) noexcept
    : limits_{limits}, compatibility_profile_{compatibility_profile}
{
}

bool StockRuntimeMessageCatalogDecoder::valid_configuration() const noexcept
{
    return valid_stock_runtime_compatibility_profile(compatibility_profile_) &&
        valid_stock_runtime_decode_limits(limits_);
}

const StockRuntimeDecodeLimits& StockRuntimeMessageCatalogDecoder::limits() const noexcept
{
    return limits_;
}

StockRuntimeCompatibilityProfile
StockRuntimeMessageCatalogDecoder::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

StockRuntimeMessageCatalogDecodeResult StockRuntimeMessageCatalogDecoder::decode(
    const OwnedServicePayload& payload,
    const PostResourceResponseBoundary& boundary,
    const DeltaSchemaRegistryState& delta_registry) const
{
    try {
        if (!valid_stock_runtime_compatibility_profile(compatibility_profile_)) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::invalid_profile,
                "Stock runtime compatibility profile is invalid");
        }
        if (!valid_stock_runtime_decode_limits(limits_)) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::invalid_configuration,
                "Stock runtime decode limits are invalid");
        }

        // This reserved profile must stay unreachable until signed-stock
        // evidence promotes its complete grammar. Do not inspect any source
        // payload, boundary, or registry on this branch.
        if (compatibility_profile_ == StockRuntimeCompatibilityProfile::
                stock_protocol_48_build_10210_runtime_v1) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::evidence_not_confirmed,
                "Reserved stock runtime v1 grammar has not been confirmed");
        }

        if (payload.bytes.size() > limits_.maximum_payload_bytes) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::payload_too_large,
                "Post-resource runtime payload exceeds its configured bound");
        }
        if (delta_registry.schema_count() == 0U) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::missing_delta_registry,
                "Stock runtime boundary requires the published delta schema registry");
        }
        if (limits_.maximum_messages_per_payload < 1U) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::message_limit_exceeded,
                "Stock runtime catalog cannot retain its first exact boundary");
        }
        if (limits_.maximum_decode_steps < 1U) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::decode_step_limit_exceeded,
                "Stock runtime catalog cannot inspect its first exact boundary");
        }

        const auto cursor = StockRuntimeSourceCursor::create(
            boundary.byte_offset(),
            boundary.bit_offset(),
            payload.bytes.size());
        const auto& source = boundary.source_payload();
        if (!cursor.has_value() ||
            boundary.kind() !=
                PostResourceResponseBoundaryKind::opcode_at_payload_start ||
            boundary.byte_offset() != 0U || boundary.bit_offset() != 0U ||
            !boundary.opcode().has_value() || payload.bytes.empty() ||
            boundary.remaining_byte_count() != payload.bytes.size() - 1U ||
            source.direction != payload.direction ||
            source.source_sequence != payload.source_sequence ||
            source.reliable != payload.source_reliable ||
            source.reassembled != payload.reassembled ||
            source.decompressed != payload.decompressed ||
            source.decoded_payload_byte_count != payload.bytes.size() ||
            boundary.opcode().value() !=
                std::to_integer<std::uint8_t>(payload.bytes.front())) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::invalid_boundary_geometry,
                "Post-resource boundary does not own the exact runtime payload");
        }

        std::size_t remaining_payload_bit_count = 0U;
        if (!checked_multiply(payload.bytes.size(), 8U, remaining_payload_bit_count)) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::size_overflow,
                "Remaining stock runtime payload bit count overflows");
        }

        std::size_t accounted_metadata_bytes = sizeof(StockRuntimeEvidenceBoundaryState);
        if (!checked_add(
                accounted_metadata_bytes,
                sizeof(StockRuntimeMessageCatalogEntry),
                accounted_metadata_bytes)) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::size_overflow,
                "Stock runtime catalog metadata accounting overflows");
        }
        if (accounted_metadata_bytes > limits_.maximum_metadata_bytes) {
            return decode_failure(
                StockRuntimeDecodeErrorCode::metadata_limit_exceeded,
                "Stock runtime catalog metadata exceeds its configured bound");
        }

        const auto evidence_profile = stock_runtime_evidence_profile_for(
            compatibility_profile_);
        const auto delta_profile = stock_runtime_delta_profile_for(
            compatibility_profile_);
        const StockRuntimeSourceMetadata source_metadata{
            0U,
            payload.direction,
            payload.source_sequence,
            payload.source_acknowledgement,
            payload.source_reliable,
            payload.acknowledgement_reliable,
            payload.reassembled,
            payload.decompressed,
            payload.bytes.size(),
        };

        std::vector<StockRuntimeMessageCatalogEntry> entries;
        entries.reserve(1U);
        entries.push_back(StockRuntimeMessageCatalogEntry{
            boundary.opcode().value(),
            StockRuntimeMessageCategory::unsupported_runtime_message,
            cursor.value(),
            cursor.value(),
            StockRuntimeMessageAlignment::evidence_pending,
            std::nullopt,
            0U,
            source_metadata,
            1U,
            StockRuntimeScenarioCorrelationStatus::not_observed,
            compatibility_profile_,
            evidence_profile,
            delta_profile,
        });

        auto catalog = StockRuntimeMessageCatalogState{
            std::move(entries),
            accounted_metadata_bytes,
            compatibility_profile_,
            evidence_profile,
            delta_profile,
        };
        auto ready_state = StockRuntimeReadyState{
            StockRuntimeReadyStatus::evidence_pending,
            std::nullopt,
            compatibility_profile_,
            evidence_profile,
        };
        auto unsupported_boundary = StockRuntimeUnsupportedBoundary{
            cursor.value(),
            payload.bytes.size(),
            remaining_payload_bit_count,
            boundary.remaining_byte_count(),
            0U,
            boundary.opcode().value(),
            StockRuntimeUnsupportedBoundaryReason::
                runtime_message_grammar_evidence_pending,
            evidence_profile,
        };

        return StockRuntimeMessageCatalogDecodeResult{
            StockRuntimeEvidenceBoundaryState{
                std::move(catalog),
                std::move(ready_state),
                std::move(unsupported_boundary)},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return allocation_failure();
    } catch (const std::length_error&) {
        return allocation_failure();
    }
}

} // namespace hlclient::goldsrc

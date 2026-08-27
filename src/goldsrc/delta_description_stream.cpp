#include <hlclient/goldsrc/delta_description.hpp>

#include <hlclient/goldsrc/service_message_stream.hpp>

#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] DeltaDescriptionStreamDecodeResult stream_failure(
    const DeltaDescriptionStreamErrorCode code,
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::optional<DeltaDescriptionErrorCode> parser_code,
    const std::optional<DeltaRegistryErrorCode> registry_code,
    std::string context)
{
    return DeltaDescriptionStreamDecodeResult{
        std::nullopt,
        DeltaDescriptionStreamError{
            code,
            byte_offset,
            bit_offset,
            parser_code,
            registry_code,
            std::move(context),
        },
        0U,
    };
}

} // namespace

PostDeltaBoundary::PostDeltaBoundary(
    const std::uint8_t opcode,
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::size_t remaining_byte_count,
    const PostDeltaBoundaryCategory category,
    const PostDeltaBoundaryEvidenceStatus evidence_status) noexcept
    : opcode_{opcode},
      byte_offset_{byte_offset},
      bit_offset_{bit_offset},
      remaining_byte_count_{remaining_byte_count},
      category_{category},
      evidence_status_{evidence_status}
{
}

std::uint8_t PostDeltaBoundary::opcode() const noexcept { return opcode_; }
std::size_t PostDeltaBoundary::byte_offset() const noexcept { return byte_offset_; }
std::size_t PostDeltaBoundary::bit_offset() const noexcept { return bit_offset_; }
std::size_t PostDeltaBoundary::remaining_byte_count() const noexcept { return remaining_byte_count_; }
PostDeltaBoundaryCategory PostDeltaBoundary::category() const noexcept { return category_; }
PostDeltaBoundaryEvidenceStatus PostDeltaBoundary::evidence_status() const noexcept { return evidence_status_; }

DeltaDescriptionStreamDecoder::DeltaDescriptionStreamDecoder(
    DeltaDescriptionLimits limits,
    const DeltaCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool DeltaDescriptionStreamDecoder::valid_configuration() const noexcept
{
    return valid_delta_description_limits(limits_);
}

DeltaDescriptionStreamDecodeResult DeltaDescriptionStreamDecoder::decode(
    const std::span<const std::byte> service_payload,
    const ResourcePhaseBoundary& initial_boundary) const
{
    if (!valid_configuration()) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::invalid_configuration,
            0U,
            0U,
            std::nullopt,
            std::nullopt,
            "Delta stream limits are outside project hard caps");
    }
    if (initial_boundary.direction() != ResourcePhaseBoundaryDirection::server_message ||
        initial_boundary.opcode() != kDeltaDescriptionOpcode ||
        initial_boundary.byte_offset() >= service_payload.size()) {
        return stream_failure(
            initial_boundary.opcode() != kDeltaDescriptionOpcode
                ? DeltaDescriptionStreamErrorCode::wrong_initial_opcode
                : DeltaDescriptionStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.byte_offset() * 8U,
            std::nullopt,
            std::nullopt,
            "Pre-resource continuation does not identify the exact opcode-14 boundary");
    }
    const auto expected_remaining =
        service_payload.size() - initial_boundary.byte_offset() - 1U;
    if (initial_boundary.remaining_byte_count() != expected_remaining) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.byte_offset() * 8U,
            std::nullopt,
            std::nullopt,
            "Pre-resource remaining-byte count does not match the owning payload");
    }

    DeltaDescriptionParser parser{limits_, profile_};
    DeltaSchemaRegistryBuilder builder{limits_};
    auto cursor = initial_boundary.byte_offset();
    std::size_t message_count = 0U;
    while (true) {
        if (cursor >= service_payload.size()) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::missing_post_delta_boundary,
                cursor,
                cursor * 8U,
                std::nullopt,
                std::nullopt,
                "Delta schema stream ends without a following service opcode");
        }
        const auto opcode = std::to_integer<std::uint8_t>(service_payload[cursor]);
        if (opcode != kDeltaDescriptionOpcode) {
            break;
        }
        if (message_count >= limits_.maximum_schema_count) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::message_count_limit_exceeded,
                cursor,
                cursor * 8U,
                std::nullopt,
                std::nullopt,
                "Delta schema message count exceeds the configured bound");
        }

        auto parsed = parser.parse(service_payload, cursor);
        if (!parsed || !parsed.schema) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::parser_failure,
                parsed.error ? parsed.error->byte_offset : cursor,
                parsed.error ? parsed.error->bit_offset : cursor * 8U,
                parsed.error ? std::optional{parsed.error->code} : std::nullopt,
                std::nullopt,
                parsed.error ? parsed.error->context
                             : "Delta parser returned no candidate or diagnostic");
        }
        auto inserted = builder.insert(*parsed.schema);
        if (!inserted) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::registry_failure,
                cursor,
                cursor * 8U,
                std::nullopt,
                inserted.error ? std::optional{inserted.error->code} : std::nullopt,
                inserted.error ? inserted.error->context
                               : "Delta registry rejected a schema without a diagnostic");
        }
        if (parsed.next_bit_offset != 0U || parsed.next_byte_offset <= cursor) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::size_overflow,
                cursor,
                cursor * 8U,
                std::nullopt,
                std::nullopt,
                "Delta parser returned a non-advancing or unaligned cursor");
        }
        cursor = parsed.next_byte_offset;
        ++message_count;
    }

    if (message_count == 0U) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::wrong_initial_opcode,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Delta stream contains no opcode-14 schema message");
    }
    if (cursor >= service_payload.size() ||
        service_payload.size() - cursor < 2U) {
        const bool truncated_post_delta_boundary =
            cursor < service_payload.size() &&
            std::to_integer<std::uint8_t>(service_payload[cursor]) ==
                kStockPostDeltaBoundaryOpcode;
        return stream_failure(
            truncated_post_delta_boundary
                ? DeltaDescriptionStreamErrorCode::malformed_post_delta_boundary
                : DeltaDescriptionStreamErrorCode::missing_post_delta_boundary,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Post-delta boundary must retain at least one unconsumed body byte");
    }

    const auto boundary_opcode = std::to_integer<std::uint8_t>(service_payload[cursor]);
    const bool stock_boundary =
        boundary_opcode == kStockPostDeltaBoundaryOpcode;
    auto boundary = PostDeltaBoundary{
        boundary_opcode,
        cursor,
        0U,
        service_payload.size() - cursor - 1U,
        stock_boundary ? PostDeltaBoundaryCategory::stock_observed_opcode_44
                       : PostDeltaBoundaryCategory::neutral_message,
        stock_boundary
            ? PostDeltaBoundaryEvidenceStatus::
                  stock_confirmed_opcode_44_body_unconsumed
            : PostDeltaBoundaryEvidenceStatus::synthetic_neutral_boundary,
    };
    if (message_count > std::numeric_limits<std::size_t>::max() - 2U) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::size_overflow,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Delta event count overflowed");
    }
    const auto bytes_consumed = cursor - initial_boundary.byte_offset();
    if (bytes_consumed > std::numeric_limits<std::size_t>::max() / 8U) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::size_overflow,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Delta stream bit count overflowed");
    }

    return DeltaDescriptionStreamDecodeResult{
        DeltaDescriptionStreamState{
            std::move(builder).publish(),
            std::move(boundary),
            message_count,
            bytes_consumed * 8U,
            bytes_consumed,
        },
        std::nullopt,
        message_count + 2U,
    };
}

} // namespace hlclient::goldsrc

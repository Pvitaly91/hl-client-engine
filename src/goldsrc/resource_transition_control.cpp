#include <hlclient/goldsrc/resource_transition_control.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>

#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool supported_profile(
    const ResourceTransitionControlCompatibilityProfile profile) noexcept
{
    return profile == ResourceTransitionControlCompatibilityProfile::
                          stock_protocol_48_build_10210;
}

[[nodiscard]] ResourceTransitionControlParseResult failure(
    const ResourceTransitionControlErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ResourceTransitionControlParseResult{
        std::nullopt,
        std::nullopt,
        ResourceTransitionControlError{
            code,
            byte_offset,
            std::move(context),
        },
        0U,
        0U,
    };
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max)() - left) {
        return false;
    }
    output = left + right;
    return true;
}

} // namespace

bool valid_resource_transition_control_limits(
    const ResourceTransitionControlLimits& limits) noexcept
{
    return limits.maximum_transition_control_size ==
               kResourceTransitionControlMessageSize &&
           limits.maximum_opaque_counter_value > 0U &&
           limits.maximum_opaque_counter_value <= kMaximumOpaqueCounterValue;
}

ResourceTransitionControlState::ResourceTransitionControlState(
    const std::uint32_t opaque_counter,
    const std::uint32_t reserved_zero,
    const std::size_t source_message_offset,
    const ResourceTransitionControlCompatibilityProfile profile) noexcept
    : opaque_counter_{opaque_counter},
      reserved_zero_{reserved_zero},
      source_message_offset_{source_message_offset},
      profile_{profile}
{
}

std::size_t ResourceTransitionControlState::source_message_offset() const noexcept
{
    return source_message_offset_;
}

ResourceTransitionControlCompatibilityProfile
ResourceTransitionControlState::compatibility_profile() const noexcept
{
    return profile_;
}

ResourceTransitionControlEvidenceProfile
ResourceTransitionControlState::evidence_profile() const noexcept
{
    return ResourceTransitionControlEvidenceProfile::
        repeated_signed_stock_capture_opaque_semantics;
}

Opcode43Boundary::Opcode43Boundary(
    const std::size_t byte_offset,
    const std::size_t remaining_byte_count,
    const std::size_t source_payload_size,
    const ResourceTransitionControlCompatibilityProfile profile) noexcept
    : byte_offset_{byte_offset},
      remaining_byte_count_{remaining_byte_count},
      source_payload_size_{source_payload_size},
      profile_{profile}
{
}

std::size_t Opcode43Boundary::byte_offset() const noexcept
{
    return byte_offset_;
}

std::size_t Opcode43Boundary::remaining_byte_count() const noexcept
{
    return remaining_byte_count_;
}

std::size_t Opcode43Boundary::source_payload_size() const noexcept
{
    return source_payload_size_;
}

ResourceTransitionControlCompatibilityProfile
Opcode43Boundary::compatibility_profile() const noexcept
{
    return profile_;
}

ResourceTransitionControlEvidenceProfile
Opcode43Boundary::evidence_profile() const noexcept
{
    return ResourceTransitionControlEvidenceProfile::
        repeated_signed_stock_capture_opaque_semantics;
}

ResourceTransitionControlParser::ResourceTransitionControlParser(
    ResourceTransitionControlLimits limits,
    const ResourceTransitionControlCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool ResourceTransitionControlParser::valid_configuration() const noexcept
{
    return valid_resource_transition_control_limits(limits_) &&
           supported_profile(profile_);
}

const ResourceTransitionControlLimits&
ResourceTransitionControlParser::limits() const noexcept
{
    return limits_;
}

ResourceTransitionControlParseResult ResourceTransitionControlParser::parse(
    const std::span<const std::byte> second_service_payload,
    const std::size_t opcode_byte_offset) const
{
    if (!valid_configuration()) {
        return failure(
            ResourceTransitionControlErrorCode::invalid_configuration,
            0U,
            "Transition-control limits or compatibility profile are unsupported");
    }
    if (opcode_byte_offset >= second_service_payload.size()) {
        return failure(
            ResourceTransitionControlErrorCode::invalid_input_geometry,
            opcode_byte_offset,
            "Transition-control opcode offset is outside the service payload");
    }
    if (std::to_integer<std::uint8_t>(
            second_service_payload[opcode_byte_offset]) !=
        kResourceTransitionControlOpcode) {
        return failure(
            ResourceTransitionControlErrorCode::wrong_opcode,
            opcode_byte_offset,
            "Transition-control parser requires the exact opcode-45 cursor");
    }

    std::size_t body_offset = 0U;
    if (!checked_add(opcode_byte_offset, 1U, body_offset)) {
        return failure(
            ResourceTransitionControlErrorCode::size_overflow,
            opcode_byte_offset,
            "Transition-control body cursor overflowed");
    }
    const auto available_body_bytes =
        second_service_payload.size() - body_offset;
    if (available_body_bytes < kResourceTransitionControlBodySize) {
        return failure(
            ResourceTransitionControlErrorCode::truncated_body,
            second_service_payload.size(),
            "Transition-control fixed eight-byte body is truncated");
    }

    ByteReader reader{second_service_payload.subspan(
        body_offset,
        kResourceTransitionControlBodySize)};
    const auto opaque_counter = reader.read_uint32_le();
    const auto reserved_zero = reader.read_uint32_le();
    if (!opaque_counter || !reserved_zero ||
        reader.position() != kResourceTransitionControlBodySize) {
        return failure(
            ResourceTransitionControlErrorCode::truncated_body,
            body_offset + reader.position(),
            "Transition-control fixed body could not be decoded exactly");
    }
    if (static_cast<std::uint64_t>(*opaque_counter) >
        limits_.maximum_opaque_counter_value) {
        return failure(
            ResourceTransitionControlErrorCode::opaque_counter_limit_exceeded,
            body_offset,
            "Opaque transition-control value exceeds the configured project bound");
    }
    if (*reserved_zero != 0U) {
        return failure(
            ResourceTransitionControlErrorCode::invalid_reserved_value,
            body_offset + sizeof(std::uint32_t),
            "Transition-control reserved u32 must equal zero");
    }

    std::size_t boundary_offset = 0U;
    if (!checked_add(
            opcode_byte_offset,
            kResourceTransitionControlMessageSize,
            boundary_offset)) {
        return failure(
            ResourceTransitionControlErrorCode::size_overflow,
            opcode_byte_offset,
            "Transition-control next cursor overflowed");
    }
    if (boundary_offset >= second_service_payload.size()) {
        return failure(
            ResourceTransitionControlErrorCode::missing_next_boundary,
            boundary_offset,
            "Transition-control payload ends before the required next opcode");
    }
    const auto boundary_opcode = std::to_integer<std::uint8_t>(
        second_service_payload[boundary_offset]);
    if (boundary_opcode != kPostResourceTransitionControlBoundaryOpcode) {
        return failure(
            ResourceTransitionControlErrorCode::wrong_next_boundary_opcode,
            boundary_offset,
            "Transition-control exact next cursor does not contain opcode 43");
    }
    if (second_service_payload.size() - boundary_offset < 2U) {
        return failure(
            ResourceTransitionControlErrorCode::truncated_next_boundary_body,
            boundary_offset,
            "Opcode-43 boundary must retain at least one unconsumed body byte");
    }

    return ResourceTransitionControlParseResult{
        ResourceTransitionControlState{
            *opaque_counter,
            *reserved_zero,
            opcode_byte_offset,
            profile_,
        },
        Opcode43Boundary{
            boundary_offset,
            second_service_payload.size() - boundary_offset - 1U,
            second_service_payload.size(),
            profile_,
        },
        std::nullopt,
        kResourceTransitionControlMessageSize,
        boundary_offset,
    };
}

} // namespace hlclient::goldsrc

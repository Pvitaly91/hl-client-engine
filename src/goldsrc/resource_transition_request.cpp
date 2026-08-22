#include <hlclient/goldsrc/resource_transition_request.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::array<std::byte, kResourceTransitionRequestSize>
    kConfirmedResourceTransitionRequest{
        std::byte{3U},
        std::byte{'s'},
        std::byte{'e'},
        std::byte{'n'},
        std::byte{'d'},
        std::byte{'r'},
        std::byte{'e'},
        std::byte{'s'},
        std::byte{0U},
    };

[[nodiscard]] bool supported_profile(
    const ResourceTransitionRequestCompatibilityProfile profile) noexcept
{
    return profile == ResourceTransitionRequestCompatibilityProfile::
                          stock_protocol_48_build_10210;
}

[[nodiscard]] ResourceTransitionRequestBuildResult build_failure(
    const ResourceTransitionRequestErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ResourceTransitionRequestBuildResult{
        std::nullopt,
        ResourceTransitionRequestError{
            code,
            byte_offset,
            std::move(context),
        },
    };
}

[[nodiscard]] ResourceTransitionRequestParseResult parse_failure(
    const ResourceTransitionRequestErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ResourceTransitionRequestParseResult{
        std::nullopt,
        ResourceTransitionRequestError{
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

bool valid_resource_transition_request_limits(
    const ResourceTransitionRequestLimits& limits) noexcept
{
    return limits.maximum_resource_transition_request_size ==
           kResourceTransitionRequestSize;
}

ResourceTransitionRequest::ResourceTransitionRequest(
    std::array<std::byte, kResourceTransitionRequestSize> bytes,
    const std::size_t source_message_offset,
    const ResourceTransitionRequestCompatibilityProfile profile) noexcept
    : bytes_{bytes},
      source_message_offset_{source_message_offset},
      profile_{profile}
{
}

std::span<const std::byte, kResourceTransitionRequestSize>
ResourceTransitionRequest::bytes() const noexcept
{
    return bytes_;
}

std::size_t ResourceTransitionRequest::source_message_offset() const noexcept
{
    return source_message_offset_;
}

ResourceTransitionRequestCompatibilityProfile
ResourceTransitionRequest::compatibility_profile() const noexcept
{
    return profile_;
}

ResourceTransitionRequestEvidenceProfile
ResourceTransitionRequest::evidence_profile() const noexcept
{
    return ResourceTransitionRequestEvidenceProfile::repeated_signed_stock_capture;
}

ResourceTransitionRequestBuilder::ResourceTransitionRequestBuilder(
    ResourceTransitionRequestLimits limits,
    const ResourceTransitionRequestCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool ResourceTransitionRequestBuilder::valid_configuration() const noexcept
{
    return valid_resource_transition_request_limits(limits_) &&
           supported_profile(profile_);
}

const ResourceTransitionRequestLimits&
ResourceTransitionRequestBuilder::limits() const noexcept
{
    return limits_;
}

ResourceTransitionRequestBuildResult
ResourceTransitionRequestBuilder::build() const
{
    if (!valid_configuration()) {
        return build_failure(
            ResourceTransitionRequestErrorCode::invalid_configuration,
            0U,
            "Resource-transition request limits or profile are unsupported");
    }

    return ResourceTransitionRequestBuildResult{
        ResourceTransitionRequest{
            kConfirmedResourceTransitionRequest,
            0U,
            profile_,
        },
        std::nullopt,
    };
}

ResourceTransitionRequestParser::ResourceTransitionRequestParser(
    ResourceTransitionRequestLimits limits,
    const ResourceTransitionRequestCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool ResourceTransitionRequestParser::valid_configuration() const noexcept
{
    return valid_resource_transition_request_limits(limits_) &&
           supported_profile(profile_);
}

const ResourceTransitionRequestLimits&
ResourceTransitionRequestParser::limits() const noexcept
{
    return limits_;
}

ResourceTransitionRequestParseResult ResourceTransitionRequestParser::parse(
    const std::span<const std::byte> reliable_message_stream,
    const std::size_t opcode_byte_offset) const
{
    if (!valid_configuration()) {
        return parse_failure(
            ResourceTransitionRequestErrorCode::invalid_configuration,
            0U,
            "Resource-transition request limits or profile are unsupported");
    }
    if (opcode_byte_offset >= reliable_message_stream.size()) {
        return parse_failure(
            ResourceTransitionRequestErrorCode::invalid_input_geometry,
            opcode_byte_offset,
            "Resource-transition request opcode offset is outside the input");
    }
    if (reliable_message_stream[opcode_byte_offset] !=
        kConfirmedResourceTransitionRequest.front()) {
        return parse_failure(
            ResourceTransitionRequestErrorCode::wrong_opcode,
            opcode_byte_offset,
            "Resource-transition request requires the captured opcode-3 cursor");
    }

    std::size_t next_byte_offset = 0U;
    if (!checked_add(
            opcode_byte_offset,
            kResourceTransitionRequestSize,
            next_byte_offset)) {
        return parse_failure(
            ResourceTransitionRequestErrorCode::size_overflow,
            opcode_byte_offset,
            "Resource-transition request cursor arithmetic overflowed");
    }

    for (std::size_t index = 0U;
         index < kResourceTransitionRequestCommandLength;
         ++index) {
        const auto absolute_offset = opcode_byte_offset + 1U + index;
        if (absolute_offset >= reliable_message_stream.size()) {
            return parse_failure(
                ResourceTransitionRequestErrorCode::truncated_request,
                reliable_message_stream.size(),
                "Resource-transition command is truncated");
        }
        if (reliable_message_stream[absolute_offset] == std::byte{0U}) {
            return parse_failure(
                ResourceTransitionRequestErrorCode::unexpected_terminator,
                absolute_offset,
                "Resource-transition command terminates before the captured boundary");
        }
        if (reliable_message_stream[absolute_offset] !=
            kConfirmedResourceTransitionRequest[index + 1U]) {
            return parse_failure(
                ResourceTransitionRequestErrorCode::unsupported_command_variant,
                absolute_offset,
                "Resource-transition command does not match the captured fixed profile");
        }
    }

    const auto terminator_offset =
        opcode_byte_offset + 1U + kResourceTransitionRequestCommandLength;
    if (terminator_offset >= reliable_message_stream.size()) {
        return parse_failure(
            ResourceTransitionRequestErrorCode::missing_terminator,
            reliable_message_stream.size(),
            "Resource-transition command has no NUL terminator");
    }
    if (reliable_message_stream[terminator_offset] != std::byte{0U}) {
        return parse_failure(
            ResourceTransitionRequestErrorCode::missing_terminator,
            terminator_offset,
            "Resource-transition command lacks the exact captured NUL terminator");
    }

    std::array<std::byte, kResourceTransitionRequestSize> owned_bytes{};
    std::copy_n(
        reliable_message_stream.begin() +
            static_cast<std::ptrdiff_t>(opcode_byte_offset),
        kResourceTransitionRequestSize,
        owned_bytes.begin());

    return ResourceTransitionRequestParseResult{
        ResourceTransitionRequest{
            owned_bytes,
            opcode_byte_offset,
            profile_,
        },
        std::nullopt,
        kResourceTransitionRequestSize,
        next_byte_offset,
    };
}

} // namespace hlclient::goldsrc

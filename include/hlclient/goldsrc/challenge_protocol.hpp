#pragma once

#include <hlclient/goldsrc/connectionless_packet.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// The stock Protocol 48 server transmits a canonical unsigned decimal value.
// Preserve all 32 bits; the connect request formats the same bit pattern as a
// signed decimal integer, matching the observed stock client behavior.
using ChallengeToken = std::uint32_t;

struct ChallengeResponse {
    ChallengeToken challenge{0};
    std::uint32_t profile_parameter_1{0};
    std::uint64_t profile_parameter_2{0};
    std::uint32_t profile_parameter_3{0};
};

enum class ChallengeProtocolErrorCode {
    packet_too_short,
    invalid_header,
    payload_too_large,
    unexpected_response_type,
    missing_challenge,
    invalid_challenge,
    challenge_overflow,
    invalid_terminator,
    unexpected_trailing_data,
    unsupported_variant,
};

struct ChallengeProtocolError {
    ChallengeProtocolErrorCode code{ChallengeProtocolErrorCode::unsupported_variant};
    std::size_t byte_offset{0};
    std::string context;
};

struct ChallengeResponseParseResult {
    std::optional<ChallengeResponse> response;
    std::optional<ChallengeProtocolError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return response.has_value();
    }
};

struct GetChallengeRequestResult {
    std::optional<std::vector<std::byte>> datagram;
    std::optional<ChallengeProtocolError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return datagram.has_value();
    }
};

[[nodiscard]] GetChallengeRequestResult build_getchallenge_request();

[[nodiscard]] ChallengeResponseParseResult parse_challenge_response(
    std::span<const std::byte> datagram);

[[nodiscard]] constexpr std::string_view to_string(
    const ChallengeProtocolErrorCode code) noexcept
{
    switch (code) {
    case ChallengeProtocolErrorCode::packet_too_short:
        return "packet_too_short";
    case ChallengeProtocolErrorCode::invalid_header:
        return "invalid_header";
    case ChallengeProtocolErrorCode::payload_too_large:
        return "payload_too_large";
    case ChallengeProtocolErrorCode::unexpected_response_type:
        return "unexpected_response_type";
    case ChallengeProtocolErrorCode::missing_challenge:
        return "missing_challenge";
    case ChallengeProtocolErrorCode::invalid_challenge:
        return "invalid_challenge";
    case ChallengeProtocolErrorCode::challenge_overflow:
        return "challenge_overflow";
    case ChallengeProtocolErrorCode::invalid_terminator:
        return "invalid_terminator";
    case ChallengeProtocolErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    case ChallengeProtocolErrorCode::unsupported_variant:
        return "unsupported_variant";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

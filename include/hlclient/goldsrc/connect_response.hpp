#pragma once

#include <hlclient/network/network_address.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace hlclient::goldsrc {

inline constexpr std::size_t kMaximumConnectResponseDatagramSize = 1'024U;
inline constexpr std::size_t kMaximumConnectRejectMessageSize = 512U;
inline constexpr std::size_t kMaximumConnectRejectPresentationSize = 256U;
inline constexpr std::byte kConnectAcceptedResponseClass{'B'};
inline constexpr std::byte kConnectRejectedResponseClass{'9'};

struct ConnectAccepted {
    std::uint32_t user_id{0U};
    network::NetworkAddress server_view_of_client;
    bool secure{false};
    std::uint32_t server_build{0U};
};

struct ConnectRejected {
    std::string message;
};

using ConnectResponse = std::variant<ConnectAccepted, ConnectRejected>;

enum class ConnectResponseErrorCode {
    packet_too_short,
    invalid_header,
    payload_too_large,
    unknown_response_class,
    missing_user_id,
    invalid_user_id,
    user_id_overflow,
    invalid_separator,
    invalid_quote,
    invalid_client_endpoint,
    invalid_secure_flag,
    missing_server_build,
    invalid_server_build,
    server_build_overflow,
    empty_rejection_message,
    rejection_message_too_large,
    invalid_rejection_message,
    invalid_terminator,
    unexpected_trailing_data,
};

struct ConnectResponseError {
    ConnectResponseErrorCode code{ConnectResponseErrorCode::packet_too_short};
    std::size_t byte_offset{0U};
    std::string context;
};

struct ConnectResponseParseResult {
    std::optional<ConnectResponse> response;
    std::optional<ConnectResponseError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return response.has_value();
    }
};

[[nodiscard]] ConnectResponseParseResult parse_connect_response(
    std::span<const std::byte> datagram);

// Produces a single-line, terminal-safe diagnostic. The caller may request a
// smaller result, but never a result larger than the project presentation cap.
[[nodiscard]] std::string sanitize_connect_rejection_for_presentation(
    std::string_view message,
    std::size_t maximum_output_size = kMaximumConnectRejectPresentationSize);

[[nodiscard]] constexpr std::string_view to_string(
    const ConnectResponseErrorCode code) noexcept
{
    switch (code) {
    case ConnectResponseErrorCode::packet_too_short:
        return "packet_too_short";
    case ConnectResponseErrorCode::invalid_header:
        return "invalid_header";
    case ConnectResponseErrorCode::payload_too_large:
        return "payload_too_large";
    case ConnectResponseErrorCode::unknown_response_class:
        return "unknown_response_class";
    case ConnectResponseErrorCode::missing_user_id:
        return "missing_user_id";
    case ConnectResponseErrorCode::invalid_user_id:
        return "invalid_user_id";
    case ConnectResponseErrorCode::user_id_overflow:
        return "user_id_overflow";
    case ConnectResponseErrorCode::invalid_separator:
        return "invalid_separator";
    case ConnectResponseErrorCode::invalid_quote:
        return "invalid_quote";
    case ConnectResponseErrorCode::invalid_client_endpoint:
        return "invalid_client_endpoint";
    case ConnectResponseErrorCode::invalid_secure_flag:
        return "invalid_secure_flag";
    case ConnectResponseErrorCode::missing_server_build:
        return "missing_server_build";
    case ConnectResponseErrorCode::invalid_server_build:
        return "invalid_server_build";
    case ConnectResponseErrorCode::server_build_overflow:
        return "server_build_overflow";
    case ConnectResponseErrorCode::empty_rejection_message:
        return "empty_rejection_message";
    case ConnectResponseErrorCode::rejection_message_too_large:
        return "rejection_message_too_large";
    case ConnectResponseErrorCode::invalid_rejection_message:
        return "invalid_rejection_message";
    case ConnectResponseErrorCode::invalid_terminator:
        return "invalid_terminator";
    case ConnectResponseErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

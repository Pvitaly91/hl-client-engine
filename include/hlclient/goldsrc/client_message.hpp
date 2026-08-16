#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// Confirmed by repeated stock Protocol 48 capture. The semantic message is
// five bytes; minimum-size netchan padding is transport-owned and is not part
// of this codec.
inline constexpr std::size_t kInitialSignonRequestSize = 5U;
inline constexpr std::size_t kInitialSignonCommandLength = 3U;
inline constexpr std::size_t kMaximumInitialSignonCommandLength = 64U;
inline constexpr std::size_t kMaximumInitialSignonMessageSize =
    1U + kMaximumInitialSignonCommandLength + 1U;
inline constexpr std::size_t kClientMessageDiagnosticTextLimit = 256U;

enum class ClientMessageOpcode : std::uint8_t {
    string_command = 3U,
};

class InitialSignonRequest final {
public:
    [[nodiscard]] constexpr ClientMessageOpcode opcode() const noexcept
    {
        return ClientMessageOpcode::string_command;
    }

    [[nodiscard]] constexpr std::string_view command() const noexcept
    {
        return "new";
    }

private:
    InitialSignonRequest() = default;

    friend struct InitialSignonRequestFactory;
};

enum class ClientMessageErrorCode {
    packet_too_short,
    message_too_large,
    wrong_opcode,
    empty_required_command,
    command_too_large,
    embedded_nul,
    invalid_command_character,
    unsupported_command_variant,
    missing_terminator,
    unexpected_trailing_data,
};

struct ClientMessageError {
    ClientMessageErrorCode code{ClientMessageErrorCode::packet_too_short};
    std::size_t byte_offset{0U};
    std::string context;
};

struct ClientMessageBuildResult {
    std::optional<std::vector<std::byte>> bytes;
    std::optional<ClientMessageError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return bytes.has_value();
    }
};

struct ClientMessageParseResult {
    std::optional<InitialSignonRequest> request;
    std::optional<ClientMessageError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return request.has_value();
    }
};

class InitialSignonRequestBuilder final {
public:
    // This intentionally has no arbitrary-command input surface.
    [[nodiscard]] static ClientMessageBuildResult build();
};

[[nodiscard]] ClientMessageParseResult parse_initial_signon_request(
    std::span<const std::byte> bytes);

[[nodiscard]] constexpr std::string_view to_string(
    const ClientMessageErrorCode code) noexcept
{
    switch (code) {
    case ClientMessageErrorCode::packet_too_short:
        return "packet_too_short";
    case ClientMessageErrorCode::message_too_large:
        return "message_too_large";
    case ClientMessageErrorCode::wrong_opcode:
        return "wrong_opcode";
    case ClientMessageErrorCode::empty_required_command:
        return "empty_required_command";
    case ClientMessageErrorCode::command_too_large:
        return "command_too_large";
    case ClientMessageErrorCode::embedded_nul:
        return "embedded_nul";
    case ClientMessageErrorCode::invalid_command_character:
        return "invalid_command_character";
    case ClientMessageErrorCode::unsupported_command_variant:
        return "unsupported_command_variant";
    case ClientMessageErrorCode::missing_terminator:
        return "missing_terminator";
    case ClientMessageErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

#include <hlclient/goldsrc/client_message.hpp>

#include <array>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::array<std::byte, kInitialSignonRequestSize>
    kInitialSignonRequestBytes{
        std::byte{3U},
        std::byte{'n'},
        std::byte{'e'},
        std::byte{'w'},
        std::byte{0U},
    };

[[nodiscard]] ClientMessageParseResult failure(
    const ClientMessageErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ClientMessageParseResult{
        std::nullopt,
        ClientMessageError{code, byte_offset, std::move(context)},
    };
}

[[nodiscard]] bool forbidden_command_character(const unsigned char value) noexcept
{
    return value == '\r' || value == '\n';
}

} // namespace

struct InitialSignonRequestFactory final {
    [[nodiscard]] static InitialSignonRequest create() noexcept
    {
        return InitialSignonRequest{};
    }
};

ClientMessageBuildResult InitialSignonRequestBuilder::build()
{
    return ClientMessageBuildResult{
        std::vector<std::byte>{
            kInitialSignonRequestBytes.begin(),
            kInitialSignonRequestBytes.end()},
        std::nullopt,
    };
}

ClientMessageParseResult parse_initial_signon_request(
    const std::span<const std::byte> bytes)
{
    if (bytes.empty()) {
        return failure(
            ClientMessageErrorCode::packet_too_short,
            0U,
            "Initial sign-on request has no client-message opcode");
    }
    if (bytes.size() > kMaximumInitialSignonMessageSize) {
        return failure(
            ClientMessageErrorCode::message_too_large,
            kMaximumInitialSignonMessageSize,
            "Initial sign-on client message exceeds the project bound");
    }

    const auto opcode = std::to_integer<std::uint8_t>(bytes.front());
    if (opcode != static_cast<std::uint8_t>(ClientMessageOpcode::string_command)) {
        return failure(
            ClientMessageErrorCode::wrong_opcode,
            0U,
            "Initial sign-on request has the wrong captured client-message opcode");
    }

    std::size_t terminator_offset = bytes.size();
    for (std::size_t offset = 1U; offset < bytes.size(); ++offset) {
        const auto value = std::to_integer<unsigned char>(bytes[offset]);
        if (value == 0U) {
            terminator_offset = offset;
            break;
        }
        if (forbidden_command_character(value)) {
            return failure(
                ClientMessageErrorCode::invalid_command_character,
                offset,
                "Initial sign-on command must not contain CR or LF");
        }
        if (offset > kMaximumInitialSignonCommandLength) {
            return failure(
                ClientMessageErrorCode::command_too_large,
                offset,
                "Initial sign-on command exceeds the project bound");
        }
    }

    if (terminator_offset == bytes.size()) {
        if (bytes.size() - 1U > kMaximumInitialSignonCommandLength) {
            return failure(
                ClientMessageErrorCode::command_too_large,
                1U + kMaximumInitialSignonCommandLength,
                "Initial sign-on command exceeds the project bound");
        }
        return failure(
            ClientMessageErrorCode::missing_terminator,
            bytes.size(),
            "Initial sign-on command has no NUL terminator");
    }
    if (terminator_offset == 1U) {
        return failure(
            ClientMessageErrorCode::empty_required_command,
            terminator_offset,
            "Initial sign-on command is empty");
    }

    const auto command_size = terminator_offset - 1U;
    if (command_size > kMaximumInitialSignonCommandLength) {
        return failure(
            ClientMessageErrorCode::command_too_large,
            1U + kMaximumInitialSignonCommandLength,
            "Initial sign-on command exceeds the project bound");
    }
    if (terminator_offset < kInitialSignonCommandLength + 1U) {
        return failure(
            ClientMessageErrorCode::embedded_nul,
            terminator_offset,
            "Initial sign-on command contains NUL before the captured terminator");
    }

    constexpr std::array<std::byte, kInitialSignonCommandLength> expected_command{
        std::byte{'n'},
        std::byte{'e'},
        std::byte{'w'},
    };
    if (command_size != expected_command.size()) {
        return failure(
            ClientMessageErrorCode::unsupported_command_variant,
            1U,
            "Initial sign-on command does not match the captured fixed profile");
    }
    for (std::size_t index = 0U; index < expected_command.size(); ++index) {
        if (bytes[index + 1U] != expected_command[index]) {
            return failure(
                ClientMessageErrorCode::unsupported_command_variant,
                index + 1U,
                "Initial sign-on command does not match the captured fixed profile");
        }
    }

    if (terminator_offset + 1U != bytes.size()) {
        return failure(
            ClientMessageErrorCode::unexpected_trailing_data,
            terminator_offset + 1U,
            "Client-message codec does not accept transport padding or trailing bytes");
    }

    return ClientMessageParseResult{
        InitialSignonRequestFactory::create(),
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc

#pragma once

#include <hlclient/goldsrc/connect_request.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::uint32_t kSupportedServerInfoProtocolVersion =
    kGoldSrcProtocolVersion;
inline constexpr std::uint8_t kMaximumSupportedServerClients = 32U;

// These are project safety limits, not claims about stock engine buffer sizes.
inline constexpr std::size_t kDefaultMaximumServerInfoStringLength = 1'024U;
inline constexpr std::size_t kMaximumServerInfoStringLength = 4'096U;
inline constexpr std::size_t kServerInfoDiagnosticTextLimit = 256U;

struct ServerInfoLimits {
    std::size_t maximum_string_length{kDefaultMaximumServerInfoStringLength};
};

[[nodiscard]] bool valid_server_info_limits(const ServerInfoLimits& limits) noexcept;

enum class ServerInfoCompatibilityProfile {
    valve_half_life_protocol_48_build_10210,
};

enum class ServerInfoEvidenceProfile {
    differential_stock_capture,
};

class MaximumClients final {
public:
    explicit constexpr MaximumClients(const std::uint8_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint8_t value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const MaximumClients& left,
        const MaximumClients& right) noexcept = default;

private:
    std::uint8_t value_{0U};
};

// Immutable, owning public state. Only differential-capture-confirmed fields
// are exposed. The unconfirmed fixed fields, ordinal candidate, fixed-binary
// field, client-slot candidate, and cursor-only string never leave
// server_info.cpp.
class ServerInfoState final {
public:
    ServerInfoState(const ServerInfoState&) = default;
    ServerInfoState& operator=(const ServerInfoState&) = delete;
    ServerInfoState(ServerInfoState&&) noexcept = default;
    ServerInfoState& operator=(ServerInfoState&&) noexcept = delete;
    ~ServerInfoState() = default;

    [[nodiscard]] ProtocolVersion protocol_version() const noexcept;
    [[nodiscard]] MaximumClients maximum_clients() const noexcept;
    [[nodiscard]] bool multi_client_mode() const noexcept;

    [[nodiscard]] const std::string& game_directory() const noexcept;
    [[nodiscard]] const std::string& server_label() const noexcept;

    // Untrusted metadata only. This value must not be used for filesystem,
    // URL, shell, asset, or renderer operations in M2.4.2.
    [[nodiscard]] const std::string& map_file_path() const noexcept;

    [[nodiscard]] ServerInfoCompatibilityProfile compatibility_profile() const noexcept;
    [[nodiscard]] ServerInfoEvidenceProfile evidence_profile() const noexcept;

private:
    friend class ServerInfoParser;

    ServerInfoState(
        ProtocolVersion protocol_version,
        MaximumClients maximum_clients,
        bool multi_client_mode,
        std::string game_directory,
        std::string server_label,
        std::string map_file_path) noexcept;

    ProtocolVersion protocol_version_;
    MaximumClients maximum_clients_;
    bool multi_client_mode_{false};
    std::string game_directory_;
    std::string server_label_;
    std::string map_file_path_;
};

enum class ServerInfoErrorCode {
    invalid_configuration,
    empty_body,
    truncated_fixed_field,
    unsupported_protocol,
    invalid_maximum_clients,
    unterminated_string_field,
    string_field_too_long,
    invalid_profile_flag,
    inconsistent_multi_client_mode,
    invalid_reserved_value,
    size_overflow,
};

struct ServerInfoError {
    ServerInfoErrorCode code{ServerInfoErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::string context;
};

struct ServerInfoParseResult {
    std::optional<ServerInfoState> state;
    std::optional<ServerInfoError> error;
    // Relative to the first byte after the opcode-11 service byte. This is
    // zero on failure, so no caller can publish a partially advanced cursor.
    std::size_t bytes_consumed{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class ServerInfoParser final {
public:
    explicit ServerInfoParser(ServerInfoLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ServerInfoLimits& limits() const noexcept;

    // `body` starts immediately after opcode 11 and may include subsequent
    // service messages. Success consumes exactly the confirmed server-info
    // body prefix and leaves all following bytes to the service decoder.
    [[nodiscard]] ServerInfoParseResult parse(
        std::span<const std::byte> body) const;

private:
    ServerInfoLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ServerInfoErrorCode code) noexcept
{
    switch (code) {
    case ServerInfoErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ServerInfoErrorCode::empty_body:
        return "empty_body";
    case ServerInfoErrorCode::truncated_fixed_field:
        return "truncated_fixed_field";
    case ServerInfoErrorCode::unsupported_protocol:
        return "unsupported_protocol";
    case ServerInfoErrorCode::invalid_maximum_clients:
        return "invalid_maximum_clients";
    case ServerInfoErrorCode::unterminated_string_field:
        return "unterminated_string_field";
    case ServerInfoErrorCode::string_field_too_long:
        return "string_field_too_long";
    case ServerInfoErrorCode::invalid_profile_flag:
        return "invalid_profile_flag";
    case ServerInfoErrorCode::inconsistent_multi_client_mode:
        return "inconsistent_multi_client_mode";
    case ServerInfoErrorCode::invalid_reserved_value:
        return "invalid_reserved_value";
    case ServerInfoErrorCode::size_overflow:
        return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

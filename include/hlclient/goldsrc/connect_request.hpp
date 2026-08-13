#pragma once

#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/info_string.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint32_t kGoldSrcProtocolVersion = 48U;
inline constexpr std::size_t kMaximumConnectDatagramSize = 1'400U;
inline constexpr std::size_t kMaximumProtocolInfoSerializedSize = 255U;
inline constexpr std::size_t kMaximumUserInfoSerializedSize = 255U;
inline constexpr std::size_t kMaximumConnectProtectedAuthenticationSize = 127U;
inline constexpr std::size_t kMaximumConnectAuthenticationSuffixSize = 1'200U;
inline constexpr std::size_t kMaximumPlayerNameLength = 31U;
inline constexpr std::size_t kMaximumPlayerModelLength = 31U;

inline constexpr std::size_t kObservedConnectProtocolInfoSize = 66U;
inline constexpr std::size_t kObservedConnectUserInfoSize = 179U;
inline constexpr std::size_t kObservedConnectAuthenticationSuffixSize = 213U;
inline constexpr std::size_t kObservedMaximumConnectDatagramSize = 490U;

[[nodiscard]] constexpr InfoStringLimits connect_protocol_info_limits() noexcept
{
    return InfoStringLimits{63U, 127U, 16U, kMaximumProtocolInfoSerializedSize};
}

[[nodiscard]] constexpr InfoStringLimits connect_user_info_limits() noexcept
{
    return InfoStringLimits{63U, 127U, 32U, kMaximumUserInfoSerializedSize};
}

enum class ProtocolVersion : std::uint32_t {
    goldsrc_48 = kGoldSrcProtocolVersion,
};

struct ConnectCompatibilityProfile {
    std::size_t maximum_datagram_size{kMaximumConnectDatagramSize};
    std::size_t required_protected_authentication_size{32U};
    std::size_t required_binary_authentication_size{kObservedConnectAuthenticationSuffixSize};
    bool protected_authentication_is_ascii_hex{true};
};

struct ClientConnectionSettings {
    std::string bottom_color{"6"};
    std::string automatic_weapon_switch{"1"};
    std::string maximum_download_size{"1024"};
    std::string client_lag_compensation{"1"};
    std::string client_weapon_prediction{"1"};
    std::string update_rate{"102"};
    std::string hud_class_auto_kill{"1"};
    std::string model{"ivan"};
    std::string display_name{"Player"};
    std::string top_color{"30"};
    std::string observed_parameter_esevcmmx{"0"};
    std::string observed_parameter_gm{"3154"};
    std::string vgui_menus{"0"};
    std::string rate{"25000"};
};

enum class ConnectRequestErrorCode {
    invalid_configuration,
    missing_protocol_info,
    missing_user_info,
    invalid_protocol_info,
    invalid_user_info,
    missing_authentication,
    invalid_authentication,
    authentication_too_large,
    packet_too_short,
    invalid_connectionless_header,
    packet_too_large,
    unexpected_command,
    missing_protocol,
    invalid_protocol,
    missing_challenge,
    invalid_challenge,
    missing_protocol_info_argument,
    missing_user_info_argument,
    invalid_quote,
    invalid_terminator,
    unexpected_trailing_data,
    size_overflow,
};

struct ConnectRequestError {
    ConnectRequestErrorCode code{ConnectRequestErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::string context;
};

class AuthenticationMaterial;
struct AuthenticationMaterialCreateResult;

class AuthenticationMaterial final {
public:
    [[nodiscard]] static AuthenticationMaterialCreateResult create(
        std::span<const std::byte> protected_info_value,
        std::span<const std::byte> binary_suffix);

    ~AuthenticationMaterial() = default;
    AuthenticationMaterial(AuthenticationMaterial&&) noexcept = default;
    AuthenticationMaterial& operator=(AuthenticationMaterial&&) noexcept = default;
    AuthenticationMaterial(const AuthenticationMaterial&) = delete;
    AuthenticationMaterial& operator=(const AuthenticationMaterial&) = delete;

    [[nodiscard]] std::size_t protected_info_size() const noexcept;
    [[nodiscard]] std::size_t binary_suffix_size() const noexcept;
    [[nodiscard]] std::size_t total_size() const noexcept;
    [[nodiscard]] bool protected_info_is_ascii_hex() const noexcept;
    [[nodiscard]] bool matches(
        std::span<const std::byte> protected_info_value,
        std::span<const std::byte> binary_suffix) const noexcept;

private:
    AuthenticationMaterial(std::string protected_info_value, std::vector<std::byte> binary_suffix);

    std::string protected_info_value_;
    std::vector<std::byte> binary_suffix_;

    friend class ConnectRequestBuilder;
    friend class PreparedConnectRequest;
};

struct AuthenticationMaterialCreateResult {
    std::optional<AuthenticationMaterial> value;
    std::optional<ConnectRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept;
};

[[nodiscard]] std::string format_authentication_redaction(std::size_t byte_count);

class ProtocolInfo final {
public:
    ProtocolInfo(const ProtocolInfo&) = default;
    ProtocolInfo& operator=(const ProtocolInfo&) = default;
    ProtocolInfo(ProtocolInfo&&) noexcept = default;
    ProtocolInfo& operator=(ProtocolInfo&&) noexcept = default;

    [[nodiscard]] const InfoString& value() const noexcept;

private:
    explicit ProtocolInfo(InfoString value) noexcept;
    InfoString value_;

    friend struct ProtocolInfoFactory;
};

class UserInfo final {
public:
    UserInfo(const UserInfo&) = default;
    UserInfo& operator=(const UserInfo&) = default;
    UserInfo(UserInfo&&) noexcept = default;
    UserInfo& operator=(UserInfo&&) noexcept = default;

    [[nodiscard]] const InfoString& value() const noexcept;

private:
    explicit UserInfo(InfoString value) noexcept;
    InfoString value_;

    friend struct UserInfoFactory;
};

struct ProtocolInfoBuildResult {
    std::optional<ProtocolInfo> value;
    std::optional<ConnectRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

struct UserInfoBuildResult {
    std::optional<UserInfo> value;
    std::optional<ConnectRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

[[nodiscard]] ProtocolInfoBuildResult build_stock_protocol_info();
[[nodiscard]] UserInfoBuildResult build_stock_user_info(
    const ClientConnectionSettings& settings);

class ConnectRequest final {
public:
    ConnectRequest(
        ProtocolVersion protocol,
        ChallengeToken challenge,
        ProtocolInfo protocol_info,
        UserInfo user_info,
        AuthenticationMaterial authentication) noexcept;

    ConnectRequest(ConnectRequest&&) noexcept = default;
    ConnectRequest& operator=(ConnectRequest&&) noexcept = default;
    ConnectRequest(const ConnectRequest&) = delete;
    ConnectRequest& operator=(const ConnectRequest&) = delete;

    [[nodiscard]] ProtocolVersion protocol() const noexcept;
    [[nodiscard]] ChallengeToken challenge() const noexcept;
    [[nodiscard]] const ProtocolInfo& protocol_info() const noexcept;
    [[nodiscard]] const UserInfo& user_info() const noexcept;
    [[nodiscard]] std::size_t authentication_size() const noexcept;
    [[nodiscard]] std::size_t authentication_suffix_size() const noexcept;
    [[nodiscard]] bool authentication_matches(
        std::span<const std::byte> protected_info_value,
        std::span<const std::byte> binary_suffix) const noexcept;

private:
    ProtocolVersion protocol_;
    ChallengeToken challenge_;
    ProtocolInfo protocol_info_;
    UserInfo user_info_;
    AuthenticationMaterial authentication_;

    friend class ConnectRequestBuilder;
};

struct ConnectRequestBuildResult {
    std::optional<std::vector<std::byte>> datagram;
    std::optional<ConnectRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return datagram.has_value();
    }
};

struct ConnectRequestParseResult {
    std::optional<ConnectRequest> request;
    std::optional<ConnectRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return request.has_value();
    }
};

class ConnectRequestBuilder final {
public:
    [[nodiscard]] static ConnectRequestBuildResult build(
        const ConnectRequest& request,
        const ConnectCompatibilityProfile& profile = {});
};

[[nodiscard]] ConnectRequestParseResult parse_connect_request(
    std::span<const std::byte> datagram,
    const ConnectCompatibilityProfile& profile = {});

[[nodiscard]] bool checked_connect_request_size_add(
    std::size_t left,
    std::size_t right,
    std::size_t& result) noexcept;

class PreparedConnectRequest final {
public:
    PreparedConnectRequest(PreparedConnectRequest&&) noexcept = default;
    PreparedConnectRequest& operator=(PreparedConnectRequest&&) noexcept = default;
    PreparedConnectRequest(const PreparedConnectRequest&) = delete;
    PreparedConnectRequest& operator=(const PreparedConnectRequest&) = delete;

    [[nodiscard]] ConnectRequest make_request(ChallengeToken challenge) && noexcept;
    [[nodiscard]] const ProtocolInfo& protocol_info() const noexcept;
    [[nodiscard]] const UserInfo& user_info() const noexcept;
    [[nodiscard]] std::size_t authentication_size() const noexcept;
    [[nodiscard]] std::size_t authentication_suffix_size() const noexcept;
    [[nodiscard]] std::size_t protocol_info_wire_size() const noexcept;
    [[nodiscard]] const ConnectCompatibilityProfile& profile() const noexcept;

private:
    PreparedConnectRequest(
        ProtocolInfo protocol_info,
        UserInfo user_info,
        AuthenticationMaterial authentication,
        ConnectCompatibilityProfile profile) noexcept;

    ProtocolInfo protocol_info_;
    UserInfo user_info_;
    AuthenticationMaterial authentication_;
    ConnectCompatibilityProfile profile_;

    friend struct PreparedConnectRequestFactory;
};

struct PrepareConnectRequestResult {
    std::optional<PreparedConnectRequest> value;
    std::optional<ConnectRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

[[nodiscard]] PrepareConnectRequestResult prepare_connect_request(
    const ClientConnectionSettings& settings,
    AuthenticationMaterial authentication,
    const ConnectCompatibilityProfile& profile = {});

[[nodiscard]] constexpr std::string_view to_string(const ConnectRequestErrorCode code) noexcept
{
    switch (code) {
    case ConnectRequestErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ConnectRequestErrorCode::missing_protocol_info:
        return "missing_protocol_info";
    case ConnectRequestErrorCode::missing_user_info:
        return "missing_user_info";
    case ConnectRequestErrorCode::invalid_protocol_info:
        return "invalid_protocol_info";
    case ConnectRequestErrorCode::invalid_user_info:
        return "invalid_user_info";
    case ConnectRequestErrorCode::missing_authentication:
        return "missing_authentication";
    case ConnectRequestErrorCode::invalid_authentication:
        return "invalid_authentication";
    case ConnectRequestErrorCode::authentication_too_large:
        return "authentication_too_large";
    case ConnectRequestErrorCode::packet_too_short:
        return "packet_too_short";
    case ConnectRequestErrorCode::invalid_connectionless_header:
        return "invalid_connectionless_header";
    case ConnectRequestErrorCode::packet_too_large:
        return "packet_too_large";
    case ConnectRequestErrorCode::unexpected_command:
        return "unexpected_command";
    case ConnectRequestErrorCode::missing_protocol:
        return "missing_protocol";
    case ConnectRequestErrorCode::invalid_protocol:
        return "invalid_protocol";
    case ConnectRequestErrorCode::missing_challenge:
        return "missing_challenge";
    case ConnectRequestErrorCode::invalid_challenge:
        return "invalid_challenge";
    case ConnectRequestErrorCode::missing_protocol_info_argument:
        return "missing_protocol_info_argument";
    case ConnectRequestErrorCode::missing_user_info_argument:
        return "missing_user_info_argument";
    case ConnectRequestErrorCode::invalid_quote:
        return "invalid_quote";
    case ConnectRequestErrorCode::invalid_terminator:
        return "invalid_terminator";
    case ConnectRequestErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    case ConnectRequestErrorCode::size_overflow:
        return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

#include <hlclient/goldsrc/connect_request.hpp>

#include <hlclient/goldsrc/connectionless_packet.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <ranges>
#include <system_error>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::string_view kConnectPrefix = "connect ";
inline constexpr std::string_view kProtocolVersionText = "48";
inline constexpr std::string_view kProtectedAuthenticationKey = "cdkey";
inline constexpr std::string_view kProtectedAuthenticationPlaceholder =
    "PENDING_AUTHENTICATION_MATERIAL";

[[nodiscard]] ConnectRequestError make_error(
    const ConnectRequestErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ConnectRequestError{code, byte_offset, std::move(context)};
}

template<class Result>
[[nodiscard]] Result failure(
    const ConnectRequestErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return Result{std::nullopt, make_error(code, byte_offset, std::move(context))};
}

[[nodiscard]] bool valid_protected_auth_character(const unsigned char value) noexcept
{
    return value >= 0x20U && value <= 0x7eU && value != '\\' && value != '"' && value != ';';
}

[[nodiscard]] bool ascii_hex(const unsigned char value) noexcept
{
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') ||
           (value >= 'a' && value <= 'f');
}

[[nodiscard]] bool valid_info_value_character(const unsigned char value) noexcept
{
    return value >= 0x20U && value <= 0x7eU && value != '\\' && value != '"' &&
           value != ';';
}

[[nodiscard]] bool valid_profile(const ConnectCompatibilityProfile& profile) noexcept
{
    return profile.maximum_datagram_size >= 1U &&
           profile.maximum_datagram_size <= kMaximumConnectDatagramSize &&
           profile.required_protected_authentication_size > 0U &&
           profile.required_protected_authentication_size <=
               kMaximumConnectProtectedAuthenticationSize &&
           profile.required_binary_authentication_size > 0U &&
           profile.required_binary_authentication_size <=
               kMaximumConnectAuthenticationSuffixSize;
}

[[nodiscard]] std::string signed_challenge_text(const ChallengeToken challenge)
{
    const auto signed_value = std::bit_cast<std::int32_t>(challenge);
    std::array<char, std::numeric_limits<std::int32_t>::digits10 + 4U> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), signed_value, 10);
    if (result.ec != std::errc{}) {
        return {};
    }
    return std::string{buffer.data(), result.ptr};
}

[[nodiscard]] bool parse_signed_challenge(
    const std::string_view text,
    ChallengeToken& challenge) noexcept
{
    if (text.empty() || text.front() == '+' || (text.size() > 1U && text.front() == '0') ||
        text == "-0" || (text.size() > 2U && text.starts_with("-0"))) {
        return false;
    }
    std::int32_t signed_value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), signed_value, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    challenge = std::bit_cast<std::uint32_t>(signed_value);
    return true;
}

[[nodiscard]] const InfoStringEntry* find_entry(
    const InfoString& info,
    const std::string_view key) noexcept
{
    const auto iterator = std::ranges::find(
        info.entries(), key, &InfoStringEntry::key);
    return iterator == info.entries().end() ? nullptr : &*iterator;
}

[[nodiscard]] bool has_keys_in_exact_order(
    const InfoString& info,
    const std::span<const std::string_view> expected) noexcept
{
    if (info.entries().size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (info.entries()[index].key != expected[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<ConnectRequestError> validate_protocol_info(
    const InfoString& info,
    const std::size_t offset)
{
    constexpr std::array keys{
        std::string_view{"prot"},
        std::string_view{"unique"},
        std::string_view{"raw"},
        kProtectedAuthenticationKey,
    };
    if (!has_keys_in_exact_order(info, keys)) {
        return make_error(
            ConnectRequestErrorCode::invalid_protocol_info,
            offset,
            "Protocol-info keys do not match the captured stock order");
    }
    if (info.entries()[0].value != "3" || info.entries()[1].value != "-1" ||
        info.entries()[2].value != "steam") {
        return make_error(
            ConnectRequestErrorCode::invalid_protocol_info,
            offset,
            "Protocol-info fixed values do not match the captured stock profile");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ConnectRequestError> validate_user_info(
    const InfoString& info,
    const std::size_t offset)
{
    constexpr std::array keys{
        std::string_view{"bottomcolor"},
        std::string_view{"cl_autowepswitch"},
        std::string_view{"cl_dlmax"},
        std::string_view{"cl_lc"},
        std::string_view{"cl_lw"},
        std::string_view{"cl_updaterate"},
        std::string_view{"hud_classautokill"},
        std::string_view{"model"},
        std::string_view{"name"},
        std::string_view{"topcolor"},
        std::string_view{"esevcmmx"},
        std::string_view{"_gm"},
        std::string_view{"_vgui_menus"},
        std::string_view{"rate"},
    };
    if (!has_keys_in_exact_order(info, keys)) {
        return make_error(
            ConnectRequestErrorCode::invalid_user_info,
            offset,
            "User-info keys do not match the captured stock order");
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view bytes_as_text(const std::span<const std::byte> bytes) noexcept
{
    return std::string_view{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
}

[[nodiscard]] std::vector<std::byte> text_as_bytes(const std::string_view text)
{
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

[[nodiscard]] ConnectRequestError from_info_error(
    const InfoStringError& error,
    const ConnectRequestErrorCode code,
    const std::size_t base_offset)
{
    return make_error(code, base_offset + error.byte_offset, error.context);
}

[[nodiscard]] bool compute_packet_size(
    const std::size_t challenge_size,
    const std::size_t protocol_info_size,
    const std::size_t user_info_size,
    const std::size_t authentication_suffix_size,
    std::size_t& result) noexcept
{
    // Header + "connect " + "48 " + challenge + SP + two quoted arguments,
    // the one inter-argument SP, and the undelimited binary suffix.
    result = kConnectionlessPacketHeaderSize;
    return checked_connect_request_size_add(result, kConnectPrefix.size(), result) &&
           checked_connect_request_size_add(result, kProtocolVersionText.size() + 1U, result) &&
           checked_connect_request_size_add(result, challenge_size + 1U, result) &&
           checked_connect_request_size_add(result, protocol_info_size + 2U, result) &&
           checked_connect_request_size_add(result, 1U, result) &&
           checked_connect_request_size_add(result, user_info_size + 2U, result) &&
           checked_connect_request_size_add(result, authentication_suffix_size, result);
}

} // namespace

AuthenticationMaterialCreateResult::operator bool() const noexcept
{
    return value.has_value();
}

AuthenticationMaterial::AuthenticationMaterial(
    std::string protected_info_value,
    std::vector<std::byte> binary_suffix)
    : protected_info_value_{std::move(protected_info_value)},
      binary_suffix_{std::move(binary_suffix)}
{
}

AuthenticationMaterialCreateResult AuthenticationMaterial::create(
    const std::span<const std::byte> protected_info_value,
    const std::span<const std::byte> binary_suffix)
{
    if (protected_info_value.empty() || binary_suffix.empty()) {
        return failure<AuthenticationMaterialCreateResult>(
            ConnectRequestErrorCode::missing_authentication,
            0U,
            "Both protected authentication regions must be present");
    }
    if (protected_info_value.size() > kMaximumConnectProtectedAuthenticationSize ||
        binary_suffix.size() > kMaximumConnectAuthenticationSuffixSize) {
        return failure<AuthenticationMaterialCreateResult>(
            ConnectRequestErrorCode::authentication_too_large,
            0U,
            "Authentication material exceeds the bounded project profile");
    }
    if (!std::ranges::all_of(protected_info_value, [](const std::byte byte) {
            return valid_protected_auth_character(std::to_integer<unsigned char>(byte));
        })) {
        return failure<AuthenticationMaterialCreateResult>(
            ConnectRequestErrorCode::invalid_authentication,
            0U,
            "Protected authentication value contains a forbidden byte");
    }

    return AuthenticationMaterialCreateResult{
        AuthenticationMaterial{
            std::string{bytes_as_text(protected_info_value)},
            std::vector<std::byte>{binary_suffix.begin(), binary_suffix.end()}},
        std::nullopt,
    };
}

std::size_t AuthenticationMaterial::protected_info_size() const noexcept
{
    return protected_info_value_.size();
}

std::size_t AuthenticationMaterial::binary_suffix_size() const noexcept
{
    return binary_suffix_.size();
}

std::size_t AuthenticationMaterial::total_size() const noexcept
{
    return protected_info_value_.size() + binary_suffix_.size();
}

bool AuthenticationMaterial::protected_info_is_ascii_hex() const noexcept
{
    return std::ranges::all_of(protected_info_value_, [](const char value) {
        return ascii_hex(static_cast<unsigned char>(value));
    });
}

bool AuthenticationMaterial::matches(
    const std::span<const std::byte> protected_info_value,
    const std::span<const std::byte> binary_suffix) const noexcept
{
    if (protected_info_value.size() != protected_info_value_.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < protected_info_value.size(); ++index) {
        if (std::to_integer<unsigned char>(protected_info_value[index]) !=
            static_cast<unsigned char>(protected_info_value_[index])) {
            return false;
        }
    }
    return std::ranges::equal(binary_suffix_, binary_suffix);
}

std::string format_authentication_redaction(const std::size_t byte_count)
{
    return "<redacted:" + std::to_string(byte_count) + " bytes>";
}

struct ProtocolInfoFactory final {
    [[nodiscard]] static ProtocolInfo create(InfoString value) noexcept
    {
        return ProtocolInfo{std::move(value)};
    }
};

struct UserInfoFactory final {
    [[nodiscard]] static UserInfo create(InfoString value) noexcept
    {
        return UserInfo{std::move(value)};
    }
};

ProtocolInfo::ProtocolInfo(InfoString value) noexcept : value_{std::move(value)} {}

const InfoString& ProtocolInfo::value() const noexcept
{
    return value_;
}

UserInfo::UserInfo(InfoString value) noexcept : value_{std::move(value)} {}

const InfoString& UserInfo::value() const noexcept
{
    return value_;
}

ProtocolInfoBuildResult build_stock_protocol_info()
{
    const std::array entries{
        InfoStringEntry{"prot", "3"},
        InfoStringEntry{"unique", "-1"},
        InfoStringEntry{"raw", "steam"},
        InfoStringEntry{"cdkey", std::string{kProtectedAuthenticationPlaceholder}},
    };
    auto built = build_info_string(entries, connect_protocol_info_limits());
    if (!built) {
        return ProtocolInfoBuildResult{
            std::nullopt,
            built.error ? std::optional{from_info_error(
                                           *built.error,
                                           ConnectRequestErrorCode::invalid_protocol_info,
                                           0U)}
                        : std::optional{make_error(
                              ConnectRequestErrorCode::invalid_protocol_info,
                              0U,
                              "Unable to build stock protocol-info")},
        };
    }
    return ProtocolInfoBuildResult{
        ProtocolInfoFactory::create(std::move(*built.value)),
        std::nullopt,
    };
}

UserInfoBuildResult build_stock_user_info(const ClientConnectionSettings& settings)
{
    if (settings.display_name.size() > kMaximumPlayerNameLength ||
        settings.model.size() > kMaximumPlayerModelLength) {
        return failure<UserInfoBuildResult>(
            ConnectRequestErrorCode::invalid_user_info,
            0U,
            "Player name or model exceeds the stock-profile project bound");
    }

    constexpr std::array keys{
        std::string_view{"bottomcolor"}, std::string_view{"cl_autowepswitch"},
        std::string_view{"cl_dlmax"}, std::string_view{"cl_lc"},
        std::string_view{"cl_lw"}, std::string_view{"cl_updaterate"},
        std::string_view{"hud_classautokill"}, std::string_view{"model"},
        std::string_view{"name"}, std::string_view{"topcolor"},
        std::string_view{"esevcmmx"}, std::string_view{"_gm"},
        std::string_view{"_vgui_menus"}, std::string_view{"rate"},
    };
    const std::array values{
        std::string_view{settings.bottom_color},
        std::string_view{settings.automatic_weapon_switch},
        std::string_view{settings.maximum_download_size},
        std::string_view{settings.client_lag_compensation},
        std::string_view{settings.client_weapon_prediction},
        std::string_view{settings.update_rate},
        std::string_view{settings.hud_class_auto_kill},
        std::string_view{settings.model},
        std::string_view{settings.display_name},
        std::string_view{settings.top_color},
        std::string_view{settings.observed_parameter_esevcmmx},
        std::string_view{settings.observed_parameter_gm},
        std::string_view{settings.vgui_menus},
        std::string_view{settings.rate},
    };
    std::size_t serialized_size = 0U;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto value = values[index];
        if (value.empty() || value.size() > connect_user_info_limits().maximum_value_length ||
            !std::ranges::all_of(value, [](const char character) {
                return valid_info_value_character(static_cast<unsigned char>(character));
            })) {
            return failure<UserInfoBuildResult>(
                ConnectRequestErrorCode::invalid_user_info,
                index,
                "User-info value violates the bounded printable-ASCII policy");
        }
        std::size_t entry_size = 2U;
        if (!checked_connect_request_size_add(entry_size, keys[index].size(), entry_size) ||
            !checked_connect_request_size_add(entry_size, value.size(), entry_size) ||
            !checked_connect_request_size_add(serialized_size, entry_size, serialized_size) ||
            serialized_size > connect_user_info_limits().maximum_serialized_length) {
            return failure<UserInfoBuildResult>(
                ConnectRequestErrorCode::invalid_user_info,
                index,
                "User-info exceeds the bounded stock-profile size");
        }
    }

    std::vector<InfoStringEntry> entries;
    entries.reserve(keys.size());
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        entries.push_back(InfoStringEntry{std::string{keys[index]}, std::string{values[index]}});
    }
    auto built = build_info_string(entries, connect_user_info_limits());
    if (!built) {
        return UserInfoBuildResult{
            std::nullopt,
            built.error ? std::optional{from_info_error(
                                           *built.error,
                                           ConnectRequestErrorCode::invalid_user_info,
                                           0U)}
                        : std::optional{make_error(
                              ConnectRequestErrorCode::invalid_user_info,
                              0U,
                              "Unable to build stock user-info")},
        };
    }
    return UserInfoBuildResult{
        UserInfoFactory::create(std::move(*built.value)),
        std::nullopt,
    };
}

ConnectRequest::ConnectRequest(
    const ProtocolVersion protocol,
    const ChallengeToken challenge,
    ProtocolInfo protocol_info,
    UserInfo user_info,
    AuthenticationMaterial authentication) noexcept
    : protocol_{protocol},
      challenge_{challenge},
      protocol_info_{std::move(protocol_info)},
      user_info_{std::move(user_info)},
      authentication_{std::move(authentication)}
{
}

ProtocolVersion ConnectRequest::protocol() const noexcept { return protocol_; }
ChallengeToken ConnectRequest::challenge() const noexcept { return challenge_; }
const ProtocolInfo& ConnectRequest::protocol_info() const noexcept { return protocol_info_; }
const UserInfo& ConnectRequest::user_info() const noexcept { return user_info_; }
std::size_t ConnectRequest::authentication_size() const noexcept
{
    return authentication_.total_size();
}
std::size_t ConnectRequest::authentication_suffix_size() const noexcept
{
    return authentication_.binary_suffix_size();
}
bool ConnectRequest::authentication_matches(
    const std::span<const std::byte> protected_info_value,
    const std::span<const std::byte> binary_suffix) const noexcept
{
    return authentication_.matches(protected_info_value, binary_suffix);
}

ConnectRequestBuildResult ConnectRequestBuilder::build(
    const ConnectRequest& request,
    const ConnectCompatibilityProfile& profile)
{
    if (!valid_profile(profile)) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::invalid_configuration,
            0U,
            "Connect compatibility profile is invalid");
    }
    if (request.protocol_ != ProtocolVersion::goldsrc_48) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::invalid_protocol,
            0U,
            "Only the captured GoldSrc protocol 48 profile is supported");
    }
    if (request.authentication_.protected_info_size() !=
            profile.required_protected_authentication_size ||
        request.authentication_.binary_suffix_size() !=
            profile.required_binary_authentication_size) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::invalid_authentication,
            0U,
            "Authentication region lengths do not match the selected profile");
    }
    if (profile.protected_authentication_is_ascii_hex &&
        !std::ranges::all_of(request.authentication_.protected_info_value_, [](const char value) {
            return ascii_hex(static_cast<unsigned char>(value));
        })) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::invalid_authentication,
            0U,
            "Protected authentication value does not match the captured ASCII-hex encoding");
    }
    if (auto error = validate_protocol_info(request.protocol_info_.value(), 0U)) {
        return ConnectRequestBuildResult{std::nullopt, std::move(error)};
    }
    if (auto error = validate_user_info(request.user_info_.value(), 0U)) {
        return ConnectRequestBuildResult{std::nullopt, std::move(error)};
    }

    const auto challenge_text = signed_challenge_text(request.challenge_);
    if (challenge_text.empty()) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::invalid_challenge,
            0U,
            "Unable to format the 32-bit challenge token");
    }

    std::string protocol_text{request.protocol_info_.value().serialized()};
    const auto* authentication_slot = find_entry(
        request.protocol_info_.value(), kProtectedAuthenticationKey);
    if (authentication_slot == nullptr) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::missing_authentication,
            0U,
            "Protocol-info has no protected authentication slot");
    }
    const auto slot_offset = protocol_text.rfind(authentication_slot->value);
    if (slot_offset == std::string::npos) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::invalid_protocol_info,
            0U,
            "Protocol-info authentication slot is inconsistent");
    }
    protocol_text.replace(
        slot_offset,
        authentication_slot->value.size(),
        request.authentication_.protected_info_value_);
    if (protocol_text.size() > kMaximumProtocolInfoSerializedSize) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::invalid_protocol_info,
            0U,
            "Protocol-info exceeds the selected project bound");
    }

    std::size_t packet_size = 0U;
    if (!compute_packet_size(
            challenge_text.size(),
            protocol_text.size(),
            request.user_info_.value().serialized_size(),
            request.authentication_.binary_suffix_size(),
            packet_size)) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::size_overflow,
            0U,
            "Connect request size calculation overflowed");
    }
    if (packet_size > profile.maximum_datagram_size) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::packet_too_large,
            profile.maximum_datagram_size,
            "Connect request exceeds the selected project packet bound");
    }

    std::string payload;
    payload.reserve(packet_size - kConnectionlessPacketHeaderSize);
    payload += kConnectPrefix;
    payload += kProtocolVersionText;
    payload.push_back(' ');
    payload += challenge_text;
    payload += " \"";
    payload += protocol_text;
    payload += "\" \"";
    payload += request.user_info_.value().serialized();
    payload.push_back('"');

    auto payload_bytes = text_as_bytes(payload);
    payload_bytes.insert(
        payload_bytes.end(),
        request.authentication_.binary_suffix_.begin(),
        request.authentication_.binary_suffix_.end());
    auto encoded = encode_connectionless_packet(payload_bytes, profile.maximum_datagram_size);
    if (!encoded) {
        return failure<ConnectRequestBuildResult>(
            ConnectRequestErrorCode::packet_too_large,
            profile.maximum_datagram_size,
            "Unable to encode the bounded connect request envelope");
    }
    return ConnectRequestBuildResult{std::move(encoded.datagram), std::nullopt};
}

ConnectRequestParseResult parse_connect_request(
    const std::span<const std::byte> datagram,
    const ConnectCompatibilityProfile& profile)
{
    if (!valid_profile(profile)) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_configuration,
            0U,
            "Connect compatibility profile is invalid");
    }
    if (datagram.size() > profile.maximum_datagram_size) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::packet_too_large,
            profile.maximum_datagram_size,
            "Connect request exceeds the selected project packet bound");
    }
    if (datagram.size() < kConnectionlessPacketHeaderSize + kConnectPrefix.size()) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::packet_too_short,
            datagram.size(),
            "Connect request is too short");
    }
    const auto envelope = parse_connectionless_packet(datagram, profile.maximum_datagram_size);
    if (!envelope) {
        const auto code = envelope.error &&
                                  envelope.error->code == ConnectionlessPacketErrorCode::invalid_header
                              ? ConnectRequestErrorCode::invalid_connectionless_header
                              : ConnectRequestErrorCode::packet_too_short;
        return failure<ConnectRequestParseResult>(
            code,
            envelope.error ? envelope.error->byte_offset : 0U,
            envelope.error ? envelope.error->context : "Invalid connectionless envelope");
    }

    const auto payload = std::span<const std::byte>{envelope.packet->payload};
    const auto text = bytes_as_text(payload);
    std::size_t position = 0U;
    if (!text.starts_with(kConnectPrefix)) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::unexpected_command,
            kConnectionlessPacketHeaderSize,
            "Connectionless request does not begin with the exact connect command");
    }
    position = kConnectPrefix.size();

    const auto protocol_end = text.find(' ', position);
    if (protocol_end == std::string_view::npos) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::missing_protocol,
            kConnectionlessPacketHeaderSize + position,
            "Connect request has no protocol argument");
    }
    if (text.substr(position, protocol_end - position) != kProtocolVersionText) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_protocol,
            kConnectionlessPacketHeaderSize + position,
            "Only exact decimal protocol 48 is supported");
    }
    position = protocol_end + 1U;

    const auto challenge_end = text.find(' ', position);
    if (challenge_end == std::string_view::npos || challenge_end == position) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::missing_challenge,
            kConnectionlessPacketHeaderSize + position,
            "Connect request has no challenge argument");
    }
    ChallengeToken challenge = 0U;
    if (!parse_signed_challenge(text.substr(position, challenge_end - position), challenge)) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_challenge,
            kConnectionlessPacketHeaderSize + position,
            "Connect challenge is not a canonical signed decimal int32 bit pattern");
    }
    position = challenge_end + 1U;

    if (position >= text.size() || text[position] != '"') {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::missing_protocol_info_argument,
            kConnectionlessPacketHeaderSize + position,
            "Connect request is missing its quoted protocol-info argument");
    }
    const auto protocol_quote_end = text.find('"', position + 1U);
    if (protocol_quote_end == std::string_view::npos) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_quote,
            kConnectionlessPacketHeaderSize + position,
            "Protocol-info quote is unterminated");
    }
    const auto protocol_text = text.substr(position + 1U, protocol_quote_end - position - 1U);
    position = protocol_quote_end + 1U;
    if (position + 1U >= text.size() || text[position] != ' ' || text[position + 1U] != '"') {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::missing_user_info_argument,
            kConnectionlessPacketHeaderSize + position,
            "Connect request is missing its quoted user-info argument");
    }
    position += 2U;
    const auto user_quote_end = text.find('"', position);
    if (user_quote_end == std::string_view::npos) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_quote,
            kConnectionlessPacketHeaderSize + position,
            "User-info quote is unterminated");
    }
    const auto user_text = text.substr(position, user_quote_end - position);
    position = user_quote_end + 1U;

    if (payload.size() - position != profile.required_binary_authentication_size) {
        return failure<ConnectRequestParseResult>(
            payload.size() - position < profile.required_binary_authentication_size
                ? ConnectRequestErrorCode::invalid_terminator
                : ConnectRequestErrorCode::unexpected_trailing_data,
            kConnectionlessPacketHeaderSize + position,
            "Binary authentication suffix length does not match the selected profile");
    }

    auto parsed_protocol = parse_info_string(protocol_text, connect_protocol_info_limits());
    if (!parsed_protocol) {
        return ConnectRequestParseResult{
            std::nullopt,
            parsed_protocol.error
                ? std::optional{from_info_error(
                      *parsed_protocol.error,
                      ConnectRequestErrorCode::invalid_protocol_info,
                      kConnectionlessPacketHeaderSize + (protocol_text.data() - text.data()))}
                : std::optional{make_error(
                      ConnectRequestErrorCode::invalid_protocol_info,
                      kConnectionlessPacketHeaderSize,
                      "Unable to parse protocol-info")},
        };
    }
    if (auto error = validate_protocol_info(*parsed_protocol.value, 0U)) {
        return ConnectRequestParseResult{std::nullopt, std::move(error)};
    }
    auto parsed_user = parse_info_string(user_text, connect_user_info_limits());
    if (!parsed_user) {
        return ConnectRequestParseResult{
            std::nullopt,
            parsed_user.error
                ? std::optional{from_info_error(
                      *parsed_user.error,
                      ConnectRequestErrorCode::invalid_user_info,
                      kConnectionlessPacketHeaderSize + (user_text.data() - text.data()))}
                : std::optional{make_error(
                      ConnectRequestErrorCode::invalid_user_info,
                      kConnectionlessPacketHeaderSize,
                      "Unable to parse user-info")},
        };
    }
    if (auto error = validate_user_info(*parsed_user.value, 0U)) {
        return ConnectRequestParseResult{std::nullopt, std::move(error)};
    }

    const auto* protected_entry = find_entry(*parsed_protocol.value, kProtectedAuthenticationKey);
    if (protected_entry == nullptr) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::missing_authentication,
            0U,
            "Protocol-info has no protected authentication slot");
    }
    const auto protected_bytes = std::as_bytes(
        std::span{protected_entry->value.data(), protected_entry->value.size()});
    const auto binary_suffix = payload.subspan(position);
    auto authentication = AuthenticationMaterial::create(protected_bytes, binary_suffix);
    if (!authentication) {
        return ConnectRequestParseResult{std::nullopt, std::move(authentication.error)};
    }
    if (authentication.value->protected_info_size() !=
            profile.required_protected_authentication_size ||
        authentication.value->binary_suffix_size() !=
            profile.required_binary_authentication_size) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_authentication,
            0U,
            "Authentication region lengths do not match the selected profile");
    }
    if (profile.protected_authentication_is_ascii_hex &&
        !std::ranges::all_of(protected_entry->value, [](const char value) {
            return ascii_hex(static_cast<unsigned char>(value));
        })) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_authentication,
            0U,
            "Protected authentication value does not match the captured ASCII-hex encoding");
    }

    // The strict parser must never make protected authentication bytes observable
    // through the public ProtocolInfo API. AuthenticationMaterial owns the real
    // value; ProtocolInfo retains only the validated structure and a redacted slot.
    auto sanitized_protocol_entries = std::vector<InfoStringEntry>{
        parsed_protocol.value->entries().begin(), parsed_protocol.value->entries().end()};
    const auto sanitized_protected_entry = std::ranges::find(
        sanitized_protocol_entries,
        kProtectedAuthenticationKey,
        &InfoStringEntry::key);
    if (sanitized_protected_entry == sanitized_protocol_entries.end()) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::missing_authentication,
            0U,
            "Protocol-info has no protected authentication slot");
    }
    sanitized_protected_entry->value = kProtectedAuthenticationPlaceholder;
    auto sanitized_protocol = build_info_string(
        sanitized_protocol_entries, connect_protocol_info_limits());
    if (!sanitized_protocol) {
        return failure<ConnectRequestParseResult>(
            ConnectRequestErrorCode::invalid_protocol_info,
            0U,
            "Unable to retain sanitized protocol-info metadata");
    }

    return ConnectRequestParseResult{
        ConnectRequest{
            ProtocolVersion::goldsrc_48,
            challenge,
            ProtocolInfoFactory::create(std::move(*sanitized_protocol.value)),
            UserInfoFactory::create(std::move(*parsed_user.value)),
            std::move(*authentication.value)},
        std::nullopt,
    };
}

bool checked_connect_request_size_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

struct PreparedConnectRequestFactory final {
    [[nodiscard]] static PreparedConnectRequest create(
        ProtocolInfo protocol_info,
        UserInfo user_info,
        AuthenticationMaterial authentication,
        const ConnectCompatibilityProfile& profile) noexcept
    {
        return PreparedConnectRequest{
            std::move(protocol_info),
            std::move(user_info),
            std::move(authentication),
            profile};
    }
};

PreparedConnectRequest::PreparedConnectRequest(
    ProtocolInfo protocol_info,
    UserInfo user_info,
    AuthenticationMaterial authentication,
    const ConnectCompatibilityProfile profile) noexcept
    : protocol_info_{std::move(protocol_info)},
      user_info_{std::move(user_info)},
      authentication_{std::move(authentication)},
      profile_{profile}
{
}

ConnectRequest PreparedConnectRequest::make_request(const ChallengeToken challenge) && noexcept
{
    return ConnectRequest{
        ProtocolVersion::goldsrc_48,
        challenge,
        std::move(protocol_info_),
        std::move(user_info_),
        std::move(authentication_)};
}

const ProtocolInfo& PreparedConnectRequest::protocol_info() const noexcept { return protocol_info_; }
const UserInfo& PreparedConnectRequest::user_info() const noexcept { return user_info_; }
std::size_t PreparedConnectRequest::authentication_size() const noexcept
{
    return authentication_.total_size();
}
std::size_t PreparedConnectRequest::authentication_suffix_size() const noexcept
{
    return authentication_.binary_suffix_size();
}
std::size_t PreparedConnectRequest::protocol_info_wire_size() const noexcept
{
    const auto* slot = find_entry(protocol_info_.value(), kProtectedAuthenticationKey);
    if (slot == nullptr || slot->value.size() > protocol_info_.value().serialized_size()) {
        return 0U;
    }
    return protocol_info_.value().serialized_size() - slot->value.size() +
           authentication_.protected_info_size();
}
const ConnectCompatibilityProfile& PreparedConnectRequest::profile() const noexcept
{
    return profile_;
}

PrepareConnectRequestResult prepare_connect_request(
    const ClientConnectionSettings& settings,
    AuthenticationMaterial authentication,
    const ConnectCompatibilityProfile& profile)
{
    if (!valid_profile(profile) ||
        authentication.protected_info_size() != profile.required_protected_authentication_size ||
        authentication.binary_suffix_size() != profile.required_binary_authentication_size) {
        return failure<PrepareConnectRequestResult>(
            ConnectRequestErrorCode::invalid_configuration,
            0U,
            "Authentication lengths or connect compatibility profile are invalid");
    }
    if (profile.protected_authentication_is_ascii_hex &&
        !authentication.protected_info_is_ascii_hex()) {
        return failure<PrepareConnectRequestResult>(
            ConnectRequestErrorCode::invalid_authentication,
            0U,
            "Protected authentication value does not match the captured ASCII-hex encoding");
    }

    auto protocol_info = build_stock_protocol_info();
    if (!protocol_info) {
        return PrepareConnectRequestResult{std::nullopt, std::move(protocol_info.error)};
    }
    auto user_info = build_stock_user_info(settings);
    if (!user_info) {
        return PrepareConnectRequestResult{std::nullopt, std::move(user_info.error)};
    }

    std::size_t preflight_size = 0U;
    if (!compute_packet_size(
            11U,
            protocol_info.value->value().serialized_size() -
                kProtectedAuthenticationPlaceholder.size() +
                authentication.protected_info_size(),
            user_info.value->value().serialized_size(),
            authentication.binary_suffix_size(),
            preflight_size)) {
        return failure<PrepareConnectRequestResult>(
            ConnectRequestErrorCode::size_overflow,
            0U,
            "Connect request preflight size calculation overflowed");
    }
    if (preflight_size > profile.maximum_datagram_size) {
        return failure<PrepareConnectRequestResult>(
            ConnectRequestErrorCode::packet_too_large,
            profile.maximum_datagram_size,
            "Prepared connect request exceeds the selected project packet bound");
    }

    return PrepareConnectRequestResult{
        PreparedConnectRequestFactory::create(
            std::move(*protocol_info.value),
            std::move(*user_info.value),
            std::move(authentication),
            profile),
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc

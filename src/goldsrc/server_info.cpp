#include <hlclient/goldsrc/server_info.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::size_t kOpaqueFixedBinaryWidth = 16U;

enum class BoundedStringFailure {
    none,
    unterminated,
    too_long,
    size_overflow,
};

struct BoundedStringResult {
    std::optional<std::string> value;
    BoundedStringFailure failure{BoundedStringFailure::none};
    std::size_t error_offset{0U};
};

[[nodiscard]] ServerInfoParseResult failure(
    const ServerInfoErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ServerInfoParseResult{
        std::nullopt,
        ServerInfoError{code, byte_offset, std::move(context)},
        0U,
    };
}

[[nodiscard]] BoundedStringResult read_bounded_string(
    ByteReader& reader,
    const std::span<const std::byte> body,
    const std::size_t maximum_length)
{
    const auto begin = reader.position();
    if (maximum_length == (std::numeric_limits<std::size_t>::max)()) {
        return BoundedStringResult{
            std::nullopt,
            BoundedStringFailure::size_overflow,
            begin,
        };
    }

    const auto available = reader.remaining();
    const auto scan_count = std::min(available, maximum_length + 1U);
    std::optional<std::size_t> length;
    for (std::size_t index = 0U; index < scan_count; ++index) {
        if (body[begin + index] == std::byte{0U}) {
            length = index;
            break;
        }
    }

    if (!length) {
        if (available > maximum_length) {
            return BoundedStringResult{
                std::nullopt,
                BoundedStringFailure::too_long,
                begin + maximum_length,
            };
        }
        return BoundedStringResult{
            std::nullopt,
            BoundedStringFailure::unterminated,
            body.size(),
        };
    }

    const auto wire = reader.read_bytes(*length + 1U);
    if (!wire) {
        return BoundedStringResult{
            std::nullopt,
            BoundedStringFailure::size_overflow,
            begin,
        };
    }
    const auto* text = reinterpret_cast<const char*>(wire->data());
    return BoundedStringResult{
        std::string{text, *length},
        BoundedStringFailure::none,
        0U,
    };
}

[[nodiscard]] std::optional<ServerInfoParseResult> string_failure(
    const BoundedStringResult& result)
{
    switch (result.failure) {
    case BoundedStringFailure::none:
        return std::nullopt;
    case BoundedStringFailure::unterminated:
        return failure(
            ServerInfoErrorCode::unterminated_string_field,
            result.error_offset,
            "Server-info string field has no NUL terminator within its bound");
    case BoundedStringFailure::too_long:
        return failure(
            ServerInfoErrorCode::string_field_too_long,
            result.error_offset,
            "Server-info string field exceeds the configured project bound");
    case BoundedStringFailure::size_overflow:
        return failure(
            ServerInfoErrorCode::size_overflow,
            result.error_offset,
            "Server-info string cursor arithmetic overflowed");
    }
    return failure(
        ServerInfoErrorCode::size_overflow,
        result.error_offset,
        "Server-info string parser returned an unknown failure");
}

} // namespace

bool valid_server_info_limits(const ServerInfoLimits& limits) noexcept
{
    return limits.maximum_string_length > 0U &&
           limits.maximum_string_length <= kMaximumServerInfoStringLength;
}

ServerInfoState::ServerInfoState(
    const ProtocolVersion protocol_version,
    const MaximumClients maximum_clients,
    const bool multi_client_mode,
    std::string game_directory,
    std::string server_label,
    std::string map_file_path) noexcept
    : protocol_version_{protocol_version},
      maximum_clients_{maximum_clients},
      multi_client_mode_{multi_client_mode},
      game_directory_{std::move(game_directory)},
      server_label_{std::move(server_label)},
      map_file_path_{std::move(map_file_path)}
{
}

ProtocolVersion ServerInfoState::protocol_version() const noexcept
{
    return protocol_version_;
}

MaximumClients ServerInfoState::maximum_clients() const noexcept
{
    return maximum_clients_;
}

bool ServerInfoState::multi_client_mode() const noexcept
{
    return multi_client_mode_;
}

const std::string& ServerInfoState::game_directory() const noexcept
{
    return game_directory_;
}

const std::string& ServerInfoState::server_label() const noexcept
{
    return server_label_;
}

const std::string& ServerInfoState::map_file_path() const noexcept
{
    return map_file_path_;
}

ServerInfoCompatibilityProfile ServerInfoState::compatibility_profile() const noexcept
{
    return ServerInfoCompatibilityProfile::valve_half_life_protocol_48_build_10210;
}

ServerInfoEvidenceProfile ServerInfoState::evidence_profile() const noexcept
{
    return ServerInfoEvidenceProfile::differential_stock_capture;
}

ServerInfoParser::ServerInfoParser(ServerInfoLimits limits) noexcept
    : limits_{limits}
{
}

bool ServerInfoParser::valid_configuration() const noexcept
{
    return valid_server_info_limits(limits_);
}

const ServerInfoLimits& ServerInfoParser::limits() const noexcept
{
    return limits_;
}

ServerInfoParseResult ServerInfoParser::parse(
    const std::span<const std::byte> body) const
{
    if (!valid_configuration()) {
        return failure(
            ServerInfoErrorCode::invalid_configuration,
            0U,
            "Server-info limits are outside project hard caps");
    }
    if (body.empty()) {
        return failure(
            ServerInfoErrorCode::empty_body,
            0U,
            "Server-info body is empty");
    }

    ByteReader reader{body};
    const auto protocol = reader.read_uint32_le();
    if (!protocol) {
        return failure(
            ServerInfoErrorCode::truncated_fixed_field,
            reader.position(),
            "Server-info protocol field is truncated");
    }
    if (*protocol != kSupportedServerInfoProtocolVersion) {
        return failure(
            ServerInfoErrorCode::unsupported_protocol,
            0U,
            "Server-info protocol does not match the supported stock profile");
    }

    // A controlled same-process map start changed this ordinal candidate from
    // 1 to 2, but one differential is not enough to publish or validate a
    // server/session semantic. Preserve only its confirmed width and cursor.
    if (!reader.read_uint32_le()) {
        return failure(
            ServerInfoErrorCode::truncated_fixed_field,
            reader.position(),
            "Server-info opaque ordinal candidate is truncated");
    }

    // One fixed little-endian field and one fixed-width binary field have a
    // confirmed width and position, but not enough evidence for public names.
    if (!reader.read_uint32_le() || !reader.read_bytes(kOpaqueFixedBinaryWidth)) {
        return failure(
            ServerInfoErrorCode::truncated_fixed_field,
            reader.position(),
            "Server-info opaque fixed-width prefix is truncated");
    }

    const auto maximum_clients = reader.read_uint8();
    if (!maximum_clients) {
        return failure(
            ServerInfoErrorCode::truncated_fixed_field,
            reader.position(),
            "Server-info maximum-clients field is truncated");
    }
    if (*maximum_clients == 0U || *maximum_clients > kMaximumSupportedServerClients) {
        return failure(
            ServerInfoErrorCode::invalid_maximum_clients,
            reader.position() - 1U,
            "Server-info maximum-clients value is outside the supported 1..32 range");
    }

    // Second-client probes never reached canonical getchallenge, so this byte
    // was observed only as zero. Its width is confirmed, but its semantics and
    // range are not; consume it without publication or validation.
    if (!reader.read_uint8()) {
        return failure(
            ServerInfoErrorCode::truncated_fixed_field,
            reader.position(),
            "Server-info opaque one-byte field is truncated");
    }

    const auto profile_flag = reader.read_uint8();
    if (!profile_flag) {
        return failure(
            ServerInfoErrorCode::truncated_fixed_field,
            reader.position(),
            "Server-info profile flag is truncated");
    }
    if (*profile_flag > 1U) {
        return failure(
            ServerInfoErrorCode::invalid_profile_flag,
            reader.position() - 1U,
            "Server-info profile flag must be one of the two captured values");
    }
    const auto multi_client_mode = *profile_flag != 0U;
    if (multi_client_mode != (*maximum_clients > 1U)) {
        return failure(
            ServerInfoErrorCode::inconsistent_multi_client_mode,
            reader.position() - 1U,
            "Server-info multi-client mode is inconsistent with maximum clients");
    }

    // All four NUL-terminated field boundaries are confirmed. Launch-profile,
    // hostname, and map differentials independently confirm the first three
    // public metadata meanings; the fourth remains cursor-only and cpp-private.
    auto game_directory = read_bounded_string(reader, body, limits_.maximum_string_length);
    if (const auto error = string_failure(game_directory)) {
        return *error;
    }
    auto server_label = read_bounded_string(reader, body, limits_.maximum_string_length);
    if (const auto error = string_failure(server_label)) {
        return *error;
    }
    auto map_file_path = read_bounded_string(reader, body, limits_.maximum_string_length);
    if (const auto error = string_failure(map_file_path)) {
        return *error;
    }
    auto opaque_string_4 = read_bounded_string(reader, body, limits_.maximum_string_length);
    if (const auto error = string_failure(opaque_string_4)) {
        return *error;
    }

    const auto reserved = reader.read_uint8();
    if (!reserved) {
        return failure(
            ServerInfoErrorCode::truncated_fixed_field,
            reader.position(),
            "Server-info final reserved byte is truncated");
    }
    if (*reserved != 0U) {
        return failure(
            ServerInfoErrorCode::invalid_reserved_value,
            reader.position() - 1U,
            "Server-info final reserved byte must match the captured zero value");
    }

    return ServerInfoParseResult{
        ServerInfoState{
            ProtocolVersion::goldsrc_48,
            MaximumClients{*maximum_clients},
            multi_client_mode,
            std::move(*game_directory.value),
            std::move(*server_label.value),
            std::move(*map_file_path.value),
        },
        std::nullopt,
        reader.position(),
    };
}

} // namespace hlclient::goldsrc

#include <hlclient/goldsrc/user_info_update.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::size_t kWirePrefixBytes = 1U + 1U + 4U;
inline constexpr std::size_t kWireTerminatorBytes = 1U;
inline constexpr std::size_t kWireFixedMessageBytes =
    kWirePrefixBytes + kWireTerminatorBytes + kUserInfoOpaqueSuffixSize;

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool supported_profile(
    const UserInfoUpdateCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case UserInfoUpdateCompatibilityProfile::
        valve_half_life_protocol_48_build_10210:
        return true;
    }
    return false;
}

[[nodiscard]] char ascii_fold(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value - 'A' + 'a')
               : value;
}

[[nodiscard]] bool keys_equal_case_insensitive(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_fold(left[index]) != ascii_fold(right[index])) {
            return false;
        }
    }
    return true;
}

struct ParsedInfoString {
    std::vector<std::string> keys;
    std::vector<std::string> values;
    std::vector<UserInfoSafeFieldMetadata> safe_fields;
    std::optional<std::size_t> player_name_length;
    std::optional<std::size_t> player_model_length;
};

struct ParsedMessage {
    std::uint8_t client_index{0U};
    std::int32_t private_user_id{0};
    ParsedInfoString info;
    std::array<std::byte, kUserInfoOpaqueSuffixSize> opaque_suffix{};
    std::size_t info_string_length{0U};
    std::size_t message_bytes{0U};
};

struct PrefixParseResult {
    std::optional<ParsedMessage> message;
    std::optional<UserInfoUpdateError> error;
};

[[nodiscard]] UserInfoUpdateError make_error(
    const UserInfoUpdateErrorCode code,
    const std::size_t byte_offset,
    const std::optional<UserInfoStringErrorCode> info_string_code,
    std::string context)
{
    return UserInfoUpdateError{
        code,
        byte_offset,
        info_string_code,
        std::move(context),
    };
}

[[nodiscard]] PrefixParseResult prefix_failure(
    const UserInfoUpdateErrorCode code,
    const std::size_t byte_offset,
    std::string context,
    const std::optional<UserInfoStringErrorCode> info_string_code = std::nullopt)
{
    return PrefixParseResult{
        std::nullopt,
        make_error(
            code,
            byte_offset,
            info_string_code,
            std::move(context)),
    };
}

[[nodiscard]] std::optional<UserInfoUpdateError> parse_private_info_string(
    const std::span<const std::byte> bytes,
    const std::size_t source_offset,
    const UserInfoUpdateLimits& limits,
    ParsedInfoString& parsed)
{
    if (bytes.empty() || bytes.front() != std::byte{'\\'}) {
        return make_error(
            UserInfoUpdateErrorCode::invalid_info_string,
            source_offset,
            UserInfoStringErrorCode::missing_leading_separator,
            "User-info byte string must begin with a backslash separator");
    }

    parsed.keys.reserve(std::min(
        limits.maximum_userinfo_entries,
        bytes.size() / 4U + 1U));
    parsed.values.reserve(parsed.keys.capacity());
    parsed.safe_fields.reserve(4U);

    std::size_t position = 1U;
    while (position < bytes.size()) {
        if (parsed.keys.size() >= limits.maximum_userinfo_entries) {
            return make_error(
                UserInfoUpdateErrorCode::invalid_info_string,
                source_offset + position,
                UserInfoStringErrorCode::too_many_entries,
                "User-info entry count exceeds the configured project bound");
        }

        const auto key_begin = position;
        const auto key_terminator = std::find(
            bytes.begin() + static_cast<std::ptrdiff_t>(key_begin),
            bytes.end(),
            std::byte{'\\'});
        if (key_terminator == bytes.end()) {
            return make_error(
                UserInfoUpdateErrorCode::invalid_info_string,
                source_offset + bytes.size(),
                UserInfoStringErrorCode::malformed_key_value_sequence,
                "User-info key has no value separator");
        }
        const auto key_end = static_cast<std::size_t>(
            std::distance(bytes.begin(), key_terminator));
        if (key_end == key_begin) {
            return make_error(
                UserInfoUpdateErrorCode::invalid_info_string,
                source_offset + key_begin,
                UserInfoStringErrorCode::empty_key,
                "User-info keys must not be empty");
        }
        const auto key_length = key_end - key_begin;
        if (key_length > limits.maximum_userinfo_key_length) {
            return make_error(
                UserInfoUpdateErrorCode::invalid_info_string,
                source_offset + key_begin + limits.maximum_userinfo_key_length,
                UserInfoStringErrorCode::key_too_long,
                "User-info key exceeds the configured project bound");
        }

        const auto value_begin = key_end + 1U;
        if (value_begin >= bytes.size()) {
            return make_error(
                UserInfoUpdateErrorCode::invalid_info_string,
                source_offset + value_begin,
                UserInfoStringErrorCode::empty_value,
                "User-info values must not be empty in the bounded profile");
        }
        const auto value_terminator = std::find(
            bytes.begin() + static_cast<std::ptrdiff_t>(value_begin),
            bytes.end(),
            std::byte{'\\'});
        const auto value_end = value_terminator == bytes.end()
                                   ? bytes.size()
                                   : static_cast<std::size_t>(
                                         std::distance(bytes.begin(), value_terminator));
        if (value_end == value_begin) {
            return make_error(
                UserInfoUpdateErrorCode::invalid_info_string,
                source_offset + value_begin,
                UserInfoStringErrorCode::empty_value,
                "User-info values must not be empty in the bounded profile");
        }
        const auto value_length = value_end - value_begin;
        if (value_length > limits.maximum_userinfo_value_length) {
            return make_error(
                UserInfoUpdateErrorCode::invalid_info_string,
                source_offset + value_begin + limits.maximum_userinfo_value_length,
                UserInfoStringErrorCode::value_too_long,
                "User-info value exceeds the configured project bound");
        }

        const auto* key_characters = reinterpret_cast<const char*>(
            bytes.data() + key_begin);
        const auto* value_characters = reinterpret_cast<const char*>(
            bytes.data() + value_begin);
        std::string key{key_characters, key_length};
        std::string value{value_characters, value_length};

        for (const auto& previous : parsed.keys) {
            if (keys_equal_case_insensitive(previous, key)) {
                return make_error(
                    UserInfoUpdateErrorCode::invalid_info_string,
                    source_offset + key_begin,
                    UserInfoStringErrorCode::duplicate_key,
                    "User-info keys must be unique under ASCII case folding");
            }
        }

        const auto entry_index = parsed.keys.size();
        const auto add_safe_field = [&](const UserInfoSafeField field) {
            parsed.safe_fields.push_back(UserInfoSafeFieldMetadata{
                field,
                entry_index,
                value_length,
            });
        };
        if (key == "name") {
            parsed.player_name_length = value_length;
            add_safe_field(UserInfoSafeField::player_name);
        } else if (key == "model") {
            parsed.player_model_length = value_length;
            add_safe_field(UserInfoSafeField::player_model);
        } else if (key == "topcolor") {
            add_safe_field(UserInfoSafeField::top_color);
        } else if (key == "bottomcolor") {
            add_safe_field(UserInfoSafeField::bottom_color);
        }

        parsed.keys.push_back(std::move(key));
        parsed.values.push_back(std::move(value));
        if (value_terminator == bytes.end()) {
            position = bytes.size();
        } else {
            position = value_end + 1U;
            if (position == bytes.size()) {
                return make_error(
                    UserInfoUpdateErrorCode::invalid_info_string,
                    source_offset + value_end,
                    UserInfoStringErrorCode::malformed_key_value_sequence,
                    "User-info byte string must not end with a separator");
            }
        }
    }

    if (parsed.keys.empty()) {
        return make_error(
            UserInfoUpdateErrorCode::invalid_info_string,
            source_offset,
            UserInfoStringErrorCode::malformed_key_value_sequence,
            "User-info byte string must contain at least one key-value pair");
    }
    return std::nullopt;
}

[[nodiscard]] PrefixParseResult parse_message_prefix(
    const std::span<const std::byte> service_payload,
    const std::size_t opcode_offset,
    const UserInfoUpdateLimits& limits)
{
    if (opcode_offset >= service_payload.size()) {
        return prefix_failure(
            UserInfoUpdateErrorCode::empty_input,
            opcode_offset,
            "User-info message is missing its opcode byte");
    }
    const auto opcode =
        std::to_integer<std::uint8_t>(service_payload[opcode_offset]);
    if (opcode != kUserInfoUpdateOpcode) {
        return prefix_failure(
            UserInfoUpdateErrorCode::wrong_opcode,
            opcode_offset,
            "User-info parser requires exact opcode 13");
    }

    std::size_t body_offset = 0U;
    if (!checked_add(opcode_offset, 1U, body_offset)) {
        return prefix_failure(
            UserInfoUpdateErrorCode::size_overflow,
            opcode_offset,
            "User-info body cursor overflowed");
    }
    ByteReader reader{service_payload.subspan(body_offset)};
    const auto client_index = reader.read_uint8();
    if (!client_index) {
        return prefix_failure(
            UserInfoUpdateErrorCode::truncated_client_index,
            body_offset,
            "User-info zero-based client-index byte is truncated");
    }
    if (*client_index > kMaximumUserInfoClientIndex) {
        return prefix_failure(
            UserInfoUpdateErrorCode::invalid_client_index,
            body_offset,
            "User-info client index exceeds the Protocol 48 client range");
    }
    const auto private_user_id_wire = reader.read_uint32_le();
    if (!private_user_id_wire) {
        return prefix_failure(
            UserInfoUpdateErrorCode::truncated_user_id,
            body_offset + reader.position(),
            "User-info private user-ID field is truncated");
    }
    if (*private_user_id_wire == 0U ||
        *private_user_id_wire > static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)())) {
        return prefix_failure(
            UserInfoUpdateErrorCode::invalid_user_id,
            body_offset + 1U,
            "User-info private user ID is outside the confirmed positive int32 profile");
    }

    std::size_t info_offset = 0U;
    if (!checked_add(body_offset, reader.position(), info_offset)) {
        return prefix_failure(
            UserInfoUpdateErrorCode::size_overflow,
            body_offset,
            "User-info string cursor overflowed");
    }
    const auto available = service_payload.size() - info_offset;
    const auto scan_count = std::min(
        available,
        limits.maximum_userinfo_string_size + 1U);
    std::optional<std::size_t> info_length;
    for (std::size_t index = 0U; index < scan_count; ++index) {
        if (service_payload[info_offset + index] == std::byte{0U}) {
            info_length = index;
            break;
        }
    }
    if (!info_length) {
        return prefix_failure(
            available > limits.maximum_userinfo_string_size
                ? UserInfoUpdateErrorCode::info_string_too_large
                : UserInfoUpdateErrorCode::unterminated_info_string,
            available > limits.maximum_userinfo_string_size
                ? info_offset + limits.maximum_userinfo_string_size
                : service_payload.size(),
            available > limits.maximum_userinfo_string_size
                ? "User-info byte string exceeds the configured project bound"
                : "User-info byte string has no in-bound NUL terminator");
    }

    std::size_t message_bytes = 0U;
    if (!checked_add(kWireFixedMessageBytes, *info_length, message_bytes)) {
        return prefix_failure(
            UserInfoUpdateErrorCode::size_overflow,
            opcode_offset,
            "User-info message size overflowed");
    }
    if (message_bytes > limits.maximum_userinfo_message_size) {
        return prefix_failure(
            UserInfoUpdateErrorCode::message_too_large,
            opcode_offset + limits.maximum_userinfo_message_size,
            "User-info message exceeds the configured project bound");
    }

    std::size_t suffix_offset = 0U;
    if (!checked_add(info_offset, *info_length + 1U, suffix_offset)) {
        return prefix_failure(
            UserInfoUpdateErrorCode::size_overflow,
            info_offset,
            "User-info suffix cursor overflowed");
    }
    if (kUserInfoOpaqueSuffixSize > service_payload.size() - suffix_offset) {
        return prefix_failure(
            UserInfoUpdateErrorCode::truncated_opaque_suffix,
            service_payload.size(),
            "User-info fixed opaque suffix is truncated");
    }

    ParsedInfoString parsed_info;
    if (auto error = parse_private_info_string(
            service_payload.subspan(info_offset, *info_length),
            info_offset,
            limits,
            parsed_info)) {
        return PrefixParseResult{std::nullopt, std::move(error)};
    }

    ParsedMessage parsed;
    parsed.client_index = *client_index;
    parsed.private_user_id = static_cast<std::int32_t>(*private_user_id_wire);
    parsed.info = std::move(parsed_info);
    std::copy_n(
        service_payload.begin() + static_cast<std::ptrdiff_t>(suffix_offset),
        kUserInfoOpaqueSuffixSize,
        parsed.opaque_suffix.begin());
    parsed.info_string_length = *info_length;
    parsed.message_bytes = message_bytes;
    return PrefixParseResult{std::move(parsed), std::nullopt};
}

[[nodiscard]] UserInfoUpdateParseResult parser_failure(
    UserInfoUpdateError error)
{
    return UserInfoUpdateParseResult{
        std::nullopt,
        std::move(error),
        0U,
        0U,
    };
}

[[nodiscard]] UserInfoUpdateStreamDecodeResult stream_failure(
    const UserInfoUpdateStreamErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::uint8_t> wire_opcode,
    const std::optional<UserInfoUpdateErrorCode> parser_code,
    const std::optional<UserInfoStringErrorCode> info_string_code,
    std::string context)
{
    return UserInfoUpdateStreamDecodeResult{
        std::nullopt,
        UserInfoUpdateStreamError{
            code,
            byte_offset,
            wire_opcode,
            parser_code,
            info_string_code,
            std::move(context),
        },
        0U,
    };
}

} // namespace

bool valid_user_info_update_limits(
    const UserInfoUpdateLimits& limits) noexcept
{
    return limits.maximum_userinfo_message_size >= kWireFixedMessageBytes &&
           limits.maximum_userinfo_message_size <=
               kMaximumUserInfoMessageSize &&
           limits.maximum_userinfo_string_size > 0U &&
           limits.maximum_userinfo_string_size <=
               kMaximumUserInfoStringSize &&
           limits.maximum_userinfo_key_length > 0U &&
           limits.maximum_userinfo_key_length <=
               kMaximumUserInfoKeyLength &&
           limits.maximum_userinfo_key_length <=
               limits.maximum_userinfo_string_size &&
           limits.maximum_userinfo_value_length > 0U &&
           limits.maximum_userinfo_value_length <=
               kMaximumUserInfoValueLength &&
           limits.maximum_userinfo_value_length <=
               limits.maximum_userinfo_string_size &&
           limits.maximum_userinfo_entries > 0U &&
           limits.maximum_userinfo_entries <= kMaximumUserInfoEntries &&
           limits.maximum_userinfo_messages_per_batch > 0U &&
           limits.maximum_userinfo_messages_per_batch <=
               kMaximumUserInfoMessagesPerBatch &&
           limits.maximum_userinfo_total_bytes >=
               limits.maximum_userinfo_message_size &&
           limits.maximum_userinfo_total_bytes <=
               kMaximumUserInfoTotalBytes;
}

UserInfoUpdateState::UserInfoUpdateState(
    const std::uint8_t client_index,
    const std::int32_t private_user_id,
    std::vector<std::string> private_keys,
    std::vector<std::string> private_values,
    std::vector<UserInfoSafeFieldMetadata> safe_fields,
    const std::optional<std::size_t> player_name_length,
    const std::optional<std::size_t> player_model_length,
    std::array<std::byte, kUserInfoOpaqueSuffixSize> opaque_suffix,
    const std::size_t info_string_length,
    const std::size_t source_message_offset,
    const std::size_t message_bytes,
    const UserInfoUpdateCompatibilityProfile compatibility_profile) noexcept
    : client_index_{client_index},
      private_user_id_{private_user_id},
      private_keys_{std::move(private_keys)},
      private_values_{std::move(private_values)},
      safe_fields_{std::move(safe_fields)},
      player_name_length_{player_name_length},
      player_model_length_{player_model_length},
      opaque_suffix_{opaque_suffix},
      info_string_length_{info_string_length},
      source_message_offset_{source_message_offset},
      message_bytes_{message_bytes},
      compatibility_profile_{compatibility_profile}
{
}

std::uint8_t UserInfoUpdateState::client_index() const noexcept { return client_index_; }
std::size_t UserInfoUpdateState::info_string_length() const noexcept { return info_string_length_; }
std::size_t UserInfoUpdateState::info_entry_count() const noexcept { return private_keys_.size(); }
std::span<const UserInfoSafeFieldMetadata> UserInfoUpdateState::safe_fields() const noexcept { return safe_fields_; }
std::optional<std::size_t> UserInfoUpdateState::player_name_length() const noexcept { return player_name_length_; }
std::optional<std::size_t> UserInfoUpdateState::player_model_length() const noexcept { return player_model_length_; }
bool UserInfoUpdateState::has_private_user_id() const noexcept { return true; }
std::size_t UserInfoUpdateState::opaque_suffix_size() const noexcept { return opaque_suffix_.size(); }
std::size_t UserInfoUpdateState::source_message_offset() const noexcept { return source_message_offset_; }
std::size_t UserInfoUpdateState::body_bytes() const noexcept { return message_bytes_ - 1U; }
std::size_t UserInfoUpdateState::message_bytes() const noexcept { return message_bytes_; }
UserInfoUpdateCompatibilityProfile UserInfoUpdateState::compatibility_profile() const noexcept { return compatibility_profile_; }
UserInfoUpdateEvidenceProfile UserInfoUpdateState::evidence_profile() const noexcept { return UserInfoUpdateEvidenceProfile::stock_capture_and_public_valve_header; }

UserInfoUpdateParser::UserInfoUpdateParser(
    UserInfoUpdateLimits limits,
    const UserInfoUpdateCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool UserInfoUpdateParser::valid_configuration() const noexcept
{
    return valid_user_info_update_limits(limits_) && supported_profile(profile_);
}

const UserInfoUpdateLimits& UserInfoUpdateParser::limits() const noexcept
{
    return limits_;
}

UserInfoUpdateParseResult UserInfoUpdateParser::parse(
    const std::span<const std::byte> message) const
{
    if (!valid_configuration()) {
        return parser_failure(make_error(
            UserInfoUpdateErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "User-info limits or compatibility profile are unsupported"));
    }

    auto parsed = parse_message_prefix(message, 0U, limits_);
    if (!parsed.message) {
        return parser_failure(std::move(*parsed.error));
    }
    if (parsed.message->message_bytes != message.size()) {
        return parser_failure(make_error(
            UserInfoUpdateErrorCode::unexpected_trailing_bytes,
            parsed.message->message_bytes,
            std::nullopt,
            "Exact user-info message contains trailing bytes"));
    }

    auto fields = std::move(*parsed.message);
    const auto bytes_consumed = fields.message_bytes;
    return UserInfoUpdateParseResult{
        UserInfoUpdateState{
            fields.client_index,
            fields.private_user_id,
            std::move(fields.info.keys),
            std::move(fields.info.values),
            std::move(fields.info.safe_fields),
            fields.info.player_name_length,
            fields.info.player_model_length,
            fields.opaque_suffix,
            fields.info_string_length,
            0U,
            fields.message_bytes,
            profile_,
        },
        std::nullopt,
        bytes_consumed,
        bytes_consumed,
    };
}

UserInfoFirstBatchCompletion::UserInfoFirstBatchCompletion(
    const UserInfoBatchTerminalCondition terminal_condition,
    const std::size_t initial_byte_offset,
    const std::size_t final_byte_offset,
    const std::size_t bytes_consumed,
    const std::size_t remaining_byte_count,
    const std::optional<std::uint8_t> following_opcode) noexcept
    : terminal_condition_{terminal_condition},
      initial_byte_offset_{initial_byte_offset},
      final_byte_offset_{final_byte_offset},
      bytes_consumed_{bytes_consumed},
      remaining_byte_count_{remaining_byte_count},
      following_opcode_{following_opcode}
{
}

UserInfoBatchTerminalCondition UserInfoFirstBatchCompletion::terminal_condition() const noexcept { return terminal_condition_; }
std::size_t UserInfoFirstBatchCompletion::initial_byte_offset() const noexcept { return initial_byte_offset_; }
std::size_t UserInfoFirstBatchCompletion::final_byte_offset() const noexcept { return final_byte_offset_; }
std::size_t UserInfoFirstBatchCompletion::bytes_consumed() const noexcept { return bytes_consumed_; }
std::size_t UserInfoFirstBatchCompletion::remaining_byte_count() const noexcept { return remaining_byte_count_; }
std::optional<std::uint8_t> UserInfoFirstBatchCompletion::following_opcode() const noexcept { return following_opcode_; }
bool UserInfoFirstBatchCompletion::following_opcode_unconsumed() const noexcept { return following_opcode_.has_value(); }

UserInfoUpdateStreamState::UserInfoUpdateStreamState(
    std::vector<UserInfoUpdateState> messages,
    const std::size_t total_message_bytes,
    UserInfoFirstBatchCompletion completion) noexcept
    : messages_{std::move(messages)},
      total_message_bytes_{total_message_bytes},
      completion_{std::move(completion)}
{
}

const std::vector<UserInfoUpdateState>& UserInfoUpdateStreamState::messages() const noexcept { return messages_; }
std::size_t UserInfoUpdateStreamState::message_count() const noexcept { return messages_.size(); }
std::size_t UserInfoUpdateStreamState::total_message_bytes() const noexcept { return total_message_bytes_; }
const UserInfoFirstBatchCompletion& UserInfoUpdateStreamState::completion() const noexcept { return completion_; }

UserInfoUpdateStreamDecoder::UserInfoUpdateStreamDecoder(
    UserInfoUpdateLimits limits,
    const UserInfoUpdateCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool UserInfoUpdateStreamDecoder::valid_configuration() const noexcept
{
    return valid_user_info_update_limits(limits_) && supported_profile(profile_);
}

const UserInfoUpdateLimits& UserInfoUpdateStreamDecoder::limits() const noexcept
{
    return limits_;
}

UserInfoUpdateStreamDecodeResult UserInfoUpdateStreamDecoder::decode(
    const std::span<const std::byte> service_payload,
    const PostMoveVarsBoundary& initial_boundary) const
{
    if (!valid_configuration()) {
        return stream_failure(
            UserInfoUpdateStreamErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "User-info limits or compatibility profile are unsupported");
    }
    if (initial_boundary.opcode() != kUserInfoUpdateOpcode) {
        return stream_failure(
            UserInfoUpdateStreamErrorCode::wrong_initial_opcode,
            initial_boundary.byte_offset(),
            initial_boundary.opcode(),
            std::nullopt,
            std::nullopt,
            "Post-movevars continuation does not identify opcode 13");
    }
    if (initial_boundary.byte_offset() >= service_payload.size()) {
        return stream_failure(
            UserInfoUpdateStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.opcode(),
            std::nullopt,
            std::nullopt,
            "User-info cursor is outside the owning payload");
    }
    const auto expected_remaining =
        service_payload.size() - initial_boundary.byte_offset() - 1U;
    if (initial_boundary.remaining_byte_count() != expected_remaining) {
        return stream_failure(
            UserInfoUpdateStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.opcode(),
            std::nullopt,
            std::nullopt,
            "User-info remaining-byte count does not match the owning payload");
    }

    const auto initial_offset = initial_boundary.byte_offset();
    auto cursor = initial_offset;
    std::size_t total_message_bytes = 0U;
    std::vector<UserInfoUpdateState> messages;
    messages.reserve(std::min<std::size_t>(
        limits_.maximum_userinfo_messages_per_batch,
        32U));

    while (true) {
        if (messages.size() >= limits_.maximum_userinfo_messages_per_batch) {
            return stream_failure(
                UserInfoUpdateStreamErrorCode::message_limit_exceeded,
                cursor,
                cursor < service_payload.size()
                    ? std::optional{std::to_integer<std::uint8_t>(
                          service_payload[cursor])}
                    : std::nullopt,
                std::nullopt,
                std::nullopt,
                "User-info message count exceeds the configured project bound");
        }

        auto parsed = parse_message_prefix(service_payload, cursor, limits_);
        if (!parsed.message) {
            return stream_failure(
                UserInfoUpdateStreamErrorCode::message_parse_failed,
                parsed.error ? parsed.error->byte_offset : cursor,
                cursor < service_payload.size()
                    ? std::optional{std::to_integer<std::uint8_t>(
                          service_payload[cursor])}
                    : std::nullopt,
                parsed.error ? std::optional{parsed.error->code} : std::nullopt,
                parsed.error ? parsed.error->info_string_code : std::nullopt,
                parsed.error ? parsed.error->context
                             : "User-info parser returned no candidate or diagnostic");
        }

        auto fields = std::move(*parsed.message);
        if (fields.message_bytes >
            limits_.maximum_userinfo_total_bytes - total_message_bytes) {
            return stream_failure(
                UserInfoUpdateStreamErrorCode::total_byte_limit_exceeded,
                cursor,
                kUserInfoUpdateOpcode,
                std::nullopt,
                std::nullopt,
                "User-info batch bytes exceed the configured project bound");
        }
        if (std::any_of(
                messages.begin(),
                messages.end(),
                [&](const UserInfoUpdateState& previous) noexcept {
                    return previous.client_index() == fields.client_index;
                })) {
            return stream_failure(
                UserInfoUpdateStreamErrorCode::duplicate_client_index,
                cursor + 1U,
                kUserInfoUpdateOpcode,
                std::nullopt,
                std::nullopt,
                "User-info batch repeats a zero-based client index");
        }

        const auto message_bytes = fields.message_bytes;
        messages.emplace_back(UserInfoUpdateState{
            fields.client_index,
            fields.private_user_id,
            std::move(fields.info.keys),
            std::move(fields.info.values),
            std::move(fields.info.safe_fields),
            fields.info.player_name_length,
            fields.info.player_model_length,
            fields.opaque_suffix,
            fields.info_string_length,
            cursor,
            message_bytes,
            profile_,
        });
        total_message_bytes += message_bytes;
        if (!checked_add(cursor, message_bytes, cursor)) {
            return stream_failure(
                UserInfoUpdateStreamErrorCode::size_overflow,
                cursor,
                kUserInfoUpdateOpcode,
                std::nullopt,
                std::nullopt,
                "User-info stream cursor overflowed");
        }

        const auto exact_end = cursor == service_payload.size();
        const auto next_opcode = exact_end
                                     ? std::nullopt
                                     : std::optional{std::to_integer<std::uint8_t>(
                                           service_payload[cursor])};
        if (exact_end || *next_opcode != kUserInfoUpdateOpcode) {
            if (messages.size() ==
                (std::numeric_limits<std::size_t>::max)()) {
                return stream_failure(
                    UserInfoUpdateStreamErrorCode::size_overflow,
                    cursor,
                    next_opcode,
                    std::nullopt,
                    std::nullopt,
                    "User-info event count overflowed");
            }
            const auto remaining = service_payload.size() - cursor;
            const auto required_event_count = messages.size() + 1U;
            return UserInfoUpdateStreamDecodeResult{
                UserInfoUpdateStreamState{
                    std::move(messages),
                    total_message_bytes,
                    UserInfoFirstBatchCompletion{
                        exact_end
                            ? UserInfoBatchTerminalCondition::
                                  exact_end_of_payload
                            : UserInfoBatchTerminalCondition::following_opcode,
                        initial_offset,
                        cursor,
                        cursor - initial_offset,
                        remaining,
                        next_opcode,
                    },
                },
                std::nullopt,
                required_event_count,
            };
        }
    }
}

} // namespace hlclient::goldsrc

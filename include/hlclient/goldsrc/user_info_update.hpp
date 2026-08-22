#pragma once

#include <hlclient/goldsrc/move_vars.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kUserInfoUpdateOpcode = 13U;
inline constexpr std::size_t kUserInfoOpaqueSuffixSize = 16U;
inline constexpr std::uint8_t kMaximumUserInfoClientIndex = 31U;

// Project safety limits. They are intentionally not claims about stock
// engine buffer capacities.
inline constexpr std::size_t kDefaultMaximumUserInfoMessageSize = 2'048U;
inline constexpr std::size_t kMaximumUserInfoMessageSize = 8'192U;
inline constexpr std::size_t kDefaultMaximumUserInfoStringSize = 1'024U;
inline constexpr std::size_t kMaximumUserInfoStringSize = 4'096U;
inline constexpr std::size_t kDefaultMaximumUserInfoKeyLength = 64U;
inline constexpr std::size_t kMaximumUserInfoKeyLength = 256U;
inline constexpr std::size_t kDefaultMaximumUserInfoValueLength = 256U;
inline constexpr std::size_t kMaximumUserInfoValueLength = 4'096U;
inline constexpr std::size_t kDefaultMaximumUserInfoEntries = 64U;
inline constexpr std::size_t kMaximumUserInfoEntries = 256U;
inline constexpr std::size_t kDefaultMaximumUserInfoMessagesPerBatch = 32U;
inline constexpr std::size_t kMaximumUserInfoMessagesPerBatch = 256U;
inline constexpr std::size_t kDefaultMaximumUserInfoTotalBytes = 32'768U;
inline constexpr std::size_t kMaximumUserInfoTotalBytes = 262'144U;
inline constexpr std::size_t kUserInfoDiagnosticTextLimit = 256U;

struct UserInfoUpdateLimits {
    std::size_t maximum_userinfo_message_size{
        kDefaultMaximumUserInfoMessageSize};
    std::size_t maximum_userinfo_string_size{
        kDefaultMaximumUserInfoStringSize};
    std::size_t maximum_userinfo_key_length{
        kDefaultMaximumUserInfoKeyLength};
    std::size_t maximum_userinfo_value_length{
        kDefaultMaximumUserInfoValueLength};
    std::size_t maximum_userinfo_entries{
        kDefaultMaximumUserInfoEntries};
    std::size_t maximum_userinfo_messages_per_batch{
        kDefaultMaximumUserInfoMessagesPerBatch};
    std::size_t maximum_userinfo_total_bytes{
        kDefaultMaximumUserInfoTotalBytes};
};

[[nodiscard]] bool valid_user_info_update_limits(
    const UserInfoUpdateLimits& limits) noexcept;

enum class UserInfoUpdateCompatibilityProfile {
    valve_half_life_protocol_48_build_10210,
};

enum class UserInfoUpdateEvidenceProfile {
    stock_capture_and_public_valve_header,
};

// The key names below are exact captured byte strings. Raw values are never
// exposed: player identity text and all unknown/protected values remain
// private even when the key itself is recognized.
enum class UserInfoSafeField {
    player_name,
    player_model,
    top_color,
    bottom_color,
};

struct UserInfoSafeFieldMetadata {
    UserInfoSafeField field{UserInfoSafeField::player_name};
    std::size_t entry_index{0U};
    std::size_t value_length{0U};

    [[nodiscard]] friend bool operator==(
        const UserInfoSafeFieldMetadata& left,
        const UserInfoSafeFieldMetadata& right) = default;
};

// Immutable owning sign-on metadata. The zero-based client index is safe
// routing metadata. The server-assigned user ID, every info value, and the
// fixed 16-byte suffix are identity-sensitive/private and have no raw getter.
class UserInfoUpdateState final {
public:
    UserInfoUpdateState(const UserInfoUpdateState&) = default;
    UserInfoUpdateState& operator=(const UserInfoUpdateState&) = delete;
    UserInfoUpdateState(UserInfoUpdateState&&) noexcept = default;
    UserInfoUpdateState& operator=(UserInfoUpdateState&&) noexcept = delete;
    ~UserInfoUpdateState() = default;

    [[nodiscard]] std::uint8_t client_index() const noexcept;
    [[nodiscard]] std::size_t info_string_length() const noexcept;
    [[nodiscard]] std::size_t info_entry_count() const noexcept;
    [[nodiscard]] std::span<const UserInfoSafeFieldMetadata> safe_fields() const noexcept;
    [[nodiscard]] std::optional<std::size_t> player_name_length() const noexcept;
    [[nodiscard]] std::optional<std::size_t> player_model_length() const noexcept;
    [[nodiscard]] bool has_private_user_id() const noexcept;
    [[nodiscard]] std::size_t opaque_suffix_size() const noexcept;

    [[nodiscard]] std::size_t source_message_offset() const noexcept;
    [[nodiscard]] std::size_t body_bytes() const noexcept;
    [[nodiscard]] std::size_t message_bytes() const noexcept;
    [[nodiscard]] UserInfoUpdateCompatibilityProfile compatibility_profile() const noexcept;
    [[nodiscard]] UserInfoUpdateEvidenceProfile evidence_profile() const noexcept;

private:
    friend class UserInfoUpdateParser;
    friend class UserInfoUpdateStreamDecoder;

    UserInfoUpdateState(
        std::uint8_t client_index,
        std::int32_t private_user_id,
        std::vector<std::string> private_keys,
        std::vector<std::string> private_values,
        std::vector<UserInfoSafeFieldMetadata> safe_fields,
        std::optional<std::size_t> player_name_length,
        std::optional<std::size_t> player_model_length,
        std::array<std::byte, kUserInfoOpaqueSuffixSize> opaque_suffix,
        std::size_t info_string_length,
        std::size_t source_message_offset,
        std::size_t message_bytes,
        UserInfoUpdateCompatibilityProfile compatibility_profile) noexcept;

    std::uint8_t client_index_{0U};
    std::int32_t private_user_id_{0};
    std::vector<std::string> private_keys_;
    std::vector<std::string> private_values_;
    std::vector<UserInfoSafeFieldMetadata> safe_fields_;
    std::optional<std::size_t> player_name_length_;
    std::optional<std::size_t> player_model_length_;
    std::array<std::byte, kUserInfoOpaqueSuffixSize> opaque_suffix_{};
    std::size_t info_string_length_{0U};
    std::size_t source_message_offset_{0U};
    std::size_t message_bytes_{0U};
    UserInfoUpdateCompatibilityProfile compatibility_profile_{
        UserInfoUpdateCompatibilityProfile::
            valve_half_life_protocol_48_build_10210};
};

enum class UserInfoStringErrorCode {
    missing_leading_separator,
    empty_key,
    empty_value,
    malformed_key_value_sequence,
    duplicate_key,
    key_too_long,
    value_too_long,
    too_many_entries,
};

enum class UserInfoUpdateErrorCode {
    invalid_configuration,
    empty_input,
    wrong_opcode,
    message_too_large,
    truncated_client_index,
    invalid_client_index,
    truncated_user_id,
    invalid_user_id,
    unterminated_info_string,
    info_string_too_large,
    invalid_info_string,
    truncated_opaque_suffix,
    unexpected_trailing_bytes,
    size_overflow,
};

struct UserInfoUpdateError {
    UserInfoUpdateErrorCode code{
        UserInfoUpdateErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<UserInfoStringErrorCode> info_string_code;
    std::string context;
};

struct UserInfoUpdateParseResult {
    std::optional<UserInfoUpdateState> state;
    std::optional<UserInfoUpdateError> error;
    // Includes opcode 13. Both counts are zero on failure.
    std::size_t bytes_consumed{0U};
    std::size_t next_byte_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class UserInfoUpdateParser final {
public:
    explicit UserInfoUpdateParser(
        UserInfoUpdateLimits limits = {},
        UserInfoUpdateCompatibilityProfile profile =
            UserInfoUpdateCompatibilityProfile::
                valve_half_life_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const UserInfoUpdateLimits& limits() const noexcept;

    // Parses exactly one complete opcode-13 message. Any suffix is rejected;
    // stream continuation belongs to UserInfoUpdateStreamDecoder.
    [[nodiscard]] UserInfoUpdateParseResult parse(
        std::span<const std::byte> message) const;

private:
    UserInfoUpdateLimits limits_;
    UserInfoUpdateCompatibilityProfile profile_;
};

enum class UserInfoBatchTerminalCondition {
    exact_end_of_payload,
    following_opcode,
};

class UserInfoFirstBatchCompletion final {
public:
    [[nodiscard]] UserInfoBatchTerminalCondition terminal_condition() const noexcept;
    [[nodiscard]] std::size_t initial_byte_offset() const noexcept;
    [[nodiscard]] std::size_t final_byte_offset() const noexcept;
    [[nodiscard]] std::size_t bytes_consumed() const noexcept;
    [[nodiscard]] std::size_t remaining_byte_count() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> following_opcode() const noexcept;
    [[nodiscard]] bool following_opcode_unconsumed() const noexcept;

private:
    friend class UserInfoUpdateStreamDecoder;

    UserInfoFirstBatchCompletion(
        UserInfoBatchTerminalCondition terminal_condition,
        std::size_t initial_byte_offset,
        std::size_t final_byte_offset,
        std::size_t bytes_consumed,
        std::size_t remaining_byte_count,
        std::optional<std::uint8_t> following_opcode) noexcept;

    UserInfoBatchTerminalCondition terminal_condition_{
        UserInfoBatchTerminalCondition::exact_end_of_payload};
    std::size_t initial_byte_offset_{0U};
    std::size_t final_byte_offset_{0U};
    std::size_t bytes_consumed_{0U};
    std::size_t remaining_byte_count_{0U};
    std::optional<std::uint8_t> following_opcode_;
};

class UserInfoUpdateStreamState final {
public:
    UserInfoUpdateStreamState(const UserInfoUpdateStreamState&) = default;
    UserInfoUpdateStreamState& operator=(const UserInfoUpdateStreamState&) = delete;
    UserInfoUpdateStreamState(UserInfoUpdateStreamState&&) noexcept = default;
    UserInfoUpdateStreamState& operator=(UserInfoUpdateStreamState&&) noexcept = delete;
    ~UserInfoUpdateStreamState() = default;

    [[nodiscard]] const std::vector<UserInfoUpdateState>& messages() const noexcept;
    [[nodiscard]] std::size_t message_count() const noexcept;
    [[nodiscard]] std::size_t total_message_bytes() const noexcept;
    [[nodiscard]] const UserInfoFirstBatchCompletion& completion() const noexcept;

private:
    friend class UserInfoUpdateStreamDecoder;

    UserInfoUpdateStreamState(
        std::vector<UserInfoUpdateState> messages,
        std::size_t total_message_bytes,
        UserInfoFirstBatchCompletion completion) noexcept;

    std::vector<UserInfoUpdateState> messages_;
    std::size_t total_message_bytes_{0U};
    UserInfoFirstBatchCompletion completion_;
};

enum class UserInfoUpdateStreamErrorCode {
    invalid_configuration,
    invalid_boundary_geometry,
    wrong_initial_opcode,
    message_parse_failed,
    duplicate_client_index,
    message_limit_exceeded,
    total_byte_limit_exceeded,
    size_overflow,
};

struct UserInfoUpdateStreamError {
    UserInfoUpdateStreamErrorCode code{
        UserInfoUpdateStreamErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::uint8_t> wire_opcode;
    std::optional<UserInfoUpdateErrorCode> parser_code;
    std::optional<UserInfoStringErrorCode> info_string_code;
    std::string context;
};

struct UserInfoUpdateStreamDecodeResult {
    std::optional<UserInfoUpdateStreamState> state;
    std::optional<UserInfoUpdateStreamError> error;
    // One event per decoded message plus one completion event. Zero on error.
    std::size_t required_event_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class UserInfoUpdateStreamDecoder final {
public:
    explicit UserInfoUpdateStreamDecoder(
        UserInfoUpdateLimits limits = {},
        UserInfoUpdateCompatibilityProfile profile =
            UserInfoUpdateCompatibilityProfile::
                valve_half_life_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const UserInfoUpdateLimits& limits() const noexcept;
    [[nodiscard]] UserInfoUpdateStreamDecodeResult decode(
        std::span<const std::byte> service_payload,
        const PostMoveVarsBoundary& initial_boundary) const;

private:
    UserInfoUpdateLimits limits_;
    UserInfoUpdateCompatibilityProfile profile_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const UserInfoStringErrorCode code) noexcept
{
    switch (code) {
    case UserInfoStringErrorCode::missing_leading_separator:
        return "missing_leading_separator";
    case UserInfoStringErrorCode::empty_key: return "empty_key";
    case UserInfoStringErrorCode::empty_value: return "empty_value";
    case UserInfoStringErrorCode::malformed_key_value_sequence:
        return "malformed_key_value_sequence";
    case UserInfoStringErrorCode::duplicate_key: return "duplicate_key";
    case UserInfoStringErrorCode::key_too_long: return "key_too_long";
    case UserInfoStringErrorCode::value_too_long: return "value_too_long";
    case UserInfoStringErrorCode::too_many_entries: return "too_many_entries";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const UserInfoUpdateErrorCode code) noexcept
{
    switch (code) {
    case UserInfoUpdateErrorCode::invalid_configuration:
        return "invalid_configuration";
    case UserInfoUpdateErrorCode::empty_input: return "empty_input";
    case UserInfoUpdateErrorCode::wrong_opcode: return "wrong_opcode";
    case UserInfoUpdateErrorCode::message_too_large: return "message_too_large";
    case UserInfoUpdateErrorCode::truncated_client_index:
        return "truncated_client_index";
    case UserInfoUpdateErrorCode::invalid_client_index:
        return "invalid_client_index";
    case UserInfoUpdateErrorCode::truncated_user_id:
        return "truncated_user_id";
    case UserInfoUpdateErrorCode::invalid_user_id:
        return "invalid_user_id";
    case UserInfoUpdateErrorCode::unterminated_info_string:
        return "unterminated_info_string";
    case UserInfoUpdateErrorCode::info_string_too_large:
        return "info_string_too_large";
    case UserInfoUpdateErrorCode::invalid_info_string:
        return "invalid_info_string";
    case UserInfoUpdateErrorCode::truncated_opaque_suffix:
        return "truncated_opaque_suffix";
    case UserInfoUpdateErrorCode::unexpected_trailing_bytes:
        return "unexpected_trailing_bytes";
    case UserInfoUpdateErrorCode::size_overflow: return "size_overflow";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const UserInfoUpdateStreamErrorCode code) noexcept
{
    switch (code) {
    case UserInfoUpdateStreamErrorCode::invalid_configuration:
        return "invalid_configuration";
    case UserInfoUpdateStreamErrorCode::invalid_boundary_geometry:
        return "invalid_boundary_geometry";
    case UserInfoUpdateStreamErrorCode::wrong_initial_opcode:
        return "wrong_initial_opcode";
    case UserInfoUpdateStreamErrorCode::message_parse_failed:
        return "message_parse_failed";
    case UserInfoUpdateStreamErrorCode::duplicate_client_index:
        return "duplicate_client_index";
    case UserInfoUpdateStreamErrorCode::message_limit_exceeded:
        return "message_limit_exceeded";
    case UserInfoUpdateStreamErrorCode::total_byte_limit_exceeded:
        return "total_byte_limit_exceeded";
    case UserInfoUpdateStreamErrorCode::size_overflow: return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

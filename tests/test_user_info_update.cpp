#include <hlclient/goldsrc/user_info_update.hpp>

#include "user_info_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace fixture = hlclient::test::user_info_fixture;

template<class T>
concept ExposesRawUserIdentifier = requires(const T& value) {
    value.user_id();
};

template<class T>
concept ExposesRawInfoString = requires(const T& value) {
    value.info_string();
};

template<class T>
concept ExposesRawOpaqueSuffix = requires(const T& value) {
    value.opaque_suffix();
};

template<class T>
concept ExposesRawPlayerName = requires(const T& value) {
    value.player_name();
};

template<class T>
concept ExposesRawPlayerModel = requires(const T& value) {
    value.player_model();
};

template<class T>
concept ExposesTypedTopColor = requires(const T& value) {
    value.top_color();
};

template<class T>
concept ExposesTypedBottomColor = requires(const T& value) {
    value.bottom_color();
};

[[nodiscard]] goldsrc::UserInfoUpdateLimits limits_for_info_size(
    const std::size_t maximum_info_size)
{
    goldsrc::UserInfoUpdateLimits limits;
    limits.maximum_userinfo_message_size = 23U + maximum_info_size;
    limits.maximum_userinfo_string_size = maximum_info_size;
    limits.maximum_userinfo_key_length = std::min<std::size_t>(
        32U,
        maximum_info_size);
    limits.maximum_userinfo_value_length = std::min<std::size_t>(
        32U,
        maximum_info_size);
    limits.maximum_userinfo_total_bytes =
        limits.maximum_userinfo_message_size;
    return limits;
}

void check_error(
    const goldsrc::UserInfoUpdateParseResult& result,
    const goldsrc::UserInfoUpdateErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <=
          goldsrc::kUserInfoDiagnosticTextLimit);
    CHECK_FALSE(result.state.has_value());
    CHECK(result.bytes_consumed == 0U);
    CHECK(result.next_byte_offset == 0U);
}

void check_info_error(
    const std::string_view info,
    const goldsrc::UserInfoStringErrorCode expected)
{
    const auto message = fixture::make_message(2U, 0x12345678U, info);
    const auto result = goldsrc::UserInfoUpdateParser{}.parse(message);
    check_error(
        result,
        goldsrc::UserInfoUpdateErrorCode::invalid_info_string,
        message.size());
    REQUIRE(result.error->info_string_code.has_value());
    CHECK(*result.error->info_string_code == expected);
}

TEST_CASE("User-info parser decodes an independent exact literal transactionally",
          "[goldsrc][userinfo][fixture]")
{
    const auto result =
        goldsrc::UserInfoUpdateParser{}.parse(fixture::kExactUserInfoMessage);

    REQUIRE(result);
    REQUIRE(result.state.has_value());
    CHECK_FALSE(result.error.has_value());
    CHECK(result.bytes_consumed == fixture::kExactUserInfoMessage.size());
    CHECK(result.next_byte_offset == fixture::kExactUserInfoMessage.size());

    const auto& state = *result.state;
    CHECK(state.client_index() == 2U);
    CHECK(state.info_string_length() == fixture::kInfoStringLength);
    CHECK(state.info_entry_count() == 4U);
    CHECK(state.player_name_length() == 9U);
    CHECK(state.player_model_length() == 9U);
    CHECK(state.has_private_user_id());
    CHECK(state.opaque_suffix_size() == 16U);
    CHECK(state.source_message_offset() == 0U);
    CHECK(state.body_bytes() == 79U);
    CHECK(state.message_bytes() == 80U);
    CHECK(
        state.compatibility_profile() ==
        goldsrc::UserInfoUpdateCompatibilityProfile::
            valve_half_life_protocol_48_build_10210);
    CHECK(
        state.evidence_profile() ==
        goldsrc::UserInfoUpdateEvidenceProfile::
            stock_capture_and_public_valve_header);

    const std::array expected_safe_fields{
        goldsrc::UserInfoSafeFieldMetadata{
            goldsrc::UserInfoSafeField::bottom_color, 0U, 1U},
        goldsrc::UserInfoSafeFieldMetadata{
            goldsrc::UserInfoSafeField::player_model, 1U, 9U},
        goldsrc::UserInfoSafeFieldMetadata{
            goldsrc::UserInfoSafeField::top_color, 2U, 2U},
        goldsrc::UserInfoSafeFieldMetadata{
            goldsrc::UserInfoSafeField::player_name, 3U, 9U},
    };
    CHECK(std::ranges::equal(state.safe_fields(), expected_safe_fields));

    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(std::is_aggregate_v<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<goldsrc::UserInfoUpdateState>);
}

TEST_CASE("Every truncated user-info message prefix fails without publication",
          "[goldsrc][userinfo][truncation]")
{
    const goldsrc::UserInfoUpdateParser parser;
    for (std::size_t size = 0U;
         size < fixture::kExactUserInfoMessage.size();
         ++size) {
        INFO("prefix size " << size);
        const auto result = parser.parse(
            std::span{fixture::kExactUserInfoMessage}.first(size));
        REQUIRE_FALSE(result);
        REQUIRE(result.error.has_value());
        CHECK(result.error->byte_offset <= size);
        CHECK_FALSE(result.state.has_value());
        CHECK(result.bytes_consumed == 0U);
        CHECK(result.next_byte_offset == 0U);
    }
}

TEST_CASE("User-info parser requires exact opcode and fixed prefix widths",
          "[goldsrc][userinfo][prefix]")
{
    auto wrong_opcode = fixture::exact_message();
    wrong_opcode[0U] = std::byte{12U};
    check_error(
        goldsrc::UserInfoUpdateParser{}.parse(wrong_opcode),
        goldsrc::UserInfoUpdateErrorCode::wrong_opcode,
        wrong_opcode.size());

    const std::array opcode_only{std::byte{13U}};
    check_error(
        goldsrc::UserInfoUpdateParser{}.parse(opcode_only),
        goldsrc::UserInfoUpdateErrorCode::truncated_client_index,
        opcode_only.size());

    const std::array incomplete_identifier{
        std::byte{13U}, std::byte{2U},
        std::byte{0x78U}, std::byte{0x56U}, std::byte{0x34U},
    };
    check_error(
        goldsrc::UserInfoUpdateParser{}.parse(incomplete_identifier),
        goldsrc::UserInfoUpdateErrorCode::truncated_user_id,
        incomplete_identifier.size());
}

TEST_CASE("User-info client index is zero based and bounded to Protocol 48 clients",
          "[goldsrc][userinfo][client-index][evidence]")
{
    for (const auto index : std::array<std::uint8_t, 2U>{0U, 31U}) {
        const auto message = fixture::make_message(
            index,
            1U,
            R"(\name\Synthetic)");
        const auto result = goldsrc::UserInfoUpdateParser{}.parse(message);
        REQUIRE(result);
        CHECK(result.state->client_index() == index);
        CHECK(result.state->has_private_user_id());
    }

    for (const auto index : std::array<std::uint8_t, 2U>{32U, 255U}) {
        const auto message = fixture::make_message(
            index,
            1U,
            R"(\name\Synthetic)");
        check_error(
            goldsrc::UserInfoUpdateParser{}.parse(message),
            goldsrc::UserInfoUpdateErrorCode::invalid_client_index,
            message.size());
    }
}

TEST_CASE("User-info server user ID has private positive int32 semantics",
          "[goldsrc][userinfo][user-id][privacy][overflow]")
{
    for (const auto user_id : std::array<std::uint32_t, 2U>{
             1U,
             static_cast<std::uint32_t>(
                 (std::numeric_limits<std::int32_t>::max)()),
         }) {
        const auto message = fixture::make_message(
            0U,
            user_id,
            R"(\name\Synthetic)");
        const auto result = goldsrc::UserInfoUpdateParser{}.parse(message);
        REQUIRE(result);
        CHECK(result.state->has_private_user_id());
    }

    for (const auto user_id : std::array<std::uint32_t, 3U>{
             0U,
             0x80000000U,
             (std::numeric_limits<std::uint32_t>::max)(),
         }) {
        const auto message = fixture::make_message(
            0U,
            user_id,
            R"(\name\Synthetic)");
        check_error(
            goldsrc::UserInfoUpdateParser{}.parse(message),
            goldsrc::UserInfoUpdateErrorCode::invalid_user_id,
            message.size());
    }

    STATIC_REQUIRE_FALSE(ExposesRawUserIdentifier<goldsrc::UserInfoUpdateState>);
}

TEST_CASE("User-info string requires a bounded in-payload NUL terminator",
          "[goldsrc][userinfo][string][terminator]")
{
    auto unterminated = fixture::exact_message();
    unterminated.erase(
        unterminated.begin() +
        static_cast<std::ptrdiff_t>(fixture::kInfoTerminatorOffset));
    check_error(
        goldsrc::UserInfoUpdateParser{}.parse(unterminated),
        goldsrc::UserInfoUpdateErrorCode::unterminated_info_string,
        unterminated.size());

    auto exact_limits = limits_for_info_size(fixture::kInfoStringLength);
    REQUIRE(goldsrc::valid_user_info_update_limits(exact_limits));
    REQUIRE(goldsrc::UserInfoUpdateParser{exact_limits}.parse(
        fixture::kExactUserInfoMessage));

    auto too_long = fixture::exact_message();
    too_long.insert(
        too_long.begin() +
            static_cast<std::ptrdiff_t>(fixture::kInfoTerminatorOffset),
        std::byte{'x'});
    check_error(
        goldsrc::UserInfoUpdateParser{exact_limits}.parse(too_long),
        goldsrc::UserInfoUpdateErrorCode::info_string_too_large,
        too_long.size());
}

TEST_CASE("Dedicated user-info grammar rejects malformed ordered pairs and duplicates",
          "[goldsrc][userinfo][info-grammar]")
{
    check_info_error(
        "name\\Synthetic",
        goldsrc::UserInfoStringErrorCode::missing_leading_separator);
    check_info_error(
        R"(\\value)",
        goldsrc::UserInfoStringErrorCode::empty_key);
    check_info_error(
        R"(\name\)",
        goldsrc::UserInfoStringErrorCode::empty_value);
    check_info_error(
        R"(\name)",
        goldsrc::UserInfoStringErrorCode::malformed_key_value_sequence);
    check_info_error(
        R"(\name\Synthetic\)",
        goldsrc::UserInfoStringErrorCode::malformed_key_value_sequence);
    check_info_error(
        R"(\name\Alpha\name\Beta)",
        goldsrc::UserInfoStringErrorCode::duplicate_key);
    check_info_error(
        R"(\name\Alpha\Name\Beta)",
        goldsrc::UserInfoStringErrorCode::duplicate_key);
}

TEST_CASE("User-info key value and entry safety limits fail at limit plus one",
          "[goldsrc][userinfo][info-grammar][limits]")
{
    auto limits = goldsrc::UserInfoUpdateLimits{};
    limits.maximum_userinfo_key_length = 4U;
    const auto long_key = fixture::make_message(1U, 1U, R"(\abcde\v)");
    auto result = goldsrc::UserInfoUpdateParser{limits}.parse(long_key);
    check_error(
        result,
        goldsrc::UserInfoUpdateErrorCode::invalid_info_string,
        long_key.size());
    REQUIRE(result.error->info_string_code);
    CHECK(*result.error->info_string_code ==
          goldsrc::UserInfoStringErrorCode::key_too_long);

    limits = goldsrc::UserInfoUpdateLimits{};
    limits.maximum_userinfo_value_length = 4U;
    const auto long_value = fixture::make_message(1U, 1U, R"(\key\12345)");
    const auto value_result =
        goldsrc::UserInfoUpdateParser{limits}.parse(long_value);
    check_error(
        value_result,
        goldsrc::UserInfoUpdateErrorCode::invalid_info_string,
        long_value.size());
    REQUIRE(value_result.error->info_string_code);
    CHECK(*value_result.error->info_string_code ==
          goldsrc::UserInfoStringErrorCode::value_too_long);

    limits = goldsrc::UserInfoUpdateLimits{};
    limits.maximum_userinfo_entries = 1U;
    const auto too_many = fixture::make_message(
        1U,
        1U,
        R"(\first\one\second\two)");
    const auto entry_result =
        goldsrc::UserInfoUpdateParser{limits}.parse(too_many);
    check_error(
        entry_result,
        goldsrc::UserInfoUpdateErrorCode::invalid_info_string,
        too_many.size());
    REQUIRE(entry_result.error->info_string_code);
    CHECK(*entry_result.error->info_string_code ==
          goldsrc::UserInfoStringErrorCode::too_many_entries);
}

TEST_CASE("Unknown user-info values are byte-preserving private inert data",
          "[goldsrc][userinfo][security][bytes]")
{
    std::string info{"\\unknown\\"};
    info.push_back(static_cast<char>(0xffU));
    info += "\";literal";
    info += "\\name\\Synthetic";
    const auto message = fixture::make_message(7U, 7U, info);
    const auto result = goldsrc::UserInfoUpdateParser{}.parse(message);
    REQUIRE(result);
    CHECK(result.state->info_entry_count() == 2U);
    CHECK(result.state->safe_fields().size() == 1U);
    CHECK(result.state->player_name_length() == 9U);

    STATIC_REQUIRE_FALSE(ExposesRawInfoString<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(ExposesRawPlayerName<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(ExposesRawPlayerModel<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(ExposesTypedTopColor<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(ExposesTypedBottomColor<goldsrc::UserInfoUpdateState>);
}

TEST_CASE("User-info fixed opaque suffix has exact private width",
          "[goldsrc][userinfo][opaque][width]")
{
    auto short_suffix = fixture::exact_message();
    short_suffix.pop_back();
    check_error(
        goldsrc::UserInfoUpdateParser{}.parse(short_suffix),
        goldsrc::UserInfoUpdateErrorCode::truncated_opaque_suffix,
        short_suffix.size());

    auto long_suffix = fixture::exact_message();
    long_suffix.push_back(std::byte{0xd1U});
    check_error(
        goldsrc::UserInfoUpdateParser{}.parse(long_suffix),
        goldsrc::UserInfoUpdateErrorCode::unexpected_trailing_bytes,
        long_suffix.size());

    auto different_suffix = fixture::exact_message();
    std::ranges::fill(
        different_suffix.begin() +
            static_cast<std::ptrdiff_t>(fixture::kOpaqueSuffixOffset),
        different_suffix.end(),
        std::byte{0xeeU});
    const auto parsed =
        goldsrc::UserInfoUpdateParser{}.parse(different_suffix);
    REQUIRE(parsed);
    CHECK(parsed.state->opaque_suffix_size() == 16U);
    STATIC_REQUIRE_FALSE(ExposesRawOpaqueSuffix<goldsrc::UserInfoUpdateState>);
}

TEST_CASE("User-info parser rejects any suffix after one exact message",
          "[goldsrc][userinfo][cursor]")
{
    auto with_following = fixture::exact_message();
    with_following.push_back(std::byte{45U});
    with_following.push_back(std::byte{0xa5U});
    check_error(
        goldsrc::UserInfoUpdateParser{}.parse(with_following),
        goldsrc::UserInfoUpdateErrorCode::unexpected_trailing_bytes,
        with_following.size());
}

TEST_CASE("User-info message-size safety limit accepts the limit and rejects limit plus one",
          "[goldsrc][userinfo][message][limits]")
{
    auto limits = goldsrc::UserInfoUpdateLimits{};
    limits.maximum_userinfo_message_size =
        fixture::kExactUserInfoMessage.size();
    REQUIRE(goldsrc::valid_user_info_update_limits(limits));
    REQUIRE(goldsrc::UserInfoUpdateParser{limits}.parse(
        fixture::kExactUserInfoMessage));

    --limits.maximum_userinfo_message_size;
    REQUIRE(goldsrc::valid_user_info_update_limits(limits));
    check_error(
        goldsrc::UserInfoUpdateParser{limits}.parse(
            fixture::kExactUserInfoMessage),
        goldsrc::UserInfoUpdateErrorCode::message_too_large,
        fixture::kExactUserInfoMessage.size());
}

TEST_CASE("Unconfirmed color values remain private byte data",
          "[goldsrc][userinfo][safe-projection][evidence]")
{
    const auto message = fixture::make_message(
        4U,
        4U,
        R"(\topcolor\not-a-number\bottomcolor\999\name\Synthetic)");
    const auto result = goldsrc::UserInfoUpdateParser{}.parse(message);
    REQUIRE(result);
    CHECK(result.state->player_name_length() == 9U);
    REQUIRE(result.state->safe_fields().size() == 3U);
    CHECK(result.state->safe_fields()[0U].field ==
          goldsrc::UserInfoSafeField::top_color);
    CHECK(result.state->safe_fields()[0U].value_length == 12U);
    CHECK(result.state->safe_fields()[1U].field ==
          goldsrc::UserInfoSafeField::bottom_color);
    CHECK(result.state->safe_fields()[1U].value_length == 3U);
    STATIC_REQUIRE_FALSE(ExposesTypedTopColor<goldsrc::UserInfoUpdateState>);
    STATIC_REQUIRE_FALSE(ExposesTypedBottomColor<goldsrc::UserInfoUpdateState>);
}

TEST_CASE("User-info limit configuration is positive consistent and hard capped",
          "[goldsrc][userinfo][configuration]")
{
    CHECK(goldsrc::valid_user_info_update_limits({}));

    auto hard = goldsrc::UserInfoUpdateLimits{};
    hard.maximum_userinfo_message_size = goldsrc::kMaximumUserInfoMessageSize;
    hard.maximum_userinfo_string_size = goldsrc::kMaximumUserInfoStringSize;
    hard.maximum_userinfo_key_length = goldsrc::kMaximumUserInfoKeyLength;
    hard.maximum_userinfo_value_length = goldsrc::kMaximumUserInfoValueLength;
    hard.maximum_userinfo_entries = goldsrc::kMaximumUserInfoEntries;
    hard.maximum_userinfo_messages_per_batch =
        goldsrc::kMaximumUserInfoMessagesPerBatch;
    hard.maximum_userinfo_total_bytes = goldsrc::kMaximumUserInfoTotalBytes;
    CHECK(goldsrc::valid_user_info_update_limits(hard));

    auto invalid = goldsrc::UserInfoUpdateLimits{};
    invalid.maximum_userinfo_message_size = 0U;
    CHECK_FALSE(goldsrc::valid_user_info_update_limits(invalid));
    invalid = goldsrc::UserInfoUpdateLimits{};
    invalid.maximum_userinfo_string_size =
        goldsrc::kMaximumUserInfoStringSize + 1U;
    CHECK_FALSE(goldsrc::valid_user_info_update_limits(invalid));
    invalid = goldsrc::UserInfoUpdateLimits{};
    invalid.maximum_userinfo_key_length =
        goldsrc::kMaximumUserInfoKeyLength + 1U;
    CHECK_FALSE(goldsrc::valid_user_info_update_limits(invalid));
    invalid = goldsrc::UserInfoUpdateLimits{};
    invalid.maximum_userinfo_value_length =
        goldsrc::kMaximumUserInfoValueLength + 1U;
    CHECK_FALSE(goldsrc::valid_user_info_update_limits(invalid));
    invalid = goldsrc::UserInfoUpdateLimits{};
    invalid.maximum_userinfo_entries = goldsrc::kMaximumUserInfoEntries + 1U;
    CHECK_FALSE(goldsrc::valid_user_info_update_limits(invalid));
    invalid = goldsrc::UserInfoUpdateLimits{};
    invalid.maximum_userinfo_messages_per_batch =
        goldsrc::kMaximumUserInfoMessagesPerBatch + 1U;
    CHECK_FALSE(goldsrc::valid_user_info_update_limits(invalid));
    invalid = goldsrc::UserInfoUpdateLimits{};
    invalid.maximum_userinfo_total_bytes =
        goldsrc::kMaximumUserInfoTotalBytes + 1U;
    CHECK_FALSE(goldsrc::valid_user_info_update_limits(invalid));

    const goldsrc::UserInfoUpdateParser invalid_parser{{0U}};
    check_error(
        invalid_parser.parse(fixture::kExactUserInfoMessage),
        goldsrc::UserInfoUpdateErrorCode::invalid_configuration,
        fixture::kExactUserInfoMessage.size());

    const goldsrc::UserInfoUpdateParser unsupported_profile{
        {},
        static_cast<goldsrc::UserInfoUpdateCompatibilityProfile>(0xffU),
    };
    CHECK_FALSE(unsupported_profile.valid_configuration());
    check_error(
        unsupported_profile.parse(fixture::kExactUserInfoMessage),
        goldsrc::UserInfoUpdateErrorCode::invalid_configuration,
        fixture::kExactUserInfoMessage.size());
}

TEST_CASE("User-info state owns all private data after source storage expires",
          "[goldsrc][userinfo][ownership]")
{
    std::optional<goldsrc::UserInfoUpdateParseResult> result;
    {
        auto message = fixture::exact_message();
        result.emplace(goldsrc::UserInfoUpdateParser{}.parse(message));
        std::ranges::fill(message, std::byte{0U});
    }

    REQUIRE(*result);
    REQUIRE(result->state.has_value());
    CHECK(result->state->client_index() == 2U);
    CHECK(result->state->info_entry_count() == 4U);
    CHECK(result->state->player_name_length() == 9U);
    CHECK(result->state->player_model_length() == 9U);
    CHECK(result->state->opaque_suffix_size() == 16U);
}

} // namespace

#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/goldsrc/user_info_update.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
namespace goldsrc = hlclient::goldsrc;

struct UserInfoInput {
    goldsrc::OwnedServicePayload payload;
    goldsrc::DeltaDescriptionStreamState delta;
    goldsrc::MoveVarsStreamState move_vars;
};

[[nodiscard]] UserInfoInput decode_to_user_info_boundary(
    std::vector<std::byte> user_info_continuation)
{
    std::vector<std::byte> post_delta_body;
    move_fixture::append_move_vars_body(post_delta_body);
    move_fixture::append_confirmed_controls(post_delta_body);
    post_delta_body.insert(
        post_delta_body.end(),
        user_info_continuation.begin(),
        user_info_continuation.end());

    const std::vector<std::vector<std::byte>> schemas{
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
    };
    auto pre_resource = delta_fixture::decode_pre_resource(
        delta_fixture::service_payload(
            schemas,
            goldsrc::kMoveVarsOpcode,
            post_delta_body));
    auto delta = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        pre_resource.payload.bytes,
        pre_resource.state.boundary());
    REQUIRE(delta);
    REQUIRE(delta.state);
    auto move_vars = goldsrc::MoveVarsStreamDecoder{}.decode(
        pre_resource.payload.bytes,
        delta.state->boundary);
    REQUIRE(move_vars);
    REQUIRE(move_vars.state);
    return UserInfoInput{
        std::move(pre_resource.payload),
        std::move(*delta.state),
        std::move(*move_vars.state),
    };
}

[[nodiscard]] std::vector<std::byte> two_messages(
    const std::uint8_t first_index = 2U,
    const std::uint8_t second_index = 7U)
{
    auto bytes = user_fixture::make_message(
        first_index,
        0x11111111U,
        R"(\name\FirstSynthetic\model\scientist)");
    auto second = user_fixture::make_message(
        second_index,
        0x22222222U,
        R"(\name\SecondSynthetic\model\barney)");
    bytes.insert(bytes.end(), second.begin(), second.end());
    return bytes;
}

void check_stream_error(
    const goldsrc::UserInfoUpdateStreamDecodeResult& result,
    const goldsrc::UserInfoUpdateStreamErrorCode expected)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <=
          goldsrc::kUserInfoDiagnosticTextLimit);
    CHECK_FALSE(result.state.has_value());
    CHECK(result.required_event_count == 0U);
}

TEST_CASE("User-info stream decodes one message to exact first-batch end",
          "[goldsrc][userinfo][stream][fixture]")
{
    auto input = decode_to_user_info_boundary(user_fixture::exact_message());
    const auto initial_offset = input.move_vars.boundary().byte_offset();
    const auto result = goldsrc::UserInfoUpdateStreamDecoder{}.decode(
        input.payload.bytes,
        input.move_vars.boundary());

    REQUIRE(result);
    REQUIRE(result.state.has_value());
    CHECK_FALSE(result.error.has_value());
    CHECK(result.required_event_count == 2U);
    CHECK(result.state->message_count() == 1U);
    CHECK(result.state->total_message_bytes() ==
          user_fixture::kExactUserInfoMessage.size());
    REQUIRE(result.state->messages().size() == 1U);
    const auto& message = result.state->messages().front();
    CHECK(message.client_index() == 2U);
    CHECK(message.source_message_offset() == initial_offset);
    CHECK(message.message_bytes() == user_fixture::kExactUserInfoMessage.size());

    const auto& completion = result.state->completion();
    CHECK(
        completion.terminal_condition() ==
        goldsrc::UserInfoBatchTerminalCondition::exact_end_of_payload);
    CHECK(completion.initial_byte_offset() == initial_offset);
    CHECK(completion.final_byte_offset() == input.payload.bytes.size());
    CHECK(completion.bytes_consumed() ==
          user_fixture::kExactUserInfoMessage.size());
    CHECK(completion.remaining_byte_count() == 0U);
    CHECK_FALSE(completion.following_opcode().has_value());
    CHECK_FALSE(completion.following_opcode_unconsumed());
}

TEST_CASE("User-info stream preserves exact repeated-message order",
          "[goldsrc][userinfo][stream][multiple][order]")
{
    auto input = decode_to_user_info_boundary(two_messages());
    const auto initial_offset = input.move_vars.boundary().byte_offset();
    const auto result = goldsrc::UserInfoUpdateStreamDecoder{}.decode(
        input.payload.bytes,
        input.move_vars.boundary());
    REQUIRE(result);
    REQUIRE(result.state);
    REQUIRE(result.state->messages().size() == 2U);
    CHECK(result.required_event_count == 3U);
    CHECK(result.state->messages()[0U].client_index() == 2U);
    CHECK(result.state->messages()[1U].client_index() == 7U);
    CHECK(result.state->messages()[0U].player_name_length() == 14U);
    CHECK(result.state->messages()[1U].player_name_length() == 15U);
    CHECK(result.state->messages()[0U].source_message_offset() == initial_offset);
    CHECK(result.state->messages()[1U].source_message_offset() ==
          initial_offset + result.state->messages()[0U].message_bytes());
    CHECK(result.state->completion().bytes_consumed() ==
          result.state->total_message_bytes());
}

TEST_CASE("User-info stream leaves an exact non-13 boundary unconsumed",
          "[goldsrc][userinfo][stream][boundary]")
{
    auto bytes = user_fixture::exact_message();
    bytes.push_back(std::byte{45U});
    bytes.push_back(std::byte{0x11U});
    bytes.push_back(std::byte{0x22U});
    auto input = decode_to_user_info_boundary(std::move(bytes));
    const auto result = goldsrc::UserInfoUpdateStreamDecoder{}.decode(
        input.payload.bytes,
        input.move_vars.boundary());
    REQUIRE(result);
    REQUIRE(result.state);
    const auto& completion = result.state->completion();
    CHECK(
        completion.terminal_condition() ==
        goldsrc::UserInfoBatchTerminalCondition::following_opcode);
    CHECK(completion.following_opcode() == 45U);
    CHECK(completion.following_opcode_unconsumed());
    CHECK(completion.remaining_byte_count() == 3U);
    CHECK(input.payload.bytes[completion.final_byte_offset()] == std::byte{45U});
    CHECK(completion.bytes_consumed() == user_fixture::kExactUserInfoMessage.size());
}

TEST_CASE("Every truncated second user-info message prefix fails atomically",
          "[goldsrc][userinfo][stream][truncation][transaction]")
{
    const auto first = user_fixture::make_message(
        1U,
        1U,
        R"(\name\FirstSynthetic)");
    const auto second = user_fixture::exact_message();
    for (std::size_t prefix = 1U; prefix < second.size(); ++prefix) {
        INFO("second prefix " << prefix);
        auto bytes = first;
        bytes.insert(
            bytes.end(),
            second.begin(),
            second.begin() + static_cast<std::ptrdiff_t>(prefix));
        auto input = decode_to_user_info_boundary(std::move(bytes));
        const auto result = goldsrc::UserInfoUpdateStreamDecoder{}.decode(
            input.payload.bytes,
            input.move_vars.boundary());
        check_stream_error(
            result,
            goldsrc::UserInfoUpdateStreamErrorCode::message_parse_failed);
        REQUIRE(result.error->parser_code.has_value());
    }
}

TEST_CASE("User-info stream rejects duplicate client indexes",
          "[goldsrc][userinfo][stream][duplicate]")
{
    auto input = decode_to_user_info_boundary(two_messages(5U, 5U));
    const auto result = goldsrc::UserInfoUpdateStreamDecoder{}.decode(
        input.payload.bytes,
        input.move_vars.boundary());
    check_stream_error(
        result,
        goldsrc::UserInfoUpdateStreamErrorCode::duplicate_client_index);
}

TEST_CASE("User-info stream enforces message-count and total-byte bounds",
          "[goldsrc][userinfo][stream][limits]")
{
    SECTION("message count") {
        auto input = decode_to_user_info_boundary(two_messages());
        auto limits = goldsrc::UserInfoUpdateLimits{};
        limits.maximum_userinfo_messages_per_batch = 1U;
        const auto result = goldsrc::UserInfoUpdateStreamDecoder{limits}.decode(
            input.payload.bytes,
            input.move_vars.boundary());
        check_stream_error(
            result,
            goldsrc::UserInfoUpdateStreamErrorCode::message_limit_exceeded);
    }

    SECTION("total bytes") {
        auto first = user_fixture::exact_message();
        auto second = user_fixture::exact_message();
        second[1U] = std::byte{3U};
        first.insert(first.end(), second.begin(), second.end());
        auto input = decode_to_user_info_boundary(std::move(first));

        auto limits = goldsrc::UserInfoUpdateLimits{};
        limits.maximum_userinfo_message_size = 128U;
        limits.maximum_userinfo_string_size = 105U;
        limits.maximum_userinfo_key_length = 64U;
        limits.maximum_userinfo_value_length = 64U;
        limits.maximum_userinfo_total_bytes = 128U;
        REQUIRE(goldsrc::valid_user_info_update_limits(limits));
        const auto result = goldsrc::UserInfoUpdateStreamDecoder{limits}.decode(
            input.payload.bytes,
            input.move_vars.boundary());
        check_stream_error(
            result,
            goldsrc::UserInfoUpdateStreamErrorCode::total_byte_limit_exceeded);
    }
}

TEST_CASE("User-info stream never scans forward for a plausible opcode",
          "[goldsrc][userinfo][stream][no-scan]")
{
    auto input = decode_to_user_info_boundary(user_fixture::exact_message());
    const auto boundary_offset = input.move_vars.boundary().byte_offset();

    // The move-vars boundary was established at the original byte. Mutating
    // exactly that byte proves the decoder reports it and never searches for
    // the valid opcode 13 one byte later.
    input.payload.bytes[boundary_offset] = std::byte{99U};
    input.payload.bytes[boundary_offset + 1U] = std::byte{13U};
    const auto result = goldsrc::UserInfoUpdateStreamDecoder{}.decode(
        input.payload.bytes,
        input.move_vars.boundary());
    check_stream_error(
        result,
        goldsrc::UserInfoUpdateStreamErrorCode::message_parse_failed);
    REQUIRE(result.error->parser_code);
    CHECK(*result.error->parser_code ==
          goldsrc::UserInfoUpdateErrorCode::wrong_opcode);
    CHECK(result.error->byte_offset == boundary_offset);
}

TEST_CASE("User-info continuation validates owning boundary geometry",
          "[goldsrc][userinfo][stream][geometry]")
{
    auto changed_payload =
        decode_to_user_info_boundary(user_fixture::exact_message());
    changed_payload.payload.bytes.push_back(std::byte{0U});
    check_stream_error(
        goldsrc::UserInfoUpdateStreamDecoder{}.decode(
            changed_payload.payload.bytes,
            changed_payload.move_vars.boundary()),
        goldsrc::UserInfoUpdateStreamErrorCode::invalid_boundary_geometry);

    auto shortened = decode_to_user_info_boundary(user_fixture::exact_message());
    const auto boundary_offset = shortened.move_vars.boundary().byte_offset();
    check_stream_error(
        goldsrc::UserInfoUpdateStreamDecoder{}.decode(
            std::span<const std::byte>{shortened.payload.bytes}.first(
                boundary_offset),
            shortened.move_vars.boundary()),
        goldsrc::UserInfoUpdateStreamErrorCode::invalid_boundary_geometry);

    const goldsrc::UserInfoUpdateStreamDecoder invalid{{0U}};
    auto normal = decode_to_user_info_boundary(user_fixture::exact_message());
    check_stream_error(
        invalid.decode(normal.payload.bytes, normal.move_vars.boundary()),
        goldsrc::UserInfoUpdateStreamErrorCode::invalid_configuration);

    const goldsrc::UserInfoUpdateStreamDecoder unsupported{
        {},
        static_cast<goldsrc::UserInfoUpdateCompatibilityProfile>(0xffU),
    };
    CHECK_FALSE(unsupported.valid_configuration());
    check_stream_error(
        unsupported.decode(normal.payload.bytes, normal.move_vars.boundary()),
        goldsrc::UserInfoUpdateStreamErrorCode::invalid_configuration);
}

TEST_CASE("User-info stream state remains owning after payload destruction",
          "[goldsrc][userinfo][stream][ownership]")
{
    std::optional<goldsrc::UserInfoUpdateStreamDecodeResult> result;
    {
        auto input = decode_to_user_info_boundary(two_messages());
        result.emplace(goldsrc::UserInfoUpdateStreamDecoder{}.decode(
            input.payload.bytes,
            input.move_vars.boundary()));
        std::ranges::fill(input.payload.bytes, std::byte{0U});
    }
    REQUIRE(*result);
    REQUIRE(result->state);
    REQUIRE(result->state->messages().size() == 2U);
    CHECK(result->state->messages()[0U].client_index() == 2U);
    CHECK(result->state->messages()[1U].client_index() == 7U);
    CHECK(result->state->messages()[0U].player_model_length() == 9U);
    CHECK(result->state->messages()[1U].player_model_length() == 6U);
}

} // namespace

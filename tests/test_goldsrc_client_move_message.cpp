#include "goldsrc_usercmd_test_fixture.hpp"

#include <hlclient/goldsrc/client_move_message.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace fixture = hlclient::test::usercmd_fixture;

inline constexpr std::array<std::byte, 7U> kMinimalMoveLiteral{
    std::byte{0xe1U}, std::byte{0x2dU}, std::byte{0x00U},
    std::byte{0x10U}, std::byte{0x08U}, std::byte{0x00U},
    std::byte{0x00U}};

inline constexpr std::array<std::byte, 10U> kLerpMoveLiteral{
    std::byte{0xe1U}, std::byte{0x09U}, std::byte{0x05U},
    std::byte{0x10U}, std::byte{0x20U}, std::byte{0x00U},
    std::byte{0x01U}, std::byte{0x01U}, std::byte{0x01U},
    std::byte{0x01U}};

[[nodiscard]] goldsrc::GoldSrcClientMoveMessageCodec synthetic_codec(
    const goldsrc::GoldSrcUserCmdLimits& limits = {})
{
    return goldsrc::GoldSrcClientMoveMessageCodec{
        limits,
        goldsrc::GoldSrcClientMoveCompatibilityProfile::
            synthetic_client_move_v1};
}

[[nodiscard]] goldsrc::GoldSrcClientMoveDecodeContext decode_context(
    const std::uint32_t first_sequence,
    const std::uint32_t outgoing_sequence = 7U)
{
    goldsrc::GoldSrcClientMoveDecodeContext context;
    context.outgoing_netchan_sequence = outgoing_sequence;
    context.first_command_sequence = fixture::sequence(first_sequence);
    context.end_policy =
        goldsrc::GoldSrcClientMoveEndPolicy::require_exact_end;
    return context;
}

[[nodiscard]] std::vector<std::byte> bytes_of(
    const std::span<const std::byte> bytes)
{
    return {bytes.begin(), bytes.end()};
}

TEST_CASE("Synthetic client-move minimal packet matches an independent literal",
          "[goldsrc][usercmd][client-move][literal][roundtrip]")
{
    const auto binding = fixture::exact_binding();
    const std::vector commands{
        fixture::shared_state(fixture::default_state(1U))};
    const goldsrc::GoldSrcClientMoveEncodeContext encode_context{
        7U, 0U, 0U, 1U};

    const auto encoded = synthetic_codec().encode(
        commands, binding, encode_context);
    REQUIRE(encoded);
    REQUIRE(encoded.message);
    const auto& message = *encoded.message;
    CHECK(message.opcode() == goldsrc::kSyntheticClientMoveOpcode);
    CHECK(message.checksum() == 0x2dU);
    CHECK(message.synthetic_loss_metadata() == 0U);
    CHECK(message.backup_command_count() == 0U);
    CHECK(message.new_command_count() == 1U);
    CHECK(message.commands().size() == 1U);
    CHECK(message.commands()[0U].command_sequence().value() == 1U);
    CHECK(message.changed_field_count() == 0U);
    CHECK(message.bit_length() == 56U);
    CHECK(message.profile() == goldsrc::
          GoldSrcClientMoveCompatibilityProfile::synthetic_client_move_v1);
    CHECK(std::ranges::equal(message.bytes(), kMinimalMoveLiteral));

    const auto decoded = synthetic_codec().decode(
        kMinimalMoveLiteral, binding, decode_context(1U));
    REQUIRE(decoded);
    REQUIRE(decoded.message);
    CHECK_FALSE(decoded.error);
    CHECK(decoded.bytes_consumed == kMinimalMoveLiteral.size());
    CHECK(decoded.next_byte_offset == kMinimalMoveLiteral.size());
    CHECK(std::ranges::equal(
        decoded.message->bytes(), kMinimalMoveLiteral));
    REQUIRE(decoded.message->commands().size() == 1U);
    const auto& command = decoded.message->commands()[0U];
    CHECK(command.command_sequence().value() == 1U);
    CHECK(command.lerp_msec() == 0U);
    CHECK(command.view_angles() == std::array<float, 3U>{});
}

TEST_CASE("Synthetic client-move embeds the exact LSB usercmd delta literal",
          "[goldsrc][usercmd][client-move][literal][delta]")
{
    const auto binding = fixture::exact_binding();
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        fixture::sequence(1U));
    info.lerp_msec = 257U;
    const std::vector commands{
        fixture::shared_state(fixture::state(info))};
    const goldsrc::GoldSrcClientMoveEncodeContext context{
        7U, 5U, 0U, 1U};

    const auto encoded = synthetic_codec().encode(commands, binding, context);
    REQUIRE(encoded);
    REQUIRE(encoded.message);
    CHECK(encoded.message->checksum() == 0x09U);
    CHECK(encoded.message->changed_field_count() == 1U);
    CHECK(std::ranges::equal(encoded.message->bytes(), kLerpMoveLiteral));

    const auto decoded = synthetic_codec().decode(
        kLerpMoveLiteral, binding, decode_context(1U));
    REQUIRE(decoded);
    REQUIRE(decoded.message);
    REQUIRE(decoded.message->commands().size() == 1U);
    CHECK(decoded.message->synthetic_loss_metadata() == 5U);
    CHECK(decoded.message->commands()[0U].lerp_msec() == 257U);
    CHECK(decoded.message->changed_field_count() == 1U);
}

TEST_CASE("Client-move preserves backup then new commands oldest-first",
          "[goldsrc][usercmd][client-move][order][roundtrip]")
{
    const auto binding = fixture::exact_binding();
    auto second_info = goldsrc::goldsrc_usercmd_default_create_info(
        fixture::sequence(11U));
    second_info.lerp_msec = 257U;
    const std::vector commands{
        fixture::shared_state(fixture::default_state(10U)),
        fixture::shared_state(fixture::state(second_info)),
        fixture::shared_state(fixture::full_state(12U)),
    };
    const goldsrc::GoldSrcClientMoveEncodeContext context{
        0x1020'3040U, 23U, 1U, 2U};

    const auto encoded = synthetic_codec().encode(commands, binding, context);
    REQUIRE(encoded);
    REQUIRE(encoded.message);
    CHECK(encoded.message->backup_command_count() == 1U);
    CHECK(encoded.message->new_command_count() == 2U);
    CHECK(encoded.message->synthetic_loss_metadata() == 23U);
    CHECK(encoded.message->changed_field_count() == 16U);
    REQUIRE(encoded.message->commands().size() == 3U);
    CHECK(encoded.message->commands()[0U].command_sequence().value() == 10U);
    CHECK(encoded.message->commands()[1U].command_sequence().value() == 11U);
    CHECK(encoded.message->commands()[2U].command_sequence().value() == 12U);
    CHECK(std::to_integer<std::uint8_t>(encoded.message->bytes()[3U]) ==
          0x21U);

    auto decode = decode_context(10U, 0x1020'3040U);
    decode.first_sample_time_nanoseconds = 6'000'000;
    const auto decoded = synthetic_codec().decode(
        encoded.message->bytes(), binding, decode);
    REQUIRE(decoded);
    REQUIRE(decoded.message);
    CHECK(decoded.message->backup_command_count() == 1U);
    CHECK(decoded.message->new_command_count() == 2U);
    CHECK(decoded.message->changed_field_count() == 16U);
    REQUIRE(decoded.message->commands().size() == 3U);
    CHECK(decoded.message->commands()[0U].command_sequence().value() == 10U);
    CHECK(decoded.message->commands()[1U].command_sequence().value() == 11U);
    CHECK(decoded.message->commands()[2U].command_sequence().value() == 12U);
    CHECK(decoded.message->commands()[0U].sample_time_nanoseconds() ==
          6'000'000);
    CHECK(decoded.message->commands()[1U].sample_time_nanoseconds() ==
          7'000'000);
    CHECK(decoded.message->commands()[2U].sample_time_nanoseconds() ==
          8'000'000);
    CHECK(decoded.message->commands()[1U].lerp_msec() == 257U);
    CHECK(decoded.message->commands()[2U].impact_index() == 17);
}

TEST_CASE("Client-move encoder rejects invalid counts pointers and ordering",
          "[goldsrc][usercmd][client-move][limit][order]")
{
    const auto binding = fixture::exact_binding();
    const auto first = fixture::shared_state(fixture::default_state(1U));
    const auto second = fixture::shared_state(fixture::default_state(2U));

    SECTION("zero new commands")
    {
        const std::vector commands{first};
        const auto result = synthetic_codec().encode(
            commands, binding, {0U, 0U, 1U, 0U});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::count_limit_exceeded);
    }

    SECTION("count mismatch")
    {
        const std::vector commands{first};
        const auto result = synthetic_codec().encode(
            commands, binding, {0U, 0U, 0U, 2U});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::count_mismatch);
    }

    SECTION("configured backup bound")
    {
        const std::vector commands{first};
        const auto result = synthetic_codec().encode(
            commands, binding, {0U, 0U, 8U, 1U});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::count_limit_exceeded);
    }

    SECTION("count sum overflow")
    {
        const std::vector commands{first};
        const auto result = synthetic_codec().encode(
            commands,
            binding,
            {0U,
             0U,
             std::numeric_limits<std::size_t>::max(),
             1U});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::count_limit_exceeded);
    }

    SECTION("null command")
    {
        const std::vector<std::shared_ptr<const goldsrc::GoldSrcUserCmdState>>
            commands{nullptr};
        const auto result = synthetic_codec().encode(
            commands, binding, {0U, 0U, 0U, 1U});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::command_order_invalid);
        CHECK(result.error->command_index == 0U);
    }

    SECTION("duplicate and descending sequence")
    {
        const std::vector duplicate{first, first};
        const auto duplicate_result = synthetic_codec().encode(
            duplicate, binding, {0U, 0U, 0U, 2U});
        REQUIRE_FALSE(duplicate_result);
        REQUIRE(duplicate_result.error);
        CHECK(duplicate_result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::command_order_invalid);
        CHECK(duplicate_result.error->command_index == 1U);

        const std::vector descending{second, first};
        const auto descending_result = synthetic_codec().encode(
            descending, binding, {0U, 0U, 0U, 2U});
        REQUIRE_FALSE(descending_result);
        REQUIRE(descending_result.error);
        CHECK(descending_result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::command_order_invalid);
        CHECK(descending_result.error->command_index == 1U);
    }
}

TEST_CASE("Client-move decoder rejects malformed envelope boundaries",
          "[goldsrc][usercmd][client-move][malformed]")
{
    const auto binding = fixture::exact_binding();
    const auto context = decode_context(1U);

    SECTION("explicit identity is mandatory")
    {
        const auto result = synthetic_codec().decode(
            {}, binding, goldsrc::GoldSrcClientMoveDecodeContext{});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::invalid_context);
    }

    SECTION("every header prefix is truncated")
    {
        for (std::size_t length = 0U; length < 4U; ++length) {
            INFO("header prefix length " << length);
            const auto result = synthetic_codec().decode(
                std::span{kMinimalMoveLiteral}.first(length),
                binding,
                context);
            CHECK_FALSE(result);
            REQUIRE(result.error);
            CHECK(result.error->code ==
                  goldsrc::GoldSrcClientMoveErrorCode::truncated_header);
            CHECK(result.error->byte_offset == length);
            CHECK(result.bytes_consumed == 0U);
        }
    }

    SECTION("wrong opcode")
    {
        constexpr std::array payload{
            std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0x10U}};
        const auto result = synthetic_codec().decode(payload, binding, context);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::wrong_opcode);
        CHECK(result.error->byte_offset == 0U);
    }

    SECTION("zero new-command count")
    {
        constexpr std::array payload{
            std::byte{0xe1U}, std::byte{0U}, std::byte{0U}, std::byte{0U}};
        const auto result = synthetic_codec().decode(payload, binding, context);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::count_limit_exceeded);
        CHECK(result.error->byte_offset == 3U);
    }

    SECTION("truncated delta length")
    {
        constexpr std::array payload{
            std::byte{0xe1U}, std::byte{0U}, std::byte{0U},
            std::byte{0x10U}, std::byte{0x08U}};
        const auto result = synthetic_codec().decode(payload, binding, context);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::
              GoldSrcClientMoveErrorCode::truncated_delta_length);
        CHECK(result.error->byte_offset == 4U);
        CHECK(result.error->command_index == 0U);
    }

    SECTION("zero and truncated delta bodies")
    {
        constexpr std::array zero_length{
            std::byte{0xe1U}, std::byte{0U}, std::byte{0U},
            std::byte{0x10U}, std::byte{0U}, std::byte{0U}};
        const auto zero = synthetic_codec().decode(
            zero_length, binding, context);
        REQUIRE_FALSE(zero);
        REQUIRE(zero.error);
        CHECK(zero.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::truncated_delta);
        CHECK(zero.error->byte_offset == 6U);

        constexpr std::array truncated{
            std::byte{0xe1U}, std::byte{0U}, std::byte{0U},
            std::byte{0x10U}, std::byte{0x08U}, std::byte{0U}};
        const auto short_body = synthetic_codec().decode(
            truncated, binding, context);
        REQUIRE_FALSE(short_body);
        REQUIRE(short_body.error);
        CHECK(short_body.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::truncated_delta);
        CHECK(short_body.error->byte_offset == 6U);
    }

    SECTION("inner delta error is surfaced transactionally")
    {
        constexpr std::array noncanonical{
            std::byte{0xe1U}, std::byte{0U}, std::byte{0U},
            std::byte{0x10U}, std::byte{0x10U}, std::byte{0U},
            std::byte{0x01U}, std::byte{0x00U}};
        const auto result = synthetic_codec().decode(
            noncanonical, binding, context);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.message);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::delta_decode_failed);
        CHECK(result.error->command_index == 0U);
        CHECK(result.error->byte_offset == 6U);
        CHECK(result.error->delta_code == goldsrc::
              GoldSrcUserCmdDeltaErrorCode::noncanonical_mask);
        CHECK(result.bytes_consumed == 0U);
        CHECK(result.next_byte_offset == 0U);
    }
}

TEST_CASE("Client-move checksum binds body and outgoing sequence",
          "[goldsrc][usercmd][client-move][checksum][sensitivity]")
{
    const auto binding = fixture::exact_binding();

    const auto wrong_sequence = synthetic_codec().decode(
        kMinimalMoveLiteral, binding, decode_context(1U, 8U));
    REQUIRE_FALSE(wrong_sequence);
    REQUIRE(wrong_sequence.error);
    CHECK(wrong_sequence.error->code ==
          goldsrc::GoldSrcClientMoveErrorCode::checksum_mismatch);
    CHECK(wrong_sequence.error->byte_offset == 1U);

    auto changed_body = kMinimalMoveLiteral;
    changed_body[2U] ^= std::byte{0x01U};
    const auto body_result = synthetic_codec().decode(
        changed_body, binding, decode_context(1U));
    REQUIRE_FALSE(body_result);
    REQUIRE(body_result.error);
    CHECK(body_result.error->code ==
          goldsrc::GoldSrcClientMoveErrorCode::checksum_mismatch);

    auto changed_checksum = kMinimalMoveLiteral;
    changed_checksum[1U] ^= std::byte{0x01U};
    const auto checksum_result = synthetic_codec().decode(
        changed_checksum, binding, decode_context(1U));
    REQUIRE_FALSE(checksum_result);
    REQUIRE(checksum_result.error);
    CHECK(checksum_result.error->code ==
          goldsrc::GoldSrcClientMoveErrorCode::checksum_mismatch);
}

TEST_CASE("Client-move trailing policy and packet bounds are explicit",
          "[goldsrc][usercmd][client-move][limit][trailing]")
{
    const auto binding = fixture::exact_binding();

    SECTION("leave or require trailing byte")
    {
        auto payload = bytes_of(kMinimalMoveLiteral);
        payload.push_back(std::byte{0xaaU});
        auto leave_context = decode_context(1U);
        leave_context.end_policy =
            goldsrc::GoldSrcClientMoveEndPolicy::leave_trailing_bytes;
        const auto leave = synthetic_codec().decode(
            payload, binding, leave_context);
        REQUIRE(leave);
        REQUIRE(leave.message);
        CHECK(leave.bytes_consumed == kMinimalMoveLiteral.size());
        CHECK(leave.next_byte_offset == kMinimalMoveLiteral.size());
        CHECK(leave.message->bytes().size() == kMinimalMoveLiteral.size());

        const auto exact = synthetic_codec().decode(
            payload, binding, decode_context(1U));
        REQUIRE_FALSE(exact);
        REQUIRE(exact.error);
        CHECK(exact.error->code == goldsrc::
              GoldSrcClientMoveErrorCode::unexpected_trailing_bytes);
        CHECK(exact.error->byte_offset == kMinimalMoveLiteral.size());
        CHECK(exact.bytes_consumed == 0U);
    }

    SECTION("encoded packet byte budget")
    {
        auto limits = goldsrc::GoldSrcUserCmdLimits{};
        limits.maximum_encoded_bits = 48U;
        limits.maximum_encoded_bytes = 6U;
        const auto codec = synthetic_codec(limits);
        REQUIRE(codec.valid_configuration());
        const std::vector commands{
            fixture::shared_state(fixture::default_state(1U))};
        const auto result = codec.encode(
            commands, binding, {0U, 0U, 0U, 1U});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::packet_size_exceeded);
        CHECK(result.error->command_index == 0U);
    }

    SECTION("aggregate bit budget is stricter than the byte budget")
    {
        auto limits = goldsrc::GoldSrcUserCmdLimits{};
        limits.maximum_encoded_bits = 32U;
        limits.maximum_encoded_bytes = 1'024U;
        const auto codec = synthetic_codec(limits);
        REQUIRE(codec.valid_configuration());
        const std::vector commands{
            fixture::shared_state(fixture::default_state(1U))};
        const auto encoded = codec.encode(
            commands, binding, {0U, 0U, 0U, 1U});
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(encoded.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::packet_size_exceeded);

        const auto decoded = codec.decode(
            kMinimalMoveLiteral, binding, decode_context(1U));
        REQUIRE_FALSE(decoded);
        REQUIRE(decoded.error);
        CHECK(decoded.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::packet_size_exceeded);
    }

    SECTION("decoded sample-time sequence cannot overflow")
    {
        const std::vector commands{
            fixture::shared_state(fixture::default_state(1U)),
            fixture::shared_state(fixture::default_state(2U))};
        const auto encoded = synthetic_codec().encode(
            commands, binding, {17U, 0U, 0U, 2U});
        REQUIRE(encoded);
        REQUIRE(encoded.message);
        auto context = decode_context(1U, 17U);
        context.first_sample_time_nanoseconds =
            std::numeric_limits<std::int64_t>::max();
        const auto decoded = synthetic_codec().decode(
            encoded.message->bytes(), binding, context);
        REQUIRE_FALSE(decoded);
        REQUIRE(decoded.error);
        CHECK(decoded.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::invalid_context);
    }

    SECTION("decoded sequence domain")
    {
        auto limits = goldsrc::GoldSrcUserCmdLimits{};
        limits.maximum_command_sequence = 3U;
        const auto codec = synthetic_codec(limits);
        REQUIRE(codec.valid_configuration());
        constexpr std::array payload{
            std::byte{0xe1U}, std::byte{0U}, std::byte{0U},
            std::byte{0x20U}};
        const auto result = codec.decode(
            payload, binding, decode_context(3U));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::sequence_exhausted);
        CHECK(result.error->byte_offset == 3U);
    }
}

TEST_CASE("Client-move invalid and stock profiles fail before payload I/O",
          "[goldsrc][usercmd][client-move][evidence][configuration]")
{
    const auto binding = fixture::exact_binding();

    auto invalid_limits = goldsrc::GoldSrcUserCmdLimits{};
    invalid_limits.maximum_encoded_bytes = 0U;
    const goldsrc::GoldSrcClientMoveMessageCodec invalid{
        invalid_limits,
        goldsrc::GoldSrcClientMoveCompatibilityProfile::
            synthetic_client_move_v1};
    REQUIRE_FALSE(invalid.valid_configuration());
    const auto invalid_decode = invalid.decode(
        {}, binding, goldsrc::GoldSrcClientMoveDecodeContext{});
    REQUIRE_FALSE(invalid_decode);
    REQUIRE(invalid_decode.error);
    CHECK(invalid_decode.error->code ==
          goldsrc::GoldSrcClientMoveErrorCode::invalid_configuration);

    const goldsrc::GoldSrcClientMoveMessageCodec stock;
    REQUIRE(stock.valid_configuration());
    CHECK(stock.profile() == goldsrc::GoldSrcClientMoveCompatibilityProfile::
          stock_protocol_48_build_10210_evidence_pending);
    const auto decoded = stock.decode(
        {}, binding, goldsrc::GoldSrcClientMoveDecodeContext{});
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code ==
          goldsrc::GoldSrcClientMoveErrorCode::stock_evidence_pending);
    CHECK(decoded.error->byte_offset == 0U);

    const std::span<const std::shared_ptr<const goldsrc::GoldSrcUserCmdState>>
        no_commands;
    const auto encoded = stock.encode(
        no_commands, binding, goldsrc::GoldSrcClientMoveEncodeContext{});
    REQUIRE_FALSE(encoded);
    REQUIRE(encoded.error);
    CHECK(encoded.error->code ==
          goldsrc::GoldSrcClientMoveErrorCode::stock_evidence_pending);

    SECTION("unknown compatibility profile")
    {
        const goldsrc::GoldSrcClientMoveMessageCodec unknown{
            {},
            static_cast<goldsrc::GoldSrcClientMoveCompatibilityProfile>(
                0xffU)};
        REQUIRE_FALSE(unknown.valid_configuration());

        const auto unknown_decode = unknown.decode(
            {}, binding, goldsrc::GoldSrcClientMoveDecodeContext{});
        REQUIRE_FALSE(unknown_decode);
        REQUIRE(unknown_decode.error);
        CHECK(unknown_decode.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::invalid_configuration);

        const auto unknown_encode = unknown.encode(
            no_commands, binding, goldsrc::GoldSrcClientMoveEncodeContext{});
        REQUIRE_FALSE(unknown_encode);
        REQUIRE(unknown_encode.error);
        CHECK(unknown_encode.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::invalid_configuration);
    }

    SECTION("unknown decode end policy")
    {
        auto context = goldsrc::GoldSrcClientMoveDecodeContext{};
        context.end_policy =
            static_cast<goldsrc::GoldSrcClientMoveEndPolicy>(0xffU);
        const auto result = synthetic_codec().decode({}, binding, context);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcClientMoveErrorCode::invalid_context);
        CHECK(result.error->byte_offset == 0U);
    }
}

} // namespace

#include "goldsrc_usercmd_test_fixture.hpp"

#include <hlclient/goldsrc/bit_writer.hpp>
#include <hlclient/goldsrc/usercmd_delta_codec.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace fixture = hlclient::test::usercmd_fixture;

[[nodiscard]] goldsrc::GoldSrcUserCmdDeltaCodec synthetic_codec(
    const goldsrc::GoldSrcUserCmdLimits& limits = {})
{
    return goldsrc::GoldSrcUserCmdDeltaCodec{
        limits,
        goldsrc::GoldSrcUserCmdDeltaCompatibilityProfile::
            synthetic_usercmd_delta_v1};
}

[[nodiscard]] goldsrc::GoldSrcUserCmdDeltaDecodeContext decode_context(
    const std::span<const std::byte> bytes,
    const goldsrc::GoldSrcUserCmdSequence sequence)
{
    goldsrc::GoldSrcUserCmdDeltaDecodeContext context;
    context.bytes = bytes;
    context.bit_length = bytes.size() * 8U;
    context.end_policy =
        goldsrc::GoldSrcUserCmdDeltaEndPolicy::require_exact_end;
    context.command_sequence = sequence;
    return context;
}

TEST_CASE("BitWriter emits exact LSB-first bits and fails atomically",
          "[goldsrc][usercmd][delta][bit-writer]")
{
    std::array bytes{std::byte{0xffU}, std::byte{0xffU}};
    goldsrc::BitWriter writer{bytes, 3U, 10U};
    REQUIRE(writer.valid());
    CHECK(writer.initial_bit_offset() == 3U);
    CHECK(writer.remaining_bits() == 10U);

    REQUIRE(writer.write_bits(0x15U, 5U));
    CHECK(writer.bit_offset() == 8U);
    CHECK(writer.written_bits() == 5U);
    CHECK(bytes[0U] == std::byte{0xafU});

    const auto before = bytes;
    const auto before_cursor = writer.bit_offset();
    const auto exhausted = writer.write_bits(0xffU, 8U);
    REQUIRE_FALSE(exhausted);
    CHECK(exhausted.error == goldsrc::BitWriterError::exhausted);
    CHECK(writer.bit_offset() == before_cursor);
    CHECK(bytes == before);

    REQUIRE(writer.write_bits(0x1fU, 5U));
    CHECK(writer.remaining_bits() == 0U);
    CHECK(writer.byte_aligned() == false);
    CHECK(writer.align_to_byte_zero_padding() ==
          goldsrc::BitWriterError::exhausted);

    goldsrc::BitWriter invalid_width{bytes};
    CHECK(invalid_width.write_bits(0U, 33U).error ==
          goldsrc::BitWriterError::invalid_width);
    goldsrc::BitWriter invalid_geometry{bytes, 17U};
    CHECK_FALSE(invalid_geometry.valid());
    CHECK(invalid_geometry.write_bits(0U, 1U).error ==
          goldsrc::BitWriterError::invalid_geometry);
}

TEST_CASE("Zero usercmd delta has one exact byte and preserves its base",
          "[goldsrc][usercmd][delta][literal][base]")
{
    const auto binding = fixture::exact_binding();
    const auto base = fixture::default_state(1U);
    auto current_info = goldsrc::goldsrc_usercmd_default_create_info(
        fixture::sequence(2U));
    current_info.source_input_sequence = 77U;
    const auto current = fixture::state(current_info);

    const auto encoded = synthetic_codec().encode_delta(
        base, current, binding,
        {goldsrc::GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state});
    REQUIRE(encoded);
    REQUIRE(encoded.delta);
    CHECK(encoded.delta->bytes == std::vector<std::byte>(
              fixture::kZeroDelta.begin(), fixture::kZeroDelta.end()));
    CHECK(encoded.delta->bit_length == 8U);
    CHECK(encoded.delta->meaningful_bit_length == 8U);
    CHECK(encoded.delta->padding_bits == 0U);
    CHECK(encoded.delta->changed_field_count == 0U);
    CHECK(encoded.delta->mask_byte_count == 0U);
    CHECK(encoded.delta->field_mask == std::array<std::uint8_t, 2U>{});

    auto context = decode_context(
        fixture::kZeroDelta, fixture::sequence(2U));
    context.base_policy =
        goldsrc::GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state;
    context.source_input_sequence = 77U;
    const auto decoded = synthetic_codec().decode_delta(base, binding, context);
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.bits_consumed == 8U);
    CHECK(decoded.next_bit_offset == 8U);
    CHECK(decoded.next_byte_offset == 1U);
    CHECK(decoded.changed_field_count == 0U);
    CHECK(decoded.state->command_sequence().value() == 2U);
    CHECK(decoded.state->source_input_sequence() == 77U);
    CHECK(decoded.state->lerp_msec() == base.lerp_msec());
    CHECK(decoded.state->view_angles() == base.view_angles());
}

TEST_CASE("First and highest usercmd mask bits match independent literals",
          "[goldsrc][usercmd][delta][literal][mask]")
{
    const auto binding = fixture::exact_binding();

    SECTION("field zero")
    {
        const auto base = fixture::default_state(1U);
        auto info = goldsrc::goldsrc_usercmd_default_create_info(
            fixture::sequence(2U));
        info.lerp_msec = 257U;
        const auto current = fixture::state(info);
        const auto encoded = synthetic_codec().encode_delta(
            base, current, binding,
            {goldsrc::GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state});
        REQUIRE(encoded);
        REQUIRE(encoded.delta);
        CHECK(encoded.delta->bytes == std::vector<std::byte>(
                  fixture::kFieldZero257Delta.begin(),
                  fixture::kFieldZero257Delta.end()));
        CHECK(encoded.delta->mask_byte_count == 1U);
        CHECK(encoded.delta->field_mask[0U] == 0x01U);
        CHECK(encoded.delta->changed_field_count == 1U);
        CHECK(encoded.delta->meaningful_bit_length == 25U);
        CHECK(encoded.delta->padding_bits == 7U);
    }

    SECTION("field fourteen")
    {
        auto base_info = goldsrc::goldsrc_usercmd_default_create_info(
            fixture::sequence(1U));
        base_info.impact_index = 1;
        base_info.impact_position = {0.0F, 0.0F, 1.0F};
        const auto base = fixture::state(base_info);
        auto current_info = base_info;
        current_info.command_sequence = fixture::sequence(2U);
        current_info.impact_position[2U] = -1.0F;
        const auto current = fixture::state(current_info);
        const auto encoded = synthetic_codec().encode_delta(
            base, current, binding);
        REQUIRE(encoded);
        REQUIRE(encoded.delta);
        CHECK(encoded.delta->bytes == std::vector<std::byte>(
                  fixture::kHighestFieldMinusOneDelta.begin(),
                  fixture::kHighestFieldMinusOneDelta.end()));
        CHECK(encoded.delta->mask_byte_count == 2U);
        CHECK(encoded.delta->field_mask ==
              std::array<std::uint8_t, 2U>{0x00U, 0x40U});
        CHECK(encoded.delta->changed_field_count == 1U);
        CHECK(encoded.delta->bit_length == 40U);
    }
}

TEST_CASE("All usercmd fields match a 216-bit literal and roundtrip",
          "[goldsrc][usercmd][delta][literal][roundtrip]")
{
    const auto binding = fixture::exact_binding();
    const auto base = fixture::default_state(1U);
    const auto current = fixture::full_state(2U);
    const auto encoded = synthetic_codec().encode_delta(
        base, current, binding,
        {goldsrc::GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state});
    REQUIRE(encoded);
    REQUIRE(encoded.delta);
    CHECK(encoded.delta->bytes == std::vector<std::byte>(
              fixture::kAllFieldsDelta.begin(), fixture::kAllFieldsDelta.end()));
    CHECK(encoded.delta->meaningful_bit_length == 211U);
    CHECK(encoded.delta->padding_bits == 5U);
    CHECK(encoded.delta->bit_length == 216U);
    CHECK(encoded.delta->changed_field_count == 15U);
    CHECK(encoded.delta->mask_byte_count == 2U);
    CHECK(encoded.delta->field_mask ==
          std::array<std::uint8_t, 2U>{0xffU, 0x7fU});

    auto context = decode_context(
        fixture::kAllFieldsDelta, fixture::sequence(2U));
    context.base_policy =
        goldsrc::GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state;
    context.sample_duration_nanoseconds = 15'000'000U;
    const auto decoded = synthetic_codec().decode_delta(base, binding, context);
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.bits_consumed == 216U);
    CHECK(decoded.changed_field_count == 15U);
    CHECK(decoded.state->lerp_msec() == 300U);
    CHECK(decoded.state->msec() == 15U);
    CHECK(decoded.state->view_angles() ==
          std::array<float, 3U>{270.0F, 90.0F, 180.0F});
    CHECK(decoded.state->forward_move() == 320.0F);
    CHECK(decoded.state->side_move() == -160.0F);
    CHECK(decoded.state->up_move() == 10.0F);
    CHECK(decoded.state->buttons() == 0x1234U);
    CHECK(decoded.state->impact_index() == 17);
    CHECK(decoded.state->impact_position() ==
          std::array<float, 3U>{1.25F, -2.5F, 3.0F});
}

TEST_CASE("Synthetic usercmd quantization is deterministic and wire-canonical",
          "[goldsrc][usercmd][delta][quantization]")
{
    const auto binding = fixture::exact_binding();
    const auto base = fixture::default_state(1U);
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        fixture::sequence(2U));
    info.view_angles = {-90.0F, 360.0F, 0.0028F};
    info.forward_move = 1.5F;
    info.side_move = -1.5F;
    const auto current = fixture::state(info);
    const auto encoded = synthetic_codec().encode_delta(base, current, binding);
    REQUIRE(encoded);
    REQUIRE(encoded.delta);

    auto context = decode_context(
        encoded.delta->bytes, fixture::sequence(2U));
    const auto decoded = synthetic_codec().decode_delta(base, binding, context);
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.state->view_angles()[0U] == 270.0F);
    CHECK(decoded.state->view_angles()[1U] == 0.0F);
    CHECK(decoded.state->view_angles()[2U] ==
          Catch::Approx(360.0 / 65'536.0));
    CHECK(decoded.state->forward_move() == 2.0F);
    CHECK(decoded.state->side_move() == -2.0F);
}

TEST_CASE("Usercmd decoder enforces canonical masks padding and trailing policy",
          "[goldsrc][usercmd][delta][malformed]")
{
    const auto binding = fixture::exact_binding();
    const auto base = fixture::default_state(1U);

    const auto decode = [&](const std::span<const std::byte> bytes) {
        return synthetic_codec().decode_delta(
            base, binding, decode_context(bytes, fixture::sequence(2U)));
    };

    constexpr std::array nonminimal{
        std::byte{0x01U}, std::byte{0x00U}};
    const auto nonminimal_result = decode(nonminimal);
    REQUIRE_FALSE(nonminimal_result);
    REQUIRE(nonminimal_result.error);
    CHECK(nonminimal_result.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::noncanonical_mask);

    constexpr std::array out_of_range{
        std::byte{0x02U}, std::byte{0x00U}, std::byte{0x80U}};
    const auto out_of_range_result = decode(out_of_range);
    REQUIRE_FALSE(out_of_range_result);
    REQUIRE(out_of_range_result.error);
    CHECK(out_of_range_result.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::mask_bit_out_of_range);
    CHECK(out_of_range_result.error->field_index == 15U);

    constexpr std::array excessive_count{std::byte{0x03U}};
    const auto excessive_result = decode(excessive_count);
    REQUIRE_FALSE(excessive_result);
    REQUIRE(excessive_result.error);
    CHECK(excessive_result.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::mask_byte_count_exceeded);

    auto bad_padding = fixture::kFieldZero257Delta;
    bad_padding.back() |= std::byte{0x80U};
    const auto padding_result = decode(bad_padding);
    REQUIRE_FALSE(padding_result);
    REQUIRE(padding_result.error);
    CHECK(padding_result.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::nonzero_padding);

    constexpr std::array trailing{
        std::byte{0x00U}, std::byte{0xaaU}};
    const auto exact_result = decode(trailing);
    REQUIRE_FALSE(exact_result);
    REQUIRE(exact_result.error);
    CHECK(exact_result.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::unexpected_trailing_bits);

    auto leave_context = decode_context(trailing, fixture::sequence(2U));
    leave_context.end_policy =
        goldsrc::GoldSrcUserCmdDeltaEndPolicy::leave_trailing_bits;
    const auto leave = synthetic_codec().decode_delta(
        base, binding, leave_context);
    REQUIRE(leave);
    CHECK(leave.bits_consumed == 8U);
    CHECK(leave.next_bit_offset == 8U);
}

TEST_CASE("Every all-field delta truncation fails without partial publication",
          "[goldsrc][usercmd][delta][truncation][transaction]")
{
    const auto binding = fixture::exact_binding();
    const auto base = fixture::default_state(1U);
    for (std::size_t bit_length = 0U;
         bit_length < fixture::kAllFieldsDelta.size() * 8U;
         ++bit_length) {
        INFO("truncated bit length " << bit_length);
        auto context = decode_context(
            fixture::kAllFieldsDelta, fixture::sequence(2U));
        context.bit_length = bit_length;
        const auto result = synthetic_codec().decode_delta(
            base, binding, context);
        CHECK_FALSE(result);
        CHECK_FALSE(result.state);
        CHECK(result.error.has_value());
        CHECK(result.bits_consumed == 0U);
        CHECK(result.next_bit_offset == 0U);
    }
}

TEST_CASE("Usercmd codec rejects unsupported state and bounded geometry",
          "[goldsrc][usercmd][delta][limit][evidence]")
{
    const auto binding = fixture::exact_binding();
    const auto base = fixture::default_state(1U);

    auto weapon_info = goldsrc::goldsrc_usercmd_default_create_info(
        fixture::sequence(2U));
    weapon_info.weapon_select = 1U;
    const auto weapon = fixture::state(weapon_info);
    const auto weapon_result = synthetic_codec().encode_delta(
        base, weapon, binding);
    REQUIRE_FALSE(weapon_result);
    REQUIRE(weapon_result.error);
    CHECK(weapon_result.error->code == goldsrc::
          GoldSrcUserCmdDeltaErrorCode::unsupported_weapon_selection);

    const auto full = fixture::full_state(2U);
    goldsrc::GoldSrcUserCmdLimits tight_limits;
    tight_limits.maximum_encoded_bits = 8U;
    tight_limits.maximum_encoded_bytes = 1U;
    const auto bounded = synthetic_codec(tight_limits).encode_delta(
        base, full, binding);
    REQUIRE_FALSE(bounded);
    REQUIRE(bounded.error);
    CHECK(bounded.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::encoded_limit_exceeded);

    const auto nondefault = synthetic_codec().encode_delta(
        full, full, binding,
        {goldsrc::GoldSrcUserCmdDeltaBasePolicy::synthetic_default_state});
    REQUIRE_FALSE(nondefault);
    REQUIRE(nondefault.error);
    CHECK(nondefault.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::invalid_base_policy);

    std::array prefixed{
        std::byte{0xccU}, std::byte{0x01U}, std::byte{0x01U},
        std::byte{0x01U}, std::byte{0x01U}};
    auto cursor_context = decode_context(prefixed, fixture::sequence(2U));
    cursor_context.start_bit_offset = 8U;
    cursor_context.bit_length = 32U;
    auto decoded = synthetic_codec().decode_delta(
        base, binding, cursor_context);
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.state->lerp_msec() == 257U);
    CHECK(decoded.bits_consumed == 32U);
    CHECK(decoded.next_bit_offset == 40U);
    CHECK(decoded.next_byte_offset == 5U);

    cursor_context.start_bit_offset = 1U;
    const auto unaligned = synthetic_codec().decode_delta(
        base, binding, cursor_context);
    REQUIRE_FALSE(unaligned);
    REQUIRE(unaligned.error);
    CHECK(unaligned.error->code ==
          goldsrc::GoldSrcUserCmdDeltaErrorCode::invalid_input_geometry);
}

TEST_CASE("Stock usercmd delta profiles fail before byte and state inspection",
          "[goldsrc][usercmd][delta][evidence]")
{
    const auto binding = fixture::exact_binding();
    const auto base = fixture::default_state(1U);
    auto invalid_context = goldsrc::GoldSrcUserCmdDeltaDecodeContext{};

    for (const auto profile : {
             goldsrc::GoldSrcUserCmdDeltaCompatibilityProfile::
                 stock_protocol_48_build_10210_usercmd_v1,
             goldsrc::GoldSrcUserCmdDeltaCompatibilityProfile::
                 stock_evidence_pending}) {
        const goldsrc::GoldSrcUserCmdDeltaCodec codec{{}, profile};
        const auto decoded = codec.decode_delta(base, binding, invalid_context);
        REQUIRE_FALSE(decoded);
        REQUIRE(decoded.error);
        CHECK(decoded.error->code ==
              goldsrc::GoldSrcUserCmdDeltaErrorCode::stock_evidence_pending);
        CHECK(decoded.error->bit_offset == 0U);

        const auto encoded = codec.encode_delta(base, base, binding);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(encoded.error->code ==
              goldsrc::GoldSrcUserCmdDeltaErrorCode::stock_evidence_pending);
    }
}

} // namespace

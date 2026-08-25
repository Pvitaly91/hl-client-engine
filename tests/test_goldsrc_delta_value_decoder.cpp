#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/delta_value_decoder.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;

constexpr auto kSyntheticProfile =
    goldsrc::DeltaValueCompatibilityProfile::synthetic_neutral_v1;

[[nodiscard]] goldsrc::DeltaSchema parse_schema(
    const std::string_view name,
    const std::span<const fixture::Field> fields)
{
    const auto bytes = fixture::schema(name, fields);
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    return std::move(*parsed.schema);
}

[[nodiscard]] goldsrc::DeltaObjectState build_object(
    const goldsrc::DeltaSchema& schema,
    const std::span<const goldsrc::DeltaScalarValue> values,
    const goldsrc::GoldSrcDeltaValueLimits limits = {})
{
    auto built = goldsrc::DeltaObjectBuilder{limits, kSyntheticProfile}.build(
        schema, values);
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] goldsrc::DeltaValueDecodeContext context(
    const std::span<const std::byte> bytes,
    const std::size_t start_bit_offset = 0U,
    const std::size_t bit_length = static_cast<std::size_t>(-1))
{
    return goldsrc::DeltaValueDecodeContext{
        bytes,
        start_bit_offset,
        bit_length,
        std::nullopt,
    };
}

[[nodiscard]] goldsrc::GoldSrcDeltaValueDecoder synthetic_decoder(
    const goldsrc::GoldSrcDeltaValueLimits limits = {})
{
    return goldsrc::GoldSrcDeltaValueDecoder{limits, kSyntheticProfile};
}

constexpr fixture::Field kByteField[]{
    {"byte_value", 0x0000'0001U, 0U, 8U},
};

constexpr fixture::Field kNarrowByteField[]{
    {"byte_value", 0x0000'0001U, 0U, 7U},
};

constexpr fixture::Field kShortField[]{
    {"short_value", 0x0000'0002U, 0U, 12U},
};

constexpr fixture::Field kSignedShortField[]{
    {"signed_short", 0x8000'0002U, 0U, 12U},
};

constexpr fixture::Field kIntegerField[]{
    {"integer_value", 0x0000'0008U, 0U, 20U},
};

constexpr fixture::Field kSignedIntegerField[]{
    {"signed_integer", 0x8000'0008U, 0U, 20U},
};

constexpr fixture::Field kFloatField[]{
    {"scaled_float", 0x8000'0004U, 0U, 8U, 8'000U, 4'000U},
};

constexpr fixture::Field kAngleField[]{
    {"angle", 0x0000'0010U, 0U, 8U},
};

constexpr fixture::Field kStringField[]{
    {"bytes", 0x0000'0080U, 0U, 1U},
};

constexpr fixture::Field kTimeWindow8Field[]{
    {"time8", 0x0000'0020U, 0U, 8U},
};

constexpr fixture::Field kTimeWindowBigField[]{
    {"time_big", 0x0000'0040U, 0U, 16U},
};

TEST_CASE("Stock runtime delta profile is evidence-pending before input inspection",
          "[goldsrc][delta-value][evidence]")
{
    const auto schema = parse_schema("byte_t", kByteField);
    const goldsrc::GoldSrcDeltaValueDecoder decoder;
    const auto result = decoder.decode_delta(
        schema,
        nullptr,
        goldsrc::DeltaValueDecodeContext{
            {},
            777U,
            999U,
            goldsrc::DeltaTimeReference{123},
        });

    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == goldsrc::DeltaValueErrorCode::evidence_pending);
    CHECK(result.bits_consumed == 0U);
    CHECK(result.next_bit_offset == 777U);
    CHECK(result.next_byte_offset == 97U);
}

TEST_CASE("Delta value safety limits validate their exact hard boundary",
          "[goldsrc][delta-value][limit]")
{
    auto exact = goldsrc::GoldSrcDeltaValueLimits{};
    exact.maximum_fields_per_object =
        goldsrc::kMaximumDeltaValueFieldsPerObject;
    exact.maximum_mask_bytes = goldsrc::kMaximumDeltaValueMaskBytes;
    exact.maximum_string_bytes = goldsrc::kMaximumDeltaValueStringBytes;
    exact.maximum_total_value_bytes = goldsrc::kMaximumDeltaValueTotalBytes;
    exact.maximum_object_count_per_message =
        goldsrc::kMaximumDeltaObjectCountPerMessage;
    exact.maximum_delta_bits = goldsrc::kMaximumDeltaValueBits;
    exact.maximum_numeric_magnitude =
        goldsrc::kMaximumDeltaNumericMagnitude;
    CHECK(goldsrc::valid_goldsrc_delta_value_limits(exact));

    auto invalid = exact;
    invalid.maximum_fields_per_object =
        goldsrc::kMaximumDeltaValueFieldsPerObject + 1U;
    CHECK_FALSE(goldsrc::valid_goldsrc_delta_value_limits(invalid));
    invalid = exact;
    invalid.maximum_mask_bytes = goldsrc::kMaximumDeltaValueMaskBytes + 1U;
    CHECK_FALSE(goldsrc::valid_goldsrc_delta_value_limits(invalid));
    invalid = exact;
    invalid.maximum_string_bytes = goldsrc::kMaximumDeltaValueStringBytes + 1U;
    CHECK_FALSE(goldsrc::valid_goldsrc_delta_value_limits(invalid));
    invalid = exact;
    invalid.maximum_total_value_bytes =
        goldsrc::kMaximumDeltaValueTotalBytes + 1U;
    CHECK_FALSE(goldsrc::valid_goldsrc_delta_value_limits(invalid));
    invalid = exact;
    invalid.maximum_object_count_per_message =
        goldsrc::kMaximumDeltaObjectCountPerMessage + 1U;
    CHECK_FALSE(goldsrc::valid_goldsrc_delta_value_limits(invalid));
    invalid = exact;
    invalid.maximum_delta_bits = goldsrc::kMaximumDeltaValueBits + 1U;
    CHECK_FALSE(goldsrc::valid_goldsrc_delta_value_limits(invalid));
    invalid = exact;
    invalid.maximum_numeric_magnitude =
        goldsrc::kMaximumDeltaNumericMagnitude + 1.0;
    CHECK_FALSE(goldsrc::valid_goldsrc_delta_value_limits(invalid));
}

TEST_CASE("Delta value APIs reject an invalid compatibility profile",
          "[goldsrc][delta-value][configuration][security]")
{
    const auto profile =
        static_cast<goldsrc::DeltaValueCompatibilityProfile>(0xffU);
    const goldsrc::GoldSrcDeltaValueDecoder decoder{{}, profile};
    const goldsrc::DeltaObjectBuilder builder{{}, profile};
    CHECK_FALSE(decoder.valid_configuration());
    CHECK_FALSE(builder.valid_configuration());

    const auto schema = parse_schema("byte_t", kByteField);
    const auto decoded = decoder.decode_delta(
        schema,
        nullptr,
        goldsrc::DeltaValueDecodeContext{{}, 9U, 0U, std::nullopt});
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code ==
          goldsrc::DeltaValueErrorCode::invalid_configuration);
    CHECK(decoded.next_bit_offset == 9U);
}

TEST_CASE("Delta value operational bit and field limits are exact",
          "[goldsrc][delta-value][limit]")
{
    constexpr fixture::Field fields[]{
        {"first", 0x0000'0001U, 0U, 8U},
        {"second", 0x0000'0001U, 1U, 8U},
    };
    const auto schema = parse_schema("two_limit_t", fields);
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x03U},
        std::byte{0x11U}, std::byte{0x22U}};
    auto exact = goldsrc::GoldSrcDeltaValueLimits{};
    exact.maximum_fields_per_object = 2U;
    exact.maximum_delta_bits = 32U;
    REQUIRE(synthetic_decoder(exact).decode_delta(
        schema, nullptr, context(literal)));

    auto too_few_fields = exact;
    too_few_fields.maximum_fields_per_object = 1U;
    const auto field_result = synthetic_decoder(too_few_fields).decode_delta(
        schema, nullptr, context(literal));
    REQUIRE_FALSE(field_result);
    REQUIRE(field_result.error);
    CHECK(field_result.error->code ==
          goldsrc::DeltaValueErrorCode::field_limit_exceeded);

    auto too_few_bits = exact;
    too_few_bits.maximum_delta_bits = 31U;
    const auto bit_result = synthetic_decoder(too_few_bits).decode_delta(
        schema, nullptr, context(literal));
    REQUIRE_FALSE(bit_result);
    REQUIRE(bit_result.error);
    CHECK(bit_result.error->code ==
          goldsrc::DeltaValueErrorCode::invalid_input_geometry);
}

TEST_CASE("Delta object builder publishes immutable owning wire-order state",
          "[goldsrc][delta-value][ownership]")
{
    static_assert(!std::is_copy_assignable_v<goldsrc::DeltaFieldValue>);
    static_assert(!std::is_copy_assignable_v<goldsrc::DeltaObjectState>);

    constexpr fixture::Field fields[]{
        {"number", 0x0000'0001U, 0U, 8U},
        {"text", 0x0000'0080U, 1U, 1U},
    };
    auto state = [&fields] {
        const auto schema = parse_schema("owned_runtime_t", fields);
        const std::array<goldsrc::DeltaScalarValue, 2U> values{
            std::uint32_t{7U},
            std::string{"owned\0bytes", 11U},
        };
        return build_object(schema, values);
    }();

    CHECK(state.schema_name() == "owned_runtime_t");
    CHECK(state.decode_profile() == kSyntheticProfile);
    REQUIRE(state.field_count() == 2U);
    CHECK(state.fields()[0U].name() == "number");
    CHECK(state.fields()[0U].wire_index() == 0U);
    CHECK(std::get<std::uint32_t>(state.fields()[0U].value()) == 7U);
    REQUIRE(state.find_exact("text") != nullptr);
    CHECK(std::get<std::string>(state.find_exact("text")->value()).size() == 11U);
    CHECK(state.find_exact("TEXT") == nullptr);
    CHECK(state.accounted_value_bytes() == 15U);
}

TEST_CASE("Delta object builder rejects overflow and non-finite explicit values",
          "[goldsrc][delta-value][numeric]")
{
    constexpr fixture::Field tiny_signed[]{
        {"tiny", 0x8000'0002U, 0U, 3U},
    };
    const auto tiny_schema = parse_schema("tiny_t", tiny_signed);
    const std::array<goldsrc::DeltaScalarValue, 1U> overflow{
        std::int32_t{4},
    };
    const auto overflow_result =
        goldsrc::DeltaObjectBuilder{{}, kSyntheticProfile}.build(
            tiny_schema, overflow);
    REQUIRE_FALSE(overflow_result);
    REQUIRE(overflow_result.error);
    CHECK(overflow_result.error->code ==
          goldsrc::DeltaValueErrorCode::numeric_overflow);

    const auto float_schema = parse_schema("float_t", kFloatField);
    const std::array<goldsrc::DeltaScalarValue, 1U> non_finite{
        std::numeric_limits<double>::infinity(),
    };
    const auto non_finite_result =
        goldsrc::DeltaObjectBuilder{{}, kSyntheticProfile}.build(
            float_schema, non_finite);
    REQUIRE_FALSE(non_finite_result);
    REQUIRE(non_finite_result.error);
    CHECK(non_finite_result.error->code ==
          goldsrc::DeltaValueErrorCode::non_finite_result);
}

TEST_CASE("Empty changed mask preserves an exact base and requires one",
          "[goldsrc][delta-value][mask][base]")
{
    const auto schema = parse_schema("byte_t", kByteField);
    const std::array<goldsrc::DeltaScalarValue, 1U> values{
        std::uint32_t{37U},
    };
    const auto base = build_object(schema, values);
    // Independent literal: zero mask-byte count, hence no changed fields.
    constexpr std::array bytes{std::byte{0x00U}};

    const auto decoded = synthetic_decoder().decode_delta(
        schema, &base, context(bytes));
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(std::get<std::uint32_t>(decoded.state->fields()[0U].value()) == 37U);
    CHECK(decoded.bits_consumed == 8U);
    CHECK(decoded.next_bit_offset == 8U);

    const auto missing = synthetic_decoder().decode_delta(
        schema, nullptr, context(bytes));
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code ==
          goldsrc::DeltaValueErrorCode::missing_required_base);
    CHECK(missing.bits_consumed == 0U);
}

TEST_CASE("Synthetic runtime decoder handles byte and short integer widths",
          "[goldsrc][delta-value][integer]")
{
    const auto byte_schema = parse_schema("byte_t", kByteField);
    // count=1, field-0 mask=1, raw byte=0xa5.
    constexpr std::array byte_literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0xa5U}};
    const auto byte_result = synthetic_decoder().decode_delta(
        byte_schema, nullptr, context(byte_literal));
    REQUIRE(byte_result);
    CHECK(std::get<std::uint32_t>(
              byte_result.state->fields()[0U].value()) == 0xa5U);

    const auto short_schema = parse_schema("short_t", kShortField);
    // 12-bit 0xabc followed by four zero padding bits.
    constexpr std::array short_literal{
        std::byte{0x01U}, std::byte{0x01U},
        std::byte{0xbcU}, std::byte{0x0aU}};
    const auto short_result = synthetic_decoder().decode_delta(
        short_schema, nullptr, context(short_literal));
    REQUIRE(short_result);
    CHECK(std::get<std::uint32_t>(
              short_result.state->fields()[0U].value()) == 0xabcU);

    const auto signed_schema = parse_schema("signed_short_t", kSignedShortField);
    // 12-bit two's-complement -2 followed by zero padding.
    constexpr std::array signed_literal{
        std::byte{0x01U}, std::byte{0x01U},
        std::byte{0xfeU}, std::byte{0x0fU}};
    const auto signed_result = synthetic_decoder().decode_delta(
        signed_schema, nullptr, context(signed_literal));
    REQUIRE(signed_result);
    CHECK(std::get<std::int32_t>(
              signed_result.state->fields()[0U].value()) == -2);
}

TEST_CASE("Synthetic runtime decoder handles unsigned and signed 20-bit integers",
          "[goldsrc][delta-value][integer]")
{
    const auto unsigned_schema = parse_schema("integer_t", kIntegerField);
    constexpr std::array unsigned_literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0xdeU},
        std::byte{0xbcU}, std::byte{0x0aU}};
    const auto unsigned_result = synthetic_decoder().decode_delta(
        unsigned_schema, nullptr, context(unsigned_literal));
    REQUIRE(unsigned_result);
    CHECK(std::get<std::uint32_t>(
              unsigned_result.state->fields()[0U].value()) == 0xabcdeU);

    const auto signed_schema = parse_schema(
        "signed_integer_t", kSignedIntegerField);
    constexpr std::array signed_literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0xfeU},
        std::byte{0xffU}, std::byte{0x0fU}};
    const auto signed_result = synthetic_decoder().decode_delta(
        signed_schema, nullptr, context(signed_literal));
    REQUIRE(signed_result);
    CHECK(std::get<std::int32_t>(
              signed_result.state->fields()[0U].value()) == -2);
}

TEST_CASE("Signed byte is rejected by the reused published schema grammar",
          "[goldsrc][delta-value][integer][schema]")
{
    constexpr fixture::Field signed_byte[]{
        {"signed_byte", 0x8000'0001U, 0U, 8U},
    };
    const auto bytes = fixture::schema("signed_byte_t", signed_byte);
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error);
    CHECK(parsed.error->code ==
          goldsrc::DeltaDescriptionErrorCode::invalid_type_flags);

    constexpr fixture::Field excessive_width[]{
        {"wide_byte", 0x0000'0001U, 0U, 9U},
    };
    const auto wide_bytes = fixture::schema("wide_byte_t", excessive_width);
    const auto wide_parsed = goldsrc::DeltaDescriptionParser{}.parse(
        wide_bytes, 0U);
    REQUIRE_FALSE(wide_parsed);
    REQUIRE(wide_parsed.error);
    CHECK(wide_parsed.error->code ==
          goldsrc::DeltaDescriptionErrorCode::invalid_significant_bits);
}

TEST_CASE("Synthetic float uses checked q times post over pre rational scaling",
          "[goldsrc][delta-value][float]")
{
    const auto schema = parse_schema("float_t", kFloatField);
    // Signed q=-4, post/pre=4000/8000, result=-2.
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0xfcU}};
    const auto result = synthetic_decoder().decode_delta(
        schema, nullptr, context(literal));
    REQUIRE(result);
    CHECK(std::get<double>(result.state->fields()[0U].value()) ==
          Catch::Approx(-2.0));

    auto limits = goldsrc::GoldSrcDeltaValueLimits{};
    limits.maximum_numeric_magnitude = 1.0;
    const auto bounded = synthetic_decoder(limits).decode_delta(
        schema, nullptr, context(literal));
    REQUIRE_FALSE(bounded);
    REQUIRE(bounded.error);
    CHECK(bounded.error->code ==
          goldsrc::DeltaValueErrorCode::numeric_magnitude_exceeded);
}

TEST_CASE("Synthetic angle normalizes unsigned quantized values to degrees",
          "[goldsrc][delta-value][angle]")
{
    const auto schema = parse_schema("angle_t", kAngleField);
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0x40U}};
    const auto result = synthetic_decoder().decode_delta(
        schema, nullptr, context(literal));
    REQUIRE(result);
    CHECK(std::get<double>(result.state->fields()[0U].value()) ==
          Catch::Approx(90.0));
}

TEST_CASE("Time-window codecs remain typed evidence boundaries",
          "[goldsrc][delta-value][time][evidence]")
{
    constexpr std::array all_changed{
        std::byte{0x01U}, std::byte{0x01U}};
    const auto time8 = parse_schema("time8_t", kTimeWindow8Field);
    const auto result8 = synthetic_decoder().decode_delta(
        time8, nullptr, context(all_changed));
    REQUIRE_FALSE(result8);
    REQUIRE(result8.error);
    CHECK(result8.error->code == goldsrc::DeltaValueErrorCode::evidence_pending);
    CHECK(result8.error->field_index == 0U);
    CHECK(result8.bits_consumed == 0U);

    const auto time_big = parse_schema("time_big_t", kTimeWindowBigField);
    const auto result_big = synthetic_decoder().decode_delta(
        time_big, nullptr, context(all_changed));
    REQUIRE_FALSE(result_big);
    REQUIRE(result_big.error);
    CHECK(result_big.error->code ==
          goldsrc::DeltaValueErrorCode::evidence_pending);
}

TEST_CASE("Synthetic string fields use bounded owning u16-length bytes",
          "[goldsrc][delta-value][string]")
{
    const auto schema = parse_schema("string_t", kStringField);
    // count=1, mask=1, little-endian length=3, owning bytes a, NUL, b.
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0x03U},
        std::byte{0x00U}, std::byte{'a'}, std::byte{0x00U}, std::byte{'b'}};
    const auto result = synthetic_decoder().decode_delta(
        schema, nullptr, context(literal));
    REQUIRE(result);
    const auto& value = std::get<std::string>(
        result.state->fields()[0U].value());
    REQUIRE(value.size() == 3U);
    CHECK(value[0U] == 'a');
    CHECK(value[1U] == '\0');
    CHECK(value[2U] == 'b');

    auto limits = goldsrc::GoldSrcDeltaValueLimits{};
    limits.maximum_string_bytes = 2U;
    const auto bounded = synthetic_decoder(limits).decode_delta(
        schema, nullptr, context(literal));
    REQUIRE_FALSE(bounded);
    REQUIRE(bounded.error);
    CHECK(bounded.error->code ==
          goldsrc::DeltaValueErrorCode::string_limit_exceeded);

    auto exact_total_limits = goldsrc::GoldSrcDeltaValueLimits{};
    exact_total_limits.maximum_total_value_bytes = 3U;
    REQUIRE(synthetic_decoder(exact_total_limits).decode_delta(
        schema, nullptr, context(literal)));
    exact_total_limits.maximum_total_value_bytes = 2U;
    const auto total_bounded =
        synthetic_decoder(exact_total_limits).decode_delta(
            schema, nullptr, context(literal));
    REQUIRE_FALSE(total_bounded);
    REQUIRE(total_bounded.error);
    CHECK(total_bounded.error->code ==
          goldsrc::DeltaValueErrorCode::total_value_bytes_exceeded);
}

TEST_CASE("Contiguous unaligned scalar and string bits retain an exact cursor",
          "[goldsrc][delta-value][string][cursor]")
{
    constexpr fixture::Field fields[]{
        {"tiny", 0x0000'0002U, 0U, 3U},
        {"text", 0x0000'0080U, 1U, 1U},
    };
    const auto schema = parse_schema("unaligned_t", fields);
    // mask=0b11; 3-bit value 5; unaligned u16 length 1; byte 'Z'; five
    // final zero bits. This is an independently calculated literal.
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x03U}, std::byte{0x0dU},
        std::byte{0x00U}, std::byte{0xd0U}, std::byte{0x02U}};
    const auto result = synthetic_decoder().decode_delta(
        schema, nullptr, context(literal));
    REQUIRE(result);
    CHECK(std::get<std::uint32_t>(result.state->fields()[0U].value()) == 5U);
    CHECK(std::get<std::string>(result.state->fields()[1U].value()) == "Z");
    CHECK(result.bits_consumed == 48U);
    CHECK(result.next_bit_offset == 48U);
}

TEST_CASE("Sparse multi-field delta preserves unchanged base values",
          "[goldsrc][delta-value][base][mask]")
{
    constexpr fixture::Field fields[]{
        {"first", 0x0000'0001U, 0U, 8U},
        {"second", 0x0000'0001U, 1U, 8U},
        {"third", 0x0000'0001U, 2U, 8U},
    };
    const auto schema = parse_schema("three_t", fields);
    const std::array<goldsrc::DeltaScalarValue, 3U> values{
        std::uint32_t{1U}, std::uint32_t{2U}, std::uint32_t{3U}};
    const auto base = build_object(schema, values);
    // One mask byte selects fields 0 and 2; changed values are contiguous.
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x05U},
        std::byte{0x11U}, std::byte{0x33U}};
    const auto result = synthetic_decoder().decode_delta(
        schema, &base, context(literal));
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(std::get<std::uint32_t>(result.state->fields()[0U].value()) == 0x11U);
    CHECK(std::get<std::uint32_t>(result.state->fields()[1U].value()) == 2U);
    CHECK(std::get<std::uint32_t>(result.state->fields()[2U].value()) == 0x33U);
    CHECK(std::get<std::uint32_t>(base.fields()[0U].value()) == 1U);
    CHECK(std::get<std::uint32_t>(base.fields()[2U].value()) == 3U);
}

TEST_CASE("Registry overload performs exact schema-name reuse",
          "[goldsrc][delta-value][registry]")
{
    const auto schema = parse_schema("registry_t", kByteField);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(schema));
    const auto registry = std::move(builder).publish();
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0x2aU}};

    const auto decoded = synthetic_decoder().decode_delta(
        registry, "registry_t", nullptr, context(literal));
    REQUIRE(decoded);
    CHECK(decoded.state->schema_name() == "registry_t");

    const auto absent = synthetic_decoder().decode_delta(
        registry, "REGISTRY_T", nullptr, context(literal));
    REQUIRE_FALSE(absent);
    REQUIRE(absent.error);
    CHECK(absent.error->code == goldsrc::DeltaValueErrorCode::schema_not_found);
}

TEST_CASE("Multiple mask bytes map their highest LSB-first bit to wire order",
          "[goldsrc][delta-value][mask]")
{
    constexpr fixture::Field fields[]{
        {"f0", 1U, 0U, 8U}, {"f1", 1U, 1U, 8U},
        {"f2", 1U, 2U, 8U}, {"f3", 1U, 3U, 8U},
        {"f4", 1U, 4U, 8U}, {"f5", 1U, 5U, 8U},
        {"f6", 1U, 6U, 8U}, {"f7", 1U, 7U, 8U},
        {"f8", 1U, 8U, 8U},
    };
    const auto schema = parse_schema("nine_t", fields);
    const std::array<goldsrc::DeltaScalarValue, 9U> values{
        std::uint32_t{0U}, std::uint32_t{1U}, std::uint32_t{2U},
        std::uint32_t{3U}, std::uint32_t{4U}, std::uint32_t{5U},
        std::uint32_t{6U}, std::uint32_t{7U}, std::uint32_t{8U},
    };
    const auto base = build_object(schema, values);
    // Two mask bytes, only field 8 (bit 0 of byte 1) selected.
    constexpr std::array literal{
        std::byte{0x02U}, std::byte{0x00U},
        std::byte{0x01U}, std::byte{0x5aU}};
    const auto decoded = synthetic_decoder().decode_delta(
        schema, &base, context(literal));
    REQUIRE(decoded);
    CHECK(std::get<std::uint32_t>(
              decoded.state->fields()[7U].value()) == 7U);
    CHECK(std::get<std::uint32_t>(
              decoded.state->fields()[8U].value()) == 0x5aU);
}

TEST_CASE("Runtime masks reject limits, malformed lengths, and stray field bits",
          "[goldsrc][delta-value][mask][limit]")
{
    const auto one_schema = parse_schema("one_t", kByteField);
    constexpr std::array malformed_length{
        std::byte{0x02U}, std::byte{0x00U}, std::byte{0x00U}};
    const auto malformed = synthetic_decoder().decode_delta(
        one_schema, nullptr, context(malformed_length));
    REQUIRE_FALSE(malformed);
    REQUIRE(malformed.error);
    CHECK(malformed.error->code ==
          goldsrc::DeltaValueErrorCode::mask_length_exceeds_schema);

    constexpr std::array stray_bit{
        std::byte{0x01U}, std::byte{0x80U}};
    const auto stray = synthetic_decoder().decode_delta(
        one_schema, nullptr, context(stray_bit));
    REQUIRE_FALSE(stray);
    REQUIRE(stray.error);
    CHECK(stray.error->code ==
          goldsrc::DeltaValueErrorCode::mask_bit_out_of_range);

    constexpr fixture::Field nine_fields[]{
        {"f0", 1U, 0U, 8U}, {"f1", 1U, 1U, 8U},
        {"f2", 1U, 2U, 8U}, {"f3", 1U, 3U, 8U},
        {"f4", 1U, 4U, 8U}, {"f5", 1U, 5U, 8U},
        {"f6", 1U, 6U, 8U}, {"f7", 1U, 7U, 8U},
        {"f8", 1U, 8U, 8U},
    };
    const auto nine_schema = parse_schema("nine_limit_t", nine_fields);
    auto limits = goldsrc::GoldSrcDeltaValueLimits{};
    limits.maximum_mask_bytes = 1U;
    const auto over_limit = synthetic_decoder(limits).decode_delta(
        nine_schema, nullptr, context(malformed_length));
    REQUIRE_FALSE(over_limit);
    REQUIRE(over_limit.error);
    CHECK(over_limit.error->code ==
          goldsrc::DeltaValueErrorCode::mask_byte_limit_exceeded);
}

TEST_CASE("Truncated masks and values fail without publishing partial state",
          "[goldsrc][delta-value][transaction]")
{
    const auto byte_schema = parse_schema("byte_t", kByteField);
    constexpr std::array truncated_mask{std::byte{0x01U}};
    const auto mask_result = synthetic_decoder().decode_delta(
        byte_schema, nullptr, context(truncated_mask));
    REQUIRE_FALSE(mask_result);
    REQUIRE(mask_result.error);
    CHECK(mask_result.error->code == goldsrc::DeltaValueErrorCode::truncated_mask);
    CHECK_FALSE(mask_result.state);
    CHECK(mask_result.bits_consumed == 0U);
    CHECK(mask_result.next_bit_offset == 0U);

    const auto short_schema = parse_schema("short_t", kShortField);
    constexpr std::array truncated_value{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0xffU}};
    const auto value_result = synthetic_decoder().decode_delta(
        short_schema, nullptr, context(truncated_value));
    REQUIRE_FALSE(value_result);
    REQUIRE(value_result.error);
    CHECK(value_result.error->code == goldsrc::DeltaValueErrorCode::truncated_value);
    CHECK_FALSE(value_result.state);
    CHECK(value_result.bits_consumed == 0U);
}

TEST_CASE("Runtime delta enforces zero padding and exact object end",
          "[goldsrc][delta-value][padding][cursor]")
{
    constexpr fixture::Field tiny_field[]{
        {"tiny", 0x0000'0002U, 0U, 3U},
    };
    const auto tiny_schema = parse_schema("tiny_t", tiny_field);
    constexpr std::array valid{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0x05U}};
    const auto valid_result = synthetic_decoder().decode_delta(
        tiny_schema, nullptr, context(valid));
    REQUIRE(valid_result);
    CHECK(valid_result.bits_consumed == 24U);

    constexpr std::array nonzero_padding{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0x0dU}};
    const auto padding_result = synthetic_decoder().decode_delta(
        tiny_schema, nullptr, context(nonzero_padding));
    REQUIRE_FALSE(padding_result);
    REQUIRE(padding_result.error);
    CHECK(padding_result.error->code ==
          goldsrc::DeltaValueErrorCode::nonzero_padding);

    const auto byte_schema = parse_schema("byte_t", kByteField);
    constexpr std::array trailing{
        std::byte{0x01U}, std::byte{0x01U},
        std::byte{0x2aU}, std::byte{0x00U}};
    const auto trailing_result = synthetic_decoder().decode_delta(
        byte_schema, nullptr, context(trailing));
    REQUIRE_FALSE(trailing_result);
    REQUIRE(trailing_result.error);
    CHECK(trailing_result.error->code ==
          goldsrc::DeltaValueErrorCode::unexpected_trailing_bits);
}

TEST_CASE("Runtime delta reports an exact nonzero source cursor",
          "[goldsrc][delta-value][cursor]")
{
    const auto schema = parse_schema("byte_t", kByteField);
    constexpr std::array packet{
        std::byte{0xccU}, std::byte{0x01U},
        std::byte{0x01U}, std::byte{0x2aU}};
    const auto result = synthetic_decoder().decode_delta(
        schema, nullptr, context(packet, 8U, 24U));
    REQUIRE(result);
    CHECK(result.bits_consumed == 24U);
    CHECK(result.next_bit_offset == 32U);
    CHECK(result.next_byte_offset == 4U);
}

TEST_CASE("Runtime numeric magnitude rejects values without clamping",
          "[goldsrc][delta-value][numeric][limit]")
{
    const auto schema = parse_schema("byte_t", kByteField);
    constexpr std::array literal{
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0x65U}};
    auto limits = goldsrc::GoldSrcDeltaValueLimits{};
    limits.maximum_numeric_magnitude = 100.0;
    const auto result = synthetic_decoder(limits).decode_delta(
        schema, nullptr, context(literal));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          goldsrc::DeltaValueErrorCode::numeric_magnitude_exceeded);
    CHECK_FALSE(result.state);
    CHECK(result.bits_consumed == 0U);

    const std::array<goldsrc::DeltaScalarValue, 1U> base_value{
        std::uint32_t{101U},
    };
    const auto loose_base = build_object(schema, base_value);
    constexpr std::array zero_mask{std::byte{0x00U}};
    const auto strict_base_result = synthetic_decoder(limits).decode_delta(
        schema, &loose_base, context(zero_mask));
    REQUIRE_FALSE(strict_base_result);
    REQUIRE(strict_base_result.error);
    CHECK(strict_base_result.error->code ==
          goldsrc::DeltaValueErrorCode::invalid_base);
}

TEST_CASE("Runtime delta base retains the exact source schema descriptor",
          "[goldsrc][delta-value][base][schema-identity]")
{
    const auto expected_schema = parse_schema("byte_t", kByteField);
    const auto foreign_schema = parse_schema("byte_t", kNarrowByteField);
    const std::array<goldsrc::DeltaScalarValue, 1U> values{
        std::uint32_t{17U},
    };
    const auto foreign_base = build_object(foreign_schema, values);
    CHECK_FALSE(foreign_base.matches_schema(expected_schema));

    constexpr std::array zero_mask{std::byte{0x00U}};
    const auto decoded = synthetic_decoder().decode_delta(
        expected_schema, &foreign_base, context(zero_mask));
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code == goldsrc::DeltaValueErrorCode::invalid_base);
    CHECK_FALSE(decoded.state);
}

} // namespace

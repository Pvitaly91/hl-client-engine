#include <hlclient/goldsrc/delta_description.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

class TestBitWriter final {
public:
    void write(const std::uint32_t value, const std::size_t width)
    {
        for (std::size_t index = 0U; index < width; ++index) {
            const auto byte_index = bit_offset_ >> 3U;
            if (byte_index == bytes_.size()) {
                bytes_.push_back(std::byte{0U});
            }
            if (((value >> index) & 1U) != 0U) {
                bytes_[byte_index] |= static_cast<std::byte>(
                    1U << (bit_offset_ & 7U));
            }
            ++bit_offset_;
        }
    }

    void string(const std::string_view value)
    {
        for (const auto character : value) {
            write(static_cast<std::uint8_t>(character), 8U);
        }
        write(0U, 8U);
    }

    void align_zero()
    {
        while ((bit_offset_ & 7U) != 0U) {
            write(0U, 1U);
        }
    }

    [[nodiscard]] std::size_t bit_offset() const noexcept { return bit_offset_; }
    [[nodiscard]] std::vector<std::byte>& bytes() noexcept { return bytes_; }
    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return bytes_; }

private:
    std::vector<std::byte> bytes_;
    std::size_t bit_offset_{0U};
};

struct FieldFixture {
    std::string_view name;
    std::uint32_t type;
    std::uint16_t offset;
    std::uint8_t bits;
    std::uint32_t premultiply;
    std::uint32_t postmultiply;
    std::uint8_t storage_size{1U};
};

struct EncodedSchema {
    std::vector<std::byte> bytes;
    std::size_t padding_start_bit{0U};
    std::size_t padding_bits{0U};
};

[[nodiscard]] EncodedSchema encode_schema(
    const std::string_view name,
    const std::span<const FieldFixture> fields)
{
    TestBitWriter writer;
    writer.write(goldsrc::kDeltaDescriptionOpcode, 8U);
    writer.string(name);
    writer.write(static_cast<std::uint32_t>(fields.size()), 16U);
    for (const auto& field : fields) {
        writer.write(1U, 3U);
        const auto mask = field.offset == 0U ? 0x7bU : 0x7fU;
        writer.write(mask, 8U);
        writer.write(field.type, 32U);
        writer.string(field.name);
        if (field.offset != 0U) {
            writer.write(field.offset, 16U);
        }
        writer.write(field.storage_size, 8U);
        writer.write(field.bits, 8U);
        writer.write(field.premultiply, 32U);
        writer.write(field.postmultiply, 32U);
    }
    const auto padding_start = writer.bit_offset();
    const auto padding_bits = (8U - (padding_start & 7U)) & 7U;
    writer.align_zero();
    return {writer.bytes(), padding_start, padding_bits};
}

constexpr FieldFixture kEventFields[]{
    {"entindex", 0x0000'0002U, 0U, 11U, 4'000U, 4'000U},
    {"origin[0]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
    {"angles[1]", 0x0000'0010U, 8U, 8U, 400U, 4'000U},
};

constexpr FieldFixture kUserCmdFields[]{
    {"lerp_msec", 0x0000'0002U, 0U, 9U, 4'000U, 4'000U},
    {"viewangles[0]", 0x0000'0010U, 4U, 16U, 400U, 4'000U},
};

// Independent literal for one stock-compatible schema. It was derived from
// the confirmed LSB-first grammar, not from a production or test encoder:
// opcode 14, schema "x", one byte field "y", offset omitted, 8 significant
// bits, and fixed-point multipliers 4000/4000. The encoded body is 179 bits
// followed by five zero alignment bits.
constexpr std::array<std::byte, 23U> kLiteralOneSchema{
    std::byte{0x0eU}, std::byte{0x78U}, std::byte{0x00U}, std::byte{0x01U},
    std::byte{0x00U}, std::byte{0xd9U}, std::byte{0x0bU}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0x03U},
    std::byte{0x08U}, std::byte{0x40U}, std::byte{0x00U}, std::byte{0x7dU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x7dU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
};

[[nodiscard]] goldsrc::DeltaDescriptionParseResult parse_event_schema()
{
    const auto fixture = encode_schema("event_t", kEventFields);
    return goldsrc::DeltaDescriptionParser{}.parse(fixture.bytes, 0U);
}

TEST_CASE("Delta description parser matches an independent literal schema fixture",
          "[goldsrc][delta][fixture]")
{
    const auto result = goldsrc::DeltaDescriptionParser{}.parse(
        kLiteralOneSchema,
        0U);

    REQUIRE(result);
    REQUIRE(result.schema);
    CHECK(result.error == std::nullopt);
    CHECK(result.bits_consumed == 184U);
    CHECK(result.bytes_consumed == kLiteralOneSchema.size());
    CHECK(result.next_byte_offset == kLiteralOneSchema.size());
    CHECK(result.next_bit_offset == 0U);
    CHECK(result.schema->name() == "x");
    REQUIRE(result.schema->field_count() == 1U);
    const auto& field = result.schema->fields().front();
    CHECK(field.name() == "y");
    CHECK(field.type_flags().base_type() ==
          goldsrc::DeltaFieldBaseType::byte_value);
    CHECK_FALSE(field.type_flags().signed_value());
    CHECK(field.offset() == 0U);
    CHECK(field.storage_size() == 1U);
    CHECK(field.significant_bits() == 8U);
    CHECK(field.premultiply_wire_value() == 4'000U);
    CHECK(field.postmultiply_wire_value() == 4'000U);
}

TEST_CASE("Delta description parser reconstructs confirmed bit-packed fields", "[goldsrc][delta]")
{
    const auto fixture = encode_schema("event_t", kEventFields);
    const auto result = goldsrc::DeltaDescriptionParser{}.parse(fixture.bytes, 0U);

    REQUIRE(result);
    REQUIRE(result.schema);
    CHECK(result.error == std::nullopt);
    CHECK(result.next_bit_offset == 0U);
    CHECK(result.next_byte_offset == fixture.bytes.size());
    CHECK(result.schema->message_bytes() == fixture.bytes.size());
    CHECK(result.schema->name() == "event_t");
    REQUIRE(result.schema->field_count() == 3U);

    const auto& first = result.schema->fields()[0U];
    CHECK(first.name() == "entindex");
    CHECK(first.type_flags().base_type() == goldsrc::DeltaFieldBaseType::short_value);
    CHECK_FALSE(first.type_flags().signed_value());
    CHECK(first.offset() == 0U);
    CHECK(first.presence_mask() == 0x7bU);
    CHECK(first.significant_bits() == 11U);
    CHECK(first.premultiply() == 1.0);

    const auto& second = result.schema->fields()[1U];
    CHECK(second.type_flags().base_type() == goldsrc::DeltaFieldBaseType::float_value);
    CHECK(second.type_flags().signed_value());
    CHECK(second.offset() == 4U);
    CHECK(second.presence_mask() == 0x7fU);
    CHECK(second.premultiply_wire_value() == 32'000U);
    CHECK(second.premultiply() == 8.0);
    CHECK(second.postmultiply() == 1.0);
}

TEST_CASE("Delta description parser rejects every truncated byte boundary transactionally", "[goldsrc][delta]")
{
    const auto fixture = encode_schema("event_t", kEventFields);
    goldsrc::DeltaDescriptionParser parser;
    for (std::size_t size = 0U; size < fixture.bytes.size(); ++size) {
        INFO("truncated size=" << size);
        const auto result = parser.parse(
            std::span<const std::byte>{fixture.bytes.data(), size},
            0U);
        CHECK_FALSE(result);
        CHECK_FALSE(result.schema);
        CHECK(result.bits_consumed == 0U);
        CHECK(result.bytes_consumed == 0U);
    }
}

TEST_CASE("Delta description parser rejects every truncated bit boundary transactionally", "[goldsrc][delta]")
{
    const auto fixture = encode_schema("event_t", kEventFields);
    const auto total_bits = fixture.bytes.size() * 8U;
    goldsrc::DeltaDescriptionParser parser;
    for (std::size_t bit_length = 8U; bit_length < total_bits; ++bit_length) {
        INFO("truncated bit length=" << bit_length);
        const auto result = parser.parse(fixture.bytes, 0U, bit_length);
        CHECK_FALSE(result);
        CHECK_FALSE(result.schema);
        CHECK(result.bits_consumed == 0U);
        CHECK(result.bytes_consumed == 0U);
    }
}

TEST_CASE("Delta description parser rejects non-zero alignment padding", "[goldsrc][delta]")
{
    auto fixture = encode_schema("event_t", kEventFields);
    REQUIRE(fixture.padding_bits > 0U);
    const auto byte_index = fixture.padding_start_bit >> 3U;
    const auto bit_index = fixture.padding_start_bit & 7U;
    fixture.bytes[byte_index] |= static_cast<std::byte>(1U << bit_index);

    const auto result = goldsrc::DeltaDescriptionParser{}.parse(fixture.bytes, 0U);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == goldsrc::DeltaDescriptionErrorCode::nonzero_padding);
}

TEST_CASE("Delta description parser rejects duplicate field names and invalid flags", "[goldsrc][delta]")
{
    constexpr FieldFixture duplicate[]{
        {"same", 0x0000'0001U, 0U, 8U, 4'000U, 4'000U},
        {"same", 0x0000'0001U, 1U, 8U, 4'000U, 4'000U},
    };
    const auto duplicate_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("duplicate_t", duplicate).bytes,
        0U);
    REQUIRE_FALSE(duplicate_result);
    REQUIRE(duplicate_result.error);
    CHECK(duplicate_result.error->code ==
          goldsrc::DeltaDescriptionErrorCode::duplicate_field_name);

    constexpr FieldFixture conflicting[]{
        {"bad", 0x0000'0003U, 0U, 8U, 4'000U, 4'000U},
    };
    const auto conflicting_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("bad_t", conflicting).bytes,
        0U);
    REQUIRE_FALSE(conflicting_result);
    REQUIRE(conflicting_result.error);
    CHECK(conflicting_result.error->code ==
          goldsrc::DeltaDescriptionErrorCode::conflicting_base_type_flags);
}

TEST_CASE("Delta description parser enforces bounded names and field counts",
          "[goldsrc][delta][limit]")
{
    const std::string exact_schema_name(64U, 's');
    const auto exact_schema_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema(exact_schema_name, kEventFields).bytes,
        0U);
    REQUIRE(exact_schema_result);
    CHECK(exact_schema_result.schema->name().size() == 64U);

    const std::string long_schema_name(65U, 's');
    const auto long_schema = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema(long_schema_name, kEventFields).bytes,
        0U);
    REQUIRE_FALSE(long_schema);
    REQUIRE(long_schema.error);
    CHECK(long_schema.error->code ==
          goldsrc::DeltaDescriptionErrorCode::schema_name_too_long);

    const std::string exact_field_name(64U, 'f');
    const std::array exact_field{
        FieldFixture{exact_field_name, 1U, 0U, 8U, 4'000U, 4'000U}};
    const auto exact_field_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("exact_t", exact_field).bytes,
        0U);
    REQUIRE(exact_field_result);
    CHECK(exact_field_result.schema->fields().front().name().size() == 64U);

    const std::string long_field_name(65U, 'f');
    const std::array long_field{
        FieldFixture{long_field_name, 1U, 0U, 8U, 4'000U, 4'000U}};
    const auto long_field_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("long_t", long_field).bytes,
        0U);
    REQUIRE_FALSE(long_field_result);
    REQUIRE(long_field_result.error);
    CHECK(long_field_result.error->code ==
          goldsrc::DeltaDescriptionErrorCode::field_name_too_long);

    auto limits = goldsrc::DeltaDescriptionLimits{};
    limits.maximum_fields_per_schema = 2U;
    const auto exact_count = goldsrc::DeltaDescriptionParser{limits}.parse(
        encode_schema("two_t", std::span{kEventFields}.first(2U)).bytes,
        0U);
    REQUIRE(exact_count);
    const auto over_count = goldsrc::DeltaDescriptionParser{limits}.parse(
        encode_schema("three_t", kEventFields).bytes,
        0U);
    REQUIRE_FALSE(over_count);
    REQUIRE(over_count.error);
    CHECK(over_count.error->code ==
          goldsrc::DeltaDescriptionErrorCode::field_count_limit_exceeded);

    const std::span<const FieldFixture> empty;
    const auto zero_count = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("empty_t", empty).bytes,
        0U);
    REQUIRE_FALSE(zero_count);
    REQUIRE(zero_count.error);
    CHECK(zero_count.error->code ==
          goldsrc::DeltaDescriptionErrorCode::zero_field_count);
}

TEST_CASE("Delta description parser rejects invalid numeric metadata",
          "[goldsrc][delta][numeric]")
{
    constexpr std::array zero_multiplier{
        FieldFixture{"value", 1U, 0U, 8U, 0U, 4'000U}};
    const auto zero_multiplier_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("zero_t", zero_multiplier).bytes,
        0U);
    REQUIRE_FALSE(zero_multiplier_result);
    REQUIRE(zero_multiplier_result.error);
    CHECK(zero_multiplier_result.error->code ==
          goldsrc::DeltaDescriptionErrorCode::invalid_multiplier);

    constexpr std::array excessive_bits{
        FieldFixture{"value", 1U, 0U, 9U, 4'000U, 4'000U}};
    const auto excessive_bits_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("bits_t", excessive_bits).bytes,
        0U);
    REQUIRE_FALSE(excessive_bits_result);
    REQUIRE(excessive_bits_result.error);
    CHECK(excessive_bits_result.error->code ==
          goldsrc::DeltaDescriptionErrorCode::invalid_significant_bits);

    constexpr std::array invalid_storage{
        FieldFixture{"value", 1U, 0U, 8U, 4'000U, 4'000U, 2U}};
    const auto invalid_storage_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("storage_t", invalid_storage).bytes,
        0U);
    REQUIRE_FALSE(invalid_storage_result);
    REQUIRE(invalid_storage_result.error);
    CHECK(invalid_storage_result.error->code ==
          goldsrc::DeltaDescriptionErrorCode::invalid_storage_size);

    constexpr std::array reserved_flag{
        FieldFixture{"value", 0x0000'0101U, 0U, 8U, 4'000U, 4'000U}};
    const auto reserved_flag_result = goldsrc::DeltaDescriptionParser{}.parse(
        encode_schema("reserved_t", reserved_flag).bytes,
        0U);
    REQUIRE_FALSE(reserved_flag_result);
    REQUIRE(reserved_flag_result.error);
    CHECK(reserved_flag_result.error->code ==
          goldsrc::DeltaDescriptionErrorCode::reserved_type_flag);
}

TEST_CASE("Delta description parse result owns names after input lifetime ends",
          "[goldsrc][delta][ownership]")
{
    const auto result = [] {
        auto bytes = encode_schema("owned_t", kEventFields).bytes;
        return goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    }();
    REQUIRE(result);
    REQUIRE(result.schema);
    CHECK(result.schema->name() == "owned_t");
    CHECK(result.schema->fields()[1U].name() == "origin[0]");
}

TEST_CASE("Delta registry publishes ordered immutable exact-name lookup", "[goldsrc][delta]")
{
    auto first = parse_event_schema();
    REQUIRE(first);
    const auto second_fixture = encode_schema("usercmd_t", kUserCmdFields);
    auto second = goldsrc::DeltaDescriptionParser{}.parse(second_fixture.bytes, 0U);
    REQUIRE(second);

    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*first.schema));
    REQUIRE(builder.insert(*second.schema));
    const auto before_duplicate = builder.candidate_schemas().size();
    const auto duplicate = builder.insert(*first.schema);
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code ==
          goldsrc::DeltaRegistryErrorCode::duplicate_schema_name);
    CHECK(builder.candidate_schemas().size() == before_duplicate);

    auto registry = std::move(builder).publish();
    CHECK(registry.schema_count() == 2U);
    CHECK(registry.total_field_count() == 5U);
    REQUIRE(registry.find_exact("event_t") != nullptr);
    CHECK(registry.find_exact("EVENT_T") == nullptr);
    CHECK(registry.schemas()[0U].name() == "event_t");
    CHECK(registry.schemas()[1U].name() == "usercmd_t");
}

TEST_CASE("Delta registry enforces aggregate bounds without partial publication", "[goldsrc][delta]")
{
    auto parsed = parse_event_schema();
    REQUIRE(parsed);
    auto limits = goldsrc::DeltaDescriptionLimits{};
    limits.maximum_total_fields = 2U;
    goldsrc::DeltaSchemaRegistryBuilder builder{limits};
    const auto result = builder.insert(*parsed.schema);
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          goldsrc::DeltaRegistryErrorCode::total_field_limit_exceeded);
    CHECK(builder.candidate_schemas().empty());
}

} // namespace

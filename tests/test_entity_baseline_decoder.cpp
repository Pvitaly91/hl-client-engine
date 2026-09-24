#include <hlclient/goldsrc/entity_baseline_decoder.hpp>

#include "delta_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace fixture = hlclient::test::delta_fixture;

constexpr std::array kByteSchema{
    std::byte{0x0e}, std::byte{0x78}, std::byte{0x00}, std::byte{0x01},
    std::byte{0x00}, std::byte{0xd9}, std::byte{0x0b}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xc8}, std::byte{0x03},
    std::byte{0x08}, std::byte{0x40}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x7d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

[[nodiscard]] goldsrc::DeltaSchemaRegistryState byte_schemas()
{
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(kByteSchema, 0U);
    REQUIRE(parsed);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::move(builder).publish();
}

[[nodiscard]] goldsrc::DeltaSchemaRegistryState time_schemas()
{
    constexpr fixture::Field fields[]{
        {"animtime", 0x0000'0020U, 0U, 8U, 4'000U, 4'000U},
    };
    const auto bytes = fixture::schema("time", fields);
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE(parsed);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::move(builder).publish();
}

[[nodiscard]] goldsrc::OwnedServicePayload baseline_payload(
    const std::span<const std::byte> bytes)
{
    auto payload = fixture::owning_payload(
        std::vector<std::byte>{bytes.begin(), bytes.end()});
    payload.source_sequence = 17U;
    return payload;
}

[[nodiscard]] goldsrc::EntityBaselineDecodeResult decode_literal(
    const std::span<const std::byte> bytes,
    const goldsrc::DeltaSchemaRegistryState& schemas,
    const std::size_t start_bit = 0U,
    const std::string_view schema_name = "x")
{
    auto payload = baseline_payload(bytes);
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U, start_bit, payload.bytes.size());
    REQUIRE(cursor);
    return goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
        goldsrc::EntityBaselineDecodeInput{
            &payload, *cursor, 9U, 3U, 1U, schema_name, schema_name,
            schema_name, payload.bytes.size() * 8U - start_bit},
        schemas);
}

} // namespace

TEST_CASE("GoldSrc baseline decoder rejects non-owning or wrong-direction input")
{
    using namespace hlclient::goldsrc;
    DeltaSchemaRegistryBuilder schemas;
    OwnedServicePayload payload;
    payload.decompressed = false;
    payload.direction = NetchanDirection::client_to_server;
    const auto cursor = StockRuntimeSourceCursor::create(0U, 0U, 0U);
    REQUIRE(cursor.has_value());
    const EntityBaselineDecodeInput input{&payload, *cursor, 0U, 1U, 1U,
                                          "entity_state_t", "entity_state_player_t",
                                          "custom_entity_state_t", 0U};
    const auto result = GoldSrcEntityBaselineDecoder{}.decode(input,
                                                                std::move(schemas).publish());
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == EntityBaselineDecodeErrorCode::payload_not_decompressed);
}

TEST_CASE("GoldSrc baseline decoder has bounded configuration")
{
    using namespace hlclient::goldsrc;
    EntityBaselineDecodeLimits limits;
    limits.maximum_instanced_baselines = 64U;
    CHECK_FALSE(GoldSrcEntityBaselineDecoder{limits}.valid_configuration());
}

TEST_CASE("GoldSrc baseline consumes the full literal 0xffff terminator and count",
          "[goldsrc][baseline][literal][terminator]")
{
    const auto schemas = byte_schemas();

    SECTION("empty baseline list")
    {
        constexpr std::array bytes{
            std::byte{0xff}, std::byte{0xff}, std::byte{0x00}};
        const auto result = decode_literal(bytes, schemas);
        REQUIRE(result);
        CHECK(result.entity_count == 0U);
        CHECK(result.instanced_count == 0U);
        CHECK(result.bits_consumed == 24U);
        CHECK(result.end_cursor.absolute_bit_offset() == 24U);
    }

    SECTION("nonzero instanced count follows the full terminator")
    {
        // ffff | count=1 (6 bits) | public delta 1/01/value=42 | pad.
        constexpr std::array bytes{
            std::byte{0xff}, std::byte{0xff}, std::byte{0x41},
            std::byte{0x02}, std::byte{0x54}, std::byte{0x00}};
        const auto result = decode_literal(bytes, schemas);
        REQUIRE(result);
        CHECK(result.entity_count == 0U);
        CHECK(result.instanced_count == 1U);
        REQUIRE(result.registry);
        const auto* baseline = result.registry->find_exact(
            goldsrc::EntityBaselineKey::for_alternate_slot(0U));
        REQUIRE(baseline != nullptr);
        const auto* field = baseline->object().find_exact("y");
        REQUIRE(field != nullptr);
        CHECK(std::get<std::uint32_t>(field->value()) == 42U);
        CHECK(result.end_cursor.absolute_bit_offset() == 48U);
    }

    SECTION("next service opcode remains unconsumed")
    {
        constexpr std::array bytes{
            std::byte{0xff}, std::byte{0xff}, std::byte{0x00},
            std::byte{0x01}};
        auto payload = baseline_payload(bytes);
        const auto cursor = goldsrc::StockRuntimeSourceCursor::create(
            0U, 0U, payload.bytes.size());
        REQUIRE(cursor);
        const auto result = goldsrc::GoldSrcEntityBaselineDecoder{}.decode(
            goldsrc::EntityBaselineDecodeInput{
                &payload, *cursor, 9U, 3U, 1U, "x", "x", "x"},
            schemas);
        REQUIRE(result);
        CHECK(result.end_cursor.byte_offset() == 3U);
        CHECK(payload.bytes[result.end_cursor.byte_offset()] == std::byte{0x01});
    }
}

TEST_CASE("GoldSrc full baseline terminator works at literal bit offsets",
          "[goldsrc][baseline][literal][alignment]")
{
    const auto schemas = byte_schemas();
    struct Case final {
        std::vector<std::byte> bytes;
        std::size_t offset;
        std::size_t consumed;
    };
    const std::array cases{
        Case{{std::byte{0xfe}, std::byte{0xff}, std::byte{0x01}}, 1U, 23U},
        Case{{std::byte{0xf8}, std::byte{0xff}, std::byte{0x07},
              std::byte{0x00}}, 3U, 29U},
        Case{{std::byte{0x80}, std::byte{0xff}, std::byte{0x7f},
              std::byte{0x00}}, 7U, 25U},
    };
    for (const auto& item : cases) {
        CAPTURE(item.offset);
        const auto result = decode_literal(item.bytes, schemas, item.offset);
        REQUIRE(result);
        CHECK(result.bits_consumed == item.consumed);
        CHECK(result.end_cursor.byte_aligned());
    }
}

TEST_CASE("GoldSrc baseline rejects truncated and merely 11-bit terminators",
          "[goldsrc][baseline][literal][negative]")
{
    const auto schemas = byte_schemas();

    const auto truncated_terminator = decode_literal(
        std::array{std::byte{0xff}}, schemas);
    REQUIRE_FALSE(truncated_terminator);
    REQUIRE(truncated_terminator.error);
    CHECK(truncated_terminator.error->code ==
          goldsrc::EntityBaselineDecodeErrorCode::truncated_terminator);

    const auto truncated_count = decode_literal(
        std::array{std::byte{0xff}, std::byte{0xff}}, schemas);
    REQUIRE_FALSE(truncated_count);
    REQUIRE(truncated_count.error);
    CHECK(truncated_count.error->code ==
          goldsrc::EntityBaselineDecodeErrorCode::truncated_instanced_count);

    // The low eleven bits are one, but the full 16-bit word is 0x07ff.
    const auto short_marker = decode_literal(
        std::array{std::byte{0xff}, std::byte{0x07}, std::byte{0x00},
                   std::byte{0x00}},
        schemas);
    CHECK_FALSE(short_marker);
}

TEST_CASE("Initial baseline time-window values use fixed GoldSrc time base 1.0",
          "[goldsrc][baseline][time-window]")
{
    const auto schemas = time_schemas();
    // entity=2,type=ordinary | mask 1/01 | raw=50 | ffff | count=0.
    constexpr std::array bytes{
        std::byte{0x02}, std::byte{0x20}, std::byte{0x01},
        std::byte{0x32}, std::byte{0xff}, std::byte{0xff},
        std::byte{0x00}};
    const auto result = decode_literal(bytes, schemas, 0U, "time");
    const auto context = result.error ? result.error->context
                                      : std::string{"no baseline error"};
    INFO(context);
    REQUIRE(result);
    REQUIRE(result.registry);
    const auto* baseline = result.registry->find_exact(
        goldsrc::EntityBaselineKey::for_entity(2U));
    REQUIRE(baseline != nullptr);
    const auto* value = baseline->object().find_exact("animtime");
    REQUIRE(value != nullptr);
    REQUIRE(std::holds_alternative<double>(value->value()));
    CHECK(std::abs(std::get<double>(value->value()) - 0.5) < 0.000'001);
}

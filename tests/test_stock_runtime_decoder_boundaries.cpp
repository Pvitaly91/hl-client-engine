#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::DeltaSchemaRegistryState registry()
{
    const auto bytes = fixture::schema(
        "entity_state_t", fixture::kSchemaAlphaFields);
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::move(builder).publish();
}

[[nodiscard]] goldsrc::DeltaSchemaRegistryState empty_registry()
{
    return goldsrc::DeltaSchemaRegistryBuilder{}.publish();
}

struct BoundaryInput final {
    goldsrc::OwnedServicePayload payload;
    goldsrc::PostResourceResponseBoundary boundary;
};

[[nodiscard]] BoundaryInput input(std::vector<std::byte> bytes)
{
    auto payload = fixture::owning_payload(std::move(bytes));
    const auto source = goldsrc::PostResourceResponseSourcePayloadMetadata{
        payload.direction,
        payload.source_sequence,
        payload.source_reliable,
        payload.reassembled,
        payload.decompressed,
        payload.bytes.size(),
    };
    auto parsed = goldsrc::PostResourceResponseBoundaryParser{}.parse(
        payload.bytes, source);
    REQUIRE(parsed);
    REQUIRE(parsed.boundary);
    return BoundaryInput{std::move(payload), std::move(*parsed.boundary)};
}

void require_transactional_error(
    const goldsrc::StockRuntimeMessageCatalogDecodeResult& result,
    const goldsrc::StockRuntimeDecodeErrorCode code)
{
    CHECK_FALSE(result);
    CHECK_FALSE(result.state);
    REQUIRE(result.error);
    CHECK(result.error->code == code);
    CHECK_FALSE(result.error->context.empty());
}

TEST_CASE("Pending stock runtime decoder never scans an arbitrary suffix",
          "[goldsrc][stock-runtime][boundary][no-resync]")
{
    auto source = input({
        std::byte{0x9aU},
        std::byte{0x01U},
        std::byte{0x9aU},
        std::byte{0x00U},
        std::byte{0xffU},
        std::byte{0x2cU},
        std::byte{0x9aU},
    });
    const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{}.decode(
        source.payload, source.boundary, registry());

    REQUIRE(result);
    REQUIRE(result.state);
    REQUIRE(result.state->catalog().message_count() == 1U);
    const auto& entry = result.state->catalog().entries().front();
    CHECK(entry.opcode() == 0x9aU);
    CHECK(entry.category() ==
          goldsrc::StockRuntimeMessageCategory::unsupported_runtime_message);
    CHECK(entry.start_cursor().absolute_bit_offset() == 0U);
    CHECK(entry.end_cursor() == entry.start_cursor());
    CHECK_FALSE(entry.body_bit_length());
    CHECK(result.state->unsupported_boundary().remaining_payload_byte_count() ==
          source.payload.bytes.size());
    CHECK(result.state->unsupported_boundary().unconfirmed_body_byte_count() ==
          source.payload.bytes.size() - 1U);
    CHECK_FALSE(result.state->ready_state().ready());
    CHECK_FALSE(result.state->ready_state().runtime_generation());
}

TEST_CASE("Every bounded arbitrary suffix stops at the same first runtime cursor",
          "[goldsrc][stock-runtime][boundary][suffix]")
{
    const auto schemas = registry();
    const goldsrc::StockRuntimeMessageCatalogDecoder decoder;
    for (std::size_t suffix_size = 0U; suffix_size <= 256U; ++suffix_size) {
        CAPTURE(suffix_size);
        std::vector<std::byte> bytes(suffix_size + 1U, std::byte{0U});
        bytes.front() = std::byte{0x7dU};
        for (std::size_t index = 0U; index < suffix_size; ++index) {
            bytes[index + 1U] = static_cast<std::byte>(
                (index * 37U + suffix_size * 19U) & 0xffU);
        }
        auto source = input(std::move(bytes));
        const auto result = decoder.decode(
            source.payload, source.boundary, schemas);

        REQUIRE(result);
        REQUIRE(result.state);
        REQUIRE(result.state->catalog().message_count() == 1U);
        const auto& entry = result.state->catalog().entries().front();
        CHECK(entry.opcode() == 0x7dU);
        CHECK(entry.start_cursor().absolute_bit_offset() == 0U);
        CHECK(entry.end_cursor().absolute_bit_offset() == 0U);
        CHECK_FALSE(entry.body_bit_length());
        CHECK(result.state->unsupported_boundary().remaining_payload_byte_count() ==
              suffix_size + 1U);
        CHECK(result.state->unsupported_boundary().remaining_payload_bit_count() ==
              (suffix_size + 1U) * 8U);
        CHECK(result.state->unsupported_boundary().unconfirmed_body_byte_count() ==
              suffix_size);
    }
}

TEST_CASE("Reserved stock runtime v1 fails before inspecting unconfirmed inputs",
          "[goldsrc][stock-runtime][profile][fail-closed]")
{
    auto malformed = input({});
    const auto reserved =
        goldsrc::StockRuntimeCompatibilityProfile::
            stock_protocol_48_build_10210_runtime_v1;
    const goldsrc::StockRuntimeMessageCatalogDecoder decoder{{}, reserved};
    REQUIRE(decoder.valid_configuration());

    const auto result = decoder.decode(
        malformed.payload, malformed.boundary, empty_registry());
    require_transactional_error(
        result, goldsrc::StockRuntimeDecodeErrorCode::evidence_not_confirmed);
    CHECK(goldsrc::to_string(result.error->code) == "evidence_not_confirmed");
}

TEST_CASE("Invalid stock runtime profile fails as a typed configuration domain error",
          "[goldsrc][stock-runtime][profile][invalid]")
{
    auto malformed = input({});
    const auto invalid = static_cast<goldsrc::StockRuntimeCompatibilityProfile>(
        0xffU);
    const goldsrc::StockRuntimeMessageCatalogDecoder decoder{{}, invalid};
    CHECK_FALSE(decoder.valid_configuration());

    const auto result = decoder.decode(
        malformed.payload, malformed.boundary, empty_registry());
    require_transactional_error(
        result, goldsrc::StockRuntimeDecodeErrorCode::invalid_profile);
}

TEST_CASE("Pending stock runtime decoder rejects source and boundary drift transactionally",
          "[goldsrc][stock-runtime][boundary][transactional]")
{
    SECTION("netchan source sequence drift")
    {
        auto source = input({std::byte{0x91U}, std::byte{0x33U}});
        ++source.payload.source_sequence;
        const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{}.decode(
            source.payload, source.boundary, registry());
        require_transactional_error(
            result,
            goldsrc::StockRuntimeDecodeErrorCode::invalid_boundary_geometry);
    }

    SECTION("first opcode drift")
    {
        auto source = input({std::byte{0x91U}, std::byte{0x33U}});
        source.payload.bytes.front() = std::byte{0x92U};
        const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{}.decode(
            source.payload, source.boundary, registry());
        require_transactional_error(
            result,
            goldsrc::StockRuntimeDecodeErrorCode::invalid_boundary_geometry);
    }

    SECTION("payload extent drift")
    {
        auto source = input({std::byte{0x91U}, std::byte{0x33U}});
        source.payload.bytes.push_back(std::byte{0x44U});
        const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{}.decode(
            source.payload, source.boundary, registry());
        require_transactional_error(
            result,
            goldsrc::StockRuntimeDecodeErrorCode::invalid_boundary_geometry);
    }
}

TEST_CASE("Pending stock runtime decoder enforces registry payload and metadata bounds",
          "[goldsrc][stock-runtime][limits][transactional]")
{
    SECTION("published delta registry required")
    {
        auto source = input({std::byte{0x93U}});
        const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{}.decode(
            source.payload, source.boundary, empty_registry());
        require_transactional_error(
            result,
            goldsrc::StockRuntimeDecodeErrorCode::missing_delta_registry);
    }

    SECTION("payload bound")
    {
        auto source = input({std::byte{0x93U}, std::byte{0x00U}});
        auto limits = goldsrc::StockRuntimeDecodeLimits{};
        limits.maximum_payload_bytes = 1U;
        const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{limits}.decode(
            source.payload, source.boundary, registry());
        require_transactional_error(
            result, goldsrc::StockRuntimeDecodeErrorCode::payload_too_large);
    }

    SECTION("metadata bound")
    {
        auto source = input({std::byte{0x93U}});
        auto limits = goldsrc::StockRuntimeDecodeLimits{};
        limits.maximum_metadata_bytes = 1U;
        const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{limits}.decode(
            source.payload, source.boundary, registry());
        require_transactional_error(
            result,
            goldsrc::StockRuntimeDecodeErrorCode::metadata_limit_exceeded);
    }

    SECTION("invalid zero bound")
    {
        auto source = input({std::byte{0x93U}});
        auto limits = goldsrc::StockRuntimeDecodeLimits{};
        limits.maximum_decode_steps = 0U;
        const goldsrc::StockRuntimeMessageCatalogDecoder decoder{limits};
        CHECK_FALSE(decoder.valid_configuration());
        const auto result = decoder.decode(
            source.payload, source.boundary, registry());
        require_transactional_error(
            result,
            goldsrc::StockRuntimeDecodeErrorCode::invalid_configuration);
    }
}

} // namespace

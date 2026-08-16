#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/delta_description.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::vector<std::vector<std::byte>> two_schemas()
{
    return {
        fixture::schema("alpha_t", fixture::kSchemaAlphaFields),
        fixture::schema("bravo_t", fixture::kSchemaBravoFields),
    };
}

TEST_CASE("Delta stream publishes multiple ordered schemas at exact post-delta boundary",
          "[goldsrc][delta][stream]")
{
    const auto schemas = two_schemas();
    auto input = fixture::decode_pre_resource(fixture::service_payload(schemas));
    const auto initial_offset = input.state.boundary().byte_offset();

    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        input.payload.bytes,
        input.state.boundary());
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.required_event_count == 4U);
    CHECK(decoded.state->delta_message_count == 2U);
    CHECK(decoded.state->registry.schema_count() == 2U);
    CHECK(decoded.state->registry.schemas()[0U].name() == "alpha_t");
    CHECK(decoded.state->registry.schemas()[1U].name() == "bravo_t");
    CHECK(decoded.state->registry.total_field_count() == 4U);
    CHECK(decoded.state->bytes_consumed ==
          schemas[0U].size() + schemas[1U].size());
    CHECK(decoded.state->bits_consumed == decoded.state->bytes_consumed * 8U);
    CHECK(decoded.state->boundary.byte_offset() ==
          initial_offset + decoded.state->bytes_consumed);
    CHECK(decoded.state->boundary.bit_offset() == 0U);
    CHECK(decoded.state->boundary.opcode() == goldsrc::kStockPostDeltaBoundaryOpcode);
    CHECK(decoded.state->boundary.category() ==
          goldsrc::PostDeltaBoundaryCategory::stock_observed_opcode_44);
    CHECK(decoded.state->boundary.remaining_byte_count() == 1U);
    CHECK(input.payload.bytes[decoded.state->boundary.byte_offset()] ==
          static_cast<std::byte>(goldsrc::kStockPostDeltaBoundaryOpcode));
    CHECK(input.payload.bytes[decoded.state->boundary.byte_offset() + 1U] ==
          std::byte{0xa5U});
}

TEST_CASE("Delta stream keeps a neutral unknown boundary body unconsumed",
          "[goldsrc][delta][stream][neutral]")
{
    const std::array body{std::byte{0x11U}, std::byte{0x22U}};
    const std::vector<std::vector<std::byte>> schemas{
        fixture::schema("alpha_t", fixture::kSchemaAlphaFields)};
    auto input = fixture::decode_pre_resource(
        fixture::service_payload(schemas, 99U, body));

    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        input.payload.bytes,
        input.state.boundary());
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.state->boundary.opcode() == 99U);
    CHECK(decoded.state->boundary.category() ==
          goldsrc::PostDeltaBoundaryCategory::neutral_message);
    CHECK(decoded.state->boundary.evidence_status() ==
          goldsrc::PostDeltaBoundaryEvidenceStatus::synthetic_neutral_boundary);
    CHECK(decoded.state->boundary.remaining_byte_count() == body.size());
    CHECK(input.payload.bytes[decoded.state->boundary.byte_offset() + 1U] == body[0U]);
}

TEST_CASE("Delta stream rejects a truncated later schema without registry publication",
          "[goldsrc][delta][stream][truncated][transaction]")
{
    auto schemas = two_schemas();
    REQUIRE(schemas[1U].size() > 2U);
    schemas[1U].resize(schemas[1U].size() - 2U);
    auto input = fixture::decode_pre_resource(fixture::service_payload(schemas));

    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        input.payload.bytes,
        input.state.boundary());
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code ==
          goldsrc::DeltaDescriptionStreamErrorCode::parser_failure);
    CHECK_FALSE(decoded.state);
    CHECK(decoded.required_event_count == 0U);
}

TEST_CASE("Delta stream rejects duplicate schemas transactionally",
          "[goldsrc][delta][stream][duplicate][transaction]")
{
    const auto duplicate = fixture::schema("alpha_t", fixture::kSchemaAlphaFields);
    const std::vector schemas{duplicate, duplicate};
    auto input = fixture::decode_pre_resource(fixture::service_payload(schemas));

    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        input.payload.bytes,
        input.state.boundary());
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code ==
          goldsrc::DeltaDescriptionStreamErrorCode::registry_failure);
    CHECK(decoded.error->registry_code ==
          goldsrc::DeltaRegistryErrorCode::duplicate_schema_name);
    CHECK_FALSE(decoded.state);
}

TEST_CASE("Delta stream never scans past an unknown opcode to a later schema",
          "[goldsrc][delta][stream][no-resync]")
{
    auto bytes = fixture::service_payload(
        std::vector<std::vector<std::byte>>{
            fixture::schema("alpha_t", fixture::kSchemaAlphaFields)},
        99U,
        std::array{std::byte{14U}, std::byte{0U}, std::byte{0U}});
    auto input = fixture::decode_pre_resource(std::move(bytes));
    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        input.payload.bytes,
        input.state.boundary());
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.state->registry.schema_count() == 1U);
    CHECK(decoded.state->boundary.opcode() == 99U);
    CHECK(decoded.state->boundary.remaining_byte_count() == 3U);
}

TEST_CASE("Delta stream requires an unconsumed post-boundary body byte",
          "[goldsrc][delta][stream][boundary][truncated]")
{
    const std::vector<std::vector<std::byte>> schemas{
        fixture::schema("alpha_t", fixture::kSchemaAlphaFields)};
    auto bytes = fixture::service_payload(schemas);
    bytes.pop_back();
    auto input = fixture::decode_pre_resource(std::move(bytes));

    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        input.payload.bytes,
        input.state.boundary());
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code ==
          goldsrc::DeltaDescriptionStreamErrorCode::malformed_post_delta_boundary);
    CHECK_FALSE(decoded.state);
}

TEST_CASE("Delta stream enforces schema message count limit",
          "[goldsrc][delta][stream][limit]")
{
    auto limits = goldsrc::DeltaDescriptionLimits{};
    limits.maximum_schema_count = 1U;
    const auto schemas = two_schemas();
    auto input = fixture::decode_pre_resource(fixture::service_payload(schemas));

    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{limits}.decode(
        input.payload.bytes,
        input.state.boundary());
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code ==
          goldsrc::DeltaDescriptionStreamErrorCode::message_count_limit_exceeded);
    CHECK_FALSE(decoded.state);
}

} // namespace

#include <hlclient/goldsrc/resource_list.hpp>

#include "resource_list_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace fixture = resource_list_test_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::ResourceListState parse_exact_state(
    const std::span<const std::byte> payload,
    const std::size_t opcode_offset)
{
    auto parsed = goldsrc::ResourceListParser{}.parse(payload, opcode_offset);
    REQUIRE(parsed);
    REQUIRE(parsed.state);
    return std::move(*parsed.state);
}

TEST_CASE("GoldSrc post-resource-list stream publishes exact EOP and response metadata",
          "[goldsrc][resource-list][post-list]")
{
    const auto resource_list = parse_exact_state(
        fixture::kExactResourceListMessage,
        0U);
    const auto decoded = goldsrc::PostResourceListStreamDecoder{}.decode(
        fixture::kExactResourceListMessage,
        resource_list);

    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK_FALSE(decoded.error);
    CHECK(decoded.required_event_count == 2U);

    const auto& boundary = decoded.state->boundary();
    CHECK(boundary.kind() ==
          goldsrc::PostResourceListBoundaryKind::exact_end_of_payload);
    CHECK(boundary.byte_offset() == fixture::kExactResourceListMessage.size());
    CHECK(boundary.bit_offset() == 0U);
    CHECK(boundary.remaining_byte_count() == 0U);
    CHECK(boundary.source_payload_bit_length() == fixture::kExactMessageBits);
    CHECK(boundary.source_opcode_byte_offset() == 0U);
    CHECK(boundary.evidence_status() ==
          goldsrc::PostResourceListEvidenceStatus::
              repeated_stock_exact_end_of_payload);

    const auto& response = decoded.state->client_response();
    CHECK(response.action_kind() ==
          goldsrc::ResourceClientResponseActionKind::
              stock_response_required_semantics_pending);
    CHECK(response.opcode_candidate() ==
          goldsrc::kResourceClientResponseOpcodeCandidate);
    CHECK(response.opcode_candidate() == 5U);
    CHECK(response.trigger_byte_offset() == boundary.byte_offset());
    CHECK(response.trigger_bit_offset() == boundary.bit_offset());
    CHECK(response.evidence_status() ==
          goldsrc::ResourceClientResponseEvidenceStatus::
              stock_fragmented_reliable_opcode_five_semantics_pending);
    CHECK_FALSE(response.response_builder_available());
    CHECK_FALSE(response.response_queued());
}

TEST_CASE("GoldSrc post-resource-list cursor remains absolute at offset nine",
          "[goldsrc][resource-list][post-list][cursor]")
{
    constexpr std::array prefix{
        std::byte{45U}, std::byte{0x10U}, std::byte{0x20U},
        std::byte{0x30U}, std::byte{0x40U}, std::byte{0x50U},
        std::byte{0x60U}, std::byte{0x70U}, std::byte{0x80U},
    };
    const auto payload = fixture::with_prefix(
        fixture::kExactResourceListMessage,
        prefix);
    const auto resource_list = parse_exact_state(payload, prefix.size());
    const auto decoded = goldsrc::PostResourceListStreamDecoder{}.decode(
        payload,
        resource_list);

    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.state->boundary().byte_offset() == payload.size());
    CHECK(decoded.state->boundary().bit_offset() == 0U);
    CHECK(decoded.state->boundary().source_payload_bit_length() ==
          payload.size() * 8U);
    CHECK(decoded.state->boundary().source_opcode_byte_offset() ==
          prefix.size());
    CHECK(decoded.state->client_response().trigger_byte_offset() ==
          payload.size());
}

TEST_CASE("GoldSrc resource list ends exactly and never scans trailing controls",
          "[goldsrc][resource-list][post-list][strict]")
{
    auto with_trailing_opcode = std::vector<std::byte>{
        fixture::kExactResourceListMessage.begin(),
        fixture::kExactResourceListMessage.end(),
    };
    with_trailing_opcode.push_back(std::byte{5U});

    const auto parsed = goldsrc::ResourceListParser{}.parse(
        with_trailing_opcode,
        0U);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error);
    CHECK(parsed.error->code ==
          goldsrc::ResourceListErrorCode::unexpected_trailing_data);
    CHECK_FALSE(parsed.state);
    CHECK(parsed.bits_consumed == 0U);
    CHECK(parsed.bytes_consumed == 0U);

    const auto original_state = parse_exact_state(
        fixture::kExactResourceListMessage,
        0U);
    const auto mismatched = goldsrc::PostResourceListStreamDecoder{}.decode(
        with_trailing_opcode,
        original_state);
    REQUIRE_FALSE(mismatched);
    REQUIRE(mismatched.error);
    CHECK(mismatched.error->code ==
          goldsrc::PostResourceListStreamErrorCode::
              incompatible_resource_list_state);
    CHECK_FALSE(mismatched.state);
    CHECK(mismatched.required_event_count == 0U);
}

TEST_CASE("GoldSrc post-resource-list decoder validates exact bit geometry",
          "[goldsrc][resource-list][post-list][geometry]")
{
    const auto resource_list = parse_exact_state(
        fixture::kExactResourceListMessage,
        0U);
    const goldsrc::PostResourceListStreamDecoder decoder;

    const auto truncated_geometry = decoder.decode(
        fixture::kExactResourceListMessage,
        resource_list,
        fixture::kExactMessageBits - 1U);
    REQUIRE_FALSE(truncated_geometry);
    REQUIRE(truncated_geometry.error);
    CHECK(truncated_geometry.error->code ==
          goldsrc::PostResourceListStreamErrorCode::
              incompatible_resource_list_state);
    CHECK_FALSE(truncated_geometry.state);

    const auto outside_geometry = decoder.decode(
        fixture::kExactResourceListMessage,
        resource_list,
        fixture::kExactMessageBits + 1U);
    REQUIRE_FALSE(outside_geometry);
    REQUIRE(outside_geometry.error);
    CHECK(outside_geometry.error->code ==
          goldsrc::PostResourceListStreamErrorCode::invalid_input_geometry);
    CHECK_FALSE(outside_geometry.state);
}

TEST_CASE("GoldSrc post-resource-list metadata performs no response action",
          "[goldsrc][resource-list][post-list][no-tx]")
{
    static_assert(
        goldsrc::kResourceClientResponseOpcodeCandidate == 5U);

    const auto resource_list = parse_exact_state(
        fixture::kExactResourceListMessage,
        0U);
    const auto decoded = goldsrc::PostResourceListStreamDecoder{}.decode(
        fixture::kExactResourceListMessage,
        resource_list);
    REQUIRE(decoded);
    REQUIRE(decoded.state);

    const auto& response = decoded.state->client_response();
    CHECK_FALSE(response.response_builder_available());
    CHECK_FALSE(response.response_queued());
    CHECK(decoded.state->boundary().remaining_byte_count() == 0U);
}

} // namespace

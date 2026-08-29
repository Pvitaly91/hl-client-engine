#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
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

TEST_CASE("Stock runtime profiles retain reserved values but do not imply evidence",
          "[goldsrc][stock-runtime][profile]")
{
    using Compatibility = goldsrc::StockRuntimeCompatibilityProfile;
    using Delta = goldsrc::StockRuntimeDeltaCompatibilityProfile;
    using Evidence = goldsrc::StockRuntimeEvidenceProfile;

    CHECK(goldsrc::valid_stock_runtime_compatibility_profile(
        Compatibility::stock_protocol_48_build_10210_evidence_pending));
    CHECK(goldsrc::valid_stock_runtime_compatibility_profile(
        Compatibility::stock_protocol_48_build_10210_runtime_v1));
    CHECK_FALSE(goldsrc::valid_stock_runtime_compatibility_profile(
        static_cast<Compatibility>(0xffU)));

    CHECK(goldsrc::valid_stock_runtime_evidence_profile(
        Evidence::controlled_signed_stock_transcript_pending));
    CHECK(goldsrc::valid_stock_runtime_evidence_profile(
        Evidence::controlled_signed_stock_transcript_v1));
    CHECK_FALSE(goldsrc::valid_stock_runtime_evidence_profile(
        static_cast<Evidence>(0xffU)));

    CHECK(goldsrc::valid_stock_runtime_delta_compatibility_profile(
        Delta::stock_protocol_48_build_10210_delta_evidence_pending));
    CHECK(goldsrc::valid_stock_runtime_delta_compatibility_profile(
        Delta::stock_protocol_48_build_10210_delta_v1));
    CHECK_FALSE(goldsrc::valid_stock_runtime_delta_compatibility_profile(
        static_cast<Delta>(0xffU)));

    CHECK(goldsrc::stock_runtime_evidence_profile_for(
              Compatibility::stock_protocol_48_build_10210_evidence_pending) ==
          Evidence::controlled_signed_stock_transcript_pending);
    CHECK(goldsrc::stock_runtime_delta_profile_for(
              Compatibility::stock_protocol_48_build_10210_evidence_pending) ==
          Delta::stock_protocol_48_build_10210_delta_evidence_pending);
    CHECK(goldsrc::stock_runtime_evidence_profile_for(
              Compatibility::stock_protocol_48_build_10210_runtime_v1) ==
          Evidence::controlled_signed_stock_transcript_v1);
    CHECK(goldsrc::stock_runtime_delta_profile_for(
              Compatibility::stock_protocol_48_build_10210_runtime_v1) ==
          Delta::stock_protocol_48_build_10210_delta_v1);
}

TEST_CASE("Stock runtime limits have nonzero defaults and independently enforced hard caps",
          "[goldsrc][stock-runtime][limits]")
{
    using Limits = goldsrc::StockRuntimeDecodeLimits;
    using Member = std::size_t Limits::*;
    constexpr std::array<Member, 12U> members{
        &Limits::maximum_payload_bytes,
        &Limits::maximum_messages_per_payload,
        &Limits::maximum_runtime_frames,
        &Limits::maximum_baselines,
        &Limits::maximum_entities,
        &Limits::maximum_clientdata_fields,
        &Limits::maximum_weapon_entries,
        &Limits::maximum_history_frames,
        &Limits::maximum_value_bytes,
        &Limits::maximum_metadata_bytes,
        &Limits::maximum_decode_steps,
        &Limits::maximum_pending_delta_bases,
    };

    const auto defaults = Limits{};
    const auto hard = goldsrc::hard_stock_runtime_decode_limits();
    REQUIRE(goldsrc::valid_stock_runtime_decode_limits(defaults));
    REQUIRE(goldsrc::valid_stock_runtime_decode_limits(hard));

    for (const auto member : members) {
        CAPTURE(hard.*member);
        CHECK(defaults.*member > 0U);
        CHECK(defaults.*member <= hard.*member);

        auto zero = defaults;
        zero.*member = 0U;
        CHECK_FALSE(goldsrc::valid_stock_runtime_decode_limits(zero));

        auto above_hard = defaults;
        above_hard.*member = hard.*member + 1U;
        CHECK_FALSE(goldsrc::valid_stock_runtime_decode_limits(above_hard));
    }
}

TEST_CASE("Stock runtime source cursor has canonical byte and bit geometry",
          "[goldsrc][stock-runtime][cursor]")
{
    const auto origin = goldsrc::StockRuntimeSourceCursor::create(0U, 0U, 3U);
    REQUIRE(origin);
    CHECK(origin->absolute_bit_offset() == 0U);
    CHECK(origin->byte_offset() == 0U);
    CHECK(origin->bit_offset() == 0U);
    CHECK(origin->byte_aligned());
    CHECK(goldsrc::valid_stock_runtime_source_cursor(*origin));
    CHECK(goldsrc::valid_stock_runtime_source_cursor(*origin, 3U));

    const auto mixed = goldsrc::StockRuntimeSourceCursor::create(1U, 5U, 3U);
    REQUIRE(mixed);
    CHECK(mixed->absolute_bit_offset() == 13U);
    CHECK(mixed->byte_offset() == 1U);
    CHECK(mixed->bit_offset() == 5U);
    CHECK_FALSE(mixed->byte_aligned());
    CHECK(goldsrc::valid_stock_runtime_source_cursor(*mixed, 3U));
    CHECK_FALSE(goldsrc::valid_stock_runtime_source_cursor(*mixed, 1U));

    const auto exact_end = goldsrc::StockRuntimeSourceCursor::create(3U, 0U, 3U);
    REQUIRE(exact_end);
    CHECK(goldsrc::valid_stock_runtime_source_cursor(*exact_end, 3U));
    CHECK_FALSE(goldsrc::valid_stock_runtime_source_cursor(*exact_end, 2U));
    CHECK_FALSE(goldsrc::StockRuntimeSourceCursor::create(3U, 1U, 3U));
    CHECK_FALSE(goldsrc::StockRuntimeSourceCursor::create(0U, 8U, 3U));
    CHECK_FALSE(goldsrc::StockRuntimeSourceCursor::create(4U, 0U, 3U));
    CHECK_FALSE(goldsrc::StockRuntimeSourceCursor::create(
        std::numeric_limits<std::size_t>::max(),
        0U,
        std::numeric_limits<std::size_t>::max()));
}

TEST_CASE("Pending stock runtime catalog publishes one immutable exact boundary entry",
          "[goldsrc][stock-runtime][catalog][evidence-pending]")
{
    STATIC_REQUIRE_FALSE(
        std::is_copy_assignable_v<goldsrc::StockRuntimeMessageCatalogEntry>);
    STATIC_REQUIRE_FALSE(
        std::is_copy_assignable_v<goldsrc::StockRuntimeMessageCatalogState>);
    STATIC_REQUIRE_FALSE(
        std::is_copy_assignable_v<goldsrc::StockRuntimeEvidenceBoundaryState>);

    auto source = input(
        {std::byte{0x9aU}, std::byte{0x11U}, std::byte{0x22U}});
    const auto result = goldsrc::StockRuntimeMessageCatalogDecoder{}.decode(
        source.payload, source.boundary, registry());

    REQUIRE(result);
    REQUIRE(result.state);
    CHECK_FALSE(result.error);
    const auto& state = *result.state;
    REQUIRE(state.catalog().message_count() == 1U);
    REQUIRE(state.catalog().entries().size() == 1U);
    CHECK(state.catalog().accounted_metadata_bytes() > 0U);
    CHECK(state.catalog().accounted_metadata_bytes() <=
          goldsrc::StockRuntimeDecodeLimits{}.maximum_metadata_bytes);

    const auto& entry = state.catalog().entries().front();
    CHECK(entry.opcode() == 0x9aU);
    CHECK(entry.category() ==
          goldsrc::StockRuntimeMessageCategory::unsupported_runtime_message);
    CHECK(entry.start_cursor().absolute_bit_offset() == 0U);
    CHECK(entry.end_cursor() == entry.start_cursor());
    CHECK(entry.alignment() ==
          goldsrc::StockRuntimeMessageAlignment::evidence_pending);
    CHECK_FALSE(entry.body_bit_length());
    CHECK(entry.message_ordinal() == 0U);
    CHECK(entry.recurrence_count() == 1U);
    CHECK(entry.scenario_correlation() ==
          goldsrc::StockRuntimeScenarioCorrelationStatus::not_observed);
    CHECK(entry.compatibility_profile() ==
          goldsrc::StockRuntimeCompatibilityProfile::
              stock_protocol_48_build_10210_evidence_pending);
    CHECK(entry.evidence_profile() ==
          goldsrc::StockRuntimeEvidenceProfile::
              controlled_signed_stock_transcript_pending);
    CHECK(entry.delta_profile() ==
          goldsrc::StockRuntimeDeltaCompatibilityProfile::
              stock_protocol_48_build_10210_delta_evidence_pending);

    const auto& metadata = entry.source();
    CHECK(metadata.payload_ordinal == 0U);
    CHECK(metadata.direction == goldsrc::NetchanDirection::server_to_client);
    CHECK(metadata.source_sequence == source.payload.source_sequence);
    CHECK(metadata.source_acknowledgement ==
          source.payload.source_acknowledgement);
    CHECK(metadata.source_reliable == source.payload.source_reliable);
    CHECK(metadata.acknowledgement_reliable ==
          source.payload.acknowledgement_reliable);
    CHECK(metadata.reassembled == source.payload.reassembled);
    CHECK(metadata.decompressed == source.payload.decompressed);
    CHECK(metadata.decoded_payload_byte_count == source.payload.bytes.size());

    CHECK(state.ready_state().status() ==
          goldsrc::StockRuntimeReadyStatus::evidence_pending);
    CHECK_FALSE(state.ready_state().ready());
    CHECK_FALSE(state.ready_state().runtime_generation());

    const auto& unsupported = state.unsupported_boundary();
    CHECK(unsupported.cursor() == entry.start_cursor());
    CHECK(unsupported.remaining_payload_byte_count() == 3U);
    CHECK(unsupported.remaining_payload_bit_count() == 24U);
    CHECK(unsupported.unconfirmed_body_byte_count() == 2U);
    CHECK(unsupported.message_ordinal() == 0U);
    CHECK(unsupported.opcode() == 0x9aU);
    CHECK(unsupported.reason() ==
          goldsrc::StockRuntimeUnsupportedBoundaryReason::
              runtime_message_grammar_evidence_pending);
}

} // namespace

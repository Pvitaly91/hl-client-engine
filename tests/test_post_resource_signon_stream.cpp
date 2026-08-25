#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/post_resource_signon.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::DeltaSchemaRegistryState registry()
{
    const auto bytes = fixture::schema("entity_state_t", fixture::kSchemaAlphaFields);
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::move(builder).publish();
}

struct BoundaryInput {
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
        payload.bytes.size()};
    const auto parsed = goldsrc::PostResourceResponseBoundaryParser{}.parse(
        payload.bytes, source);
    REQUIRE(parsed);
    REQUIRE(parsed.boundary);
    return BoundaryInput{std::move(payload), std::move(*parsed.boundary)};
}

TEST_CASE("Stock post-resource mode preserves the exact unsupported cursor",
          "[goldsrc][post-resource][evidence-pending]")
{
    auto source = input(
        {std::byte{0x9aU}, std::byte{0x11U}, std::byte{0x22U}});
    const auto decoded = goldsrc::PostResourceSignonStreamDecoder{}.decode(
        source.payload, source.boundary, registry());

    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK(decoded.state->unsupported_boundary());
    CHECK(decoded.state->next_byte_offset() == 0U);
    CHECK(decoded.state->next_bit_offset() == 0U);
    CHECK(decoded.state->progress().progress() ==
          goldsrc::PostResourceSignonProgress::unsupported_message);
    CHECK_FALSE(decoded.state->progress().completion().completed());
    REQUIRE(decoded.state->transcript().server_messages().size() == 1U);
    const auto& message = decoded.state->transcript().server_messages().front();
    CHECK(message.opcode == 0x9aU);
    CHECK(message.byte_start == 0U);
    CHECK(message.byte_end == 0U);
    CHECK_FALSE(message.body_byte_count);
    CHECK(message.remaining_payload_byte_count == 2U);
    CHECK(message.evidence_status ==
          goldsrc::PostResourceSignonEvidenceStatus::stock_capture_required);
    CHECK(message.direction == goldsrc::NetchanDirection::server_to_client);
    CHECK(message.source_sequence == 31U);
    CHECK(message.decompressed_payload_ordinal == 0U);
    CHECK(message.decompressed);
    CHECK(message.cursor_byte_aligned);
    CHECK(decoded.state->transcript().client_requests().empty());
}

TEST_CASE("Stock continuation builder fails closed without accepted evidence",
          "[goldsrc][post-resource][request]")
{
    const auto built = goldsrc::PostResourceClientRequestBuilder{}.build_first();
    CHECK_FALSE(built);
    CHECK_FALSE(built.encoding);
    REQUIRE(built.error);
    CHECK(built.error->code ==
          goldsrc::PostResourceClientRequestErrorCode::
              stock_request_layout_evidence_pending);
}

TEST_CASE("Synthetic neutral request uses one fixed binary fixture",
          "[goldsrc][post-resource][synthetic]")
{
    constexpr std::array expected_request{
        std::byte{0xfdU}, std::byte{0x45U}, std::byte{0x01U}};
    auto source = input(
        {std::byte{0xfeU}, std::byte{0x45U}, std::byte{0x01U}});
    const auto profile =
        goldsrc::PostResourceSignonCompatibilityProfile::synthetic_neutral_v1;
    const auto decoded = goldsrc::PostResourceSignonStreamDecoder{
        {}, profile}.decode(source.payload, source.boundary, registry());

    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK_FALSE(decoded.state->unsupported_boundary());
    CHECK(decoded.state->next_byte_offset() == 3U);
    CHECK(decoded.state->next_bit_offset() == 0U);
    CHECK(decoded.state->progress().progress() ==
          goldsrc::PostResourceSignonProgress::client_request_ready);
    REQUIRE(decoded.state->transcript().client_requests().size() == 1U);
    const auto built = goldsrc::PostResourceClientRequestBuilder{
        {}, profile}.build_first();
    REQUIRE(built);
    REQUIRE(built.encoding);
    CHECK(std::ranges::equal(
        built.encoding->semantic_bytes(), expected_request));
    CHECK(built.encoding->metadata().semantic_byte_count == 3U);
    CHECK(built.encoding->metadata().reliable_required);
}

TEST_CASE("Synthetic neutral post-resource sequence has four sealed messages",
          "[goldsrc][post-resource][synthetic][sequence]")
{
    const auto profile =
        goldsrc::PostResourceSignonCompatibilityProfile::synthetic_neutral_v1;
    struct ExpectedMessage {
        std::span<const std::byte> bytes;
        goldsrc::PostResourceServerMessageCategory category;
        goldsrc::PostResourceSignonProgress progress;
        bool completed;
        std::size_t request_count;
    };
    const std::array expected{
        ExpectedMessage{
            goldsrc::kSyntheticPostResourceRequestTrigger,
            goldsrc::PostResourceServerMessageCategory::
                synthetic_request_trigger,
            goldsrc::PostResourceSignonProgress::client_request_ready,
            false,
            1U},
        ExpectedMessage{
            goldsrc::kSyntheticPostResourceBaselinePublication,
            goldsrc::PostResourceServerMessageCategory::
                synthetic_baseline_publication,
            goldsrc::PostResourceSignonProgress::
                synthetic_baseline_publication_observed,
            false,
            0U},
        ExpectedMessage{
            goldsrc::kSyntheticPostResourceFullSnapshotPublication,
            goldsrc::PostResourceServerMessageCategory::
                synthetic_full_snapshot_publication,
            goldsrc::PostResourceSignonProgress::
                synthetic_full_snapshot_publication_observed,
            false,
            0U},
        ExpectedMessage{
            goldsrc::kSyntheticPostResourceDeltaSnapshotPublication,
            goldsrc::PostResourceServerMessageCategory::
                synthetic_delta_snapshot_publication,
            goldsrc::PostResourceSignonProgress::
                synthetic_delta_snapshot_publication_observed,
            false,
            0U},
    };
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        auto source = input(std::vector<std::byte>(
            expected[index].bytes.begin(), expected[index].bytes.end()));
        const auto decoded = goldsrc::PostResourceSignonStreamDecoder{
            {}, profile}.decode(source.payload, source.boundary, registry());
        CAPTURE(index);
        REQUIRE(decoded);
        REQUIRE(decoded.state);
        REQUIRE(decoded.state->transcript().server_messages().size() == 1U);
        const auto& message =
            decoded.state->transcript().server_messages().front();
        CHECK(message.category == expected[index].category);
        REQUIRE(message.body_byte_count);
        CHECK(*message.body_byte_count == expected[index].bytes.size() - 1U);
        CHECK(message.remaining_payload_byte_count ==
              expected[index].bytes.size() - 1U);
        CHECK(message.source_sequence == 31U);
        CHECK(decoded.state->progress().progress() == expected[index].progress);
        CHECK(decoded.state->progress().signon_generation() == 1U);
        CHECK(decoded.state->progress().completion().completed() ==
              expected[index].completed);
        CHECK(decoded.state->progress().completion().condition() ==
              goldsrc::ServerSignonCompletionCondition::
                  synthetic_sequence_in_progress);
        CHECK(decoded.state->progress().completion().evidence_status() ==
              goldsrc::PostResourceSignonEvidenceStatus::
                  independently_authored_synthetic_fixture);
        CHECK(decoded.state->transcript().client_requests().size() ==
              expected[index].request_count);
        CHECK(decoded.state->next_byte_offset() == expected[index].bytes.size());
        CHECK(decoded.state->next_bit_offset() == 0U);
    }
}

TEST_CASE("Synthetic post-resource trigger rejects every truncation and suffix",
          "[goldsrc][post-resource][synthetic]")
{
    const auto profile =
        goldsrc::PostResourceSignonCompatibilityProfile::synthetic_neutral_v1;
    constexpr std::array fixture_bytes{
        std::byte{0xfeU}, std::byte{0x45U}, std::byte{0x01U}};
    for (std::size_t size = 1U; size < fixture_bytes.size(); ++size) {
        auto source = input(std::vector<std::byte>(
            fixture_bytes.begin(), fixture_bytes.begin() + size));
        const auto decoded = goldsrc::PostResourceSignonStreamDecoder{
            {}, profile}.decode(source.payload, source.boundary, registry());
        CAPTURE(size);
        CHECK_FALSE(decoded);
        REQUIRE(decoded.error);
        CHECK(decoded.error->code ==
              goldsrc::PostResourceSignonStreamErrorCode::
                  synthetic_fixture_mismatch);
        CHECK_FALSE(decoded.error->context.empty());
    }
    auto source = input(
        {std::byte{0xfeU}, std::byte{0x45U}, std::byte{0x01U}, std::byte{0U}});
    const auto decoded = goldsrc::PostResourceSignonStreamDecoder{
        {}, profile}.decode(source.payload, source.boundary, registry());
    CHECK_FALSE(decoded);
}

TEST_CASE("Post-resource decoder validates limits registry and owning geometry",
          "[goldsrc][post-resource][limits]")
{
    goldsrc::PostResourceSignonLimits limits;
    limits.maximum_post_resource_payload_bytes = 3U;
    auto source = input(
        {std::byte{0xfeU}, std::byte{0x45U}, std::byte{0x01U}});
    const auto profile =
        goldsrc::PostResourceSignonCompatibilityProfile::synthetic_neutral_v1;
    CHECK(goldsrc::PostResourceSignonStreamDecoder{limits, profile}.decode(
        source.payload, source.boundary, registry()));

    source.payload.source_sequence += 1U;
    const auto mismatched =
        goldsrc::PostResourceSignonStreamDecoder{limits, profile}.decode(
            source.payload, source.boundary, registry());
    CHECK_FALSE(mismatched);
    REQUIRE(mismatched.error);
    CHECK(mismatched.error->code ==
          goldsrc::PostResourceSignonStreamErrorCode::invalid_boundary_geometry);

    auto empty_registry = goldsrc::DeltaSchemaRegistryBuilder{}.publish();
    source.payload.source_sequence -= 1U;
    const auto missing =
        goldsrc::PostResourceSignonStreamDecoder{limits, profile}.decode(
            source.payload, source.boundary, empty_registry);
    CHECK_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code ==
          goldsrc::PostResourceSignonStreamErrorCode::missing_delta_registry);

    limits.maximum_post_resource_payload_bytes =
        goldsrc::kMaximumPostResourcePayloadBytes + 1U;
    CHECK_FALSE(goldsrc::PostResourceSignonStreamDecoder{limits, profile}
                    .valid_configuration());
}

TEST_CASE("Post-resource APIs reject out-of-domain compatibility profiles",
          "[goldsrc][post-resource][profile][invalid]")
{
    const auto invalid_profile =
        static_cast<goldsrc::PostResourceSignonCompatibilityProfile>(0xffU);
    CHECK_FALSE(goldsrc::valid_post_resource_signon_profile(invalid_profile));
    CHECK_FALSE(goldsrc::PostResourceSignonStreamDecoder{
                    {}, invalid_profile}.valid_configuration());
    const auto request = goldsrc::PostResourceClientRequestBuilder{
        {}, invalid_profile}.build_first();
    CHECK_FALSE(request);
    REQUIRE(request.error);
    CHECK(request.error->code ==
          goldsrc::PostResourceClientRequestErrorCode::invalid_configuration);
}

TEST_CASE("Post-resource message and request limits honor exact hard caps",
          "[goldsrc][post-resource][limits][hard-cap]")
{
    auto limits = goldsrc::PostResourceSignonLimits{};
    limits.maximum_post_resource_payload_bytes =
        goldsrc::kMaximumPostResourcePayloadBytes;
    limits.maximum_post_resource_messages =
        goldsrc::kMaximumPostResourceMessages;
    limits.maximum_client_signon_requests =
        goldsrc::kMaximumClientSignonRequests;
    CHECK(goldsrc::valid_post_resource_signon_limits(limits));

    limits.maximum_post_resource_messages =
        goldsrc::kMaximumPostResourceMessages + 1U;
    CHECK_FALSE(goldsrc::valid_post_resource_signon_limits(limits));
    limits.maximum_post_resource_messages =
        goldsrc::kMaximumPostResourceMessages;
    limits.maximum_client_signon_requests =
        goldsrc::kMaximumClientSignonRequests + 1U;
    CHECK_FALSE(goldsrc::valid_post_resource_signon_limits(limits));
}

} // namespace

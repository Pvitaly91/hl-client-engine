#include "resource_client_response_test_fixture.hpp"

#include <hlclient/goldsrc/resource_client_response.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::resource_client_response_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] goldsrc::NetchanHeader carrier_header()
{
    return {
        goldsrc::NetchanSequenceWord{
            sequence(80U), goldsrc::NetchanSequenceFlags{true, true}},
        goldsrc::NetchanAcknowledgementWord{sequence(18U), true},
    };
}

[[nodiscard]] std::vector<std::byte> carrier(const std::size_t tail_size)
{
    std::vector<std::byte> body{
        std::byte{0x01U},
        std::byte{0x01U}, std::byte{0x00U},
        std::byte{0x01U}, std::byte{0x00U},
        std::byte{0x00U}, std::byte{0x00U},
        std::byte{0x29U}, std::byte{0x00U},
        std::byte{0x00U},
    };
    body.insert(
        body.end(),
        fixture::kExactSyntheticResponse.begin(),
        fixture::kExactSyntheticResponse.end());
    for (std::size_t index = 0U; index < tail_size; ++index) {
        body.push_back(static_cast<std::byte>((index * 17U + 2U) & 0xffU));
    }
    return body;
}

TEST_CASE("Resource response carrier derives semantic and tail ranges",
          "[goldsrc][resource-response][carrier]")
{
    constexpr std::array<std::byte, 32U> expected_tail_11_sha256{
        std::byte{0x32U}, std::byte{0x16U}, std::byte{0xbbU}, std::byte{0x4dU},
        std::byte{0x48U}, std::byte{0x62U}, std::byte{0x87U}, std::byte{0x26U},
        std::byte{0x9dU}, std::byte{0xa7U}, std::byte{0x1bU}, std::byte{0x63U},
        std::byte{0x19U}, std::byte{0x58U}, std::byte{0x66U}, std::byte{0x8eU},
        std::byte{0x5aU}, std::byte{0x7dU}, std::byte{0x46U}, std::byte{0xedU},
        std::byte{0xeeU}, std::byte{0x04U}, std::byte{0x7fU}, std::byte{0x71U},
        std::byte{0xcaU}, std::byte{0x49U}, std::byte{0x79U}, std::byte{0x27U},
        std::byte{0x75U}, std::byte{0x59U}, std::byte{0xbbU}, std::byte{0xc0U},
    };
    constexpr std::array<std::size_t, 4U> tails{11U, 13U, 15U, 17U};
    constexpr std::array<goldsrc::ResourceResponseConcurrentTailProfile, 4U>
        profiles{
            goldsrc::ResourceResponseConcurrentTailProfile::
                stock_opaque_length_11,
            goldsrc::ResourceResponseConcurrentTailProfile::
                stock_coalesced_opaque_length_13,
            goldsrc::ResourceResponseConcurrentTailProfile::
                stock_coalesced_opaque_length_15,
            goldsrc::ResourceResponseConcurrentTailProfile::
                stock_coalesced_opaque_length_17,
        };

    for (std::size_t variant = 0U; variant < tails.size(); ++variant) {
        CAPTURE(variant);
        const auto body = carrier(tails[variant]);
        const auto parsed = goldsrc::Opcode5ResourceResponseCarrierParser{}.parse(
            carrier_header(), body, 7U);
        REQUIRE(parsed);
        REQUIRE(parsed.state);
        CHECK_FALSE(parsed.error);
        const auto& geometry = parsed.state->geometry();
        CHECK(geometry.descriptor_range().byte_offset() == 0U);
        CHECK(geometry.descriptor_range().byte_count() == 10U);
        CHECK(geometry.descriptor_range().end_byte_offset() == 10U);
        CHECK(geometry.semantic_reliable_range().byte_offset() == 10U);
        CHECK(geometry.semantic_reliable_range().byte_count() == 41U);
        CHECK(geometry.semantic_reliable_range().end_byte_offset() == 51U);
        CHECK(geometry.tail_range().byte_offset() == 51U);
        CHECK(geometry.tail_range().byte_count() == tails[variant]);
        CHECK(geometry.full_decoded_body_size() == 51U + tails[variant]);
        CHECK(geometry.source_sequence() == 80U);
        CHECK(geometry.reliable_generation() == 7U);
        CHECK(parsed.state->response().wire_name() == "synthetic.wad");
        CHECK(parsed.state->response().bytes_consumed() == 41U);
        CHECK(parsed.state->concurrent_tail().byte_count() == tails[variant]);
        CHECK(parsed.state->concurrent_tail().profile() == profiles[variant]);
        if (tails[variant] == 11U) {
            CHECK(parsed.state->concurrent_tail().sha256() ==
                  expected_tail_11_sha256);
        }
    }
}

TEST_CASE("Resource response carrier rejects descriptor and semantic range mismatch",
          "[goldsrc][resource-response][carrier]")
{
    SECTION("zero is not a real transport reliable generation")
    {
        const auto parsed =
            goldsrc::Opcode5ResourceResponseCarrierParser{}.parse(
                carrier_header(), carrier(11U), 0U);
        CHECK_FALSE(parsed.state);
        REQUIRE(parsed.error);
        CHECK(parsed.error->code ==
              goldsrc::ResourceResponseCarrierErrorCode::
                  invalid_reliable_generation);
    }

    SECTION("reliable fragmented flags are required")
    {
        auto header = carrier_header();
        header.sequence.flags.fragmented = false;
        const auto parsed = goldsrc::Opcode5ResourceResponseCarrierParser{}.parse(
            header, carrier(11U), 1U);
        CHECK_FALSE(parsed.state);
        REQUIRE(parsed.error);
        CHECK(parsed.error->code ==
              goldsrc::ResourceResponseCarrierErrorCode::
                  reliable_fragment_required);
    }

    SECTION("selected fragment length must match the semantic profile")
    {
        auto body = carrier(11U);
        body[7U] = std::byte{0x28U};
        const auto parsed = goldsrc::Opcode5ResourceResponseCarrierParser{}.parse(
            carrier_header(), body, 1U);
        CHECK_FALSE(parsed.state);
        REQUIRE(parsed.error);
        CHECK(parsed.error->code ==
              goldsrc::ResourceResponseCarrierErrorCode::
                  semantic_range_mismatch);
    }

    SECTION("selected semantic range may not overrun the payload area")
    {
        auto body = carrier(11U);
        body[7U] = std::byte{0xffU};
        body[8U] = std::byte{0x7fU};
        const auto parsed = goldsrc::Opcode5ResourceResponseCarrierParser{}.parse(
            carrier_header(), body, 1U);
        CHECK_FALSE(parsed.state);
        REQUIRE(parsed.error);
        CHECK(parsed.error->code ==
              goldsrc::ResourceResponseCarrierErrorCode::transport_decode_failed);
    }

    SECTION("secondary stream is rejected")
    {
        auto body = carrier(11U);
        body[0U] = std::byte{0x00U};
        body[1U] = std::byte{0x01U};
        const auto parsed = goldsrc::Opcode5ResourceResponseCarrierParser{}.parse(
            carrier_header(), body, 1U);
        CHECK_FALSE(parsed.state);
        REQUIRE(parsed.error);
    }
}

TEST_CASE("Resource response tail is owning metadata and respects its exact bound",
          "[goldsrc][resource-response][carrier]")
{
    goldsrc::ResourceClientResponseLimits limits;
    limits.maximum_concurrent_tail_size = 17U;
    goldsrc::Opcode5ResourceResponseCarrierParser parser{limits};

    auto source = carrier(17U);
    auto parsed = parser.parse(carrier_header(), source, 9U);
    REQUIRE(parsed.state);
    const auto retained_hash = parsed.state->concurrent_tail().sha256();
    source.assign(source.size(), std::byte{0xffU});
    source.clear();
    CHECK(parsed.state->concurrent_tail().byte_count() == 17U);
    CHECK(parsed.state->concurrent_tail().sha256() == retained_hash);
    CHECK(parsed.state->geometry().semantic_reliable_range().byte_count() ==
          fixture::kExactSyntheticResponse.size());

    const auto too_large = parser.parse(carrier_header(), carrier(18U), 9U);
    CHECK_FALSE(too_large.state);
    REQUIRE(too_large.error);
    CHECK(too_large.error->code ==
          goldsrc::ResourceResponseCarrierErrorCode::concurrent_tail_too_large);
}

TEST_CASE("Post-resource boundary reads only byte zero of a complete payload",
          "[goldsrc][resource-response][boundary]")
{
    std::vector<std::byte> payload{
        std::byte{0x2aU}, std::byte{0xdeU}, std::byte{0xadU},
        std::byte{0xbeU}, std::byte{0xefU}};
    goldsrc::PostResourceResponseSourcePayloadMetadata metadata;
    metadata.direction = goldsrc::NetchanDirection::server_to_client;
    metadata.source_sequence = 91U;
    metadata.reliable = true;
    metadata.reassembled = true;
    metadata.decompressed = true;
    metadata.decoded_payload_byte_count = payload.size();

    const auto parsed = goldsrc::PostResourceResponseBoundaryParser{}.parse(
        payload, metadata);
    REQUIRE(parsed.boundary);
    CHECK(parsed.boundary->kind() ==
          goldsrc::PostResourceResponseBoundaryKind::opcode_at_payload_start);
    REQUIRE(parsed.boundary->opcode());
    CHECK(*parsed.boundary->opcode() == 0x2aU);
    CHECK(parsed.boundary->byte_offset() == 0U);
    CHECK(parsed.boundary->bit_offset() == 0U);
    CHECK(parsed.boundary->remaining_byte_count() == payload.size() - 1U);
    CHECK(parsed.boundary->source_payload().source_sequence == 91U);

    payload.assign(payload.size(), std::byte{0x00U});
    CHECK(*parsed.boundary->opcode() == 0x2aU);

    metadata.decoded_payload_byte_count = 0U;
    const auto end = goldsrc::PostResourceResponseBoundaryParser{}.parse(
        std::span<const std::byte>{}, metadata);
    REQUIRE(end.boundary);
    CHECK(end.boundary->kind() ==
          goldsrc::PostResourceResponseBoundaryKind::exact_end_of_payload);
    CHECK_FALSE(end.boundary->opcode());
    CHECK(end.boundary->remaining_byte_count() == 0U);
}

TEST_CASE("Post-resource boundary enforces configured payload size",
          "[goldsrc][resource-response][boundary]")
{
    goldsrc::ResourceClientResponseLimits limits;
    limits.maximum_post_response_payload_size = 5U;
    goldsrc::PostResourceResponseBoundaryParser parser{limits};
    goldsrc::PostResourceResponseSourcePayloadMetadata metadata;
    metadata.direction = goldsrc::NetchanDirection::server_to_client;
    metadata.source_sequence = 1U;
    metadata.reliable = true;
    metadata.reassembled = true;
    metadata.decompressed = true;

    std::array<std::byte, 5U> exact{};
    metadata.decoded_payload_byte_count = exact.size();
    CHECK(parser.parse(exact, metadata));
    std::array<std::byte, 6U> too_large{};
    metadata.decoded_payload_byte_count = too_large.size();
    const auto rejected = parser.parse(too_large, metadata);
    CHECK_FALSE(rejected.boundary);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::PostResourceResponseBoundaryErrorCode::payload_too_large);
}

} // namespace

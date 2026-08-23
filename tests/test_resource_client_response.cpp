#include "resource_client_response_test_fixture.hpp"

#include <hlclient/goldsrc/resource_client_response.hpp>
#include <hlclient/resource_consistency/provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::resource_client_response_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace consistency = hlclient::resource_consistency;

[[nodiscard]] goldsrc::Opcode5ResourceResponseSourceGeometry fixture_geometry()
{
    return {
        0U,
        fixture::kExactSyntheticResponse.size(),
        fixture::kExactSyntheticResponse.size(),
    };
}

[[nodiscard]] goldsrc::Opcode5ResourceResponseParseResult parse_fixture(
    const std::span<const std::byte> bytes)
{
    return goldsrc::Opcode5ResourceResponseParser{}.parse(
        bytes,
        goldsrc::Opcode5ResourceResponseSourceGeometry{
            0U, bytes.size(), bytes.size()},
        goldsrc::Opcode5ResourceResponseSourceProfile::
            independently_authored_synthetic_fixture);
}

[[nodiscard]] goldsrc::ResourceClientResponseInput synthetic_input()
{
    auto material = consistency::make_resource_consistency_material(
        0x01020304U,
        fixture::kSyntheticOpaqueMaterial);
    REQUIRE(material);
    return goldsrc::ResourceClientResponseInput{
        "synthetic.wad",
        goldsrc::kOpcode5ResourceResponseFieldType,
        0U,
        goldsrc::kOpcode5ResourceResponseFieldFlags,
        std::move(*material.material),
    };
}

TEST_CASE("Opcode-5 response parser accepts the exact independent fixture",
          "[goldsrc][resource-response][codec]")
{
    const auto parsed = parse_fixture(fixture::kExactSyntheticResponse);

    REQUIRE(parsed);
    REQUIRE(parsed.response);
    CHECK_FALSE(parsed.error);
    CHECK(parsed.bytes_consumed == fixture::kExactSyntheticResponse.size());
    CHECK(parsed.response->opcode() == 5U);
    CHECK(parsed.response->entry_count() == 1U);
    CHECK(parsed.response->wire_name() == "synthetic.wad");
    CHECK(parsed.response->field_type() == 3U);
    CHECK(parsed.response->field_index() == 0U);
    CHECK(parsed.response->byte_count() == 0x01020304U);
    CHECK(parsed.response->field_flags() == 4U);
    CHECK(parsed.response->opaque_byte_count() == 16U);
    CHECK(parsed.response->bytes_consumed() == 41U);
    CHECK(parsed.response->source_geometry().semantic_byte_offset == 0U);
    CHECK(parsed.response->source_geometry().semantic_byte_count == 41U);
    CHECK(parsed.response->source_geometry().source_body_byte_count == 41U);
    CHECK(parsed.response->source_profile() ==
          goldsrc::Opcode5ResourceResponseSourceProfile::
              independently_authored_synthetic_fixture);
}

TEST_CASE("Opcode-5 response parser rejects every truncation without a candidate",
          "[goldsrc][resource-response][codec]")
{
    for (std::size_t size = 0U;
         size < fixture::kExactSyntheticResponse.size();
         ++size) {
        CAPTURE(size);
        const auto parsed = parse_fixture(std::span{
            fixture::kExactSyntheticResponse.data(), size});
        CHECK_FALSE(parsed);
        CHECK_FALSE(parsed.response);
        REQUIRE(parsed.error);
        CHECK(parsed.bytes_consumed <= size);
    }
}

TEST_CASE("Opcode-5 response parser strictly rejects malformed exact-size fields",
          "[goldsrc][resource-response][codec]")
{
    const auto rejects = [](const std::size_t offset, const std::byte value) {
        auto bytes = fixture::kExactSyntheticResponse;
        bytes[offset] = value;
        const auto parsed = parse_fixture(bytes);
        CHECK_FALSE(parsed);
        CHECK_FALSE(parsed.response);
        REQUIRE(parsed.error);
        return parsed.error->code;
    };

    SECTION("wrong opcode")
    {
        CHECK(rejects(0U, std::byte{0x06U}) ==
              goldsrc::Opcode5ResourceResponseErrorCode::wrong_opcode);
    }
    SECTION("wrong endian count")
    {
        auto bytes = fixture::kExactSyntheticResponse;
        bytes[1U] = std::byte{0x00U};
        bytes[2U] = std::byte{0x01U};
        const auto parsed = parse_fixture(bytes);
        CHECK_FALSE(parsed.response);
        REQUIRE(parsed.error);
        CHECK(parsed.error->code ==
              goldsrc::Opcode5ResourceResponseErrorCode::
                  unsupported_entry_count);
    }
    SECTION("invalid count")
    {
        CHECK(rejects(1U, std::byte{0x02U}) ==
              goldsrc::Opcode5ResourceResponseErrorCode::
                  unsupported_entry_count);
    }
    SECTION("unterminated fixed identifier")
    {
        CHECK(rejects(16U, std::byte{'x'}) ==
              goldsrc::Opcode5ResourceResponseErrorCode::
                  unterminated_wire_name);
    }
    SECTION("unsupported structural type")
    {
        CHECK(rejects(17U, std::byte{0x02U}) ==
              goldsrc::Opcode5ResourceResponseErrorCode::
                  unsupported_field_type);
    }
    SECTION("unsupported reserved index")
    {
        CHECK(rejects(18U, std::byte{0x01U}) ==
              goldsrc::Opcode5ResourceResponseErrorCode::
                  unsupported_field_index);
    }
    SECTION("unsupported structural flags")
    {
        CHECK(rejects(24U, std::byte{0x00U}) ==
              goldsrc::Opcode5ResourceResponseErrorCode::
                  unsupported_field_flags);
    }
}

TEST_CASE("Opcode-5 response parser rejects trailing bytes and invalid geometry",
          "[goldsrc][resource-response][codec]")
{
    std::vector<std::byte> trailing{
        fixture::kExactSyntheticResponse.begin(),
        fixture::kExactSyntheticResponse.end()};
    trailing.push_back(std::byte{0x00U});
    auto parsed = parse_fixture(trailing);
    CHECK_FALSE(parsed.response);
    REQUIRE(parsed.error);
    CHECK(parsed.error->code ==
          goldsrc::Opcode5ResourceResponseErrorCode::
              unexpected_trailing_bytes);

    const auto invalid_geometry = goldsrc::Opcode5ResourceResponseParser{}.parse(
        fixture::kExactSyntheticResponse,
        goldsrc::Opcode5ResourceResponseSourceGeometry{10U, 41U, 40U},
        goldsrc::Opcode5ResourceResponseSourceProfile::
            captured_reliable_semantic_fragment);
    CHECK_FALSE(invalid_geometry.response);
    REQUIRE(invalid_geometry.error);
    CHECK(invalid_geometry.error->code ==
          goldsrc::Opcode5ResourceResponseErrorCode::invalid_source_geometry);
}

TEST_CASE("Opcode-5 response state owns its decoded fields",
          "[goldsrc][resource-response][codec]")
{
    std::vector<std::byte> temporary{
        fixture::kExactSyntheticResponse.begin(),
        fixture::kExactSyntheticResponse.end()};
    auto parsed = parse_fixture(temporary);
    REQUIRE(parsed.response);
    temporary.assign(temporary.size(), std::byte{0xffU});
    temporary.clear();
    CHECK(parsed.response->wire_name() == "synthetic.wad");
    CHECK(parsed.response->byte_count() == 0x01020304U);
    CHECK(parsed.response->opaque_byte_count() == 16U);
}

TEST_CASE("Opcode-5 response builder produces semantic bytes only",
          "[goldsrc][resource-response][codec]")
{
    const auto built =
        goldsrc::Opcode5ResourceResponseBuilder{}.build(synthetic_input());
    REQUIRE(built);
    REQUIRE(built.encoding);
    CHECK_FALSE(built.error);
    const auto semantic = built.encoding->semantic_bytes();
    CHECK(semantic.size() == goldsrc::kOpcode5ResourceResponseSemanticSize);
    CHECK(std::ranges::equal(semantic, fixture::kExactSyntheticResponse));
    CHECK(semantic.front() == std::byte{0x05U});
    CHECK(built.encoding->response().bytes_consumed() == 41U);
    CHECK(built.encoding->response().source_profile() ==
          goldsrc::Opcode5ResourceResponseSourceProfile::
              canonical_builder_output);

    const auto reparsed = goldsrc::Opcode5ResourceResponseParser{}.parse(
        semantic,
        fixture_geometry(),
        goldsrc::Opcode5ResourceResponseSourceProfile::
            canonical_builder_output);
    REQUIRE(reparsed.response);
    CHECK(reparsed.response->wire_name() ==
          built.encoding->response().wire_name());
    CHECK(reparsed.response->byte_count() ==
          built.encoding->response().byte_count());
}

TEST_CASE("Opcode-5 response builder rejects noncanonical typed inputs",
          "[goldsrc][resource-response][codec]")
{
    const auto build = [](
                           std::string name,
                           const std::uint8_t type,
                           const std::uint16_t index,
                           const std::uint8_t flags) {
        auto material = consistency::make_resource_consistency_material(
            4U, fixture::kSyntheticOpaqueMaterial);
        REQUIRE(material);
        return goldsrc::Opcode5ResourceResponseBuilder{}.build(
            goldsrc::ResourceClientResponseInput{
                std::move(name),
                type,
                index,
                flags,
                std::move(*material.material),
            });
    };

    CHECK_FALSE(build("short.wad", 3U, 0U, 4U));
    CHECK_FALSE(build("synthetic.wad", 2U, 0U, 4U));
    CHECK_FALSE(build("synthetic.wad", 3U, 1U, 4U));
    CHECK_FALSE(build("synthetic.wad", 3U, 0U, 0U));

    std::array<std::byte, 15U> short_material{};
    consistency::ResourceConsistencyLimits provider_limits;
    provider_limits.maximum_opaque_bytes_per_material = short_material.size();
    auto material = consistency::make_resource_consistency_material(
        4U, short_material, provider_limits);
    REQUIRE(material);
    const auto wrong_width = goldsrc::Opcode5ResourceResponseBuilder{}.build(
        goldsrc::ResourceClientResponseInput{
            "synthetic.wad", 3U, 0U, 4U, std::move(*material.material)});
    CHECK_FALSE(wrong_width);
    REQUIRE(wrong_width.error);
    CHECK(wrong_width.error->code ==
          goldsrc::Opcode5ResourceResponseErrorCode::opaque_size_mismatch);
}

TEST_CASE("Resource response safety limits validate exact caps and cap plus one",
          "[goldsrc][resource-response][limits]")
{
    goldsrc::ResourceClientResponseLimits limits;
    CHECK(goldsrc::valid_resource_client_response_limits(limits));

    limits.maximum_resource_response_size =
        goldsrc::kMaximumResourceResponseSize;
    limits.maximum_response_field_count =
        goldsrc::kMaximumResponseFieldCount;
    limits.maximum_response_opaque_bytes =
        goldsrc::kMaximumResponseOpaqueBytes;
    limits.maximum_concurrent_tail_size =
        goldsrc::kMaximumConcurrentTailSize;
    limits.maximum_pre_ack_server_payloads =
        goldsrc::kMaximumPreAckServerPayloads;
    limits.maximum_response_stage_events =
        goldsrc::kMaximumResponseStageEvents;
    limits.maximum_post_response_payload_size =
        goldsrc::kMaximumPostResponsePayloadSize;
    CHECK(goldsrc::valid_resource_client_response_limits(limits));

    const auto rejects = [](auto mutate) {
        goldsrc::ResourceClientResponseLimits candidate;
        mutate(candidate);
        CHECK_FALSE(goldsrc::valid_resource_client_response_limits(candidate));
    };
    rejects([](auto& value) {
        value.maximum_resource_response_size =
            goldsrc::kMaximumResourceResponseSize + 1U;
    });
    rejects([](auto& value) {
        value.maximum_response_field_count =
            goldsrc::kMaximumResponseFieldCount + 1U;
    });
    rejects([](auto& value) {
        value.maximum_response_opaque_bytes =
            goldsrc::kMaximumResponseOpaqueBytes + 1U;
    });
    rejects([](auto& value) {
        value.maximum_concurrent_tail_size =
            goldsrc::kMaximumConcurrentTailSize + 1U;
    });
    rejects([](auto& value) {
        value.maximum_pre_ack_server_payloads =
            goldsrc::kMaximumPreAckServerPayloads + 1U;
    });
    rejects([](auto& value) {
        value.maximum_response_stage_events =
            goldsrc::kMaximumResponseStageEvents + 1U;
    });
    rejects([](auto& value) {
        value.maximum_post_response_payload_size =
            goldsrc::kMaximumPostResponsePayloadSize + 1U;
    });
}

static_assert(std::is_copy_constructible_v<goldsrc::Opcode5ResourceResponse>);
static_assert(!std::is_copy_assignable_v<goldsrc::Opcode5ResourceResponse>);

} // namespace

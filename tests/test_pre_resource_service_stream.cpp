#include <hlclient/goldsrc/service_message_stream.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

void append_u32_le(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void append_string(std::vector<std::byte>& bytes, const std::string_view value)
{
    const auto source = std::as_bytes(std::span{value.data(), value.size()});
    bytes.insert(bytes.end(), source.begin(), source.end());
    bytes.push_back(std::byte{0U});
}

[[nodiscard]] std::vector<std::byte> synthetic_server_info_body(
    const std::uint8_t maximum_clients = 8U,
    const std::uint8_t opaque_slot_candidate = 0U,
    const std::string_view map_path = "maps/test_alpha.bsp")
{
    std::vector<std::byte> bytes;
    append_u32_le(bytes, 48U);
    append_u32_le(bytes, 0x1234'5678U);
    append_u32_le(bytes, 0xdead'beefU);
    for (std::uint8_t value = 0U; value < 16U; ++value) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    bytes.push_back(static_cast<std::byte>(maximum_clients));
    bytes.push_back(static_cast<std::byte>(opaque_slot_candidate));
    bytes.push_back(maximum_clients == 1U ? std::byte{0U} : std::byte{1U});
    append_string(bytes, "sample");
    append_string(bytes, "Local Test");
    append_string(bytes, map_path);
    append_string(bytes, "alpha beta");
    bytes.push_back(std::byte{0U});
    return bytes;
}

[[nodiscard]] std::vector<std::byte> pre_resource_payload(
    const bool include_leading_text = true,
    const std::uint8_t maximum_clients = 8U,
    const std::uint8_t opaque_slot_candidate = 0U,
    const std::string_view map_path = "maps/test_alpha.bsp")
{
    std::vector<std::byte> bytes;
    if (include_leading_text) {
        bytes.push_back(std::byte{8U});
        bytes.push_back(std::byte{'x'});
        bytes.push_back(std::byte{0U});
    }
    bytes.push_back(std::byte{11U});
    const auto body = synthetic_server_info_body(
        maximum_clients,
        opaque_slot_candidate,
        map_path);
    bytes.insert(bytes.end(), body.begin(), body.end());
    bytes.push_back(static_cast<std::byte>(goldsrc::kPreResourceSimpleControlOpcode));
    bytes.push_back(std::byte{0U});
    bytes.push_back(std::byte{0U});
    bytes.push_back(static_cast<std::byte>(goldsrc::kPreResourceComplexBoundaryOpcode));
    bytes.push_back(std::byte{0xa5U});
    bytes.push_back(std::byte{0x5aU});
    return bytes;
}

[[nodiscard]] goldsrc::OwnedServicePayload decompressed(
    std::vector<std::byte> bytes)
{
    goldsrc::OwnedServicePayload payload;
    payload.bytes = std::move(bytes);
    payload.source_sequence = 31U;
    payload.source_acknowledgement = 17U;
    payload.source_reliable = true;
    payload.reassembled = true;
    payload.decompressed = true;
    payload.acknowledgement_reliable = true;
    payload.direction = goldsrc::NetchanDirection::server_to_client;
    return payload;
}

struct InitialBoundaryFixture {
    goldsrc::OwnedServicePayload payload;
    goldsrc::ServiceMessageBoundary boundary;
};

[[nodiscard]] InitialBoundaryFixture decode_initial_boundary(
    const goldsrc::ServiceMessageStreamDecoder& decoder,
    std::vector<std::byte> bytes)
{
    auto initial = decoder.decode(decompressed(std::move(bytes)));
    REQUIRE(initial);
    REQUIRE(initial.stream.has_value());
    REQUIRE(initial.stream->boundary.has_value());
    return InitialBoundaryFixture{
        std::move(initial.stream->payload),
        *initial.stream->boundary,
    };
}

void check_error(
    const goldsrc::PreResourceServiceDecodeResult& result,
    const goldsrc::PreResourceServiceErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kServiceMessageDiagnosticTextLimit);
    CHECK_FALSE(result.state.has_value());
    CHECK(result.required_event_count == 0U);
}

TEST_CASE("Service continuation publishes owning server info, control, and neutral boundary",
          "[goldsrc][signon][pre-resource][service][fixture]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    auto initial = decode_initial_boundary(decoder, pre_resource_payload());
    const auto original = initial.payload.bytes;
    const auto result = decoder.continue_to_pre_resource(
        initial.payload,
        initial.boundary);

    REQUIRE(result);
    REQUIRE(result.state.has_value());
    CHECK_FALSE(result.error.has_value());
    CHECK(result.required_event_count == 3U);
    CHECK(initial.payload.bytes == original);

    const auto& state = *result.state;
    CHECK(
        state.server_info().protocol_version() ==
        goldsrc::ProtocolVersion::goldsrc_48);
    CHECK(state.server_info().maximum_clients().value() == 8U);
    CHECK(state.server_info().map_file_path() == "maps/test_alpha.bsp");
    REQUIRE(state.controls().size() == 1U);
    CHECK(state.controls()[0U].opcode() == goldsrc::kPreResourceSimpleControlOpcode);
    CHECK(state.controls()[0U].byte_offset() == 85U);
    CHECK(state.controls()[0U].byte_count() == 3U);
    CHECK(state.controls()[0U].string_length() == 0U);
    CHECK(state.controls()[0U].control_value() == 0U);

    CHECK(state.boundary().opcode() == goldsrc::kPreResourceComplexBoundaryOpcode);
    CHECK(state.boundary().byte_offset() == 88U);
    CHECK(state.boundary().remaining_byte_count() == 2U);
    CHECK(
        state.boundary().direction() ==
        goldsrc::ResourcePhaseBoundaryDirection::server_message);
    CHECK(
        state.boundary().evidence_status() ==
        goldsrc::ResourcePhaseEvidenceStatus::
            confirmed_pre_resource_boundary_body_pending);

    const auto& source = state.source_payload();
    CHECK(source.payload_size() == 91U);
    CHECK(source.source_sequence() == 31U);
    CHECK(source.source_acknowledgement() == 17U);
    CHECK(source.source_reliable());
    CHECK(source.reassembled());
    CHECK(source.decompressed());
    CHECK(source.acknowledgement_reliable());
    CHECK(source.direction() == goldsrc::NetchanDirection::server_to_client);
    CHECK(source.received_at() == goldsrc::NetchanDriverTimePoint{});
    CHECK(source.initial_boundary_offset() == 3U);
    CHECK(source.server_info_body_offset() == 4U);
    CHECK(source.server_info_body_size() == 81U);

    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<goldsrc::PreResourceSignonState>);
    STATIC_REQUIRE_FALSE(std::is_aggregate_v<goldsrc::PreResourceSignonState>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<goldsrc::PreResourceSignonState>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<goldsrc::PreResourceSignonState>);
    STATIC_REQUIRE(std::is_same_v<
                   decltype(std::declval<const goldsrc::PreResourceSignonState&>().controls()),
                   const std::vector<goldsrc::PreResourceControl>&>);
}

TEST_CASE("Service continuation supports the stock shape with opcode 11 at offset zero",
          "[goldsrc][signon][pre-resource][service][offset-zero]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    auto initial = decode_initial_boundary(
        decoder,
        pre_resource_payload(false, 1U));
    REQUIRE(initial.boundary.byte_offset == 0U);
    const auto result = decoder.continue_to_pre_resource(
        initial.payload,
        initial.boundary);
    REQUIRE(result);
    CHECK(result.state->server_info().maximum_clients().value() == 1U);
    CHECK(result.state->source_payload().server_info_body_offset() == 1U);
    CHECK(result.state->boundary().byte_offset() == 85U);
}

TEST_CASE("Service continuation validates exact initial boundary geometry",
          "[goldsrc][signon][pre-resource][service][boundary][security]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    auto initial = decode_initial_boundary(decoder, pre_resource_payload());

    auto wrong_opcode = initial.boundary;
    wrong_opcode.opcode = goldsrc::ServiceMessageOpcode::text_control;
    check_error(
        decoder.continue_to_pre_resource(initial.payload, wrong_opcode),
        goldsrc::PreResourceServiceErrorCode::wrong_initial_boundary_opcode,
        initial.payload.bytes.size());

    auto wrong_offset = initial.boundary;
    wrong_offset.byte_offset = 0U;
    check_error(
        decoder.continue_to_pre_resource(initial.payload, wrong_offset),
        goldsrc::PreResourceServiceErrorCode::invalid_initial_boundary_geometry,
        initial.payload.bytes.size());

    auto wrong_remaining = initial.boundary;
    ++wrong_remaining.remaining_byte_count;
    check_error(
        decoder.continue_to_pre_resource(initial.payload, wrong_remaining),
        goldsrc::PreResourceServiceErrorCode::invalid_initial_boundary_geometry,
        initial.payload.bytes.size());

    auto wrong_direction = initial.payload;
    wrong_direction.direction = goldsrc::NetchanDirection::client_to_server;
    check_error(
        decoder.continue_to_pre_resource(wrong_direction, initial.boundary),
        goldsrc::PreResourceServiceErrorCode::invalid_initial_boundary_geometry,
        wrong_direction.bytes.size());
}

TEST_CASE("Service continuation maps server-info failures without partial state",
          "[goldsrc][signon][pre-resource][service][server-info][transaction]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    auto bytes = pre_resource_payload();
    REQUIRE(bytes[3U] == std::byte{11U});
    bytes[4U] = std::byte{47U};
    auto initial = decode_initial_boundary(decoder, std::move(bytes));
    const auto result = decoder.continue_to_pre_resource(
        initial.payload,
        initial.boundary);

    check_error(
        result,
        goldsrc::PreResourceServiceErrorCode::server_info_decode_failed,
        initial.payload.bytes.size());
    REQUIRE(result.error->server_info_code.has_value());
    CHECK(
        *result.error->server_info_code ==
        goldsrc::ServerInfoErrorCode::unsupported_protocol);
}

TEST_CASE("Server-info at payload end does not invent a continuation",
          "[goldsrc][signon][pre-resource][service][missing-control]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    auto bytes = pre_resource_payload();
    bytes.resize(4U + synthetic_server_info_body().size());
    auto initial = decode_initial_boundary(decoder, std::move(bytes));
    check_error(
        decoder.continue_to_pre_resource(initial.payload, initial.boundary),
        goldsrc::PreResourceServiceErrorCode::missing_post_server_info_control,
        initial.payload.bytes.size());
}

TEST_CASE("Post-serverinfo control layout is exact and never resynchronized",
          "[goldsrc][signon][pre-resource][service][control][strict]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    constexpr std::size_t control_offset = 85U;

    SECTION("unknown opcode before a later valid sequence")
    {
        auto bytes = pre_resource_payload();
        bytes[control_offset] = std::byte{0xfeU};
        bytes.insert(
            bytes.begin() + static_cast<std::ptrdiff_t>(control_offset + 1U),
            static_cast<std::byte>(goldsrc::kPreResourceSimpleControlOpcode));
        auto initial = decode_initial_boundary(decoder, std::move(bytes));
        const auto result = decoder.continue_to_pre_resource(
            initial.payload,
            initial.boundary);
        check_error(
            result,
            goldsrc::PreResourceServiceErrorCode::unsupported_post_server_info_opcode,
            initial.payload.bytes.size());
        REQUIRE(result.error->wire_opcode.has_value());
        CHECK(*result.error->wire_opcode == 0xfeU);
    }

    SECTION("duplicate server info")
    {
        auto bytes = pre_resource_payload();
        bytes[control_offset] = std::byte{11U};
        auto initial = decode_initial_boundary(decoder, std::move(bytes));
        check_error(
            decoder.continue_to_pre_resource(initial.payload, initial.boundary),
            goldsrc::PreResourceServiceErrorCode::duplicate_server_info,
            initial.payload.bytes.size());
    }

    SECTION("truncated control")
    {
        auto bytes = pre_resource_payload();
        bytes.resize(control_offset + 2U);
        auto initial = decode_initial_boundary(decoder, std::move(bytes));
        check_error(
            decoder.continue_to_pre_resource(initial.payload, initial.boundary),
            goldsrc::PreResourceServiceErrorCode::truncated_post_server_info_control,
            initial.payload.bytes.size());
    }

    SECTION("nonempty string")
    {
        auto bytes = pre_resource_payload();
        bytes[control_offset + 1U] = std::byte{'x'};
        auto initial = decode_initial_boundary(decoder, std::move(bytes));
        check_error(
            decoder.continue_to_pre_resource(initial.payload, initial.boundary),
            goldsrc::PreResourceServiceErrorCode::invalid_post_server_info_control,
            initial.payload.bytes.size());
    }

    SECTION("nonzero fixed value")
    {
        auto bytes = pre_resource_payload();
        bytes[control_offset + 2U] = std::byte{1U};
        auto initial = decode_initial_boundary(decoder, std::move(bytes));
        check_error(
            decoder.continue_to_pre_resource(initial.payload, initial.boundary),
            goldsrc::PreResourceServiceErrorCode::invalid_post_server_info_control,
            initial.payload.bytes.size());
    }
}

TEST_CASE("Complex pre-resource boundary opcode is exact and its body stays unconsumed",
          "[goldsrc][signon][pre-resource][service][complex-boundary]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    constexpr std::size_t complex_offset = 88U;

    auto missing = pre_resource_payload();
    missing.resize(complex_offset);
    auto initial_missing = decode_initial_boundary(decoder, std::move(missing));
    check_error(
        decoder.continue_to_pre_resource(
            initial_missing.payload,
            initial_missing.boundary),
        goldsrc::PreResourceServiceErrorCode::missing_pre_resource_boundary,
        initial_missing.payload.bytes.size());

    auto wrong = pre_resource_payload();
    wrong[complex_offset] = std::byte{0xfeU};
    wrong.push_back(static_cast<std::byte>(goldsrc::kPreResourceComplexBoundaryOpcode));
    auto initial_wrong = decode_initial_boundary(decoder, std::move(wrong));
    check_error(
        decoder.continue_to_pre_resource(initial_wrong.payload, initial_wrong.boundary),
        goldsrc::PreResourceServiceErrorCode::unsupported_post_server_info_opcode,
        initial_wrong.payload.bytes.size());

    auto body_missing = pre_resource_payload();
    body_missing.resize(complex_offset + 1U);
    auto initial_body_missing = decode_initial_boundary(decoder, std::move(body_missing));
    check_error(
        decoder.continue_to_pre_resource(
            initial_body_missing.payload,
            initial_body_missing.boundary),
        goldsrc::PreResourceServiceErrorCode::boundary_body_missing,
        initial_body_missing.payload.bytes.size());
}

TEST_CASE("Pre-resource continuation obeys message, payload, and decode prerequisites",
          "[goldsrc][signon][pre-resource][service][limits]")
{
    auto message_limits = goldsrc::ServiceMessageLimits{};
    message_limits.maximum_messages_per_payload = 2U;
    const goldsrc::ServiceMessageStreamDecoder message_decoder{message_limits};
    auto message_initial = decode_initial_boundary(
        message_decoder,
        pre_resource_payload(false));
    check_error(
        message_decoder.continue_to_pre_resource(
            message_initial.payload,
            message_initial.boundary),
        goldsrc::PreResourceServiceErrorCode::message_limit_exceeded,
        message_initial.payload.bytes.size());

    const auto bytes = pre_resource_payload(false);
    auto payload_limits = goldsrc::ServiceMessageLimits{};
    payload_limits.maximum_payload_size = bytes.size() - 1U;
    const goldsrc::ServiceMessageStreamDecoder payload_decoder{payload_limits};
    // Build the exact M2.4.1 metadata independently because its decoder must
    // correctly reject this same oversized payload before producing a stream.
    auto payload = decompressed(bytes);
    const goldsrc::ServiceMessageBoundary boundary{
        goldsrc::ServiceMessageOpcode::complex_signon_boundary,
        0U,
        payload.bytes.size() - 1U,
    };
    check_error(
        payload_decoder.continue_to_pre_resource(payload, boundary),
        goldsrc::PreResourceServiceErrorCode::payload_too_large,
        payload.bytes.size());

    payload.decompressed = false;
    const goldsrc::ServiceMessageStreamDecoder decoder;
    check_error(
        decoder.continue_to_pre_resource(payload, boundary),
        goldsrc::PreResourceServiceErrorCode::payload_not_decompressed,
        payload.bytes.size());
}

TEST_CASE("Pre-resource state owns metadata after the source payload is destroyed",
          "[goldsrc][signon][pre-resource][service][ownership][security]")
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    std::optional<goldsrc::PreResourceServiceDecodeResult> result;
    {
        auto initial = decode_initial_boundary(
            decoder,
            pre_resource_payload(true, 8U, 0xffU, "maps/test_bravo.bsp"));
        result.emplace(decoder.continue_to_pre_resource(
            initial.payload,
            initial.boundary));
        std::ranges::fill(initial.payload.bytes, std::byte{0U});
    }

    REQUIRE(*result);
    REQUIRE(result->state.has_value());
    CHECK(result->state->server_info().map_file_path() == "maps/test_bravo.bsp");
    CHECK(result->state->server_info().maximum_clients().value() == 8U);
    CHECK(result->state->controls().size() == 1U);
    CHECK(result->state->boundary().remaining_byte_count() == 2U);
}

} // namespace

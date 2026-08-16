#include <hlclient/goldsrc/service_payload_envelope.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

template<std::size_t Size>
[[nodiscard]] std::vector<std::byte> bytes(
    const std::array<std::uint8_t, Size>& values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(
        values,
        std::back_inserter(output),
        [](const std::uint8_t value) { return std::byte{value}; });
    return output;
}

[[nodiscard]] std::vector<std::byte> expected_payload()
{
    constexpr std::string_view text{"SYNTHETIC_SERVICE_PAYLOAD"};
    const auto encoded = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{encoded.begin(), encoded.end()};
}

[[nodiscard]] std::vector<std::byte> independent_envelope_fixture()
{
    // Independently generated standard bzip2 level-9 stream for the literal
    // payload above, prefixed by the stock-captured BZ2-NUL envelope.
    return bytes(std::array<std::uint8_t, 63U>{
        0x42U, 0x5AU, 0x32U, 0x00U,
        0x42U, 0x5AU, 0x68U, 0x39U, 0x31U, 0x41U, 0x59U, 0x26U,
        0x53U, 0x59U, 0x50U, 0x7AU, 0x53U, 0x30U, 0x00U, 0x00U,
        0x08U, 0x86U, 0x00U, 0x2EU, 0x65U, 0xDDU, 0x20U, 0xA0U,
        0x00U, 0x31U, 0x4CU, 0x00U, 0x13U, 0x42U, 0x26U, 0x9EU,
        0xA6U, 0x8DU, 0x1BU, 0x42U, 0x3FU, 0x78U, 0x82U, 0x1FU,
        0x66U, 0x56U, 0x90U, 0xDCU, 0xACU, 0xD0U, 0xB2U, 0x44U,
        0x7CU, 0x5DU, 0xC9U, 0x14U, 0xE1U, 0x42U, 0x41U, 0x41U,
        0xE9U, 0x4CU, 0xC0U,
    });
}

[[nodiscard]] std::vector<std::byte> independent_service_stream_envelope_fixture()
{
    // BZ2-NUL plus an independently generated bzip2 stream containing the
    // sanitized stock-shape service fixture: opcode 8, forty text bytes, NUL,
    // opcode 11, and two opaque boundary-body bytes.
    return bytes(std::array<std::uint8_t, 87U>{
        0x42U, 0x5AU, 0x32U, 0x00U,
        0x42U, 0x5AU, 0x68U, 0x39U, 0x31U, 0x41U, 0x59U, 0x26U,
        0x53U, 0x59U, 0xAFU, 0x5DU, 0x04U, 0xC7U, 0x00U, 0x00U,
        0x00U, 0xCEU, 0x18U, 0x40U, 0x48U, 0x44U, 0x00U, 0x1AU,
        0x6DU, 0x9CU, 0x60U, 0x80U, 0x10U, 0x00U, 0x08U, 0x20U,
        0x00U, 0x23U, 0x1EU, 0x6AU, 0x6AU, 0x7AU, 0x04U, 0x68U,
        0xF3U, 0x52U, 0x14U, 0x68U, 0xC8U, 0x1AU, 0x34U, 0xC8U,
        0xD2U, 0x76U, 0xBDU, 0x0EU, 0x34U, 0x99U, 0x8EU, 0xD3U,
        0x9CU, 0xB1U, 0x41U, 0x14U, 0x57U, 0xC6U, 0x9CU, 0x40U,
        0x95U, 0x20U, 0x43U, 0x19U, 0xBEU, 0x0BU, 0x39U, 0xDFU,
        0xE2U, 0xEEU, 0x48U, 0xA7U, 0x0AU, 0x12U, 0x15U, 0xEBU,
        0xA0U, 0x98U, 0xE0U,
    });
}

[[nodiscard]] goldsrc::OwnedServicePayload service_payload(
    std::vector<std::byte> payload)
{
    goldsrc::OwnedServicePayload result;
    result.bytes = std::move(payload);
    result.source_sequence = 17U;
    result.source_acknowledgement = 9U;
    result.source_reliable = true;
    result.reassembled = true;
    result.acknowledgement_reliable = true;
    result.received_at = goldsrc::NetchanDriverTimePoint{
        std::chrono::milliseconds{123}};
    return result;
}

void check_error(
    const goldsrc::ServicePayloadEnvelopeDecodeResult& result,
    const goldsrc::ServicePayloadEnvelopeErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(
        result.error->context.size() <=
        goldsrc::kServicePayloadEnvelopeDiagnosticTextLimit);
    CHECK_FALSE(result.envelope.has_value());
}

TEST_CASE("Service payload envelope matches an independent compressed fixture",
          "[goldsrc][signon][envelope][capture]")
{
    const auto fixture = independent_envelope_fixture();
    const goldsrc::ServicePayloadEnvelopeDecoder decoder;
    const auto result = decoder.decode(service_payload(fixture));

    REQUIRE(result);
    REQUIRE(result.envelope.has_value());
    CHECK(result.envelope->payload.bytes == expected_payload());
    CHECK(result.envelope->compressed_byte_count == fixture.size() - 4U);
    CHECK(result.envelope->decompressed_byte_count == expected_payload().size());
    CHECK(result.envelope->payload.source_sequence == 17U);
    CHECK(result.envelope->payload.source_acknowledgement == 9U);
    CHECK(result.envelope->payload.source_reliable);
    CHECK(result.envelope->payload.reassembled);
    CHECK(result.envelope->payload.decompressed);
    CHECK(result.envelope->payload.acknowledgement_reliable);
    CHECK_FALSE(result.error.has_value());
}

TEST_CASE("Service envelope hands an owning decompressed fixture to the stream decoder",
          "[goldsrc][signon][envelope][service][integration]")
{
    const goldsrc::ServicePayloadEnvelopeDecoder envelope_decoder;
    auto envelope = envelope_decoder.decode(
        service_payload(independent_service_stream_envelope_fixture()));
    REQUIRE(envelope);
    REQUIRE(envelope.envelope.has_value());
    CHECK(envelope.envelope->decompressed_byte_count == 45U);

    const goldsrc::ServiceMessageStreamDecoder stream_decoder;
    const auto stream = stream_decoder.decode(
        std::move(envelope.envelope->payload));
    REQUIRE(stream);
    REQUIRE(stream.stream.has_value());
    REQUIRE(stream.stream->messages.size() == 1U);
    REQUIRE(stream.stream->boundary.has_value());
    CHECK(stream.stream->messages[0].byte_offset == 0U);
    CHECK(stream.stream->messages[0].byte_count == 42U);
    CHECK(
        std::get<goldsrc::ServiceTextControl>(stream.stream->messages[0].body)
            .text == "SYNTHETIC_STOCK_TEXT_CONTROL_40_BYTES___");
    CHECK(stream.stream->boundary->byte_offset == 42U);
    CHECK(stream.stream->boundary->remaining_byte_count == 2U);
    CHECK(stream.stream->bytes_consumed == 43U);
    CHECK(stream.stream->required_event_count == 2U);
    CHECK(stream.stream->payload.bytes[43U] == std::byte{0xaaU});
    CHECK(stream.stream->payload.bytes[44U] == std::byte{0xbbU});
}

TEST_CASE("Service payload envelope limits are positive and hard capped",
          "[goldsrc][signon][envelope][limits]")
{
    CHECK(goldsrc::valid_service_payload_envelope_limits({}));
    CHECK(goldsrc::valid_service_payload_envelope_limits(
        {goldsrc::kMaximumDecompressedServicePayloadSize}));
    CHECK_FALSE(goldsrc::valid_service_payload_envelope_limits({0U}));
    CHECK_FALSE(goldsrc::valid_service_payload_envelope_limits(
        {goldsrc::kMaximumDecompressedServicePayloadSize + 1U}));

    const goldsrc::ServicePayloadEnvelopeDecoder zero{{0U}};
    check_error(
        zero.decode(service_payload(independent_envelope_fixture())),
        goldsrc::ServicePayloadEnvelopeErrorCode::invalid_configuration,
        independent_envelope_fixture().size());
}

TEST_CASE("Service payload envelope magic is exact and bounded",
          "[goldsrc][signon][envelope][strict]")
{
    const goldsrc::ServicePayloadEnvelopeDecoder decoder;
    const auto fixture = independent_envelope_fixture();

    for (std::size_t size = 0U;
         size < goldsrc::kServicePayloadEnvelopeHeaderSize;
         ++size) {
        INFO("magic prefix size " << size);
        auto prefix = fixture;
        prefix.resize(size);
        check_error(
            decoder.decode(service_payload(std::move(prefix))),
            goldsrc::ServicePayloadEnvelopeErrorCode::payload_too_short,
            size);
    }

    for (std::size_t index = 0U;
         index < goldsrc::kServicePayloadEnvelopeHeaderSize;
         ++index) {
        auto wrong_magic = fixture;
        wrong_magic[index] ^= std::byte{0xffU};
        INFO("wrong magic byte " << index);
        check_error(
            decoder.decode(service_payload(wrong_magic)),
            goldsrc::ServicePayloadEnvelopeErrorCode::invalid_envelope_magic,
            wrong_magic.size());
    }

    std::vector<std::byte> magic_only{
        goldsrc::kBzip2ServicePayloadEnvelopeMagic.begin(),
        goldsrc::kBzip2ServicePayloadEnvelopeMagic.end()};
    check_error(
        decoder.decode(service_payload(magic_only)),
        goldsrc::ServicePayloadEnvelopeErrorCode::missing_compressed_stream,
        magic_only.size());
}

TEST_CASE("Service payload envelope rejects invalid and truncated bzip2 streams",
          "[goldsrc][signon][envelope][malformed]")
{
    const goldsrc::ServicePayloadEnvelopeDecoder decoder;
    const auto fixture = independent_envelope_fixture();

    for (std::size_t compressed_prefix = 1U; compressed_prefix < 4U;
         ++compressed_prefix) {
        auto truncated = fixture;
        truncated.resize(
            goldsrc::kServicePayloadEnvelopeHeaderSize + compressed_prefix);
        check_error(
            decoder.decode(service_payload(truncated)),
            goldsrc::ServicePayloadEnvelopeErrorCode::truncated_compressed_stream,
            truncated.size());
    }

    auto invalid_header = fixture;
    invalid_header[4] = std::byte{'X'};
    check_error(
        decoder.decode(service_payload(invalid_header)),
        goldsrc::ServicePayloadEnvelopeErrorCode::invalid_compressed_header,
        invalid_header.size());

    for (std::size_t size = 8U; size < fixture.size(); ++size) {
        INFO("envelope prefix size " << size);
        auto truncated = fixture;
        truncated.resize(size);
        const auto result = decoder.decode(service_payload(std::move(truncated)));
        REQUIRE_FALSE(result);
        REQUIRE(result.error.has_value());
        CHECK(
            result.error->code ==
            goldsrc::ServicePayloadEnvelopeErrorCode::truncated_compressed_stream);
        CHECK(result.error->byte_offset <= size);
    }

    auto corrupt = fixture;
    corrupt[31] ^= std::byte{0x80U};
    check_error(
        decoder.decode(service_payload(corrupt)),
        goldsrc::ServicePayloadEnvelopeErrorCode::corrupt_compressed_stream,
        corrupt.size());
}

TEST_CASE("Service payload envelope rejects bytes after the one compressed stream",
          "[goldsrc][signon][envelope][trailing]")
{
    const goldsrc::ServicePayloadEnvelopeDecoder decoder;
    auto trailing = independent_envelope_fixture();
    trailing.push_back(std::byte{0x00U});

    check_error(
        decoder.decode(service_payload(trailing)),
        goldsrc::ServicePayloadEnvelopeErrorCode::unexpected_trailing_data,
        trailing.size());
}

TEST_CASE("Service payload envelope distinguishes exact output limit from limit plus one",
          "[goldsrc][signon][envelope][limits]")
{
    const auto expected = expected_payload();
    const goldsrc::ServicePayloadEnvelopeDecoder exact{{expected.size()}};
    const auto accepted = exact.decode(service_payload(independent_envelope_fixture()));
    REQUIRE(accepted);
    CHECK(accepted.envelope->payload.bytes == expected);

    const goldsrc::ServicePayloadEnvelopeDecoder one_too_small{{expected.size() - 1U}};
    check_error(
        one_too_small.decode(service_payload(independent_envelope_fixture())),
        goldsrc::ServicePayloadEnvelopeErrorCode::decompressed_payload_too_large,
        independent_envelope_fixture().size());
}

TEST_CASE("Service payload envelope rejects compressed input above its hard cap",
          "[goldsrc][signon][envelope][limits]")
{
    std::vector<std::byte> oversized(
        goldsrc::kMaximumCompressedServiceEnvelopeSize + 1U,
        std::byte{0U});
    std::ranges::copy(
        goldsrc::kBzip2ServicePayloadEnvelopeMagic,
        oversized.begin());

    const goldsrc::ServicePayloadEnvelopeDecoder decoder;
    check_error(
        decoder.decode(service_payload(std::move(oversized))),
        goldsrc::ServicePayloadEnvelopeErrorCode::compressed_payload_too_large,
        goldsrc::kMaximumCompressedServiceEnvelopeSize + 1U);
}

} // namespace

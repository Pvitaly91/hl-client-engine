#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result.has_value());
    return *result;
}

[[nodiscard]] goldsrc::NetchanHeader header(
    const std::uint32_t sequence_value,
    const goldsrc::NetchanSequenceFlags flags,
    const std::uint32_t acknowledgement_value,
    const bool reliable_acknowledgement = false)
{
    return goldsrc::NetchanHeader{
        goldsrc::NetchanSequenceWord{sequence(sequence_value), flags},
        goldsrc::NetchanAcknowledgementWord{
            sequence(acknowledgement_value),
            reliable_acknowledgement,
        },
    };
}

template<std::size_t Size>
[[nodiscard]] std::vector<std::byte> bytes(const std::array<std::uint8_t, Size>& values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(values, std::back_inserter(output), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return output;
}

[[nodiscard]] bool bytes_equal(
    const std::span<const std::byte> left,
    const std::span<const std::byte> right)
{
    return std::ranges::equal(left, right);
}

template<typename Result>
void check_decode_error(
    const Result& result,
    const goldsrc::NetchanPacketErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.packet.has_value());
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kNetchanPacketDiagnosticTextLimit);
    CHECK(result.error->byte_offset <= input_size);
}

TEST_CASE("Netchan payload transform matches independent capture and synthetic goldens",
          "[goldsrc][netchan][packet][transform][capture]")
{
    SECTION("captured client-to-server sequence two padding")
    {
        const auto encoded_fixture = bytes(std::array<std::uint8_t, 8U>{
            0x59U, 0x19U, 0x01U, 0x03U, 0x19U, 0x01U, 0x11U, 0x43U});
        const auto plain_fixture = bytes(std::array<std::uint8_t, 8U>{
            0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U});

        auto decoded = encoded_fixture;
        goldsrc::decode_netchan_payload(decoded, sequence(2U));
        CHECK(bytes_equal(decoded, plain_fixture));

        goldsrc::encode_netchan_payload(decoded, sequence(2U));
        CHECK(bytes_equal(decoded, encoded_fixture));
    }

    SECTION("captured server-to-client sequence six padding")
    {
        const auto encoded_fixture = bytes(std::array<std::uint8_t, 8U>{
            0x5dU, 0x19U, 0x01U, 0x07U, 0x1dU, 0x01U, 0x11U, 0x47U});
        const auto plain_fixture = bytes(std::array<std::uint8_t, 8U>{
            0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U});

        auto decoded = encoded_fixture;
        goldsrc::decode_netchan_payload(decoded, sequence(6U));
        CHECK(bytes_equal(decoded, plain_fixture));
        goldsrc::encode_netchan_payload(decoded, sequence(6U));
        CHECK(bytes_equal(decoded, encoded_fixture));
    }

    SECTION("captured fragment descriptor prefix")
    {
        auto encoded = bytes(std::array<std::uint8_t, 8U>{
            0x5aU, 0x18U, 0x05U, 0x00U, 0x1bU, 0x00U, 0x10U, 0x41U});
        const auto expected = bytes(std::array<std::uint8_t, 8U>{
            0x01U, 0x05U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U});
        goldsrc::decode_netchan_payload(encoded, sequence(1U));
        CHECK(bytes_equal(encoded, expected));
    }

    SECTION("independent key-42 word and unchanged tails")
    {
        const auto complete_plain = bytes(std::array<std::uint8_t, 7U>{
            0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x45U, 0x46U});
        const auto complete_encoded = bytes(std::array<std::uint8_t, 7U>{
            0x43U, 0x3aU, 0x11U, 0x2aU, 0x44U, 0x45U, 0x46U});

        for (std::size_t tail_size = 0U; tail_size <= 3U; ++tail_size) {
            auto plain = complete_plain;
            plain.resize(4U + tail_size);
            auto expected = complete_encoded;
            expected.resize(4U + tail_size);

            INFO("tail bytes " << tail_size);
            goldsrc::encode_netchan_payload(plain, sequence(42U));
            CHECK(bytes_equal(plain, expected));
            goldsrc::decode_netchan_payload(plain, sequence(42U));

            auto original = complete_plain;
            original.resize(4U + tail_size);
            CHECK(bytes_equal(plain, original));
        }
    }
}

TEST_CASE("Netchan transform leaves sub-word payloads unchanged",
          "[goldsrc][netchan][packet][transform]")
{
    const auto complete = bytes(std::array<std::uint8_t, 3U>{0x11U, 0x22U, 0x33U});
    for (std::size_t size = 0U; size <= complete.size(); ++size) {
        auto payload = complete;
        payload.resize(size);
        const auto expected = payload;
        goldsrc::encode_netchan_payload(payload, sequence(0x123U));
        CHECK(bytes_equal(payload, expected));
        goldsrc::decode_netchan_payload(payload, sequence(0x123U));
        CHECK(bytes_equal(payload, expected));
    }
}

TEST_CASE("Netchan classifier separates connectionless and sequenced datagrams",
          "[goldsrc][netchan][packet][classifier]")
{
    for (std::size_t size = 0U; size < 4U; ++size) {
        const std::vector<std::byte> prefix(size, std::byte{0xff});
        CHECK(
            goldsrc::classify_netchan_datagram(prefix).classification ==
            goldsrc::NetchanDatagramClassification::malformed);
    }

    const auto connectionless = bytes(std::array<std::uint8_t, 8U>{
        0xffU, 0xffU, 0xffU, 0xffU, 0x42U, 0x00U, 0x00U, 0x00U});
    CHECK(
        goldsrc::classify_netchan_datagram(
            std::span<const std::byte>{connectionless}.first(4U))
                .classification ==
        goldsrc::NetchanDatagramClassification::connectionless);
    CHECK(
        goldsrc::classify_netchan_datagram(connectionless).classification ==
        goldsrc::NetchanDatagramClassification::connectionless);

    const auto split_marker = bytes(std::array<std::uint8_t, 8U>{
        0xfeU, 0xffU, 0xffU, 0xffU, 0x00U, 0x00U, 0x00U, 0x00U});
    CHECK(
        goldsrc::classify_netchan_datagram(split_marker).classification ==
        goldsrc::NetchanDatagramClassification::unsupported_special);

    const std::vector<std::byte> short_sequenced(7U, std::byte{0});
    CHECK(
        goldsrc::classify_netchan_datagram(short_sequenced).classification ==
        goldsrc::NetchanDatagramClassification::malformed);

    const std::vector<std::byte> minimum_sequenced(8U, std::byte{0});
    const auto classified = goldsrc::classify_netchan_datagram(minimum_sequenced);
    CHECK(classified.classification == goldsrc::NetchanDatagramClassification::sequenced);
    CHECK(
        classified.classification !=
        goldsrc::NetchanDatagramClassification::unsupported_special);
}

TEST_CASE("Direction-specific codecs match an independent transformed fixture",
          "[goldsrc][netchan][packet][capture]")
{
    const auto fixture = bytes(std::array<std::uint8_t, 16U>{
        0x02U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U,
        0x59U, 0x19U, 0x01U, 0x03U,
        0x19U, 0x01U, 0x11U, 0x43U,
    });
    const auto expected_payload = bytes(std::array<std::uint8_t, 8U>{
        0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U});

    const auto client = goldsrc::decode_client_to_server_netchan_packet(fixture);
    REQUIRE(client);
    CHECK(client.packet->header.sequence.sequence.value() == 2U);
    CHECK_FALSE(client.packet->header.sequence.flags.reliable);
    CHECK_FALSE(client.packet->header.sequence.flags.fragmented);
    CHECK(client.packet->header.acknowledgement.sequence.value() == 0U);
    CHECK_FALSE(client.packet->header.acknowledgement.reliable);
    CHECK(bytes_equal(client.packet->payload, expected_payload));

    const auto server = goldsrc::decode_server_to_client_netchan_packet(fixture);
    REQUIRE(server);
    CHECK(server.packet->header.sequence.sequence.value() == 2U);
    CHECK(bytes_equal(server.packet->payload, expected_payload));

    const goldsrc::ClientToServerNetchanPacket packet{
        header(2U, goldsrc::NetchanSequenceFlags{false, false}, 0U),
        {},
        expected_payload,
    };
    const auto encoded = goldsrc::encode_client_to_server_netchan_packet(packet);
    REQUIRE(encoded);
    CHECK(bytes_equal(*encoded.datagram, fixture));

    const goldsrc::ServerToClientNetchanPacket server_packet{
        packet.header,
        packet.fragments,
        packet.payload,
    };
    const auto server_encoded =
        goldsrc::encode_server_to_client_netchan_packet(server_packet);
    REQUIRE(server_encoded);
    CHECK(bytes_equal(*server_encoded.datagram, fixture));
}

TEST_CASE("Netchan header is little-endian and acknowledgement bit 30 is rejected",
          "[goldsrc][netchan][packet][header]")
{
    const auto valid = bytes(std::array<std::uint8_t, 8U>{
        0x01U, 0x00U, 0x00U, 0x80U,
        0x02U, 0x00U, 0x00U, 0x80U,
    });
    const auto decoded = goldsrc::decode_server_to_client_netchan_packet(valid);
    REQUIRE(decoded);
    CHECK(decoded.packet->header.sequence.sequence.value() == 1U);
    CHECK(decoded.packet->header.sequence.flags.reliable);
    CHECK_FALSE(decoded.packet->header.sequence.flags.fragmented);
    CHECK(decoded.packet->header.acknowledgement.sequence.value() == 2U);
    CHECK(decoded.packet->header.acknowledgement.reliable);
    CHECK(decoded.packet->payload.empty());

    const auto peeked = goldsrc::peek_netchan_header(valid);
    REQUIRE(peeked);
    REQUIRE(peeked.packet.has_value());
    CHECK(peeked.packet->header.sequence.sequence.value() == 1U);
    CHECK(peeked.packet->header.sequence.flags.reliable);
    CHECK(peeked.packet->header.acknowledgement.sequence.value() == 2U);
    CHECK(peeked.packet->header.acknowledgement.reliable);
    CHECK_FALSE(peeked.packet->reserved_acknowledgement_flag);

    auto reserved = valid;
    reserved[7] = std::byte{0x40};
    const auto reserved_peek = goldsrc::peek_netchan_header(reserved);
    REQUIRE(reserved_peek);
    REQUIRE(reserved_peek.packet.has_value());
    CHECK(reserved_peek.packet->reserved_acknowledgement_flag);
    check_decode_error(
        goldsrc::decode_server_to_client_netchan_packet(reserved),
        goldsrc::NetchanPacketErrorCode::reserved_acknowledgement_flag,
        reserved.size());
    check_decode_error(
        goldsrc::decode_client_to_server_netchan_packet(reserved),
        goldsrc::NetchanPacketErrorCode::reserved_acknowledgement_flag,
        reserved.size());
}

TEST_CASE("Direction-specific decoders reject every truncated header prefix",
          "[goldsrc][netchan][packet][truncation]")
{
    const auto fixture = bytes(std::array<std::uint8_t, 8U>{
        0x01U, 0x00U, 0x00U, 0x80U,
        0x00U, 0x00U, 0x00U, 0x00U,
    });
    for (std::size_t size = 0U; size < fixture.size(); ++size) {
        const auto prefix = std::span<const std::byte>{fixture}.first(size);
        INFO("prefix length " << size);
        check_decode_error(
            goldsrc::peek_netchan_header(prefix),
            goldsrc::NetchanPacketErrorCode::datagram_too_short,
            size);
        check_decode_error(
            goldsrc::decode_server_to_client_netchan_packet(prefix),
            goldsrc::NetchanPacketErrorCode::datagram_too_short,
            size);
        check_decode_error(
            goldsrc::decode_client_to_server_netchan_packet(prefix),
            goldsrc::NetchanPacketErrorCode::datagram_too_short,
            size);
    }
}

TEST_CASE("Netchan decoder refuses connectionless packets",
          "[goldsrc][netchan][packet][classifier]")
{
    const auto fixture = bytes(std::array<std::uint8_t, 8U>{
        0xffU, 0xffU, 0xffU, 0xffU,
        0x00U, 0x00U, 0x00U, 0x00U,
    });
    check_decode_error(
        goldsrc::decode_server_to_client_netchan_packet(fixture),
        goldsrc::NetchanPacketErrorCode::connectionless_packet,
        fixture.size());

    const auto split = bytes(std::array<std::uint8_t, 8U>{
        0xfeU, 0xffU, 0xffU, 0xffU,
        0x00U, 0x00U, 0x00U, 0x00U,
    });
    check_decode_error(
        goldsrc::decode_server_to_client_netchan_packet(split),
        goldsrc::NetchanPacketErrorCode::unsupported_special_packet,
        split.size());
}

TEST_CASE("Netchan packet size configuration is strictly bounded",
          "[goldsrc][netchan][packet][bounds]")
{
    const std::vector<std::byte> minimum(goldsrc::kNetchanHeaderSize, std::byte{0});
    check_decode_error(
        goldsrc::decode_server_to_client_netchan_packet(
            minimum,
            goldsrc::NetchanPacketLimits{goldsrc::kNetchanHeaderSize - 1U}),
        goldsrc::NetchanPacketErrorCode::invalid_configuration,
        minimum.size());
    check_decode_error(
        goldsrc::decode_server_to_client_netchan_packet(
            minimum,
            goldsrc::NetchanPacketLimits{goldsrc::kMaximumNetchanDatagramSize + 1U}),
        goldsrc::NetchanPacketErrorCode::invalid_configuration,
        minimum.size());

    std::vector<std::byte> at_default(goldsrc::kDefaultNetchanDatagramSize, std::byte{0});
    REQUIRE(goldsrc::decode_server_to_client_netchan_packet(at_default));
    at_default.push_back(std::byte{0});
    check_decode_error(
        goldsrc::decode_server_to_client_netchan_packet(at_default),
        goldsrc::NetchanPacketErrorCode::datagram_too_large,
        at_default.size());

    const std::vector<std::byte> at_hard_limit(
        goldsrc::kMaximumNetchanDatagramSize,
        std::byte{0});
    REQUIRE(goldsrc::decode_server_to_client_netchan_packet(
        at_hard_limit,
        goldsrc::NetchanPacketLimits{goldsrc::kMaximumNetchanDatagramSize}));
}

TEST_CASE("Fragment descriptors decode in ordered neutral slots",
          "[goldsrc][netchan][packet][fragments]")
{
    goldsrc::NetchanFragmentSlots fragments{};
    fragments[0] = goldsrc::NetchanFragmentDescriptor{0U, 0x0001'0005U, 0U, 2U, 0U};
    fragments[1] = goldsrc::NetchanFragmentDescriptor{1U, 0x1122'3344U, 7U, 3U, 7U};
    const auto payload = bytes(std::array<std::uint8_t, 10U>{
        0x10U, 0x11U, 0x90U, 0x91U, 0x92U,
        0x93U, 0x94U, 0x20U, 0x21U, 0x22U});
    const goldsrc::ServerToClientNetchanPacket packet{
        header(1U, goldsrc::NetchanSequenceFlags{true, true}, 2U, true),
        fragments,
        payload,
    };

    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    const auto decoded = goldsrc::decode_server_to_client_netchan_packet(*encoded.datagram);
    REQUIRE(decoded);
    REQUIRE(decoded.packet->fragments[0].has_value());
    REQUIRE(decoded.packet->fragments[1].has_value());
    CHECK(decoded.packet->fragments[0]->slot_index == 0U);
    CHECK(decoded.packet->fragments[0]->fragment_id == 0x0001'0005U);
    CHECK(decoded.packet->fragments[0]->offset == 0U);
    CHECK(decoded.packet->fragments[0]->length == 2U);
    CHECK(decoded.packet->fragments[0]->payload_offset == 0U);
    CHECK(decoded.packet->fragments[1]->slot_index == 1U);
    CHECK(decoded.packet->fragments[1]->fragment_id == 0x1122'3344U);
    CHECK(decoded.packet->fragments[1]->offset == 7U);
    CHECK(decoded.packet->fragments[1]->length == 3U);
    CHECK(decoded.packet->fragments[1]->payload_offset == 7U);
    CHECK(bytes_equal(decoded.packet->payload, payload));

    auto owned_payload = decoded.packet->payload;
    encoded.datagram->clear();
    CHECK(bytes_equal(owned_payload, payload));
}

TEST_CASE("Fragment decoder rejects malformed descriptor boundaries",
          "[goldsrc][netchan][packet][fragments][bounds]")
{
    auto fragmented_header = bytes(std::array<std::uint8_t, 8U>{
        0x00U, 0x00U, 0x00U, 0x40U,
        0x00U, 0x00U, 0x00U, 0x00U,
    });

    SECTION("presence is neither zero nor one")
    {
        auto fixture = fragmented_header;
        fixture.push_back(std::byte{2U});
        check_decode_error(
            goldsrc::decode_server_to_client_netchan_packet(fixture),
            goldsrc::NetchanPacketErrorCode::invalid_fragment_presence,
            fixture.size());
    }

    SECTION("present descriptor is truncated")
    {
        auto fixture = fragmented_header;
        fixture.push_back(std::byte{1U});
        check_decode_error(
            goldsrc::decode_server_to_client_netchan_packet(fixture),
            goldsrc::NetchanPacketErrorCode::fragment_descriptor_truncated,
            fixture.size());
    }

    SECTION("fragment flag has no present slots")
    {
        auto fixture = fragmented_header;
        fixture.push_back(std::byte{0U});
        fixture.push_back(std::byte{0U});
        check_decode_error(
            goldsrc::decode_server_to_client_netchan_packet(fixture),
            goldsrc::NetchanPacketErrorCode::fragment_flag_without_descriptor,
            fixture.size());
    }
}

TEST_CASE("Fragment encoder rejects inconsistent remote-controlled sizes",
          "[goldsrc][netchan][packet][fragments][bounds]")
{
    const auto payload = bytes(std::array<std::uint8_t, 2U>{0xaaU, 0xbbU});

    SECTION("fragment flag without descriptor")
    {
        const goldsrc::ServerToClientNetchanPacket packet{
            header(1U, goldsrc::NetchanSequenceFlags{false, true}, 0U),
            {},
            payload,
        };
        const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(
            encoded.error->code ==
            goldsrc::NetchanPacketErrorCode::fragment_flag_without_descriptor);
    }

    SECTION("descriptor without fragment flag")
    {
        goldsrc::NetchanFragmentSlots fragments{};
        fragments[0] = goldsrc::NetchanFragmentDescriptor{0U, 1U, 0U, 2U, 0U};
        const goldsrc::ServerToClientNetchanPacket packet{
            header(1U, goldsrc::NetchanSequenceFlags{false, false}, 0U),
            fragments,
            payload,
        };
        const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(
            encoded.error->code ==
            goldsrc::NetchanPacketErrorCode::descriptors_without_fragment_flag);
    }

    SECTION("zero length")
    {
        goldsrc::NetchanFragmentSlots fragments{};
        fragments[0] = goldsrc::NetchanFragmentDescriptor{0U, 1U, 0U, 0U, 0U};
        const goldsrc::ServerToClientNetchanPacket packet{
            header(1U, goldsrc::NetchanSequenceFlags{false, true}, 0U),
            fragments,
            {},
        };
        const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(encoded.error->code == goldsrc::NetchanPacketErrorCode::zero_fragment_length);
    }

    SECTION("descriptor length exceeds payload")
    {
        goldsrc::NetchanFragmentSlots fragments{};
        fragments[0] = goldsrc::NetchanFragmentDescriptor{0U, 1U, 0U, 3U, 0U};
        const goldsrc::ServerToClientNetchanPacket packet{
            header(1U, goldsrc::NetchanSequenceFlags{false, true}, 0U),
            fragments,
            payload,
        };
        const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(
            encoded.error->code ==
            goldsrc::NetchanPacketErrorCode::fragment_payload_out_of_bounds);
    }

    SECTION("descriptor ranges overlap")
    {
        goldsrc::NetchanFragmentSlots fragments{};
        fragments[0] = goldsrc::NetchanFragmentDescriptor{0U, 1U, 0U, 2U, 0U};
        fragments[1] = goldsrc::NetchanFragmentDescriptor{1U, 2U, 1U, 1U, 1U};
        const goldsrc::ServerToClientNetchanPacket packet{
            header(1U, goldsrc::NetchanSequenceFlags{false, true}, 0U),
            fragments,
            payload,
        };
        const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(encoded.error->code == goldsrc::NetchanPacketErrorCode::fragment_payload_overlap);
    }

    SECTION("maximum wire offset plus length is rejected before slicing")
    {
        goldsrc::NetchanFragmentSlots fragments{};
        fragments[0] = goldsrc::NetchanFragmentDescriptor{
            0U,
            1U,
            std::numeric_limits<std::uint16_t>::max(),
            1U,
            std::numeric_limits<std::uint16_t>::max(),
        };
        const goldsrc::ServerToClientNetchanPacket packet{
            header(1U, goldsrc::NetchanSequenceFlags{true, true}, 0U),
            fragments,
            payload,
        };
        const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(
            encoded.error->code ==
            goldsrc::NetchanPacketErrorCode::fragment_payload_out_of_bounds);
    }
}

TEST_CASE("Secondary stream observation is bounded by named datagram policy",
          "[goldsrc][netchan][packet][fragments][secondary][bounds]")
{
    const auto make_packet = [](const std::size_t payload_size) {
        goldsrc::NetchanFragmentSlots fragments{};
        fragments[1] = goldsrc::NetchanFragmentDescriptor{
            1U,
            0x0001'0001U,
            0U,
            static_cast<std::uint16_t>(payload_size),
            0U,
        };
        return goldsrc::ServerToClientNetchanPacket{
            header(1U, goldsrc::NetchanSequenceFlags{true, true}, 0U),
            fragments,
            std::vector<std::byte>(payload_size, std::byte{0x5a}),
        };
    };

    const auto at_default = make_packet(
        goldsrc::kDefaultNetchanSecondaryStreamObservationBytes);
    const auto encoded_default =
        goldsrc::encode_server_to_client_netchan_packet(at_default);
    REQUIRE(encoded_default);
    CHECK(encoded_default.datagram->size() == goldsrc::kDefaultNetchanDatagramSize);

    const auto above_default = make_packet(
        goldsrc::kDefaultNetchanSecondaryStreamObservationBytes + 1U);
    const auto rejected_default =
        goldsrc::encode_server_to_client_netchan_packet(above_default);
    REQUIRE_FALSE(rejected_default);
    REQUIRE(rejected_default.error);
    CHECK(rejected_default.error->code == goldsrc::NetchanPacketErrorCode::packet_too_large);

    const auto at_hard = make_packet(
        goldsrc::kMaximumNetchanSecondaryStreamObservationBytes);
    const auto encoded_hard = goldsrc::encode_server_to_client_netchan_packet(
        at_hard,
        goldsrc::NetchanPacketLimits{goldsrc::kMaximumNetchanDatagramSize});
    REQUIRE(encoded_hard);
    CHECK(encoded_hard.datagram->size() == goldsrc::kMaximumNetchanDatagramSize);

    const auto above_hard = make_packet(
        goldsrc::kMaximumNetchanSecondaryStreamObservationBytes + 1U);
    const auto rejected_hard = goldsrc::encode_server_to_client_netchan_packet(
        above_hard,
        goldsrc::NetchanPacketLimits{goldsrc::kMaximumNetchanDatagramSize});
    REQUIRE_FALSE(rejected_hard);
    REQUIRE(rejected_hard.error);
    CHECK(rejected_hard.error->code == goldsrc::NetchanPacketErrorCode::packet_too_large);
}

TEST_CASE("Every prefix of a valid fragmented fixture fails without partial output",
          "[goldsrc][netchan][packet][fragments][truncation]")
{
    goldsrc::NetchanFragmentSlots fragments{};
    fragments[0] = goldsrc::NetchanFragmentDescriptor{0U, 0x0001'0005U, 0U, 4U, 0U};
    const goldsrc::ServerToClientNetchanPacket packet{
        header(1U, goldsrc::NetchanSequenceFlags{true, true}, 0U),
        fragments,
        bytes(std::array<std::uint8_t, 4U>{0x10U, 0x20U, 0x30U, 0x40U}),
    };
    const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);

    for (std::size_t size = 0U; size < encoded.datagram->size(); ++size) {
        const auto prefix = std::span<const std::byte>{*encoded.datagram}.first(size);
        INFO("fragment fixture prefix length " << size);
        const auto decoded = goldsrc::decode_server_to_client_netchan_packet(prefix);
        REQUIRE_FALSE(decoded);
        REQUIRE(decoded.error);
        CHECK_FALSE(decoded.packet);
        CHECK(decoded.error->byte_offset <= size);
    }
}

TEST_CASE("Netchan encoder preserves its packet bound and reliable payload",
          "[goldsrc][netchan][packet][bounds]")
{
    const goldsrc::ClientToServerNetchanPacket at_limit{
        header(1U, goldsrc::NetchanSequenceFlags{true, false}, 0U),
        {},
        std::vector<std::byte>(
            goldsrc::kDefaultNetchanDatagramSize - goldsrc::kNetchanHeaderSize,
            std::byte{0x55}),
    };
    REQUIRE(goldsrc::encode_client_to_server_netchan_packet(at_limit));

    auto over_limit = at_limit;
    over_limit.payload.push_back(std::byte{0x66});
    const auto encoded = goldsrc::encode_client_to_server_netchan_packet(over_limit);
    REQUIRE_FALSE(encoded);
    REQUIRE(encoded.error);
    CHECK(encoded.error->code == goldsrc::NetchanPacketErrorCode::packet_too_large);
    CHECK(over_limit.payload.size() ==
          goldsrc::kDefaultNetchanDatagramSize - goldsrc::kNetchanHeaderSize + 1U);
}

TEST_CASE("Netchan encoder rejects sequence words that collide with packet markers",
          "[goldsrc][netchan][packet][classifier][bounds]")
{
    for (const auto numeric :
         {goldsrc::kNetchanSequenceMask - 1U, goldsrc::kNetchanSequenceMask}) {
        const goldsrc::ClientToServerNetchanPacket packet{
            header(numeric, goldsrc::NetchanSequenceFlags{true, true}, 0U),
            {},
            {},
        };
        const auto encoded = goldsrc::encode_client_to_server_netchan_packet(packet);
        REQUIRE_FALSE(encoded);
        REQUIRE(encoded.error);
        CHECK(encoded.error->code == goldsrc::NetchanPacketErrorCode::reserved_sequence_word);
    }
}

} // namespace

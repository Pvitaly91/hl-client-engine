#include <hlclient/goldsrc/connectionless_packet.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

TEST_CASE("Connectionless envelope encodes exact wire bytes", "[goldsrc][connectionless]")
{
    const std::array payload{
        std::byte{'p'},
        std::byte{'i'},
        std::byte{'n'},
        std::byte{'g'},
        std::byte{'\n'},
    };
    const std::array expected{
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{'p'},
        std::byte{'i'},
        std::byte{'n'},
        std::byte{'g'},
        std::byte{'\n'},
    };

    const auto result = goldsrc::encode_connectionless_packet(payload);

    REQUIRE(result);
    CHECK(std::ranges::equal(*result.datagram, expected));
}

TEST_CASE("Connectionless envelope parsing returns owned payload bytes", "[goldsrc][connectionless]")
{
    std::array datagram{
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0x10},
        std::byte{0x20},
    };

    auto result = goldsrc::parse_connectionless_packet(datagram);
    REQUIRE(result);
    datagram.fill(std::byte{0});

    const std::array expected{std::byte{0x10}, std::byte{0x20}};
    CHECK(std::ranges::equal(result.packet->payload, expected));
}

TEST_CASE("Connectionless envelope rejects a header-only packet", "[goldsrc][connectionless]")
{
    const std::array datagram{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto result = goldsrc::parse_connectionless_packet(datagram);

    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == goldsrc::ConnectionlessPacketErrorCode::empty_payload);

    const auto encoded = goldsrc::encode_connectionless_packet({});
    REQUIRE_FALSE(encoded);
    REQUIRE(encoded.error);
    CHECK(encoded.error->code == goldsrc::ConnectionlessPacketErrorCode::empty_payload);
}

TEST_CASE("Connectionless envelope rejects a truncated header", "[goldsrc][connectionless]")
{
    const std::array datagram{std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto result = goldsrc::parse_connectionless_packet(datagram);

    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == goldsrc::ConnectionlessPacketErrorCode::truncated_header);
    CHECK(result.error->byte_offset == datagram.size());
}

TEST_CASE("Connectionless envelope rejects each wrong header byte",
          "[goldsrc][connectionless]")
{
    const std::array valid{
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{'x'},
    };

    for (std::size_t index = 0; index < goldsrc::kConnectionlessPacketHeaderSize; ++index) {
        auto wrong = valid;
        wrong[index] = std::byte{0xfe};
        const auto result = goldsrc::parse_connectionless_packet(wrong);
        INFO("header byte " << index);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::ConnectionlessPacketErrorCode::invalid_header);
    }
}

TEST_CASE("Connectionless envelope rejects sequenced and split headers", "[goldsrc][connectionless]")
{
    SECTION("sequenced")
    {
        const std::array datagram{
            std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
        const auto result = goldsrc::parse_connectionless_packet(datagram);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::ConnectionlessPacketErrorCode::invalid_header);
    }

    SECTION("split packet marker")
    {
        const std::array datagram{
            std::byte{0xfe}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        const auto result = goldsrc::parse_connectionless_packet(datagram);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code == goldsrc::ConnectionlessPacketErrorCode::invalid_header);
    }
}

TEST_CASE("Connectionless envelope enforces IPv4 UDP bounds before copying", "[goldsrc][connectionless]")
{
    std::vector<std::byte> oversized_datagram(
        goldsrc::kMaximumConnectionlessChallengeDatagramSize + 1U, std::byte{0xff});
    const auto parsed = goldsrc::parse_connectionless_packet(oversized_datagram);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error);
    CHECK(parsed.error->code ==
          goldsrc::ConnectionlessPacketErrorCode::datagram_too_large);

    std::vector<std::byte> oversized_payload(
        goldsrc::kMaximumConnectionlessChallengePayloadSize + 1U, std::byte{0});
    const auto encoded = goldsrc::encode_connectionless_packet(oversized_payload);
    REQUIRE_FALSE(encoded);
    REQUIRE(encoded.error);
    CHECK(encoded.error->code ==
          goldsrc::ConnectionlessPacketErrorCode::payload_too_large);
}

TEST_CASE("Maximum connectionless payload round-trips exactly", "[goldsrc][connectionless]")
{
    std::vector<std::byte> payload(
        goldsrc::kMaximumConnectionlessChallengePayloadSize, std::byte{0x5a});

    const auto encoded = goldsrc::encode_connectionless_packet(payload);
    REQUIRE(encoded);
    CHECK(encoded.datagram->size() ==
          goldsrc::kMaximumConnectionlessChallengeDatagramSize);

    const auto parsed = goldsrc::parse_connectionless_packet(*encoded.datagram);
    REQUIRE(parsed);
    CHECK(parsed.packet->payload == payload);
}

} // namespace

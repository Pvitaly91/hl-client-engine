#include <hlclient/goldsrc/netchan_packet.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

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

[[nodiscard]] std::vector<std::byte> bytes(
    const std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> output;
    output.reserve(values.size());
    std::ranges::transform(values, std::back_inserter(output), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return output;
}

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto result = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(result);
    return *result;
}

[[nodiscard]] goldsrc::NetchanHeader fragment_header(
    const std::uint32_t sequence_value = 1U)
{
    return goldsrc::NetchanHeader{
        goldsrc::NetchanSequenceWord{
            sequence(sequence_value),
            goldsrc::NetchanSequenceFlags{true, true},
        },
        goldsrc::NetchanAcknowledgementWord{sequence(0U), false},
    };
}

template<typename Result>
void check_error(
    const Result& result,
    const goldsrc::NetchanPacketErrorCode expected)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

inline constexpr std::array<std::uint8_t, 11U> kMinimumDecodedNormalBody{
    0x01U,
    0x01U, 0x00U, 0x01U, 0x00U,
    0x00U, 0x00U,
    0x01U, 0x00U,
    0x00U,
    0xabU,
};

// Independent literal: the body above transformed with numeric sequence 1.
inline constexpr std::array<std::uint8_t, 19U> kMinimumEncodedDatagramSequence1{
    0x01U, 0x00U, 0x00U, 0xc0U,
    0x00U, 0x00U, 0x00U, 0x00U,
    0x5aU, 0x18U, 0x01U, 0x00U,
    0x1aU, 0x00U, 0x10U, 0x41U,
    0x00U, 0x00U, 0xabU,
};

// Same decoded descriptor/body under the fresh numeric sequence-2 key.
inline constexpr std::array<std::uint8_t, 19U> kMinimumEncodedDatagramSequence2{
    0x02U, 0x00U, 0x00U, 0xc0U,
    0x00U, 0x00U, 0x00U, 0x00U,
    0x59U, 0x18U, 0x01U, 0x03U,
    0x19U, 0x00U, 0x10U, 0x42U,
    0x00U, 0x00U, 0xabU,
};

TEST_CASE("Strict fragment codec matches independent descriptor literals",
          "[goldsrc][netchan][fragment][packet]")
{
    const auto decoded_body = bytes(kMinimumDecodedNormalBody);
    const auto decoded = goldsrc::decode_netchan_fragment_body(
        goldsrc::NetchanDirection::server_to_client,
        fragment_header(),
        decoded_body);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    REQUIRE(decoded.packet->fragments[0]);
    CHECK(decoded.packet->fragments[0]->stream() ==
          goldsrc::NetchanFragmentStream::slot_0);
    CHECK(decoded.packet->fragments[0]->fragment_id == 0x0001'0001U);
    CHECK(decoded.packet->fragments[0]->offset == 0U);
    CHECK(decoded.packet->fragments[0]->length == 1U);
    REQUIRE(decoded.packet->fragments[0]->packed_id());
    CHECK(decoded.packet->fragments[0]->packed_id()->fragment_index() == 1U);
    CHECK(decoded.packet->fragments[0]->packed_id()->fragment_count() == 1U);
    CHECK(decoded.packet->fragments[0]->packed_id()->wire_value() == 0x0001'0001U);
    CHECK_FALSE(decoded.packet->fragments[1]);
    CHECK(decoded.packet->payload == bytes({0xabU}));
    CHECK(decoded.packet->fragment_payload_size == 1U);

    const auto encoded_body = goldsrc::encode_netchan_fragment_body(
        goldsrc::NetchanDirection::server_to_client,
        fragment_header(),
        decoded.packet->fragments,
        decoded.packet->payload);
    REQUIRE(encoded_body);
    CHECK(*encoded_body.decoded_body == decoded_body);

    const auto datagram = bytes(kMinimumEncodedDatagramSequence1);
    const auto full = goldsrc::decode_server_to_client_netchan_packet(datagram);
    REQUIRE(full);
    REQUIRE(full.packet->fragments[0]);
    CHECK(full.packet->payload == bytes({0xabU}));

    const auto reencoded = goldsrc::encode_server_to_client_netchan_packet(*full.packet);
    REQUIRE(reencoded);
    CHECK(*reencoded.datagram == datagram);
}

TEST_CASE("Retransmitted fragment uses a fresh sequence transform over stable bytes",
          "[goldsrc][netchan][fragment][packet][retransmit]")
{
    const auto first_datagram = bytes(kMinimumEncodedDatagramSequence1);
    const auto retry_datagram = bytes(kMinimumEncodedDatagramSequence2);
    REQUIRE(first_datagram != retry_datagram);

    const auto first = goldsrc::decode_server_to_client_netchan_packet(first_datagram);
    const auto retry = goldsrc::decode_server_to_client_netchan_packet(retry_datagram);
    REQUIRE(first);
    REQUIRE(retry);
    CHECK(first.packet->header.sequence.sequence == sequence(1U));
    CHECK(retry.packet->header.sequence.sequence == sequence(2U));
    CHECK(first.packet->fragments[0]->fragment_id ==
          retry.packet->fragments[0]->fragment_id);
    CHECK(first.packet->payload == retry.packet->payload);
}

TEST_CASE("Strict fragment codec accepts the previously observed final shape",
          "[goldsrc][netchan][fragment][packet][final]")
{
    auto final_body = bytes({
        0x01U,
        0x05U, 0x00U, 0x05U, 0x00U,
        0x00U, 0x00U,
        0x5aU, 0x00U,
        0x00U,
    });
    for (std::uint16_t value = 0U; value < 90U; ++value) {
        final_body.push_back(static_cast<std::byte>(value));
    }

    const auto decoded = goldsrc::decode_netchan_fragment_body(
        goldsrc::NetchanDirection::server_to_client,
        fragment_header(5U),
        final_body);
    REQUIRE(decoded);
    REQUIRE(decoded.packet->fragments[0]);
    REQUIRE(decoded.packet->fragments[0]->packed_id());
    CHECK(decoded.packet->fragments[0]->packed_id()->fragment_index() == 5U);
    CHECK(decoded.packet->fragments[0]->packed_id()->fragment_count() == 5U);
    CHECK(decoded.packet->fragments[0]->length == 90U);
    CHECK(decoded.packet->payload.size() == 90U);
    CHECK(decoded.packet->fragment_payload_size == 90U);
}

TEST_CASE("Both ordered fragment slots have strict typed classification",
          "[goldsrc][netchan][fragment][packet][streams]")
{
    const auto slot_one_body = bytes({
        0x00U,
        0x01U,
        0x01U, 0x00U, 0x01U, 0x00U,
        0x00U, 0x00U,
        0x01U, 0x00U,
        0xcdU,
    });
    const auto decoded = goldsrc::decode_netchan_fragment_body(
        goldsrc::NetchanDirection::server_to_client,
        fragment_header(),
        slot_one_body);
    REQUIRE(decoded);
    CHECK_FALSE(decoded.packet->fragments[0]);
    REQUIRE(decoded.packet->fragments[1]);
    CHECK(decoded.packet->fragments[1]->stream() ==
          goldsrc::NetchanFragmentStream::slot_1);
    CHECK(decoded.packet->payload == bytes({0xcdU}));
    CHECK(decoded.packet->fragment_payload_size == 1U);
}

TEST_CASE("Fragment descriptor minimum and every truncation prefix are bounded",
          "[goldsrc][netchan][fragment][packet][truncation]")
{
    const auto valid = bytes(kMinimumDecodedNormalBody);
    REQUIRE(goldsrc::decode_netchan_fragment_body(
        goldsrc::NetchanDirection::server_to_client,
        fragment_header(),
        valid));

    for (std::size_t size = 0U; size < valid.size(); ++size) {
        CAPTURE(size);
        const auto result = goldsrc::decode_netchan_fragment_body(
            goldsrc::NetchanDirection::server_to_client,
            fragment_header(),
            std::span<const std::byte>{valid}.first(size));
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
    }
}

TEST_CASE("Fragment descriptor rejects malformed identities and ranges atomically",
          "[goldsrc][netchan][fragment][packet][malformed]")
{
    SECTION("presence is not zero or one")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({0x02U})),
            goldsrc::NetchanPacketErrorCode::invalid_fragment_presence);
    }

    SECTION("packed identity is zero")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({
                    0x01U, 0x00U, 0x00U, 0x00U, 0x00U,
                    0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0xaaU,
                })),
            goldsrc::NetchanPacketErrorCode::invalid_fragment_id);
    }

    SECTION("count is zero")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({
                    0x01U, 0x00U, 0x00U, 0x01U, 0x00U,
                    0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0xaaU,
                })),
            goldsrc::NetchanPacketErrorCode::invalid_fragment_count);
    }

    SECTION("index is zero or greater than count")
    {
        for (const auto body : {
                 bytes({
                     0x01U, 0x01U, 0x00U, 0x00U, 0x00U,
                     0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0xaaU,
                 }),
                 bytes({
                     0x01U, 0x01U, 0x00U, 0x02U, 0x00U,
                     0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0xaaU,
                 })}) {
            check_error(
                goldsrc::decode_netchan_fragment_body(
                    goldsrc::NetchanDirection::server_to_client,
                    fragment_header(),
                    body),
                goldsrc::NetchanPacketErrorCode::invalid_fragment_index);
        }
    }

    SECTION("nonempty payload range starting at the available end is truncated")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({
                    0x01U, 0x01U, 0x00U, 0x01U, 0x00U,
                    0x02U, 0x00U, 0x01U, 0x00U, 0x00U, 0xaaU, 0xbbU,
                })),
            goldsrc::NetchanPacketErrorCode::fragment_payload_truncated);
    }

    SECTION("length is zero")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({
                    0x01U, 0x01U, 0x00U, 0x01U, 0x00U,
                    0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
                })),
            goldsrc::NetchanPacketErrorCode::invalid_fragment_length);
    }

    SECTION("offset plus length exceeds the 16-bit range")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({
                    0x01U, 0x01U, 0x00U, 0x01U, 0x00U,
                    0xffU, 0xffU, 0x01U, 0x00U, 0x00U, 0xaaU,
                })),
            goldsrc::NetchanPacketErrorCode::fragment_range_overflow);
    }

    SECTION("payload is shorter than declared")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({
                    0x01U, 0x01U, 0x00U, 0x01U, 0x00U,
                    0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0xaaU,
                })),
            goldsrc::NetchanPacketErrorCode::fragment_payload_truncated);
    }

    SECTION("stock-shaped 41-byte fragment preserves an 11-byte contemporaneous suffix")
    {
        auto body = bytes({
            0x01U, 0x01U, 0x00U, 0x01U, 0x00U,
            0x00U, 0x00U, 0x29U, 0x00U, 0x00U,
        });
        body.insert(body.end(), 41U, std::byte{0xa1});
        body.insert(body.end(), 11U, std::byte{0xb2});
        const auto decoded = goldsrc::decode_netchan_fragment_body(
            goldsrc::NetchanDirection::server_to_client,
            fragment_header(),
            body);
        REQUIRE(decoded);
        REQUIRE(decoded.packet->payload.size() == 52U);
        CHECK(decoded.packet->fragment_payload_size == 41U);
        CHECK(std::ranges::all_of(
            std::span<const std::byte>{decoded.packet->payload}.first(41U),
            [](const std::byte value) { return value == std::byte{0xa1}; }));
        CHECK(std::ranges::all_of(
            std::span<const std::byte>{decoded.packet->payload}.subspan(41U),
            [](const std::byte value) { return value == std::byte{0xb2}; }));
    }

    SECTION("overlapping descriptor ranges are rejected")
    {
        check_error(
            goldsrc::decode_netchan_fragment_body(
                goldsrc::NetchanDirection::server_to_client,
                fragment_header(),
                bytes({
                    0x01U, 0x01U, 0x00U, 0x01U, 0x00U,
                    0x00U, 0x00U, 0x02U, 0x00U,
                    0x01U, 0x01U, 0x00U, 0x01U, 0x00U,
                    0x01U, 0x00U, 0x02U, 0x00U,
                    0xaaU, 0xbbU, 0xccU,
                })),
            goldsrc::NetchanPacketErrorCode::fragment_payload_overlap);
    }
}

TEST_CASE("Fragment full decoder rejects reserved flags and wrong transform keys",
          "[goldsrc][netchan][fragment][packet][transform]")
{
    auto reserved = bytes(kMinimumEncodedDatagramSequence1);
    reserved[7] = std::byte{0x40};
    check_error(
        goldsrc::decode_server_to_client_netchan_packet(reserved),
        goldsrc::NetchanPacketErrorCode::reserved_acknowledgement_flag);

    auto wrong_key = bytes(kMinimumEncodedDatagramSequence1);
    wrong_key[0] = std::byte{0x02};
    const auto rejected =
        goldsrc::decode_server_to_client_netchan_packet(wrong_key);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
}

TEST_CASE("Fragment route rejects connectionless and leaves nonfragment bodies alone",
          "[goldsrc][netchan][fragment][packet][routing]")
{
    check_error(
        goldsrc::decode_server_to_client_netchan_packet(bytes({
            0xffU, 0xffU, 0xffU, 0xffU,
            0x00U, 0x00U, 0x00U, 0x00U,
        })),
        goldsrc::NetchanPacketErrorCode::connectionless_packet);

    const auto ordinary = bytes({
        0x01U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U,
        0xabU,
    });
    const auto decoded = goldsrc::decode_server_to_client_netchan_packet(ordinary);
    REQUIRE(decoded);
    CHECK_FALSE(decoded.packet->header.sequence.flags.fragmented);
    CHECK(decoded.packet->payload == bytes({0xabU}));

    auto nonfragment_header = fragment_header();
    nonfragment_header.sequence.flags.fragmented = false;
    check_error(
        goldsrc::decode_netchan_fragment_body(
            goldsrc::NetchanDirection::server_to_client,
            nonfragment_header,
            bytes(kMinimumDecodedNormalBody)),
        goldsrc::NetchanPacketErrorCode::fragment_flag_required);
}

} // namespace

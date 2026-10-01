#pragma once

#include "delta_test_fixture.hpp"

#include <array>
#include <memory>

namespace hlclient::test::event_fixture {

namespace goldsrc = hlclient::goldsrc;

// Independent literal LSB-first packets derived from the documented field
// widths, not from a production event encoder. The delta schema intentionally
// has a project-owned field: wire framing must follow the advertised schema,
// not a hardcoded Half-Life event_args_t native layout.
inline constexpr delta_fixture::Field kFields[]{
    {"fixture_value", 0x0000'0001U, 0U, 8U},
};

[[nodiscard]] inline std::shared_ptr<const goldsrc::DeltaSchemaRegistryState>
schemas()
{
    const auto bytes = delta_fixture::schema("event_t", kFields);
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE(parsed);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

// svc_event: count5, index10, packet-present1, optional packet11 and
// delta-present1/delta, delay-present1/optional delay16. One final alignment.
inline constexpr std::array kEmpty{
    std::byte{3}, std::byte{0x00}}; // 5 meaningful body bits
inline constexpr std::array kNoPacket{
    std::byte{3}, std::byte{0xe1}, std::byte{0x00}, std::byte{0x00}}; // index7, 17 bits
inline constexpr std::array kNoPacketDelay{
    std::byte{3}, std::byte{0xe1}, std::byte{0x00}, std::byte{0x4b},
    std::byte{0x00}, std::byte{0x00}}; // index7, delay37, 33 bits
inline constexpr std::array kPacketNoDelta{
    std::byte{3}, std::byte{0x21}, std::byte{0x81}, std::byte{0x11},
    std::byte{0x00}}; // index9, packet17, 29 bits
inline constexpr std::array kPacketDelay{
    std::byte{3}, std::byte{0x21}, std::byte{0x81}, std::byte{0x11},
    std::byte{0xb0}, std::byte{0x04}, std::byte{0x00}}; // delay37, 45 bits
inline constexpr std::array kPacketDelta{
    std::byte{3}, std::byte{0x21}, std::byte{0x81}, std::byte{0x11},
    std::byte{0x98}, std::byte{0x00}, std::byte{0x15}}; // fixture_value42, 48 bits
inline constexpr std::array kPacketDefault{
    std::byte{3}, std::byte{0x21}, std::byte{0x81}, std::byte{0x11},
    std::byte{0x08}}; // zero-mask delta/default, 32 bits
inline constexpr std::array kMaximumReferences{
    std::byte{3}, std::byte{0xe1}, std::byte{0xff}, std::byte{0xff},
    std::byte{0x07}}; // index1023, packet2047, no delta/delay, 29 bits
inline constexpr std::array kMultiple{
    std::byte{3}, std::byte{0x83}, std::byte{0x00}, std::byte{0xc9},
    std::byte{0x00}, std::byte{0xfe}, std::byte{0x1b}, std::byte{0x81},
    std::byte{0x09}, std::byte{0x50}, std::byte{0xf1}, std::byte{0x3f}};
// Three entries: index4/no packet/delay100; index511/packet17/delta42;
// index1023/no packet/no delay. 88 body bits; entries are NOT byte-aligned.

// svc_event_reliable: index10, mandatory event_t delta (mask zero is still
// a delta), delay-present1/optional delay16, final alignment. Not count-prefixed.
inline constexpr std::array kReliableDefault{
    std::byte{21}, std::byte{0x07}, std::byte{0x00}}; // index7/default, 14 bits
inline constexpr std::array kReliableDelta{
    std::byte{21}, std::byte{0x07}, std::byte{0x24}, std::byte{0x40},
    std::byte{0x05}}; // index7/fixture_value42, 30 bits
inline constexpr std::array kReliableDelay{
    std::byte{21}, std::byte{0x07}, std::byte{0x60}, std::byte{0x09},
    std::byte{0x00}}; // index7/default/delay37, 30 bits

// Existing svc_sound grammar: zero optional mask, CHAN3/entity19/sound5,
// coordinate-presence bits all zero, six final alignment bits.
inline constexpr std::array kSound{
    std::byte{6}, std::byte{0x00}, std::byte{0x36}, std::byte{0x81},
    std::byte{0x02}, std::byte{0x00}};

template <std::size_t Size>
[[nodiscard]] inline std::vector<std::byte> bytes(
    const std::array<std::byte, Size>& values)
{
    return {values.begin(), values.end()};
}

} // namespace hlclient::test::event_fixture

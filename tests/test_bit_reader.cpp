#include <hlclient/goldsrc/bit_reader.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

namespace goldsrc = hlclient::goldsrc;

TEST_CASE("GoldSrc bit reader consumes least-significant bits first", "[goldsrc][delta][bits]")
{
    constexpr std::array bytes{
        std::byte{0b1011'0010U},
        std::byte{0b0101'1100U},
    };
    goldsrc::BitReader reader{bytes};

    CHECK(reader.valid());
    CHECK(reader.byte_aligned());
    REQUIRE(reader.read_bits(3U));
    CHECK(reader.bit_offset() == 3U);

    goldsrc::BitReader exact{bytes};
    const auto first = exact.read_bits(4U);
    const auto second = exact.read_bits(8U);
    const auto third = exact.read_bits(4U);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(third);
    CHECK(first.value == 0x2U);
    CHECK(second.value == 0xcbU);
    CHECK(third.value == 0x5U);
    CHECK(exact.remaining_bits() == 0U);
}

TEST_CASE("GoldSrc bit reader covers one-bit exact-end and maximum-width reads",
          "[goldsrc][delta][bits]")
{
    constexpr std::array bytes{
        std::byte{0x01U},
        std::byte{0x23U},
        std::byte{0x45U},
        std::byte{0x80U},
    };
    goldsrc::BitReader one_bit{bytes, 31U, 1U};
    const auto final_bit = one_bit.read_bits(1U);
    REQUIRE(final_bit);
    CHECK(final_bit.value == 1U);
    CHECK(one_bit.remaining_bits() == 0U);
    const auto beyond = one_bit.read_bits(1U);
    CHECK_FALSE(beyond);
    CHECK(beyond.error == goldsrc::BitReaderError::truncated);
    CHECK(one_bit.bit_offset() == 32U);

    goldsrc::BitReader maximum{bytes};
    const auto full = maximum.read_bits(32U);
    REQUIRE(full);
    CHECK(full.value == 0x8045'2301U);
    CHECK(maximum.remaining_bits() == 0U);
}

TEST_CASE("GoldSrc bit reader failures preserve its cursor", "[goldsrc][delta][bits]")
{
    constexpr std::array bytes{std::byte{0x01U}};
    goldsrc::BitReader reader{bytes, 1U, 6U};
    REQUIRE(reader.valid());
    const auto initial = reader.bit_offset();

    const auto too_wide = reader.read_bits(33U);
    CHECK_FALSE(too_wide);
    CHECK(too_wide.error == goldsrc::BitReaderError::invalid_width);
    CHECK(reader.bit_offset() == initial);

    const auto truncated = reader.read_bits(7U);
    CHECK_FALSE(truncated);
    CHECK(truncated.error == goldsrc::BitReaderError::truncated);
    CHECK(reader.bit_offset() == initial);

    const auto empty = reader.read_bits(0U);
    REQUIRE(empty);
    CHECK(empty.value == 0U);
    CHECK(reader.bit_offset() == initial);

    goldsrc::BitReader invalid{bytes, 9U};
    CHECK_FALSE(invalid.valid());
    CHECK(invalid.read_bits(1U).error == goldsrc::BitReaderError::invalid_geometry);
}

TEST_CASE("GoldSrc bit reader accepts only zero byte-alignment padding", "[goldsrc][delta][bits]")
{
    constexpr std::array zero_padding{std::byte{0b0000'0011U}};
    goldsrc::BitReader accepted{zero_padding, 2U};
    CHECK(accepted.align_to_byte_zero_padding() == goldsrc::BitReaderError::none);
    CHECK(accepted.bit_offset() == 8U);

    constexpr std::array nonzero_padding{std::byte{0b0010'0011U}};
    goldsrc::BitReader rejected{nonzero_padding, 2U};
    CHECK(rejected.align_to_byte_zero_padding() ==
          goldsrc::BitReaderError::nonzero_padding);
    CHECK(rejected.bit_offset() == 2U);
}

} // namespace

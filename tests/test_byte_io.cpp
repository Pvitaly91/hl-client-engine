#include <hlclient/goldsrc/byte_reader.hpp>
#include <hlclient/goldsrc/byte_writer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

using hlclient::goldsrc::ByteReader;
using hlclient::goldsrc::ByteWriter;

[[nodiscard]] bool bytes_equal(
    const std::span<const std::byte> left,
    const std::span<const std::byte> right)
{
    return std::ranges::equal(left, right);
}

TEST_CASE("GoldSrc byte values round-trip through writer and reader", "[goldsrc][bytes]")
{
    std::array<std::byte, 64> storage{};
    ByteWriter writer{storage};
    const std::array raw_bytes{std::byte{0xde}, std::byte{0xad}};

    REQUIRE(writer.write_uint8(0xabU));
    REQUIRE(writer.write_int8(-7));
    REQUIRE(writer.write_uint16_le(0x1234U));
    REQUIRE(writer.write_int16_le(-12'345));
    REQUIRE(writer.write_uint32_le(0x89ab'cdefU));
    REQUIRE(writer.write_int32_le(-12'345'678));
    REQUIRE(writer.write_float32_le(123.25F));
    REQUIRE(writer.write_bytes(raw_bytes));
    REQUIRE(writer.write_c_string("GoldSrc"));

    ByteReader reader{writer.written_bytes()};

    const auto unsigned_byte = reader.read_uint8();
    REQUIRE(unsigned_byte.has_value());
    CHECK(*unsigned_byte == 0xabU);

    const auto signed_byte = reader.read_int8();
    REQUIRE(signed_byte.has_value());
    CHECK(*signed_byte == -7);

    const auto unsigned_short = reader.read_uint16_le();
    REQUIRE(unsigned_short.has_value());
    CHECK(*unsigned_short == 0x1234U);

    const auto signed_short = reader.read_int16_le();
    REQUIRE(signed_short.has_value());
    CHECK(*signed_short == -12'345);

    const auto unsigned_integer = reader.read_uint32_le();
    REQUIRE(unsigned_integer.has_value());
    CHECK(*unsigned_integer == 0x89ab'cdefU);

    const auto signed_integer = reader.read_int32_le();
    REQUIRE(signed_integer.has_value());
    CHECK(*signed_integer == -12'345'678);

    const auto floating_point = reader.read_float32_le();
    REQUIRE(floating_point.has_value());
    CHECK(*floating_point == 123.25F);

    const auto raw = reader.read_bytes(raw_bytes.size());
    REQUIRE(raw.has_value());
    CHECK(bytes_equal(*raw, raw_bytes));

    const auto text = reader.read_c_string();
    REQUIRE(text.has_value());
    CHECK(*text == "GoldSrc");
    CHECK(reader.remaining() == 0U);
}

TEST_CASE("ByteWriter emits exact little-endian wire bytes", "[goldsrc][bytes]")
{
    std::array<std::byte, 10> storage{};
    ByteWriter writer{storage};

    REQUIRE(writer.write_uint16_le(0x1234U));
    REQUIRE(writer.write_uint32_le(0x89ab'cdefU));
    REQUIRE(writer.write_float32_le(1.0F));

    const std::array expected{
        std::byte{0x34},
        std::byte{0x12},
        std::byte{0xef},
        std::byte{0xcd},
        std::byte{0xab},
        std::byte{0x89},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x80},
        std::byte{0x3f},
    };
    CHECK(bytes_equal(writer.written_bytes(), expected));
}

TEST_CASE("ByteReader bounds failures do not advance its cursor", "[goldsrc][bytes]")
{
    const std::array bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    ByteReader reader{bytes};

    CHECK_FALSE(reader.read_uint32_le().has_value());
    CHECK(reader.position() == 0U);
    CHECK(reader.remaining() == bytes.size());

    CHECK_FALSE(reader.read_bytes(4).has_value());
    CHECK(reader.position() == 0U);

    const auto value = reader.read_uint16_le();
    REQUIRE(value.has_value());
    CHECK(*value == 0x0201U);
    CHECK(reader.position() == 2U);

    CHECK_FALSE(reader.read_uint16_le().has_value());
    CHECK(reader.position() == 2U);
}

TEST_CASE("ByteWriter capacity failures are atomic", "[goldsrc][bytes]")
{
    std::array<std::byte, 3> storage{};
    storage.fill(std::byte{0xa5});
    ByteWriter writer{storage};

    CHECK_FALSE(writer.write_uint32_le(0x1234'5678U));
    CHECK(writer.position() == 0U);
    CHECK(std::ranges::all_of(storage, [](const std::byte value) {
        return value == std::byte{0xa5};
    }));

    REQUIRE(writer.write_uint8(0x11U));
    CHECK_FALSE(writer.write_uint32_le(0x1234'5678U));
    CHECK(writer.position() == 1U);
    CHECK(storage[0] == std::byte{0x11});
    CHECK(storage[1] == std::byte{0xa5});
    CHECK(storage[2] == std::byte{0xa5});
}

TEST_CASE("C string reads require a terminator and preserve cursor on failure", "[goldsrc][bytes]")
{
    const std::array bytes{std::byte{'h'}, std::byte{'l'}};
    ByteReader reader{bytes};

    CHECK_FALSE(reader.read_c_string().has_value());
    CHECK(reader.position() == 0U);
    CHECK(reader.remaining() == bytes.size());
}

TEST_CASE("C string reads stop at embedded NUL bytes", "[goldsrc][bytes]")
{
    const std::array bytes{
        std::byte{'a'},
        std::byte{0},
        std::byte{'b'},
        std::byte{0},
    };
    ByteReader reader{bytes};

    const auto first = reader.read_c_string();
    REQUIRE(first.has_value());
    CHECK(*first == "a");
    CHECK(reader.position() == 2U);

    const auto second = reader.read_c_string();
    REQUIRE(second.has_value());
    CHECK(*second == "b");
    CHECK(reader.remaining() == 0U);
}

TEST_CASE("ByteWriter rejects embedded NUL strings without a partial write", "[goldsrc][bytes]")
{
    std::array<std::byte, 8> storage{};
    storage.fill(std::byte{0xa5});
    ByteWriter writer{storage};
    constexpr char embedded_nul[]{'a', '\0', 'b'};

    CHECK_FALSE(writer.write_c_string(std::string_view{embedded_nul, sizeof(embedded_nul)}));
    CHECK(writer.position() == 0U);
    CHECK(std::ranges::all_of(storage, [](const std::byte value) {
        return value == std::byte{0xa5};
    }));
}

TEST_CASE("ByteWriter rejects an oversized C string without a partial write", "[goldsrc][bytes]")
{
    std::array<std::byte, 3> storage{};
    storage.fill(std::byte{0xa5});
    ByteWriter writer{storage};

    CHECK_FALSE(writer.write_c_string("abc"));
    CHECK(writer.position() == 0U);
    CHECK(std::ranges::all_of(storage, [](const std::byte value) {
        return value == std::byte{0xa5};
    }));
}

TEST_CASE("ByteWriter handles overlapping source and destination spans", "[goldsrc][bytes]")
{
    std::array storage{
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
        std::byte{0x05},
    };
    ByteWriter writer{storage};

    REQUIRE(writer.write_uint8(0x09U));
    REQUIRE(writer.write_bytes(std::span<const std::byte>{storage}.first(4)));

    const std::array expected{
        std::byte{0x09},
        std::byte{0x09},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
    };
    CHECK(bytes_equal(writer.written_bytes(), expected));
}

} // namespace

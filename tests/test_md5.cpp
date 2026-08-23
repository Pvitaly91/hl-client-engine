#include <hlclient/hash/md5.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

namespace hash = hlclient::hash;

[[nodiscard]] constexpr std::uint8_t hex_nibble(const char value) noexcept
{
    return value >= '0' && value <= '9'
               ? static_cast<std::uint8_t>(value - '0')
               : static_cast<std::uint8_t>(value - 'a' + 10);
}

[[nodiscard]] constexpr hash::Md5Digest expected_digest(
    const std::string_view hexadecimal) noexcept
{
    hash::Md5Digest result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto high = hex_nibble(hexadecimal[index * 2U]);
        const auto low = hex_nibble(hexadecimal[index * 2U + 1U]);
        result[index] = std::byte{static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(high << 4U) | low)};
    }
    return result;
}

[[nodiscard]] std::span<const std::byte> text_bytes(
    const std::string_view text) noexcept
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] hash::Md5Digest hash_in_one_update(
    const std::span<const std::byte> bytes)
{
    hash::Md5Hasher hasher;
    REQUIRE(hasher.update(bytes));
    const auto digest = hasher.finalize();
    REQUIRE(digest);
    return *digest;
}

TEST_CASE("MD5 matches the independent standard vectors",
          "[hash][md5]")
{
    struct Vector {
        std::string_view message;
        std::string_view digest;
    };

    constexpr std::array<Vector, 7U> vectors{{
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"a", "0cc175b9c0f1b6a831c399e269772661"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"abcdefghijklmnopqrstuvwxyz",
         "c3fcd3d76192e4007dfb496cca67e13b"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
         "d174ab98d277d9f5a5611c2c9f419d9f"},
        {"123456789012345678901234567890123456789012345678901234567890"
         "12345678901234567890",
         "57edf4a22be3c955ac49da2e2107b67a"},
    }};

    for (const auto& vector : vectors) {
        INFO("message length: " << vector.message.size());
        CHECK(hash_in_one_update(text_bytes(vector.message)) ==
              expected_digest(vector.digest));
    }
}

TEST_CASE("MD5 matches the independent one-million-a vector",
          "[hash][md5]")
{
    constexpr std::size_t chunk_size = 1'000U;
    constexpr std::size_t chunk_count = 1'000U;
    std::array<std::byte, chunk_size> chunk{};
    chunk.fill(std::byte{'a'});

    hash::Md5Hasher hasher;
    for (std::size_t index = 0U; index < chunk_count; ++index) {
        REQUIRE(hasher.update(chunk));
    }

    CHECK(hasher.byte_count() == 1'000'000U);
    const auto digest = hasher.finalize();
    REQUIRE(digest);
    CHECK(*digest == expected_digest("7707d6ae4e027c70eea2a935c2296f21"));
}

TEST_CASE("MD5 padding handles every adjacent block boundary",
          "[hash][md5]")
{
    struct BoundaryVector {
        std::size_t byte_count;
        std::string_view digest;
    };

    constexpr std::array<BoundaryVector, 7U> vectors{{
        {0U, "d41d8cd98f00b204e9800998ecf8427e"},
        {1U, "0cc175b9c0f1b6a831c399e269772661"},
        {55U, "ef1772b6dff9a122358552954ad0df65"},
        {56U, "3b0c8ac703f828b04c6c197006d17218"},
        {63U, "b06521f39153d618550606be297466d5"},
        {64U, "014842d480b571495a4a0363793f7367"},
        {65U, "c743a45e0d2e6a95cb859adae0248435"},
    }};

    for (const auto& vector : vectors) {
        INFO("message length: " << vector.byte_count);
        const std::vector<std::byte> message(
            vector.byte_count, std::byte{'a'});
        CHECK(hash_in_one_update(message) == expected_digest(vector.digest));
    }
}

TEST_CASE("MD5 update is incremental across arbitrary chunking",
          "[hash][md5]")
{
    constexpr std::string_view message =
        "The quick brown fox jumps over the lazy dog";
    constexpr auto expected =
        expected_digest("9e107d9d372bb6826bd81d3542a419d6");

    SECTION("multiple differently sized updates")
    {
        const auto bytes = text_bytes(message);
        hash::Md5Hasher hasher;
        REQUIRE(hasher.update(bytes.first(1U)));
        REQUIRE(hasher.update(bytes.subspan(1U, 7U)));
        REQUIRE(hasher.update(bytes.subspan(8U, 16U)));
        REQUIRE(hasher.update(bytes.subspan(24U)));
        REQUIRE(hasher.finalize() == expected);
    }

    SECTION("one-byte updates")
    {
        hash::Md5Hasher hasher;
        for (const auto byte : text_bytes(message)) {
            REQUIRE(hasher.update(std::span{&byte, 1U}));
        }
        REQUIRE(hasher.finalize() == expected);
    }
}

TEST_CASE("MD5 finalization is idempotent and closes the update lifecycle",
          "[hash][md5]")
{
    hash::Md5Hasher hasher;
    REQUIRE(hasher.update(text_bytes("abc")));
    const auto byte_count_before_finalize = hasher.byte_count();

    const auto first = hasher.finalize();
    const auto second = hasher.finalize();

    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == expected_digest("900150983cd24fb0d6963f7d28e17f72"));
    CHECK(*second == *first);
    CHECK(hasher.finalized());
    CHECK(hasher.byte_count() == byte_count_before_finalize);
    CHECK_FALSE(hasher.update(text_bytes("x")));
    CHECK_FALSE(hasher.update({}));
    CHECK(hasher.byte_count() == byte_count_before_finalize);
    CHECK(hasher.finalize() == first);
}

TEST_CASE("MD5 reset restores a fresh independent lifecycle", "[hash][md5]")
{
    hash::Md5Hasher hasher;
    REQUIRE(hasher.update(text_bytes("abc")));
    REQUIRE(hasher.finalize());

    hasher.reset();

    CHECK_FALSE(hasher.finalized());
    CHECK(hasher.byte_count() == 0U);
    REQUIRE(hasher.update(text_bytes("message digest")));
    const auto reset_digest = hasher.finalize();
    REQUIRE(reset_digest);

    hash::Md5Hasher new_hasher;
    REQUIRE(new_hasher.update(text_bytes("message digest")));
    const auto new_digest = new_hasher.finalize();
    REQUIRE(new_digest);

    CHECK(*reset_digest ==
          expected_digest("f96b697d7cb7938d525a2f31aaf161d0"));
    CHECK(*new_digest == *reset_digest);
}

static_assert(hash::kMd5DigestSize == 16U);
static_assert(std::is_nothrow_default_constructible_v<hash::Md5Hasher>);

} // namespace

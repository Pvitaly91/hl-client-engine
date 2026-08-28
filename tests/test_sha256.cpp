#include <catch2/catch_test_macros.hpp>

#include <hlclient/hash/sha256.hpp>

#include <cstddef>
#include <span>
#include <string_view>

TEST_CASE("SHA-256 matches public empty and abc vectors")
{
    SECTION("empty")
    {
        const auto digest = hlclient::hash::sha256({});
        REQUIRE(digest);
        CHECK(hlclient::hash::sha256_hex(*digest) ==
              "e3b0c44298fc1c149afbf4c8996fb924"
              "27ae41e4649b934ca495991b7852b855");
    }

    SECTION("abc")
    {
        constexpr std::string_view input{"abc"};
        const auto bytes = std::as_bytes(std::span{input});
        const auto digest = hlclient::hash::sha256(bytes);
        REQUIRE(digest);
        CHECK(hlclient::hash::sha256_hex(*digest) ==
              "ba7816bf8f01cfea414140de5dae2223"
              "b00361a396177a9cb410ff61f20015ad");
    }
}

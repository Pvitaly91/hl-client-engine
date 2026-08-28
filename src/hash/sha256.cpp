#include <hlclient/hash/sha256.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace hlclient::hash {
namespace {

[[nodiscard]] constexpr std::uint32_t choose(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) noexcept
{
    return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t majority(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) noexcept
{
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t big_sigma_zero(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 2) ^ std::rotr(value, 13) ^ std::rotr(value, 22);
}

[[nodiscard]] constexpr std::uint32_t big_sigma_one(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 6) ^ std::rotr(value, 11) ^ std::rotr(value, 25);
}

[[nodiscard]] constexpr std::uint32_t small_sigma_zero(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 7) ^ std::rotr(value, 18) ^ (value >> 3U);
}

[[nodiscard]] constexpr std::uint32_t small_sigma_one(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 17) ^ std::rotr(value, 19) ^ (value >> 10U);
}

inline constexpr std::array<std::uint32_t, 64U> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

} // namespace

std::optional<Sha256Digest> sha256(
    const std::span<const std::byte> bytes) noexcept
{
    constexpr auto maximum_bytes =
        (std::numeric_limits<std::uint64_t>::max)() / 8U;
    if (bytes.size() > maximum_bytes ||
        bytes.size() > (std::numeric_limits<std::size_t>::max)() - 72U) {
        return std::nullopt;
    }

    std::array<std::uint32_t, 8U> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    const auto block_count = (bytes.size() + 72U) / 64U;
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    std::array<std::byte, 64U> block{};
    std::array<std::uint32_t, 64U> words{};

    for (std::size_t block_index = 0U; block_index < block_count;
         ++block_index) {
        block.fill(std::byte{0});
        const auto block_offset = block_index * block.size();
        for (std::size_t index = 0U; index < block.size(); ++index) {
            const auto source_offset = block_offset + index;
            if (source_offset < bytes.size()) {
                block[index] = bytes[source_offset];
            } else if (source_offset == bytes.size()) {
                block[index] = std::byte{0x80U};
            }
        }
        if (block_index + 1U == block_count) {
            for (std::size_t index = 0U; index < 8U; ++index) {
                const auto shift = static_cast<unsigned int>((7U - index) * 8U);
                block[56U + index] = std::byte{static_cast<std::uint8_t>(
                    (bit_length >> shift) & 0xffU)};
            }
        }
        for (std::size_t index = 0U; index < 16U; ++index) {
            const auto offset = index * 4U;
            words[index] =
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(block[offset]))
                 << 24U) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(block[offset + 1U]))
                 << 16U) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(block[offset + 2U]))
                 << 8U) |
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(block[offset + 3U]));
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            words[index] = small_sigma_one(words[index - 2U]) +
                words[index - 7U] + small_sigma_zero(words[index - 15U]) +
                words[index - 16U];
        }

        auto a = state[0U];
        auto b = state[1U];
        auto c = state[2U];
        auto d = state[3U];
        auto e = state[4U];
        auto f = state[5U];
        auto g = state[6U];
        auto h = state[7U];
        for (std::size_t index = 0U; index < words.size(); ++index) {
            const auto first = h + big_sigma_one(e) + choose(e, f, g) +
                kRoundConstants[index] + words[index];
            const auto second = big_sigma_zero(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state[0U] += a;
        state[1U] += b;
        state[2U] += c;
        state[3U] += d;
        state[4U] += e;
        state[5U] += f;
        state[6U] += g;
        state[7U] += h;
    }

    Sha256Digest result{};
    for (std::size_t index = 0U; index < state.size(); ++index) {
        const auto offset = index * 4U;
        result[offset] = std::byte{
            static_cast<std::uint8_t>(state[index] >> 24U)};
        result[offset + 1U] = std::byte{
            static_cast<std::uint8_t>(state[index] >> 16U)};
        result[offset + 2U] = std::byte{
            static_cast<std::uint8_t>(state[index] >> 8U)};
        result[offset + 3U] =
            std::byte{static_cast<std::uint8_t>(state[index])};
    }
    return result;
}

std::string sha256_hex(const Sha256Digest& digest)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.resize(digest.size() * 2U);
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        const auto value = std::to_integer<std::uint8_t>(digest[index]);
        result[index * 2U] = digits[value >> 4U];
        result[index * 2U + 1U] = digits[value & 0x0fU];
    }
    return result;
}

} // namespace hlclient::hash

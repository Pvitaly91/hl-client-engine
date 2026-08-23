#include <hlclient/hash/md5.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace hlclient::hash {
namespace {

inline constexpr std::array<std::uint32_t, 4U> kInitialState{
    0x67452301U,
    0xEFCDAB89U,
    0x98BADCFEU,
    0x10325476U,
};

inline constexpr std::array<unsigned int, 64U> kRotationAmounts{
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U,
    5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U,
};

inline constexpr std::array<std::uint32_t, 64U> kRoundConstants{
    0xD76AA478U, 0xE8C7B756U, 0x242070DBU, 0xC1BDCEEEU,
    0xF57C0FAFU, 0x4787C62AU, 0xA8304613U, 0xFD469501U,
    0x698098D8U, 0x8B44F7AFU, 0xFFFF5BB1U, 0x895CD7BEU,
    0x6B901122U, 0xFD987193U, 0xA679438EU, 0x49B40821U,
    0xF61E2562U, 0xC040B340U, 0x265E5A51U, 0xE9B6C7AAU,
    0xD62F105DU, 0x02441453U, 0xD8A1E681U, 0xE7D3FBC8U,
    0x21E1CDE6U, 0xC33707D6U, 0xF4D50D87U, 0x455A14EDU,
    0xA9E3E905U, 0xFCEFA3F8U, 0x676F02D9U, 0x8D2A4C8AU,
    0xFFFA3942U, 0x8771F681U, 0x6D9D6122U, 0xFDE5380CU,
    0xA4BEEA44U, 0x4BDECFA9U, 0xF6BB4B60U, 0xBEBFBC70U,
    0x289B7EC6U, 0xEAA127FAU, 0xD4EF3085U, 0x04881D05U,
    0xD9D4D039U, 0xE6DB99E5U, 0x1FA27CF8U, 0xC4AC5665U,
    0xF4292244U, 0x432AFF97U, 0xAB9423A7U, 0xFC93A039U,
    0x655B59C3U, 0x8F0CCC92U, 0xFFEFF47DU, 0x85845DD1U,
    0x6FA87E4FU, 0xFE2CE6E0U, 0xA3014314U, 0x4E0811A1U,
    0xF7537E82U, 0xBD3AF235U, 0x2AD7D2BBU, 0xEB86D391U,
};

[[nodiscard]] constexpr std::uint32_t load_u32_le(
    const std::span<const std::byte, 4U> bytes) noexcept
{
    return std::uint32_t{std::to_integer<std::uint8_t>(bytes[0U])} |
           (std::uint32_t{std::to_integer<std::uint8_t>(bytes[1U])} << 8U) |
           (std::uint32_t{std::to_integer<std::uint8_t>(bytes[2U])} << 16U) |
           (std::uint32_t{std::to_integer<std::uint8_t>(bytes[3U])} << 24U);
}

constexpr void store_u32_le(
    const std::uint32_t value,
    const std::span<std::byte, 4U> output) noexcept
{
    output[0U] = std::byte{static_cast<std::uint8_t>(value)};
    output[1U] = std::byte{static_cast<std::uint8_t>(value >> 8U)};
    output[2U] = std::byte{static_cast<std::uint8_t>(value >> 16U)};
    output[3U] = std::byte{static_cast<std::uint8_t>(value >> 24U)};
}

constexpr void store_u64_le(
    const std::uint64_t value,
    const std::span<std::byte, 8U> output) noexcept
{
    for (std::size_t index = 0U; index < output.size(); ++index) {
        output[index] = std::byte{static_cast<std::uint8_t>(
            value >> static_cast<unsigned int>(index * 8U))};
    }
}

} // namespace

Md5Hasher::Md5Hasher() noexcept
{
    reset();
}

bool Md5Hasher::update(const std::span<const std::byte> bytes) noexcept
{
    if (finalized_) {
        return false;
    }

    const auto incoming_byte_count = static_cast<std::uint64_t>(bytes.size());
    if (incoming_byte_count > kMd5MaximumMessageBytes - byte_count_) {
        return false;
    }

    byte_count_ += incoming_byte_count;
    std::size_t source_offset = 0U;

    if (buffered_byte_count_ != 0U) {
        const auto copy_count = (std::min)(
            buffer_.size() - buffered_byte_count_, bytes.size());
        std::copy_n(
            bytes.begin(),
            copy_count,
            buffer_.begin() +
                static_cast<std::ptrdiff_t>(buffered_byte_count_));
        buffered_byte_count_ += copy_count;
        source_offset += copy_count;

        if (buffered_byte_count_ == buffer_.size()) {
            transform(std::span<const std::byte, kMd5BlockSize>{buffer_});
            buffered_byte_count_ = 0U;
        }
    }

    while (bytes.size() - source_offset >= kMd5BlockSize) {
        transform(std::span<const std::byte, kMd5BlockSize>{
            bytes.data() + static_cast<std::ptrdiff_t>(source_offset),
            kMd5BlockSize});
        source_offset += kMd5BlockSize;
    }

    const auto remaining_byte_count = bytes.size() - source_offset;
    if (remaining_byte_count != 0U) {
        std::copy_n(
            bytes.begin() + static_cast<std::ptrdiff_t>(source_offset),
            remaining_byte_count,
            buffer_.begin());
        buffered_byte_count_ = remaining_byte_count;
    }

    return true;
}

std::optional<Md5Digest> Md5Hasher::finalize() noexcept
{
    if (finalized_) {
        return digest_;
    }

    std::array<std::byte, kMd5BlockSize * 2U> final_blocks{};
    std::copy_n(
        buffer_.begin(), buffered_byte_count_, final_blocks.begin());
    final_blocks[buffered_byte_count_] = std::byte{0x80U};

    const auto final_block_byte_count =
        buffered_byte_count_ < 56U ? kMd5BlockSize : kMd5BlockSize * 2U;
    const auto message_bit_count = byte_count_ * 8U;
    store_u64_le(
        message_bit_count,
        std::span<std::byte, 8U>{
            final_blocks.data() +
                static_cast<std::ptrdiff_t>(final_block_byte_count - 8U),
            8U});

    transform(std::span<const std::byte, kMd5BlockSize>{
        final_blocks.data(), kMd5BlockSize});
    if (final_block_byte_count == kMd5BlockSize * 2U) {
        transform(std::span<const std::byte, kMd5BlockSize>{
            final_blocks.data() +
                static_cast<std::ptrdiff_t>(kMd5BlockSize),
            kMd5BlockSize});
    }

    for (std::size_t index = 0U; index < state_.size(); ++index) {
        store_u32_le(
            state_[index],
            std::span<std::byte, 4U>{
                digest_.data() + static_cast<std::ptrdiff_t>(index * 4U),
                4U});
    }

    buffer_.fill(std::byte{0U});
    buffered_byte_count_ = 0U;
    finalized_ = true;
    return digest_;
}

void Md5Hasher::reset() noexcept
{
    state_ = kInitialState;
    buffer_.fill(std::byte{0U});
    digest_.fill(std::byte{0U});
    byte_count_ = 0U;
    buffered_byte_count_ = 0U;
    finalized_ = false;
}

bool Md5Hasher::finalized() const noexcept
{
    return finalized_;
}

std::uint64_t Md5Hasher::byte_count() const noexcept
{
    return byte_count_;
}

void Md5Hasher::transform(
    const std::span<const std::byte, kMd5BlockSize> block) noexcept
{
    std::array<std::uint32_t, 16U> words{};
    for (std::size_t index = 0U; index < words.size(); ++index) {
        words[index] = load_u32_le(std::span<const std::byte, 4U>{
            block.data() + static_cast<std::ptrdiff_t>(index * 4U), 4U});
    }

    auto a = state_[0U];
    auto b = state_[1U];
    auto c = state_[2U];
    auto d = state_[3U];

    for (std::size_t index = 0U; index < kRoundConstants.size(); ++index) {
        std::uint32_t round_value = 0U;
        std::size_t word_index = 0U;

        if (index < 16U) {
            round_value = (b & c) | (~b & d);
            word_index = index;
        } else if (index < 32U) {
            round_value = (d & b) | (~d & c);
            word_index = (5U * index + 1U) % 16U;
        } else if (index < 48U) {
            round_value = b ^ c ^ d;
            word_index = (3U * index + 5U) % 16U;
        } else {
            round_value = c ^ (b | ~d);
            word_index = (7U * index) % 16U;
        }

        const auto previous_d = d;
        d = c;
        c = b;
        b += std::rotl(
            a + round_value + kRoundConstants[index] + words[word_index],
            static_cast<int>(kRotationAmounts[index]));
        a = previous_d;
    }

    state_[0U] += a;
    state_[1U] += b;
    state_[2U] += c;
    state_[3U] += d;
}

} // namespace hlclient::hash

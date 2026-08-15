#include <hlclient/goldsrc/netchan_payload_transform.hpp>

#include <array>

namespace hlclient::goldsrc {
namespace {

constexpr std::array<std::uint8_t, 16U> kTransformTable{
    0x05U,
    0x61U,
    0x7aU,
    0xedU,
    0x1bU,
    0xcaU,
    0x0dU,
    0x9bU,
    0x4aU,
    0xf1U,
    0x64U,
    0xc7U,
    0xb5U,
    0x8eU,
    0xdfU,
    0xa0U,
};

[[nodiscard]] constexpr std::uint32_t byte_swap_32(const std::uint32_t value) noexcept
{
    return ((value & 0x0000'00ffU) << 24U) | ((value & 0x0000'ff00U) << 8U) |
           ((value & 0x00ff'0000U) >> 8U) | ((value & 0xff00'0000U) >> 24U);
}

[[nodiscard]] constexpr std::uint32_t read_uint32_le(
    const std::span<const std::byte, kNetchanPayloadTransformWordSize> bytes) noexcept
{
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index]))
                 << (index * 8U);
    }
    return value;
}

constexpr void write_uint32_le(
    const std::span<std::byte, kNetchanPayloadTransformWordSize> bytes,
    const std::uint32_t value) noexcept
{
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = std::byte{static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU)};
    }
}

[[nodiscard]] constexpr std::uint8_t transform_mask(
    const std::size_t word_index,
    const std::size_t byte_index) noexcept
{
    const auto shifted_index = static_cast<std::uint32_t>(byte_index) << byte_index;
    const auto table_value = static_cast<std::uint32_t>(
        kTransformTable[((word_index & 15U) + byte_index) & 15U]);
    return static_cast<std::uint8_t>(
        0xa5U | shifted_index | static_cast<std::uint32_t>(byte_index) | table_value);
}

[[nodiscard]] std::uint32_t xor_word_bytes(
    const std::uint32_t value,
    const std::size_t word_index) noexcept
{
    std::uint32_t result = value;
    for (std::size_t byte_index = 0U;
         byte_index < kNetchanPayloadTransformWordSize;
         ++byte_index) {
        const auto shift = byte_index * 8U;
        result ^= static_cast<std::uint32_t>(transform_mask(word_index, byte_index)) << shift;
    }
    return result;
}

void transform_complete_words(
    const std::span<std::byte> payload,
    const NetchanSequence outgoing_sequence,
    const bool encode) noexcept
{
    const auto key = static_cast<std::uint32_t>(
        netchan_payload_transform_key(outgoing_sequence));
    const auto word_count = payload.size() / kNetchanPayloadTransformWordSize;

    for (std::size_t word_index = 0U; word_index < word_count; ++word_index) {
        auto word_bytes = payload.subspan(
            word_index * kNetchanPayloadTransformWordSize,
            kNetchanPayloadTransformWordSize);
        const std::span<std::byte, kNetchanPayloadTransformWordSize> fixed_word{word_bytes};
        std::uint32_t value = read_uint32_le(fixed_word);

        if (encode) {
            value ^= ~key;
            value = byte_swap_32(value);
            value = xor_word_bytes(value, word_index);
            value ^= key;
        } else {
            value ^= key;
            value = xor_word_bytes(value, word_index);
            value = byte_swap_32(value);
            value ^= ~key;
        }

        write_uint32_le(fixed_word, value);
    }
}

} // namespace

std::uint8_t netchan_payload_transform_key(const NetchanSequence outgoing_sequence) noexcept
{
    return static_cast<std::uint8_t>(outgoing_sequence.value() & 0xffU);
}

void encode_netchan_payload(
    const std::span<std::byte> payload,
    const NetchanSequence outgoing_sequence) noexcept
{
    transform_complete_words(payload, outgoing_sequence, true);
}

void decode_netchan_payload(
    const std::span<std::byte> payload,
    const NetchanSequence outgoing_sequence) noexcept
{
    transform_complete_words(payload, outgoing_sequence, false);
}

} // namespace hlclient::goldsrc

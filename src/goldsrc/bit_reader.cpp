#include <hlclient/goldsrc/bit_reader.hpp>

#include <limits>

namespace hlclient::goldsrc {

BitReader::BitReader(
    const std::span<const std::byte> bytes,
    const std::size_t start_bit_offset,
    const std::size_t bit_length) noexcept
    : bytes_{bytes}
{
    if (bytes.size() > std::numeric_limits<std::size_t>::max() / 8U) {
        return;
    }
    const auto available_bits = bytes.size() * 8U;
    if (start_bit_offset > available_bits) {
        return;
    }

    const auto remaining = available_bits - start_bit_offset;
    const auto selected_length = bit_length == static_cast<std::size_t>(-1)
                                     ? remaining
                                     : bit_length;
    if (selected_length > remaining) {
        return;
    }

    bit_offset_ = start_bit_offset;
    bit_limit_ = start_bit_offset + selected_length;
    valid_ = true;
}

bool BitReader::valid() const noexcept
{
    return valid_;
}

std::size_t BitReader::bit_offset() const noexcept
{
    return bit_offset_;
}

std::size_t BitReader::bit_limit() const noexcept
{
    return bit_limit_;
}

std::size_t BitReader::remaining_bits() const noexcept
{
    return valid_ ? bit_limit_ - bit_offset_ : 0U;
}

bool BitReader::byte_aligned() const noexcept
{
    return valid_ && (bit_offset_ & 7U) == 0U;
}

BitReadResult BitReader::read_bits(const std::size_t width) noexcept
{
    if (!valid_) {
        return {0U, BitReaderError::invalid_geometry};
    }
    if (width > kMaximumBitReaderWidth) {
        return {0U, BitReaderError::invalid_width};
    }
    if (width > remaining_bits()) {
        return {0U, BitReaderError::truncated};
    }

    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < width; ++index) {
        const auto position = bit_offset_ + index;
        const auto byte_value = std::to_integer<std::uint8_t>(bytes_[position >> 3U]);
        const auto bit = static_cast<std::uint32_t>(
            (byte_value >> (position & 7U)) & 1U);
        value |= bit << index;
    }
    bit_offset_ += width;
    return {value, BitReaderError::none};
}

BitReaderError BitReader::align_to_byte_zero_padding() noexcept
{
    if (!valid_) {
        return BitReaderError::invalid_geometry;
    }
    const auto padding = (8U - (bit_offset_ & 7U)) & 7U;
    if (padding == 0U) {
        return BitReaderError::none;
    }
    const auto candidate = read_bits(padding);
    if (!candidate) {
        return candidate.error;
    }
    if (candidate.value != 0U) {
        bit_offset_ -= padding;
        return BitReaderError::nonzero_padding;
    }
    return BitReaderError::none;
}

} // namespace hlclient::goldsrc

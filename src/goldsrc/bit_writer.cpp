#include <hlclient/goldsrc/bit_writer.hpp>

#include <limits>

namespace hlclient::goldsrc {

BitWriter::BitWriter(
    const std::span<std::byte> bytes,
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

    initial_bit_offset_ = start_bit_offset;
    bit_offset_ = start_bit_offset;
    bit_limit_ = start_bit_offset + selected_length;
    valid_ = true;
}

bool BitWriter::valid() const noexcept
{
    return valid_;
}

std::size_t BitWriter::initial_bit_offset() const noexcept
{
    return initial_bit_offset_;
}

std::size_t BitWriter::bit_offset() const noexcept
{
    return bit_offset_;
}

std::size_t BitWriter::bit_limit() const noexcept
{
    return bit_limit_;
}

std::size_t BitWriter::written_bits() const noexcept
{
    return valid_ ? bit_offset_ - initial_bit_offset_ : 0U;
}

std::size_t BitWriter::remaining_bits() const noexcept
{
    return valid_ ? bit_limit_ - bit_offset_ : 0U;
}

bool BitWriter::byte_aligned() const noexcept
{
    return valid_ && (bit_offset_ & 7U) == 0U;
}

BitWriteResult BitWriter::write_bits(
    const std::uint32_t value,
    const std::size_t width) noexcept
{
    if (!valid_) {
        return {BitWriterError::invalid_geometry};
    }
    if (width > kMaximumBitWriterWidth) {
        return {BitWriterError::invalid_width};
    }
    if (width > remaining_bits()) {
        return {BitWriterError::exhausted};
    }

    for (std::size_t index = 0U; index < width; ++index) {
        const auto position = bit_offset_ + index;
        const auto byte_index = position >> 3U;
        const auto bit_mask = static_cast<std::uint8_t>(
            std::uint8_t{1U} << (position & 7U));
        auto byte_value = std::to_integer<std::uint8_t>(bytes_[byte_index]);
        if (((value >> index) & 1U) != 0U) {
            byte_value = static_cast<std::uint8_t>(byte_value | bit_mask);
        } else {
            byte_value = static_cast<std::uint8_t>(
                byte_value & static_cast<std::uint8_t>(~bit_mask));
        }
        bytes_[byte_index] = static_cast<std::byte>(byte_value);
    }
    bit_offset_ += width;
    return {BitWriterError::none};
}

BitWriterError BitWriter::align_to_byte_zero_padding() noexcept
{
    if (!valid_) {
        return BitWriterError::invalid_geometry;
    }
    const auto padding = (8U - (bit_offset_ & 7U)) & 7U;
    if (padding > remaining_bits()) {
        return BitWriterError::exhausted;
    }
    return write_bits(0U, padding).error;
}

} // namespace hlclient::goldsrc

#include <hlclient/goldsrc/byte_writer.hpp>

#include <bit>
#include <cstring>
#include <limits>

namespace hlclient::goldsrc {

ByteWriter::ByteWriter(const std::span<std::byte> destination) noexcept
    : destination_{destination}
{
}

std::size_t ByteWriter::position() const noexcept
{
    return position_;
}

std::size_t ByteWriter::remaining() const noexcept
{
    return destination_.size() - position_;
}

std::span<const std::byte> ByteWriter::written_bytes() const noexcept
{
    return destination_.first(position_);
}

bool ByteWriter::write_uint8(const std::uint8_t value) noexcept
{
    const std::byte byte{value};
    return write_bytes(std::span{&byte, std::size_t{1}});
}

bool ByteWriter::write_int8(const std::int8_t value) noexcept
{
    return write_uint8(std::bit_cast<std::uint8_t>(value));
}

bool ByteWriter::write_uint16_le(const std::uint16_t value) noexcept
{
    const std::byte bytes[]{
        std::byte{static_cast<std::uint8_t>(value & 0xffU)},
        std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xffU)},
    };
    return write_bytes(bytes);
}

bool ByteWriter::write_int16_le(const std::int16_t value) noexcept
{
    return write_uint16_le(std::bit_cast<std::uint16_t>(value));
}

bool ByteWriter::write_uint32_le(const std::uint32_t value) noexcept
{
    const std::byte bytes[]{
        std::byte{static_cast<std::uint8_t>(value & 0xffU)},
        std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xffU)},
        std::byte{static_cast<std::uint8_t>((value >> 16U) & 0xffU)},
        std::byte{static_cast<std::uint8_t>((value >> 24U) & 0xffU)},
    };
    return write_bytes(bytes);
}

bool ByteWriter::write_int32_le(const std::int32_t value) noexcept
{
    return write_uint32_le(std::bit_cast<std::uint32_t>(value));
}

bool ByteWriter::write_float32_le(const float value) noexcept
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    return write_uint32_le(std::bit_cast<std::uint32_t>(value));
}

bool ByteWriter::write_bytes(const std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() > remaining()) {
        return false;
    }
    if (bytes.empty()) {
        return true;
    }

    std::memmove(destination_.data() + position_, bytes.data(), bytes.size());
    position_ += bytes.size();
    return true;
}

bool ByteWriter::write_c_string(const std::string_view value) noexcept
{
    if (value.find('\0') != std::string_view::npos || value.size() >= remaining()) {
        return false;
    }

    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    if (!write_bytes(bytes)) {
        return false;
    }
    return write_uint8(0);
}

} // namespace hlclient::goldsrc

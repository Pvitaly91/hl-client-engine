#include <hlclient/goldsrc/byte_reader.hpp>

#include <bit>
#include <limits>

namespace hlclient::goldsrc {

ByteReader::ByteReader(const std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

std::size_t ByteReader::position() const noexcept
{
    return position_;
}

std::size_t ByteReader::remaining() const noexcept
{
    return bytes_.size() - position_;
}

std::optional<std::uint8_t> ByteReader::read_uint8() noexcept
{
    const auto bytes = read_bytes(1);
    if (!bytes) {
        return std::nullopt;
    }
    return std::to_integer<std::uint8_t>((*bytes)[0]);
}

std::optional<std::int8_t> ByteReader::read_int8() noexcept
{
    const auto value = read_uint8();
    return value ? std::optional{std::bit_cast<std::int8_t>(*value)} : std::nullopt;
}

std::optional<std::uint16_t> ByteReader::read_uint16_le() noexcept
{
    const auto bytes = read_bytes(2);
    if (!bytes) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*bytes)[0])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*bytes)[1])) << 8U));
}

std::optional<std::int16_t> ByteReader::read_int16_le() noexcept
{
    const auto value = read_uint16_le();
    return value ? std::optional{std::bit_cast<std::int16_t>(*value)} : std::nullopt;
}

std::optional<std::uint32_t> ByteReader::read_uint32_le() noexcept
{
    const auto bytes = read_bytes(4);
    if (!bytes) {
        return std::nullopt;
    }

    std::uint32_t value = 0;
    for (std::size_t index = 0; index < bytes->size(); ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>((*bytes)[index]))
                 << (index * 8U);
    }
    return value;
}

std::optional<std::int32_t> ByteReader::read_int32_le() noexcept
{
    const auto value = read_uint32_le();
    return value ? std::optional{std::bit_cast<std::int32_t>(*value)} : std::nullopt;
}

std::optional<float> ByteReader::read_float32_le() noexcept
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    const auto value = read_uint32_le();
    return value ? std::optional{std::bit_cast<float>(*value)} : std::nullopt;
}

std::optional<std::span<const std::byte>> ByteReader::read_bytes(const std::size_t count) noexcept
{
    if (count > remaining()) {
        return std::nullopt;
    }

    const auto result = bytes_.subspan(position_, count);
    position_ += count;
    return result;
}

std::optional<std::string> ByteReader::read_c_string()
{
    for (std::size_t index = position_; index < bytes_.size(); ++index) {
        if (bytes_[index] == std::byte{0}) {
            const auto length = index - position_;
            const auto* begin = reinterpret_cast<const char*>(bytes_.data() + position_);
            std::string result{begin, length};
            position_ = index + 1;
            return result;
        }
    }
    return std::nullopt;
}

} // namespace hlclient::goldsrc

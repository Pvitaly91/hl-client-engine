#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hlclient::goldsrc {

class ByteWriter final {
public:
    explicit ByteWriter(std::span<std::byte> destination) noexcept;

    [[nodiscard]] std::size_t position() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] std::span<const std::byte> written_bytes() const noexcept;

    [[nodiscard]] bool write_uint8(std::uint8_t value) noexcept;
    [[nodiscard]] bool write_int8(std::int8_t value) noexcept;
    [[nodiscard]] bool write_uint16_le(std::uint16_t value) noexcept;
    [[nodiscard]] bool write_int16_le(std::int16_t value) noexcept;
    [[nodiscard]] bool write_uint32_le(std::uint32_t value) noexcept;
    [[nodiscard]] bool write_int32_le(std::int32_t value) noexcept;
    [[nodiscard]] bool write_float32_le(float value) noexcept;
    [[nodiscard]] bool write_bytes(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] bool write_c_string(std::string_view value) noexcept;

private:
    std::span<std::byte> destination_;
    std::size_t position_{0};
};

} // namespace hlclient::goldsrc

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace hlclient::goldsrc {

class ByteReader final {
public:
    explicit ByteReader(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] std::size_t position() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;

    [[nodiscard]] std::optional<std::uint8_t> read_uint8() noexcept;
    [[nodiscard]] std::optional<std::int8_t> read_int8() noexcept;
    [[nodiscard]] std::optional<std::uint16_t> read_uint16_le() noexcept;
    [[nodiscard]] std::optional<std::int16_t> read_int16_le() noexcept;
    [[nodiscard]] std::optional<std::uint32_t> read_uint32_le() noexcept;
    [[nodiscard]] std::optional<std::int32_t> read_int32_le() noexcept;
    [[nodiscard]] std::optional<float> read_float32_le() noexcept;
    [[nodiscard]] std::optional<std::span<const std::byte>> read_bytes(std::size_t count) noexcept;
    [[nodiscard]] std::optional<std::string> read_c_string();

private:
    std::span<const std::byte> bytes_;
    std::size_t position_{0};
};

} // namespace hlclient::goldsrc

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace hlclient::goldsrc {

inline constexpr std::size_t kMaximumBitWriterWidth = 32U;

enum class BitWriterError : std::uint8_t {
    none,
    invalid_geometry,
    invalid_width,
    exhausted,
};

struct BitWriteResult {
    BitWriterError error{BitWriterError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == BitWriterError::none;
    }
};

// Writes least-significant-bit first into a caller-owned bounded span. Each
// operation validates its full width before touching the destination; callers
// that require a multi-operation transaction should write into a local staging
// buffer and publish it only after every operation succeeds.
class BitWriter final {
public:
    explicit BitWriter(
        std::span<std::byte> bytes,
        std::size_t start_bit_offset = 0U,
        std::size_t bit_length = static_cast<std::size_t>(-1)) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::size_t initial_bit_offset() const noexcept;
    [[nodiscard]] std::size_t bit_offset() const noexcept;
    [[nodiscard]] std::size_t bit_limit() const noexcept;
    [[nodiscard]] std::size_t written_bits() const noexcept;
    [[nodiscard]] std::size_t remaining_bits() const noexcept;
    [[nodiscard]] bool byte_aligned() const noexcept;

    // Width zero is a successful no-op. Widths above 32 fail. A capacity
    // failure leaves both the cursor and every destination bit unchanged.
    [[nodiscard]] BitWriteResult write_bits(
        std::uint32_t value,
        std::size_t width) noexcept;

    // Writes explicit zero bits through the next byte boundary.
    [[nodiscard]] BitWriterError align_to_byte_zero_padding() noexcept;

private:
    std::span<std::byte> bytes_;
    std::size_t initial_bit_offset_{0U};
    std::size_t bit_offset_{0U};
    std::size_t bit_limit_{0U};
    bool valid_{false};
};

} // namespace hlclient::goldsrc

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace hlclient::goldsrc {

inline constexpr std::size_t kMaximumBitReaderWidth = 32U;

enum class BitReaderError {
    none,
    invalid_geometry,
    invalid_width,
    truncated,
    nonzero_padding,
};

struct BitReadResult {
    std::uint32_t value{0U};
    BitReaderError error{BitReaderError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == BitReaderError::none;
    }
};

// Reads the stock-confirmed GoldSrc least-significant-bit-first bit order.
// Failed operations never advance the cursor and the reader never allocates.
class BitReader final {
public:
    explicit BitReader(
        std::span<const std::byte> bytes,
        std::size_t start_bit_offset = 0U,
        std::size_t bit_length = static_cast<std::size_t>(-1)) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::size_t bit_offset() const noexcept;
    [[nodiscard]] std::size_t bit_limit() const noexcept;
    [[nodiscard]] std::size_t remaining_bits() const noexcept;
    [[nodiscard]] bool byte_aligned() const noexcept;

    // Width zero is a successful no-op returning zero. Widths above 32 fail.
    [[nodiscard]] BitReadResult read_bits(std::size_t width) noexcept;

    // Advances to the next byte only when every skipped padding bit is zero.
    [[nodiscard]] BitReaderError align_to_byte_zero_padding() noexcept;

private:
    std::span<const std::byte> bytes_;
    std::size_t bit_offset_{0U};
    std::size_t bit_limit_{0U};
    bool valid_{false};
};

} // namespace hlclient::goldsrc

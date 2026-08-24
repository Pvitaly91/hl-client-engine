#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::spatial {

inline constexpr std::size_t kGoldSrcPvsDefaultMaximumRowBytes = 1'024U;
inline constexpr std::size_t kGoldSrcPvsHardMaximumRowBytes = 1'024U;
inline constexpr std::size_t kGoldSrcPvsDefaultMaximumDecompressedBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcPvsHardMaximumDecompressedBytes =
    64U * 1024U * 1024U;

struct GoldSrcPvsDecodeLimits {
    std::size_t maximum_row_bytes{kGoldSrcPvsDefaultMaximumRowBytes};
    std::size_t maximum_decompressed_bytes{
        kGoldSrcPvsDefaultMaximumDecompressedBytes};
};

[[nodiscard]] bool valid_goldsrc_pvs_decode_limits(
    const GoldSrcPvsDecodeLimits& limits) noexcept;

enum class GoldSrcPvsDecodeErrorCode {
    invalid_configuration,
    row_byte_limit_exceeded,
    decompressed_byte_limit_exceeded,
    offset_out_of_bounds,
    truncated_stream,
    missing_zero_run_count,
    zero_run_count,
    output_overflow,
    unable_to_retain_row,
};

[[nodiscard]] std::string_view to_string(GoldSrcPvsDecodeErrorCode code) noexcept;

struct GoldSrcPvsDecodeError {
    GoldSrcPvsDecodeErrorCode code{
        GoldSrcPvsDecodeErrorCode::invalid_configuration};
    std::size_t source_offset{0U};
    std::size_t output_byte_count{0U};
};

struct GoldSrcPvsDecodeResult {
    std::optional<std::vector<std::byte>> row;
    std::optional<GoldSrcPvsDecodeError> error;
    std::size_t source_bytes_consumed{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return row.has_value();
    }
};

class GoldSrcPvsDecoder final {
public:
    [[nodiscard]] static GoldSrcPvsDecodeResult decode(
        std::span<const std::byte> visibility_lump,
        std::size_t starting_offset,
        std::size_t exact_row_byte_count,
        const GoldSrcPvsDecodeLimits& limits = {});
};

} // namespace hlclient::goldsrc::spatial

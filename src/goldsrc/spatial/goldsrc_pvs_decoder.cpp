#include <hlclient/goldsrc/spatial/goldsrc_pvs_decoder.hpp>

#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::spatial {
namespace {

[[nodiscard]] GoldSrcPvsDecodeResult failure(
    const GoldSrcPvsDecodeErrorCode code,
    const std::size_t source_offset,
    const std::size_t output_byte_count,
    const std::size_t source_bytes_consumed) noexcept
{
    return GoldSrcPvsDecodeResult{
        std::nullopt,
        GoldSrcPvsDecodeError{code, source_offset, output_byte_count},
        source_bytes_consumed,
    };
}

} // namespace

bool valid_goldsrc_pvs_decode_limits(const GoldSrcPvsDecodeLimits& limits) noexcept
{
    return limits.maximum_row_bytes > 0U &&
        limits.maximum_row_bytes <= kGoldSrcPvsHardMaximumRowBytes &&
        limits.maximum_decompressed_bytes > 0U &&
        limits.maximum_decompressed_bytes <=
            kGoldSrcPvsHardMaximumDecompressedBytes;
}

std::string_view to_string(const GoldSrcPvsDecodeErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcPvsDecodeErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcPvsDecodeErrorCode::row_byte_limit_exceeded:
        return "row_byte_limit_exceeded";
    case GoldSrcPvsDecodeErrorCode::decompressed_byte_limit_exceeded:
        return "decompressed_byte_limit_exceeded";
    case GoldSrcPvsDecodeErrorCode::offset_out_of_bounds:
        return "offset_out_of_bounds";
    case GoldSrcPvsDecodeErrorCode::truncated_stream:
        return "truncated_stream";
    case GoldSrcPvsDecodeErrorCode::missing_zero_run_count:
        return "missing_zero_run_count";
    case GoldSrcPvsDecodeErrorCode::zero_run_count:
        return "zero_run_count";
    case GoldSrcPvsDecodeErrorCode::output_overflow:
        return "output_overflow";
    case GoldSrcPvsDecodeErrorCode::unable_to_retain_row:
        return "unable_to_retain_row";
    }
    return "unknown";
}

GoldSrcPvsDecodeResult GoldSrcPvsDecoder::decode(
    const std::span<const std::byte> visibility_lump,
    const std::size_t starting_offset,
    const std::size_t exact_row_byte_count,
    const GoldSrcPvsDecodeLimits& limits)
{
    try {
    if (!valid_goldsrc_pvs_decode_limits(limits)) {
        return failure(
            GoldSrcPvsDecodeErrorCode::invalid_configuration,
            starting_offset,
            0U,
            0U);
    }
    if (exact_row_byte_count > limits.maximum_row_bytes) {
        return failure(
            GoldSrcPvsDecodeErrorCode::row_byte_limit_exceeded,
            starting_offset,
            0U,
            0U);
    }
    if (exact_row_byte_count > limits.maximum_decompressed_bytes) {
        return failure(
            GoldSrcPvsDecodeErrorCode::decompressed_byte_limit_exceeded,
            starting_offset,
            0U,
            0U);
    }
    if (starting_offset > visibility_lump.size() ||
        (exact_row_byte_count != 0U && starting_offset == visibility_lump.size())) {
        return failure(
            GoldSrcPvsDecodeErrorCode::offset_out_of_bounds,
            starting_offset,
            0U,
            0U);
    }

    std::vector<std::byte> output;
    output.reserve(exact_row_byte_count);
    std::size_t source_position = starting_offset;

    while (output.size() < exact_row_byte_count) {
        if (source_position >= visibility_lump.size()) {
            return failure(
                GoldSrcPvsDecodeErrorCode::truncated_stream,
                source_position,
                output.size(),
                source_position - starting_offset);
        }

        const auto value = std::to_integer<std::uint8_t>(
            visibility_lump[source_position]);
        ++source_position;
        if (value != 0U) {
            output.push_back(static_cast<std::byte>(value));
            continue;
        }

        if (source_position >= visibility_lump.size()) {
            return failure(
                GoldSrcPvsDecodeErrorCode::missing_zero_run_count,
                source_position,
                output.size(),
                source_position - starting_offset);
        }
        const auto run_count = std::to_integer<std::uint8_t>(
            visibility_lump[source_position]);
        ++source_position;
        if (run_count == 0U) {
            return failure(
                GoldSrcPvsDecodeErrorCode::zero_run_count,
                source_position - 1U,
                output.size(),
                source_position - starting_offset);
        }
        const auto remaining = exact_row_byte_count - output.size();
        if (static_cast<std::size_t>(run_count) > remaining) {
            return failure(
                GoldSrcPvsDecodeErrorCode::output_overflow,
                source_position - 1U,
                output.size(),
                source_position - starting_offset);
        }
        output.insert(output.end(), run_count, std::byte{0U});
    }

    return GoldSrcPvsDecodeResult{
        std::move(output),
        std::nullopt,
        source_position - starting_offset,
    };
    } catch (const std::bad_alloc&) {
        return failure(
            GoldSrcPvsDecodeErrorCode::unable_to_retain_row,
            starting_offset,
            0U,
            0U);
    } catch (const std::length_error&) {
        return failure(
            GoldSrcPvsDecodeErrorCode::unable_to_retain_row,
            starting_offset,
            0U,
            0U);
    }
}

} // namespace hlclient::goldsrc::spatial

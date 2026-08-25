#include <hlclient/goldsrc/studio/goldsrc_studio_animation.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::studio {
namespace {

[[nodiscard]] GoldSrcStudioAnimationChannelParseResult failure(
    const GoldSrcStudioAnimationErrorCode error) noexcept
{
    return GoldSrcStudioAnimationChannelParseResult{
        std::nullopt, error, 0U};
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] std::optional<std::int16_t> read_i16_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    if (offset > source.size() || source.size() - offset < 2U) {
        return std::nullopt;
    }
    const auto bits = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(source[offset + 1U]))
            << 8U));
    return std::bit_cast<std::int16_t>(bits);
}

} // namespace

std::string_view to_string(const GoldSrcStudioAnimationErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcStudioAnimationErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcStudioAnimationErrorCode::offset_overflow: return "offset_overflow";
    case GoldSrcStudioAnimationErrorCode::offset_out_of_bounds:
        return "offset_out_of_bounds";
    case GoldSrcStudioAnimationErrorCode::truncated_run: return "truncated_run";
    case GoldSrcStudioAnimationErrorCode::invalid_run: return "invalid_run";
    case GoldSrcStudioAnimationErrorCode::run_limit_exceeded:
        return "run_limit_exceeded";
    case GoldSrcStudioAnimationErrorCode::value_byte_limit_exceeded:
        return "value_byte_limit_exceeded";
    case GoldSrcStudioAnimationErrorCode::frame_coverage_mismatch:
        return "frame_coverage_mismatch";
    case GoldSrcStudioAnimationErrorCode::frame_out_of_range:
        return "frame_out_of_range";
    case GoldSrcStudioAnimationErrorCode::unable_to_retain_animation:
        return "unable_to_retain_animation";
    }
    return "unknown";
}

GoldSrcStudioAnimationChannelParseResult parse_goldsrc_studio_animation_channel(
    const GoldSrcStudioAnimationChannelParseInput& input,
    const GoldSrcStudioAnimationChannelLimits& limits)
{
    if (input.sequence_frame_count == 0U || limits.maximum_runs == 0U ||
        limits.maximum_value_bytes == 0U || !std::isfinite(input.source_default) ||
        !std::isfinite(input.source_scale)) {
        return failure(GoldSrcStudioAnimationErrorCode::invalid_configuration);
    }
    assets::ModelAnimationChannel channel;
    channel.semantic = input.semantic;
    channel.source_default = input.source_default;
    channel.source_scale = input.source_scale;
    channel.source_sequence_group_ordinal = input.source_sequence_group_ordinal;
    if (input.channel_relative_offset == 0U) {
        channel.frame_coverage = input.sequence_frame_count;
        return GoldSrcStudioAnimationChannelParseResult{
            std::move(channel), std::nullopt, 0U};
    }
    std::size_t cursor = 0U;
    if (!checked_add(input.animation_record_offset,
            static_cast<std::size_t>(input.channel_relative_offset), cursor)) {
        return failure(GoldSrcStudioAnimationErrorCode::offset_overflow);
    }
    if (cursor >= input.source.size()) {
        return failure(GoldSrcStudioAnimationErrorCode::offset_out_of_bounds);
    }

    try {
        std::size_t value_bytes = 0U;
        std::uint32_t coverage = 0U;
        while (coverage < input.sequence_frame_count) {
            if (channel.runs.size() >= limits.maximum_runs) {
                return failure(GoldSrcStudioAnimationErrorCode::run_limit_exceeded);
            }
            if (cursor > input.source.size() || input.source.size() - cursor < 2U) {
                return failure(GoldSrcStudioAnimationErrorCode::truncated_run);
            }
            const auto valid = std::to_integer<std::uint8_t>(input.source[cursor]);
            const auto total = std::to_integer<std::uint8_t>(input.source[cursor + 1U]);
            cursor += 2U;
            if (total == 0U || valid == 0U || valid > total) {
                return failure(GoldSrcStudioAnimationErrorCode::invalid_run);
            }
            if (static_cast<std::uint64_t>(coverage) + total >
                std::numeric_limits<std::uint32_t>::max()) {
                return failure(GoldSrcStudioAnimationErrorCode::frame_coverage_mismatch);
            }
            const auto bytes = static_cast<std::size_t>(valid) * 2U;
            if (bytes > input.source.size() - cursor) {
                return failure(GoldSrcStudioAnimationErrorCode::truncated_run);
            }
            if (bytes > limits.maximum_value_bytes -
                            std::min(value_bytes, limits.maximum_value_bytes)) {
                return failure(
                    GoldSrcStudioAnimationErrorCode::value_byte_limit_exceeded);
            }
            assets::ModelAnimationRun run;
            run.first_frame = coverage;
            run.valid_value_count = valid;
            run.total_frame_count = total;
            run.quantized_values.reserve(valid);
            for (std::size_t index = 0U; index < valid; ++index) {
                const auto value = read_i16_le(input.source, cursor + index * 2U);
                if (!value) {
                    return failure(GoldSrcStudioAnimationErrorCode::truncated_run);
                }
                run.quantized_values.push_back(*value);
            }
            cursor += bytes;
            value_bytes += bytes;
            coverage += total;
            channel.runs.push_back(std::move(run));
        }
        if (coverage != input.sequence_frame_count) {
            return failure(GoldSrcStudioAnimationErrorCode::frame_coverage_mismatch);
        }
        channel.frame_coverage = coverage;
        return GoldSrcStudioAnimationChannelParseResult{
            std::move(channel), std::nullopt, value_bytes};
    } catch (const std::bad_alloc&) {
        return failure(GoldSrcStudioAnimationErrorCode::unable_to_retain_animation);
    } catch (const std::length_error&) {
        return failure(GoldSrcStudioAnimationErrorCode::unable_to_retain_animation);
    }
}

std::optional<std::int16_t> StudioAnimationChannelSampler::sample_quantized(
    const assets::ModelAnimationChannel& channel,
    const std::uint32_t integer_frame) noexcept
{
    if (integer_frame >= channel.frame_coverage) {
        return std::nullopt;
    }
    if (channel.runs.empty()) {
        return std::int16_t{0};
    }
    for (const auto& run : channel.runs) {
        if (run.total_frame_count == 0U || run.valid_value_count == 0U ||
            run.valid_value_count > run.total_frame_count ||
            run.quantized_values.size() != run.valid_value_count) {
            return std::nullopt;
        }
        const auto run_end = static_cast<std::uint64_t>(run.first_frame) +
                             run.total_frame_count;
        if (integer_frame < run.first_frame ||
            static_cast<std::uint64_t>(integer_frame) >= run_end) {
            continue;
        }
        const auto local_frame = integer_frame - run.first_frame;
        const auto value_index = std::min<std::size_t>(
            local_frame, static_cast<std::size_t>(run.valid_value_count - 1U));
        return run.quantized_values[value_index];
    }
    return std::nullopt;
}

std::optional<float> StudioAnimationChannelSampler::sample_default_scaled(
    const assets::ModelAnimationChannel& channel,
    const std::uint32_t integer_frame) noexcept
{
    const auto quantized = sample_quantized(channel, integer_frame);
    if (!quantized || !std::isfinite(channel.source_default) ||
        !std::isfinite(channel.source_scale)) {
        return std::nullopt;
    }
    const auto value = channel.source_default +
                       static_cast<float>(*quantized) * channel.source_scale;
    return std::isfinite(value) ? std::optional{value} : std::nullopt;
}

} // namespace hlclient::goldsrc::studio

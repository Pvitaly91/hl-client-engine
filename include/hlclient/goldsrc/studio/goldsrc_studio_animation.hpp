#pragma once

#include <hlclient/assets/model_asset_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::goldsrc::studio {

enum class GoldSrcStudioAnimationErrorCode {
    invalid_configuration,
    offset_overflow,
    offset_out_of_bounds,
    truncated_run,
    invalid_run,
    run_limit_exceeded,
    value_byte_limit_exceeded,
    frame_coverage_mismatch,
    frame_out_of_range,
    unable_to_retain_animation,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcStudioAnimationErrorCode code) noexcept;

struct GoldSrcStudioAnimationChannelLimits {
    std::size_t maximum_runs{65'536U};
    std::size_t maximum_value_bytes{16U * 1024U * 1024U};
};

struct GoldSrcStudioAnimationChannelParseInput {
    std::span<const std::byte> source;
    std::size_t animation_record_offset{0U};
    std::uint16_t channel_relative_offset{0U};
    std::uint32_t sequence_frame_count{0U};
    assets::ModelAnimationChannelSemantic semantic{
        assets::ModelAnimationChannelSemantic::translation_x};
    float source_default{0.0F};
    float source_scale{0.0F};
    std::uint32_t source_sequence_group_ordinal{0U};
};

struct GoldSrcStudioAnimationChannelParseResult {
    std::optional<assets::ModelAnimationChannel> channel;
    std::optional<GoldSrcStudioAnimationErrorCode> error;
    std::size_t source_value_bytes{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return channel.has_value();
    }
};

[[nodiscard]] GoldSrcStudioAnimationChannelParseResult
parse_goldsrc_studio_animation_channel(
    const GoldSrcStudioAnimationChannelParseInput& input,
    const GoldSrcStudioAnimationChannelLimits& limits = {});

class StudioAnimationChannelSampler final {
public:
    [[nodiscard]] static std::optional<std::int16_t> sample_quantized(
        const assets::ModelAnimationChannel& channel,
        std::uint32_t integer_frame) noexcept;

    [[nodiscard]] static std::optional<float> sample_default_scaled(
        const assets::ModelAnimationChannel& channel,
        std::uint32_t integer_frame) noexcept;
};

} // namespace hlclient::goldsrc::studio

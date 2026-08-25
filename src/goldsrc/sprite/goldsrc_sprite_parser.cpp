#include <hlclient/goldsrc/sprite/goldsrc_sprite_parser.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace hlclient::goldsrc::sprite {
namespace {

constexpr std::size_t kRgbaBytesPerPixel = 4U;
constexpr std::size_t kMinimumSpriteSourceBytes =
    kGoldSrcSpriteHeaderWireSize + kGoldSrcSpritePaletteCountWireSize +
    kGoldSrcSpritePaletteRgbByteCount;

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

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] std::uint16_t read_u16_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(source[offset + 1U]))
            << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(source[offset + index]))
                 << static_cast<unsigned int>(index * 8U);
    }
    return value;
}

[[nodiscard]] std::int32_t read_i32_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    return std::bit_cast<std::int32_t>(read_u32_le(source, offset));
}

[[nodiscard]] float read_f32_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    return std::bit_cast<float>(read_u32_le(source, offset));
}

[[nodiscard]] GoldSrcSpriteParseResult fail(
    const GoldSrcSpriteErrorCode code,
    const std::size_t byte_offset,
    std::optional<std::size_t> top_level_entry,
    std::optional<std::size_t> group_frame,
    std::string context)
{
    return GoldSrcSpriteParseResult{
        std::nullopt,
        GoldSrcSpriteError{
            code,
            byte_offset,
            top_level_entry,
            group_frame,
            std::move(context),
        },
    };
}

[[nodiscard]] std::optional<assets::SpriteOrientation> decode_orientation(
    const std::int32_t value) noexcept
{
    switch (value) {
    case 0: return assets::SpriteOrientation::view_parallel_upright;
    case 1: return assets::SpriteOrientation::facing_upright;
    case 2: return assets::SpriteOrientation::view_parallel;
    case 3: return assets::SpriteOrientation::oriented;
    case 4: return assets::SpriteOrientation::view_parallel_oriented;
    default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<assets::SpriteTextureFormat> decode_texture_format(
    const std::int32_t value) noexcept
{
    switch (value) {
    case 0: return assets::SpriteTextureFormat::normal;
    case 1: return assets::SpriteTextureFormat::additive;
    case 2: return assets::SpriteTextureFormat::index_alpha;
    case 3: return assets::SpriteTextureFormat::alpha_test;
    default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<assets::SpriteSyncType> decode_sync_type(
    const std::int32_t value) noexcept
{
    switch (value) {
    case 0: return assets::SpriteSyncType::synchronized;
    case 1: return assets::SpriteSyncType::random;
    default: return std::nullopt;
    }
}

[[nodiscard]] assets::SpriteRgbaEvidenceProfile rgba_profile(
    const assets::SpriteTextureFormat format) noexcept
{
    switch (format) {
    case assets::SpriteTextureFormat::normal:
        return assets::SpriteRgbaEvidenceProfile::normal_opaque;
    case assets::SpriteTextureFormat::additive:
        return assets::SpriteRgbaEvidenceProfile::
            additive_opaque_preview_not_blend_semantics;
    case assets::SpriteTextureFormat::index_alpha:
        return assets::SpriteRgbaEvidenceProfile::
            index_alpha_conversion_evidence_pending;
    case assets::SpriteTextureFormat::alpha_test:
        return assets::SpriteRgbaEvidenceProfile::alpha_test_index_255;
    }
    return assets::SpriteRgbaEvidenceProfile::
        index_alpha_conversion_evidence_pending;
}

class Parser final {
public:
    Parser(
        const std::span<const std::byte> source,
        const GoldSrcSpriteImportLimits& limits) noexcept
        : source_{source}, limits_{limits}
    {
    }

    [[nodiscard]] GoldSrcSpriteParseResult parse()
    {
        auto header_result = parse_header_and_palette();
        if (header_result) {
            return std::move(*header_result);
        }

        try {
            document_.source_data.top_level_entries.reserve(top_level_count_);
            document_.source_data.indexed_frames.reserve(top_level_count_);
            document_.source_data.groups.reserve(top_level_count_);
            document_.compatibility_frames.reserve(top_level_count_);
        } catch (const std::bad_alloc&) {
            return allocation_failure();
        } catch (const std::length_error&) {
            return allocation_failure();
        }

        for (std::size_t entry_index = 0U;
             entry_index < top_level_count_;
             ++entry_index) {
            auto entry_result = parse_top_level_entry(entry_index);
            if (entry_result) {
                return std::move(*entry_result);
            }
        }
        if (position_ != source_.size()) {
            return fail(GoldSrcSpriteErrorCode::unexpected_trailing_data,
                position_,
                std::nullopt,
                std::nullopt,
                "Sprite contains unexplained bytes after its declared top-level entries");
        }

        auto& statistics = document_.source_data.statistics;
        statistics.top_level_entry_count =
            document_.source_data.top_level_entries.size();
        statistics.flattened_frame_count =
            document_.source_data.indexed_frames.size();
        statistics.group_count = document_.source_data.groups.size();
        statistics.indexed_pixel_byte_count = total_indexed_bytes_;
        statistics.derived_rgba_byte_count = total_rgba_payload_bytes_;
        return GoldSrcSpriteParseResult{std::move(document_), std::nullopt};
    }

private:
    [[nodiscard]] GoldSrcSpriteParseResult allocation_failure() const
    {
        return fail(GoldSrcSpriteErrorCode::unable_to_retain_sprite,
            position_,
            current_top_level_entry_,
            current_group_frame_,
            "Unable to retain bounded owning sprite metadata and pixels");
    }

    [[nodiscard]] std::optional<GoldSrcSpriteParseResult> add_metadata_bytes(
        const std::size_t byte_count,
        const std::size_t byte_offset,
        const std::optional<std::size_t> top_level_entry,
        const std::optional<std::size_t> group_frame)
    {
        std::size_t next = 0U;
        if (!checked_add(metadata_bytes_, byte_count, next) ||
            next > limits_.maximum_metadata_bytes) {
            return fail(GoldSrcSpriteErrorCode::metadata_limit_exceeded,
                byte_offset,
                top_level_entry,
                group_frame,
                "Sprite wire metadata exceeds the configured aggregate limit");
        }
        metadata_bytes_ = next;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<GoldSrcSpriteParseResult> parse_header_and_palette()
    {
        if (source_.size() < kGoldSrcSpriteHeaderWireSize) {
            return fail(GoldSrcSpriteErrorCode::source_too_small,
                source_.size(),
                std::nullopt,
                std::nullopt,
                "Sprite source is shorter than the exact 40-byte IDSP v2 header");
        }
        if (read_u32_le(source_, 0U) != kGoldSrcSpriteIdentifier) {
            return fail(GoldSrcSpriteErrorCode::invalid_identifier,
                0U,
                std::nullopt,
                std::nullopt,
                "Sprite identifier is not little-endian IDSP");
        }
        if (read_i32_le(source_, 4U) != kGoldSrcSpriteVersion) {
            return fail(GoldSrcSpriteErrorCode::unsupported_version,
                4U,
                std::nullopt,
                std::nullopt,
                "Only the palette-bearing GoldSrc sprite version 2 profile is supported");
        }

        const auto orientation = decode_orientation(read_i32_le(source_, 8U));
        if (!orientation) {
            return fail(GoldSrcSpriteErrorCode::unsupported_orientation,
                8U,
                std::nullopt,
                std::nullopt,
                "Sprite orientation is outside the five supported GoldSrc categories");
        }
        const auto texture_format =
            decode_texture_format(read_i32_le(source_, 12U));
        if (!texture_format) {
            return fail(GoldSrcSpriteErrorCode::unsupported_texture_format,
                12U,
                std::nullopt,
                std::nullopt,
                "Sprite texture format is outside the supported structural profile");
        }
        const auto bounding_radius = read_f32_le(source_, 16U);
        if (!std::isfinite(bounding_radius) || bounding_radius < 0.0F) {
            return fail(GoldSrcSpriteErrorCode::invalid_bounding_radius,
                16U,
                std::nullopt,
                std::nullopt,
                "Sprite bounding radius must be finite and nonnegative");
        }
        const auto signed_width = read_i32_le(source_, 20U);
        const auto signed_height = read_i32_le(source_, 24U);
        if (signed_width <= 0 || signed_height <= 0 ||
            static_cast<std::uint32_t>(signed_width) > limits_.maximum_width ||
            static_cast<std::uint32_t>(signed_height) > limits_.maximum_height) {
            return fail(GoldSrcSpriteErrorCode::invalid_header_dimensions,
                20U,
                std::nullopt,
                std::nullopt,
                "Sprite maximum dimensions must be positive and within configured limits");
        }
        const auto signed_top_level_count = read_i32_le(source_, 28U);
        if (signed_top_level_count <= 0 ||
            static_cast<std::uint64_t>(signed_top_level_count) >
                static_cast<std::uint64_t>(limits_.maximum_top_level_entries) ||
            static_cast<std::uint64_t>(signed_top_level_count) >
                static_cast<std::uint64_t>(limits_.maximum_flattened_frames)) {
            return fail(GoldSrcSpriteErrorCode::invalid_top_level_count,
                28U,
                std::nullopt,
                std::nullopt,
                "Sprite top-level entry count is nonpositive or exceeds a configured limit");
        }
        const auto beam_length = read_f32_le(source_, 32U);
        if (!std::isfinite(beam_length)) {
            return fail(GoldSrcSpriteErrorCode::invalid_beam_length,
                32U,
                std::nullopt,
                std::nullopt,
                "Sprite beam length must be finite");
        }
        const auto sync_type = decode_sync_type(read_i32_le(source_, 36U));
        if (!sync_type) {
            return fail(GoldSrcSpriteErrorCode::unsupported_sync_type,
                36U,
                std::nullopt,
                std::nullopt,
                "Sprite synchronization type is neither synchronized nor random");
        }

        auto& source_data = document_.source_data;
        source_data.source_version = kGoldSrcSpriteVersion;
        source_data.orientation = *orientation;
        source_data.texture_format = *texture_format;
        source_data.sync_type = *sync_type;
        source_data.bounding_radius = bounding_radius;
        source_data.maximum_width = static_cast<std::uint32_t>(signed_width);
        source_data.maximum_height = static_cast<std::uint32_t>(signed_height);
        source_data.beam_length = beam_length;
        top_level_count_ = static_cast<std::size_t>(signed_top_level_count);
        position_ = kGoldSrcSpriteHeaderWireSize;

        if (source_.size() - position_ < kGoldSrcSpritePaletteCountWireSize) {
            return fail(GoldSrcSpriteErrorCode::missing_palette_count,
                position_,
                std::nullopt,
                std::nullopt,
                "Sprite is truncated before its little-endian palette count");
        }
        const auto palette_count = read_u16_le(source_, position_);
        position_ += kGoldSrcSpritePaletteCountWireSize;
        if (palette_count != kGoldSrcSpritePaletteColorCount) {
            return fail(GoldSrcSpriteErrorCode::unsupported_palette_count,
                kGoldSrcSpriteHeaderWireSize,
                std::nullopt,
                std::nullopt,
                "Supported GoldSrc sprites require exactly 256 palette entries");
        }
        if (source_.size() - position_ < kGoldSrcSpritePaletteRgbByteCount) {
            return fail(GoldSrcSpriteErrorCode::truncated_palette,
                position_,
                std::nullopt,
                std::nullopt,
                "Sprite RGB palette is truncated");
        }
        for (std::size_t index = 0U;
             index < kGoldSrcSpritePaletteColorCount;
             ++index) {
            const auto offset = position_ + index * 3U;
            source_data.palette.colors[index] = assets::SpritePaletteColor{
                std::to_integer<std::uint8_t>(source_[offset]),
                std::to_integer<std::uint8_t>(source_[offset + 1U]),
                std::to_integer<std::uint8_t>(source_[offset + 2U]),
            };
        }
        position_ += kGoldSrcSpritePaletteRgbByteCount;
        metadata_bytes_ = kMinimumSpriteSourceBytes;
        if (metadata_bytes_ > limits_.maximum_metadata_bytes) {
            return fail(GoldSrcSpriteErrorCode::metadata_limit_exceeded,
                0U,
                std::nullopt,
                std::nullopt,
                "Sprite header and palette exceed the configured metadata limit");
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<GoldSrcSpriteParseResult> parse_top_level_entry(
        const std::size_t entry_index)
    {
        current_top_level_entry_ = entry_index;
        current_group_frame_.reset();
        if (source_.size() - position_ < kGoldSrcSpriteFrameTypeWireSize) {
            return fail(GoldSrcSpriteErrorCode::invalid_frame_type,
                position_,
                entry_index,
                std::nullopt,
                "Sprite is truncated before a top-level frame type");
        }
        const auto type_offset = position_;
        const auto frame_type = read_i32_le(source_, position_);
        position_ += kGoldSrcSpriteFrameTypeWireSize;
        if (auto result = add_metadata_bytes(kGoldSrcSpriteFrameTypeWireSize,
                type_offset,
                entry_index,
                std::nullopt)) {
            return result;
        }

        const auto first_flattened = document_.source_data.indexed_frames.size();
        if (frame_type == 0) {
            if (auto result = parse_frame(
                    entry_index, std::nullopt, std::nullopt, 0.0F)) {
                return result;
            }
            document_.source_data.top_level_entries.push_back(
                assets::SpriteTopLevelEntry{
                    assets::SpriteTopLevelEntryKind::single,
                    static_cast<std::uint32_t>(entry_index),
                    static_cast<std::uint32_t>(first_flattened),
                    1U,
                    std::nullopt,
                });
            return std::nullopt;
        }
        if (frame_type != 1) {
            return fail(GoldSrcSpriteErrorCode::invalid_frame_type,
                type_offset,
                entry_index,
                std::nullopt,
                "Sprite top-level frame type is neither single nor group");
        }
        return parse_group(entry_index, first_flattened);
    }

    [[nodiscard]] std::optional<GoldSrcSpriteParseResult> parse_group(
        const std::size_t entry_index,
        const std::size_t first_flattened)
    {
        if (source_.size() - position_ < kGoldSrcSpriteGroupHeaderWireSize) {
            return fail(GoldSrcSpriteErrorCode::invalid_group_frame_count,
                position_,
                entry_index,
                std::nullopt,
                "Sprite group is truncated before its frame count");
        }
        const auto count_offset = position_;
        const auto signed_count = read_i32_le(source_, position_);
        position_ += kGoldSrcSpriteGroupHeaderWireSize;
        if (auto result = add_metadata_bytes(kGoldSrcSpriteGroupHeaderWireSize,
                count_offset,
                entry_index,
                std::nullopt)) {
            return result;
        }
        if (signed_count <= 0 ||
            static_cast<std::uint64_t>(signed_count) >
                static_cast<std::uint64_t>(limits_.maximum_group_frames)) {
            return fail(GoldSrcSpriteErrorCode::invalid_group_frame_count,
                count_offset,
                entry_index,
                std::nullopt,
                "Sprite group frame count is nonpositive or exceeds its configured limit");
        }
        const auto group_count = static_cast<std::size_t>(signed_count);
        std::size_t flattened_after_group = 0U;
        if (!checked_add(document_.source_data.indexed_frames.size(),
                group_count,
                flattened_after_group) ||
            flattened_after_group > limits_.maximum_flattened_frames) {
            return fail(GoldSrcSpriteErrorCode::flattened_frame_limit_exceeded,
                count_offset,
                entry_index,
                std::nullopt,
                "Flattened sprite frame count exceeds the configured aggregate limit");
        }
        std::size_t interval_bytes = 0U;
        if (!checked_multiply(group_count,
                kGoldSrcSpriteGroupIntervalWireSize,
                interval_bytes) ||
            source_.size() - position_ < interval_bytes) {
            return fail(GoldSrcSpriteErrorCode::truncated_group_intervals,
                position_,
                entry_index,
                std::nullopt,
                "Sprite group cumulative interval array is truncated or overflows");
        }
        if (auto result = add_metadata_bytes(
                interval_bytes, position_, entry_index, std::nullopt)) {
            return result;
        }

        assets::SpriteFrameGroup group;
        group.source_top_level_entry = static_cast<std::uint32_t>(entry_index);
        group.source_group_ordinal =
            static_cast<std::uint32_t>(document_.source_data.groups.size());
        try {
            group.cumulative_intervals_seconds.reserve(group_count);
            group.frame_durations_seconds.reserve(group_count);
            group.flattened_frame_indices.reserve(group_count);
        } catch (const std::bad_alloc&) {
            return allocation_failure();
        } catch (const std::length_error&) {
            return allocation_failure();
        }

        float previous = 0.0F;
        for (std::size_t frame_index = 0U; frame_index < group_count; ++frame_index) {
            const auto interval_offset =
                position_ + frame_index * kGoldSrcSpriteGroupIntervalWireSize;
            const auto cumulative = read_f32_le(source_, interval_offset);
            if (!std::isfinite(cumulative) || cumulative <= 0.0F ||
                cumulative <= previous) {
                return fail(GoldSrcSpriteErrorCode::invalid_group_interval,
                    interval_offset,
                    entry_index,
                    frame_index,
                    "Sprite group intervals must be finite, positive, and strictly increasing");
            }
            const auto duration = cumulative - previous;
            if (!std::isfinite(duration) || duration <= 0.0F ||
                cumulative > limits_.maximum_duration_seconds ||
                duration > limits_.maximum_duration_seconds) {
                return fail(GoldSrcSpriteErrorCode::duration_limit_exceeded,
                    interval_offset,
                    entry_index,
                    frame_index,
                    "Sprite group cumulative or per-frame duration exceeds its configured limit");
            }
            group.cumulative_intervals_seconds.push_back(cumulative);
            group.frame_durations_seconds.push_back(duration);
            previous = cumulative;
        }
        position_ += interval_bytes;

        const auto group_ordinal = group.source_group_ordinal;
        for (std::size_t frame_index = 0U; frame_index < group_count; ++frame_index) {
            current_group_frame_ = frame_index;
            if (auto result = parse_frame(entry_index,
                    group_ordinal,
                    static_cast<std::uint32_t>(frame_index),
                    group.frame_durations_seconds[frame_index])) {
                return result;
            }
            group.flattened_frame_indices.push_back(static_cast<std::uint32_t>(
                first_flattened + frame_index));
        }
        document_.source_data.groups.push_back(std::move(group));
        document_.source_data.top_level_entries.push_back(
            assets::SpriteTopLevelEntry{
                assets::SpriteTopLevelEntryKind::group,
                static_cast<std::uint32_t>(entry_index),
                static_cast<std::uint32_t>(first_flattened),
                static_cast<std::uint32_t>(group_count),
                group_ordinal,
            });
        return std::nullopt;
    }

    [[nodiscard]] std::optional<GoldSrcSpriteParseResult> parse_frame(
        const std::size_t top_level_entry,
        const std::optional<std::uint32_t> group_ordinal,
        const std::optional<std::uint32_t> group_frame_ordinal,
        const float duration_seconds)
    {
        if (source_.size() - position_ < kGoldSrcSpriteFrameHeaderWireSize) {
            return fail(GoldSrcSpriteErrorCode::truncated_frame_header,
                position_,
                top_level_entry,
                current_group_frame_,
                "Sprite frame is truncated before its exact 16-byte header");
        }
        const auto frame_offset = position_;
        const auto origin_x = read_i32_le(source_, position_);
        const auto origin_y = read_i32_le(source_, position_ + 4U);
        const auto signed_width = read_i32_le(source_, position_ + 8U);
        const auto signed_height = read_i32_le(source_, position_ + 12U);
        position_ += kGoldSrcSpriteFrameHeaderWireSize;
        if (auto result = add_metadata_bytes(kGoldSrcSpriteFrameHeaderWireSize,
                frame_offset,
                top_level_entry,
                current_group_frame_)) {
            return result;
        }

        if (origin_x < -limits_.maximum_origin_magnitude ||
            origin_x > limits_.maximum_origin_magnitude ||
            origin_y < -limits_.maximum_origin_magnitude ||
            origin_y > limits_.maximum_origin_magnitude) {
            return fail(GoldSrcSpriteErrorCode::invalid_frame_origin,
                frame_offset,
                top_level_entry,
                current_group_frame_,
                "Sprite frame origin exceeds the configured signed magnitude");
        }
        if (signed_width <= 0 || signed_height <= 0 ||
            static_cast<std::uint32_t>(signed_width) > limits_.maximum_width ||
            static_cast<std::uint32_t>(signed_height) > limits_.maximum_height) {
            return fail(GoldSrcSpriteErrorCode::invalid_frame_dimensions,
                frame_offset + 8U,
                top_level_entry,
                current_group_frame_,
                "Sprite frame dimensions must be positive and within configured limits");
        }
        const auto width = static_cast<std::uint32_t>(signed_width);
        const auto height = static_cast<std::uint32_t>(signed_height);
        if (width > document_.source_data.maximum_width ||
            height > document_.source_data.maximum_height) {
            return fail(GoldSrcSpriteErrorCode::header_frame_dimensions_too_small,
                frame_offset + 8U,
                top_level_entry,
                current_group_frame_,
                "Sprite header maximum dimensions do not cover every frame");
        }

        const auto pixel_count_u64 = static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height);
        if (pixel_count_u64 > limits_.maximum_pixels_per_frame ||
            pixel_count_u64 >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
            return fail(GoldSrcSpriteErrorCode::frame_pixel_limit_exceeded,
                frame_offset + 8U,
                top_level_entry,
                current_group_frame_,
                "Sprite frame pixel count exceeds a configured or host limit");
        }
        const auto pixel_count = static_cast<std::size_t>(pixel_count_u64);
        std::size_t pixel_end = 0U;
        if (!checked_add(position_, pixel_count, pixel_end)) {
            return fail(GoldSrcSpriteErrorCode::frame_pixel_range_overflow,
                position_,
                top_level_entry,
                current_group_frame_,
                "Sprite frame indexed-pixel range overflows the host size type");
        }
        if (pixel_end > source_.size()) {
            return fail(GoldSrcSpriteErrorCode::truncated_frame_pixels,
                position_,
                top_level_entry,
                current_group_frame_,
                "Sprite frame indexed pixels extend beyond the source");
        }

        std::size_t next_indexed_total = 0U;
        if (!checked_add(total_indexed_bytes_, pixel_count, next_indexed_total) ||
            next_indexed_total > limits_.maximum_total_indexed_bytes) {
            return fail(GoldSrcSpriteErrorCode::total_indexed_limit_exceeded,
                position_,
                top_level_entry,
                current_group_frame_,
                "Sprite aggregate indexed pixels exceed the configured limit");
        }
        const auto profile = rgba_profile(document_.source_data.texture_format);
        const auto derives_rgba =
            profile != assets::SpriteRgbaEvidenceProfile::
                index_alpha_conversion_evidence_pending;
        std::size_t rgba_byte_count = 0U;
        std::size_t retained_rgba_byte_count = 0U;
        std::size_t next_rgba_payload_total = total_rgba_payload_bytes_;
        std::size_t next_retained_rgba_total = total_retained_rgba_bytes_;
        if (derives_rgba &&
            (!checked_multiply(pixel_count, kRgbaBytesPerPixel, rgba_byte_count) ||
                !checked_multiply(
                    rgba_byte_count, 2U, retained_rgba_byte_count) ||
                !checked_add(total_rgba_payload_bytes_, rgba_byte_count,
                    next_rgba_payload_total) ||
                !checked_add(total_retained_rgba_bytes_,
                    retained_rgba_byte_count, next_retained_rgba_total) ||
                next_retained_rgba_total > limits_.maximum_total_rgba_bytes)) {
            return fail(GoldSrcSpriteErrorCode::total_rgba_limit_exceeded,
                position_,
                top_level_entry,
                current_group_frame_,
                "Sprite aggregate retained RGBA copies exceed the configured limit");
        }

        assets::SpriteIndexedFrame frame;
        frame.origin = assets::SpriteFrameOrigin{origin_x, origin_y};
        frame.width = width;
        frame.height = height;
        frame.source_top_level_entry =
            static_cast<std::uint32_t>(top_level_entry);
        frame.source_group_ordinal = group_ordinal;
        frame.source_group_frame_ordinal = group_frame_ordinal;
        frame.rgba_evidence = profile;
        try {
            frame.indexed_pixels.assign(
                source_.begin() + static_cast<std::ptrdiff_t>(position_),
                source_.begin() + static_cast<std::ptrdiff_t>(pixel_end));
            if (derives_rgba) {
                frame.derived_rgba8.resize(rgba_byte_count);
                for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
                    const auto palette_index = std::to_integer<std::uint8_t>(
                        frame.indexed_pixels[pixel]);
                    const auto& color =
                        document_.source_data.palette.colors[palette_index];
                    const auto output = pixel * kRgbaBytesPerPixel;
                    frame.derived_rgba8[output] = std::byte{color.red};
                    frame.derived_rgba8[output + 1U] = std::byte{color.green};
                    frame.derived_rgba8[output + 2U] = std::byte{color.blue};
                    frame.derived_rgba8[output + 3U] =
                        document_.source_data.texture_format ==
                                    assets::SpriteTextureFormat::alpha_test &&
                                palette_index == 255U
                            ? std::byte{0}
                            : std::byte{0xFF};
                }
            }

            assets::SpriteFrame compatibility;
            compatibility.image.width = width;
            compatibility.image.height = height;
            compatibility.image.pixel_format = assets::ImagePixelFormat::rgba8;
            compatibility.image.pixels = frame.derived_rgba8;
            compatibility.duration_seconds = duration_seconds;
            document_.source_data.indexed_frames.push_back(std::move(frame));
            document_.compatibility_frames.push_back(std::move(compatibility));
        } catch (const std::bad_alloc&) {
            return allocation_failure();
        } catch (const std::length_error&) {
            return allocation_failure();
        }

        position_ = pixel_end;
        total_indexed_bytes_ = next_indexed_total;
        total_rgba_payload_bytes_ = next_rgba_payload_total;
        total_retained_rgba_bytes_ = next_retained_rgba_total;
        return std::nullopt;
    }

    std::span<const std::byte> source_;
    const GoldSrcSpriteImportLimits& limits_;
    GoldSrcSpriteParsedDocument document_;
    std::size_t position_{0U};
    std::size_t top_level_count_{0U};
    std::size_t metadata_bytes_{0U};
    std::size_t total_indexed_bytes_{0U};
    // A derived RGBA payload is retained both in authoritative source metadata
    // and in the additive pre-M4.5.2 compatibility frame. Keep the public
    // payload statistic distinct, but enforce the configured byte cap against
    // both owning allocations before either one is made.
    std::size_t total_rgba_payload_bytes_{0U};
    std::size_t total_retained_rgba_bytes_{0U};
    std::optional<std::size_t> current_top_level_entry_;
    std::optional<std::size_t> current_group_frame_;
};

} // namespace

bool valid_goldsrc_sprite_import_limits(
    const GoldSrcSpriteImportLimits& limits) noexcept
{
    return limits.maximum_source_bytes >= kMinimumSpriteSourceBytes &&
           limits.maximum_source_bytes <= kGoldSrcSpriteHardMaximumSourceBytes &&
           limits.maximum_top_level_entries > 0U &&
           limits.maximum_top_level_entries <=
               kGoldSrcSpriteHardMaximumTopLevelEntries &&
           limits.maximum_flattened_frames > 0U &&
           limits.maximum_flattened_frames <=
               kGoldSrcSpriteHardMaximumFlattenedFrames &&
           limits.maximum_top_level_entries <= limits.maximum_flattened_frames &&
           limits.maximum_group_frames > 0U &&
           limits.maximum_group_frames <= kGoldSrcSpriteHardMaximumGroupFrames &&
           limits.maximum_group_frames <= limits.maximum_flattened_frames &&
           limits.maximum_width > 0U &&
           limits.maximum_width <= kGoldSrcSpriteHardMaximumDimension &&
           limits.maximum_height > 0U &&
           limits.maximum_height <= kGoldSrcSpriteHardMaximumDimension &&
           limits.maximum_pixels_per_frame > 0U &&
           limits.maximum_pixels_per_frame <=
               kGoldSrcSpriteHardMaximumPixelsPerFrame &&
           limits.maximum_total_indexed_bytes > 0U &&
           limits.maximum_total_indexed_bytes <=
               kGoldSrcSpriteHardMaximumTotalIndexedBytes &&
           limits.maximum_total_rgba_bytes > 0U &&
           limits.maximum_total_rgba_bytes <=
               kGoldSrcSpriteHardMaximumTotalRgbaBytes &&
           std::isfinite(limits.maximum_duration_seconds) &&
           limits.maximum_duration_seconds > 0.0F &&
           limits.maximum_duration_seconds <=
               kGoldSrcSpriteHardMaximumDurationSeconds &&
           limits.maximum_metadata_bytes >= kMinimumSpriteSourceBytes &&
           limits.maximum_metadata_bytes <=
               kGoldSrcSpriteHardMaximumMetadataBytes &&
           limits.maximum_origin_magnitude > 0 &&
           limits.maximum_origin_magnitude <=
               kGoldSrcSpriteHardMaximumOriginMagnitude;
}

std::string_view to_string(const GoldSrcSpriteErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcSpriteErrorCode::invalid_configuration: return "invalid_configuration";
    case GoldSrcSpriteErrorCode::source_limit_exceeded: return "source_limit_exceeded";
    case GoldSrcSpriteErrorCode::source_too_small: return "source_too_small";
    case GoldSrcSpriteErrorCode::invalid_identifier: return "invalid_identifier";
    case GoldSrcSpriteErrorCode::unsupported_version: return "unsupported_version";
    case GoldSrcSpriteErrorCode::unsupported_orientation:
        return "unsupported_orientation";
    case GoldSrcSpriteErrorCode::unsupported_texture_format:
        return "unsupported_texture_format";
    case GoldSrcSpriteErrorCode::invalid_bounding_radius:
        return "invalid_bounding_radius";
    case GoldSrcSpriteErrorCode::invalid_header_dimensions:
        return "invalid_header_dimensions";
    case GoldSrcSpriteErrorCode::invalid_top_level_count:
        return "invalid_top_level_count";
    case GoldSrcSpriteErrorCode::invalid_beam_length: return "invalid_beam_length";
    case GoldSrcSpriteErrorCode::unsupported_sync_type: return "unsupported_sync_type";
    case GoldSrcSpriteErrorCode::metadata_limit_exceeded:
        return "metadata_limit_exceeded";
    case GoldSrcSpriteErrorCode::missing_palette_count: return "missing_palette_count";
    case GoldSrcSpriteErrorCode::unsupported_palette_count:
        return "unsupported_palette_count";
    case GoldSrcSpriteErrorCode::truncated_palette: return "truncated_palette";
    case GoldSrcSpriteErrorCode::invalid_frame_type: return "invalid_frame_type";
    case GoldSrcSpriteErrorCode::invalid_group_frame_count:
        return "invalid_group_frame_count";
    case GoldSrcSpriteErrorCode::flattened_frame_limit_exceeded:
        return "flattened_frame_limit_exceeded";
    case GoldSrcSpriteErrorCode::truncated_group_intervals:
        return "truncated_group_intervals";
    case GoldSrcSpriteErrorCode::invalid_group_interval:
        return "invalid_group_interval";
    case GoldSrcSpriteErrorCode::duration_limit_exceeded:
        return "duration_limit_exceeded";
    case GoldSrcSpriteErrorCode::truncated_frame_header:
        return "truncated_frame_header";
    case GoldSrcSpriteErrorCode::invalid_frame_origin: return "invalid_frame_origin";
    case GoldSrcSpriteErrorCode::invalid_frame_dimensions:
        return "invalid_frame_dimensions";
    case GoldSrcSpriteErrorCode::frame_pixel_limit_exceeded:
        return "frame_pixel_limit_exceeded";
    case GoldSrcSpriteErrorCode::frame_pixel_range_overflow:
        return "frame_pixel_range_overflow";
    case GoldSrcSpriteErrorCode::truncated_frame_pixels:
        return "truncated_frame_pixels";
    case GoldSrcSpriteErrorCode::total_indexed_limit_exceeded:
        return "total_indexed_limit_exceeded";
    case GoldSrcSpriteErrorCode::total_rgba_limit_exceeded:
        return "total_rgba_limit_exceeded";
    case GoldSrcSpriteErrorCode::header_frame_dimensions_too_small:
        return "header_frame_dimensions_too_small";
    case GoldSrcSpriteErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    case GoldSrcSpriteErrorCode::unable_to_retain_sprite:
        return "unable_to_retain_sprite";
    }
    return "unknown";
}

GoldSrcSpriteParseResult GoldSrcSpriteParser::parse(
    const std::span<const std::byte> source,
    const GoldSrcSpriteImportLimits& limits)
{
    if (!valid_goldsrc_sprite_import_limits(limits)) {
        return fail(GoldSrcSpriteErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            std::nullopt,
            "Sprite import limits are outside the supported bounded profile");
    }
    if (source.size() > limits.maximum_source_bytes) {
        return fail(GoldSrcSpriteErrorCode::source_limit_exceeded,
            limits.maximum_source_bytes,
            std::nullopt,
            std::nullopt,
            "Sprite source exceeds the configured byte limit");
    }
    try {
        return Parser{source, limits}.parse();
    } catch (const std::bad_alloc&) {
        return fail(GoldSrcSpriteErrorCode::unable_to_retain_sprite,
            0U,
            std::nullopt,
            std::nullopt,
            "Unable to retain bounded owning sprite metadata and pixels");
    } catch (const std::length_error&) {
        return fail(GoldSrcSpriteErrorCode::unable_to_retain_sprite,
            0U,
            std::nullopt,
            std::nullopt,
            "Sprite output exceeds an owning container limit");
    }
}

} // namespace hlclient::goldsrc::sprite

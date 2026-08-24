#include <hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::indexed_texture {
namespace {

inline constexpr std::uint32_t kHardMaximumDimension = 16'384U;
inline constexpr std::uint64_t kHardMaximumLevelZeroTexels = 268'435'456ULL;
inline constexpr std::size_t kHardMaximumDecodedBytes = 256U * 1024U * 1024U;
inline constexpr std::size_t kRgbaBytesPerTexel = 4U;

[[nodiscard]] std::uint16_t read_u16_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes[offset + 1U]))
            << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    std::uint32_t result = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[offset + index]))
            << static_cast<unsigned int>(index * 8U);
    }
    return result;
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

[[nodiscard]] GoldSrcMiptexParseResult parse_fail(
    const GoldSrcMiptexErrorCode code,
    const std::size_t offset,
    const std::optional<std::size_t> level,
    std::string context)
{
    return GoldSrcMiptexParseResult{
        std::nullopt,
        GoldSrcMiptexError{code, offset, level, std::move(context)},
    };
}

[[nodiscard]] GoldSrcIndexedTextureDecodeStartResult begin_fail(
    GoldSrcMiptexError error)
{
    return GoldSrcIndexedTextureDecodeStartResult{
        std::nullopt,
        std::move(error),
    };
}

[[nodiscard]] char uppercase_ascii(const char value) noexcept
{
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - 'a' + 'A');
    }
    return value;
}

} // namespace

bool valid_goldsrc_indexed_texture_limits(
    const GoldSrcIndexedTextureLimits& limits) noexcept
{
    return limits.maximum_dimension > 0U &&
        limits.maximum_dimension <= kHardMaximumDimension &&
        limits.maximum_level_zero_texels > 0U &&
        limits.maximum_level_zero_texels <= kHardMaximumLevelZeroTexels &&
        limits.maximum_decoded_rgba_bytes > 0U &&
        limits.maximum_decoded_rgba_bytes <= kHardMaximumDecodedBytes &&
        limits.maximum_trailing_zero_fill_bytes <= 3U;
}

std::string_view to_string(const GoldSrcMiptexErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcMiptexErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcMiptexErrorCode::source_too_small: return "source_too_small";
    case GoldSrcMiptexErrorCode::invalid_name: return "invalid_name";
    case GoldSrcMiptexErrorCode::invalid_dimensions: return "invalid_dimensions";
    case GoldSrcMiptexErrorCode::texture_limit_exceeded:
        return "texture_limit_exceeded";
    case GoldSrcMiptexErrorCode::mixed_mip_offsets: return "mixed_mip_offsets";
    case GoldSrcMiptexErrorCode::external_reference_not_allowed:
        return "external_reference_not_allowed";
    case GoldSrcMiptexErrorCode::mip_offset_before_header:
        return "mip_offset_before_header";
    case GoldSrcMiptexErrorCode::mip_range_overflow: return "mip_range_overflow";
    case GoldSrcMiptexErrorCode::mip_range_out_of_bounds:
        return "mip_range_out_of_bounds";
    case GoldSrcMiptexErrorCode::overlapping_mip_ranges:
        return "overlapping_mip_ranges";
    case GoldSrcMiptexErrorCode::missing_palette_count:
        return "missing_palette_count";
    case GoldSrcMiptexErrorCode::unsupported_palette_count:
        return "unsupported_palette_count";
    case GoldSrcMiptexErrorCode::palette_range_out_of_bounds:
        return "palette_range_out_of_bounds";
    case GoldSrcMiptexErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    case GoldSrcMiptexErrorCode::decoded_size_limit_exceeded:
        return "decoded_size_limit_exceeded";
    case GoldSrcMiptexErrorCode::invalid_update_budget:
        return "invalid_update_budget";
    case GoldSrcMiptexErrorCode::unable_to_retain_texture:
        return "unable_to_retain_texture";
    }
    return "unknown";
}

GoldSrcMiptexParseResult GoldSrcMiptexParser::parse(
    const std::span<const std::byte> record,
    const GoldSrcMiptexSourceProfile source_profile,
    const GoldSrcIndexedTextureLimits& limits)
{
    if (!valid_goldsrc_indexed_texture_limits(limits)) {
        return parse_fail(GoldSrcMiptexErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "Indexed texture limits are outside the supported bounded profile");
    }
    if (record.size() < kGoldSrcMiptexHeaderWireSize) {
        return parse_fail(GoldSrcMiptexErrorCode::source_too_small,
            record.size(),
            std::nullopt,
            "Miptex record is shorter than its exact 40-byte wire header");
    }

    GoldSrcParsedMiptex parsed;
    std::size_t name_length = 0U;
    while (name_length < kGoldSrcMiptexNameWireSize &&
           record[name_length] != std::byte{0}) {
        const auto character = std::to_integer<std::uint8_t>(record[name_length]);
        if (character < 0x20U || character > 0x7EU) {
            return parse_fail(GoldSrcMiptexErrorCode::invalid_name,
                name_length,
                std::nullopt,
                "Miptex names support printable ASCII bytes only");
        }
        ++name_length;
    }
    if (name_length == 0U) {
        return parse_fail(GoldSrcMiptexErrorCode::invalid_name,
            0U,
            std::nullopt,
            "Miptex name must not be empty");
    }
    try {
        parsed.name.reserve(name_length);
        parsed.normalized_name.reserve(name_length);
        for (std::size_t index = 0U; index < name_length; ++index) {
            const auto character = static_cast<char>(
                std::to_integer<unsigned char>(record[index]));
            parsed.name.push_back(character);
            parsed.normalized_name.push_back(uppercase_ascii(character));
        }
    } catch (const std::bad_alloc&) {
        return parse_fail(GoldSrcMiptexErrorCode::unable_to_retain_texture,
            0U,
            std::nullopt,
            "Unable to retain bounded miptex name metadata");
    } catch (const std::length_error&) {
        return parse_fail(GoldSrcMiptexErrorCode::unable_to_retain_texture,
            0U,
            std::nullopt,
            "Miptex name exceeds an owning container limit");
    }

    parsed.width = read_u32_le(record, 16U);
    parsed.height = read_u32_le(record, 20U);
    if (parsed.width == 0U || parsed.height == 0U ||
        parsed.width % 16U != 0U || parsed.height % 16U != 0U) {
        return parse_fail(GoldSrcMiptexErrorCode::invalid_dimensions,
            16U,
            std::nullopt,
            "Miptex dimensions must be positive multiples of 16");
    }
    if (parsed.width > limits.maximum_dimension ||
        parsed.height > limits.maximum_dimension) {
        return parse_fail(GoldSrcMiptexErrorCode::texture_limit_exceeded,
            16U,
            std::nullopt,
            "Miptex dimensions exceed the configured limit");
    }
    const auto level_zero_texels = static_cast<std::uint64_t>(parsed.width) *
        static_cast<std::uint64_t>(parsed.height);
    if (level_zero_texels > limits.maximum_level_zero_texels) {
        return parse_fail(GoldSrcMiptexErrorCode::texture_limit_exceeded,
            16U,
            std::nullopt,
            "Miptex level-zero texels exceed the configured limit");
    }

    std::array<std::uint32_t, kGoldSrcMiptexLevelCount> offsets{};
    std::size_t zero_count = 0U;
    for (std::size_t level = 0U; level < offsets.size(); ++level) {
        offsets[level] = read_u32_le(record, 24U + level * 4U);
        zero_count += offsets[level] == 0U ? 1U : 0U;
        const auto width = parsed.width >> level;
        const auto height = parsed.height >> level;
        if (width == 0U || height == 0U) {
            return parse_fail(GoldSrcMiptexErrorCode::invalid_dimensions,
                16U,
                level,
                "Every retained source mip level must have positive dimensions");
        }
        parsed.mip_levels[level].width = width;
        parsed.mip_levels[level].height = height;
    }

    if (zero_count != 0U && zero_count != offsets.size()) {
        return parse_fail(GoldSrcMiptexErrorCode::mixed_mip_offsets,
            24U,
            std::nullopt,
            "Mip offsets must be either all zero or all nonzero");
    }
    if (zero_count == offsets.size()) {
        if (source_profile == GoldSrcMiptexSourceProfile::wad3_lump) {
            return parse_fail(GoldSrcMiptexErrorCode::external_reference_not_allowed,
                24U,
                std::nullopt,
                "A WAD3 miptex lump must contain all four indexed mip levels");
        }
        const auto trailing = record.subspan(kGoldSrcMiptexHeaderWireSize);
        if (trailing.size() > limits.maximum_trailing_zero_fill_bytes ||
            !std::all_of(trailing.begin(), trailing.end(), [](const std::byte value) {
                return value == std::byte{0};
            })) {
            return parse_fail(GoldSrcMiptexErrorCode::unexpected_trailing_data,
                kGoldSrcMiptexHeaderWireSize,
                std::nullopt,
                "External-reference miptex contains unexplained trailing bytes");
        }
        parsed.storage_profile = GoldSrcMiptexStorageProfile::external_reference;
        parsed.bytes_consumed = record.size();
        return GoldSrcMiptexParseResult{std::move(parsed), std::nullopt};
    }

    parsed.storage_profile = GoldSrcMiptexStorageProfile::indexed_pixels;
    std::size_t previous_end = kGoldSrcMiptexHeaderWireSize;
    for (std::size_t level = 0U; level < offsets.size(); ++level) {
        auto& range = parsed.mip_levels[level];
        range.byte_offset = static_cast<std::size_t>(offsets[level]);
        if (range.byte_offset < kGoldSrcMiptexHeaderWireSize) {
            return parse_fail(GoldSrcMiptexErrorCode::mip_offset_before_header,
                24U + level * 4U,
                level,
                "Mip level begins before the end of the miptex header");
        }
        if (!checked_multiply(static_cast<std::size_t>(range.width),
                static_cast<std::size_t>(range.height), range.byte_count)) {
            return parse_fail(GoldSrcMiptexErrorCode::mip_range_overflow,
                24U + level * 4U,
                level,
                "Mip level byte count overflows the host size type");
        }
        std::size_t range_end = 0U;
        if (!checked_add(range.byte_offset, range.byte_count, range_end)) {
            return parse_fail(GoldSrcMiptexErrorCode::mip_range_overflow,
                24U + level * 4U,
                level,
                "Mip level byte range overflows the host size type");
        }
        if (range_end > record.size()) {
            return parse_fail(GoldSrcMiptexErrorCode::mip_range_out_of_bounds,
                24U + level * 4U,
                level,
                "Mip level byte range extends outside its bounded record");
        }
        if (range.byte_offset < previous_end) {
            return parse_fail(GoldSrcMiptexErrorCode::overlapping_mip_ranges,
                24U + level * 4U,
                level,
                "Mip level ranges must be increasing and non-overlapping");
        }
        previous_end = range_end;
    }

    const auto palette_count_offset = previous_end;
    std::size_t after_palette_count = 0U;
    if (!checked_add(palette_count_offset, 2U, after_palette_count) ||
        after_palette_count > record.size()) {
        return parse_fail(GoldSrcMiptexErrorCode::missing_palette_count,
            palette_count_offset,
            std::nullopt,
            "Miptex palette count is truncated after the fourth mip level");
    }
    parsed.palette_color_count = read_u16_le(record, palette_count_offset);
    if (parsed.palette_color_count != kGoldSrcMiptexPaletteColorCount) {
        return parse_fail(GoldSrcMiptexErrorCode::unsupported_palette_count,
            palette_count_offset,
            std::nullopt,
            "Supported GoldSrc indexed textures require exactly 256 palette colors");
    }
    parsed.palette_byte_offset = after_palette_count;
    std::size_t palette_end = 0U;
    if (!checked_add(*parsed.palette_byte_offset,
            kGoldSrcMiptexPaletteRgbByteCount, palette_end) ||
        palette_end > record.size()) {
        return parse_fail(GoldSrcMiptexErrorCode::palette_range_out_of_bounds,
            *parsed.palette_byte_offset,
            std::nullopt,
            "Miptex RGB palette extends outside its bounded record");
    }
    const auto trailing = record.subspan(palette_end);
    if (trailing.size() > limits.maximum_trailing_zero_fill_bytes ||
        !std::all_of(trailing.begin(), trailing.end(), [](const std::byte value) {
            return value == std::byte{0};
        })) {
        return parse_fail(GoldSrcMiptexErrorCode::unexpected_trailing_data,
            palette_end,
            std::nullopt,
            "Miptex permits only bounded zero alignment fill after its palette");
    }
    parsed.bytes_consumed = record.size();
    return GoldSrcMiptexParseResult{std::move(parsed), std::nullopt};
}

GoldSrcIndexedTextureDecodeOperation::GoldSrcIndexedTextureDecodeOperation(
    const std::span<const std::byte> record,
    GoldSrcParsedMiptex parsed,
    assets::WorldTextureAsset texture,
    const std::size_t total_rgba_bytes) noexcept
    : record_{record},
      parsed_{std::move(parsed)},
      texture_{std::move(texture)},
      total_rgba_bytes_{total_rgba_bytes}
{
}

GoldSrcIndexedTextureDecodeStartResult GoldSrcIndexedTextureDecodeOperation::begin(
    const std::span<const std::byte> record,
    const GoldSrcMiptexSourceProfile source_profile,
    const assets::WorldTextureSourceKind source_kind,
    const std::optional<std::uint32_t> source_bsp_texture_index,
    const std::optional<std::uint32_t> source_archive_ordinal,
    const GoldSrcIndexedTextureLimits& limits)
{
    auto parsed_result = GoldSrcMiptexParser::parse(record, source_profile, limits);
    if (!parsed_result) {
        return begin_fail(std::move(*parsed_result.error));
    }
    auto parsed = std::move(*parsed_result.texture);
    if (parsed.storage_profile != GoldSrcMiptexStorageProfile::indexed_pixels ||
        !parsed.palette_byte_offset) {
        return begin_fail(GoldSrcMiptexError{
            GoldSrcMiptexErrorCode::external_reference_not_allowed,
            24U,
            std::nullopt,
            "Indexed RGBA conversion requires a pixel-backed miptex record",
        });
    }

    std::size_t total_rgba_bytes = 0U;
    assets::WorldTextureAsset texture;
    try {
        texture.name = parsed.name;
        texture.width = parsed.width;
        texture.height = parsed.height;
        texture.source_kind = source_kind;
        texture.alpha_mode = !parsed.name.empty() && parsed.name.front() == '{'
            ? assets::WorldTextureAlphaMode::masked_index_255
            : assets::WorldTextureAlphaMode::opaque;
        texture.source_bsp_texture_index = source_bsp_texture_index;
        texture.source_archive_ordinal = source_archive_ordinal;
        for (std::size_t level = 0U; level < parsed.mip_levels.size(); ++level) {
            const auto& source_level = parsed.mip_levels[level];
            auto& output_level = texture.mip_levels[level];
            output_level.width = source_level.width;
            output_level.height = source_level.height;
            std::size_t rgba_bytes = 0U;
            if (!checked_multiply(source_level.byte_count,
                    kRgbaBytesPerTexel, rgba_bytes) ||
                !checked_add(total_rgba_bytes, rgba_bytes, total_rgba_bytes) ||
                total_rgba_bytes > limits.maximum_decoded_rgba_bytes) {
                return begin_fail(GoldSrcMiptexError{
                    GoldSrcMiptexErrorCode::decoded_size_limit_exceeded,
                    source_level.byte_offset,
                    level,
                    "Decoded texture RGBA bytes exceed the configured per-texture limit",
                });
            }
            output_level.rgba_pixels.resize(rgba_bytes);
        }
    } catch (const std::bad_alloc&) {
        return begin_fail(GoldSrcMiptexError{
            GoldSrcMiptexErrorCode::unable_to_retain_texture,
            0U,
            std::nullopt,
            "Unable to allocate the bounded owning RGBA texture",
        });
    } catch (const std::length_error&) {
        return begin_fail(GoldSrcMiptexError{
            GoldSrcMiptexErrorCode::unable_to_retain_texture,
            0U,
            std::nullopt,
            "Decoded texture exceeds an owning container limit",
        });
    }

    return GoldSrcIndexedTextureDecodeStartResult{
        GoldSrcIndexedTextureDecodeOperation{
            record, std::move(parsed), std::move(texture), total_rgba_bytes},
        std::nullopt,
    };
}

GoldSrcIndexedTextureDecodeState GoldSrcIndexedTextureDecodeOperation::update(
    const std::size_t maximum_rgba_bytes) noexcept
{
    if (state_ != GoldSrcIndexedTextureDecodeState::decoding) {
        return state_;
    }
    const auto texel_budget = maximum_rgba_bytes / kRgbaBytesPerTexel;
    if (texel_budget == 0U) {
        error_ = GoldSrcMiptexError{
            GoldSrcMiptexErrorCode::invalid_update_budget,
            0U,
            std::nullopt,
            "Pixel conversion update budget must permit at least one RGBA texel",
        };
        texture_.reset();
        state_ = GoldSrcIndexedTextureDecodeState::failed;
        return state_;
    }

    const auto palette_offset = *parsed_.palette_byte_offset;
    std::size_t remaining_texels = texel_budget;
    while (remaining_texels > 0U && current_mip_level_ < kGoldSrcMiptexLevelCount) {
        const auto& source_level = parsed_.mip_levels[current_mip_level_];
        auto& output = texture_->mip_levels[current_mip_level_].rgba_pixels;
        const auto available = source_level.byte_count - current_level_texel_;
        const auto convert_count = std::min(available, remaining_texels);
        for (std::size_t index = 0U; index < convert_count; ++index) {
            const auto source_index = std::to_integer<std::uint8_t>(
                record_[source_level.byte_offset + current_level_texel_ + index]);
            const auto palette_entry = palette_offset +
                static_cast<std::size_t>(source_index) * 3U;
            const auto output_offset = (current_level_texel_ + index) * 4U;
            output[output_offset] = record_[palette_entry];
            output[output_offset + 1U] = record_[palette_entry + 1U];
            output[output_offset + 2U] = record_[palette_entry + 2U];
            output[output_offset + 3U] =
                texture_->alpha_mode == assets::WorldTextureAlphaMode::masked_index_255 &&
                    source_index == 255U
                ? std::byte{0}
                : std::byte{0xFF};
        }
        current_level_texel_ += convert_count;
        remaining_texels -= convert_count;
        converted_rgba_bytes_ += convert_count * kRgbaBytesPerTexel;
        if (current_level_texel_ == source_level.byte_count) {
            ++current_mip_level_;
            current_level_texel_ = 0U;
        }
    }
    if (current_mip_level_ == kGoldSrcMiptexLevelCount) {
        state_ = GoldSrcIndexedTextureDecodeState::complete;
    }
    return state_;
}

GoldSrcIndexedTextureDecodeState GoldSrcIndexedTextureDecodeOperation::state()
    const noexcept
{
    return state_;
}

const GoldSrcMiptexError* GoldSrcIndexedTextureDecodeOperation::error() const noexcept
{
    return error_ ? &*error_ : nullptr;
}

const assets::WorldTextureAsset* GoldSrcIndexedTextureDecodeOperation::texture()
    const noexcept
{
    return state_ == GoldSrcIndexedTextureDecodeState::complete && texture_
        ? &*texture_
        : nullptr;
}

std::optional<assets::WorldTextureAsset>
GoldSrcIndexedTextureDecodeOperation::take_texture() noexcept
{
    if (state_ != GoldSrcIndexedTextureDecodeState::complete || !texture_) {
        return std::nullopt;
    }
    auto result = std::move(texture_);
    texture_.reset();
    return result;
}

std::size_t GoldSrcIndexedTextureDecodeOperation::converted_rgba_byte_count()
    const noexcept
{
    return converted_rgba_bytes_;
}

std::size_t GoldSrcIndexedTextureDecodeOperation::total_rgba_byte_count() const noexcept
{
    return total_rgba_bytes_;
}

GoldSrcIndexedTextureDecodeResult GoldSrcIndexedTextureDecoder::decode(
    const std::span<const std::byte> record,
    const GoldSrcMiptexSourceProfile source_profile,
    const assets::WorldTextureSourceKind source_kind,
    const std::optional<std::uint32_t> source_bsp_texture_index,
    const std::optional<std::uint32_t> source_archive_ordinal,
    const GoldSrcIndexedTextureLimits& limits)
{
    auto started = GoldSrcIndexedTextureDecodeOperation::begin(record,
        source_profile,
        source_kind,
        source_bsp_texture_index,
        source_archive_ordinal,
        limits);
    if (!started) {
        return GoldSrcIndexedTextureDecodeResult{
            std::nullopt,
            std::move(started.error),
        };
    }
    auto operation = std::move(*started.operation);
    const auto state = operation.update(operation.total_rgba_byte_count());
    if (state != GoldSrcIndexedTextureDecodeState::complete) {
        return GoldSrcIndexedTextureDecodeResult{
            std::nullopt,
            operation.error() ? std::optional{*operation.error()}
                              : std::optional{GoldSrcMiptexError{
                                    GoldSrcMiptexErrorCode::unable_to_retain_texture,
                                    0U,
                                    std::nullopt,
                                    "Indexed texture conversion did not complete",
                                }},
        };
    }
    return GoldSrcIndexedTextureDecodeResult{
        operation.take_texture(),
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc::indexed_texture

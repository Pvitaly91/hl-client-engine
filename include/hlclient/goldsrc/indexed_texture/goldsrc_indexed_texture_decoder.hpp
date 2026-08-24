#pragma once

#include <hlclient/assets/world_texture_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc::indexed_texture {

inline constexpr std::size_t kGoldSrcMiptexHeaderWireSize = 40U;
inline constexpr std::size_t kGoldSrcMiptexNameWireSize = 16U;
inline constexpr std::size_t kGoldSrcMiptexLevelCount = 4U;
inline constexpr std::uint16_t kGoldSrcMiptexPaletteColorCount = 256U;
inline constexpr std::size_t kGoldSrcMiptexPaletteRgbByteCount =
    static_cast<std::size_t>(kGoldSrcMiptexPaletteColorCount) * 3U;

enum class GoldSrcMiptexSourceProfile {
    bsp_embedded,
    wad3_lump,
};

enum class GoldSrcMiptexStorageProfile {
    external_reference,
    indexed_pixels,
};

struct GoldSrcIndexedTextureLimits {
    std::uint32_t maximum_dimension{4'096U};
    std::uint64_t maximum_level_zero_texels{16'777'216ULL};
    std::size_t maximum_decoded_rgba_bytes{64U * 1024U * 1024U};
    // Public Valve qlumpy aligns emitted lumps with zero bytes. Only this
    // bounded suffix is accepted; arbitrary trailing data remains malformed.
    std::size_t maximum_trailing_zero_fill_bytes{3U};
};

[[nodiscard]] bool valid_goldsrc_indexed_texture_limits(
    const GoldSrcIndexedTextureLimits& limits) noexcept;

struct GoldSrcMipLevelRange {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::size_t byte_offset{0U};
    std::size_t byte_count{0U};
};

struct GoldSrcParsedMiptex {
    std::string name;
    std::string normalized_name;
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::array<GoldSrcMipLevelRange, kGoldSrcMiptexLevelCount> mip_levels{};
    GoldSrcMiptexStorageProfile storage_profile{
        GoldSrcMiptexStorageProfile::external_reference};
    std::optional<std::size_t> palette_byte_offset;
    std::uint16_t palette_color_count{0U};
    std::size_t bytes_consumed{0U};
};

enum class GoldSrcMiptexErrorCode {
    invalid_configuration,
    source_too_small,
    invalid_name,
    invalid_dimensions,
    texture_limit_exceeded,
    mixed_mip_offsets,
    external_reference_not_allowed,
    mip_offset_before_header,
    mip_range_overflow,
    mip_range_out_of_bounds,
    overlapping_mip_ranges,
    missing_palette_count,
    unsupported_palette_count,
    palette_range_out_of_bounds,
    unexpected_trailing_data,
    decoded_size_limit_exceeded,
    invalid_update_budget,
    unable_to_retain_texture,
};

[[nodiscard]] std::string_view to_string(GoldSrcMiptexErrorCode code) noexcept;

struct GoldSrcMiptexError {
    GoldSrcMiptexErrorCode code{GoldSrcMiptexErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> mip_level;
    std::string context;
};

struct GoldSrcMiptexParseResult {
    std::optional<GoldSrcParsedMiptex> texture;
    std::optional<GoldSrcMiptexError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return texture.has_value();
    }
};

class GoldSrcMiptexParser final {
public:
    [[nodiscard]] static GoldSrcMiptexParseResult parse(
        std::span<const std::byte> record,
        GoldSrcMiptexSourceProfile source_profile,
        const GoldSrcIndexedTextureLimits& limits = {});
};

enum class GoldSrcIndexedTextureDecodeState {
    decoding,
    complete,
    failed,
};

struct GoldSrcIndexedTextureDecodeStartResult;

// Caller-driven conversion. Source bytes must outlive the operation. Each
// update converts at most floor(maximum_rgba_bytes / 4) indexed texels.
class GoldSrcIndexedTextureDecodeOperation final {
public:
    [[nodiscard]] static GoldSrcIndexedTextureDecodeStartResult begin(
        std::span<const std::byte> record,
        GoldSrcMiptexSourceProfile source_profile,
        assets::WorldTextureSourceKind source_kind,
        std::optional<std::uint32_t> source_bsp_texture_index = std::nullopt,
        std::optional<std::uint32_t> source_archive_ordinal = std::nullopt,
        const GoldSrcIndexedTextureLimits& limits = {});

    GoldSrcIndexedTextureDecodeOperation(
        const GoldSrcIndexedTextureDecodeOperation&) = delete;
    GoldSrcIndexedTextureDecodeOperation& operator=(
        const GoldSrcIndexedTextureDecodeOperation&) = delete;
    GoldSrcIndexedTextureDecodeOperation(
        GoldSrcIndexedTextureDecodeOperation&&) noexcept = default;
    GoldSrcIndexedTextureDecodeOperation& operator=(
        GoldSrcIndexedTextureDecodeOperation&&) noexcept = default;
    ~GoldSrcIndexedTextureDecodeOperation() = default;

    [[nodiscard]] GoldSrcIndexedTextureDecodeState update(
        std::size_t maximum_rgba_bytes) noexcept;
    [[nodiscard]] GoldSrcIndexedTextureDecodeState state() const noexcept;
    [[nodiscard]] const GoldSrcMiptexError* error() const noexcept;
    [[nodiscard]] const assets::WorldTextureAsset* texture() const noexcept;
    [[nodiscard]] std::optional<assets::WorldTextureAsset> take_texture() noexcept;
    [[nodiscard]] std::size_t converted_rgba_byte_count() const noexcept;
    [[nodiscard]] std::size_t total_rgba_byte_count() const noexcept;

private:
    GoldSrcIndexedTextureDecodeOperation(
        std::span<const std::byte> record,
        GoldSrcParsedMiptex parsed,
        assets::WorldTextureAsset texture,
        std::size_t total_rgba_bytes) noexcept;

    std::span<const std::byte> record_;
    GoldSrcParsedMiptex parsed_;
    std::optional<assets::WorldTextureAsset> texture_;
    std::optional<GoldSrcMiptexError> error_;
    GoldSrcIndexedTextureDecodeState state_{
        GoldSrcIndexedTextureDecodeState::decoding};
    std::size_t current_mip_level_{0U};
    std::size_t current_level_texel_{0U};
    std::size_t converted_rgba_bytes_{0U};
    std::size_t total_rgba_bytes_{0U};
};

struct GoldSrcIndexedTextureDecodeStartResult {
    std::optional<GoldSrcIndexedTextureDecodeOperation> operation;
    std::optional<GoldSrcMiptexError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return operation.has_value();
    }
};

struct GoldSrcIndexedTextureDecodeResult {
    std::optional<assets::WorldTextureAsset> texture;
    std::optional<GoldSrcMiptexError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return texture.has_value();
    }
};

class GoldSrcIndexedTextureDecoder final {
public:
    // Convenience entry point for bounded offline/unit callers. Production
    // incremental stages use GoldSrcIndexedTextureDecodeOperation directly.
    [[nodiscard]] static GoldSrcIndexedTextureDecodeResult decode(
        std::span<const std::byte> record,
        GoldSrcMiptexSourceProfile source_profile,
        assets::WorldTextureSourceKind source_kind,
        std::optional<std::uint32_t> source_bsp_texture_index = std::nullopt,
        std::optional<std::uint32_t> source_archive_ordinal = std::nullopt,
        const GoldSrcIndexedTextureLimits& limits = {});
};

} // namespace hlclient::goldsrc::indexed_texture

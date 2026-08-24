#pragma once

#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc::wad3 {

struct GoldSrcWad3TextureRequest {
    std::optional<std::string_view> expected_texture_name;
    std::optional<std::uint32_t> expected_width;
    std::optional<std::uint32_t> expected_height;
    std::optional<std::uint32_t> source_bsp_texture_index;
    std::optional<std::uint32_t> source_archive_ordinal;
};

enum class GoldSrcWad3TextureErrorCode {
    invalid_configuration,
    invalid_entry_type,
    unsupported_compression,
    entry_size_mismatch,
    source_range_overflow,
    source_range_out_of_bounds,
    miptex_parse_failed,
    directory_name_mismatch,
    expected_texture_name_mismatch,
    dimension_mismatch,
    miptex_decode_failed,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcWad3TextureErrorCode code) noexcept;

struct GoldSrcWad3TextureError {
    GoldSrcWad3TextureErrorCode code{
        GoldSrcWad3TextureErrorCode::invalid_configuration};
    std::size_t directory_ordinal{0U};
    std::size_t byte_offset{0U};
    std::optional<indexed_texture::GoldSrcMiptexError> miptex_error;
    std::string context;
};

// Owning metadata needed to start an incremental shared decoder operation.
// It deliberately retains no source span, WAD bytes, native path, or file handle.
struct GoldSrcWad3PreparedTexture {
    indexed_texture::GoldSrcParsedMiptex miptex;
    std::size_t record_byte_offset{0U};
    std::size_t record_byte_count{0U};
    std::size_t directory_ordinal{0U};
    std::optional<std::uint32_t> source_bsp_texture_index;
    std::optional<std::uint32_t> source_archive_ordinal;
};

struct GoldSrcWad3TexturePrepareResult {
    std::optional<GoldSrcWad3PreparedTexture> texture;
    std::optional<GoldSrcWad3TextureError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return texture.has_value();
    }
};

class GoldSrcWad3TextureParser final {
public:
    [[nodiscard]] static GoldSrcWad3TexturePrepareResult parse(
        std::span<const std::byte> wad_source,
        const GoldSrcWad3Entry& entry,
        const GoldSrcWad3TextureRequest& request = {},
        const indexed_texture::GoldSrcIndexedTextureLimits& limits = {});
};

struct GoldSrcWad3TextureDecodeResult {
    std::optional<assets::WorldTextureAsset> texture;
    std::optional<GoldSrcWad3TextureError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return texture.has_value();
    }
};

class GoldSrcWad3TextureDecoder final {
public:
    [[nodiscard]] static GoldSrcWad3TextureDecodeResult decode(
        std::span<const std::byte> wad_source,
        const GoldSrcWad3Entry& entry,
        const GoldSrcWad3TextureRequest& request = {},
        const indexed_texture::GoldSrcIndexedTextureLimits& limits = {});
};

} // namespace hlclient::goldsrc::wad3

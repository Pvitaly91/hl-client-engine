#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/indexed_texture/goldsrc_indexed_texture_decoder.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::bsp {

struct GoldSrcBspTextureSourceLimits {
    std::size_t maximum_source_bytes{32U * 1024U * 1024U};
    std::size_t maximum_material_count{8'192U};
    std::size_t maximum_texture_directory_count{512U};
    indexed_texture::GoldSrcIndexedTextureLimits indexed_texture_limits{};
};

enum class GoldSrcBspTextureSourceStorage {
    missing,
    external_reference,
    embedded,
};

struct GoldSrcBspTextureSource {
    // The lowest directory ordinal sharing this physical record. Missing
    // entries have only their own ordinal.
    std::uint32_t canonical_source_texture_index{0U};
    std::vector<std::uint32_t> source_texture_indices;
    GoldSrcBspTextureSourceStorage storage{GoldSrcBspTextureSourceStorage::missing};
    std::optional<std::size_t> source_record_offset;
    std::optional<std::size_t> source_record_byte_count;
    std::optional<indexed_texture::GoldSrcParsedMiptex> miptex;
};

class GoldSrcBspTextureSourceDocument final {
public:
    GoldSrcBspTextureSourceDocument(
        std::vector<GoldSrcBspTextureSource> sources,
        std::size_t entity_lump_offset,
        std::size_t entity_lump_byte_count,
        std::size_t texture_directory_count) noexcept;

    [[nodiscard]] std::span<const GoldSrcBspTextureSource> sources() const noexcept;
    [[nodiscard]] const GoldSrcBspTextureSource* source_for_texture_index(
        std::uint32_t source_texture_index) const noexcept;
    [[nodiscard]] std::size_t entity_lump_offset() const noexcept;
    [[nodiscard]] std::size_t entity_lump_byte_count() const noexcept;
    [[nodiscard]] std::size_t texture_directory_count() const noexcept;

private:
    std::vector<GoldSrcBspTextureSource> sources_;
    std::size_t entity_lump_offset_{0U};
    std::size_t entity_lump_byte_count_{0U};
    std::size_t texture_directory_count_{0U};
};

enum class GoldSrcBspTextureSourceErrorCode {
    invalid_configuration,
    source_too_small,
    source_limit_exceeded,
    unsupported_version,
    invalid_lump_range,
    invalid_texture_directory,
    material_count_limit_exceeded,
    invalid_material_texture_reference,
    malformed_miptex_record,
    material_metadata_mismatch,
    unable_to_retain_sources,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcBspTextureSourceErrorCode code) noexcept;

struct GoldSrcBspTextureSourceError {
    GoldSrcBspTextureSourceErrorCode code{
        GoldSrcBspTextureSourceErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> element_index;
    std::optional<indexed_texture::GoldSrcMiptexError> miptex_error;
    std::string context;
};

struct GoldSrcBspTextureSourceParseResult {
    std::optional<GoldSrcBspTextureSourceDocument> document;
    std::optional<GoldSrcBspTextureSourceError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return document.has_value();
    }
};

// Extracts bounded source ranges from the already-approved BSP bytes. It does
// not open files, retain source bytes, or decode RGBA pixels.
class GoldSrcBspTextureSourceParser final {
public:
    [[nodiscard]] static GoldSrcBspTextureSourceParseResult parse(
        std::span<const std::byte> bsp_source,
        std::span<const assets::WorldMaterialReference> world_materials,
        const GoldSrcBspTextureSourceLimits& limits = {});
};

} // namespace hlclient::goldsrc::bsp

#include <hlclient/goldsrc/bsp/goldsrc_bsp_texture_source_parser.hpp>

#include <hlclient/goldsrc/bsp/goldsrc_bsp_format.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::bsp {
namespace {

struct SourceRange {
    std::size_t offset{0U};
    std::size_t byte_count{0U};
};

[[nodiscard]] std::uint32_t read_u32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
            << static_cast<unsigned int>(index * 8U);
    }
    return value;
}

[[nodiscard]] std::int32_t read_i32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    return std::bit_cast<std::int32_t>(read_u32_le(bytes, offset));
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

[[nodiscard]] GoldSrcBspTextureSourceParseResult fail(
    const GoldSrcBspTextureSourceErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::size_t> element_index,
    std::string context,
    std::optional<indexed_texture::GoldSrcMiptexError> miptex_error = std::nullopt)
{
    return GoldSrcBspTextureSourceParseResult{
        std::nullopt,
        GoldSrcBspTextureSourceError{
            code,
            byte_offset,
            element_index,
            std::move(miptex_error),
            std::move(context),
        },
    };
}

[[nodiscard]] std::optional<SourceRange> read_lump_range(
    const std::span<const std::byte> source,
    const GoldSrcBspLumpId lump_id) noexcept
{
    const auto descriptor = 4U + goldsrc_bsp_lump_index(lump_id) * 8U;
    const auto signed_offset = read_i32_le(source, descriptor);
    const auto signed_length = read_i32_le(source, descriptor + 4U);
    if (signed_offset < 0 || signed_length < 0) {
        return std::nullopt;
    }
    const auto offset = static_cast<std::size_t>(
        static_cast<std::uint32_t>(signed_offset));
    const auto length = static_cast<std::size_t>(
        static_cast<std::uint32_t>(signed_length));
    std::size_t end = 0U;
    if (!checked_add(offset, length, end) || end > source.size() ||
        (length != 0U && offset < kGoldSrcBspHeaderWireSize)) {
        return std::nullopt;
    }
    return SourceRange{offset, length};
}

[[nodiscard]] bool metadata_matches(
    const assets::WorldMaterialReference& material,
    const GoldSrcBspTextureSourceStorage storage,
    const std::optional<indexed_texture::GoldSrcParsedMiptex>& miptex) noexcept
{
    if (storage == GoldSrcBspTextureSourceStorage::missing) {
        return material.texture_storage == assets::WorldTextureStorage::missing &&
            !material.texture_name && !material.width && !material.height;
    }
    if (!miptex) {
        return false;
    }
    const auto expected_storage = storage == GoldSrcBspTextureSourceStorage::embedded
        ? assets::WorldTextureStorage::embedded
        : assets::WorldTextureStorage::external_reference;
    return material.texture_storage == expected_storage &&
        material.texture_name == miptex->name && material.width == miptex->width &&
        material.height == miptex->height;
}

} // namespace

GoldSrcBspTextureSourceDocument::GoldSrcBspTextureSourceDocument(
    std::vector<GoldSrcBspTextureSource> sources,
    const std::size_t entity_lump_offset,
    const std::size_t entity_lump_byte_count,
    const std::size_t texture_directory_count) noexcept
    : sources_{std::move(sources)},
      entity_lump_offset_{entity_lump_offset},
      entity_lump_byte_count_{entity_lump_byte_count},
      texture_directory_count_{texture_directory_count}
{
}

std::span<const GoldSrcBspTextureSource>
GoldSrcBspTextureSourceDocument::sources() const noexcept
{
    return sources_;
}

const GoldSrcBspTextureSource*
GoldSrcBspTextureSourceDocument::source_for_texture_index(
    const std::uint32_t source_texture_index) const noexcept
{
    for (const auto& source : sources_) {
        if (std::find(source.source_texture_indices.begin(),
                source.source_texture_indices.end(), source_texture_index) !=
            source.source_texture_indices.end()) {
            return &source;
        }
    }
    return nullptr;
}

std::size_t GoldSrcBspTextureSourceDocument::entity_lump_offset() const noexcept
{
    return entity_lump_offset_;
}

std::size_t GoldSrcBspTextureSourceDocument::entity_lump_byte_count() const noexcept
{
    return entity_lump_byte_count_;
}

std::size_t GoldSrcBspTextureSourceDocument::texture_directory_count() const noexcept
{
    return texture_directory_count_;
}

std::string_view to_string(const GoldSrcBspTextureSourceErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcBspTextureSourceErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcBspTextureSourceErrorCode::source_too_small:
        return "source_too_small";
    case GoldSrcBspTextureSourceErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case GoldSrcBspTextureSourceErrorCode::unsupported_version:
        return "unsupported_version";
    case GoldSrcBspTextureSourceErrorCode::invalid_lump_range:
        return "invalid_lump_range";
    case GoldSrcBspTextureSourceErrorCode::invalid_texture_directory:
        return "invalid_texture_directory";
    case GoldSrcBspTextureSourceErrorCode::material_count_limit_exceeded:
        return "material_count_limit_exceeded";
    case GoldSrcBspTextureSourceErrorCode::invalid_material_texture_reference:
        return "invalid_material_texture_reference";
    case GoldSrcBspTextureSourceErrorCode::malformed_miptex_record:
        return "malformed_miptex_record";
    case GoldSrcBspTextureSourceErrorCode::material_metadata_mismatch:
        return "material_metadata_mismatch";
    case GoldSrcBspTextureSourceErrorCode::unable_to_retain_sources:
        return "unable_to_retain_sources";
    }
    return "unknown";
}

GoldSrcBspTextureSourceParseResult GoldSrcBspTextureSourceParser::parse(
    const std::span<const std::byte> bsp_source,
    const std::span<const assets::WorldMaterialReference> world_materials,
    const GoldSrcBspTextureSourceLimits& limits)
{
    if (limits.maximum_source_bytes < kGoldSrcBspHeaderWireSize ||
        limits.maximum_source_bytes > kGoldSrcBspHardMaximumSourceBytes ||
        limits.maximum_material_count == 0U ||
        limits.maximum_material_count > kGoldSrcBspHardMaximumOutputMaterials ||
        limits.maximum_texture_directory_count == 0U ||
        limits.maximum_texture_directory_count > kGoldSrcBspHardMaximumTextures ||
        !indexed_texture::valid_goldsrc_indexed_texture_limits(
            limits.indexed_texture_limits)) {
        return fail(GoldSrcBspTextureSourceErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "BSP texture-source limits are outside the supported profile");
    }
    if (bsp_source.size() < kGoldSrcBspHeaderWireSize) {
        return fail(GoldSrcBspTextureSourceErrorCode::source_too_small,
            bsp_source.size(),
            std::nullopt,
            "BSP source is shorter than its exact v30 header");
    }
    if (bsp_source.size() > limits.maximum_source_bytes) {
        return fail(GoldSrcBspTextureSourceErrorCode::source_limit_exceeded,
            bsp_source.size(),
            std::nullopt,
            "BSP source exceeds the configured texture-stage limit");
    }
    if (read_i32_le(bsp_source, 0U) != kGoldSrcBspVersion) {
        return fail(GoldSrcBspTextureSourceErrorCode::unsupported_version,
            0U,
            std::nullopt,
            "BSP texture source supports exact GoldSrc version 30 only");
    }
    if (world_materials.size() > limits.maximum_material_count) {
        return fail(GoldSrcBspTextureSourceErrorCode::material_count_limit_exceeded,
            0U,
            world_materials.size(),
            "World material count exceeds the configured texture-stage limit");
    }

    const auto entity_range = read_lump_range(bsp_source, GoldSrcBspLumpId::entities);
    const auto texture_range = read_lump_range(bsp_source, GoldSrcBspLumpId::textures);
    if (!entity_range || !texture_range) {
        return fail(GoldSrcBspTextureSourceErrorCode::invalid_lump_range,
            4U,
            std::nullopt,
            "Entity or texture lump range is invalid for the retained BSP source");
    }
    const auto texture_bytes = bsp_source.subspan(
        texture_range->offset, texture_range->byte_count);
    if (texture_bytes.size() < 4U) {
        return fail(GoldSrcBspTextureSourceErrorCode::invalid_texture_directory,
            texture_range->offset,
            std::nullopt,
            "Texture lump does not contain its signed directory count");
    }
    const auto signed_count = read_i32_le(texture_bytes, 0U);
    if (signed_count < 0) {
        return fail(GoldSrcBspTextureSourceErrorCode::invalid_texture_directory,
            texture_range->offset,
            std::nullopt,
            "Texture directory count is negative");
    }
    const auto count = static_cast<std::size_t>(
        static_cast<std::uint32_t>(signed_count));
    if (count > limits.maximum_texture_directory_count) {
        return fail(GoldSrcBspTextureSourceErrorCode::invalid_texture_directory,
            texture_range->offset,
            count,
            "Texture directory count exceeds the configured limit");
    }
    std::size_t table_bytes = 0U;
    std::size_t directory_bytes = 0U;
    if (!checked_multiply(count, 4U, table_bytes) ||
        !checked_add(4U, table_bytes, directory_bytes) ||
        directory_bytes > texture_bytes.size()) {
        return fail(GoldSrcBspTextureSourceErrorCode::invalid_texture_directory,
            texture_range->offset,
            count,
            "Texture directory offset table extends outside the texture lump");
    }

    std::vector<std::int32_t> offsets;
    std::vector<std::size_t> distinct_offsets;
    try {
        offsets.reserve(count);
        distinct_offsets.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto offset = read_i32_le(texture_bytes, 4U + index * 4U);
            if (offset < -1) {
                return fail(GoldSrcBspTextureSourceErrorCode::invalid_texture_directory,
                    texture_range->offset + 4U + index * 4U,
                    index,
                    "Texture directory entry must be -1 or a non-negative offset");
            }
            offsets.push_back(offset);
            if (offset >= 0) {
                const auto converted = static_cast<std::size_t>(
                    static_cast<std::uint32_t>(offset));
                std::size_t header_end = 0U;
                if (converted < directory_bytes ||
                    !checked_add(converted,
                        indexed_texture::kGoldSrcMiptexHeaderWireSize, header_end) ||
                    header_end > texture_bytes.size()) {
                    return fail(GoldSrcBspTextureSourceErrorCode::invalid_texture_directory,
                        texture_range->offset + 4U + index * 4U,
                        index,
                        "Miptex source header overlaps the directory or leaves the texture lump");
                }
                distinct_offsets.push_back(converted);
            }
        }
        std::sort(distinct_offsets.begin(), distinct_offsets.end());
        distinct_offsets.erase(
            std::unique(distinct_offsets.begin(), distinct_offsets.end()),
            distinct_offsets.end());
    } catch (const std::bad_alloc&) {
        return fail(GoldSrcBspTextureSourceErrorCode::unable_to_retain_sources,
            texture_range->offset,
            std::nullopt,
            "Unable to retain bounded BSP texture directory metadata");
    } catch (const std::length_error&) {
        return fail(GoldSrcBspTextureSourceErrorCode::unable_to_retain_sources,
            texture_range->offset,
            std::nullopt,
            "BSP texture directory exceeds an owning container limit");
    }

    std::vector<GoldSrcBspTextureSource> sources;
    std::vector<std::uint32_t> handled_indices;
    try {
        sources.reserve(std::min(world_materials.size(), count));
        handled_indices.reserve(std::min(world_materials.size(), count));
        for (std::size_t material_index = 0U;
             material_index < world_materials.size(); ++material_index) {
            const auto& material = world_materials[material_index];
            if (!material.source_texture_index) {
                continue;
            }
            const auto source_index = *material.source_texture_index;
            if (source_index >= count) {
                return fail(
                    GoldSrcBspTextureSourceErrorCode::invalid_material_texture_reference,
                    texture_range->offset,
                    material_index,
                    "World material references a texture-directory index outside the BSP");
            }
            if (std::find(handled_indices.begin(), handled_indices.end(), source_index) !=
                handled_indices.end()) {
                const auto* existing = [&sources, source_index]() {
                    for (const auto& source : sources) {
                        if (std::find(source.source_texture_indices.begin(),
                                source.source_texture_indices.end(), source_index) !=
                            source.source_texture_indices.end()) {
                            return &source;
                        }
                    }
                    return static_cast<const GoldSrcBspTextureSource*>(nullptr);
                }();
                if (!existing || !metadata_matches(material,
                        existing->storage, existing->miptex)) {
                    return fail(GoldSrcBspTextureSourceErrorCode::material_metadata_mismatch,
                        texture_range->offset,
                        material_index,
                        "Materials sharing one BSP texture index retain inconsistent metadata");
                }
                continue;
            }

            const auto signed_offset = offsets[source_index];
            if (signed_offset == -1) {
                GoldSrcBspTextureSource missing;
                missing.canonical_source_texture_index = source_index;
                missing.source_texture_indices.push_back(source_index);
                if (!metadata_matches(material, missing.storage, missing.miptex)) {
                    return fail(GoldSrcBspTextureSourceErrorCode::material_metadata_mismatch,
                        texture_range->offset + 4U + source_index * 4U,
                        material_index,
                        "Missing BSP directory entry disagrees with retained material metadata");
                }
                sources.push_back(std::move(missing));
                handled_indices.push_back(source_index);
                continue;
            }

            const auto relative_offset = static_cast<std::size_t>(
                static_cast<std::uint32_t>(signed_offset));
            const auto physical = std::lower_bound(
                distinct_offsets.begin(), distinct_offsets.end(), relative_offset);
            const auto next = std::next(physical);
            const auto relative_end = next == distinct_offsets.end()
                ? texture_bytes.size()
                : *next;
            const auto record = texture_bytes.subspan(
                relative_offset, relative_end - relative_offset);
            auto parsed = indexed_texture::GoldSrcMiptexParser::parse(record,
                indexed_texture::GoldSrcMiptexSourceProfile::bsp_embedded,
                limits.indexed_texture_limits);
            if (!parsed) {
                return fail(GoldSrcBspTextureSourceErrorCode::malformed_miptex_record,
                    texture_range->offset + relative_offset + parsed.error->byte_offset,
                    source_index,
                    "Used BSP miptex record does not match the supported indexed profile",
                    std::move(parsed.error));
            }

            GoldSrcBspTextureSource candidate;
            candidate.source_record_offset = texture_range->offset + relative_offset;
            candidate.source_record_byte_count = record.size();
            candidate.miptex = std::move(parsed.texture);
            candidate.storage = candidate.miptex->storage_profile ==
                    indexed_texture::GoldSrcMiptexStorageProfile::indexed_pixels
                ? GoldSrcBspTextureSourceStorage::embedded
                : GoldSrcBspTextureSourceStorage::external_reference;
            for (std::size_t ordinal = 0U; ordinal < offsets.size(); ++ordinal) {
                if (offsets[ordinal] == signed_offset) {
                    candidate.source_texture_indices.push_back(
                        static_cast<std::uint32_t>(ordinal));
                    handled_indices.push_back(static_cast<std::uint32_t>(ordinal));
                }
            }
            candidate.canonical_source_texture_index =
                candidate.source_texture_indices.front();
            if (!metadata_matches(material, candidate.storage, candidate.miptex)) {
                return fail(GoldSrcBspTextureSourceErrorCode::material_metadata_mismatch,
                    texture_range->offset + relative_offset,
                    material_index,
                    "Used BSP miptex record disagrees with retained material metadata");
            }
            sources.push_back(std::move(candidate));
        }
    } catch (const std::bad_alloc&) {
        return fail(GoldSrcBspTextureSourceErrorCode::unable_to_retain_sources,
            texture_range->offset,
            std::nullopt,
            "Unable to retain bounded used BSP texture-source metadata");
    } catch (const std::length_error&) {
        return fail(GoldSrcBspTextureSourceErrorCode::unable_to_retain_sources,
            texture_range->offset,
            std::nullopt,
            "Used BSP texture-source metadata exceeds an owning container limit");
    }

    return GoldSrcBspTextureSourceParseResult{
        GoldSrcBspTextureSourceDocument{std::move(sources),
            entity_range->offset,
            entity_range->byte_count,
            count},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc::bsp

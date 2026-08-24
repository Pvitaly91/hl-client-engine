#include <hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::goldsrc::wad3 {
namespace {

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

[[nodiscard]] constexpr char ascii_upper(const char value) noexcept
{
    return value >= 'a' && value <= 'z'
        ? static_cast<char>(value - ('a' - 'A'))
        : value;
}

[[nodiscard]] bool valid_texture_name(const std::string_view name) noexcept
{
    if (name.empty() || name.size() > kGoldSrcWad3EntryNameWireSize) {
        return false;
    }
    return std::ranges::all_of(name, [](const char value) {
        const auto byte = static_cast<std::uint8_t>(value);
        return byte >= 0x20U && byte <= 0x7EU;
    });
}

[[nodiscard]] bool ascii_case_equal(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_upper(left[index]) != ascii_upper(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    return std::string{context.substr(0U, kGoldSrcWad3MaximumDiagnosticContextBytes)};
}

[[nodiscard]] GoldSrcWad3TexturePrepareResult prepare_failure(
    const GoldSrcWad3TextureErrorCode code,
    const GoldSrcWad3Entry& entry,
    const std::size_t byte_offset,
    const std::string_view context,
    std::optional<indexed_texture::GoldSrcMiptexError> miptex_error = std::nullopt)
{
    return GoldSrcWad3TexturePrepareResult{
        std::nullopt,
        GoldSrcWad3TextureError{
            code,
            entry.directory_ordinal,
            byte_offset,
            std::move(miptex_error),
            bounded_context(context),
        },
    };
}

[[nodiscard]] GoldSrcWad3TextureDecodeResult decode_failure(
    GoldSrcWad3TextureError error)
{
    return GoldSrcWad3TextureDecodeResult{std::nullopt, std::move(error)};
}

} // namespace

std::string_view to_string(const GoldSrcWad3TextureErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcWad3TextureErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcWad3TextureErrorCode::invalid_entry_type: return "invalid_entry_type";
    case GoldSrcWad3TextureErrorCode::unsupported_compression:
        return "unsupported_compression";
    case GoldSrcWad3TextureErrorCode::entry_size_mismatch:
        return "entry_size_mismatch";
    case GoldSrcWad3TextureErrorCode::source_range_overflow:
        return "source_range_overflow";
    case GoldSrcWad3TextureErrorCode::source_range_out_of_bounds:
        return "source_range_out_of_bounds";
    case GoldSrcWad3TextureErrorCode::miptex_parse_failed:
        return "miptex_parse_failed";
    case GoldSrcWad3TextureErrorCode::directory_name_mismatch:
        return "directory_name_mismatch";
    case GoldSrcWad3TextureErrorCode::expected_texture_name_mismatch:
        return "expected_texture_name_mismatch";
    case GoldSrcWad3TextureErrorCode::dimension_mismatch: return "dimension_mismatch";
    case GoldSrcWad3TextureErrorCode::miptex_decode_failed:
        return "miptex_decode_failed";
    }
    return "unknown";
}

GoldSrcWad3TexturePrepareResult GoldSrcWad3TextureParser::parse(
    const std::span<const std::byte> wad_source,
    const GoldSrcWad3Entry& entry,
    const GoldSrcWad3TextureRequest& request,
    const indexed_texture::GoldSrcIndexedTextureLimits& limits)
{
    try {
        if (!indexed_texture::valid_goldsrc_indexed_texture_limits(limits) ||
            request.expected_width.has_value() != request.expected_height.has_value()) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::invalid_configuration,
                entry,
                entry.file_offset,
                "WAD3 texture request or shared decoder limits are invalid");
        }
        if (!entry.is_miptex()) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::invalid_entry_type,
                entry,
                entry.file_offset,
                "Only the evidence-confirmed WAD3 miptex entry type 0x43 can be decoded");
        }
        if (entry.compression != kGoldSrcWad3NoCompression) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::unsupported_compression,
                entry,
                entry.file_offset,
                "M4.2 does not decode compressed WAD3 entries");
        }
        if (entry.disk_size != entry.uncompressed_size) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::entry_size_mismatch,
                entry,
                entry.file_offset,
                "Uncompressed WAD3 entry sizes differ");
        }

        std::size_t record_end = 0U;
        if (!checked_add(entry.file_offset, entry.disk_size, record_end)) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::source_range_overflow,
                entry,
                entry.file_offset,
                "WAD3 miptex source range overflows the host size domain");
        }
        if (record_end > wad_source.size()) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::source_range_out_of_bounds,
                entry,
                entry.file_offset,
                "WAD3 miptex source range exceeds the supplied archive bytes");
        }

        const auto record = wad_source.subspan(entry.file_offset, entry.disk_size);
        auto parsed = indexed_texture::GoldSrcMiptexParser::parse(
            record,
            indexed_texture::GoldSrcMiptexSourceProfile::wad3_lump,
            limits);
        if (!parsed) {
            auto nested_error = std::move(parsed.error);
            const auto nested_offset = nested_error
                ? entry.file_offset + nested_error->byte_offset
                : entry.file_offset;
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::miptex_parse_failed,
                entry,
                nested_offset,
                "Shared GoldSrc miptex parser rejected the WAD3 texture record",
                std::move(nested_error));
        }
        if (parsed.texture->normalized_name != entry.normalized_name) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::directory_name_mismatch,
                entry,
                entry.file_offset,
                "WAD3 directory and miptex record names differ under ASCII normalization");
        }
        if (request.expected_texture_name &&
            (!valid_texture_name(*request.expected_texture_name) ||
             !ascii_case_equal(*request.expected_texture_name, parsed.texture->name))) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::expected_texture_name_mismatch,
                entry,
                entry.file_offset,
                "WAD3 miptex name does not match the requested BSP material name");
        }
        if (request.expected_width &&
            (*request.expected_width != parsed.texture->width ||
             *request.expected_height != parsed.texture->height)) {
            return prepare_failure(
                GoldSrcWad3TextureErrorCode::dimension_mismatch,
                entry,
                entry.file_offset + 16U,
                "WAD3 miptex dimensions do not match the BSP material metadata");
        }

        return GoldSrcWad3TexturePrepareResult{
            GoldSrcWad3PreparedTexture{
                std::move(*parsed.texture),
                entry.file_offset,
                entry.disk_size,
                entry.directory_ordinal,
                request.source_bsp_texture_index,
                request.source_archive_ordinal,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return prepare_failure(
            GoldSrcWad3TextureErrorCode::miptex_parse_failed,
            entry,
            entry.file_offset,
            "Unable to retain bounded WAD3 miptex metadata");
    } catch (...) {
        return prepare_failure(
            GoldSrcWad3TextureErrorCode::miptex_parse_failed,
            entry,
            entry.file_offset,
            "Unexpected failure while validating a WAD3 miptex record");
    }
}

GoldSrcWad3TextureDecodeResult GoldSrcWad3TextureDecoder::decode(
    const std::span<const std::byte> wad_source,
    const GoldSrcWad3Entry& entry,
    const GoldSrcWad3TextureRequest& request,
    const indexed_texture::GoldSrcIndexedTextureLimits& limits)
{
    auto prepared = GoldSrcWad3TextureParser::parse(wad_source, entry, request, limits);
    if (!prepared) {
        return decode_failure(std::move(*prepared.error));
    }

    const auto& metadata = *prepared.texture;
    const auto record = wad_source.subspan(
        metadata.record_byte_offset, metadata.record_byte_count);
    auto decoded = indexed_texture::GoldSrcIndexedTextureDecoder::decode(
        record,
        indexed_texture::GoldSrcMiptexSourceProfile::wad3_lump,
        assets::WorldTextureSourceKind::external_wad3,
        metadata.source_bsp_texture_index,
        metadata.source_archive_ordinal,
        limits);
    if (!decoded) {
        auto nested_error = std::move(decoded.error);
        const auto nested_offset = nested_error
            ? metadata.record_byte_offset + nested_error->byte_offset
            : metadata.record_byte_offset;
        return decode_failure(GoldSrcWad3TextureError{
            GoldSrcWad3TextureErrorCode::miptex_decode_failed,
            metadata.directory_ordinal,
            nested_offset,
            std::move(nested_error),
            bounded_context("Shared GoldSrc indexed-texture decoder rejected the WAD3 record"),
        });
    }
    return GoldSrcWad3TextureDecodeResult{std::move(decoded.texture), std::nullopt};
}

} // namespace hlclient::goldsrc::wad3

#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::goldsrc::bsp {
namespace {

struct ProbeLumpRange {
    std::size_t offset{0U};
    std::size_t length{0U};
};

[[nodiscard]] std::optional<std::int32_t> read_i32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned int>(index * 8U);
    }
    return std::bit_cast<std::int32_t>(value);
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

[[nodiscard]] bool ascii_equal_case_insensitive(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto fold = [](const char value) noexcept {
            return value >= 'A' && value <= 'Z'
                       ? static_cast<char>(value - 'A' + 'a')
                       : value;
        };
        if (fold(left[index]) != fold(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decode_plausible_directory(
    const std::span<const std::byte> bytes,
    std::array<ProbeLumpRange, kGoldSrcBspLumpCount>& ranges) noexcept
{
    if (bytes.size() < kGoldSrcBspHeaderWireSize) {
        return false;
    }
    for (std::size_t index = 0U; index < ranges.size(); ++index) {
        const auto descriptor = 4U + index * kGoldSrcBspLumpDescriptorWireSize;
        const auto signed_offset = read_i32_le(bytes, descriptor);
        const auto signed_length = read_i32_le(bytes, descriptor + 4U);
        if (!signed_offset || !signed_length || *signed_offset < 0 || *signed_length < 0) {
            return false;
        }
        const auto offset = static_cast<std::size_t>(
            static_cast<std::uint32_t>(*signed_offset));
        const auto length = static_cast<std::size_t>(
            static_cast<std::uint32_t>(*signed_length));
        std::size_t end = 0U;
        if (!checked_add(offset, length, end) || end > bytes.size() ||
            (length != 0U && offset < kGoldSrcBspHeaderWireSize)) {
            return false;
        }
        ranges[index] = ProbeLumpRange{offset, length};
    }
    for (std::size_t left = 0U; left < ranges.size(); ++left) {
        if (ranges[left].length == 0U) {
            continue;
        }
        std::size_t left_end = 0U;
        if (!checked_add(ranges[left].offset, ranges[left].length, left_end)) {
            return false;
        }
        for (std::size_t right = left + 1U; right < ranges.size(); ++right) {
            if (ranges[right].length == 0U) {
                continue;
            }
            std::size_t right_end = 0U;
            if (!checked_add(ranges[right].offset, ranges[right].length, right_end)) {
                return false;
            }
            if (ranges[left].offset < right_end && ranges[right].offset < left_end) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool plausible_fixed_lump(
    const std::array<ProbeLumpRange, kGoldSrcBspLumpCount>& ranges,
    const GoldSrcBspLumpId id,
    const std::size_t record_size,
    const std::size_t count_limit) noexcept
{
    const auto length = ranges[goldsrc_bsp_lump_index(id)].length;
    return length % record_size == 0U && length / record_size <= count_limit;
}

[[nodiscard]] bool plausible_geometry_profile(
    const std::span<const std::byte> bytes,
    const std::array<ProbeLumpRange, kGoldSrcBspLumpCount>& ranges,
    const GoldSrcBspImportLimits& limits) noexcept
{
    if (!plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::planes,
            kGoldSrcBspPlaneWireSize,
            limits.maximum_planes) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::vertices,
            kGoldSrcBspVertexWireSize,
            limits.maximum_vertices) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::nodes,
            kGoldSrcBspNodeWireSize,
            limits.maximum_nodes) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::texinfo,
            kGoldSrcBspTexinfoWireSize,
            limits.maximum_texinfo) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::faces,
            kGoldSrcBspFaceWireSize,
            limits.maximum_faces) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::clipnodes,
            kGoldSrcBspClipnodeWireSize,
            limits.maximum_clipnodes) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::leaves,
            kGoldSrcBspLeafWireSize,
            limits.maximum_leaves) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::marksurfaces,
            kGoldSrcBspMarksurfaceWireSize,
            limits.maximum_marksurfaces) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::edges,
            kGoldSrcBspEdgeWireSize,
            limits.maximum_edges) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::surfedges,
            kGoldSrcBspSurfedgeWireSize,
            limits.maximum_surfedges) ||
        !plausible_fixed_lump(
            ranges,
            GoldSrcBspLumpId::models,
            kGoldSrcBspModelWireSize,
            limits.maximum_models)) {
        return false;
    }

    if (ranges[goldsrc_bsp_lump_index(GoldSrcBspLumpId::models)].length <
        kGoldSrcBspModelWireSize) {
        return false;
    }
    const auto& texture_range = ranges[goldsrc_bsp_lump_index(GoldSrcBspLumpId::textures)];
    if (texture_range.length < 4U) {
        return false;
    }
    const auto texture_count = read_i32_le(bytes, texture_range.offset);
    if (!texture_count || *texture_count < 0 ||
        static_cast<std::uint64_t>(*texture_count) >
            static_cast<std::uint64_t>(limits.maximum_textures)) {
        return false;
    }
    const auto unsigned_count = static_cast<std::size_t>(
        static_cast<std::uint32_t>(*texture_count));
    std::size_t offset_bytes = 0U;
    std::size_t table_size = 0U;
    return checked_multiply(unsigned_count, 4U, offset_bytes) &&
           checked_add(4U, offset_bytes, table_size) &&
           table_size <= texture_range.length;
}

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const auto code_unit : encoded) {
        result.push_back(static_cast<char>(code_unit));
    }
    return result;
}

} // namespace

GoldSrcBspWorldImporter::GoldSrcBspWorldImporter(GoldSrcBspImportLimits limits)
    : limits_{std::move(limits)}
{
}

std::string_view GoldSrcBspWorldImporter::id() const noexcept
{
    return kGoldSrcBspWorldImporterId;
}

assets::AssetProbeConfidence GoldSrcBspWorldImporter::probe(
    const assets::AssetProbe& probe) const noexcept
{
    const auto bytes = probe.structural_bytes;
    const auto version = read_i32_le(bytes, 0U);
    if (!version || *version != kGoldSrcBspVersion ||
        !valid_goldsrc_bsp_import_limits(limits_)) {
        return assets::kAssetProbeNoMatch;
    }

    const auto extension_boost = ascii_equal_case_insensitive(probe.extension_hint, ".bsp")
                                     ? kGoldSrcBspExtensionHintBoost
                                     : assets::kAssetProbeNoMatch;
    if (bytes.size() < kGoldSrcBspHeaderWireSize) {
        return static_cast<assets::AssetProbeConfidence>(
            kGoldSrcBspVersionProbeConfidence + extension_boost);
    }

    std::array<ProbeLumpRange, kGoldSrcBspLumpCount> ranges{};
    if (!decode_plausible_directory(bytes, ranges)) {
        return static_cast<assets::AssetProbeConfidence>(
            kGoldSrcBspHeaderProbeConfidence + extension_boost);
    }
    if (!plausible_geometry_profile(bytes, ranges, limits_)) {
        return static_cast<assets::AssetProbeConfidence>(
            kGoldSrcBspDirectoryProbeConfidence + extension_boost);
    }
    return static_cast<assets::AssetProbeConfidence>(
        kGoldSrcBspGeometryProbeConfidence + extension_boost);
}

assets::WorldAssetResult GoldSrcBspWorldImporter::import(
    const assets::AssetSource& source) const
{
    auto parsed = GoldSrcBspParser::parse(source.bytes(), limits_);
    if (!parsed) {
        const auto& parser_error = *parsed.error;
        const auto asset_error_code =
            parser_error.code == GoldSrcBspErrorCode::unsupported_version
                ? assets::AssetErrorCode::UnsupportedFormat
                : assets::AssetErrorCode::MalformedData;
        std::string context{"goldsrc-bsp-v30: code="};
        context.append(to_string(parser_error.code));
        if (parser_error.lump_id) {
            context.append("; lump=");
            context.append(to_string(*parser_error.lump_id));
        }
        context.append("; offset=");
        context.append(std::to_string(parser_error.byte_offset));
        if (parser_error.element_index) {
            context.append("; element=");
            context.append(std::to_string(*parser_error.element_index));
        }
        if (!parser_error.context.empty()) {
            context.append("; detail=");
            context.append(parser_error.context);
        }
        if (context.size() > kGoldSrcBspMaximumDiagnosticContextBytes) {
            context.resize(kGoldSrcBspMaximumDiagnosticContextBytes);
        }
        return assets::WorldAssetResult::failure(assets::AssetError{
            asset_error_code,
            source.virtual_path(),
            std::string{kGoldSrcBspWorldImporterId},
            std::move(context),
            {},
        });
    }

    auto world = std::move(parsed.document->world_asset);
    world.identity.source_name = path_as_utf8(source.virtual_path());
    return assets::WorldAssetResult::success(std::move(world));
}

assets::AssetImporterRegistrationResult register_builtin_asset_importers(
    assets::AssetImporterRegistries& registries,
    GoldSrcBspImportLimits limits)
{
    try {
        return registries.worlds.register_importer(
            std::make_unique<GoldSrcBspWorldImporter>(std::move(limits)),
            kGoldSrcBspWorldImporterPriority);
    } catch (const std::exception& exception) {
        return assets::AssetImporterRegistrationResult{
            false,
            assets::AssetImporterRegistrationError{
                assets::AssetImporterRegistrationErrorCode::NullImporter,
                std::string{kGoldSrcBspWorldImporterId},
                std::string{"Unable to construct the built-in BSP importer: "} +
                    exception.what(),
            },
        };
    } catch (...) {
        return assets::AssetImporterRegistrationResult{
            false,
            assets::AssetImporterRegistrationError{
                assets::AssetImporterRegistrationErrorCode::NullImporter,
                std::string{kGoldSrcBspWorldImporterId},
                "Unable to construct the built-in BSP importer",
            },
        };
    }
}

} // namespace hlclient::goldsrc::bsp

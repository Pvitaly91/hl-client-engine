#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::goldsrc::sprite {
namespace {

[[nodiscard]] std::optional<std::uint32_t> read_u32_le(
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
    return value;
}

[[nodiscard]] std::optional<std::int32_t> read_i32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    const auto value = read_u32_le(bytes, offset);
    return value ? std::optional{std::bit_cast<std::int32_t>(*value)}
                 : std::nullopt;
}

[[nodiscard]] std::optional<float> read_f32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    const auto value = read_u32_le(bytes, offset);
    return value ? std::optional{std::bit_cast<float>(*value)} : std::nullopt;
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

[[nodiscard]] bool valid_probe_orientation(const std::int32_t value) noexcept
{
    return value >= 0 && value <= 4;
}

[[nodiscard]] bool valid_probe_texture_format(const std::int32_t value) noexcept
{
    return value >= 0 && value <= 3;
}

[[nodiscard]] bool valid_probe_sync_type(const std::int32_t value) noexcept
{
    return value == 0 || value == 1;
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

[[nodiscard]] assets::AssetErrorCode map_error_code(
    const GoldSrcSpriteErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcSpriteErrorCode::invalid_identifier:
    case GoldSrcSpriteErrorCode::unsupported_version:
        return assets::AssetErrorCode::UnsupportedFormat;
    case GoldSrcSpriteErrorCode::invalid_configuration:
    case GoldSrcSpriteErrorCode::unable_to_retain_sprite:
        return assets::AssetErrorCode::ImportFailed;
    default: return assets::AssetErrorCode::MalformedData;
    }
}

} // namespace

GoldSrcSpriteImporter::GoldSrcSpriteImporter(GoldSrcSpriteImportLimits limits)
    : limits_{std::move(limits)}
{
}

std::string_view GoldSrcSpriteImporter::id() const noexcept
{
    return kGoldSrcSpriteImporterId;
}

assets::AssetProbeConfidence GoldSrcSpriteImporter::probe(
    const assets::AssetProbe& probe) const noexcept
{
    const auto bytes = probe.structural_bytes;
    if (bytes.size() < 8U) {
        return assets::kAssetProbeNoMatch;
    }
    const auto identifier = read_u32_le(bytes, 0U);
    const auto version = read_i32_le(bytes, 4U);
    if (!identifier || *identifier != kGoldSrcSpriteIdentifier ||
        !version || *version != kGoldSrcSpriteVersion) {
        return assets::kAssetProbeNoMatch;
    }
    const auto extension_boost =
        ascii_equal_case_insensitive(probe.extension_hint, ".spr")
            ? kGoldSrcSpriteExtensionHintBoost
            : assets::kAssetProbeNoMatch;
    const auto signature_confidence =
        static_cast<assets::AssetProbeConfidence>(
            kGoldSrcSpriteSignatureProbeConfidence + extension_boost);
    if (!valid_goldsrc_sprite_import_limits(limits_) ||
        bytes.size() < kGoldSrcSpriteHeaderWireSize) {
        return signature_confidence;
    }
    const auto orientation = read_i32_le(bytes, 8U);
    const auto texture_format = read_i32_le(bytes, 12U);
    const auto radius = read_f32_le(bytes, 16U);
    const auto width = read_i32_le(bytes, 20U);
    const auto height = read_i32_le(bytes, 24U);
    const auto frame_count = read_i32_le(bytes, 28U);
    const auto beam_length = read_f32_le(bytes, 32U);
    const auto sync_type = read_i32_le(bytes, 36U);
    if (!orientation || !valid_probe_orientation(*orientation) ||
        !texture_format || !valid_probe_texture_format(*texture_format) ||
        !radius || !std::isfinite(*radius) || *radius < 0.0F ||
        !width || *width <= 0 ||
        static_cast<std::uint32_t>(*width) > limits_.maximum_width ||
        !height || *height <= 0 ||
        static_cast<std::uint32_t>(*height) > limits_.maximum_height ||
        !frame_count || *frame_count <= 0 ||
        static_cast<std::uint64_t>(*frame_count) >
            static_cast<std::uint64_t>(limits_.maximum_top_level_entries) ||
        static_cast<std::uint64_t>(*frame_count) >
            static_cast<std::uint64_t>(limits_.maximum_flattened_frames) ||
        !beam_length || !std::isfinite(*beam_length) ||
        !sync_type || !valid_probe_sync_type(*sync_type)) {
        return signature_confidence;
    }

    return static_cast<assets::AssetProbeConfidence>(
        kGoldSrcSpriteHeaderProbeConfidence + extension_boost);
}

assets::SpriteAssetResult GoldSrcSpriteImporter::import(
    const assets::AssetSource& source) const
{
    auto parsed = GoldSrcSpriteParser::parse(source.bytes(), limits_);
    if (!parsed) {
        const auto& error = *parsed.error;
        return assets::SpriteAssetResult::failure(assets::AssetError{
            map_error_code(error.code),
            source.virtual_path(),
            std::string{kGoldSrcSpriteImporterId},
            std::string{to_string(error.code)} + " at byte " +
                std::to_string(error.byte_offset) + ": " + error.context,
            {},
        });
    }

    try {
        const auto source_name = path_as_utf8(source.virtual_path());
        assets::SpriteAsset asset;
        asset.identity.source_name = source_name;
        asset.frames = std::move(parsed.document->compatibility_frames);
        for (auto& frame : asset.frames) {
            frame.image.identity.source_name = source_name;
        }
        asset.source_data = std::move(parsed.document->source_data);
        return assets::SpriteAssetResult::success(std::move(asset));
    } catch (const std::exception& exception) {
        return assets::SpriteAssetResult::failure(assets::AssetError{
            assets::AssetErrorCode::ImportFailed,
            source.virtual_path(),
            std::string{kGoldSrcSpriteImporterId},
            std::string{"Unable to retain owning sprite asset: "} + exception.what(),
            {},
        });
    } catch (...) {
        return assets::SpriteAssetResult::failure(assets::AssetError{
            assets::AssetErrorCode::ImportFailed,
            source.virtual_path(),
            std::string{kGoldSrcSpriteImporterId},
            "Unable to retain owning sprite asset",
            {},
        });
    }
}

} // namespace hlclient::goldsrc::sprite

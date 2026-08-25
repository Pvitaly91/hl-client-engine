#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>

#include <algorithm>
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
#include <utility>

namespace hlclient::goldsrc::studio {
namespace {

[[nodiscard]] std::optional<std::int32_t> read_i32_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    if (offset > source.size() || source.size() - offset < 4U) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(source[offset + index]))
                 << static_cast<unsigned int>(index * 8U);
    }
    return std::bit_cast<std::int32_t>(value);
}

[[nodiscard]] bool identifier_matches(
    const std::span<const std::byte> source) noexcept
{
    return source.size() >= kGoldSrcStudioIdentifier.size() &&
           std::equal(kGoldSrcStudioIdentifier.begin(),
               kGoldSrcStudioIdentifier.end(), source.begin());
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

[[nodiscard]] bool plausible_range(
    const std::span<const std::byte> source,
    const std::size_t count_offset,
    const std::size_t record_size,
    const std::size_t count_limit,
    const std::size_t declared_length) noexcept
{
    const auto count = read_i32_le(source, count_offset);
    const auto offset = read_i32_le(source, count_offset + 4U);
    if (!count || !offset || *count < 0 || *offset < 0 ||
        static_cast<std::size_t>(*count) > count_limit) {
        return false;
    }
    if (*count == 0) {
        return static_cast<std::size_t>(*offset) <= declared_length;
    }
    const auto unsigned_count = static_cast<std::size_t>(*count);
    const auto unsigned_offset = static_cast<std::size_t>(*offset);
    if (unsigned_count > std::numeric_limits<std::size_t>::max() / record_size) {
        return false;
    }
    const auto length = unsigned_count * record_size;
    return unsigned_offset >= kGoldSrcStudioHeaderWireSize &&
           unsigned_offset <= declared_length &&
           length <= declared_length - unsigned_offset;
}

[[nodiscard]] bool plausible_directory(
    const std::span<const std::byte> source,
    const GoldSrcStudioModelImportLimits& limits,
    const std::size_t declared_length) noexcept
{
    return plausible_range(source, kGoldSrcStudioHeaderBonesOffset,
               kGoldSrcStudioBoneWireSize, limits.maximum_bones, declared_length) &&
           plausible_range(source, kGoldSrcStudioHeaderBoneControllersOffset,
               kGoldSrcStudioBoneControllerWireSize, limits.maximum_bone_controllers,
               declared_length) &&
           plausible_range(source, kGoldSrcStudioHeaderHitboxesOffset,
               kGoldSrcStudioHitboxWireSize, limits.maximum_hitboxes, declared_length) &&
           plausible_range(source, kGoldSrcStudioHeaderSequencesOffset,
               kGoldSrcStudioSequenceWireSize, limits.maximum_sequences,
               declared_length) &&
           plausible_range(source, kGoldSrcStudioHeaderSequenceGroupsOffset,
               kGoldSrcStudioSequenceGroupWireSize, limits.maximum_sequence_groups,
               declared_length) &&
           plausible_range(source, kGoldSrcStudioHeaderTexturesOffset,
               kGoldSrcStudioTextureWireSize, limits.maximum_textures,
               declared_length) &&
           plausible_range(source, kGoldSrcStudioHeaderBodyPartsOffset,
               kGoldSrcStudioBodyPartWireSize, limits.maximum_bodyparts,
               declared_length) &&
           plausible_range(source, kGoldSrcStudioHeaderAttachmentsOffset,
               kGoldSrcStudioAttachmentWireSize, limits.maximum_attachments,
               declared_length);
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

[[nodiscard]] assets::ModelAssetResult parser_failure(
    const assets::AssetSource& source,
    const GoldSrcStudioError& error)
{
    auto code = assets::AssetErrorCode::MalformedData;
    if (error.code == GoldSrcStudioErrorCode::unsupported_identifier ||
        error.code == GoldSrcStudioErrorCode::unsupported_version) {
        code = assets::AssetErrorCode::UnsupportedFormat;
    } else if (error.code == GoldSrcStudioErrorCode::external_dependency_required ||
               error.code == GoldSrcStudioErrorCode::missing_texture_companion ||
               error.code == GoldSrcStudioErrorCode::missing_sequence_group) {
        code = assets::AssetErrorCode::ExternalDependencyRequired;
    }
    std::string context{"goldsrc-studio-mdl-v10: code="};
    context.append(to_string(error.code));
    context.append("; offset=");
    context.append(std::to_string(error.byte_offset));
    if (error.element_index) {
        context.append("; element=");
        context.append(std::to_string(*error.element_index));
    }
    if (error.source_group_ordinal) {
        context.append("; group=");
        context.append(std::to_string(*error.source_group_ordinal));
    }
    if (!error.context.empty()) {
        context.append("; detail=");
        context.append(error.context);
    }
    if (context.size() > kGoldSrcStudioMaximumDiagnosticContextBytes) {
        context.resize(kGoldSrcStudioMaximumDiagnosticContextBytes);
    }
    return assets::ModelAssetResult::failure(assets::AssetError{
        code,
        source.virtual_path(),
        std::string{kGoldSrcStudioModelImporterId},
        std::move(context),
        {},
    });
}

} // namespace

GoldSrcStudioModelImporter::GoldSrcStudioModelImporter(
    GoldSrcStudioModelImportLimits limits)
    : limits_{std::move(limits)}
{
}

std::string_view GoldSrcStudioModelImporter::id() const noexcept
{
    return kGoldSrcStudioModelImporterId;
}

assets::AssetProbeConfidence GoldSrcStudioModelImporter::probe(
    const assets::AssetProbe& probe) const noexcept
{
    const auto bytes = probe.structural_bytes;
    if (!valid_goldsrc_studio_model_import_limits(limits_) || bytes.size() < 8U ||
        !identifier_matches(bytes) ||
        read_i32_le(bytes, 4U) != kGoldSrcStudioVersion) {
        return assets::kAssetProbeNoMatch;
    }
    const auto extension_boost = ascii_equal_case_insensitive(probe.extension_hint, ".mdl")
                                     ? kGoldSrcStudioExtensionHintBoost
                                     : assets::kAssetProbeNoMatch;
    if (bytes.size() < kGoldSrcStudioHeaderWireSize) {
        return static_cast<assets::AssetProbeConfidence>(
            kGoldSrcStudioSignatureProbeConfidence + extension_boost);
    }
    const auto declared = read_i32_le(bytes, kGoldSrcStudioHeaderLengthOffset);
    if (!declared || *declared <
                         static_cast<std::int32_t>(kGoldSrcStudioHeaderWireSize) ||
        static_cast<std::size_t>(*declared) > bytes.size() ||
        static_cast<std::size_t>(*declared) > limits_.maximum_main_source_bytes) {
        return static_cast<assets::AssetProbeConfidence>(
            kGoldSrcStudioSignatureProbeConfidence + extension_boost);
    }
    const auto declared_length = static_cast<std::size_t>(*declared);
    if (!plausible_directory(bytes, limits_, declared_length)) {
        return static_cast<assets::AssetProbeConfidence>(
            kGoldSrcStudioHeaderProbeConfidence + extension_boost);
    }
    return static_cast<assets::AssetProbeConfidence>(
        kGoldSrcStudioDirectoryProbeConfidence + extension_boost);
}

assets::ModelAssetResult GoldSrcStudioModelImporter::import(
    const assets::AssetSource& source) const
{
    return import_with_configuration(source, limits_);
}

assets::ModelAssetResult
GoldSrcStudioModelImporter::import_with_configuration(
    const assets::AssetSource& source,
    const GoldSrcStudioModelImportLimits& limits)
{
    const auto inspected = GoldSrcStudioParser::inspect_dependencies(
        source.bytes(), limits);
    if (!inspected) {
        return parser_failure(source, *inspected.error);
    }
    if (inspected.plan->texture_companion_required ||
        !inspected.plan->required_sequence_group_ordinals.empty()) {
        return parser_failure(source, GoldSrcStudioError{
            GoldSrcStudioErrorCode::external_dependency_required,
            0U,
            std::nullopt,
            std::nullopt,
            "Validated Studio model requires derived companion sources",
        });
    }
    const GoldSrcStudioSourceBundleView bundle{
        source.bytes(), std::nullopt, {}};
    return import_bundle_with_configuration(source, bundle, limits);
}

assets::ModelAssetResult GoldSrcStudioModelImporter::import_with_limits(
    const assets::AssetSource& source,
    const GoldSrcStudioModelImportLimits& limits) const
{
    return import_with_configuration(source, limits);
}

assets::ModelAssetResult GoldSrcStudioModelImporter::import_bundle(
    const assets::AssetSource& main_source,
    const GoldSrcStudioSourceBundleView& bundle) const
{
    return import_bundle_with_configuration(main_source, bundle, limits_);
}

assets::ModelAssetResult
GoldSrcStudioModelImporter::import_bundle_with_configuration(
    const assets::AssetSource& main_source,
    const GoldSrcStudioSourceBundleView& bundle,
    const GoldSrcStudioModelImportLimits& limits)
{
    if (bundle.main_source.size() != main_source.bytes().size() ||
        !std::equal(bundle.main_source.begin(), bundle.main_source.end(),
            main_source.bytes().begin())) {
        return assets::ModelAssetResult::failure(assets::AssetError{
            assets::AssetErrorCode::MalformedData,
            main_source.virtual_path(),
            std::string{kGoldSrcStudioModelImporterId},
            "Studio bundle main source does not match the approved source",
            {},
        });
    }
    auto parsed = GoldSrcStudioParser::parse(bundle, limits);
    if (!parsed) {
        return parser_failure(main_source, *parsed.error);
    }
    try {
        assets::ModelAsset model;
        model.identity.source_name = path_as_utf8(main_source.virtual_path());
        model.skeletal_data = std::make_shared<assets::SkeletalModelAssetData>(
            std::move(parsed.document->skeletal_model));
        return assets::ModelAssetResult::success(std::move(model));
    } catch (const std::exception& exception) {
        return assets::ModelAssetResult::failure(assets::AssetError{
            assets::AssetErrorCode::ImportFailed,
            main_source.virtual_path(),
            std::string{kGoldSrcStudioModelImporterId},
            std::string{"Unable to retain owning Studio model: "} + exception.what(),
            {},
        });
    } catch (...) {
        return assets::ModelAssetResult::failure(assets::AssetError{
            assets::AssetErrorCode::ImportFailed,
            main_source.virtual_path(),
            std::string{kGoldSrcStudioModelImporterId},
            "Unable to retain owning Studio model",
            {},
        });
    }
}

} // namespace hlclient::goldsrc::studio

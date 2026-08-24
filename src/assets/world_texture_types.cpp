#include <hlclient/assets/world_texture_types.hpp>

#include <limits>
#include <new>
#include <stdexcept>

namespace hlclient::assets {
namespace {

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

[[nodiscard]] WorldTextureSetCreateResult fail(
    const WorldTextureSetErrorCode code,
    std::optional<std::size_t> index,
    std::string context)
{
    return WorldTextureSetCreateResult{
        std::nullopt,
        WorldTextureSetError{code, index, std::move(context)},
    };
}

[[nodiscard]] bool valid_texture_profile(
    const WorldTextureAsset& texture) noexcept
{
    const bool source_kind_valid =
        texture.source_kind == WorldTextureSourceKind::embedded_bsp ||
        texture.source_kind == WorldTextureSourceKind::external_wad3;
    const bool alpha_mode_valid =
        texture.alpha_mode == WorldTextureAlphaMode::opaque ||
        texture.alpha_mode == WorldTextureAlphaMode::masked_index_255;
    return source_kind_valid && alpha_mode_valid &&
        texture.compatibility_profile ==
            WorldTextureCompatibilityProfile::goldsrc_indexed_miptex_v1 &&
        texture.evidence_profile == WorldTextureEvidenceProfile::
                                        valve_public_tools_and_synthetic_fixtures;
}

} // namespace

std::string_view to_string(const WorldTextureSetErrorCode code) noexcept
{
    switch (code) {
    case WorldTextureSetErrorCode::invalid_configuration:
        return "invalid_configuration";
    case WorldTextureSetErrorCode::texture_count_limit_exceeded:
        return "texture_count_limit_exceeded";
    case WorldTextureSetErrorCode::binding_count_mismatch:
        return "binding_count_mismatch";
    case WorldTextureSetErrorCode::archive_count_limit_exceeded:
        return "archive_count_limit_exceeded";
    case WorldTextureSetErrorCode::invalid_texture_asset:
        return "invalid_texture_asset";
    case WorldTextureSetErrorCode::invalid_material_binding:
        return "invalid_material_binding";
    case WorldTextureSetErrorCode::total_decoded_bytes_limit_exceeded:
        return "total_decoded_bytes_limit_exceeded";
    case WorldTextureSetErrorCode::unable_to_retain_texture_set:
        return "unable_to_retain_texture_set";
    }
    return "unknown";
}

WorldTextureSet::WorldTextureSet(
    std::vector<WorldTextureAsset> textures,
    std::vector<WorldMaterialTextureBinding> bindings,
    std::vector<WorldTextureArchiveMetadata> archive_metadata,
    WorldTextureSetStatistics statistics,
    const bool complete) noexcept
    : textures_{std::move(textures)},
      bindings_{std::move(bindings)},
      archive_metadata_{std::move(archive_metadata)},
      statistics_{statistics},
      complete_{complete}
{
}

WorldTextureSetCreateResult WorldTextureSet::create(
    std::vector<WorldTextureAsset> textures,
    std::vector<WorldMaterialTextureBinding> bindings,
    std::vector<WorldTextureArchiveMetadata> archive_metadata,
    const std::size_t expected_material_count,
    const WorldTextureSetLimits& limits)
{
    if (limits.maximum_texture_count == 0U ||
        limits.maximum_material_binding_count == 0U ||
        limits.maximum_archive_metadata_count == 0U ||
        limits.maximum_total_rgba_bytes == 0U) {
        return fail(WorldTextureSetErrorCode::invalid_configuration,
            std::nullopt,
            "World texture-set limits must all be positive");
    }
    if (textures.size() > limits.maximum_texture_count) {
        return fail(WorldTextureSetErrorCode::texture_count_limit_exceeded,
            textures.size(),
            "Texture asset count exceeds the configured limit");
    }
    if (expected_material_count > limits.maximum_material_binding_count ||
        bindings.size() != expected_material_count) {
        return fail(WorldTextureSetErrorCode::binding_count_mismatch,
            bindings.size(),
            "Material binding count does not equal the expected world material count");
    }
    if (archive_metadata.size() > limits.maximum_archive_metadata_count) {
        return fail(WorldTextureSetErrorCode::archive_count_limit_exceeded,
            archive_metadata.size(),
            "Archive metadata count exceeds the configured limit");
    }

    WorldTextureSetStatistics statistics;
    statistics.material_binding_count = bindings.size();
    statistics.decoded_texture_count = textures.size();
    statistics.wad_declaration_count = archive_metadata.size();
    std::size_t total_rgba_bytes = 0U;

    for (std::size_t texture_index = 0U; texture_index < textures.size(); ++texture_index) {
        const auto& texture = textures[texture_index];
        if (texture.name.empty() || texture.width == 0U || texture.height == 0U ||
            !valid_texture_profile(texture)) {
            return fail(WorldTextureSetErrorCode::invalid_texture_asset,
                texture_index,
                "Texture asset name, dimensions or closed compatibility profile is invalid");
        }
        for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
            const auto& mip = texture.mip_levels[level];
            const auto expected_width = texture.width >> level;
            const auto expected_height = texture.height >> level;
            std::size_t expected_pixels = 0U;
            std::size_t expected_bytes = 0U;
            if (expected_width == 0U || expected_height == 0U ||
                mip.width != expected_width || mip.height != expected_height ||
                mip.pixel_format != WorldTexturePixelFormat::rgba8 ||
                !checked_multiply(static_cast<std::size_t>(mip.width),
                    static_cast<std::size_t>(mip.height), expected_pixels) ||
                !checked_multiply(expected_pixels, 4U, expected_bytes) ||
                mip.rgba_pixels.size() != expected_bytes ||
                !checked_add(total_rgba_bytes, expected_bytes, total_rgba_bytes) ||
                total_rgba_bytes > limits.maximum_total_rgba_bytes) {
                return fail(
                    total_rgba_bytes > limits.maximum_total_rgba_bytes
                        ? WorldTextureSetErrorCode::total_decoded_bytes_limit_exceeded
                        : WorldTextureSetErrorCode::invalid_texture_asset,
                    texture_index,
                    "Texture mip dimensions or owning RGBA byte count are invalid");
            }
        }
        ++statistics.total_mip_level_count;
        statistics.total_mip_level_count += texture.mip_levels.size() - 1U;
        if (texture.source_kind == WorldTextureSourceKind::embedded_bsp) {
            ++statistics.embedded_texture_count;
        } else {
            ++statistics.wad3_texture_count;
        }
        if (texture.alpha_mode == WorldTextureAlphaMode::masked_index_255) {
            ++statistics.masked_texture_count;
        } else {
            ++statistics.opaque_texture_count;
        }
    }
    statistics.total_rgba_byte_count = total_rgba_bytes;

    try {
        std::vector<std::uint8_t> referenced_textures(
            textures.size(), std::uint8_t{0U});
        bool complete = true;
        for (std::size_t binding_index = 0U; binding_index < bindings.size();
             ++binding_index) {
            const auto& binding = bindings[binding_index];
            if (binding.material_index != binding_index) {
                return fail(WorldTextureSetErrorCode::invalid_material_binding,
                    binding_index,
                    "Material bindings must retain exact world material order");
            }
            if (binding.compatibility_profile !=
                    WorldTextureCompatibilityProfile::
                        goldsrc_indexed_miptex_v1 ||
                binding.evidence_profile != WorldTextureEvidenceProfile::
                                                valve_public_tools_and_synthetic_fixtures) {
                return fail(WorldTextureSetErrorCode::invalid_material_binding,
                    binding_index,
                    "Material binding compatibility or evidence profile is invalid");
            }
            const bool resolved = is_resolved(binding.status);
            if (resolved != binding.texture_asset_index.has_value() ||
                (binding.texture_asset_index &&
                    *binding.texture_asset_index >= textures.size())) {
                return fail(WorldTextureSetErrorCode::invalid_material_binding,
                    binding_index,
                    "Resolved binding status and texture asset index disagree");
            }
            if (resolved) {
                const auto texture_index = *binding.texture_asset_index;
                const auto& texture = textures[texture_index];
                if ((binding.status ==
                            WorldMaterialTextureBindingStatus::resolved_embedded &&
                        texture.source_kind !=
                            WorldTextureSourceKind::embedded_bsp) ||
                    (binding.status ==
                            WorldMaterialTextureBindingStatus::resolved_wad3 &&
                        texture.source_kind !=
                            WorldTextureSourceKind::external_wad3)) {
                    return fail(WorldTextureSetErrorCode::invalid_material_binding,
                        binding_index,
                        "Resolved binding source status disagrees with its texture asset");
                }
                referenced_textures[texture_index] = 1U;
            } else {
                complete = false;
                ++statistics.unresolved_material_count;
                if (binding.status ==
                    WorldMaterialTextureBindingStatus::
                        missing_bsp_texture_reference) {
                    ++statistics.missing_bsp_reference_count;
                }
                if (binding.status ==
                    WorldMaterialTextureBindingStatus::
                        external_texture_dimension_mismatch) {
                    ++statistics.dimension_mismatch_count;
                }
            }
        }
        for (std::size_t texture_index = 0U;
             texture_index < referenced_textures.size(); ++texture_index) {
            if (referenced_textures[texture_index] == 0U) {
                return fail(WorldTextureSetErrorCode::invalid_texture_asset,
                    texture_index,
                    "Every retained texture asset must be referenced by a material binding");
            }
        }
        for (const auto& archive : archive_metadata) {
            if (archive.status == WorldTextureArchiveStatus::resolved) {
                ++statistics.wad_archive_resolved_count;
            } else if (archive.status == WorldTextureArchiveStatus::missing) {
                ++statistics.wad_archive_missing_count;
            }
        }

        return WorldTextureSetCreateResult{
            WorldTextureSet{std::move(textures),
                std::move(bindings),
                std::move(archive_metadata),
                statistics,
                complete},
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(WorldTextureSetErrorCode::unable_to_retain_texture_set,
            std::nullopt,
            "Unable to retain the owning world texture set");
    } catch (const std::length_error&) {
        return fail(WorldTextureSetErrorCode::unable_to_retain_texture_set,
            std::nullopt,
            "World texture set exceeds an owning container limit");
    }
}

std::span<const WorldTextureAsset> WorldTextureSet::textures() const noexcept
{
    return textures_;
}

std::span<const WorldMaterialTextureBinding> WorldTextureSet::bindings() const noexcept
{
    return bindings_;
}

std::span<const WorldTextureArchiveMetadata> WorldTextureSet::archive_metadata()
    const noexcept
{
    return archive_metadata_;
}

std::size_t WorldTextureSet::texture_count() const noexcept
{
    return textures_.size();
}

std::size_t WorldTextureSet::binding_count() const noexcept
{
    return bindings_.size();
}

const WorldMaterialTextureBinding* WorldTextureSet::binding_for_material(
    const std::size_t material_index) const noexcept
{
    if (material_index >= bindings_.size() ||
        bindings_[material_index].material_index != material_index) {
        return nullptr;
    }
    return &bindings_[material_index];
}

bool WorldTextureSet::complete_for_world_materials() const noexcept
{
    return complete_;
}

const WorldTextureSetStatistics& WorldTextureSet::statistics() const noexcept
{
    return statistics_;
}

} // namespace hlclient::assets

#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::assets {

inline constexpr std::size_t kWorldTextureMipLevelCount = 4U;

enum class WorldTexturePixelFormat {
    rgba8,
};

enum class WorldTextureSourceKind {
    embedded_bsp,
    external_wad3,
};

enum class WorldTextureAlphaMode {
    opaque,
    masked_index_255,
};

enum class WorldTextureCompatibilityProfile {
    goldsrc_indexed_miptex_v1,
};

enum class WorldTextureEvidenceProfile {
    valve_public_tools_and_synthetic_fixtures,
};

struct WorldTextureMipLevel {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    WorldTexturePixelFormat pixel_format{WorldTexturePixelFormat::rgba8};
    std::vector<std::byte> rgba_pixels;
};

struct WorldTextureAsset {
    std::string name;
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::array<WorldTextureMipLevel, kWorldTextureMipLevelCount> mip_levels{};
    WorldTextureSourceKind source_kind{WorldTextureSourceKind::embedded_bsp};
    WorldTextureAlphaMode alpha_mode{WorldTextureAlphaMode::opaque};
    std::optional<std::uint32_t> source_bsp_texture_index;
    std::optional<std::uint32_t> source_archive_ordinal;
    WorldTextureCompatibilityProfile compatibility_profile{
        WorldTextureCompatibilityProfile::goldsrc_indexed_miptex_v1};
    WorldTextureEvidenceProfile evidence_profile{
        WorldTextureEvidenceProfile::valve_public_tools_and_synthetic_fixtures};
};

enum class WorldMaterialTextureBindingStatus {
    resolved_embedded,
    resolved_wad3,
    missing_bsp_texture_reference,
    external_wad_list_missing,
    external_wad_archive_missing,
    external_texture_not_found,
    external_texture_dimension_mismatch,
    malformed_embedded_texture,
    malformed_wad_texture,
    unsupported_texture_profile,
};

struct WorldMaterialTextureBinding {
    std::size_t material_index{0U};
    WorldMaterialTextureBindingStatus status{
        WorldMaterialTextureBindingStatus::missing_bsp_texture_reference};
    std::optional<std::size_t> texture_asset_index;
    std::optional<std::uint32_t> source_bsp_texture_index;
    std::optional<std::uint32_t> source_archive_ordinal;
    WorldTextureCompatibilityProfile compatibility_profile{
        WorldTextureCompatibilityProfile::goldsrc_indexed_miptex_v1};
    WorldTextureEvidenceProfile evidence_profile{
        WorldTextureEvidenceProfile::valve_public_tools_and_synthetic_fixtures};
};

enum class WorldTextureArchiveStatus {
    not_required,
    resolved,
    missing,
    malformed,
    unsupported_profile,
};

struct WorldTextureArchiveMetadata {
    std::uint32_t declaration_ordinal{0U};
    std::size_t basename_byte_count{0U};
    std::optional<std::uint32_t> source_root_ordinal;
    WorldTextureArchiveStatus status{WorldTextureArchiveStatus::missing};
    std::size_t catalog_entry_count{0U};
    std::size_t textures_supplied_count{0U};
    std::size_t source_byte_count{0U};
    WorldTextureCompatibilityProfile compatibility_profile{
        WorldTextureCompatibilityProfile::goldsrc_indexed_miptex_v1};
    WorldTextureEvidenceProfile evidence_profile{
        WorldTextureEvidenceProfile::valve_public_tools_and_synthetic_fixtures};
};

struct WorldTextureSetStatistics {
    std::size_t material_binding_count{0U};
    std::size_t decoded_texture_count{0U};
    std::size_t embedded_texture_count{0U};
    std::size_t wad3_texture_count{0U};
    std::size_t masked_texture_count{0U};
    std::size_t opaque_texture_count{0U};
    std::size_t total_mip_level_count{0U};
    std::size_t total_rgba_byte_count{0U};
    std::size_t wad_declaration_count{0U};
    std::size_t wad_archive_resolved_count{0U};
    std::size_t wad_archive_missing_count{0U};
    std::size_t unresolved_material_count{0U};
    std::size_t missing_bsp_reference_count{0U};
    std::size_t dimension_mismatch_count{0U};
};

struct WorldTextureSetLimits {
    std::size_t maximum_texture_count{512U};
    std::size_t maximum_material_binding_count{8'192U};
    std::size_t maximum_archive_metadata_count{128U};
    std::size_t maximum_total_rgba_bytes{256U * 1024U * 1024U};
};

enum class WorldTextureSetErrorCode {
    invalid_configuration,
    texture_count_limit_exceeded,
    binding_count_mismatch,
    archive_count_limit_exceeded,
    invalid_texture_asset,
    invalid_material_binding,
    total_decoded_bytes_limit_exceeded,
    unable_to_retain_texture_set,
};

[[nodiscard]] std::string_view to_string(WorldTextureSetErrorCode code) noexcept;

struct WorldTextureSetError {
    WorldTextureSetErrorCode code{WorldTextureSetErrorCode::invalid_configuration};
    std::optional<std::size_t> element_index;
    std::string context;
};

struct WorldTextureSetCreateResult;

// Immutable, owning, format-neutral CPU texture snapshot. Construction is
// transactional and validates every mip buffer and material binding before
// publishing the state.
class WorldTextureSet final {
public:
    [[nodiscard]] static WorldTextureSetCreateResult create(
        std::vector<WorldTextureAsset> textures,
        std::vector<WorldMaterialTextureBinding> bindings,
        std::vector<WorldTextureArchiveMetadata> archive_metadata,
        std::size_t expected_material_count,
        const WorldTextureSetLimits& limits = {});

    WorldTextureSet(const WorldTextureSet&) = default;
    WorldTextureSet(WorldTextureSet&&) noexcept = default;
    WorldTextureSet& operator=(const WorldTextureSet&) = delete;
    WorldTextureSet& operator=(WorldTextureSet&&) noexcept = delete;
    ~WorldTextureSet() = default;

    [[nodiscard]] std::span<const WorldTextureAsset> textures() const noexcept;
    [[nodiscard]] std::span<const WorldMaterialTextureBinding> bindings() const noexcept;
    [[nodiscard]] std::span<const WorldTextureArchiveMetadata> archive_metadata()
        const noexcept;
    [[nodiscard]] std::size_t texture_count() const noexcept;
    [[nodiscard]] std::size_t binding_count() const noexcept;
    [[nodiscard]] const WorldMaterialTextureBinding* binding_for_material(
        std::size_t material_index) const noexcept;
    [[nodiscard]] bool complete_for_world_materials() const noexcept;
    [[nodiscard]] const WorldTextureSetStatistics& statistics() const noexcept;

private:
    WorldTextureSet(
        std::vector<WorldTextureAsset> textures,
        std::vector<WorldMaterialTextureBinding> bindings,
        std::vector<WorldTextureArchiveMetadata> archive_metadata,
        WorldTextureSetStatistics statistics,
        bool complete) noexcept;

    std::vector<WorldTextureAsset> textures_;
    std::vector<WorldMaterialTextureBinding> bindings_;
    std::vector<WorldTextureArchiveMetadata> archive_metadata_;
    WorldTextureSetStatistics statistics_{};
    bool complete_{false};
};

struct WorldTextureSetCreateResult {
    std::optional<WorldTextureSet> texture_set;
    std::optional<WorldTextureSetError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return texture_set.has_value();
    }
};

struct TexturedWorldAsset {
    WorldAsset world;
    WorldTextureSet textures;
};

[[nodiscard]] constexpr bool is_resolved(
    const WorldMaterialTextureBindingStatus status) noexcept
{
    return status == WorldMaterialTextureBindingStatus::resolved_embedded ||
        status == WorldMaterialTextureBindingStatus::resolved_wad3;
}

} // namespace hlclient::assets

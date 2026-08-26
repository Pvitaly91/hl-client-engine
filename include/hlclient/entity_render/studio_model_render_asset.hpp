#pragma once

#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/entity_render/entity_render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::entity_render {

struct StudioRenderVertex {
    assets::AssetVector3 bone_local_position{};
    assets::AssetVector3 bone_local_normal{};
    std::int16_t raw_texture_s{0};
    std::int16_t raw_texture_t{0};
    std::uint32_t position_bone_index{0U};
    std::uint32_t normal_bone_index{0U};
};

struct StudioRenderMesh {
    std::uint32_t first_index{0U};
    std::uint32_t index_count{0U};
    std::uint32_t skin_reference_slot{0U};
    std::uint32_t submodel_index{0U};
    std::uint32_t source_mesh_ordinal{0U};
};

struct StudioRenderSubmodel {
    std::uint32_t first_vertex{0U};
    std::uint32_t vertex_count{0U};
    std::uint32_t first_index{0U};
    std::uint32_t index_count{0U};
    std::uint32_t first_mesh{0U};
    std::uint32_t mesh_count{0U};
    std::uint32_t source_model_ordinal{0U};
    assets::ModelBounds bounds{};
};

struct StudioRenderBodyPart {
    std::int32_t base{0};
    std::vector<std::uint32_t> submodel_indices;
};

enum class StudioRenderMaterialProfile {
    opaque,
    masked,
    unsupported,
};

enum class StudioRenderMaterialSupportStatus {
    supported_opaque,
    supported_masked,
    unsupported_chrome,
    unsupported_additive,
    unsupported_alpha,
    unsupported_unknown_bits,
    unsupported_multiple_features,
};

struct StudioRenderMaterial {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::vector<std::byte> rgba8_level_zero;
    std::uint32_t source_flags{0U};
    std::uint32_t unknown_rendering_bits{0U};
    bool no_mipmaps{false};
    bool fullbright_metadata{false};
    bool flatshade_metadata{false};
    StudioRenderMaterialProfile profile{StudioRenderMaterialProfile::opaque};
    StudioRenderMaterialSupportStatus support_status{
        StudioRenderMaterialSupportStatus::supported_opaque};
};

struct StudioModelRenderStatistics {
    std::size_t bone_count{0U};
    std::size_t bodypart_count{0U};
    std::size_t submodel_count{0U};
    std::size_t mesh_count{0U};
    std::size_t vertex_count{0U};
    std::size_t index_count{0U};
    std::size_t material_count{0U};
    std::size_t skin_family_count{0U};
    std::size_t supported_material_count{0U};
    std::size_t unsupported_material_count{0U};
    std::size_t geometry_bytes{0U};
    std::size_t texture_rgba_bytes{0U};
    std::size_t total_gpu_source_bytes{0U};
};

enum class StudioModelRenderAssetErrorCode {
    invalid_configuration,
    invalid_source_identity,
    missing_skeletal_data,
    invalid_geometry,
    invalid_bodypart,
    invalid_mesh,
    invalid_material,
    invalid_skin_family,
    source_limit_exceeded,
    unable_to_retain_asset,
};

[[nodiscard]] std::string_view to_string(
    StudioModelRenderAssetErrorCode code) noexcept;

struct StudioModelRenderAssetError {
    StudioModelRenderAssetErrorCode code{
        StudioModelRenderAssetErrorCode::invalid_configuration};
    std::optional<std::size_t> element_index;
    std::string context;
};

enum class StudioRenderSelectionErrorCode {
    invalid_body_value,
    invalid_bodypart,
    invalid_submodel,
    invalid_mesh,
    invalid_skin_family,
    invalid_skin_reference,
    invalid_material,
};

struct StudioSubmodelSelectionResult {
    std::vector<std::uint32_t> submodel_indices;
    std::optional<StudioRenderSelectionErrorCode> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

struct StudioMaterialSelectionResult {
    std::optional<std::uint32_t> material_index;
    std::optional<StudioRenderSelectionErrorCode> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return material_index.has_value();
    }
};

class StudioModelRenderAsset;
struct StudioModelRenderAssetBuildResult;

class StudioModelRenderAsset final {
public:
    StudioModelRenderAsset(const StudioModelRenderAsset&) = delete;
    StudioModelRenderAsset& operator=(const StudioModelRenderAsset&) = delete;
    StudioModelRenderAsset(StudioModelRenderAsset&&) noexcept = default;
    StudioModelRenderAsset& operator=(StudioModelRenderAsset&&) = delete;
    ~StudioModelRenderAsset() = default;

    [[nodiscard]] EntityRenderResourceIdentity source_identity() const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;
    [[nodiscard]] std::span<const StudioRenderVertex> vertices() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
    [[nodiscard]] std::span<const StudioRenderMesh> meshes() const noexcept;
    [[nodiscard]] std::span<const StudioRenderSubmodel> submodels() const noexcept;
    [[nodiscard]] std::span<const StudioRenderBodyPart> bodyparts() const noexcept;
    [[nodiscard]] std::span<const StudioRenderMaterial> materials() const noexcept;
    [[nodiscard]] std::span<const std::vector<std::uint16_t>> skin_families()
        const noexcept;
    [[nodiscard]] const assets::ModelBounds& bounds() const noexcept;
    [[nodiscard]] const StudioModelRenderStatistics& statistics() const noexcept;

    // Bodypart selection follows the retained Studio base/count metadata and
    // returns exactly one submodel per bodypart. It never selects all variants.
    [[nodiscard]] StudioSubmodelSelectionResult select_submodels(
        std::uint32_t body_value) const;

    // UV normalization remains a draw-time operation because this lookup can
    // select textures with different dimensions across skin families.
    [[nodiscard]] StudioMaterialSelectionResult select_material(
        std::uint32_t mesh_index,
        std::uint32_t skin_family_index) const noexcept;

private:
    friend class StudioModelRenderAssetBuilder;

    StudioModelRenderAsset(
        EntityRenderResourceIdentity source_identity,
        std::uint64_t render_revision,
        std::vector<StudioRenderVertex> vertices,
        std::vector<std::uint32_t> indices,
        std::vector<StudioRenderMesh> meshes,
        std::vector<StudioRenderSubmodel> submodels,
        std::vector<StudioRenderBodyPart> bodyparts,
        std::vector<StudioRenderMaterial> materials,
        std::vector<std::vector<std::uint16_t>> skin_families,
        assets::ModelBounds bounds,
        StudioModelRenderStatistics statistics) noexcept;

    EntityRenderResourceIdentity source_identity_{};
    std::uint64_t render_revision_{0U};
    std::vector<StudioRenderVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<StudioRenderMesh> meshes_;
    std::vector<StudioRenderSubmodel> submodels_;
    std::vector<StudioRenderBodyPart> bodyparts_;
    std::vector<StudioRenderMaterial> materials_;
    std::vector<std::vector<std::uint16_t>> skin_families_;
    assets::ModelBounds bounds_{};
    StudioModelRenderStatistics statistics_{};
};

struct StudioModelRenderAssetBuildResult {
    std::optional<StudioModelRenderAsset> asset;
    std::optional<StudioModelRenderAssetError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return asset.has_value();
    }
};

class StudioModelRenderAssetBuilder final {
public:
    [[nodiscard]] StudioModelRenderAssetBuildResult build(
        const assets::ModelAsset& source,
        EntityRenderResourceIdentity source_identity,
        const RuntimeEntityVisualLimits& limits = {}) const;
};

} // namespace hlclient::entity_render

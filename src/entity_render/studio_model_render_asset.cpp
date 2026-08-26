#include <hlclient/entity_render/studio_model_render_asset.hpp>

#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace hlclient::entity_render {
namespace {

class StableHasher final {
public:
    void add(const std::uint64_t value) noexcept
    {
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
            value_ ^= static_cast<std::uint8_t>(value >> (index * 8U));
            value_ *= 1'099'511'628'211ULL;
        }
    }

    void add(const std::uint32_t value) noexcept
    {
        add(static_cast<std::uint64_t>(value));
    }

    void add(const std::int32_t value) noexcept
    {
        add(static_cast<std::uint32_t>(value));
    }

    void add(const std::uint16_t value) noexcept
    {
        add(static_cast<std::uint32_t>(value));
    }

    void add(const std::int16_t value) noexcept
    {
        add(static_cast<std::uint16_t>(value));
    }

    void add(const float value) noexcept
    {
        add(std::bit_cast<std::uint32_t>(value));
    }

    void add_bytes(const std::span<const std::byte> bytes) noexcept
    {
        for (const auto byte : bytes) {
            value_ ^= std::to_integer<std::uint8_t>(byte);
            value_ *= 1'099'511'628'211ULL;
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? 1U : value_;
    }

private:
    std::uint64_t value_{14'695'981'039'346'656'037ULL};
};

[[nodiscard]] StudioModelRenderAssetBuildResult fail(
    const StudioModelRenderAssetErrorCode code,
    const std::optional<std::size_t> element_index,
    std::string context)
{
    return {
        std::nullopt,
        StudioModelRenderAssetError{code, element_index, std::move(context)},
    };
}

[[nodiscard]] bool add_size(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool multiply_size(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite_bounds(const assets::ModelBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

void hash_vector(StableHasher& hash, const assets::AssetVector3& value) noexcept
{
    hash.add(value.x);
    hash.add(value.y);
    hash.add(value.z);
}

[[nodiscard]] StudioRenderMaterialSupportStatus material_support(
    const std::uint32_t flags) noexcept
{
    using namespace goldsrc::studio;
    const auto unknown = flags & ~kGoldSrcStudioKnownTextureFlags;
    const bool chrome = (flags & kGoldSrcStudioTextureChrome) != 0U;
    const bool additive = (flags & kGoldSrcStudioTextureAdditive) != 0U;
    const bool alpha = (flags & kGoldSrcStudioTextureAlpha) != 0U;
    const auto unsupported_count = static_cast<unsigned>(chrome) +
        static_cast<unsigned>(additive) + static_cast<unsigned>(alpha) +
        static_cast<unsigned>(unknown != 0U);
    if (unsupported_count > 1U) {
        return StudioRenderMaterialSupportStatus::unsupported_multiple_features;
    }
    if (chrome) {
        return StudioRenderMaterialSupportStatus::unsupported_chrome;
    }
    if (additive) {
        return StudioRenderMaterialSupportStatus::unsupported_additive;
    }
    if (alpha) {
        return StudioRenderMaterialSupportStatus::unsupported_alpha;
    }
    if (unknown != 0U) {
        return StudioRenderMaterialSupportStatus::unsupported_unknown_bits;
    }
    if ((flags & kGoldSrcStudioTextureMasked) != 0U) {
        return StudioRenderMaterialSupportStatus::supported_masked;
    }
    return StudioRenderMaterialSupportStatus::supported_opaque;
}

[[nodiscard]] bool supported(
    const StudioRenderMaterialSupportStatus status) noexcept
{
    return status == StudioRenderMaterialSupportStatus::supported_opaque ||
        status == StudioRenderMaterialSupportStatus::supported_masked;
}

} // namespace

std::string_view to_string(const StudioModelRenderAssetErrorCode code) noexcept
{
    switch (code) {
    case StudioModelRenderAssetErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StudioModelRenderAssetErrorCode::invalid_source_identity:
        return "invalid_source_identity";
    case StudioModelRenderAssetErrorCode::missing_skeletal_data:
        return "missing_skeletal_data";
    case StudioModelRenderAssetErrorCode::invalid_geometry:
        return "invalid_geometry";
    case StudioModelRenderAssetErrorCode::invalid_bodypart:
        return "invalid_bodypart";
    case StudioModelRenderAssetErrorCode::invalid_mesh:
        return "invalid_mesh";
    case StudioModelRenderAssetErrorCode::invalid_material:
        return "invalid_material";
    case StudioModelRenderAssetErrorCode::invalid_skin_family:
        return "invalid_skin_family";
    case StudioModelRenderAssetErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case StudioModelRenderAssetErrorCode::unable_to_retain_asset:
        return "unable_to_retain_asset";
    }
    return "unknown";
}

StudioModelRenderAsset::StudioModelRenderAsset(
    const EntityRenderResourceIdentity source_identity,
    const std::uint64_t render_revision,
    std::vector<StudioRenderVertex> vertices,
    std::vector<std::uint32_t> indices,
    std::vector<StudioRenderMesh> meshes,
    std::vector<StudioRenderSubmodel> submodels,
    std::vector<StudioRenderBodyPart> bodyparts,
    std::vector<StudioRenderMaterial> materials,
    std::vector<std::vector<std::uint16_t>> skin_families,
    const assets::ModelBounds bounds,
    const StudioModelRenderStatistics statistics) noexcept
    : source_identity_(source_identity),
      render_revision_(render_revision),
      vertices_(std::move(vertices)),
      indices_(std::move(indices)),
      meshes_(std::move(meshes)),
      submodels_(std::move(submodels)),
      bodyparts_(std::move(bodyparts)),
      materials_(std::move(materials)),
      skin_families_(std::move(skin_families)),
      bounds_(bounds),
      statistics_(statistics)
{
}

EntityRenderResourceIdentity StudioModelRenderAsset::source_identity() const noexcept
{
    return source_identity_;
}

std::uint64_t StudioModelRenderAsset::resource_id() const noexcept
{
    return source_identity_.resource_id;
}

std::uint64_t StudioModelRenderAsset::resource_revision() const noexcept
{
    return render_revision_;
}

std::span<const StudioRenderVertex> StudioModelRenderAsset::vertices() const noexcept
{
    return vertices_;
}

std::span<const std::uint32_t> StudioModelRenderAsset::indices() const noexcept
{
    return indices_;
}

std::span<const StudioRenderMesh> StudioModelRenderAsset::meshes() const noexcept
{
    return meshes_;
}

std::span<const StudioRenderSubmodel> StudioModelRenderAsset::submodels() const noexcept
{
    return submodels_;
}

std::span<const StudioRenderBodyPart> StudioModelRenderAsset::bodyparts() const noexcept
{
    return bodyparts_;
}

std::span<const StudioRenderMaterial> StudioModelRenderAsset::materials() const noexcept
{
    return materials_;
}

std::span<const std::vector<std::uint16_t>>
StudioModelRenderAsset::skin_families() const noexcept
{
    return skin_families_;
}

const assets::ModelBounds& StudioModelRenderAsset::bounds() const noexcept
{
    return bounds_;
}

const StudioModelRenderStatistics& StudioModelRenderAsset::statistics() const noexcept
{
    return statistics_;
}

StudioSubmodelSelectionResult StudioModelRenderAsset::select_submodels(
    const std::uint32_t body_value) const
{
    StudioSubmodelSelectionResult result;
    try {
        result.submodel_indices.reserve(bodyparts_.size());
        for (const auto& bodypart : bodyparts_) {
            if (bodypart.base <= 0 || bodypart.submodel_indices.empty()) {
                result.submodel_indices.clear();
                result.error = StudioRenderSelectionErrorCode::invalid_bodypart;
                return result;
            }
            const auto selection =
                (body_value / static_cast<std::uint32_t>(bodypart.base)) %
                static_cast<std::uint32_t>(bodypart.submodel_indices.size());
            const auto submodel_index = bodypart.submodel_indices[selection];
            if (static_cast<std::size_t>(submodel_index) >= submodels_.size()) {
                result.submodel_indices.clear();
                result.error = StudioRenderSelectionErrorCode::invalid_submodel;
                return result;
            }
            result.submodel_indices.push_back(submodel_index);
        }
    } catch (const std::bad_alloc&) {
        result.submodel_indices.clear();
        result.error = StudioRenderSelectionErrorCode::invalid_bodypart;
    }
    return result;
}

StudioMaterialSelectionResult StudioModelRenderAsset::select_material(
    const std::uint32_t mesh_index,
    const std::uint32_t skin_family_index) const noexcept
{
    if (static_cast<std::size_t>(mesh_index) >= meshes_.size()) {
        return {std::nullopt, StudioRenderSelectionErrorCode::invalid_mesh};
    }
    if (static_cast<std::size_t>(skin_family_index) >= skin_families_.size()) {
        return {std::nullopt, StudioRenderSelectionErrorCode::invalid_skin_family};
    }
    const auto skin_slot = meshes_[mesh_index].skin_reference_slot;
    const auto& family = skin_families_[skin_family_index];
    if (static_cast<std::size_t>(skin_slot) >= family.size()) {
        return {std::nullopt, StudioRenderSelectionErrorCode::invalid_skin_reference};
    }
    const auto material_index = static_cast<std::uint32_t>(family[skin_slot]);
    if (static_cast<std::size_t>(material_index) >= materials_.size()) {
        return {std::nullopt, StudioRenderSelectionErrorCode::invalid_material};
    }
    return {material_index, std::nullopt};
}

StudioModelRenderAssetBuildResult StudioModelRenderAssetBuilder::build(
    const assets::ModelAsset& source,
    const EntityRenderResourceIdentity source_identity,
    const RuntimeEntityVisualLimits& limits) const
{
    using namespace goldsrc::studio;

    if (!valid_runtime_entity_visual_limits(limits)) {
        return fail(StudioModelRenderAssetErrorCode::invalid_configuration,
            std::nullopt,
            "Runtime entity visual limits are invalid or exceed hard caps");
    }
    if (source_identity.resource_id == 0U || source_identity.revision == 0U) {
        return fail(StudioModelRenderAssetErrorCode::invalid_source_identity,
            std::nullopt,
            "Studio source identity and revision must both be nonzero");
    }
    if (!source.skeletal_data) {
        return fail(StudioModelRenderAssetErrorCode::missing_skeletal_data,
            std::nullopt,
            "Model asset has no immutable skeletal payload");
    }

    const auto& skeletal = *source.skeletal_data;
    if (skeletal.bones.empty() || skeletal.submodels.empty() ||
        skeletal.bodyparts.empty() || skeletal.textures.empty() ||
        skeletal.skin_families.empty()) {
        return fail(StudioModelRenderAssetErrorCode::invalid_geometry,
            std::nullopt,
            "Studio render source is missing required skeleton, geometry, or material tables");
    }
    if (!finite_bounds(skeletal.source_clipping_bounds)) {
        return fail(StudioModelRenderAssetErrorCode::invalid_geometry,
            std::nullopt,
            "Studio source clipping bounds are invalid");
    }

    try {
        std::size_t total_vertices = 0U;
        std::size_t total_indices = 0U;
        std::size_t total_meshes = 0U;
        for (std::size_t index = 0U; index < skeletal.submodels.size(); ++index) {
            const auto& submodel = skeletal.submodels[index];
            if (!finite_bounds(submodel.bounds) ||
                submodel.vertices.size() >
                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
                submodel.indices.size() >
                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                return fail(StudioModelRenderAssetErrorCode::invalid_geometry,
                    index,
                    "Submodel bounds or cardinalities are invalid");
            }
            if (!add_size(total_vertices, submodel.vertices.size(), total_vertices) ||
                !add_size(total_indices, submodel.indices.size(), total_indices) ||
                !add_size(total_meshes, submodel.meshes.size(), total_meshes) ||
                total_vertices > std::numeric_limits<std::uint32_t>::max() ||
                total_indices > std::numeric_limits<std::uint32_t>::max() ||
                total_meshes > std::numeric_limits<std::uint32_t>::max()) {
                return fail(StudioModelRenderAssetErrorCode::source_limit_exceeded,
                    index,
                    "Aggregate Studio geometry exceeds indexable limits");
            }
        }

        std::size_t vertex_bytes = 0U;
        std::size_t index_bytes = 0U;
        if (!multiply_size(total_vertices, sizeof(StudioRenderVertex), vertex_bytes) ||
            !multiply_size(total_indices, sizeof(std::uint32_t), index_bytes)) {
            return fail(StudioModelRenderAssetErrorCode::source_limit_exceeded,
                std::nullopt,
                "Studio geometry byte count overflows");
        }
        std::size_t geometry_bytes = 0U;
        if (!add_size(vertex_bytes, index_bytes, geometry_bytes)) {
            return fail(StudioModelRenderAssetErrorCode::source_limit_exceeded,
                std::nullopt,
                "Studio geometry byte count overflows");
        }

        std::vector<StudioRenderMaterial> materials;
        materials.reserve(skeletal.textures.size());
        std::size_t texture_bytes = 0U;
        std::size_t supported_material_count = 0U;
        for (std::size_t index = 0U; index < skeletal.textures.size(); ++index) {
            const auto& texture = skeletal.textures[index];
            std::size_t pixel_count = 0U;
            std::size_t expected_rgba_bytes = 0U;
            if (texture.width == 0U || texture.height == 0U ||
                !multiply_size(texture.width, texture.height, pixel_count) ||
                !multiply_size(pixel_count, 4U, expected_rgba_bytes) ||
                texture.rgba8_level_zero.size() != expected_rgba_bytes ||
                !add_size(texture_bytes, expected_rgba_bytes, texture_bytes)) {
                return fail(StudioModelRenderAssetErrorCode::invalid_material,
                    index,
                    "Studio texture has invalid dimensions or RGBA byte count");
            }
            const auto support_status = material_support(texture.source_flags);
            const auto is_supported = supported(support_status);
            supported_material_count += static_cast<std::size_t>(is_supported);
            StudioRenderMaterial material;
            material.width = texture.width;
            material.height = texture.height;
            material.rgba8_level_zero = texture.rgba8_level_zero;
            material.source_flags = texture.source_flags;
            material.unknown_rendering_bits =
                texture.source_flags & ~kGoldSrcStudioKnownTextureFlags;
            material.no_mipmaps =
                (texture.source_flags & kGoldSrcStudioTextureNoMips) != 0U;
            material.fullbright_metadata =
                (texture.source_flags & kGoldSrcStudioTextureFullbright) != 0U;
            material.flatshade_metadata =
                (texture.source_flags & kGoldSrcStudioTextureFlatshade) != 0U;
            material.support_status = support_status;
            if (!is_supported) {
                material.profile = StudioRenderMaterialProfile::unsupported;
            } else if (support_status ==
                StudioRenderMaterialSupportStatus::supported_masked) {
                material.profile = StudioRenderMaterialProfile::masked;
            } else {
                material.profile = StudioRenderMaterialProfile::opaque;
            }
            materials.push_back(std::move(material));
        }

        std::size_t gpu_bytes = 0U;
        if (!add_size(geometry_bytes, texture_bytes, gpu_bytes) ||
            gpu_bytes > limits.maximum_model_gpu_bytes) {
            return fail(StudioModelRenderAssetErrorCode::source_limit_exceeded,
                std::nullopt,
                "Studio immutable GPU-source bytes exceed the configured limit");
        }

        std::vector<std::vector<std::uint16_t>> skin_families;
        skin_families.reserve(skeletal.skin_families.size());
        std::optional<std::size_t> skin_reference_count;
        for (std::size_t index = 0U; index < skeletal.skin_families.size(); ++index) {
            const auto& family = skeletal.skin_families[index].texture_indices;
            if (family.empty() ||
                (skin_reference_count && *skin_reference_count != family.size())) {
                return fail(StudioModelRenderAssetErrorCode::invalid_skin_family,
                    index,
                    "Studio skin families must have one common nonzero slot count");
            }
            skin_reference_count = family.size();
            for (const auto material_index : family) {
                if (static_cast<std::size_t>(material_index) >= materials.size()) {
                    return fail(StudioModelRenderAssetErrorCode::invalid_skin_family,
                        index,
                        "Studio skin family references an unavailable material");
                }
            }
            skin_families.push_back(family);
        }

        std::vector<StudioRenderBodyPart> bodyparts;
        bodyparts.reserve(skeletal.bodyparts.size());
        for (std::size_t index = 0U; index < skeletal.bodyparts.size(); ++index) {
            const auto& bodypart = skeletal.bodyparts[index];
            if (bodypart.base <= 0 || bodypart.submodel_indices.empty()) {
                return fail(StudioModelRenderAssetErrorCode::invalid_bodypart,
                    index,
                    "Studio bodypart requires a positive base and submodels");
            }
            for (const auto submodel_index : bodypart.submodel_indices) {
                if (static_cast<std::size_t>(submodel_index) >=
                    skeletal.submodels.size()) {
                    return fail(StudioModelRenderAssetErrorCode::invalid_bodypart,
                        index,
                        "Studio bodypart references an unavailable submodel");
                }
            }
            bodyparts.push_back({bodypart.base, bodypart.submodel_indices});
        }

        std::vector<StudioRenderVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<StudioRenderMesh> meshes;
        std::vector<StudioRenderSubmodel> submodels;
        vertices.reserve(total_vertices);
        indices.reserve(total_indices);
        meshes.reserve(total_meshes);
        submodels.reserve(skeletal.submodels.size());

        for (std::size_t submodel_index = 0U;
             submodel_index < skeletal.submodels.size();
             ++submodel_index) {
            const auto& source_submodel = skeletal.submodels[submodel_index];
            const auto vertex_base = static_cast<std::uint32_t>(vertices.size());
            const auto index_base = static_cast<std::uint32_t>(indices.size());
            const auto mesh_base = static_cast<std::uint32_t>(meshes.size());

            for (const auto& vertex : source_submodel.vertices) {
                if (!finite_vector(vertex.source_position) ||
                    !finite_vector(vertex.source_normal) ||
                    static_cast<std::size_t>(vertex.position_bone_index) >=
                        skeletal.bones.size() ||
                    static_cast<std::size_t>(vertex.normal_bone_index) >=
                        skeletal.bones.size()) {
                    return fail(StudioModelRenderAssetErrorCode::invalid_geometry,
                        submodel_index,
                        "Studio vertex is non-finite or has an invalid bone index");
                }
                vertices.push_back({
                    vertex.source_position,
                    vertex.source_normal,
                    vertex.raw_texture_s,
                    vertex.raw_texture_t,
                    vertex.position_bone_index,
                    vertex.normal_bone_index,
                });
            }
            for (const auto local_index : source_submodel.indices) {
                if (static_cast<std::size_t>(local_index) >=
                    source_submodel.vertices.size() ||
                    local_index >
                        std::numeric_limits<std::uint32_t>::max() - vertex_base) {
                    return fail(StudioModelRenderAssetErrorCode::invalid_geometry,
                        submodel_index,
                        "Studio index references an unavailable local vertex");
                }
                indices.push_back(vertex_base + local_index);
            }
            for (const auto& mesh : source_submodel.meshes) {
                const auto mesh_end = static_cast<std::uint64_t>(mesh.first_index) +
                    mesh.index_count;
                if (mesh.index_count == 0U || mesh.index_count % 3U != 0U ||
                    mesh_end > source_submodel.indices.size() ||
                    !skin_reference_count ||
                    static_cast<std::size_t>(mesh.skin_reference_slot) >=
                        *skin_reference_count) {
                    return fail(StudioModelRenderAssetErrorCode::invalid_mesh,
                        submodel_index,
                        "Studio mesh range or skin-reference slot is invalid");
                }
                meshes.push_back({
                    index_base + mesh.first_index,
                    mesh.index_count,
                    mesh.skin_reference_slot,
                    static_cast<std::uint32_t>(submodel_index),
                    mesh.source_mesh_ordinal,
                });
            }
            submodels.push_back({
                vertex_base,
                static_cast<std::uint32_t>(source_submodel.vertices.size()),
                index_base,
                static_cast<std::uint32_t>(source_submodel.indices.size()),
                mesh_base,
                static_cast<std::uint32_t>(source_submodel.meshes.size()),
                source_submodel.source_model_ordinal,
                source_submodel.bounds,
            });
        }

        StableHasher revision_hash;
        revision_hash.add(source_identity.resource_id);
        revision_hash.add(source_identity.revision);
        revision_hash.add(static_cast<std::uint64_t>(vertices.size()));
        for (const auto& vertex : vertices) {
            hash_vector(revision_hash, vertex.bone_local_position);
            hash_vector(revision_hash, vertex.bone_local_normal);
            revision_hash.add(vertex.raw_texture_s);
            revision_hash.add(vertex.raw_texture_t);
            revision_hash.add(vertex.position_bone_index);
            revision_hash.add(vertex.normal_bone_index);
        }
        revision_hash.add(static_cast<std::uint64_t>(indices.size()));
        for (const auto index : indices) {
            revision_hash.add(index);
        }
        for (const auto& mesh : meshes) {
            revision_hash.add(mesh.first_index);
            revision_hash.add(mesh.index_count);
            revision_hash.add(mesh.skin_reference_slot);
            revision_hash.add(mesh.submodel_index);
        }
        for (const auto& material : materials) {
            revision_hash.add(material.width);
            revision_hash.add(material.height);
            revision_hash.add(material.source_flags);
            revision_hash.add_bytes(material.rgba8_level_zero);
        }
        for (const auto& family : skin_families) {
            revision_hash.add(static_cast<std::uint64_t>(family.size()));
            for (const auto material_index : family) {
                revision_hash.add(material_index);
            }
        }
        for (const auto& bodypart : bodyparts) {
            revision_hash.add(bodypart.base);
            for (const auto submodel_index : bodypart.submodel_indices) {
                revision_hash.add(submodel_index);
            }
        }

        const StudioModelRenderStatistics statistics{
            skeletal.bones.size(),
            bodyparts.size(),
            submodels.size(),
            meshes.size(),
            vertices.size(),
            indices.size(),
            materials.size(),
            skin_families.size(),
            supported_material_count,
            materials.size() - supported_material_count,
            geometry_bytes,
            texture_bytes,
            gpu_bytes,
        };
        return {
            StudioModelRenderAsset{
                source_identity,
                revision_hash.value(),
                std::move(vertices),
                std::move(indices),
                std::move(meshes),
                std::move(submodels),
                std::move(bodyparts),
                std::move(materials),
                std::move(skin_families),
                skeletal.source_clipping_bounds,
                statistics,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(StudioModelRenderAssetErrorCode::unable_to_retain_asset,
            std::nullopt,
            "Unable to retain immutable Studio render asset");
    } catch (const std::length_error&) {
        return fail(StudioModelRenderAssetErrorCode::source_limit_exceeded,
            std::nullopt,
            "Studio render asset exceeds an owning container limit");
    }
}

} // namespace hlclient::entity_render

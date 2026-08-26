#include <hlclient/entity_render/entity_scene_render.hpp>

#include <hlclient/world_spatial/world_spatial_query.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
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

    void add(const float value) noexcept
    {
        add(std::bit_cast<std::uint32_t>(value));
    }

    void add(const double value) noexcept
    {
        add(std::bit_cast<std::uint64_t>(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? 1U : value_;
    }

private:
    std::uint64_t value_{14'695'981'039'346'656'037ULL};
};

[[nodiscard]] EntityRenderFrameBuildResult frame_fail(
    const EntityRenderFrameErrorCode code,
    const std::optional<std::uint32_t> entity_number,
    std::string context)
{
    return {std::nullopt,
        EntityRenderFrameError{code, entity_number, std::move(context)}};
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

void hash_vector(StableHasher& hash, const assets::AssetVector3& value) noexcept
{
    hash.add(value.x);
    hash.add(value.y);
    hash.add(value.z);
}

void hash_bounds(StableHasher& hash, const assets::WorldBounds& bounds) noexcept
{
    hash_vector(hash, bounds.minimum);
    hash_vector(hash, bounds.maximum);
}

void hash_transform(
    StableHasher& hash,
    const EntityRenderTransform& transform) noexcept
{
    hash_vector(hash, transform.origin);
    hash_vector(hash, transform.rotation_degrees);
    hash.add(transform.uniform_scale);
}

[[nodiscard]] bool finite_interpolation(
    const EntityRenderInterpolationMetadata& interpolation) noexcept
{
    if (interpolation.profile !=
            EntityRenderInterpolationProfile::synthetic_seconds_v1 ||
        !std::isfinite(interpolation.sample_time_seconds) ||
        !std::isfinite(interpolation.previous_time_seconds) ||
        !std::isfinite(interpolation.current_time_seconds) ||
        !std::isfinite(interpolation.alpha) || interpolation.alpha < 0.0F ||
        interpolation.alpha > 1.0F ||
        interpolation.previous_time_seconds >
            interpolation.current_time_seconds ||
        interpolation.previous_state_identity == 0U ||
        interpolation.current_state_identity == 0U) {
        return false;
    }
    const auto duration = interpolation.current_time_seconds -
        interpolation.previous_time_seconds;
    if (duration == 0.0) {
        return interpolation.alpha == 0.0F &&
            interpolation.previous_state_identity ==
                interpolation.current_state_identity;
    }
    if (!(duration > 0.0) || !std::isfinite(duration)) {
        return false;
    }
    if (interpolation.sample_time_seconds <
            interpolation.previous_time_seconds ||
        interpolation.sample_time_seconds >
            interpolation.current_time_seconds ||
        interpolation.previous_state_identity ==
            interpolation.current_state_identity) {
        return false;
    }
    const auto expected_alpha =
        (interpolation.sample_time_seconds -
            interpolation.previous_time_seconds) /
        duration;
    constexpr double alpha_tolerance =
        static_cast<double>(std::numeric_limits<float>::epsilon()) * 4.0;
    return std::isfinite(expected_alpha) &&
        std::abs(expected_alpha - static_cast<double>(interpolation.alpha)) <=
            alpha_tolerance;
}

[[nodiscard]] bool finite_matrix(
    const std::array<float, 16U>& matrix) noexcept
{
    return std::all_of(matrix.begin(), matrix.end(), [](const float value) {
        return std::isfinite(value);
    });
}

[[nodiscard]] std::optional<StudioEntityMaterialSupportStatus>
resolve_studio_material_support(
    const StudioModelRenderAsset& asset,
    const std::uint32_t body_value,
    const std::uint32_t skin_family_index)
{
    const auto selection = asset.select_submodels(body_value);
    if (!selection) {
        return std::nullopt;
    }
    bool has_opaque = false;
    bool has_masked = false;
    for (const auto submodel_index : selection.submodel_indices) {
        if (static_cast<std::size_t>(submodel_index) >= asset.submodels().size()) {
            return std::nullopt;
        }
        const auto& submodel = asset.submodels()[submodel_index];
        const auto mesh_end = static_cast<std::uint64_t>(submodel.first_mesh) +
            submodel.mesh_count;
        if (mesh_end > asset.meshes().size()) {
            return std::nullopt;
        }
        for (std::uint32_t offset = 0U; offset < submodel.mesh_count; ++offset) {
            const auto mesh_index = submodel.first_mesh + offset;
            const auto material_selection =
                asset.select_material(mesh_index, skin_family_index);
            if (!material_selection) {
                return std::nullopt;
            }
            const auto& material =
                asset.materials()[*material_selection.material_index];
            if (material.profile == StudioRenderMaterialProfile::unsupported) {
                return StudioEntityMaterialSupportStatus::unsupported_material;
            }
            has_masked = has_masked ||
                material.profile == StudioRenderMaterialProfile::masked;
            has_opaque = has_opaque ||
                material.profile == StudioRenderMaterialProfile::opaque;
        }
    }
    if (has_opaque && has_masked) {
        return StudioEntityMaterialSupportStatus::supported_opaque_and_masked;
    }
    if (has_masked) {
        return StudioEntityMaterialSupportStatus::supported_masked;
    }
    if (has_opaque) {
        return StudioEntityMaterialSupportStatus::supported_opaque;
    }
    return std::nullopt;
}

[[nodiscard]] bool visible_status(
    const RuntimeEntityVisibilityStatus status) noexcept
{
    return status == RuntimeEntityVisibilityStatus::visible;
}

void count_status(
    const RuntimeEntityVisibilityStatus status,
    EntityRenderFrameStatistics& statistics) noexcept
{
    switch (status) {
    case RuntimeEntityVisibilityStatus::visible:
        ++statistics.visible_count;
        break;
    case RuntimeEntityVisibilityStatus::culled_by_pvs:
        ++statistics.culled_by_pvs_count;
        break;
    case RuntimeEntityVisibilityStatus::culled_by_frustum:
        ++statistics.culled_by_frustum_count;
        break;
    case RuntimeEntityVisibilityStatus::asset_unavailable:
        ++statistics.unavailable_count;
        break;
    case RuntimeEntityVisibilityStatus::visual_projection_pending:
        ++statistics.projection_pending_count;
        break;
    case RuntimeEntityVisibilityStatus::unsupported_visual:
        ++statistics.unsupported_visual_count;
        break;
    }
}

enum class CullResult {
    success,
    invalid_frustum,
    spatial_query_failed,
};

[[nodiscard]] bool supported_sprite_orientation(
    const assets::SpriteOrientation orientation) noexcept
{
    switch (orientation) {
    case assets::SpriteOrientation::view_parallel:
    case assets::SpriteOrientation::view_parallel_upright:
    case assets::SpriteOrientation::oriented:
        return true;
    case assets::SpriteOrientation::facing_upright:
    case assets::SpriteOrientation::view_parallel_oriented:
        return false;
    }
    return false;
}

[[nodiscard]] CullResult apply_visibility(
    const assets::WorldBounds& bounds,
    RuntimeEntityVisibilityStatus& status,
    const world_visibility::WorldViewFrustum* frustum,
    const world_spatial::WorldSpatialPackage* spatial,
    const std::optional<std::uint32_t> camera_leaf_index)
{
    if (!visible_status(status)) {
        return CullResult::success;
    }
    // Synthetic policy: an entity is PVS-visible when any non-solid touched
    // leaf is visible. Frustum classification then conservatively refines it.
    if (spatial != nullptr) {
        const auto touched =
            world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
                *spatial, bounds);
        if (!touched) {
            return CullResult::spatial_query_failed;
        }
        bool pvs_visible = false;
        const auto leaves = spatial->leaves();
        for (const auto leaf_index : touched.result->leaf_indices) {
            if (static_cast<std::size_t>(leaf_index) >= leaves.size() ||
                leaves[leaf_index].solid_or_special) {
                continue;
            }
            const auto visible = spatial->pvs_table().leaf_is_visible_from(
                *camera_leaf_index, leaf_index);
            if (visible.value_or(false)) {
                pvs_visible = true;
                break;
            }
        }
        if (!pvs_visible) {
            status = RuntimeEntityVisibilityStatus::culled_by_pvs;
            return CullResult::success;
        }
    }
    if (frustum != nullptr) {
        const auto classification = frustum->classify(bounds);
        if (!classification) {
            return CullResult::invalid_frustum;
        }
        if (*classification.classification ==
            world_visibility::WorldBoundsClassification::outside) {
            status = RuntimeEntityVisibilityStatus::culled_by_frustum;
        }
    }
    return CullResult::success;
}

} // namespace

std::string_view to_string(const EntitySceneRenderErrorCode code) noexcept
{
    switch (code) {
    case EntitySceneRenderErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntitySceneRenderErrorCode::missing_asset_library:
        return "missing_asset_library";
    case EntitySceneRenderErrorCode::invalid_library_identity:
        return "invalid_library_identity";
    case EntitySceneRenderErrorCode::invalid_resource_identity:
        return "invalid_resource_identity";
    case EntitySceneRenderErrorCode::invalid_studio_asset:
        return "invalid_studio_asset";
    case EntitySceneRenderErrorCode::invalid_sprite_asset:
        return "invalid_sprite_asset";
    case EntitySceneRenderErrorCode::duplicate_asset_identity:
        return "duplicate_asset_identity";
    case EntitySceneRenderErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case EntitySceneRenderErrorCode::unable_to_retain_scene:
        return "unable_to_retain_scene";
    }
    return "unknown";
}

EntitySceneRenderPackage::EntitySceneRenderPackage(
    std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState>
        asset_library,
    const EntityRenderResourceIdentity asset_library_identity,
    const std::uint64_t resource_id,
    const std::uint64_t resource_revision,
    const std::optional<EntityRenderResourceIdentity> world_scene_association,
    std::vector<std::shared_ptr<const StudioModelRenderAsset>> studio_assets,
    std::vector<std::shared_ptr<const SpriteRenderAsset>> sprite_assets,
    const EntitySceneRenderStatistics statistics) noexcept
    : asset_library_(std::move(asset_library)),
      asset_library_identity_(asset_library_identity),
      resource_id_(resource_id),
      resource_revision_(resource_revision),
      world_scene_association_(world_scene_association),
      studio_assets_(std::move(studio_assets)),
      sprite_assets_(std::move(sprite_assets)),
      statistics_(statistics)
{
}

const std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState>&
EntitySceneRenderPackage::asset_library() const noexcept
{
    return asset_library_;
}

EntityRenderResourceIdentity EntitySceneRenderPackage::asset_library_identity()
    const noexcept
{
    return asset_library_identity_;
}

std::span<const std::shared_ptr<const StudioModelRenderAsset>>
EntitySceneRenderPackage::studio_assets() const noexcept
{
    return studio_assets_;
}

std::span<const std::shared_ptr<const SpriteRenderAsset>>
EntitySceneRenderPackage::sprite_assets() const noexcept
{
    return sprite_assets_;
}

std::optional<EntityRenderResourceIdentity>
EntitySceneRenderPackage::world_scene_association() const noexcept
{
    return world_scene_association_;
}

const EntitySceneRenderStatistics& EntitySceneRenderPackage::statistics() const noexcept
{
    return statistics_;
}

std::uint64_t EntitySceneRenderPackage::resource_id() const noexcept
{
    return resource_id_;
}

std::uint64_t EntitySceneRenderPackage::resource_revision() const noexcept
{
    return resource_revision_;
}

std::string_view to_string(const EntityRenderFrameErrorCode code) noexcept
{
    switch (code) {
    case EntityRenderFrameErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntityRenderFrameErrorCode::invalid_resource_identity:
        return "invalid_resource_identity";
    case EntityRenderFrameErrorCode::invalid_interpolation_metadata:
        return "invalid_interpolation_metadata";
    case EntityRenderFrameErrorCode::invalid_pose: return "invalid_pose";
    case EntityRenderFrameErrorCode::invalid_instance: return "invalid_instance";
    case EntityRenderFrameErrorCode::duplicate_entity_number:
        return "duplicate_entity_number";
    case EntityRenderFrameErrorCode::invalid_asset_reference:
        return "invalid_asset_reference";
    case EntityRenderFrameErrorCode::invalid_pose_reference:
        return "invalid_pose_reference";
    case EntityRenderFrameErrorCode::invalid_world_spatial_context:
        return "invalid_world_spatial_context";
    case EntityRenderFrameErrorCode::spatial_query_failed:
        return "spatial_query_failed";
    case EntityRenderFrameErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case EntityRenderFrameErrorCode::unable_to_retain_frame:
        return "unable_to_retain_frame";
    }
    return "unknown";
}

EntityRenderFrame::EntityRenderFrame(
    const EntityRenderResourceIdentity scene_package_identity,
    const std::uint64_t resource_id,
    const std::uint64_t resource_revision,
    const std::uint64_t frame_signature,
    const EntityRenderInterpolationMetadata interpolation,
    std::vector<StudioRenderPose> studio_poses,
    std::vector<StudioEntityRenderInstance> studio_instances,
    std::vector<SpriteEntityRenderInstance> sprite_instances,
    std::vector<UnsupportedEntityVisualInstance> unsupported_instances,
    std::vector<EntityDrawCommand> draw_commands,
    const EntityRenderFrameStatistics statistics) noexcept
    : scene_package_identity_(scene_package_identity),
      resource_id_(resource_id),
      resource_revision_(resource_revision),
      frame_signature_(frame_signature),
      interpolation_(interpolation),
      studio_poses_(std::move(studio_poses)),
      studio_instances_(std::move(studio_instances)),
      sprite_instances_(std::move(sprite_instances)),
      unsupported_instances_(std::move(unsupported_instances)),
      draw_commands_(std::move(draw_commands)),
      statistics_(statistics)
{
}

EntityRenderResourceIdentity EntityRenderFrame::scene_package_identity()
    const noexcept
{
    return scene_package_identity_;
}

const EntityRenderInterpolationMetadata& EntityRenderFrame::interpolation()
    const noexcept
{
    return interpolation_;
}

std::span<const StudioRenderPose> EntityRenderFrame::studio_poses() const noexcept
{
    return studio_poses_;
}

std::span<const StudioEntityRenderInstance>
EntityRenderFrame::studio_instances() const noexcept
{
    return studio_instances_;
}

std::span<const SpriteEntityRenderInstance>
EntityRenderFrame::sprite_instances() const noexcept
{
    return sprite_instances_;
}

std::span<const UnsupportedEntityVisualInstance>
EntityRenderFrame::unsupported_instances() const noexcept
{
    return unsupported_instances_;
}

std::span<const EntityDrawCommand> EntityRenderFrame::draw_commands() const noexcept
{
    return draw_commands_;
}

const EntityRenderFrameStatistics& EntityRenderFrame::statistics() const noexcept
{
    return statistics_;
}

std::uint64_t EntityRenderFrame::resource_id() const noexcept
{
    return resource_id_;
}

std::uint64_t EntityRenderFrame::resource_revision() const noexcept
{
    return resource_revision_;
}

std::uint64_t EntityRenderFrame::frame_signature() const noexcept
{
    return frame_signature_;
}

EntityRenderFrameBuildResult EntityRenderFrameBuilder::build(
    const EntitySceneRenderPackage& scene_package,
    EntityRenderFrameBuildInput input,
    const RuntimeEntityVisualLimits& limits) const
{
    if (!valid_runtime_entity_visual_limits(limits)) {
        return frame_fail(EntityRenderFrameErrorCode::invalid_configuration,
            std::nullopt,
            "Runtime entity visual limits are invalid or exceed hard caps");
    }
    if (input.resource_id == 0U || input.resource_revision == 0U) {
        return frame_fail(EntityRenderFrameErrorCode::invalid_resource_identity,
            std::nullopt,
            "Entity render frame identity and revision must be nonzero");
    }
    if (!finite_interpolation(input.interpolation)) {
        return frame_fail(
            EntityRenderFrameErrorCode::invalid_interpolation_metadata,
            std::nullopt,
            "Entity render interpolation metadata is non-finite or unordered");
    }
    if (input.camera_cull_exempt_entity_number &&
        *input.camera_cull_exempt_entity_number == 0U) {
        return frame_fail(EntityRenderFrameErrorCode::invalid_configuration,
            std::nullopt,
            "Camera-cull exempt entity number must be nonzero");
    }
    if ((input.spatial_package == nullptr) !=
            !input.camera_leaf_index.has_value() ||
        (input.spatial_package != nullptr &&
            static_cast<std::size_t>(*input.camera_leaf_index) >=
                input.spatial_package->leaves().size()) ||
        (input.spatial_package != nullptr &&
            !input.spatial_package->pvs_table().leaf_has_usable_row(
                *input.camera_leaf_index))) {
        return frame_fail(
            EntityRenderFrameErrorCode::invalid_world_spatial_context,
            std::nullopt,
            "Spatial entity visibility requires one exact camera leaf with usable PVS");
    }

    std::size_t candidate_count = 0U;
    if (!add_size(input.studio_instances.size(),
            input.sprite_instances.size(),
            candidate_count) ||
        !add_size(candidate_count,
            input.unsupported_instances.size(),
            candidate_count) ||
        candidate_count > limits.maximum_entities ||
        input.studio_instances.size() > limits.maximum_studio_instances ||
        input.sprite_instances.size() > limits.maximum_sprite_instances ||
        input.studio_poses.size() > limits.maximum_pose_count) {
        return frame_fail(EntityRenderFrameErrorCode::source_limit_exceeded,
            std::nullopt,
            "Entity render frame cardinality exceeds the configured limit");
    }

    try {
        std::size_t bone_matrix_count = 0U;
        for (const auto& pose : input.studio_poses) {
            if (pose.model_resource_identity.resource_id == 0U ||
                pose.model_resource_identity.revision == 0U ||
                pose.bone_matrices.empty()) {
                return frame_fail(EntityRenderFrameErrorCode::invalid_pose,
                    std::nullopt,
                    "Studio render pose has no exact model identity or bone palette");
            }
            if (!add_size(bone_matrix_count,
                    pose.bone_matrices.size(),
                    bone_matrix_count) ||
                bone_matrix_count > limits.maximum_total_bone_matrices ||
                !std::all_of(pose.bone_matrices.begin(),
                    pose.bone_matrices.end(),
                    finite_matrix)) {
                return frame_fail(EntityRenderFrameErrorCode::invalid_pose,
                    std::nullopt,
                    "Studio render pose is non-finite or exceeds the bone-matrix limit");
            }
        }

        const auto by_entity = [](const auto& left, const auto& right) {
            return left.entity_number < right.entity_number;
        };
        std::sort(input.studio_instances.begin(),
            input.studio_instances.end(),
            by_entity);
        std::sort(input.sprite_instances.begin(),
            input.sprite_instances.end(),
            by_entity);
        std::sort(input.unsupported_instances.begin(),
            input.unsupported_instances.end(),
            by_entity);

        std::unordered_set<std::uint32_t> entity_numbers;
        entity_numbers.reserve(candidate_count);
        const auto retain_entity_number = [&entity_numbers](
                                            const std::uint32_t number) {
            return number != 0U && entity_numbers.insert(number).second;
        };
        for (const auto& instance : input.studio_instances) {
            if (!retain_entity_number(instance.entity_number)) {
                return frame_fail(
                    EntityRenderFrameErrorCode::duplicate_entity_number,
                    instance.entity_number,
                    "Entity number is zero or repeated across frame candidates");
            }
        }
        for (const auto& instance : input.sprite_instances) {
            if (!retain_entity_number(instance.entity_number)) {
                return frame_fail(
                    EntityRenderFrameErrorCode::duplicate_entity_number,
                    instance.entity_number,
                    "Entity number is zero or repeated across frame candidates");
            }
        }
        for (const auto& instance : input.unsupported_instances) {
            if (!retain_entity_number(instance.entity_number)) {
                return frame_fail(
                    EntityRenderFrameErrorCode::duplicate_entity_number,
                    instance.entity_number,
                    "Entity number is zero or repeated across frame candidates");
            }
        }

        std::vector<EntityDrawCommand> draw_commands;
        draw_commands.reserve(std::min(
            limits.maximum_entity_draws,
            input.studio_instances.size() * 2U +
                input.sprite_instances.size()));

        const auto apply_camera_visibility = [&input](
                                                 const assets::WorldBounds& bounds,
                                                 RuntimeEntityVisibilityStatus& status,
                                                 const std::uint32_t entity_number) {
            if (input.camera_cull_exempt_entity_number == entity_number) {
                return CullResult::success;
            }
            return apply_visibility(bounds,
                status,
                input.view_frustum,
                input.spatial_package,
                input.camera_leaf_index);
        };

        for (std::size_t index = 0U;
             index < input.studio_instances.size();
             ++index) {
            auto& instance = input.studio_instances[index];
            if (!finite_entity_render_transform(instance.transform) ||
                !finite_entity_render_bounds(instance.interpolated_bounds)) {
                return frame_fail(EntityRenderFrameErrorCode::invalid_instance,
                    instance.entity_number,
                    "Studio entity transform or interpolated bounds are invalid");
            }
            if (static_cast<std::size_t>(instance.studio_asset_index) >=
                scene_package.studio_assets().size()) {
                instance.visibility_status =
                    RuntimeEntityVisibilityStatus::asset_unavailable;
                continue;
            }
            const auto& asset =
                *scene_package.studio_assets()[instance.studio_asset_index];
            if (static_cast<std::size_t>(instance.pose_index) >=
                input.studio_poses.size()) {
                return frame_fail(
                    EntityRenderFrameErrorCode::invalid_pose_reference,
                    instance.entity_number,
                    "Studio entity references an unavailable pose");
            }
            const auto& pose = input.studio_poses[instance.pose_index];
            if (pose.model_resource_identity != asset.source_identity() ||
                pose.bone_matrices.size() != asset.statistics().bone_count) {
                return frame_fail(
                    EntityRenderFrameErrorCode::invalid_pose_reference,
                    instance.entity_number,
                    "Studio pose identity or bone count does not match its render asset");
            }
            const auto material_support = resolve_studio_material_support(
                asset, instance.body_value, instance.skin_family_index);
            if (!material_support) {
                return frame_fail(
                    EntityRenderFrameErrorCode::invalid_asset_reference,
                    instance.entity_number,
                    "Studio body, skin, mesh, or material selection is invalid");
            }
            instance.material_support_status = *material_support;
            if (*material_support ==
                StudioEntityMaterialSupportStatus::unsupported_material) {
                instance.visibility_status =
                    RuntimeEntityVisibilityStatus::unsupported_visual;
            }
            const auto cull = apply_camera_visibility(
                instance.interpolated_bounds,
                instance.visibility_status,
                instance.entity_number);
            if (cull != CullResult::success) {
                return frame_fail(
                    cull == CullResult::spatial_query_failed
                        ? EntityRenderFrameErrorCode::spatial_query_failed
                        : EntityRenderFrameErrorCode::invalid_instance,
                    instance.entity_number,
                    "Studio entity visibility query failed");
            }
            if (!visible_status(instance.visibility_status)) {
                continue;
            }
            if (*material_support ==
                    StudioEntityMaterialSupportStatus::supported_opaque ||
                *material_support == StudioEntityMaterialSupportStatus::
                    supported_opaque_and_masked) {
                draw_commands.push_back({EntityDrawClass::studio_opaque,
                    RuntimeEntityVisualKind::studio_model,
                    instance.entity_number,
                    static_cast<std::uint32_t>(index)});
            }
            if (*material_support ==
                    StudioEntityMaterialSupportStatus::supported_masked ||
                *material_support == StudioEntityMaterialSupportStatus::
                    supported_opaque_and_masked) {
                draw_commands.push_back({EntityDrawClass::studio_masked,
                    RuntimeEntityVisualKind::studio_model,
                    instance.entity_number,
                    static_cast<std::uint32_t>(index)});
            }
        }

        for (std::size_t index = 0U;
             index < input.sprite_instances.size();
             ++index) {
            auto& instance = input.sprite_instances[index];
            if (!finite_entity_render_transform(instance.transform) ||
                !finite_entity_render_bounds(instance.bounds)) {
                return frame_fail(EntityRenderFrameErrorCode::invalid_instance,
                    instance.entity_number,
                    "Sprite entity transform or bounds are invalid");
            }
            if (static_cast<std::size_t>(instance.sprite_asset_index) >=
                scene_package.sprite_assets().size()) {
                instance.visibility_status =
                    RuntimeEntityVisibilityStatus::asset_unavailable;
                continue;
            }
            const auto& asset =
                *scene_package.sprite_assets()[instance.sprite_asset_index];
            if (static_cast<std::size_t>(instance.selected_frame_index) >=
                asset.frames().size() || instance.orientation != asset.orientation()) {
                return frame_fail(
                    EntityRenderFrameErrorCode::invalid_asset_reference,
                    instance.entity_number,
                    "Sprite frame or orientation does not match its render asset");
            }
            instance.texture_format_support = asset.texture_support_status();
            if (asset.render_profile() == SpriteRenderTextureProfile::unsupported ||
                !supported_sprite_orientation(instance.orientation)) {
                instance.visibility_status =
                    RuntimeEntityVisibilityStatus::unsupported_visual;
            }
            const auto cull = apply_camera_visibility(instance.bounds,
                instance.visibility_status,
                instance.entity_number);
            if (cull != CullResult::success) {
                return frame_fail(
                    cull == CullResult::spatial_query_failed
                        ? EntityRenderFrameErrorCode::spatial_query_failed
                        : EntityRenderFrameErrorCode::invalid_instance,
                    instance.entity_number,
                    "Sprite entity visibility query failed");
            }
            if (!visible_status(instance.visibility_status)) {
                continue;
            }
            draw_commands.push_back({
                asset.render_profile() ==
                        SpriteRenderTextureProfile::alpha_test_masked
                    ? EntityDrawClass::sprite_alpha_test
                    : EntityDrawClass::sprite_normal,
                RuntimeEntityVisualKind::sprite,
                instance.entity_number,
                static_cast<std::uint32_t>(index),
            });
        }

        for (auto& instance : input.unsupported_instances) {
            switch (instance.reason) {
            case UnsupportedEntityVisualReason::missing_asset:
                instance.visibility_status =
                    RuntimeEntityVisibilityStatus::asset_unavailable;
                break;
            case UnsupportedEntityVisualReason::projection_evidence_pending:
                instance.visibility_status =
                    RuntimeEntityVisibilityStatus::visual_projection_pending;
                break;
            case UnsupportedEntityVisualReason::unsupported_asset_kind:
            case UnsupportedEntityVisualReason::unsupported_material:
            case UnsupportedEntityVisualReason::unsupported_sprite_format:
            case UnsupportedEntityVisualReason::unsupported_sprite_orientation:
            case UnsupportedEntityVisualReason::unsupported_render_mode:
                instance.visibility_status =
                    RuntimeEntityVisibilityStatus::unsupported_visual;
                break;
            }
        }

        if (draw_commands.size() > limits.maximum_entity_draws) {
            return frame_fail(EntityRenderFrameErrorCode::source_limit_exceeded,
                std::nullopt,
                "Entity draw count exceeds the configured limit");
        }
        std::sort(draw_commands.begin(),
            draw_commands.end(),
            [](const EntityDrawCommand& left, const EntityDrawCommand& right) {
                if (left.draw_class != right.draw_class) {
                    return left.draw_class < right.draw_class;
                }
                if (left.entity_number != right.entity_number) {
                    return left.entity_number < right.entity_number;
                }
                return left.instance_index < right.instance_index;
            });

        EntityRenderFrameStatistics statistics;
        statistics.candidate_count = candidate_count;
        statistics.studio_instance_count = input.studio_instances.size();
        statistics.sprite_instance_count = input.sprite_instances.size();
        statistics.unsupported_instance_count =
            input.unsupported_instances.size();
        statistics.pose_count = input.studio_poses.size();
        statistics.total_bone_matrix_count = bone_matrix_count;
        statistics.draw_count = draw_commands.size();
        for (const auto& instance : input.studio_instances) {
            count_status(instance.visibility_status, statistics);
        }
        for (const auto& instance : input.sprite_instances) {
            count_status(instance.visibility_status, statistics);
        }
        for (const auto& instance : input.unsupported_instances) {
            count_status(instance.visibility_status, statistics);
        }

        StableHasher signature;
        signature.add(scene_package.resource_id());
        signature.add(scene_package.resource_revision());
        signature.add(input.resource_id);
        signature.add(input.resource_revision);
        signature.add(input.interpolation.sample_time_seconds);
        signature.add(input.interpolation.previous_time_seconds);
        signature.add(input.interpolation.current_time_seconds);
        signature.add(input.interpolation.alpha);
        signature.add(input.interpolation.previous_state_identity);
        signature.add(input.interpolation.current_state_identity);
        signature.add(static_cast<std::uint32_t>(input.interpolation.profile));
        for (const auto& pose : input.studio_poses) {
            signature.add(pose.model_resource_identity.resource_id);
            signature.add(pose.model_resource_identity.revision);
            for (const auto& matrix : pose.bone_matrices) {
                for (const auto value : matrix) {
                    signature.add(value);
                }
            }
        }
        for (const auto& instance : input.studio_instances) {
            signature.add(instance.entity_number);
            signature.add(instance.studio_asset_index);
            signature.add(instance.pose_index);
            hash_transform(signature, instance.transform);
            signature.add(instance.body_value);
            signature.add(instance.skin_family_index);
            signature.add(static_cast<std::uint32_t>(
                instance.material_support_status));
            hash_bounds(signature, instance.interpolated_bounds);
            signature.add(static_cast<std::uint32_t>(
                instance.visibility_status));
        }
        for (const auto& instance : input.sprite_instances) {
            signature.add(instance.entity_number);
            signature.add(instance.sprite_asset_index);
            signature.add(instance.selected_frame_index);
            hash_transform(signature, instance.transform);
            signature.add(static_cast<std::uint32_t>(instance.orientation));
            signature.add(static_cast<std::uint32_t>(
                instance.texture_format_support));
            hash_bounds(signature, instance.bounds);
            signature.add(static_cast<std::uint32_t>(
                instance.visibility_status));
        }
        for (const auto& instance : input.unsupported_instances) {
            signature.add(instance.entity_number);
            signature.add(instance.visual_asset_index.value_or(
                std::numeric_limits<std::uint32_t>::max()));
            signature.add(static_cast<std::uint32_t>(instance.reason));
            signature.add(static_cast<std::uint32_t>(
                instance.visibility_status));
        }

        return {
            EntityRenderFrame{
                {scene_package.resource_id(),
                    scene_package.resource_revision()},
                input.resource_id,
                input.resource_revision,
                signature.value(),
                input.interpolation,
                std::move(input.studio_poses),
                std::move(input.studio_instances),
                std::move(input.sprite_instances),
                std::move(input.unsupported_instances),
                std::move(draw_commands),
                statistics,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return frame_fail(EntityRenderFrameErrorCode::unable_to_retain_frame,
            std::nullopt,
            "Unable to retain immutable entity render frame");
    } catch (const std::length_error&) {
        return frame_fail(EntityRenderFrameErrorCode::source_limit_exceeded,
            std::nullopt,
            "Entity render frame exceeds an owning container limit");
    }
}

} // namespace hlclient::entity_render

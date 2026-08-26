#include <hlclient/entity_render/entity_render_frame_composer.hpp>

#include <hlclient/entity_visual/entity_visual_asset_library.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::entity_render {

namespace {

namespace sprite = goldsrc::sprite;
namespace studio = goldsrc::studio;

[[nodiscard]] EntityRenderFrameCompositionResult fail(
    const EntityRenderFrameComposerErrorCode code,
    std::string context,
    const std::optional<std::uint32_t> entity_number = std::nullopt,
    const std::optional<entity_visual::EntityVisualModelReference>
        model_reference = std::nullopt)
{
    EntityRenderFrameComposerError error;
    error.code = code;
    error.entity_number = entity_number;
    error.model_reference = model_reference;
    error.context = std::move(context);
    return {std::nullopt, std::move(error)};
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] std::optional<std::uint8_t> quantize_unit_control(
    const float value) noexcept
{
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(
        std::lround(static_cast<double>(value) * 255.0));
}

template <std::size_t Size>
[[nodiscard]] std::optional<std::array<std::uint8_t, Size>>
quantize_unit_controls(const std::array<float, Size>& values) noexcept
{
    std::array<std::uint8_t, Size> result{};
    for (std::size_t index = 0U; index < Size; ++index) {
        const auto converted = quantize_unit_control(values[index]);
        if (!converted) {
            return std::nullopt;
        }
        result[index] = *converted;
    }
    return result;
}

[[nodiscard]] std::array<float, 16U> column_major_matrix(
    const studio::StudioMatrix3x4& matrix) noexcept
{
    const auto& value = matrix.values;
    return {
        value[0U], value[4U], value[8U], 0.0F,
        value[1U], value[5U], value[9U], 0.0F,
        value[2U], value[6U], value[10U], 0.0F,
        value[3U], value[7U], value[11U], 1.0F,
    };
}

[[nodiscard]] assets::AssetVector3 rotate_xyz(
    const assets::AssetVector3& value,
    const assets::AssetVector3& rotation_degrees) noexcept
{
    constexpr double radians = 0.017453292519943295769;
    const double x = static_cast<double>(rotation_degrees.x) * radians;
    const double y = static_cast<double>(rotation_degrees.y) * radians;
    const double z = static_cast<double>(rotation_degrees.z) * radians;
    const double cx = std::cos(x);
    const double sx = std::sin(x);
    const double cy = std::cos(y);
    const double sy = std::sin(y);
    const double cz = std::cos(z);
    const double sz = std::sin(z);

    const double source_x = value.x;
    const double source_y = value.y;
    const double source_z = value.z;
    const double x_rotated = source_x;
    const double y_rotated = cx * source_y - sx * source_z;
    const double z_rotated = sx * source_y + cx * source_z;
    const double x_twice = cy * x_rotated + sy * z_rotated;
    const double y_twice = y_rotated;
    const double z_twice = -sy * x_rotated + cy * z_rotated;
    return {
        static_cast<float>(cz * x_twice - sz * y_twice),
        static_cast<float>(sz * x_twice + cz * y_twice),
        static_cast<float>(z_twice),
    };
}

[[nodiscard]] std::optional<assets::WorldBounds> transformed_bounds(
    const assets::WorldBounds& source,
    const EntityRenderTransform& transform) noexcept
{
    if (!finite_entity_render_bounds(source) ||
        !finite_entity_render_transform(transform)) {
        return std::nullopt;
    }
    assets::WorldBounds result{
        {std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()},
        {std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()},
    };
    for (std::uint32_t corner = 0U; corner < 8U; ++corner) {
        assets::AssetVector3 local{
            (corner & 1U) != 0U ? source.maximum.x : source.minimum.x,
            (corner & 2U) != 0U ? source.maximum.y : source.minimum.y,
            (corner & 4U) != 0U ? source.maximum.z : source.minimum.z,
        };
        local.x *= transform.uniform_scale;
        local.y *= transform.uniform_scale;
        local.z *= transform.uniform_scale;
        auto world = rotate_xyz(local, transform.rotation_degrees);
        world.x += transform.origin.x;
        world.y += transform.origin.y;
        world.z += transform.origin.z;
        if (!finite_vector(world)) {
            return std::nullopt;
        }
        result.minimum.x = std::min(result.minimum.x, world.x);
        result.minimum.y = std::min(result.minimum.y, world.y);
        result.minimum.z = std::min(result.minimum.z, world.z);
        result.maximum.x = std::max(result.maximum.x, world.x);
        result.maximum.y = std::max(result.maximum.y, world.y);
        result.maximum.z = std::max(result.maximum.z, world.z);
    }
    return finite_entity_render_bounds(result)
        ? std::optional<assets::WorldBounds>{result}
        : std::nullopt;
}

[[nodiscard]] std::optional<assets::WorldBounds> sprite_bounds(
    const SpriteRenderAsset& asset,
    const EntityRenderTransform& transform) noexcept
{
    if (!finite_entity_render_transform(transform) ||
        !std::isfinite(asset.bounding_radius()) ||
        asset.bounding_radius() < 0.0F) {
        return std::nullopt;
    }
    const float extent = asset.bounding_radius() * transform.uniform_scale;
    if (!std::isfinite(extent)) {
        return std::nullopt;
    }
    const assets::WorldBounds result{
        {transform.origin.x - extent,
            transform.origin.y - extent,
            transform.origin.z - extent},
        {transform.origin.x + extent,
            transform.origin.y + extent,
            transform.origin.z + extent},
    };
    return finite_entity_render_bounds(result)
        ? std::optional<assets::WorldBounds>{result}
        : std::nullopt;
}

struct StudioMaterialClassification {
    bool valid{false};
    bool has_opaque{false};
    bool has_masked{false};
    bool has_unsupported{false};
};

[[nodiscard]] StudioMaterialClassification classify_studio_materials(
    const StudioModelRenderAsset& asset,
    const std::uint32_t body_value,
    const std::uint32_t skin_family_index)
{
    const auto selected = asset.select_submodels(body_value);
    if (!selected) {
        return {};
    }
    StudioMaterialClassification result;
    const auto submodels = asset.submodels();
    const auto meshes = asset.meshes();
    const auto materials = asset.materials();
    for (const auto selected_index : selected.submodel_indices) {
        if (static_cast<std::size_t>(selected_index) >= submodels.size()) {
            return {};
        }
        const auto& submodel = submodels[selected_index];
        const auto first = static_cast<std::size_t>(submodel.first_mesh);
        const auto count = static_cast<std::size_t>(submodel.mesh_count);
        if (first > meshes.size() || count > meshes.size() - first) {
            return {};
        }
        for (std::size_t offset = 0U; offset < count; ++offset) {
            const auto mesh_index = first + offset;
            const auto material_index = asset.select_material(
                static_cast<std::uint32_t>(mesh_index), skin_family_index);
            if (!material_index ||
                static_cast<std::size_t>(*material_index.material_index) >=
                    materials.size()) {
                return {};
            }
            switch (materials[*material_index.material_index].profile) {
            case StudioRenderMaterialProfile::opaque:
                result.has_opaque = true;
                break;
            case StudioRenderMaterialProfile::masked:
                result.has_masked = true;
                break;
            case StudioRenderMaterialProfile::unsupported:
                result.has_unsupported = true;
                break;
            }
        }
    }
    result.valid = result.has_opaque || result.has_masked ||
        result.has_unsupported;
    return result;
}

[[nodiscard]] std::optional<UnsupportedEntityVisualReason>
unsupported_studio_reason(
    const StudioMaterialClassification& materials,
    const entity_visual::EntityVisualRenderMode render_mode) noexcept
{
    if (materials.has_unsupported) {
        return UnsupportedEntityVisualReason::unsupported_material;
    }
    switch (render_mode) {
    case entity_visual::EntityVisualRenderMode::source_asset_default:
        return std::nullopt;
    case entity_visual::EntityVisualRenderMode::opaque:
        return materials.has_masked
            ? std::optional<UnsupportedEntityVisualReason>{
                  UnsupportedEntityVisualReason::unsupported_render_mode}
            : std::nullopt;
    case entity_visual::EntityVisualRenderMode::alpha_test:
        return materials.has_opaque
            ? std::optional<UnsupportedEntityVisualReason>{
                  UnsupportedEntityVisualReason::unsupported_render_mode}
            : std::nullopt;
    case entity_visual::EntityVisualRenderMode::additive:
        return UnsupportedEntityVisualReason::unsupported_render_mode;
    }
    return UnsupportedEntityVisualReason::unsupported_render_mode;
}

[[nodiscard]] std::optional<UnsupportedEntityVisualReason>
unsupported_sprite_reason(
    const SpriteRenderAsset& asset,
    const entity_visual::EntityVisualRenderMode render_mode) noexcept
{
    if (asset.orientation() == assets::SpriteOrientation::facing_upright ||
        asset.orientation() ==
            assets::SpriteOrientation::view_parallel_oriented) {
        return UnsupportedEntityVisualReason::unsupported_sprite_orientation;
    }
    if (asset.render_profile() == SpriteRenderTextureProfile::unsupported) {
        return UnsupportedEntityVisualReason::unsupported_sprite_format;
    }
    switch (render_mode) {
    case entity_visual::EntityVisualRenderMode::source_asset_default:
        return std::nullopt;
    case entity_visual::EntityVisualRenderMode::opaque:
        return asset.render_profile() == SpriteRenderTextureProfile::opaque
            ? std::nullopt
            : std::optional<UnsupportedEntityVisualReason>{
                  UnsupportedEntityVisualReason::unsupported_render_mode};
    case entity_visual::EntityVisualRenderMode::alpha_test:
        return asset.render_profile() ==
                SpriteRenderTextureProfile::alpha_test_masked
            ? std::nullopt
            : std::optional<UnsupportedEntityVisualReason>{
                  UnsupportedEntityVisualReason::unsupported_render_mode};
    case entity_visual::EntityVisualRenderMode::additive:
        return UnsupportedEntityVisualReason::unsupported_render_mode;
    }
    return UnsupportedEntityVisualReason::unsupported_render_mode;
}

[[nodiscard]] std::optional<std::size_t> find_studio_asset(
    const EntitySceneRenderPackage& package,
    const entity_visual::EntityVisualAssetRecord& record) noexcept
{
    const auto assets_span = package.studio_assets();
    const auto found = std::find_if(
        assets_span.begin(), assets_span.end(), [&record](const auto& asset) {
            return asset && asset->source_identity() ==
                EntityRenderResourceIdentity{
                    record.resource_id(), record.resource_revision()};
        });
    return found == assets_span.end()
        ? std::nullopt
        : std::optional<std::size_t>{
              static_cast<std::size_t>(found - assets_span.begin())};
}

[[nodiscard]] std::optional<std::size_t> find_sprite_asset(
    const EntitySceneRenderPackage& package,
    const entity_visual::EntityVisualAssetRecord& record) noexcept
{
    const auto assets_span = package.sprite_assets();
    const auto found = std::find_if(
        assets_span.begin(), assets_span.end(), [&record](const auto& asset) {
            return asset && asset->source_identity() ==
                EntityRenderResourceIdentity{
                    record.resource_id(), record.resource_revision()};
        });
    return found == assets_span.end()
        ? std::nullopt
        : std::optional<std::size_t>{
              static_cast<std::size_t>(found - assets_span.begin())};
}

[[nodiscard]] EntityRenderTransform render_transform(
    const goldsrc::InterpolatedEntityState& entity) noexcept
{
    return {entity.position(), entity.angles_degrees(), entity.scale().x};
}

[[nodiscard]] bool uniform_scale(
    const goldsrc::InterpolatedEntityState& entity) noexcept
{
    const auto& scale = entity.scale();
    return scale.x == scale.y && scale.x == scale.z;
}

[[nodiscard]] std::string pose_resource_identity(
    const entity_visual::EntityVisualAssetRecord& record)
{
    return "entity-visual-asset-" + std::to_string(record.resource_id()) +
        "-" + std::to_string(record.resource_revision());
}

} // namespace

std::string_view to_string(
    const EntityRenderFrameComposerErrorCode code) noexcept
{
    switch (code) {
    case EntityRenderFrameComposerErrorCode::invalid_configuration:
        return "invalid_configuration";
    case EntityRenderFrameComposerErrorCode::scene_package_mismatch:
        return "scene_package_mismatch";
    case EntityRenderFrameComposerErrorCode::interpolation_evidence_pending:
        return "interpolation_evidence_pending";
    case EntityRenderFrameComposerErrorCode::missing_asset_library:
        return "missing_asset_library";
    case EntityRenderFrameComposerErrorCode::render_asset_mismatch:
        return "render_asset_mismatch";
    case EntityRenderFrameComposerErrorCode::missing_skeletal_data:
        return "missing_skeletal_data";
    case EntityRenderFrameComposerErrorCode::studio_pose_failed:
        return "studio_pose_failed";
    case EntityRenderFrameComposerErrorCode::sprite_selection_failed:
        return "sprite_selection_failed";
    case EntityRenderFrameComposerErrorCode::non_finite_transformed_bounds:
        return "non_finite_transformed_bounds";
    case EntityRenderFrameComposerErrorCode::render_frame_rejected:
        return "render_frame_rejected";
    case EntityRenderFrameComposerErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case EntityRenderFrameComposerErrorCode::unable_to_retain_composition:
        return "unable_to_retain_composition";
    }
    return "unknown";
}

EntityRenderFrameCompositionResult EntityRenderFrameComposer::compose(
    const EntitySceneRenderPackage& scene_package,
    const goldsrc::InterpolatedEntityFrame& interpolated_frame,
    const EntityRenderFrameCompositionInput& input,
    studio::StudioPoseCache& pose_cache,
    const RuntimeEntityVisualLimits& limits,
    const sprite::SpritePlaybackLimits& sprite_limits) const
{
    const EntityRenderResourceIdentity package_identity{
        scene_package.resource_id(), scene_package.resource_revision()};
    if (!valid_runtime_entity_visual_limits(limits) ||
        !sprite::valid_sprite_playback_limits(sprite_limits) ||
        !pose_cache.valid_configuration() ||
        input.expected_scene_package_identity.resource_id == 0U ||
        input.expected_scene_package_identity.revision == 0U ||
        input.frame_identity.resource_id == 0U ||
        input.frame_identity.revision == 0U) {
        return fail(EntityRenderFrameComposerErrorCode::invalid_configuration,
            "Composition limits, cache, and resource identities must be valid");
    }
    if (input.expected_scene_package_identity != package_identity) {
        return fail(EntityRenderFrameComposerErrorCode::scene_package_mismatch,
            "Expected entity scene identity does not match the exact package");
    }
    if (interpolated_frame.evidence_profile() !=
        goldsrc::EntityInterpolationEvidenceProfile::
            synthetic_explicit_projection_v1) {
        return fail(
            EntityRenderFrameComposerErrorCode::interpolation_evidence_pending,
            "Only explicit synthetic interpolation evidence may be composed");
    }
    if (input.previous_time_seconds !=
            interpolated_frame.previous_seconds() ||
        input.current_time_seconds != interpolated_frame.current_seconds()) {
        return fail(
            EntityRenderFrameComposerErrorCode::invalid_configuration,
            "Composition times must exactly match the immutable interpolation pair");
    }
    const auto& library = scene_package.asset_library();
    if (!library) {
        return fail(EntityRenderFrameComposerErrorCode::missing_asset_library,
            "Entity scene package has no retained visual asset library");
    }
    if (scene_package.asset_library_identity() !=
            EntityRenderResourceIdentity{
                library->resource_id(), library->resource_revision()} ||
        interpolated_frame.entities().size() > limits.maximum_entities) {
        return fail(EntityRenderFrameComposerErrorCode::scene_package_mismatch,
            "Entity scene package library identity or frame cardinality is invalid");
    }

    pose_cache.reset_for_frame(input.frame_identity.revision);
    try {
        EntityRenderFrameBuildInput build_input;
        build_input.resource_id = input.frame_identity.resource_id;
        build_input.resource_revision = input.frame_identity.revision;
        build_input.interpolation = {
            interpolated_frame.sample_seconds(),
            interpolated_frame.previous_seconds(),
            interpolated_frame.current_seconds(),
            static_cast<float>(interpolated_frame.alpha()),
            interpolated_frame.previous_snapshot_reference(),
            interpolated_frame.current_snapshot_reference(),
            EntityRenderInterpolationProfile::synthetic_seconds_v1,
        };
        build_input.view_frustum = input.view_frustum;
        build_input.spatial_package = input.spatial_package;
        build_input.camera_leaf_index = input.camera_leaf_index;

        std::vector<std::shared_ptr<const studio::StudioPoseState>> pose_owners;
        pose_owners.reserve(std::min(
            interpolated_frame.entities().size(), limits.maximum_pose_count));

        for (const auto& entity : interpolated_frame.entities()) {
            const auto reference = entity.model_reference();
            const auto record_index = library->find_exact_index(reference);
            const auto* const record = library->find_exact(reference);
            if (!record_index || record == nullptr) {
                build_input.unsupported_instances.push_back({
                    entity.entity_number(),
                    std::nullopt,
                    UnsupportedEntityVisualReason::missing_asset,
                    RuntimeEntityVisibilityStatus::asset_unavailable,
                });
                continue;
            }
            if (!uniform_scale(entity)) {
                return fail(
                    EntityRenderFrameComposerErrorCode::invalid_configuration,
                    "Entity render composition requires exact uniform scale",
                    entity.entity_number(),
                    reference);
            }
            const auto transform = render_transform(entity);
            if (!finite_entity_render_transform(transform)) {
                return fail(
                    EntityRenderFrameComposerErrorCode::invalid_configuration,
                    "Interpolated entity transform is non-finite",
                    entity.entity_number(),
                    reference);
            }

            if (record->kind() ==
                entity_visual::EntityVisualAssetKind::studio_model) {
                const auto asset_index = find_studio_asset(scene_package, *record);
                if (!asset_index) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::render_asset_mismatch,
                        "Studio library record has no exact package render asset",
                        entity.entity_number(),
                        reference);
                }
                const auto& render_asset =
                    *scene_package.studio_assets()[*asset_index];
                const auto material = classify_studio_materials(render_asset,
                    static_cast<std::uint32_t>(entity.discrete().body_value),
                    entity.discrete().skin_family_index);
                if (!material.valid) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::render_asset_mismatch,
                        "Studio body, skin, mesh, or material selection is invalid",
                        entity.entity_number(),
                        reference);
                }
                if (const auto unsupported = unsupported_studio_reason(
                        material, entity.render_mode())) {
                    build_input.unsupported_instances.push_back({
                        entity.entity_number(),
                        static_cast<std::uint32_t>(*record_index),
                        *unsupported,
                        RuntimeEntityVisibilityStatus::unsupported_visual,
                    });
                    continue;
                }
                if (!record->model_asset() ||
                    !record->model_asset()->skeletal_data) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::missing_skeletal_data,
                        "Studio library record has no immutable skeletal payload",
                        entity.entity_number(),
                        reference);
                }
                const auto controllers =
                    quantize_unit_controls(entity.controller_values());
                const auto blends =
                    quantize_unit_controls(entity.blending_values());
                const auto mouth = quantize_unit_control(entity.mouth_value());
                if (!controllers || !blends || !mouth) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::invalid_configuration,
                        "Studio controls are outside the explicit normalized range",
                        entity.entity_number(),
                        reference);
                }
                studio::StudioPoseInput pose_input;
                pose_input.sequence_index = entity.discrete().sequence_index;
                pose_input.frame_coordinate =
                    entity.studio_frame_coordinate();
                pose_input.body_value = entity.discrete().body_value;
                pose_input.skin_family_index =
                    entity.discrete().skin_family_index;
                pose_input.controller_values = *controllers;
                pose_input.blending_values = *blends;
                pose_input.mouth_value = *mouth;
                pose_input.entity_scale = entity.scale();
                if (static_cast<std::size_t>(pose_input.sequence_index) >=
                    record->model_asset()->skeletal_data->sequences.size()) {
                    auto result = fail(
                        EntityRenderFrameComposerErrorCode::studio_pose_failed,
                        "Studio sequence index is outside the exact model asset",
                        entity.entity_number(),
                        reference);
                    result.error->studio_pose_error =
                        studio::StudioPoseErrorCode::invalid_sequence;
                    return result;
                }
                const studio::StudioPoseModelIdentity model_identity{
                    pose_resource_identity(*record),
                    record->source_fingerprint(),
                };
                auto evaluated = pose_cache.find_or_evaluate(model_identity,
                    *record->model_asset()->skeletal_data,
                    pose_input);
                if (!evaluated || !evaluated.pose) {
                    auto result = fail(
                        EntityRenderFrameComposerErrorCode::studio_pose_failed,
                        evaluated.error
                            ? "Studio pose evaluation failed: " +
                                evaluated.error->context
                            : "Studio pose evaluation failed",
                        entity.entity_number(),
                        reference);
                    if (evaluated.error) {
                        result.error->studio_pose_error = evaluated.error->code;
                    }
                    return result;
                }
                const auto existing_pose = std::find(
                    pose_owners.begin(), pose_owners.end(), evaluated.pose);
                std::size_t pose_index = 0U;
                if (existing_pose == pose_owners.end()) {
                    if (pose_owners.size() >= limits.maximum_pose_count ||
                        pose_owners.size() >=
                            limits.maximum_pose_evaluations_per_update) {
                        return fail(
                            EntityRenderFrameComposerErrorCode::source_limit_exceeded,
                            "Unique Studio poses exceed the configured frame limit",
                            entity.entity_number(),
                            reference);
                    }
                    StudioRenderPose render_pose;
                    render_pose.model_resource_identity =
                        render_asset.source_identity();
                    render_pose.bone_matrices.reserve(
                        evaluated.pose->world_bones().size());
                    for (const auto& bone : evaluated.pose->world_bones()) {
                        render_pose.bone_matrices.push_back(
                            column_major_matrix(bone.transform));
                    }
                    pose_index = pose_owners.size();
                    pose_owners.push_back(evaluated.pose);
                    build_input.studio_poses.push_back(std::move(render_pose));
                } else {
                    pose_index = static_cast<std::size_t>(
                        existing_pose - pose_owners.begin());
                }
                const auto bounds = transformed_bounds(
                    {render_asset.bounds().minimum,
                        render_asset.bounds().maximum},
                    transform);
                if (!bounds) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::
                            non_finite_transformed_bounds,
                        "Studio transformed bounds are non-finite",
                        entity.entity_number(),
                        reference);
                }
                StudioEntityRenderInstance instance;
                instance.entity_number = entity.entity_number();
                instance.studio_asset_index =
                    static_cast<std::uint32_t>(*asset_index);
                instance.pose_index = static_cast<std::uint32_t>(pose_index);
                instance.transform = transform;
                instance.body_value = static_cast<std::uint32_t>(
                    entity.discrete().body_value);
                instance.skin_family_index =
                    entity.discrete().skin_family_index;
                instance.interpolated_bounds = *bounds;
                build_input.studio_instances.push_back(instance);
                continue;
            }

            if (record->kind() ==
                entity_visual::EntityVisualAssetKind::sprite) {
                const auto asset_index = find_sprite_asset(scene_package, *record);
                if (!asset_index) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::render_asset_mismatch,
                        "Sprite library record has no exact package render asset",
                        entity.entity_number(),
                        reference);
                }
                const auto& render_asset =
                    *scene_package.sprite_assets()[*asset_index];
                if (const auto unsupported = unsupported_sprite_reason(
                        render_asset, entity.render_mode())) {
                    build_input.unsupported_instances.push_back({
                        entity.entity_number(),
                        static_cast<std::uint32_t>(*record_index),
                        *unsupported,
                        RuntimeEntityVisibilityStatus::unsupported_visual,
                    });
                    continue;
                }
                if (!record->sprite_asset()) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::render_asset_mismatch,
                        "Sprite library record has no immutable source asset",
                        entity.entity_number(),
                        reference);
                }
                const double animation_start =
                    entity.animation_start_time_seconds().value_or(0.0);
                sprite::SpritePlaybackInput playback;
                playback.top_level_entry_index = entity.sprite_frame_index();
                playback.elapsed_seconds =
                    interpolated_frame.sample_seconds() - animation_start;
                auto selected = sprite::SpriteFrameSelector{}.select(
                    *record->sprite_asset(), playback, sprite_limits);
                if (!selected || !selected.selection) {
                    auto result = fail(
                        EntityRenderFrameComposerErrorCode::
                            sprite_selection_failed,
                        selected.error
                            ? "Sprite frame selection failed: " +
                                selected.error->context
                            : "Sprite frame selection failed",
                        entity.entity_number(),
                        reference);
                    if (selected.error) {
                        result.error->sprite_playback_error =
                            selected.error->code;
                    }
                    return result;
                }
                const auto bounds = sprite_bounds(render_asset, transform);
                if (!bounds) {
                    return fail(
                        EntityRenderFrameComposerErrorCode::
                            non_finite_transformed_bounds,
                        "Sprite transformed bounds are non-finite",
                        entity.entity_number(),
                        reference);
                }
                SpriteEntityRenderInstance instance;
                instance.entity_number = entity.entity_number();
                instance.sprite_asset_index =
                    static_cast<std::uint32_t>(*asset_index);
                instance.selected_frame_index =
                    selected.selection->flattened_frame_index();
                instance.transform = transform;
                instance.orientation = render_asset.orientation();
                instance.texture_format_support =
                    render_asset.texture_support_status();
                instance.bounds = *bounds;
                build_input.sprite_instances.push_back(instance);
                continue;
            }

            build_input.unsupported_instances.push_back({
                entity.entity_number(),
                static_cast<std::uint32_t>(*record_index),
                UnsupportedEntityVisualReason::unsupported_asset_kind,
                RuntimeEntityVisibilityStatus::unsupported_visual,
            });
        }

        auto built = EntityRenderFrameBuilder{}.build(
            scene_package, std::move(build_input), limits);
        if (!built || !built.frame) {
            auto result = fail(
                EntityRenderFrameComposerErrorCode::render_frame_rejected,
                built.error
                    ? "Entity render frame builder rejected composition: " +
                        built.error->context
                    : "Entity render frame builder rejected composition");
            if (built.error) {
                result.error->render_frame_error = built.error->code;
                result.error->entity_number = built.error->entity_number;
            }
            return result;
        }
        return {std::move(built.frame), std::nullopt};
    } catch (const std::bad_alloc&) {
        return fail(
            EntityRenderFrameComposerErrorCode::unable_to_retain_composition,
            "Unable to retain atomic entity render frame composition");
    } catch (const std::length_error&) {
        return fail(EntityRenderFrameComposerErrorCode::source_limit_exceeded,
            "Entity render composition exceeds an owning container limit");
    }
}

} // namespace hlclient::entity_render

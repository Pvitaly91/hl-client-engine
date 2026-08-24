#include <hlclient/world_scene_render/world_scene_render_types.hpp>

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace hlclient::world_scene_render {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

class StableHasher final {
public:
    void add(const bool value) noexcept
    {
        add(static_cast<std::uint8_t>(value ? 1U : 0U));
    }

    template <typename Integer>
        requires std::is_integral_v<Integer> &&
            (!std::is_same_v<std::remove_cv_t<Integer>, bool>)
    void add(const Integer value) noexcept
    {
        using Unsigned = std::make_unsigned_t<Integer>;
        auto remaining = static_cast<Unsigned>(value);
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            value_ ^= static_cast<std::uint8_t>(remaining & 0xFFU);
            value_ *= kFnvPrime;
            remaining >>= 8U;
        }
    }

    template <typename Enumeration>
        requires std::is_enum_v<Enumeration>
    void add(const Enumeration value) noexcept
    {
        add(static_cast<std::underlying_type_t<Enumeration>>(value));
    }

    void add_float(const float value) noexcept
    {
        add(std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? kFnvOffsetBasis : value_;
    }

private:
    std::uint64_t value_{kFnvOffsetBasis};
};

void hash_vector(
    StableHasher& hasher,
    const assets::AssetVector3& value) noexcept
{
    hasher.add_float(value.x);
    hasher.add_float(value.y);
    hasher.add_float(value.z);
}

void hash_bounds(
    StableHasher& hasher,
    const assets::WorldBounds& value) noexcept
{
    hash_vector(hasher, value.minimum);
    hash_vector(hasher, value.maximum);
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return renderer::is_finite(bounds.minimum) &&
        renderer::is_finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool exact_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return std::bit_cast<std::uint32_t>(left.x) ==
            std::bit_cast<std::uint32_t>(right.x) &&
        std::bit_cast<std::uint32_t>(left.y) ==
            std::bit_cast<std::uint32_t>(right.y) &&
        std::bit_cast<std::uint32_t>(left.z) ==
            std::bit_cast<std::uint32_t>(right.z);
}

[[nodiscard]] bool exact_bounds(
    const assets::WorldBounds& left,
    const assets::WorldBounds& right) noexcept
{
    // The producer retains the same local-bound and matrix float payload used
    // here, so a bit-exact comparison is deterministic and admits no forged
    // expansion or contraction at this immutable-package boundary.
    return exact_vector(left.minimum, right.minimum) &&
        exact_vector(left.maximum, right.maximum);
}

[[nodiscard]] std::optional<assets::WorldBounds> transform_bounds(
    const assets::WorldBounds& local_bounds,
    const renderer::RenderMatrix4& transform) noexcept
{
    assets::WorldBounds result{};
    bool first = true;
    for (std::size_t corner = 0U; corner < 8U; ++corner) {
        const assets::AssetVector3 local_point{
            (corner & 1U) == 0U
                ? local_bounds.minimum.x
                : local_bounds.maximum.x,
            (corner & 2U) == 0U
                ? local_bounds.minimum.y
                : local_bounds.maximum.y,
            (corner & 4U) == 0U
                ? local_bounds.minimum.z
                : local_bounds.maximum.z,
        };
        const auto transformed = renderer::transform(transform,
            renderer::RenderHomogeneousVector{
                local_point.x, local_point.y, local_point.z, 1.0F});
        const assets::AssetVector3 world_point{
            transformed.x, transformed.y, transformed.z};
        if (!renderer::is_finite(world_point)) {
            return std::nullopt;
        }
        if (first) {
            result = {world_point, world_point};
            first = false;
            continue;
        }
        result.minimum.x = std::min(result.minimum.x, world_point.x);
        result.minimum.y = std::min(result.minimum.y, world_point.y);
        result.minimum.z = std::min(result.minimum.z, world_point.z);
        result.maximum.x = std::max(result.maximum.x, world_point.x);
        result.maximum.y = std::max(result.maximum.y, world_point.y);
        result.maximum.z = std::max(result.maximum.z, world_point.z);
    }
    return result;
}

void include_bounds(
    assets::WorldBounds& target,
    const assets::WorldBounds& added) noexcept
{
    target.minimum.x = std::min(target.minimum.x, added.minimum.x);
    target.minimum.y = std::min(target.minimum.y, added.minimum.y);
    target.minimum.z = std::min(target.minimum.z, added.minimum.z);
    target.maximum.x = std::max(target.maximum.x, added.maximum.x);
    target.maximum.y = std::max(target.maximum.y, added.maximum.y);
    target.maximum.z = std::max(target.maximum.z, added.maximum.z);
}

[[nodiscard]] bool valid_support_status(
    const BrushSubmodelRenderSupportStatus status) noexcept
{
    switch (status) {
    case BrushSubmodelRenderSupportStatus::supported_static_opaque:
    case BrushSubmodelRenderSupportStatus::unsupported_transform:
    case BrushSubmodelRenderSupportStatus::unsupported_rendermode:
    case BrushSubmodelRenderSupportStatus::invalid_model_reference:
    case BrushSubmodelRenderSupportStatus::missing_model_geometry:
    case BrushSubmodelRenderSupportStatus::invalid_entity_metadata:
    case BrushSubmodelRenderSupportStatus::outside_world_spatial_tree:
    case BrushSubmodelRenderSupportStatus::no_visible_leaf_membership:
        return true;
    }
    return false;
}

[[nodiscard]] WorldSceneRenderPackageBuildResult fail(
    const WorldSceneRenderErrorCode code,
    const std::optional<std::size_t> index,
    std::string context)
{
    return {std::nullopt, WorldSceneRenderError{code, index, std::move(context)}};
}

[[nodiscard]] world_visibility::WorldVisibleSurfaceInput surface_input(
    const world_render::WorldRenderSurfaceRange& range) noexcept
{
    return {
        static_cast<std::uint32_t>(range.source_world_surface_index),
        range.first_index,
        range.index_count,
        range.render_material_index,
        range.bounds,
        range.alpha_mode,
        range.lightmap_mode,
        range.lightmap_atlas_page_index,
    };
}

} // namespace

std::string_view to_string(const WorldSceneRenderErrorCode code) noexcept
{
    switch (code) {
    case WorldSceneRenderErrorCode::invalid_world_package:
        return "invalid_world_package";
    case WorldSceneRenderErrorCode::invalid_spatial_package:
        return "invalid_spatial_package";
    case WorldSceneRenderErrorCode::invalid_world_surface_range:
        return "invalid_world_surface_range";
    case WorldSceneRenderErrorCode::invalid_brush_library:
        return "invalid_brush_library";
    case WorldSceneRenderErrorCode::invalid_brush_model:
        return "invalid_brush_model";
    case WorldSceneRenderErrorCode::duplicate_brush_model:
        return "duplicate_brush_model";
    case WorldSceneRenderErrorCode::duplicate_brush_surface:
        return "duplicate_brush_surface";
    case WorldSceneRenderErrorCode::invalid_brush_instance:
        return "invalid_brush_instance";
    case WorldSceneRenderErrorCode::duplicate_brush_instance:
        return "duplicate_brush_instance";
    case WorldSceneRenderErrorCode::missing_brush_model:
        return "missing_brush_model";
    case WorldSceneRenderErrorCode::invalid_scene_bounds:
        return "invalid_scene_bounds";
    case WorldSceneRenderErrorCode::unable_to_retain_scene:
        return "unable_to_retain_scene";
    }
    return "unknown";
}

BrushSubmodelRenderModel::BrushSubmodelRenderModel(
    const std::uint32_t source_model_index,
    const assets::WorldBounds local_bounds,
    std::vector<std::uint32_t> render_surface_indices,
    std::vector<world_visibility::WorldVisibleSurfaceInput> surfaces) noexcept
    : source_model_index_{source_model_index},
      local_bounds_{local_bounds},
      render_surface_indices_{std::move(render_surface_indices)},
      surfaces_{std::move(surfaces)}
{
}

std::uint32_t BrushSubmodelRenderModel::source_model_index() const noexcept
{
    return source_model_index_;
}

const assets::WorldBounds& BrushSubmodelRenderModel::local_bounds() const noexcept
{
    return local_bounds_;
}

std::span<const std::uint32_t>
BrushSubmodelRenderModel::render_surface_indices() const noexcept
{
    return render_surface_indices_;
}

std::span<const world_visibility::WorldVisibleSurfaceInput>
BrushSubmodelRenderModel::surfaces() const noexcept
{
    return surfaces_;
}

BrushSubmodelRenderLibrary::BrushSubmodelRenderLibrary(
    std::shared_ptr<const world_render::WorldRenderPackage> render_package,
    std::vector<BrushSubmodelRenderModel> models) noexcept
    : render_package_{std::move(render_package)},
      models_{std::move(models)}
{
}

const std::shared_ptr<const world_render::WorldRenderPackage>&
BrushSubmodelRenderLibrary::render_package() const noexcept
{
    return render_package_;
}

std::span<const BrushSubmodelRenderModel>
BrushSubmodelRenderLibrary::models() const noexcept
{
    return models_;
}

WorldSceneRenderPackage::WorldSceneRenderPackage(
    std::shared_ptr<const world_render::WorldRenderPackage> world_package,
    world_spatial::WorldSpatialPackage spatial_package,
    BrushSubmodelRenderLibrary brush_library,
    std::vector<BrushSubmodelRenderInstance> brush_instances,
    std::vector<world_visibility::WorldVisibleSurfaceInput> world_surfaces,
    const assets::WorldBounds bounds,
    const WorldSceneRenderStatistics statistics,
    const WorldSceneRendererResourceIdentity resource_identity,
    const world_visibility::WorldVisibilitySceneIdentity
        visibility_scene_identity) noexcept
    : world_package_{std::move(world_package)},
      spatial_package_{std::move(spatial_package)},
      brush_library_{std::move(brush_library)},
      brush_instances_{std::move(brush_instances)},
      world_surfaces_{std::move(world_surfaces)},
      bounds_{bounds},
      statistics_{statistics},
      resource_identity_{resource_identity},
      visibility_scene_identity_{visibility_scene_identity}
{
}

const std::shared_ptr<const world_render::WorldRenderPackage>&
WorldSceneRenderPackage::world_package() const noexcept
{
    return world_package_;
}

const world_spatial::WorldSpatialPackage&
WorldSceneRenderPackage::spatial_package() const noexcept
{
    return spatial_package_;
}

const BrushSubmodelRenderLibrary&
WorldSceneRenderPackage::brush_library() const noexcept
{
    return brush_library_;
}

std::span<const BrushSubmodelRenderInstance>
WorldSceneRenderPackage::brush_instances() const noexcept
{
    return brush_instances_;
}

std::span<const world_visibility::WorldVisibleSurfaceInput>
WorldSceneRenderPackage::world_surfaces() const noexcept
{
    return world_surfaces_;
}

const assets::WorldBounds& WorldSceneRenderPackage::bounds() const noexcept
{
    return bounds_;
}

const WorldSceneRenderStatistics&
WorldSceneRenderPackage::statistics() const noexcept
{
    return statistics_;
}

WorldSceneRendererResourceIdentity
WorldSceneRenderPackage::resource_identity() const noexcept
{
    return resource_identity_;
}

world_visibility::WorldVisibilitySceneIdentity
WorldSceneRenderPackage::visibility_scene_identity() const noexcept
{
    return visibility_scene_identity_;
}

std::uint64_t WorldSceneRenderPackage::resource_id() const noexcept
{
    return resource_identity_.resource_id;
}

std::uint64_t WorldSceneRenderPackage::resource_revision() const noexcept
{
    return resource_identity_.revision;
}

WorldSceneRenderCompatibilityProfile
WorldSceneRenderPackage::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

WorldSceneRenderEvidenceProfile
WorldSceneRenderPackage::evidence_profile() const noexcept
{
    return evidence_profile_;
}

WorldSceneRenderPackageBuildResult WorldSceneRenderPackageBuilder::build(
    std::shared_ptr<const world_render::WorldRenderPackage> world_package,
    world_spatial::WorldSpatialPackage spatial_package,
    BrushSubmodelRenderLibrary brush_library,
    std::vector<BrushSubmodelRenderInstance> brush_instances,
    const WorldSceneRenderPackageLimits& limits) const
{
    try {
        if (world_package == nullptr || limits.maximum_world_surfaces == 0U ||
            limits.maximum_brush_models == 0U ||
            limits.maximum_brush_surfaces == 0U ||
            limits.maximum_brush_instances == 0U ||
            limits.maximum_instance_leaf_links == 0U) {
            return fail(WorldSceneRenderErrorCode::invalid_world_package,
                std::nullopt,
                "Scene package requires a world package and positive limits");
        }
        const auto world_ranges = world_package->surface_ranges();
        if (world_ranges.empty() ||
            world_ranges.size() !=
                world_package->textured_world().world.surfaces.size() ||
            world_ranges.size() > limits.maximum_world_surfaces ||
            !valid_bounds(world_package->bounds())) {
            return fail(WorldSceneRenderErrorCode::invalid_world_package,
                std::nullopt,
                "World package has incomplete surface ranges or invalid bounds");
        }

        std::vector<world_visibility::WorldVisibleSurfaceInput> world_surfaces;
        world_surfaces.reserve(world_ranges.size());
        for (std::size_t index = 0U; index < world_ranges.size(); ++index) {
            const auto& range = world_ranges[index];
            const std::size_t first = range.first_index;
            const std::size_t count = range.index_count;
            if (range.source_world_surface_index != index || count == 0U ||
                count % 3U != 0U ||
                first > world_package->indices().size() ||
                count > world_package->indices().size() - first ||
                range.render_material_index >= world_package->materials().size() ||
                !valid_bounds(range.bounds)) {
                return fail(
                    WorldSceneRenderErrorCode::invalid_world_surface_range,
                    index,
                    "World per-surface render range is not exact");
            }
            world_surfaces.push_back(surface_input(range));
        }

        if (spatial_package.leaves().empty() ||
            spatial_package.nodes().empty() ||
            spatial_package.world_model().root_node_index >=
                spatial_package.nodes().size()) {
            return fail(WorldSceneRenderErrorCode::invalid_spatial_package,
                std::nullopt,
                "Spatial package does not retain a valid world root");
        }
        for (const auto& leaf : spatial_package.leaves()) {
            for (const auto surface_index :
                leaf.surface_membership.world_surface_indices) {
                if (surface_index >= world_surfaces.size()) {
                    return fail(
                        WorldSceneRenderErrorCode::invalid_spatial_package,
                        leaf.source_leaf_index,
                        "Spatial leaf references an unavailable world surface");
                }
            }
        }

        if ((brush_library.render_package_ == nullptr) !=
            brush_library.models_.empty()) {
            return fail(WorldSceneRenderErrorCode::invalid_brush_library,
                std::nullopt,
                "Brush library package and model table must be both present or absent");
        }
        if (brush_library.models_.size() > limits.maximum_brush_models) {
            return fail(WorldSceneRenderErrorCode::invalid_brush_library,
                brush_library.models_.size(),
                "Brush model count exceeds the configured limit");
        }

        std::vector<std::uint32_t> model_ordinals;
        std::vector<std::uint8_t> brush_surface_covered;
        std::size_t brush_surface_count = 0U;
        if (brush_library.render_package_ != nullptr) {
            const auto brush_ranges =
                brush_library.render_package_->surface_ranges();
            if (brush_ranges.size() > limits.maximum_brush_surfaces) {
                return fail(WorldSceneRenderErrorCode::invalid_brush_library,
                    brush_ranges.size(),
                    "Brush surface count exceeds the configured limit");
            }
            brush_surface_covered.assign(brush_ranges.size(), 0U);
            model_ordinals.reserve(brush_library.models_.size());
            for (std::size_t model_index = 0U;
                 model_index < brush_library.models_.size(); ++model_index) {
                auto& model = brush_library.models_[model_index];
                if (model.source_model_index_ == 0U ||
                    !valid_bounds(model.local_bounds_) ||
                    model.render_surface_indices_.empty()) {
                    return fail(WorldSceneRenderErrorCode::invalid_brush_model,
                        model_index,
                        "Brush model metadata is empty or invalid");
                }
                model_ordinals.push_back(model.source_model_index_);
                model.surfaces_.clear();
                model.surfaces_.reserve(model.render_surface_indices_.size());
                for (const auto surface_index : model.render_surface_indices_) {
                    if (surface_index >= brush_ranges.size()) {
                        return fail(WorldSceneRenderErrorCode::invalid_brush_model,
                            model_index,
                            "Brush model surface index is out of range");
                    }
                    if (brush_surface_covered[surface_index] != 0U) {
                        return fail(
                            WorldSceneRenderErrorCode::duplicate_brush_surface,
                            surface_index,
                            "Brush surface belongs to more than one render model");
                    }
                    brush_surface_covered[surface_index] = 1U;
                    model.surfaces_.push_back(surface_input(
                        brush_ranges[surface_index]));
                }
                brush_surface_count += model.surfaces_.size();
            }
            std::ranges::sort(model_ordinals);
            if (std::adjacent_find(model_ordinals.begin(), model_ordinals.end()) !=
                model_ordinals.end()) {
                return fail(WorldSceneRenderErrorCode::duplicate_brush_model,
                    std::nullopt,
                    "Brush source-model ordinals must be unique");
            }
            if (!std::ranges::all_of(brush_surface_covered, [](const auto value) {
                    return value != 0U;
                })) {
                return fail(WorldSceneRenderErrorCode::invalid_brush_library,
                    std::nullopt,
                    "Brush model table does not cover every aggregate surface");
            }
        }

        if (brush_instances.size() > limits.maximum_brush_instances) {
            return fail(WorldSceneRenderErrorCode::invalid_brush_instance,
                brush_instances.size(),
                "Brush instance count exceeds the configured limit");
        }
        std::vector<std::uint32_t> instance_ordinals;
        instance_ordinals.reserve(brush_instances.size());
        std::size_t total_leaf_links = 0U;
        std::size_t supported_instances = 0U;
        assets::WorldBounds scene_bounds = world_package->bounds();
        for (std::size_t index = 0U; index < brush_instances.size(); ++index) {
            const auto& instance = brush_instances[index];
            if (!valid_support_status(instance.support_status) ||
                !renderer::is_finite(instance.model_transform) ||
                !valid_bounds(instance.transformed_bounds) ||
                instance.touched_leaf_indices.size() >
                    std::numeric_limits<std::size_t>::max() - total_leaf_links) {
                return fail(WorldSceneRenderErrorCode::invalid_brush_instance,
                    index,
                    "Brush instance transform, bounds, status, or leaf count is invalid");
            }
            total_leaf_links += instance.touched_leaf_indices.size();
            if (total_leaf_links > limits.maximum_instance_leaf_links ||
                !std::ranges::is_sorted(instance.touched_leaf_indices) ||
                std::adjacent_find(instance.touched_leaf_indices.begin(),
                    instance.touched_leaf_indices.end()) !=
                    instance.touched_leaf_indices.end()) {
                return fail(WorldSceneRenderErrorCode::invalid_brush_instance,
                    index,
                    "Brush instance leaf membership is not bounded and deduplicated");
            }
            instance_ordinals.push_back(instance.source_instance_index);
            if (instance.support_status ==
                BrushSubmodelRenderSupportStatus::supported_static_opaque) {
                if (!instance.source_model_index) {
                    return fail(WorldSceneRenderErrorCode::missing_brush_model,
                        index,
                        "Supported brush instance has no render model");
                }
                const auto model = std::ranges::find_if(
                    brush_library.models_,
                    [&instance](const BrushSubmodelRenderModel& candidate) {
                        return candidate.source_model_index_ ==
                            *instance.source_model_index;
                    });
                if (model == brush_library.models_.end()) {
                    return fail(WorldSceneRenderErrorCode::missing_brush_model,
                        index,
                        "Supported brush instance has no render model");
                }
                if (instance.touched_leaf_indices.empty()) {
                    return fail(WorldSceneRenderErrorCode::invalid_brush_instance,
                        index,
                        "Supported brush instance has no visible leaf membership");
                }
                const auto spatial_leaves = spatial_package.leaves();
                for (const auto leaf_index : instance.touched_leaf_indices) {
                    if (leaf_index == 0U ||
                        static_cast<std::size_t>(leaf_index) >=
                            spatial_leaves.size() ||
                        spatial_leaves[leaf_index].solid_or_special ||
                        !spatial_leaves[leaf_index].pvs_bit_addressable) {
                        return fail(
                            WorldSceneRenderErrorCode::invalid_brush_instance,
                            index,
                            "Supported brush instance references an invalid visibility leaf");
                    }
                }
                const auto expected_bounds =
                    transform_bounds(model->local_bounds_, instance.model_transform);
                if (!expected_bounds ||
                    !exact_bounds(*expected_bounds, instance.transformed_bounds)) {
                    return fail(WorldSceneRenderErrorCode::invalid_brush_instance,
                        index,
                        "Supported brush instance bounds do not exactly match its model transform");
                }
                ++supported_instances;
                include_bounds(scene_bounds, instance.transformed_bounds);
            }
        }
        std::ranges::sort(instance_ordinals);
        if (std::adjacent_find(instance_ordinals.begin(), instance_ordinals.end()) !=
            instance_ordinals.end()) {
            return fail(WorldSceneRenderErrorCode::duplicate_brush_instance,
                std::nullopt,
                "Brush source-instance ordinals must be unique");
        }
        if (!valid_bounds(scene_bounds)) {
            return fail(WorldSceneRenderErrorCode::invalid_scene_bounds,
                std::nullopt,
                "Scene bounds union is invalid");
        }

        StableHasher identity_hash;
        identity_hash.add(world_package->resource_id());
        identity_hash.add(brush_library.render_package_
                ? brush_library.render_package_->resource_id()
                : 0U);
        StableHasher revision_hash;
        revision_hash.add(world_package->resource_revision());
        revision_hash.add(brush_library.render_package_
                ? brush_library.render_package_->resource_revision()
                : 0U);

        std::vector<world_visibility::WorldVisibilityBrushInstanceInput>
            visibility_instances;
        std::vector<world_visibility::WorldVisibleBrushModelInput>
            draw_models;
        std::vector<world_visibility::WorldVisibleBrushInstanceDrawInput>
            draw_instances;
        visibility_instances.reserve(brush_instances.size());
        draw_instances.reserve(supported_instances);
        for (const auto& instance : brush_instances) {
            const bool supported = instance.support_status ==
                    BrushSubmodelRenderSupportStatus::supported_static_opaque &&
                instance.source_model_index.has_value();
            visibility_instances.push_back({
                instance.source_instance_index,
                instance.transformed_bounds,
                instance.touched_leaf_indices,
                supported,
            });
            if (supported) {
                draw_instances.push_back({
                    instance.source_instance_index,
                    *instance.source_model_index,
                    instance.model_transform,
                });
            }
        }
        if (brush_library.render_package_) {
            draw_models.reserve(brush_library.models_.size());
            for (const auto& model : brush_library.models_) {
                draw_models.push_back({
                    model.source_model_index_,
                    brush_library.render_package_->indices().size(),
                    brush_library.render_package_->materials().size(),
                    model.surfaces_,
                });
            }
        }

        world_visibility::WorldVisibilityResolveInput visibility_signature_input;
        visibility_signature_input.spatial_package = &spatial_package;
        visibility_signature_input.world_surfaces = world_surfaces;
        visibility_signature_input.brush_instances = visibility_instances;
        const auto visibility_signature =
            world_visibility::world_visibility_input_signature(
                visibility_signature_input);
        const world_visibility::WorldVisibleDrawListBuildInput
            draw_signature_input{
                nullptr,
                world_surfaces,
                world_package->indices().size(),
                world_package->materials().size(),
                draw_models,
                draw_instances,
            };
        const auto draw_signature =
            world_visibility::world_visible_draw_input_signature(
                draw_signature_input);
        revision_hash.add(visibility_signature);
        revision_hash.add(draw_signature);
        hash_bounds(revision_hash, scene_bounds);
        revision_hash.add(static_cast<std::uint64_t>(
            brush_library.models_.size()));
        for (const auto& model : brush_library.models_) {
            revision_hash.add(model.source_model_index_);
            hash_bounds(revision_hash, model.local_bounds_);
            revision_hash.add(static_cast<std::uint64_t>(
                model.render_surface_indices_.size()));
            for (const auto surface_index : model.render_surface_indices_) {
                revision_hash.add(surface_index);
            }
        }
        revision_hash.add(static_cast<std::uint64_t>(brush_instances.size()));
        for (const auto& instance : brush_instances) {
            revision_hash.add(instance.source_instance_index);
            revision_hash.add(static_cast<std::uint64_t>(
                instance.source_entity_ordinal));
            revision_hash.add(instance.source_model_index.value_or(0U));
            revision_hash.add(instance.support_status);
        }

        const WorldSceneRendererResourceIdentity resource_identity{
            identity_hash.value(), revision_hash.value()};
        const world_visibility::WorldVisibilitySceneIdentity
            visibility_scene_identity{
                resource_identity.resource_id,
                resource_identity.revision,
                visibility_signature,
                draw_signature,
            };

        const WorldSceneRenderStatistics statistics{
            world_surfaces.size(),
            brush_library.models_.size(),
            brush_surface_count,
            brush_instances.size(),
            supported_instances,
            brush_instances.size() - supported_instances,
        };
        return {
            WorldSceneRenderPackage{
                std::move(world_package),
                std::move(spatial_package),
                std::move(brush_library),
                std::move(brush_instances),
                std::move(world_surfaces),
                scene_bounds,
                statistics,
                resource_identity,
                visibility_scene_identity,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail(WorldSceneRenderErrorCode::unable_to_retain_scene,
            std::nullopt,
            "Unable to retain immutable world scene package");
    } catch (const std::length_error&) {
        return fail(WorldSceneRenderErrorCode::unable_to_retain_scene,
            std::nullopt,
            "World scene package exceeds an owning container limit");
    }
}

} // namespace hlclient::world_scene_render

#include <hlclient/goldsrc/brush_models/brush_submodel_instances.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::brush_models {
namespace {

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite_vector(bounds.minimum) && finite_vector(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] BrushSubmodelInstanceBuildResult fail(
    const BrushSubmodelInstanceBuildErrorCode code,
    const std::optional<std::size_t> entity_ordinal = std::nullopt,
    const std::optional<std::uint32_t> model_index = std::nullopt) noexcept
{
    return {
        std::nullopt,
        BrushSubmodelInstanceBuildError{code, entity_ordinal, model_index},
    };
}

void record_status(
    BrushSubmodelInstanceStatistics& statistics,
    const BrushSubmodelInstanceStatus status) noexcept
{
    switch (status) {
    case BrushSubmodelInstanceStatus::supported_static_opaque:
        ++statistics.supported_static_opaque_count;
        break;
    case BrushSubmodelInstanceStatus::unsupported_transform:
        ++statistics.unsupported_transform_count;
        break;
    case BrushSubmodelInstanceStatus::unsupported_rendermode:
        ++statistics.unsupported_rendermode_count;
        break;
    case BrushSubmodelInstanceStatus::invalid_model_reference:
        ++statistics.invalid_model_reference_count;
        break;
    case BrushSubmodelInstanceStatus::missing_model_geometry:
        ++statistics.missing_model_geometry_count;
        break;
    case BrushSubmodelInstanceStatus::invalid_entity_metadata:
        ++statistics.invalid_entity_metadata_count;
        break;
    case BrushSubmodelInstanceStatus::outside_world_spatial_tree:
        ++statistics.outside_world_spatial_tree_count;
        break;
    case BrushSubmodelInstanceStatus::no_visible_leaf_membership:
        ++statistics.no_visible_leaf_membership_count;
        break;
    }
}

[[nodiscard]] const BrushSubmodelModelMetadata* find_model(
    const std::span<const BrushSubmodelModelMetadata> models,
    const std::uint32_t source_model_index) noexcept
{
    const auto iterator = std::lower_bound(models.begin(),
        models.end(),
        source_model_index,
        [](const BrushSubmodelModelMetadata& model,
            const std::uint32_t index) {
            return model.source_model_index < index;
        });
    return iterator != models.end() &&
            iterator->source_model_index == source_model_index
        ? &*iterator
        : nullptr;
}

} // namespace

bool valid_brush_submodel_instance_build_limits(
    const BrushSubmodelInstanceBuildLimits& limits) noexcept
{
    return limits.maximum_instances > 0U &&
        limits.maximum_instances <= kHardMaximumBrushSubmodelInstances &&
        limits.maximum_touched_leaf_links > 0U &&
        limits.maximum_touched_leaf_links <=
            kHardMaximumBrushTouchedLeafLinks &&
        limits.maximum_source_models > 0U &&
        limits.maximum_source_models <= kHardMaximumBrushSourceModels &&
        world_spatial::valid_world_spatial_query_limits(
            limits.spatial_query_limits);
}

BrushSubmodelInstanceSet::BrushSubmodelInstanceSet(
    std::vector<BrushSubmodelInstance> instances,
    const BrushSubmodelInstanceStatistics statistics) noexcept
    : instances_{std::move(instances)}, statistics_{statistics}
{
}

std::span<const BrushSubmodelInstance> BrushSubmodelInstanceSet::instances()
    const noexcept
{
    return instances_;
}

const BrushSubmodelInstanceStatistics& BrushSubmodelInstanceSet::statistics()
    const noexcept
{
    return statistics_;
}

std::size_t BrushSubmodelInstanceSet::size() const noexcept
{
    return instances_.size();
}

bool BrushSubmodelInstanceSet::empty() const noexcept
{
    return instances_.empty();
}

std::string_view to_string(const BrushSubmodelInstanceBuildErrorCode code) noexcept
{
    switch (code) {
    case BrushSubmodelInstanceBuildErrorCode::invalid_configuration:
        return "invalid_configuration";
    case BrushSubmodelInstanceBuildErrorCode::invalid_source_model_count:
        return "invalid_source_model_count";
    case BrushSubmodelInstanceBuildErrorCode::invalid_model_metadata:
        return "invalid_model_metadata";
    case BrushSubmodelInstanceBuildErrorCode::instance_limit_exceeded:
        return "instance_limit_exceeded";
    case BrushSubmodelInstanceBuildErrorCode::touched_leaf_limit_exceeded:
        return "touched_leaf_limit_exceeded";
    case BrushSubmodelInstanceBuildErrorCode::unable_to_retain_instances:
        return "unable_to_retain_instances";
    }
    return "unknown";
}

BrushSubmodelInstanceBuildResult BrushSubmodelInstanceSetBuilder::build(
    const bsp::GoldSrcEntityDocument& entity_document,
    const std::span<const BrushSubmodelModelMetadata> ordered_models,
    const std::size_t source_model_count,
    const world_spatial::WorldSpatialPackage& spatial_package,
    const BrushSubmodelInstanceBuildLimits& limits)
{
    if (!valid_brush_submodel_instance_build_limits(limits)) {
        return fail(BrushSubmodelInstanceBuildErrorCode::invalid_configuration);
    }
    if (source_model_count == 0U ||
        source_model_count > limits.maximum_source_models ||
        ordered_models.size() >= source_model_count ||
        ordered_models.size() > limits.maximum_source_models - 1U) {
        return fail(
            BrushSubmodelInstanceBuildErrorCode::invalid_source_model_count);
    }
    std::uint32_t previous_model_index = 0U;
    for (const auto& model : ordered_models) {
        if (model.source_model_index == 0U ||
            static_cast<std::size_t>(model.source_model_index) >=
                source_model_count ||
            model.source_model_index <= previous_model_index ||
            !finite_vector(model.source_model_origin) ||
            (model.geometry_present && !valid_bounds(model.local_bounds))) {
            return fail(
                BrushSubmodelInstanceBuildErrorCode::invalid_model_metadata,
                std::nullopt,
                model.source_model_index);
        }
        previous_model_index = model.source_model_index;
    }

    std::vector<BrushSubmodelInstance> instances;
    BrushSubmodelInstanceStatistics statistics;
    statistics.source_entity_count = entity_document.size();
    try {
        instances.reserve(std::min({
            entity_document.size(),
            limits.maximum_instances,
            static_cast<std::size_t>(128U),
        }));
        const auto entities = entity_document.entities();
        for (std::size_t entity_ordinal = 0U;
             entity_ordinal < entities.size();
             ++entity_ordinal) {
            const auto interpreted = interpret_brush_entity(
                entities[entity_ordinal],
                entity_ordinal,
                source_model_count);
            if (!interpreted.metadata) {
                continue;
            }
            if (instances.size() == limits.maximum_instances) {
                return fail(
                    BrushSubmodelInstanceBuildErrorCode::instance_limit_exceeded,
                    entity_ordinal,
                    interpreted.metadata->source_model_index);
            }
            ++statistics.brush_candidate_count;

            BrushSubmodelInstance instance;
            instance.source_entity_ordinal = entity_ordinal;
            instance.source_model_index =
                interpreted.metadata->source_model_index;
            instance.classname_category =
                interpreted.metadata->classname_category;
            instance.status = interpreted.metadata->status;

            if (instance.status ==
                    BrushSubmodelInstanceStatus::invalid_model_reference ||
                instance.status ==
                    BrushSubmodelInstanceStatus::invalid_entity_metadata ||
                !instance.source_model_index) {
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }

            const auto* model = find_model(
                ordered_models, *instance.source_model_index);
            if (model == nullptr || !model->geometry_present) {
                instance.status =
                    BrushSubmodelInstanceStatus::missing_model_geometry;
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }
            if (instance.status ==
                BrushSubmodelInstanceStatus::unsupported_transform) {
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }

            const auto built_transform = make_brush_submodel_transform(
                interpreted.metadata->origin,
                interpreted.metadata->angles_degrees,
                model->source_model_origin);
            if (!built_transform) {
                instance.status =
                    BrushSubmodelInstanceStatus::unsupported_transform;
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }
            const auto transformed_bounds = transform_brush_bounds(
                model->local_bounds, *built_transform.transform);
            if (!transformed_bounds) {
                instance.status =
                    BrushSubmodelInstanceStatus::unsupported_transform;
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }
            instance.transform = *built_transform.transform;
            instance.transformed_bounds = *transformed_bounds.bounds;

            if (instance.status ==
                BrushSubmodelInstanceStatus::unsupported_rendermode) {
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }
            if (instance.status !=
                BrushSubmodelInstanceStatus::supported_static_opaque) {
                instance.status =
                    BrushSubmodelInstanceStatus::invalid_entity_metadata;
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }

            const auto touched =
                world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
                    spatial_package,
                    *instance.transformed_bounds,
                    limits.spatial_query_limits);
            if (!touched) {
                instance.status =
                    BrushSubmodelInstanceStatus::outside_world_spatial_tree;
                instance.spatial_query_error = touched.error->code;
                record_status(statistics, instance.status);
                instances.push_back(std::move(instance));
                continue;
            }

            const auto spatial_leaves = spatial_package.leaves();
            instance.touched_world_leaves.reserve(
                touched.result->leaf_indices.size());
            bool invalid_leaf_reference = false;
            for (const auto leaf_index : touched.result->leaf_indices) {
                if (static_cast<std::size_t>(leaf_index) >=
                    spatial_leaves.size()) {
                    invalid_leaf_reference = true;
                    break;
                }
                const auto& leaf = spatial_leaves[leaf_index];
                if (leaf_index == 0U || leaf.solid_or_special ||
                    !leaf.pvs_bit_addressable) {
                    continue;
                }
                if (statistics.touched_leaf_link_count ==
                    limits.maximum_touched_leaf_links) {
                    return fail(
                        BrushSubmodelInstanceBuildErrorCode::
                            touched_leaf_limit_exceeded,
                        entity_ordinal,
                        instance.source_model_index);
                }
                instance.touched_world_leaves.push_back(leaf_index);
                ++statistics.touched_leaf_link_count;
            }
            if (invalid_leaf_reference) {
                instance.touched_world_leaves.clear();
                instance.status =
                    BrushSubmodelInstanceStatus::outside_world_spatial_tree;
                instance.spatial_query_error =
                    world_spatial::WorldSpatialQueryErrorCode::
                        invalid_child_reference;
            } else if (instance.touched_world_leaves.empty()) {
                instance.status =
                    BrushSubmodelInstanceStatus::no_visible_leaf_membership;
            } else {
                instance.status =
                    BrushSubmodelInstanceStatus::supported_static_opaque;
            }
            record_status(statistics, instance.status);
            instances.push_back(std::move(instance));
        }
    } catch (const std::bad_alloc&) {
        return fail(
            BrushSubmodelInstanceBuildErrorCode::unable_to_retain_instances);
    } catch (const std::length_error&) {
        return fail(
            BrushSubmodelInstanceBuildErrorCode::unable_to_retain_instances);
    }

    return {
        BrushSubmodelInstanceSet{std::move(instances), statistics},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc::brush_models

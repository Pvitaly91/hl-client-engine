#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::brush_models {
namespace {

[[nodiscard]] bool same_vector(
    const assets::AssetVector3& left,
    const assets::AssetVector3& right) noexcept
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool same_bounds(
    const assets::WorldBounds& left,
    const assets::WorldBounds& right) noexcept
{
    return same_vector(left.minimum, right.minimum) &&
        same_vector(left.maximum, right.maximum);
}

[[nodiscard]] bool world_package_matches_document(
    const world_render::WorldRenderPackage& package,
    const bsp::GoldSrcBspParsedDocument& document) noexcept
{
    const auto& retained_world = package.textured_world().world;
    if (!retained_world.source_content_fingerprint ||
        retained_world.source_content_fingerprint !=
            document.world_asset.source_content_fingerprint ||
        !same_bounds(retained_world.bounds, document.world_asset.bounds) ||
        retained_world.surfaces.size() !=
            document.world_asset.surfaces.size()) {
        return false;
    }
    for (std::size_t index = 0U;
         index < document.world_asset.surfaces.size();
         ++index) {
        const auto& retained = retained_world.surfaces[index];
        const auto& source = document.world_asset.surfaces[index];
        if (retained.source_surface_ordinal != source.source_surface_ordinal ||
            retained.first_index != source.first_index ||
            retained.index_count != source.index_count ||
            retained.first_vertex != source.first_vertex ||
            retained.vertex_count != source.vertex_count ||
            !same_bounds(retained.bounds, source.bounds)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool positive_scene_limits(
    const world_scene_render::WorldSceneRenderPackageLimits& limits) noexcept
{
    return limits.maximum_world_surfaces > 0U &&
        limits.maximum_brush_models > 0U &&
        limits.maximum_brush_surfaces > 0U &&
        limits.maximum_brush_instances > 0U &&
        limits.maximum_instance_leaf_links > 0U;
}

[[nodiscard]] bool library_matches_document(
    const world_scene_render::BrushSubmodelRenderLibrary& library,
    const bsp::GoldSrcBspParsedDocument& document) noexcept
{
    if (library.render_package() != nullptr &&
        (!library.render_package()->textured_world().world
                 .source_content_fingerprint ||
            library.render_package()->textured_world().world
                    .source_content_fingerprint !=
                document.world_asset.source_content_fingerprint)) {
        return false;
    }
    const auto render_models = library.models();
    for (std::size_t model_ordinal = 0U;
         model_ordinal < render_models.size();
         ++model_ordinal) {
        const auto& render_model = render_models[model_ordinal];
        for (std::size_t previous = 0U; previous < model_ordinal; ++previous) {
            if (render_models[previous].source_model_index() ==
                render_model.source_model_index()) {
                return false;
            }
        }
        const auto source_model = std::ranges::find_if(
            document.brush_submodels,
            [&render_model](const bsp::GoldSrcBspBrushSubmodelAsset& model) {
                return model.source_model_index ==
                    render_model.source_model_index();
            });
        if (source_model == document.brush_submodels.end() ||
            source_model->geometry.source_content_fingerprint !=
                document.world_asset.source_content_fingerprint ||
            !same_bounds(
                source_model->geometry.bounds, render_model.local_bounds())) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool library_has_model(
    const world_scene_render::BrushSubmodelRenderLibrary& library,
    const std::uint32_t source_model_index) noexcept
{
    return std::ranges::any_of(library.models(),
        [source_model_index](
            const world_scene_render::BrushSubmodelRenderModel& model) {
            return model.source_model_index() == source_model_index;
        });
}

[[nodiscard]] GoldSrcWorldSceneBuildResult failure(
    GoldSrcWorldSceneBuildError error,
    const GoldSrcWorldSceneBuildStatistics& statistics) noexcept
{
    GoldSrcWorldSceneBuildResult output;
    output.error = std::move(error);
    output.statistics = statistics;
    return output;
}

[[nodiscard]] GoldSrcWorldSceneBuildResult failure(
    const GoldSrcWorldSceneBuildErrorCode code,
    const GoldSrcWorldSceneBuildStatistics& statistics) noexcept
{
    return failure(GoldSrcWorldSceneBuildError{code}, statistics);
}

} // namespace

std::string_view to_string(const GoldSrcWorldSceneBrushMode mode) noexcept
{
    switch (mode) {
    case GoldSrcWorldSceneBrushMode::off: return "off";
    case GoldSrcWorldSceneBrushMode::static_initial: return "static";
    }
    return "unknown";
}

bool valid_goldsrc_world_scene_build_config(
    const GoldSrcWorldSceneBuildConfig& config) noexcept
{
    switch (config.brushes) {
    case GoldSrcWorldSceneBrushMode::off:
    case GoldSrcWorldSceneBrushMode::static_initial: return true;
    }
    return false;
}

bool valid_goldsrc_world_scene_build_limits(
    const GoldSrcWorldSceneBuildLimits& limits) noexcept
{
    return spatial::valid_goldsrc_spatial_import_limits(limits.spatial) &&
        bsp::valid_goldsrc_entity_document_limits(limits.entities) &&
        valid_brush_submodel_instance_build_limits(limits.instances) &&
        positive_scene_limits(limits.scene);
}

std::string_view to_string(const GoldSrcWorldSceneBuildErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcWorldSceneBuildErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcWorldSceneBuildErrorCode::invalid_world_package:
        return "invalid_world_package";
    case GoldSrcWorldSceneBuildErrorCode::world_package_document_mismatch:
        return "world_package_document_mismatch";
    case GoldSrcWorldSceneBuildErrorCode::spatial_package_build_failed:
        return "spatial_package_build_failed";
    case GoldSrcWorldSceneBuildErrorCode::entity_document_parse_failed:
        return "entity_document_parse_failed";
    case GoldSrcWorldSceneBuildErrorCode::invalid_brush_model_table:
        return "invalid_brush_model_table";
    case GoldSrcWorldSceneBuildErrorCode::brush_library_document_mismatch:
        return "brush_library_document_mismatch";
    case GoldSrcWorldSceneBuildErrorCode::brush_instance_set_build_failed:
        return "brush_instance_set_build_failed";
    case GoldSrcWorldSceneBuildErrorCode::invalid_brush_instance_conversion:
        return "invalid_brush_instance_conversion";
    case GoldSrcWorldSceneBuildErrorCode::scene_package_build_failed:
        return "scene_package_build_failed";
    case GoldSrcWorldSceneBuildErrorCode::unable_to_retain_scene:
        return "unable_to_retain_scene";
    }
    return "unknown";
}

world_scene_render::BrushSubmodelRenderSupportStatus
to_world_scene_render_support_status(
    const BrushSubmodelInstanceStatus status) noexcept
{
    using Output = world_scene_render::BrushSubmodelRenderSupportStatus;
    switch (status) {
    case BrushSubmodelInstanceStatus::supported_static_opaque:
        return Output::supported_static_opaque;
    case BrushSubmodelInstanceStatus::unsupported_transform:
        return Output::unsupported_transform;
    case BrushSubmodelInstanceStatus::unsupported_rendermode:
        return Output::unsupported_rendermode;
    case BrushSubmodelInstanceStatus::invalid_model_reference:
        return Output::invalid_model_reference;
    case BrushSubmodelInstanceStatus::missing_model_geometry:
        return Output::missing_model_geometry;
    case BrushSubmodelInstanceStatus::invalid_entity_metadata:
        return Output::invalid_entity_metadata;
    case BrushSubmodelInstanceStatus::outside_world_spatial_tree:
        return Output::outside_world_spatial_tree;
    case BrushSubmodelInstanceStatus::no_visible_leaf_membership:
        return Output::no_visible_leaf_membership;
    }
    return Output::invalid_entity_metadata;
}

GoldSrcWorldSceneBuildResult GoldSrcWorldSceneBuilder::build(
    const bsp::GoldSrcBspParsedDocument& document,
    std::shared_ptr<const world_render::WorldRenderPackage> world_package,
    std::optional<world_scene_render::BrushSubmodelRenderLibrary>
        brush_library,
    const GoldSrcWorldSceneBuildConfig& config,
    const GoldSrcWorldSceneBuildLimits& limits)
{
    GoldSrcWorldSceneBuildStatistics statistics;
    statistics.source_world_surface_count = document.world_asset.surfaces.size();
    statistics.source_brush_model_count = document.brush_submodels.size();

    if (!valid_goldsrc_world_scene_build_config(config) ||
        !valid_goldsrc_world_scene_build_limits(limits)) {
        return failure(
            GoldSrcWorldSceneBuildErrorCode::invalid_configuration,
            statistics);
    }
    if (world_package == nullptr) {
        return failure(
            GoldSrcWorldSceneBuildErrorCode::invalid_world_package,
            statistics);
    }
    if (!world_package_matches_document(*world_package, document)) {
        return failure(
            GoldSrcWorldSceneBuildErrorCode::world_package_document_mismatch,
            statistics);
    }

    try {
        const auto& source = document.spatial_source;
        auto spatial_result = spatial::GoldSrcSpatialPackageBuilder::build(
            spatial::GoldSrcSpatialBuildInput{
                source.planes,
                source.nodes,
                source.leaves,
                source.marksurface_face_ordinals,
                source.visibility_bytes,
                source.world_model,
                source.source_face_count,
                document.world_asset.surfaces,
                source.submodel_face_ordinals,
            },
            limits.spatial);
        if (!spatial_result || !spatial_result.package) {
            GoldSrcWorldSceneBuildError error{
                GoldSrcWorldSceneBuildErrorCode::spatial_package_build_failed};
            error.spatial_error = std::move(spatial_result.error);
            return failure(std::move(error), statistics);
        }
        statistics.spatial_node_count = spatial_result.package->nodes().size();
        statistics.spatial_leaf_count = spatial_result.package->leaves().size();

        const bool needs_entities = config.extract_spawn ||
            config.brushes == GoldSrcWorldSceneBrushMode::static_initial;
        std::optional<bsp::GoldSrcEntityDocument> entity_document;
        std::optional<GoldSrcSpawnCameraExtractionResult> spawn_camera;
        if (needs_entities) {
            statistics.entity_document_parse_count = 1U;
            auto parsed_entities = bsp::GoldSrcEntityDocumentParser::parse(
                document.entity_lump_bytes, limits.entities);
            if (!parsed_entities || !parsed_entities.document) {
                GoldSrcWorldSceneBuildError error{
                    GoldSrcWorldSceneBuildErrorCode::
                        entity_document_parse_failed};
                error.entity_error = std::move(parsed_entities.error);
                return failure(std::move(error), statistics);
            }
            statistics.parsed_entity_count = parsed_entities.document->size();
            statistics.parsed_entity_pair_count =
                parsed_entities.document->total_pair_count();
            entity_document.emplace(std::move(*parsed_entities.document));
            if (config.extract_spawn) {
                spawn_camera = GoldSrcSpawnCameraExtractor::extract(
                    *entity_document);
            }
        }

        auto scene_library = [&]() ->
            world_scene_render::BrushSubmodelRenderLibrary {
            if (config.brushes ==
                    GoldSrcWorldSceneBrushMode::static_initial &&
                brush_library) {
                return std::move(*brush_library);
            }
            return {};
        }();
        std::vector<world_scene_render::BrushSubmodelRenderInstance>
            scene_instances;
        if (config.brushes == GoldSrcWorldSceneBrushMode::static_initial) {
            if (!entity_document) {
                return failure(
                    GoldSrcWorldSceneBuildErrorCode::
                        invalid_brush_instance_conversion,
                    statistics);
            }
            if (!library_matches_document(scene_library, document)) {
                return failure(
                    GoldSrcWorldSceneBuildErrorCode::
                        brush_library_document_mismatch,
                    statistics);
            }
            statistics.retained_brush_render_model_count =
                scene_library.models().size();

            constexpr auto model_lump_index = static_cast<std::size_t>(
                bsp::GoldSrcBspLumpId::models);
            const auto source_model_count =
                document.lump_element_counts[model_lump_index];
            if (source_model_count == 0U ||
                document.brush_submodels.size() ==
                    std::numeric_limits<std::size_t>::max() ||
                source_model_count != document.brush_submodels.size() + 1U) {
                return failure(
                    GoldSrcWorldSceneBuildErrorCode::invalid_brush_model_table,
                    statistics);
            }

            std::vector<BrushSubmodelModelMetadata> model_metadata;
            model_metadata.reserve(document.brush_submodels.size());
            for (const auto& model : document.brush_submodels) {
                model_metadata.push_back(BrushSubmodelModelMetadata{
                    model.source_model_index,
                    model.source_model_origin,
                    model.geometry.bounds,
                    library_has_model(
                        scene_library, model.source_model_index),
                });
            }
            auto instance_result = BrushSubmodelInstanceSetBuilder::build(
                *entity_document,
                model_metadata,
                source_model_count,
                *spatial_result.package,
                limits.instances);
            if (!instance_result || !instance_result.instance_set) {
                GoldSrcWorldSceneBuildError error{
                    GoldSrcWorldSceneBuildErrorCode::
                        brush_instance_set_build_failed};
                error.instance_error = std::move(instance_result.error);
                return failure(std::move(error), statistics);
            }

            const auto& instance_statistics =
                instance_result.instance_set->statistics();
            statistics.brush_instance_count =
                instance_result.instance_set->size();
            statistics.supported_brush_instance_count =
                instance_statistics.supported_static_opaque_count;
            statistics.unsupported_brush_instance_count =
                statistics.brush_instance_count -
                statistics.supported_brush_instance_count;
            statistics.brush_instance_leaf_link_count =
                instance_statistics.touched_leaf_link_count;

            const auto instances = instance_result.instance_set->instances();
            scene_instances.reserve(instances.size());
            for (std::size_t instance_index = 0U;
                 instance_index < instances.size();
                 ++instance_index) {
                const auto& source_instance = instances[instance_index];
                if (instance_index >
                        std::numeric_limits<std::uint32_t>::max() ||
                    (source_instance.renderable() &&
                        (!source_instance.transform ||
                            !source_instance.transformed_bounds ||
                            source_instance.touched_world_leaves.empty()))) {
                    GoldSrcWorldSceneBuildError error{
                        GoldSrcWorldSceneBuildErrorCode::
                            invalid_brush_instance_conversion};
                    error.source_instance_index = instance_index;
                    return failure(std::move(error), statistics);
                }

                world_scene_render::BrushSubmodelRenderInstance converted;
                converted.source_instance_index =
                    static_cast<std::uint32_t>(instance_index);
                converted.source_entity_ordinal =
                    source_instance.source_entity_ordinal;
                converted.source_model_index =
                    source_instance.source_model_index;
                if (source_instance.transform) {
                    converted.model_transform =
                        source_instance.transform->model_matrix;
                }
                if (source_instance.transformed_bounds) {
                    converted.transformed_bounds =
                        *source_instance.transformed_bounds;
                }
                converted.touched_leaf_indices =
                    source_instance.touched_world_leaves;
                std::ranges::sort(converted.touched_leaf_indices);
                converted.touched_leaf_indices.erase(
                    std::unique(converted.touched_leaf_indices.begin(),
                        converted.touched_leaf_indices.end()),
                    converted.touched_leaf_indices.end());
                converted.support_status =
                    to_world_scene_render_support_status(
                        source_instance.status);
                scene_instances.push_back(std::move(converted));
            }
        }

        world_scene_render::WorldSceneRenderPackageBuilder scene_builder;
        auto scene_result = scene_builder.build(
            std::move(world_package),
            std::move(*spatial_result.package),
            std::move(scene_library),
            std::move(scene_instances),
            limits.scene);
        if (!scene_result || !scene_result.package) {
            GoldSrcWorldSceneBuildError error{
                GoldSrcWorldSceneBuildErrorCode::scene_package_build_failed};
            error.scene_error = std::move(scene_result.error);
            return failure(std::move(error), statistics);
        }

        GoldSrcWorldSceneBuildResult output;
        output.scene_package.emplace(std::move(*scene_result.package));
        output.spawn_camera = std::move(spawn_camera);
        output.statistics = statistics;
        return output;
    } catch (const std::bad_alloc&) {
        return failure(
            GoldSrcWorldSceneBuildErrorCode::unable_to_retain_scene,
            statistics);
    } catch (const std::length_error&) {
        return failure(
            GoldSrcWorldSceneBuildErrorCode::unable_to_retain_scene,
            statistics);
    }
}

} // namespace hlclient::goldsrc::brush_models

#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::brush_models {
namespace {

[[nodiscard]] bool checked_add(
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

[[nodiscard]] bool finite(const assets::AssetVector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const assets::AssetVector2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool valid_bounds(const assets::WorldBounds& bounds) noexcept
{
    return finite(bounds.minimum) && finite(bounds.maximum) &&
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool contains_bounds(
    const assets::WorldBounds& outer,
    const assets::WorldBounds& inner) noexcept
{
    return outer.minimum.x <= inner.minimum.x &&
        outer.minimum.y <= inner.minimum.y &&
        outer.minimum.z <= inner.minimum.z &&
        outer.maximum.x >= inner.maximum.x &&
        outer.maximum.y >= inner.maximum.y &&
        outer.maximum.z >= inner.maximum.z;
}

void include_bounds(
    assets::WorldBounds& aggregate,
    const assets::WorldBounds& bounds) noexcept
{
    aggregate.minimum.x = std::min(aggregate.minimum.x, bounds.minimum.x);
    aggregate.minimum.y = std::min(aggregate.minimum.y, bounds.minimum.y);
    aggregate.minimum.z = std::min(aggregate.minimum.z, bounds.minimum.z);
    aggregate.maximum.x = std::max(aggregate.maximum.x, bounds.maximum.x);
    aggregate.maximum.y = std::max(aggregate.maximum.y, bounds.maximum.y);
    aggregate.maximum.z = std::max(aggregate.maximum.z, bounds.maximum.z);
}

using MaterialIdentity = std::tuple<
    std::optional<std::string>,
    std::optional<std::uint32_t>,
    std::optional<std::uint32_t>,
    assets::WorldTextureStorage,
    std::int32_t,
    std::optional<std::uint32_t>,
    std::optional<std::uint32_t>,
    assets::WorldMaterialCompatibilityProfile,
    assets::WorldMaterialEvidenceProfile>;

[[nodiscard]] MaterialIdentity material_identity(
    const assets::WorldMaterialReference& material)
{
    return MaterialIdentity{
        material.texture_name,
        material.width,
        material.height,
        material.texture_storage,
        material.source_texture_flags,
        material.source_texinfo_index,
        material.source_texture_index,
        material.compatibility_profile,
        material.evidence_profile,
    };
}

[[nodiscard]] std::optional<GoldSrcBrushTextureIncompleteReason>
first_incomplete_texture_reason(
    const assets::WorldTextureSet& texture_set) noexcept
{
    const auto binding = std::ranges::find_if(
        texture_set.bindings(),
        [](const assets::WorldMaterialTextureBinding& candidate) {
            return !assets::is_resolved(candidate.status);
        });
    if (binding == texture_set.bindings().end()) {
        return std::nullopt;
    }

    GoldSrcBrushTextureIncompleteReason reason{
        binding->material_index,
        binding->status,
        binding->source_bsp_texture_index,
        binding->source_archive_ordinal,
        std::nullopt,
    };
    const auto archives = texture_set.archive_metadata();
    auto archive = archives.end();
    if (reason.source_archive_ordinal) {
        archive = std::ranges::find_if(
            archives,
            [&reason](const assets::WorldTextureArchiveMetadata& candidate) {
                return candidate.declaration_ordinal ==
                    *reason.source_archive_ordinal;
            });
    } else if (reason.binding_status ==
        assets::WorldMaterialTextureBindingStatus::
            external_wad_archive_missing) {
        archive = std::ranges::find_if(
            archives,
            [](const assets::WorldTextureArchiveMetadata& candidate) {
                return candidate.status ==
                    assets::WorldTextureArchiveStatus::missing;
            });
        if (archive != archives.end()) {
            reason.source_archive_ordinal = archive->declaration_ordinal;
        }
    }
    if (archive != archives.end()) {
        reason.archive_status = archive->status;
    }
    return reason;
}

[[nodiscard]] GoldSrcBrushRenderLibraryBuildResult fail(
    const GoldSrcBrushRenderLibraryErrorCode code,
    const GoldSrcBrushRenderLibraryStatistics& statistics,
    const std::optional<std::size_t> model_table_index = std::nullopt,
    const std::optional<std::uint32_t> source_model_index = std::nullopt,
    const std::optional<WorldTextureImportErrorCode> texture_error = std::nullopt,
    const std::optional<lightmaps::GoldSrcWorldLightmapImportErrorCode>
        lightmap_error = std::nullopt,
    const std::optional<world_render::WorldRenderPackageErrorCode>
        render_error = std::nullopt,
    const std::optional<GoldSrcBrushTextureIncompleteReason>
        texture_incomplete_reason = std::nullopt) noexcept
{
    return GoldSrcBrushRenderLibraryBuildResult{
        std::nullopt,
        GoldSrcBrushRenderLibraryError{
            code,
            model_table_index,
            source_model_index,
            texture_error,
            lightmap_error,
            render_error,
            texture_incomplete_reason,
        },
        statistics,
    };
}

[[nodiscard]] bool model_geometry_prerequisites_valid(
    const bsp::GoldSrcBspBrushSubmodelAsset& model) noexcept
{
    const auto& geometry = model.geometry;
    return model.source_model_index != 0U && finite(model.source_model_origin) &&
        valid_bounds(model.source_model_bounds) && valid_bounds(geometry.bounds) &&
        contains_bounds(model.source_model_bounds, geometry.bounds) &&
        geometry.coordinate_space ==
            assets::WorldCoordinateSpace::source_native_goldsrc_z_up &&
        geometry.texture_coordinate_space ==
            assets::WorldTextureCoordinateSpace::texel_units &&
        geometry.source_profile ==
            assets::WorldGeometrySourceProfile::goldsrc_bsp_v30 &&
        !geometry.vertices.empty() && !geometry.indices.empty() &&
        !geometry.surfaces.empty() && !geometry.materials.empty() &&
        geometry.indices.size() % 3U == 0U;
}

[[nodiscard]] world_visibility::WorldVisibleSurfaceInput surface_input(
    const world_render::WorldRenderSurfaceRange& range) noexcept
{
    return world_visibility::WorldVisibleSurfaceInput{
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

// Mutable construction state stays private to this builder translation unit.
// Only a complete immutable render model crosses the public result boundary.
struct PendingBrushRenderModel {
    std::uint32_t source_model_index{0U};
    assets::WorldBounds local_bounds{};
    std::vector<std::uint32_t> render_surface_indices;
};

} // namespace

bool valid_goldsrc_brush_render_library_limits(
    const GoldSrcBrushRenderLibraryLimits& limits) noexcept
{
    return limits.maximum_models > 0U &&
        limits.maximum_models <= kHardMaximumBrushRenderModels &&
        limits.maximum_vertices > 0U &&
        limits.maximum_vertices <= kHardMaximumBrushRenderVertices &&
        limits.maximum_indices > 0U &&
        limits.maximum_indices <= kHardMaximumBrushRenderIndices &&
        limits.maximum_surfaces > 0U &&
        limits.maximum_surfaces <= kHardMaximumBrushRenderSurfaces &&
        limits.maximum_materials > 0U &&
        limits.maximum_materials <= kHardMaximumBrushRenderMaterials &&
        limits.maximum_texture_import_updates > 0U &&
        limits.maximum_texture_import_updates <=
            kHardMaximumBrushTextureImportUpdates &&
        valid_goldsrc_world_texture_import_limits(limits.textures) &&
        lightmaps::valid_goldsrc_world_lightmap_import_limits(limits.lightmaps) &&
        limits.render_package.maximum_vertices > 0U &&
        limits.render_package.maximum_indices > 0U &&
        limits.render_package.maximum_materials > 0U &&
        limits.render_package.maximum_batches > 0U &&
        limits.render_package.maximum_base_texture_bytes > 0U &&
        limits.render_package.maximum_lightmap_bytes > 0U &&
        limits.render_package.maximum_total_cpu_render_bytes > 0U &&
        limits.maximum_vertices <= limits.render_package.maximum_vertices &&
        limits.maximum_indices <= limits.render_package.maximum_indices &&
        limits.maximum_materials <= limits.render_package.maximum_materials &&
        limits.maximum_materials <= limits.textures.maximum_material_count &&
        limits.maximum_surfaces <= limits.lightmaps.maximum_surface_count;
}

std::string_view to_string(const GoldSrcBrushRenderLibraryErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcBrushRenderLibraryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcBrushRenderLibraryErrorCode::source_document_mismatch:
        return "source_document_mismatch";
    case GoldSrcBrushRenderLibraryErrorCode::invalid_model_order:
        return "invalid_model_order";
    case GoldSrcBrushRenderLibraryErrorCode::invalid_model_geometry:
        return "invalid_model_geometry";
    case GoldSrcBrushRenderLibraryErrorCode::aggregate_limit_exceeded:
        return "aggregate_limit_exceeded";
    case GoldSrcBrushRenderLibraryErrorCode::texture_import_begin_failed:
        return "texture_import_begin_failed";
    case GoldSrcBrushRenderLibraryErrorCode::texture_import_update_limit_exceeded:
        return "texture_import_update_limit_exceeded";
    case GoldSrcBrushRenderLibraryErrorCode::textures_incomplete:
        return "textures_incomplete";
    case GoldSrcBrushRenderLibraryErrorCode::texture_import_failed:
        return "texture_import_failed";
    case GoldSrcBrushRenderLibraryErrorCode::lightmap_import_failed:
        return "lightmap_import_failed";
    case GoldSrcBrushRenderLibraryErrorCode::render_package_failed:
        return "render_package_failed";
    case GoldSrcBrushRenderLibraryErrorCode::surface_range_mismatch:
        return "surface_range_mismatch";
    case GoldSrcBrushRenderLibraryErrorCode::unable_to_retain_library:
        return "unable_to_retain_library";
    }
    return "unknown";
}

GoldSrcBrushRenderLibraryBuildResult GoldSrcBrushRenderLibraryBuilder::build(
    const bsp::GoldSrcBspParsedDocument& document,
    const std::span<const std::byte> retained_bsp_source,
    std::shared_ptr<const local_resources::LocalResourceEnvironment> environment,
    const GoldSrcBrushRenderLibraryLimits& limits)
{
    GoldSrcBrushRenderLibraryStatistics statistics;
    if (!valid_goldsrc_brush_render_library_limits(limits)) {
        return fail(
            GoldSrcBrushRenderLibraryErrorCode::invalid_configuration,
            statistics);
    }
    if (document.brush_submodels.empty()) {
        return GoldSrcBrushRenderLibraryBuildResult{
            world_scene_render::BrushSubmodelRenderLibrary{},
            std::nullopt,
            statistics,
        };
    }
    if (!document.world_asset.source_content_fingerprint ||
        bsp::goldsrc_bsp_source_fingerprint(retained_bsp_source) !=
            *document.world_asset.source_content_fingerprint) {
        return fail(
            GoldSrcBrushRenderLibraryErrorCode::source_document_mismatch,
            statistics);
    }
    if (environment == nullptr ||
        environment->root_count() == 0U) {
        return fail(
            GoldSrcBrushRenderLibraryErrorCode::invalid_configuration,
            statistics);
    }

    try {
        if (document.brush_submodels.size() > limits.maximum_models) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::aggregate_limit_exceeded,
                statistics);
        }
        statistics.source_model_count = document.brush_submodels.size();

        assets::WorldAsset aggregate;
        aggregate.identity.source_name = "brush-submodels";
        aggregate.coordinate_space =
            assets::WorldCoordinateSpace::source_native_goldsrc_z_up;
        aggregate.texture_coordinate_space =
            assets::WorldTextureCoordinateSpace::texel_units;
        aggregate.source_profile = assets::WorldGeometrySourceProfile::goldsrc_bsp_v30;
        aggregate.source_content_fingerprint =
            document.world_asset.source_content_fingerprint;
        std::vector<PendingBrushRenderModel> pending_models;
        pending_models.reserve(document.brush_submodels.size());
        std::map<MaterialIdentity, std::size_t> material_lookup;
        bool has_bounds = false;
        std::uint32_t previous_model_index = 0U;

        for (std::size_t model_table_index = 0U;
             model_table_index < document.brush_submodels.size();
             ++model_table_index) {
            const auto& source_model = document.brush_submodels[model_table_index];
            const auto source_model_index = source_model.source_model_index;
            if (source_model_index <= previous_model_index) {
                return fail(
                    GoldSrcBrushRenderLibraryErrorCode::invalid_model_order,
                    statistics,
                    model_table_index,
                    source_model_index);
            }
            previous_model_index = source_model_index;
            if (source_model.geometry.source_content_fingerprint !=
                document.world_asset.source_content_fingerprint) {
                return fail(
                    GoldSrcBrushRenderLibraryErrorCode::
                        source_document_mismatch,
                    statistics,
                    model_table_index,
                    source_model_index);
            }
            if (!model_geometry_prerequisites_valid(source_model)) {
                return fail(
                    GoldSrcBrushRenderLibraryErrorCode::invalid_model_geometry,
                    statistics,
                    model_table_index,
                    source_model_index);
            }
            const auto& geometry = source_model.geometry;
            std::size_t next_vertex_count = 0U;
            std::size_t next_index_count = 0U;
            std::size_t next_surface_count = 0U;
            std::size_t next_input_material_count = 0U;
            if (!checked_add(
                    aggregate.vertices.size(),
                    geometry.vertices.size(),
                    next_vertex_count) ||
                !checked_add(
                    aggregate.indices.size(),
                    geometry.indices.size(),
                    next_index_count) ||
                !checked_add(
                    aggregate.surfaces.size(),
                    geometry.surfaces.size(),
                    next_surface_count) ||
                !checked_add(
                    static_cast<std::size_t>(
                        statistics.input_material_reference_count),
                    geometry.materials.size(),
                    next_input_material_count) ||
                next_vertex_count > limits.maximum_vertices ||
                next_vertex_count > std::numeric_limits<std::uint32_t>::max() ||
                next_index_count > limits.maximum_indices ||
                next_index_count > std::numeric_limits<std::uint32_t>::max() ||
                next_surface_count > limits.maximum_surfaces) {
                return fail(
                    GoldSrcBrushRenderLibraryErrorCode::aggregate_limit_exceeded,
                    statistics,
                    model_table_index,
                    source_model_index);
            }
            statistics.input_material_reference_count =
                next_input_material_count;

            const auto vertex_offset = aggregate.vertices.size();
            const auto index_offset = aggregate.indices.size();
            const auto surface_offset = aggregate.surfaces.size();
            std::vector<std::size_t> aggregate_material_by_source;
            aggregate_material_by_source.reserve(geometry.materials.size());
            for (const auto& source_material : geometry.materials) {
                const auto identity = material_identity(source_material);
                auto found = material_lookup.find(identity);
                if (found == material_lookup.end()) {
                    if (aggregate.materials.size() >= limits.maximum_materials) {
                        return fail(
                            GoldSrcBrushRenderLibraryErrorCode::
                                aggregate_limit_exceeded,
                            statistics,
                            model_table_index,
                            source_model_index);
                    }
                    const auto aggregate_material_index =
                        aggregate.materials.size();
                    aggregate.materials.push_back(source_material);
                    found = material_lookup.emplace(
                        std::move(identity),
                        aggregate_material_index).first;
                }
                aggregate_material_by_source.push_back(found->second);
            }

            aggregate.vertices.insert(
                aggregate.vertices.end(),
                geometry.vertices.begin(),
                geometry.vertices.end());
            aggregate.indices.reserve(next_index_count);
            for (const auto source_vertex_index : geometry.indices) {
                if (static_cast<std::size_t>(source_vertex_index) >=
                        geometry.vertices.size() ||
                    vertex_offset > std::numeric_limits<std::uint32_t>::max() -
                        source_vertex_index) {
                    return fail(
                        GoldSrcBrushRenderLibraryErrorCode::invalid_model_geometry,
                        statistics,
                        model_table_index,
                        source_model_index);
                }
                aggregate.indices.push_back(
                    static_cast<std::uint32_t>(vertex_offset + source_vertex_index));
            }

            std::vector<std::uint32_t> model_surface_indices;
            model_surface_indices.reserve(geometry.surfaces.size());
            for (std::size_t source_surface_index = 0U;
                 source_surface_index < geometry.surfaces.size();
                 ++source_surface_index) {
                const auto& source_surface = geometry.surfaces[source_surface_index];
                if (source_surface.material_index >=
                        aggregate_material_by_source.size() ||
                    source_surface.first_index > geometry.indices.size() ||
                    source_surface.index_count >
                        geometry.indices.size() - source_surface.first_index ||
                    source_surface.first_vertex > geometry.vertices.size() ||
                    source_surface.vertex_count >
                        geometry.vertices.size() - source_surface.first_vertex ||
                    !valid_bounds(source_surface.bounds) ||
                    index_offset > std::numeric_limits<std::uint32_t>::max() -
                        source_surface.first_index ||
                    vertex_offset > std::numeric_limits<std::uint32_t>::max() -
                        source_surface.first_vertex) {
                    return fail(
                        GoldSrcBrushRenderLibraryErrorCode::invalid_model_geometry,
                        statistics,
                        model_table_index,
                        source_model_index);
                }
                auto aggregate_surface = source_surface;
                aggregate_surface.first_index = static_cast<std::uint32_t>(
                    index_offset + source_surface.first_index);
                aggregate_surface.first_vertex = static_cast<std::uint32_t>(
                    vertex_offset + source_surface.first_vertex);
                aggregate_surface.material_index = static_cast<std::uint32_t>(
                    aggregate_material_by_source[source_surface.material_index]);
                aggregate.surfaces.push_back(std::move(aggregate_surface));
                model_surface_indices.push_back(static_cast<std::uint32_t>(
                    surface_offset + source_surface_index));
            }
            pending_models.push_back(PendingBrushRenderModel{
                source_model_index,
                geometry.bounds,
                std::move(model_surface_indices),
            });
            if (!has_bounds) {
                aggregate.bounds = geometry.bounds;
                has_bounds = true;
            } else {
                include_bounds(aggregate.bounds, geometry.bounds);
            }
        }

        if (!has_bounds || aggregate.materials.empty()) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::invalid_model_geometry,
                statistics);
        }
        aggregate.source_model_bounds = aggregate.bounds;
        aggregate.statistics = assets::WorldGeometryStatistics{
            30,
            static_cast<std::uint64_t>(document.brush_submodels.size()),
            static_cast<std::uint64_t>(aggregate.surfaces.size()),
            0U,
            0U,
            static_cast<std::uint64_t>(aggregate.surfaces.size()),
            static_cast<std::uint64_t>(aggregate.vertices.size()),
            static_cast<std::uint64_t>(aggregate.indices.size() / 3U),
            static_cast<std::uint64_t>(aggregate.materials.size()),
            0U,
            0U,
            0U,
        };
        for (const auto& material : aggregate.materials) {
            switch (material.texture_storage) {
            case assets::WorldTextureStorage::missing:
                ++aggregate.statistics.missing_texture_reference_count;
                break;
            case assets::WorldTextureStorage::external_reference:
                ++aggregate.statistics.external_texture_reference_count;
                break;
            case assets::WorldTextureStorage::embedded:
                ++aggregate.statistics.embedded_texture_reference_count;
                break;
            }
        }

        statistics.aggregate_vertex_count = aggregate.vertices.size();
        statistics.aggregate_index_count = aggregate.indices.size();
        statistics.aggregate_surface_count = aggregate.surfaces.size();
        statistics.unique_material_reference_count = aggregate.materials.size();
        statistics.deduplicated_material_reference_count =
            statistics.input_material_reference_count -
            statistics.unique_material_reference_count;

        auto texture_begin = WorldTextureImportOperation::begin(
            aggregate,
            retained_bsp_source,
            std::move(environment),
            limits.textures);
        if (!texture_begin || !texture_begin.operation) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::texture_import_begin_failed,
                statistics,
                std::nullopt,
                std::nullopt,
                texture_begin.error
                    ? std::optional{texture_begin.error->code}
                    : std::nullopt);
        }
        auto& texture_operation = *texture_begin.operation;
        auto now = WorldTextureImportTimePoint{};
        while (!texture_operation.terminal() &&
               statistics.texture_import_update_count <
                   limits.maximum_texture_import_updates) {
            texture_operation.update(now);
            ++statistics.texture_import_update_count;
            now += std::chrono::milliseconds{1};
        }
        if (!texture_operation.terminal()) {
            texture_operation.cancel();
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::
                    texture_import_update_limit_exceeded,
                statistics);
        }
        if (texture_operation.state() ==
            WorldTextureImportState::textures_incomplete) {
            const auto* texture_set = texture_operation.result();
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::textures_incomplete,
                statistics,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                texture_set != nullptr
                    ? first_incomplete_texture_reason(*texture_set)
                    : std::nullopt);
        }
        if (texture_operation.state() != WorldTextureImportState::textures_ready) {
            const auto* texture_error = texture_operation.error();
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::texture_import_failed,
                statistics,
                std::nullopt,
                std::nullopt,
                texture_error ? std::optional{texture_error->code} : std::nullopt);
        }
        auto texture_set = texture_operation.take_result();
        if (!texture_set) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::texture_import_failed,
                statistics);
        }
        statistics.decoded_texture_count = texture_set->texture_count();

        auto lightmap_result = lightmaps::GoldSrcWorldLightmapImporter::import(
            aggregate,
            retained_bsp_source,
            limits.lightmaps);
        if (!lightmap_result || !lightmap_result.lightmap_set) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::lightmap_import_failed,
                statistics,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                lightmap_result.error
                    ? std::optional{lightmap_result.error->code}
                    : std::nullopt);
        }
        statistics.lightmap_atlas_page_count =
            lightmap_result.lightmap_set->page_count();

        const world_render::WorldRenderPackageBuilder render_builder;
        auto render_result = render_builder.build(
            assets::TexturedWorldAsset{
                std::move(aggregate),
                std::move(*texture_set),
            },
            std::move(*lightmap_result.lightmap_set),
            limits.render_package);
        if (!render_result || !render_result.package) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::render_package_failed,
                statistics,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                render_result.error
                    ? std::optional{render_result.error->code}
                    : std::nullopt);
        }

        auto render_package =
            std::make_shared<const world_render::WorldRenderPackage>(
                std::move(*render_result.package));
        const auto ranges = render_package->surface_ranges();
        if (ranges.size() != statistics.aggregate_surface_count) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::surface_range_mismatch,
                statistics);
        }
        std::vector<std::uint8_t> surface_covered(ranges.size(), 0U);
        std::vector<world_scene_render::BrushSubmodelRenderModel> models;
        models.reserve(pending_models.size());
        for (std::size_t model_table_index = 0U;
             model_table_index < pending_models.size();
             ++model_table_index) {
            auto& pending_model = pending_models[model_table_index];
            std::vector<world_visibility::WorldVisibleSurfaceInput> surfaces;
            surfaces.reserve(pending_model.render_surface_indices.size());
            for (const auto aggregate_surface_index :
                 pending_model.render_surface_indices) {
                if (aggregate_surface_index >= ranges.size() ||
                    surface_covered[aggregate_surface_index] != 0U ||
                    ranges[aggregate_surface_index].source_world_surface_index !=
                        aggregate_surface_index) {
                    return fail(
                        GoldSrcBrushRenderLibraryErrorCode::surface_range_mismatch,
                        statistics,
                        model_table_index,
                        pending_model.source_model_index);
                }
                surface_covered[aggregate_surface_index] = 1U;
                surfaces.push_back(
                    surface_input(ranges[aggregate_surface_index]));
            }
            models.emplace_back(
                pending_model.source_model_index,
                pending_model.local_bounds,
                std::move(pending_model.render_surface_indices),
                std::move(surfaces));
        }
        if (!std::ranges::all_of(surface_covered, [](const auto value) {
                return value != 0U;
            })) {
            return fail(
                GoldSrcBrushRenderLibraryErrorCode::surface_range_mismatch,
                statistics);
        }

        return GoldSrcBrushRenderLibraryBuildResult{
            world_scene_render::BrushSubmodelRenderLibrary{
                std::move(render_package),
                std::move(models),
            },
            std::nullopt,
            statistics,
        };
    } catch (const std::bad_alloc&) {
        return fail(
            GoldSrcBrushRenderLibraryErrorCode::unable_to_retain_library,
            statistics);
    } catch (const std::length_error&) {
        return fail(
            GoldSrcBrushRenderLibraryErrorCode::unable_to_retain_library,
            statistics);
    } catch (...) {
        return fail(
            GoldSrcBrushRenderLibraryErrorCode::unable_to_retain_library,
            statistics);
    }
}

} // namespace hlclient::goldsrc::brush_models

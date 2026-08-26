#include <hlclient/entity_render/entity_scene_render.hpp>

#include <hlclient/entity_visual/entity_visual_asset_library.hpp>

#include <algorithm>
#include <bit>
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

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_ == 0U ? 1U : value_;
    }

private:
    std::uint64_t value_{14'695'981'039'346'656'037ULL};
};

[[nodiscard]] EntitySceneRenderPackageBuildResult scene_fail(
    const EntitySceneRenderErrorCode code,
    const std::optional<std::size_t> element_index,
    std::string context)
{
    return {std::nullopt,
        EntitySceneRenderError{code, element_index, std::move(context)}};
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

} // namespace

EntitySceneRenderPackageBuildResult EntitySceneRenderPackageBuilder::build(
    EntitySceneRenderPackageCreateInfo create_info,
    const RuntimeEntityVisualLimits& limits) const
{
    if (!valid_runtime_entity_visual_limits(limits)) {
        return scene_fail(EntitySceneRenderErrorCode::invalid_configuration,
            std::nullopt,
            "Runtime entity visual limits are invalid or exceed hard caps");
    }
    if (!create_info.asset_library) {
        return scene_fail(EntitySceneRenderErrorCode::missing_asset_library,
            std::nullopt,
            "Entity scene package requires an immutable visual asset library");
    }
    if (create_info.asset_library_identity.resource_id == 0U ||
        create_info.asset_library_identity.revision == 0U) {
        return scene_fail(EntitySceneRenderErrorCode::invalid_library_identity,
            std::nullopt,
            "Entity visual asset library identity must be exact and nonzero");
    }
    if (create_info.asset_library->resource_id() !=
            create_info.asset_library_identity.resource_id ||
        create_info.asset_library->resource_revision() !=
            create_info.asset_library_identity.revision) {
        return scene_fail(EntitySceneRenderErrorCode::invalid_library_identity,
            std::nullopt,
            "Entity scene library identity does not match the retained immutable state");
    }
    if (create_info.resource_id == 0U) {
        return scene_fail(EntitySceneRenderErrorCode::invalid_resource_identity,
            std::nullopt,
            "Entity scene resource ID must be nonzero");
    }
    if (create_info.world_scene_association &&
        (create_info.world_scene_association->resource_id == 0U ||
            create_info.world_scene_association->revision == 0U)) {
        return scene_fail(EntitySceneRenderErrorCode::invalid_resource_identity,
            std::nullopt,
            "Entity world-scene association identity must be exact and nonzero");
    }

    std::size_t visual_asset_count = 0U;
    if (!add_size(create_info.studio_assets.size(),
            create_info.sprite_assets.size(),
            visual_asset_count) ||
        visual_asset_count > limits.maximum_visual_assets) {
        return scene_fail(EntitySceneRenderErrorCode::source_limit_exceeded,
            std::nullopt,
            "Entity scene visual asset count exceeds the configured limit");
    }

    try {
        std::unordered_set<std::uint64_t> resource_ids;
        resource_ids.reserve(visual_asset_count);
        EntitySceneRenderStatistics statistics;
        statistics.visual_asset_count = visual_asset_count;
        statistics.studio_asset_count = create_info.studio_assets.size();
        statistics.sprite_asset_count = create_info.sprite_assets.size();
        const auto library_records = create_info.asset_library->records();
        if (library_records.size() != visual_asset_count) {
            return scene_fail(EntitySceneRenderErrorCode::invalid_library_identity,
                std::nullopt,
                "Entity scene render assets do not exactly cover the retained visual library");
        }
        for (std::size_t index = 0U;
             index < create_info.studio_assets.size();
             ++index) {
            const auto& asset = create_info.studio_assets[index];
            if (!asset || asset->resource_id() == 0U ||
                asset->resource_revision() == 0U) {
                return scene_fail(EntitySceneRenderErrorCode::invalid_studio_asset,
                    index,
                    "Entity scene has a null or identity-less Studio render asset");
            }
            if (!resource_ids.insert(asset->resource_id()).second) {
                return scene_fail(
                    EntitySceneRenderErrorCode::duplicate_asset_identity,
                    index,
                    "Entity scene repeats a visual asset resource ID");
            }
            const auto record = std::find_if(library_records.begin(),
                library_records.end(),
                [&asset](const entity_visual::EntityVisualAssetRecord& value) {
                    return value.kind() ==
                            entity_visual::EntityVisualAssetKind::studio_model &&
                        value.resource_id() == asset->source_identity().resource_id &&
                        value.resource_revision() ==
                            asset->source_identity().revision;
                });
            if (record == library_records.end()) {
                return scene_fail(EntitySceneRenderErrorCode::invalid_studio_asset,
                    index,
                    "Studio render asset has no exact immutable library record");
            }
            if (!add_size(statistics.studio_vertex_count,
                    asset->statistics().vertex_count,
                    statistics.studio_vertex_count) ||
                !add_size(statistics.studio_index_count,
                    asset->statistics().index_count,
                    statistics.studio_index_count) ||
                !add_size(statistics.studio_mesh_count,
                    asset->statistics().mesh_count,
                    statistics.studio_mesh_count) ||
                !add_size(statistics.model_gpu_source_bytes,
                    asset->statistics().total_gpu_source_bytes,
                    statistics.model_gpu_source_bytes) ||
                statistics.model_gpu_source_bytes >
                    limits.maximum_model_gpu_bytes) {
                return scene_fail(EntitySceneRenderErrorCode::source_limit_exceeded,
                    index,
                    "Entity scene Studio geometry exceeds the configured limit");
            }
        }
        for (std::size_t index = 0U;
             index < create_info.sprite_assets.size();
             ++index) {
            const auto& asset = create_info.sprite_assets[index];
            if (!asset || asset->resource_id() == 0U ||
                asset->resource_revision() == 0U) {
                return scene_fail(EntitySceneRenderErrorCode::invalid_sprite_asset,
                    index,
                    "Entity scene has a null or identity-less Sprite render asset");
            }
            if (!resource_ids.insert(asset->resource_id()).second) {
                return scene_fail(
                    EntitySceneRenderErrorCode::duplicate_asset_identity,
                    index,
                    "Entity scene repeats a visual asset resource ID");
            }
            const auto record = std::find_if(library_records.begin(),
                library_records.end(),
                [&asset](const entity_visual::EntityVisualAssetRecord& value) {
                    return value.kind() ==
                            entity_visual::EntityVisualAssetKind::sprite &&
                        value.resource_id() == asset->source_identity().resource_id &&
                        value.resource_revision() ==
                            asset->source_identity().revision;
                });
            if (record == library_records.end()) {
                return scene_fail(EntitySceneRenderErrorCode::invalid_sprite_asset,
                    index,
                    "Sprite render asset has no exact immutable library record");
            }
            if (!add_size(statistics.sprite_frame_count,
                    asset->statistics().frame_count,
                    statistics.sprite_frame_count) ||
                !add_size(statistics.sprite_gpu_source_bytes,
                    asset->statistics().total_gpu_source_bytes,
                    statistics.sprite_gpu_source_bytes) ||
                statistics.sprite_gpu_source_bytes >
                    limits.maximum_sprite_gpu_bytes) {
                return scene_fail(EntitySceneRenderErrorCode::source_limit_exceeded,
                    index,
                    "Entity scene Sprite geometry exceeds the configured limit");
            }
        }

        StableHasher revision_hash;
        revision_hash.add(create_info.resource_id);
        revision_hash.add(create_info.asset_library_identity.resource_id);
        revision_hash.add(create_info.asset_library_identity.revision);
        revision_hash.add(static_cast<std::uint64_t>(visual_asset_count));
        if (create_info.world_scene_association) {
            revision_hash.add(create_info.world_scene_association->resource_id);
            revision_hash.add(create_info.world_scene_association->revision);
        } else {
            revision_hash.add(0U);
            revision_hash.add(0U);
        }
        for (const auto& asset : create_info.studio_assets) {
            revision_hash.add(asset->resource_id());
            revision_hash.add(asset->resource_revision());
        }
        for (const auto& asset : create_info.sprite_assets) {
            revision_hash.add(asset->resource_id());
            revision_hash.add(asset->resource_revision());
        }

        return {
            EntitySceneRenderPackage{
                std::move(create_info.asset_library),
                create_info.asset_library_identity,
                create_info.resource_id,
                revision_hash.value(),
                create_info.world_scene_association,
                std::move(create_info.studio_assets),
                std::move(create_info.sprite_assets),
                statistics,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return scene_fail(EntitySceneRenderErrorCode::unable_to_retain_scene,
            std::nullopt,
            "Unable to retain immutable entity scene package");
    } catch (const std::length_error&) {
        return scene_fail(EntitySceneRenderErrorCode::source_limit_exceeded,
            std::nullopt,
            "Entity scene package exceeds an owning container limit");
    }
}

} // namespace hlclient::entity_render

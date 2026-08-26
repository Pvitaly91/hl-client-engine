#include <hlclient/interactive_preview/interactive_entity_visibility.hpp>

#include <hlclient/world_spatial/world_spatial_types.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hlclient::interactive_preview {
namespace {

[[nodiscard]] InteractiveEntityVisibilityRefilterResult fail(
    const InteractiveEntityVisibilityRefilterErrorCode code,
    const std::string_view context) noexcept
{
    return {
        nullptr,
        InteractiveEntityVisibilityRefilterError{
            code, std::nullopt, std::nullopt, std::nullopt, context},
        {},
    };
}

template <typename Instance>
void reset_camera_cull(
    Instance& instance,
    InteractiveEntityVisibilityRefilterStatistics& statistics) noexcept
{
    using entity_render::RuntimeEntityVisibilityStatus;
    if (instance.visibility_status ==
        RuntimeEntityVisibilityStatus::culled_by_pvs) {
        instance.visibility_status = RuntimeEntityVisibilityStatus::visible;
        ++statistics.reset_culled_by_pvs_count;
    } else if (instance.visibility_status ==
        RuntimeEntityVisibilityStatus::culled_by_frustum) {
        instance.visibility_status = RuntimeEntityVisibilityStatus::visible;
        ++statistics.reset_culled_by_frustum_count;
    }
}

template <typename Instances>
void reset_camera_culls(
    Instances& instances,
    InteractiveEntityVisibilityRefilterStatistics& statistics) noexcept
{
    for (auto& instance : instances) {
        reset_camera_cull(instance, statistics);
    }
}

} // namespace

std::string_view to_string(
    const InteractiveEntityVisibilityRefilterErrorCode code) noexcept
{
    switch (code) {
    case InteractiveEntityVisibilityRefilterErrorCode::
        invalid_output_frame_revision:
        return "invalid_output_frame_revision";
    case InteractiveEntityVisibilityRefilterErrorCode::
        invalid_retained_entity_number:
        return "invalid_retained_entity_number";
    case InteractiveEntityVisibilityRefilterErrorCode::scene_package_mismatch:
        return "scene_package_mismatch";
    case InteractiveEntityVisibilityRefilterErrorCode::
        invalid_world_spatial_context:
        return "invalid_world_spatial_context";
    case InteractiveEntityVisibilityRefilterErrorCode::frustum_creation_failed:
        return "frustum_creation_failed";
    case InteractiveEntityVisibilityRefilterErrorCode::frame_rebuild_failed:
        return "frame_rebuild_failed";
    case InteractiveEntityVisibilityRefilterErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case InteractiveEntityVisibilityRefilterErrorCode::unable_to_retain_frame:
        return "unable_to_retain_frame";
    }
    return "unknown";
}

InteractiveEntityVisibilityRefilterResult
InteractiveEntityVisibilityRefilter::refilter(
    const entity_render::EntitySceneRenderPackage& scene_package,
    const entity_render::EntityRenderFrame& source_frame,
    const InteractiveEntityVisibilityRefilterInput& input) const noexcept
{
    if (input.output_frame_revision == 0U ||
        input.output_frame_revision <= source_frame.resource_revision()) {
        return fail(InteractiveEntityVisibilityRefilterErrorCode::
                invalid_output_frame_revision,
            "Interactive entity visibility output revision must advance beyond the source frame");
    }
    if (input.retained_entity_number &&
        *input.retained_entity_number == 0U) {
        return fail(InteractiveEntityVisibilityRefilterErrorCode::
                invalid_retained_entity_number,
            "Retained synthetic camera-anchor entity number must be nonzero");
    }

    const entity_render::EntityRenderResourceIdentity package_identity{
        scene_package.resource_id(), scene_package.resource_revision()};
    if (source_frame.scene_package_identity() != package_identity) {
        return fail(
            InteractiveEntityVisibilityRefilterErrorCode::
                scene_package_mismatch,
            "Source entity frame does not belong to the supplied scene package");
    }

    if ((input.spatial_package == nullptr) !=
            !input.camera_leaf_index.has_value() ||
        (input.spatial_package != nullptr &&
            static_cast<std::size_t>(*input.camera_leaf_index) >=
                input.spatial_package->leaves().size()) ||
        (input.spatial_package != nullptr &&
            !input.spatial_package->pvs_table().leaf_has_usable_row(
                *input.camera_leaf_index))) {
        return fail(InteractiveEntityVisibilityRefilterErrorCode::
                invalid_world_spatial_context,
            "PVS refiltering requires both a spatial package and camera leaf");
    }

    auto created_frustum = world_visibility::WorldViewFrustum::from_camera(
        input.camera, input.extent);
    if (!created_frustum || !created_frustum.frustum) {
        auto result = fail(InteractiveEntityVisibilityRefilterErrorCode::
                frustum_creation_failed,
            "Unable to derive an entity visibility frustum from the camera");
        result.error->frustum_error = created_frustum.error;
        return result;
    }

    try {
        entity_render::EntityRenderFrameBuildInput build_input;
        build_input.resource_id = source_frame.resource_id();
        build_input.resource_revision = input.output_frame_revision;
        build_input.interpolation = source_frame.interpolation();
        build_input.studio_poses =
            std::vector<entity_render::StudioRenderPose>{
            source_frame.studio_poses().begin(),
            source_frame.studio_poses().end()};
        build_input.studio_instances =
            std::vector<entity_render::StudioEntityRenderInstance>{
            source_frame.studio_instances().begin(),
            source_frame.studio_instances().end()};
        build_input.sprite_instances =
            std::vector<entity_render::SpriteEntityRenderInstance>{
            source_frame.sprite_instances().begin(),
            source_frame.sprite_instances().end()};
        build_input.unsupported_instances =
            std::vector<entity_render::UnsupportedEntityVisualInstance>{
            source_frame.unsupported_instances().begin(),
            source_frame.unsupported_instances().end()};

        InteractiveEntityVisibilityRefilterStatistics statistics;
        statistics.source_candidate_count =
            source_frame.statistics().candidate_count;
        reset_camera_culls(build_input.studio_instances, statistics);
        reset_camera_culls(build_input.sprite_instances, statistics);
        reset_camera_culls(build_input.unsupported_instances, statistics);

        build_input.view_frustum = &*created_frustum.frustum;
        build_input.spatial_package = input.spatial_package;
        build_input.camera_leaf_index = input.camera_leaf_index;
        build_input.camera_cull_exempt_entity_number =
            input.retained_entity_number;

        auto rebuilt = entity_render::EntityRenderFrameBuilder{}.build(
            scene_package, std::move(build_input), input.limits);
        if (!rebuilt || !rebuilt.frame) {
            auto result = fail(InteractiveEntityVisibilityRefilterErrorCode::
                    frame_rebuild_failed,
                "Entity render frame builder rejected visibility refiltering");
            if (rebuilt.error) {
                result.error->frame_builder_error = rebuilt.error->code;
                result.error->entity_number = rebuilt.error->entity_number;
            }
            return result;
        }

        statistics.result_visible_count =
            rebuilt.frame->statistics().visible_count;
        auto retained =
            std::make_shared<const entity_render::EntityRenderFrame>(
                std::move(*rebuilt.frame));
        return {std::move(retained), std::nullopt, statistics};
    } catch (const std::bad_alloc&) {
        return fail(InteractiveEntityVisibilityRefilterErrorCode::
                unable_to_retain_frame,
            "Unable to retain an immutable refiltered entity frame");
    } catch (const std::length_error&) {
        return fail(InteractiveEntityVisibilityRefilterErrorCode::
                source_limit_exceeded,
            "Entity refilter source exceeds an owning container limit");
    }
}

} // namespace hlclient::interactive_preview

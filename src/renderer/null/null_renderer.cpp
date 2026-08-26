#include <hlclient/renderer/null/null_renderer.hpp>

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_visibility/world_visible_draw_list.hpp>

#include <stdexcept>

namespace hlclient::renderer::null {

NullRenderer::~NullRenderer() noexcept
{
    shutdown();
}

const RendererInfo& NullRenderer::information() const noexcept
{
    return information_;
}

void NullRenderer::render(const RenderScene& scene, const RenderExtent extent)
{
    if (!statistics_.initialized) {
        throw std::logic_error{"Null renderer must be initialized before rendering"};
    }
    if (scene.dynamic_entities &&
        (!scene.dynamic_entities->package ||
            !scene.dynamic_entities->frame)) {
        throw std::invalid_argument{
            "Dynamic entity scene requires both a package and a frame"};
    }

    statistics_.last_clear_color = scene.clear_color;
    statistics_.camera_valid = is_valid(scene.camera);
    statistics_.last_camera = scene.camera;
    statistics_.static_world_present =
        scene.static_world.has_value() && scene.static_world->package != nullptr;
    if (statistics_.static_world_present) {
        statistics_.package_resource_id = scene.static_world->package->resource_id();
        statistics_.package_revision = scene.static_world->package->resource_revision();
    } else {
        statistics_.package_resource_id.reset();
        statistics_.package_revision.reset();
    }
    statistics_.scene_package_present = scene.static_world.has_value() &&
        scene.static_world->scene_package != nullptr;
    if (statistics_.scene_package_present) {
        statistics_.scene_resource_id =
            scene.static_world->scene_package->resource_id();
        statistics_.scene_revision =
            scene.static_world->scene_package->resource_revision();
    } else {
        statistics_.scene_resource_id.reset();
        statistics_.scene_revision.reset();
    }
    statistics_.visible_draw_list_present = scene.static_world.has_value() &&
        scene.static_world->visible_draw_list != nullptr;
    statistics_.visibility_present = scene.static_world.has_value() &&
        scene.static_world->visibility_summary.has_value();
    if (statistics_.visibility_present) {
        statistics_.visibility_revision =
            scene.static_world->visibility_summary->revision;
        statistics_.visible_world_surface_count =
            scene.static_world->visibility_summary
                ->visible_world_surface_count;
        statistics_.visible_brush_instance_count =
            scene.static_world->visibility_summary
                ->visible_brush_instance_count;
    } else {
        statistics_.visibility_revision.reset();
        statistics_.visible_world_surface_count = 0U;
        statistics_.visible_brush_instance_count = 0U;
    }
    statistics_.visible_draw_command_count =
        statistics_.visible_draw_list_present
        ? scene.static_world->visible_draw_list->statistics().command_count
        : 0U;
    statistics_.dynamic_entity_package_present =
        scene.dynamic_entities.has_value();
    if (statistics_.dynamic_entity_package_present) {
        const auto expected_identity =
            entity_render::EntityRenderResourceIdentity{
                scene.dynamic_entities->package->resource_id(),
                scene.dynamic_entities->package->resource_revision()};
        if (scene.dynamic_entities->frame->scene_package_identity() !=
            expected_identity) {
            throw std::invalid_argument{
                "Dynamic entity frame does not belong to its scene package"};
        }
        const auto world_association =
            scene.dynamic_entities->package->world_scene_association();
        if (world_association &&
            (!scene.static_world || !scene.static_world->scene_package ||
                world_association->resource_id !=
                    scene.static_world->scene_package->resource_id() ||
                world_association->revision !=
                    scene.static_world->scene_package->resource_revision())) {
            throw std::invalid_argument{
                "Dynamic entity scene does not match its associated world scene"};
        }
        statistics_.entity_scene_resource_id =
            scene.dynamic_entities->package->resource_id();
        statistics_.entity_scene_revision =
            scene.dynamic_entities->package->resource_revision();
        statistics_.entity_frame_revision =
            scene.dynamic_entities->frame->resource_revision();
        statistics_.studio_entity_instance_count =
            scene.dynamic_entities->frame->studio_instances().size();
        statistics_.sprite_entity_instance_count =
            scene.dynamic_entities->frame->sprite_instances().size();
        statistics_.visible_entity_count =
            scene.dynamic_entities->visibility_summary.visible_count;
        statistics_.unsupported_entity_count =
            scene.dynamic_entities->visibility_summary
                .unsupported_instance_count;
    } else {
        statistics_.entity_scene_resource_id.reset();
        statistics_.entity_scene_revision.reset();
        statistics_.entity_frame_revision.reset();
        statistics_.studio_entity_instance_count = 0U;
        statistics_.sprite_entity_instance_count = 0U;
        statistics_.visible_entity_count = 0U;
        statistics_.unsupported_entity_count = 0U;
    }
    statistics_.last_extent = extent;
    ++statistics_.rendered_frames;
}

void NullRenderer::initialize() noexcept
{
    statistics_.initialized = true;
    statistics_.shutdown = false;
}

void NullRenderer::shutdown() noexcept
{
    statistics_.initialized = false;
    statistics_.shutdown = true;
}

NullRendererStatistics NullRenderer::statistics() const noexcept
{
    return statistics_;
}

} // namespace hlclient::renderer::null

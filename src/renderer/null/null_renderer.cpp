#include <hlclient/renderer/null/null_renderer.hpp>

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

    statistics_.last_clear_color = scene.clear_color;
    statistics_.camera_valid = is_valid(scene.camera);
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

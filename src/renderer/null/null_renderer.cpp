#include <hlclient/renderer/null/null_renderer.hpp>

#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/world_render/world_render_types.hpp>

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

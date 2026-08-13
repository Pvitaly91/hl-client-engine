#include <hlclient/renderer/null/null_renderer.hpp>

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

    statistics_.last_scene = scene;
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

#pragma once

#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/renderer/render_camera_math.hpp>
#include <hlclient/renderer/render_scene.hpp>

#include <memory>

namespace hlclient::renderer::opengl::detail {

class OpenGlEntityRendererBackend final {
public:
    OpenGlEntityRendererBackend();
    ~OpenGlEntityRendererBackend() noexcept;

    OpenGlEntityRendererBackend(const OpenGlEntityRendererBackend&) = delete;
    OpenGlEntityRendererBackend& operator=(const OpenGlEntityRendererBackend&) =
        delete;
    OpenGlEntityRendererBackend(OpenGlEntityRendererBackend&&) = delete;
    OpenGlEntityRendererBackend& operator=(OpenGlEntityRendererBackend&&) =
        delete;

    void render(
        const RenderDynamicEntities& entities,
        const RenderCamera& camera,
        const RenderMatrix4& view_projection);
    void release_resources() noexcept;
    [[nodiscard]] const OpenGlEntityRendererStatistics& statistics()
        const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace hlclient::renderer::opengl::detail

#pragma once

#include <hlclient/renderer/renderer.hpp>

namespace hlclient::renderer::opengl {

class OpenGlRenderer final : public IRenderer {
public:
    // An OpenGL 3.3 Core context must be current on the calling thread.
    OpenGlRenderer();
    ~OpenGlRenderer() noexcept override;

    OpenGlRenderer(const OpenGlRenderer&) = delete;
    OpenGlRenderer& operator=(const OpenGlRenderer&) = delete;
    OpenGlRenderer(OpenGlRenderer&&) = delete;
    OpenGlRenderer& operator=(OpenGlRenderer&&) = delete;

    [[nodiscard]] const RendererInfo& information() const noexcept override;
    void render(const RenderScene& scene, RenderExtent extent) override;

private:
    RendererInfo information_;
    bool loader_initialized_{false};
};

} // namespace hlclient::renderer::opengl

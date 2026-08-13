#pragma once

#include <hlclient/renderer/render_scene.hpp>

#include <string>

namespace hlclient::renderer {

struct RendererInfo {
    std::string vendor;
    std::string device;
    std::string version;
};

class IRenderer {
public:
    virtual ~IRenderer();

    [[nodiscard]] virtual const RendererInfo& information() const noexcept = 0;
    virtual void render(const RenderScene& scene, RenderExtent extent) = 0;

protected:
    IRenderer() = default;
    IRenderer(const IRenderer&) = default;
    IRenderer& operator=(const IRenderer&) = default;
    IRenderer(IRenderer&&) noexcept = default;
    IRenderer& operator=(IRenderer&&) noexcept = default;
};

} // namespace hlclient::renderer

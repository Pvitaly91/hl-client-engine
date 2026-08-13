#pragma once

#include <hlclient/client/client_world_state.hpp>
#include <hlclient/renderer/render_scene.hpp>

#include <chrono>
#include <string>

namespace hlclient::client {

using FrameTime = std::chrono::duration<double>;

struct SceneUpdateResult {
    bool succeeded{true};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return succeeded;
    }
};

class IClientSceneSource {
public:
    virtual ~IClientSceneSource();

    [[nodiscard]] virtual SceneUpdateResult update(FrameTime elapsed) = 0;
    [[nodiscard]] virtual const ClientWorldState& world_state() const noexcept = 0;

protected:
    IClientSceneSource() = default;
    IClientSceneSource(const IClientSceneSource&) = default;
    IClientSceneSource& operator=(const IClientSceneSource&) = default;
    IClientSceneSource(IClientSceneSource&&) noexcept = default;
    IClientSceneSource& operator=(IClientSceneSource&&) noexcept = default;
};

[[nodiscard]] renderer::RenderScene build_render_scene(const ClientWorldState& world_state) noexcept;

} // namespace hlclient::client

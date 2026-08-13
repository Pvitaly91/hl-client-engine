#include <hlclient/client/client_scene_source.hpp>

#include <hlclient/client/client_world_state.hpp>

namespace hlclient::client {

IClientSceneSource::~IClientSceneSource() = default;

renderer::RenderScene build_render_scene(const ClientWorldState& world_state) noexcept
{
    renderer::RenderScene scene;
    if (world_state.connection_requested()) {
        scene.clear_color.blue = 0.14F;
    }
    return scene;
}

} // namespace hlclient::client

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
    const auto& camera = world_state.camera();
    scene.camera = renderer::RenderCamera{
        camera.position,
        camera.target,
        camera.up,
        camera.vertical_field_of_view_radians,
        camera.near_plane,
        camera.far_plane,
    };
    if (world_state.static_world()) {
        scene.static_world.emplace(renderer::RenderStaticWorld{
            world_state.static_world(),
            world_state.preview_render_options().cull_mode == PreviewWorldCullMode::back
                ? renderer::RenderCullMode::back
                : renderer::RenderCullMode::none,
            renderer::RenderBaselineLightStylePolicy::source_slot_zero,
        });
    }
    return scene;
}

} // namespace hlclient::client

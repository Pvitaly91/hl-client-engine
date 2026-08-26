#include <hlclient/client/client_scene_source.hpp>

#include <hlclient/client/client_world_state.hpp>

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/world_visibility/world_visibility_types.hpp>

#include <utility>

namespace hlclient::client {

IClientSceneSource::~IClientSceneSource() = default;

SceneUpdateResult IClientSceneSource::set_render_extent(
    const renderer::RenderExtent)
{
    return {};
}

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
        renderer::RenderStaticWorld static_world{
            world_state.static_world(),
            world_state.preview_render_options().cull_mode == PreviewWorldCullMode::back
                ? renderer::RenderCullMode::back
                : renderer::RenderCullMode::none,
            renderer::RenderBaselineLightStylePolicy::source_slot_zero,
        };
        static_world.scene_package = world_state.world_scene();
        static_world.visible_draw_list = world_state.visible_draw_list();
        if (world_state.world_visibility() && static_world.visible_draw_list) {
            const auto scene_identity =
                world_state.world_visibility()->scene_identity();
            const auto result_signature =
                world_state.world_visibility()->result_signature();
            static_world.visibility_summary =
                renderer::RenderStaticWorldVisibilitySummary{
                    world_state.visibility_revision(),
                    scene_identity.resource_id,
                    scene_identity.revision,
                    scene_identity.visibility_input_signature,
                    scene_identity.draw_input_signature,
                    result_signature.first,
                    result_signature.second,
                    world_state.world_visibility()
                        ->visible_world_surface_indices()
                        .size(),
                    world_state.world_visibility()
                        ->visible_brush_instance_indices()
                        .size(),
                };
        }
        scene.static_world.emplace(std::move(static_world));
    }
    if (world_state.entity_scene() && world_state.entity_frame()) {
        const auto& statistics = world_state.entity_frame()->statistics();
        scene.dynamic_entities.emplace(renderer::RenderDynamicEntities{
            world_state.entity_scene(),
            world_state.entity_frame(),
            renderer::RenderDynamicEntityVisibilitySummary{
                statistics.candidate_count,
                statistics.visible_count,
                statistics.studio_instance_count,
                statistics.sprite_instance_count,
                statistics.unsupported_instance_count,
            },
        });
    }
    return scene;
}

} // namespace hlclient::client

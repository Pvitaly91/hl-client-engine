#include <hlclient/app/hud_presentation.hpp>
#include <algorithm>
namespace hlclient::app {
HudMaterializationStatistics materialize_hud_sprites(renderer::RenderScene& scene,
    const goldsrc::HudPreparedAssets& resources) {
    HudMaterializationStatistics out;
    scene.hud_sprite_bindings.clear();
    if(!scene.basic_hud)return out;
    if(!game_api::valid_hud_draw_commands(*scene.basic_hud)) {out.invalid_commands=1;return out;}
    for(const auto& command:scene.basic_hud->sprites) {
        if(std::any_of(scene.hud_sprite_bindings.begin(),scene.hud_sprite_bindings.end(),
            [&](const auto& binding){return binding.resource_id==command.resource_id &&
                binding.resource_revision==command.resource_revision;}))continue;
        const auto resource=std::find_if(resources.sprites.begin(),resources.sprites.end(),
            [&](const auto& sprite){return sprite.resource_id==command.resource_id &&
                sprite.resource_revision==command.resource_revision;});
        if(resource==resources.sprites.end() || !resource->asset) {
            if(std::any_of(resources.sprites.begin(),resources.sprites.end(),
                [&](const auto& sprite){return sprite.resource_id==command.resource_id;}))++out.stale_revisions;
            else ++out.missing_resources;
            continue;
        }
        if(scene.hud_sprite_bindings.size()==renderer::kMaximumHudSpriteBindings) {++out.capacity_rejections;continue;}
        scene.hud_sprite_bindings.push_back({resource->resource_id,resource->resource_revision,resource->asset});
        ++out.bindings_created;
    }
    return out;
}
} // namespace hlclient::app

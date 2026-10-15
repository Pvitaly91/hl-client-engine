#pragma once
#include <hlclient/goldsrc/hud_assets.hpp>
#include <hlclient/renderer/render_scene.hpp>
namespace hlclient::app {
struct HudMaterializationStatistics final {
    std::size_t bindings_created{}, missing_resources{}, stale_revisions{}, capacity_rejections{}, invalid_commands{};
};
// Resolve owning approved immutable handles only. No role, weapon or layout
// policy, filesystem reads or native texture pointers live in this mechanism.
[[nodiscard]] HudMaterializationStatistics materialize_hud_sprites(
    renderer::RenderScene&, const goldsrc::HudPreparedAssets&);
} // namespace hlclient::app

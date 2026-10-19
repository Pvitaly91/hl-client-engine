#pragma once
#include <hlclient/core/presentation_settings.hpp>
#include <hlclient/renderer/screen_ui.hpp>

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/skybox_asset.hpp>
#include <hlclient/assets/world_texture_types.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/local_visuals.hpp>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <algorithm>

namespace hlclient::world_render {
class WorldRenderPackage;
}

namespace hlclient::world_scene_render {
class WorldSceneRenderPackage;
struct RuntimeBrushRenderFrame;
}

namespace hlclient::world_visibility {
class WorldVisibleDrawList;
}

namespace hlclient::entity_render {
class EntitySceneRenderPackage;
class EntityRenderFrame;
class SpriteRenderAsset;
}

namespace hlclient::renderer {

struct ClearColor {
    float red{0.035F};
    float green{0.055F};
    float blue{0.085F};
    float alpha{1.0F};

    [[nodiscard]] friend bool operator==(const ClearColor&, const ClearColor&) = default;
};

// Column-major, column-vector transform. Kept in the owning scene boundary so
// immutable decal placements can follow a brush without importing camera APIs.
struct RenderMatrix4 {
    std::array<float,16U> values{
        1.0F,0.0F,0.0F,0.0F, 0.0F,1.0F,0.0F,0.0F,
        0.0F,0.0F,1.0F,0.0F, 0.0F,0.0F,0.0F,1.0F};
    [[nodiscard]] friend bool operator==(const RenderMatrix4&,const RenderMatrix4&)=default;
};

struct RenderCamera {
    assets::AssetVector3 position{0.0F, -1.0F, 0.0F};
    assets::AssetVector3 target{0.0F, 0.0F, 0.0F};
    assets::AssetVector3 up{0.0F, 0.0F, 1.0F};
    float vertical_field_of_view_radians{1.0471975512F};
    float near_plane{0.1F};
    float far_plane{4'096.0F};

    [[nodiscard]] friend constexpr bool operator==(
        const RenderCamera& left,
        const RenderCamera& right) noexcept
    {
        return left.position.x == right.position.x &&
            left.position.y == right.position.y &&
            left.position.z == right.position.z &&
            left.target.x == right.target.x &&
            left.target.y == right.target.y &&
            left.target.z == right.target.z &&
            left.up.x == right.up.x && left.up.y == right.up.y &&
            left.up.z == right.up.z &&
            left.vertical_field_of_view_radians ==
                right.vertical_field_of_view_radians &&
            left.near_plane == right.near_plane &&
            left.far_plane == right.far_plane;
    }
};

enum class RenderCullMode {
    none,
    back,
};

enum class RenderBaselineLightStylePolicy {
    source_slot_zero,
};

struct RenderStaticWorldVisibilitySummary {
    std::uint64_t revision{0U};
    std::uint64_t scene_resource_id{0U};
    std::uint64_t scene_resource_revision{0U};
    std::uint64_t visibility_input_signature{0U};
    std::uint64_t draw_input_signature{0U};
    std::uint64_t result_signature_first{0U};
    std::uint64_t result_signature_second{0U};
    std::size_t visible_world_surface_count{0U};
    std::size_t visible_brush_instance_count{0U};
};

struct RenderStaticWorld {
    std::shared_ptr<const world_render::WorldRenderPackage> package;
    RenderCullMode cull_mode{RenderCullMode::none};
    RenderBaselineLightStylePolicy light_style_policy{
        RenderBaselineLightStylePolicy::source_slot_zero};
    std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>
        scene_package;
    std::shared_ptr<const world_visibility::WorldVisibleDrawList>
        visible_draw_list;
    std::optional<RenderStaticWorldVisibilitySummary> visibility_summary;
    std::shared_ptr<const world_scene_render::RuntimeBrushRenderFrame> runtime_brushes;
};

struct RenderDynamicEntityVisibilitySummary {
    std::size_t candidate_count{0U};
    std::size_t visible_count{0U};
    std::size_t studio_instance_count{0U};
    std::size_t sprite_instance_count{0U};
    std::size_t unsupported_instance_count{0U};
};

struct RenderDynamicEntities {
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package;
    std::shared_ptr<const entity_render::EntityRenderFrame> frame;
    RenderDynamicEntityVisibilitySummary visibility_summary{};
};

// Viewmodel-local, pose-attached short-lived neutral effect. No game rule or
// action is interpreted by the renderer. Drawn after first-person geometry,
// before HUD, with first-person depth still enabled.
struct RenderMuzzleFlash {
    assets::AssetVector3 center{};
    float radius_units{};
    std::array<float, 4> color{};
};

// One bounded, renderer-neutral transient point light. World and first-person
// passes use separate centers because their projection spaces are distinct.
struct RenderPointLight {
    assets::AssetVector3 center{};
    float radius_units{};
    float intensity{};
    std::array<float, 3> color{};
};

struct RenderDecalVertex {
    assets::AssetVector3 position{};
    assets::AssetVector2 uv{};
    std::array<float,3> surface_light_rgb{1,1,1};
};
struct RenderWorldDecals {
    // Immutable map-scoped texture and revisioned clipped world triangles.
    std::shared_ptr<const assets::WorldTextureAsset> texture;
    std::shared_ptr<const std::vector<RenderDecalVertex>> vertices;
    std::uint64_t revision{};
    game_api::LocalDecalMaterialMode material_mode{game_api::LocalDecalMaterialMode::straight_alpha};
    RenderMatrix4 model_transform;
};

[[nodiscard]] inline bool valid_render_point_light(const RenderPointLight& light) noexcept {
    if (!std::isfinite(light.center.x) || !std::isfinite(light.center.y) ||
        !std::isfinite(light.center.z) || !std::isfinite(light.radius_units) ||
        !std::isfinite(light.intensity) || light.radius_units<=0.0F ||
        light.radius_units>4096.0F || light.intensity<0.0F ||
        light.intensity>4.0F) return false;
    for (const auto channel:light.color)
        if (!std::isfinite(channel) || channel<0.0F || channel>4.0F) return false;
    return true;
}

using RenderBasicHud = game_api::HudDrawCommands;
inline constexpr std::size_t kMaximumHudSpriteBindings = 128U;
inline constexpr std::size_t kMaximumHudSpriteTextureBytes = 64U * 1024U * 1024U;
struct RenderHudSpriteBinding {
    std::uint64_t resource_id{}, resource_revision{};
    std::shared_ptr<const entity_render::SpriteRenderAsset> asset;
};
struct RenderWorldParticle {
    assets::AssetVector3 center{};
    float radius_units{};
    std::array<float,4> color{};
    bool emissive{};
    friend bool operator==(const RenderWorldParticle& a,const RenderWorldParticle& b) noexcept {
        return a.center.x==b.center.x && a.center.y==b.center.y && a.center.z==b.center.z &&
            a.radius_units==b.radius_units && a.color==b.color && a.emissive==b.emissive;
    }
};
struct RenderWorldLine {
    assets::AssetVector3 start{}, end{};
    float width_units{};
    std::array<float,4> color{};
    float head_taper_fraction{}; // [0,1]; flat taper leads, solid taper trails
    game_api::WorldLineGeometry geometry{game_api::WorldLineGeometry::flat_billboard};
    float taper_end_diameter_fraction{}; // solid taper only, [0,1]; zero retains its point
    float emission_gain{1.0F}; // solid radiance [0,4]; default preserves existing lines
};

struct RenderScene {
    ClearColor clear_color{};
    RenderCamera camera{};
    // Neutral, frame-owned presentation offsets/overlay. The renderer never
    // writes them back to gameplay camera, listener or authoritative aim.
    game_api::ScreenEffectsFrame screen_effects{};
    std::optional<RenderStaticWorld> static_world;
    std::shared_ptr<const assets::SkyboxAsset> skybox;
    std::optional<RenderDynamicEntities> dynamic_entities;
    // Camera-bound Studio frame. Drawn after the world with its own depth
    // buffer contents; it never enters world entity visibility/PVS.
    std::optional<RenderDynamicEntities> first_person_entities;
    // Imported additive presentation sprites, retaining each pass's depth.
    std::optional<RenderDynamicEntities> first_person_muzzle_entities;
    std::optional<RenderDynamicEntities> world_muzzle_entities;
    std::optional<RenderDynamicEntities> transient_world_entities;
    // Animated server temporary sprites, independent of shell/model ownership.
    std::optional<RenderDynamicEntities> transient_sprite_entities;
    std::optional<RenderDynamicEntities> follow_trail_entities;
    std::optional<RenderMuzzleFlash> first_person_flash;
    // World-space billboards share the flash mechanism, before first-person
    // depth reset. Fixed capacity is independent of the number of players.
    std::array<RenderMuzzleFlash,32> world_flashes{};
    std::size_t world_flash_count{};
    std::array<RenderWorldParticle,256U> world_particles{};
    std::size_t world_particle_count{};
    std::array<RenderWorldLine,64U> world_lines{};
    std::size_t world_line_count{};
    std::optional<RenderPointLight> transient_world_light;
    // Additional presentation lights never displace the existing muzzle light.
    // World/brush and Studio passes consume this fixed, frame-owned batch.
    std::array<RenderPointLight,8U> world_effect_lights{};
    std::size_t world_effect_light_count{};
    [[nodiscard]] std::span<const RenderPointLight> effect_lights() const noexcept {
        return {world_effect_lights.data(),std::min(world_effect_light_count,world_effect_lights.size())};
    }
    std::optional<RenderPointLight> transient_first_person_light;
    std::optional<RenderWorldDecals> world_decals;
    // Second bounded neutral material batch; game semantics stay upstream.
    std::optional<RenderWorldDecals> secondary_world_decals;
    // Additional materials/attachments from the same bounded map decal owner.
    // Renderers reject overflow instead of silently growing a second system.
    std::vector<RenderWorldDecals> additional_world_decals;
    std::optional<RenderBasicHud> basic_hud;
    std::vector<RenderHudSpriteBinding> hud_sprite_bindings;
    // Application overlay is independent of world/player/HUD availability and
    // is always composited after image adjustments and the gameplay HUD.
    std::optional<ScreenUiFrame> screen_ui;
    core::ImageAdjustments image_adjustments;
    // Scene geometry only. HUD is composited after native-resolution resolve.
    // Zero preserves the baseline renderer for fixtures/alternate modules.
    unsigned requested_msaa_samples{};
};

struct RenderExtent {
    int width{0};
    int height{0};

    [[nodiscard]] friend bool operator==(const RenderExtent&, const RenderExtent&) = default;
};

} // namespace hlclient::renderer

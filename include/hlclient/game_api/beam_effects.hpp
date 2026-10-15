#pragma once
#include <hlclient/game_api/local_visuals.hpp>
#include <span>
#include <cmath>
namespace hlclient::game_api {
// Optional additive halo around the unchanged textured-ribbon centerline.
// Defaults preserve all existing beams; no separate effect lifetime is owned.
struct BeamRibbonGlowProfile {
    float width_scale{1}, opacity{};
};
inline bool valid_beam_ribbon_glow(BeamRibbonGlowProfile value) noexcept {
    return std::isfinite(value.width_scale) && value.width_scale>=1 && value.width_scale<=8 &&
        std::isfinite(value.opacity) && value.opacity>=0 && value.opacity<=1;
}
// Optional illumination of the environment, independent of the visible halo.
// Only the selected logical strand carries it. Generic application sampling
// shares the existing effect-light budget and owns no additional lifetime.
struct BeamSceneLightProfile {
    float radius_units{}, intensity{};
    float spacing_units{192};
    // Scene emission may have its own color/power; ribbon tint and opacity
    // remain presentation values. A fully transparent ribbon never emits.
    std::optional<std::array<float,3>> color_override;
    bool inherit_beam_opacity{true};
};
inline bool valid_beam_scene_light(BeamSceneLightProfile value) noexcept {
    return std::isfinite(value.radius_units) && value.radius_units>=0 && value.radius_units<=512 &&
        std::isfinite(value.intensity) && value.intensity>=0 && value.intensity<=1 &&
        std::isfinite(value.spacing_units) && value.spacing_units>=32 && value.spacing_units<=512 &&
        (!value.color_override || (std::isfinite((*value.color_override)[0]) &&
            std::isfinite((*value.color_override)[1]) && std::isfinite((*value.color_override)[2]) &&
            (*value.color_override)[0]>=0 && (*value.color_override)[0]<=1 &&
            (*value.color_override)[1]>=0 && (*value.color_override)[1]<=1 &&
            (*value.color_override)[2]>=0 && (*value.color_override)[2]<=1)) &&
        ((value.radius_units>0)==(value.intensity>0));
}
// Owning optional presentation profile. Resource names are borrowed only while
// selecting it; the host retains no module or native asset handles in a trail.
struct EntityTrailProfile {
    float spacing_units{}, initial_diameter{}, final_diameter{};
    float lifetime_seconds{}, rise_units_per_second{}, head_glow_radius{};
    std::array<float,4> head_glow_color{1,1,1,1};
    // Optional exact manifest sprite, never an OS path or a synthetic server slot.
    // Offset is along the presented model's local +X axis, in world units.
    std::array<char,64> head_sprite{};
    float head_axis_offset{}, smoke_opacity{.7F};
};
// Owning identity and local geometry for a surface on one committed inline BSP
// lifetime. Resolving it later requires that exact current target; no borrowed
// collision/render object or network authority is retained.
struct EffectSurfaceAttachment {
    std::uint64_t generation{},entity_lifetime{};
    std::uint32_t entity{},model_slot{},source_model_index{};
    assets::AssetVector3 local_point{},local_normal{};
};
// Borrowed synchronous point query against world and committed solid inline BSP
// surfaces. No players/entity boxes, or retained world/engine handles.
struct StaticEffectTrace {
    bool valid{},start_solid{},all_solid{},hit{},unsupported{};
    assets::AssetVector3 point{},normal{};
    std::optional<EffectSurfaceAttachment> surface_attachment;
};
// Synchronous neutral point query against the committed prediction solids.
// The module selects geometry; only a provisional presentation may use it.
struct PlacementTraceRequest {
    assets::AssetVector3 start{}, end{};
    std::uint64_t generation{};
    std::optional<std::uint32_t> ignore_entity;
    std::optional<std::uint32_t> server_box_solid_code;
    std::optional<std::uint32_t> player_solid_code;
};
struct PlacementTraceResult {
    bool valid{}, start_solid{}, all_solid{};
    double fraction{1.0};
    std::optional<std::uint32_t> hit_entity;
    assets::AssetVector3 point{}, normal{};
};
class IStaticEffectTrace {
public: virtual ~IStaticEffectTrace()=default;
    virtual StaticEffectTrace trace(assets::AssetVector3,assets::AssetVector3) const noexcept=0;
    virtual PlacementTraceResult placement_trace(const PlacementTraceRequest&) const noexcept { return {}; }
};
struct BeamSpriteFrame {
    LocalWeaponActionIdentity action;
    std::array<char,64> virtual_sprite{};
    assets::AssetVector3 start{},end{},velocity{};
    std::array<float,3> color{1,1,1};
    float width{1},opacity{1},scale{1};bool ribbon{true};
    double starts{},expires{};
    // Current-state persistent presentation, not a network entity or TTL queue.
    bool persistent{};
    float noise_amplitude{},scroll_cycles_per_second{};
    bool sine_noise{};
    double phase_seconds{};
    BeamRibbonGlowProfile ribbon_glow{};
    BeamSceneLightProfile scene_light{};
    std::optional<std::uint32_t> server_entity, sprite_model_slot;
    std::uint32_t sprite_frame{};
    // Owning, renderer-neutral profile. The host samples motion from immutable
    // endpoints and absolute age; legacy ribbons retain the flat/default path.
    WorldLineGeometry geometry{WorldLineGeometry::flat_billboard};
    float travel_speed_units_per_second{},segment_length_units{};
    float head_taper_fraction{},taper_end_diameter_fraction{},emission_gain{1};
    TransientGlowProfile head_glow{};
    // Present only for stationary surface-attached sprites. Moving particles
    // remain billboards and never retain a borrowed collision-world pointer.
    std::optional<assets::AssetVector3> surface_normal;
    // Optional first materialization clock for moving geometry. Causal action
    // starts/expires stay unchanged; a short nearby segment is visible once
    // before advancing, as with the existing captured-muzzle streak.
    std::optional<double> motion_starts;
    std::optional<EffectSurfaceAttachment> surface_attachment;
};
struct BeamEffectsFrame {
    std::array<BeamSpriteFrame,128> sprites{};std::size_t count{};
    std::array<LocalWorldImpactRequest,32> impacts{};std::size_t impact_count{};
    std::uint64_t shots{},reflections{},penetrations{},rejected{},capacity{};
    std::uint32_t active_emitters{};
    std::uint64_t audio_start_requests{},audio_replace_requests{},audio_stop_requests{};
    bool server_handoff{};
};
}

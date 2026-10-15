#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/audio.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>

namespace hlclient::game_api {

// Borrowed, narrow presentation view. Coordinates are source-native Z-up.
// No gameplay authority or engine handles cross this boundary.
struct LocalVisualContext final {
    assets::AssetVector3 eye{}, forward{}, right{}, up{0, 0, 1}, velocity{};
    double now_seconds{};
};

// Compiled game-selected additive sprite dependency; no network slot or path.
// Scale multiplies original sprite frame extents, not a guessed quad radius.
struct MuzzleSpriteProfile final {
    std::array<char, 48> virtual_name{};
    float scale{1.0F};
    std::uint32_t frame_seed{};
};

struct LocalMuzzleFlash final {
    LocalWeaponActionIdentity action;
    std::uint32_t model_index{};
    std::uint32_t sequence{};
    std::uint32_t marker_ordinal{};
    std::uint32_t attachment_index{};
    std::uint32_t variant{};
    float radius_units{6.0F};
    std::array<float, 4> color{1.0F, 0.72F, 0.28F, 0.85F};
    double starts_at_seconds{}, ends_at_seconds{};
    std::optional<MuzzleSpriteProfile> sprite;
};

struct LocalMuzzleLight final {
    LocalWeaponActionIdentity action;
    float radius_units{96.0F};
    float intensity{0.8F};
    std::array<float,3> color{1.0F,0.62F,0.28F};
    double starts_at_seconds{}, ends_at_seconds{};
};

struct LocalShellEjection final {
    LocalWeaponActionIdentity action;
    // Fixed, project-owned local presentation asset token; never network text.
    std::array<char, 32> model_name{};
    assets::AssetVector3 origin{}, velocity{};
    float yaw_degrees{};
    double starts_at_seconds{}, expires_at_seconds{};
};

// One-shot, owning presentation request. Origin and direction are captured
// from a newly submitted command, never reconstructed from a later frame.
// Numeric, presentation-only glow. Zero disables it; no asset/game identity.
struct TransientGlowProfile final {
    float radius_units{}, light_radius_units{}, light_intensity{};
};
[[nodiscard]] inline bool valid_transient_glow(const TransientGlowProfile& p) noexcept {
    return std::isfinite(p.radius_units) && p.radius_units>=0 && p.radius_units<=16 &&
        std::isfinite(p.light_radius_units) && p.light_radius_units>=0 && p.light_radius_units<=256 &&
        std::isfinite(p.light_intensity) && p.light_intensity>=0 && p.light_intensity<=2 &&
        ((p.light_radius_units==0 && p.light_intensity==0) ||
         (p.light_radius_units>0 && p.light_intensity>0));
}
struct ImpactParticleProfile final {
    std::uint32_t count{};
    float lifetime_seconds{}, minimum_speed_units{}, maximum_speed_units{};
    float gravity_units{}, radius_units{};
    std::array<float,4> color{};
    float spread_scale{1.0F}; // scales the existing outward hemisphere's tangent components
    bool emissive{}; // additive soft sparks; false preserves alpha-blended dust
    TransientGlowProfile contact_glow{};
    float contact_glow_seconds{}; // one short flash/light per accepted burst, not per particle
    bool radial_distribution{}; // neutral spherical burst; false preserves surface hemisphere
    bool flicker{}; // deterministic absolute-age brightness, no new random/frame state
};
// Geometry only: motion, eligibility, identity and glow remain independent.
// The original billboard is retained as the neutral/default API behavior.
enum class WorldLineGeometry : std::uint8_t { flat_billboard, solid_3d };
[[nodiscard]] inline bool valid_world_line_geometry(WorldLineGeometry geometry) noexcept {
    return geometry==WorldLineGeometry::flat_billboard || geometry==WorldLineGeometry::solid_3d;
}
struct LocalWorldImpactRequest final {
    LocalWeaponActionIdentity action;
    assets::AssetVector3 origin{}, direction{};
    float maximum_distance_units{8192.0F};
    float decal_half_size_units{4.0F};
    double expires_at_seconds{};
    // Presentation-only delay after the immutable trace; zero for Glock.
    double decal_delay_seconds{};
    std::optional<ImpactParticleProfile> particles;
    struct Tracer final {
        assets::AssetVector3 origin{};
        float width_units{0.65F};
        std::array<float,4> color{1.0F,0.82F,0.45F,0.85F};
        double lifetime_seconds{0.06};
        // Stationary lines follow the pose; moving streaks capture it once at launch.
        // The trace/hit endpoint is immutable in both modes.
        bool follow_local_muzzle{};
        // Optional neutral moving streak, in world units/second and world units.
        // Both zero means a stationary line; otherwise both must be positive.
        // A moving local streak captures its first valid muzzle once, then flies freely.
        float travel_speed_units_per_second{}, segment_length_units{};
        // Fraction occupied by the taper; bounded to [0,1]. Zero preserves
        // rectangular flat lines. Flat tapers lead; solid tapers trail.
        float head_taper_fraction{};
        TransientGlowProfile head_glow{}; // follows the moving front, expires with the streak
        WorldLineGeometry geometry{WorldLineGeometry::flat_billboard};
        // Game-selected forward distance from the actual launch muzzle toward
        // the immutable hit. Chosen once per action, finite in [0,1024]. Zero
        // preserves the existing start; a hit at/before it has no visible line.
        float start_offset_units{};
        // Solid positive taper: terminal diameter / body diameter, in [0,1].
        // Zero retains the sharp cone. Positive adds an owning rounded rear
        // cap inside the original interval; flat/zero-taper shapes ignore it.
        float taper_end_diameter_fraction{};
    };
    std::optional<Tracer> tracer;
    bool suppress_surface_effects{}; // trace still supplies tracer endpoint
    bool suppress_decal{}; // valid solid-world trace/feedback, without a decal
};

enum class LocalWorldImpactOutcome : std::uint8_t {
    static_world_hit, miss, start_solid, unsupported_blocker, unavailable, invalid
};

// Owning renderer-neutral identity of the uniquely matched static BSP face.
// The name is canonical BSP metadata, not an asset path or game material kind.
struct LocalWorldSurfaceHit final {
    std::uint32_t source_surface_index{}, source_material_index{};
    std::array<char,32> texture_name{};
};
struct LocalWorldImpactDiagnostic final {
    LocalWeaponActionIdentity action;
    std::uint32_t source_surface_index{};
    std::array<char,13> normalized_texture_key{};
    std::array<char,16> material_label{};
    std::array<char,40> classification_source{};
    LocalSoundReference selected_sound;
    // Optional second sound from the same accepted hit; absent means no cue.
    std::optional<LocalSoundReference> supplemental_sound;
};

// Confirmed point-sweep contact; sample and eligibility remain game-owned.
struct LocalShellContact final {
    LocalWeaponActionIdentity action;
    std::uint32_t ordinal{};
    assets::AssetVector3 point{};
    float inward_normal_speed{};
    double at_seconds{};
};

enum class LocalDecalMaterialMode : std::uint8_t {
    straight_alpha, white_neutral_modulate, lit_straight_alpha
};

// Project-owned profile chosen by the game module, not server text.
struct LocalImpactAssetProfile final {
    std::array<char,32> wad_name{};
    std::array<char,16> texture_name{};
    LocalSoundReference sound;
    LocalSoundReference shell_contact_sound;
    // Bounded game-selected warm-up set; the engine only opens virtual tokens.
    std::array<LocalSoundReference,24> material_sounds{};
    std::size_t material_sound_count{};
    std::array<LocalSoundReference,5> supplemental_sounds{};
    std::size_t supplemental_sound_count{};
    LocalDecalMaterialMode material_mode{LocalDecalMaterialMode::straight_alpha};
};

struct LocalCrowbarImpactAssetProfile final {
    LocalImpactAssetProfile decal;
    std::array<LocalSoundReference,2U> strike_sounds;
    LocalSoundReference concrete_contact_sound;
};

struct LocalVisualStatistics final {
    std::uint64_t fire_actions_received{}, flash_scheduled{}, flash_expired{},
        light_requested{}, light_expired{},
        shells_scheduled{}, exact_duplicates_suppressed{}, late_cues_dropped{},
        unsupported_metadata{};
    std::uint64_t crowbar_actions{}, crowbar_duplicates{}, crowbar_requests{};
};

struct LocalVisualFrame final {
    std::optional<LocalMuzzleFlash> flash;
    std::optional<LocalMuzzleLight> light;
    std::optional<LocalShellEjection> shell;
    std::optional<LocalWorldImpactRequest> world_impact;
    LocalVisualStatistics statistics;
    // Per-action total bounds: two casings and eight independent traces.
    std::array<LocalShellEjection,1> extra_shells{};
    std::size_t extra_shell_count{};
    std::array<LocalWorldImpactRequest,7> extra_impacts{};
    std::size_t extra_impact_count{};
};

} // namespace hlclient::game_api

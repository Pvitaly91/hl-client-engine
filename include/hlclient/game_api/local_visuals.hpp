#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/audio.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hlclient::game_api {

// Borrowed, narrow presentation view. Coordinates are source-native Z-up.
// No gameplay authority or engine handles cross this boundary.
struct LocalVisualContext final {
    assets::AssetVector3 eye{}, forward{}, right{}, up{0, 0, 1}, velocity{};
    double now_seconds{};
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
struct LocalWorldImpactRequest final {
    LocalWeaponActionIdentity action;
    assets::AssetVector3 origin{}, direction{};
    float maximum_distance_units{8192.0F};
    float decal_half_size_units{4.0F};
    double expires_at_seconds{};
    // Presentation-only delay after the immutable trace; zero for Glock.
    double decal_delay_seconds{};
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
    straight_alpha, white_neutral_modulate
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
};

} // namespace hlclient::game_api
